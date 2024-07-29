/**
 * Copyright 2024, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE

#include <chef/platform.h>
#include <chef/containerv.h>
#include <dirent.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils.h"

#define __FD_CONTAINER 0
#define __FD_HOST      1

struct containerv_container_process {
    struct list_item list_header;
    pid_t            pid;
};

static struct containerv_container_process* containerv_container_process_new(pid_t processId)
{
    struct containerv_container_process* ptr = calloc(1, sizeof(struct containerv_container_process));
    if (ptr == NULL) { 
        return NULL;
    }
    ptr->pid = processId;
    return ptr;
}

static void containerv_container_process_delete(struct containerv_container_process* ptr)
{
    free(ptr);
}

struct containerv_container {
    pid_t       pid;
    char*       rootfs;
    char*       mountfs;
    int         status_fds[2];
    int         event_fd;
    struct list processes;
};

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    int                          status;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    // create the resources that we need immediately
    status = pipe(container->status_fds);
    if (status) {
        free(container);
        return NULL;
    }

    container->event_fd = -1;
    container->pid = -1;

    return container;
}

static void __container_delete(struct containerv_container* container)
{
    struct list_item* i;
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;

        containerv_container_process_delete(proc);
    }

    __close_safe(&container->event_fd);
    __close_safe(&container->status_fds[0]);
    __close_safe(&container->status_fds[1]);
    free(container->mountfs);
    free(container->rootfs);
    free(container);
}

enum containerv_event_type {
    CV_CONTAINER_UP,
    CV_CONTAINER_DOWN,
    CV_CONTAINER_SPAWN
};

struct containerv_event_spawn {
    int   status;
    pid_t process_id;
};

struct containerv_event {
    enum containerv_event_type type;
    union {
        int                           status;
        struct containerv_event_spawn spawn;
    } data;
};

struct containerv_command_spawn {
    enum container_spawn_flags flags;

    // lengths include zero terminator
    size_t path_length;
    size_t argument_length;
    size_t environment_length;
};

struct containerv_command_script {
    uint32_t length;
};

struct containerv_command_kill {
    pid_t process_id;
};

enum containerv_command_type {
    CV_COMMAND_SPAWN,
    CV_COMMAND_KILL,
    CV_COMMAND_SCRIPT,
    CV_COMMAND_DESTROY
};

struct containerv_command {
    enum containerv_command_type type;
    size_t                       length;
};

static char* __flatten_environment(const char* const* environment, size_t* lengthOut)
{
    char*  flatEnvironment;
    size_t flatLength = 1; // second nil
    int    i = 0, j = 0;

    while (environment[i]) {
        flatLength += strlen(environment[i]) + 1;
        i++;
    }

    flatEnvironment = calloc(flatLength, 1);
    if (flatEnvironment == NULL) {
        return NULL;
    }

    i = 0;
    while (environment[i]) {
        size_t len = strlen(environment[i]) + 1;
        memcpy(&flatEnvironment[j], environment[i], len);
        j += len;
        i++;
    }
    return flatEnvironment;
}

char** __unflatten_environment(const char* text)
{
	char** results;
	int    count = 1; // add zero terminator
	int    index = 0;

	if (text == NULL) {
		return NULL;
	}

	for (const char* p = text;; p++) {
		if (*p == '\0') {
			count++;
			
			if (*p == '\0' && *(p + 1) == '\0') {
			    break;
			}
		}
	}
	
	results = (char**)calloc(count, sizeof(char*));
	if (results == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	for (const char* p = text;; p++) {
		if (*p == '\0') {
			results[index] = (char*)malloc(p - text + 1);
			if (results[index] == NULL) {
			    // cleanup
				for (int i = 0; i < index; i++) {
					free(results[i]);
				}
				free(results);
				return NULL;
			}

			memcpy(results[index], text, p - text);
			results[index][p - text] = '\0';
			text = p + 1;
			index++;
			
			if (*p == '\0' && *(p + 1) == '\0') {
			    break;
			}
		}
	}
	return results;
}

static pid_t __exec(const char* path, const char* const* argv, const char* const* envv)
{
    pid_t processId;
    int   status;

    // fork a new child, all daemons/root programs are spawned from this code
    processId = fork();
    if (processId != 0) {
        return processId;
    }

    // switch user before exec

    status = execve(path, (char* const*)argv, (char* const*)envv);
    if (status) {
        // ehh log this probably
    }
    return 0;
}

static void __spawn_process(struct containerv_container* container, struct containerv_command_spawn* spawn)
{
    struct containerv_event event = {
        .type = CV_CONTAINER_SPAWN,
        .data.spawn.process_id = (pid_t)-1,
        .data.spawn.status = 0
    };
    char*  data;
    char*  path;
    char** argv = NULL;
    char** envv = NULL;
    pid_t  processId;

    // initialize the data pointer
    data = (char*)spawn + sizeof(struct containerv_command_spawn);

    // get the path
    path = data;
    data += spawn->path_length;

    // get the arguments if any
    if (spawn->argument_length) {
        argv = strargv(data, path, NULL);
        if (argv == NULL) {
            return;
        }
        data += spawn->argument_length;
    }

    if (spawn->environment_length) {
        envv = __unflatten_environment(data);
        if (envv == NULL) {
            return;
        }
        data += spawn->environment_length;
    }

    // perform the actual execution, only the primary process here actually returns
    processId = __exec(path, (const char* const*)argv, (const char* const*)envv);
    if (processId == (pid_t)-1) {
        // probably log this
    } else {
        struct containerv_container_process* proc = containerv_container_process_new(processId);
        if (proc) {
            list_add(&container->processes, &proc->list_header);
        }

        if (spawn->flags & CV_SPAWN_WAIT) {
            if (waitpid(processId, &event.data.spawn.status, 0) != processId) {
                // probably log this
            }
        }
    }
    event.data.spawn.process_id = processId;

    // cleanup resources temporarily allocated
    strargv_free(argv);
    strsplit_free(envv);

    // send event
    if (write(container->status_fds[__FD_CONTAINER], &event, sizeof(struct containerv_event)) != sizeof(struct containerv_event)) {
        // log this
    }
}

static void __kill_process(struct containerv_container* container, struct containerv_command_kill* cmd)
{
    struct list_item* i;

    list_foreach (&container->processes, i) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->pid == cmd->process_id) {
            kill(cmd->process_id, SIGTERM);
            list_remove(&container->processes, i);
            containerv_container_process_delete(proc);
            break;
        }
    }
}

static void __execute_script(struct containerv_container* container, const char* script)
{
    system(script);
}

static void __send_container_event(struct containerv_container* container, enum containerv_event_type type, int status)
{
    struct containerv_event event = {
        .type = type,
        .data.status = status
    };
    if (write(container->status_fds[__FD_CONTAINER], &event, sizeof(struct containerv_event)) != sizeof(struct containerv_event)) {
        // log this
    }
}

static void __destroy_container(struct containerv_container* container)
{
    struct list_item* i;

    // kill processes
    list_foreach (&container->processes, i) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        kill(proc->pid, SIGTERM);
    }

    // send event
    __send_container_event(container, CV_CONTAINER_DOWN, 0);

    // cleanup container
    __container_delete(container);
}

static int __container_command_handler(struct containerv_container* container, enum containerv_command_type type, void* command)
{
    switch (type) {
        case CV_COMMAND_SPAWN: {
            __spawn_process(container, (struct containerv_command_spawn*)command);
        } break;
        case CV_COMMAND_KILL: {
            __kill_process(container, (struct containerv_command_kill*)command);
        } break;
        case CV_COMMAND_SCRIPT: {
            __execute_script(container, command);
        } break;
        case CV_COMMAND_DESTROY: {
            __destroy_container(container);
            return 1;
        }
    }
    return 0;
}

static int __container_idle(struct containerv_container* container)
{
    struct containerv_command command;
    int                       result;

    // Drop capabilities that we no longer need
    result = containerv_drop_capabilities();
    if (result) {
        return result;
    }

    for (;;) {
        void* payload = NULL;

        result = (int)read(container->event_fd, &command, sizeof(struct containerv_command));
        if (result <= 0) {
            continue;
        }

        if (command.length > sizeof(struct containerv_command)) {
            size_t payloadLength = command.length - sizeof(struct containerv_command);
            payload = malloc(payloadLength);
            if (payload == NULL) {
                break;
            }

            result = (int)read(container->event_fd, payload, payloadLength);
            if (result <= 0) {
                continue;
            }
        }

        result = __container_command_handler(container, command.type, payload);
        free(payload);

        // result is non-zero once destroy command returns
        if (result != 0) {
            break;
        }
    }
    return 0;
}

static int __container_map_rootfs(struct containerv_container* container)
{
    DIR*           rootfs;
    struct dirent* entry;
    int            status = 0;
    char*          source;
    char*          destination;

    source      = malloc(PATH_MAX);
    destination = malloc(PATH_MAX);
    if (source == NULL || destination == NULL) {
        free(source);
        free(destination);
        return -1;
    }

    rootfs = opendir(container->rootfs);
    if (rootfs == NULL) {
        free(source);
        free(destination);
        return -1;
    }

    while ((entry = readdir(rootfs)) != NULL) {
        snprintf(source, PATH_MAX, "%s/%s", container->rootfs, entry->d_name);
        snprintf(destination, PATH_MAX, "%s/%s", container->mountfs, entry->d_name);

        status = mkdir(destination, 0755);
        if (status) {
            break;
        }

        status = mount(source, destination, NULL, MS_BIND | MS_RDONLY | MS_REC, NULL);
        if (status) {
            break;
        }
    }

    closedir(rootfs);
    free(source);
    free(destination);
    return status;
}

static int __container_map_procfs(
        struct containerv_container* container,
        enum containerv_capabilities capabilities)
{
    if (mkdir("/proc", 0555) && errno != EEXIST) {
        return -1;
    }

    if (mount("proc", "/proc", "proc", 0, NULL)) {
        return -1;
    }
    return 0;
}

static int __convert_cv_mount_flags(enum containerv_mount_flags cvFlags)
{
    int flags = 0;
    if (cvFlags & CV_MOUNT_BIND) {
        flags |= MS_BIND;
    }
    if (cvFlags & CV_MOUNT_RECURSIVE) {
        flags |= MS_REC;
    }
    if (cvFlags & CV_MOUNT_READONLY) {
        flags |= MS_RDONLY;
    }
    return flags;
}

// __container_map_mounts maps any outside paths into the container
static int __container_map_mounts(
        struct containerv_container* container,
        struct containerv_mount*     mounts,
        int                          mountsCount)
{
    char* destination;
    int   status = 0;

    if (!mountsCount) {
        return 0;
    }

    destination = malloc(PATH_MAX);
    if (destination == NULL) {
        return -1;
    }

    for (int i = 0; i < mountsCount; i++) {
        //snprintf(destination, PATH_MAX, "%s/%s", container->mountfs, mounts[i].destination);
        status = mount(
            mounts[i].source,
            //destination,
            mounts[i].destination,
            mounts[i].fstype,
            __convert_cv_mount_flags(mounts[i].flags),
            NULL
        );
        if (status) {
            break;
        }
    }
    return status;
}

static int __container_map_capabilities(
        struct containerv_container* container,
        enum containerv_capabilities capabilities)
{
    int status;

    status = __container_map_procfs(container, capabilities);
    if (status) {
        return -1;
    }
    return 0;
}

static int __container_remove_mountfs(const char* path)
{
    DIR*           rootfs;
    struct dirent* entry;
    int            status;
    char*          source;

    source = malloc(PATH_MAX);
    if (source == NULL) {
        return -1;
    }

    rootfs = opendir(path);
    if (rootfs == NULL) {
        free(source);
        return -1;
    }

    while ((entry = readdir(rootfs)) != NULL) {
        snprintf(source, PATH_MAX, "%s/%s", path, entry->d_name);
        status = umount(source);
        if (status) {
            break;
        }
    }

    closedir(rootfs);
    free(source);
    return platform_rmdir(path);
}

static int __container_run(
        struct containerv_container* container,
        enum containerv_capabilities capabilities,
        struct containerv_mount*     mounts,
        int                          mountsCount)
{
    struct containerv_event event;
    int                     status;
    int                     flags = 0;

    if (capabilities & CV_CAP_FILESYSTEM) {
        flags |= CLONE_NEWNS;
    }

    if (capabilities & CV_CAP_NETWORK) {
        flags |= CLONE_NEWNET | CLONE_NEWUTS;
    }

    if (capabilities & CV_CAP_PROCESS_CONTROL) {
        flags |= CLONE_NEWPID;
    }

    if (capabilities & CV_CAP_IPC) {
        flags |= CLONE_NEWIPC;
    }

    if (capabilities & CV_CAP_CGROUPS) {
        flags |= CLONE_NEWCGROUP;
    }

    // change the working directory so we don't accidentally lock any paths
    status = chdir("/");
    if (status) {
        return status;
    }

    status = unshare(flags | SIGCHLD);
    if (status) {
        return status;
    }

    // MS_PRIVATE makes the bind mount invisible outside of the namespace
    // MS_REC makes the mount recursive
    status = mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    if (status) {
        return status;
    }
  
    // After the unshare we are now running in separate namespaces, this means
    // we can start doing mount operations that still require the host file system
    // before we chroot
    if (capabilities & CV_CAP_FILESYSTEM) {
        // bind mount all additional mounts requested by the caller
        status = __container_map_mounts(container, mounts, mountsCount);
        if (status) {
            return status;
        }
    }

    // change root to the containers base path
    status = chroot(container->mountfs);
    if (status) {
        return status;
    }

    // after the chroot we can start setting up the unique bind-mounts
    status = __container_map_capabilities(container, capabilities);
    if (status) {
        return status;
    }
    __send_container_event(container, CV_CONTAINER_UP, 0);
    return __container_idle(container);
}

static int __container_entry(
        struct containerv_container* container,
        enum containerv_capabilities capabilities,
        struct containerv_mount*     mounts,
        int                          mountsCount)
{
    int status;

    // lets not leak the host fd
    status = __close_safe(&container->status_fds[__FD_HOST]);
    if (status) {
        // log this
    }

    // This is the primary run function, it initializes the container
    status = __container_run(container, capabilities, mounts, mountsCount);
    if (status) {
        __send_container_event(container, CV_CONTAINER_DOWN, status);
    }
    exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int __wait_for_container_event(struct containerv_container* container, struct containerv_event* event)
{
    int bytesRead;

    bytesRead = __INTSAFE_CALL(read(container->status_fds[__FD_HOST], event, sizeof(struct containerv_event)));
    if (bytesRead != sizeof(struct containerv_event)) {
        return -1;
    }
    return 0;
}

int containerv_create(
        const char*                   rootFs,
        enum containerv_capabilities  capabilities,
        struct containerv_mount*      mounts,
        int                           mountsCount,
        struct containerv_container** containerOut)
{
    struct containerv_container* container;
    struct containerv_event      event;
    pid_t                        pid;
    int                          status;

    container = __container_new();
    if (container == NULL) {
        return -1;
    }

    container->rootfs = strdup(rootFs);
    container->pid = fork();
    if (pid == (pid_t)-1) {
        return -1;
    } else if (pid) {
        __close_safe(&container->status_fds[__FD_CONTAINER]);
        status = __wait_for_container_event(container, &event);
        if (status || event.data.status) {
            __container_delete(container);
            return status;
        }
        *containerOut = container;
        return 0;
    }
    exit(__container_entry(container, capabilities, mounts, mountsCount));
}

static int __execute_command(struct containerv_container* container, enum containerv_command_type type, size_t payloadSize, const void* payload)
{
    struct containerv_command cmd = {
        .type = type,
        .length = payloadSize + sizeof(struct containerv_command)
    };
    ssize_t bytesWritten;

    bytesWritten = write(container->event_fd, &cmd, sizeof(struct containerv_command));
    if (bytesWritten != sizeof(struct containerv_command)) {
        return -1;
    }
    
    if (payloadSize) {
        bytesWritten = write(container->event_fd, payload, payloadSize);
        if (bytesWritten != payloadSize) {
            return -1;
        }
    }
    return 0;
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct containerv_command_spawn* cmd;
    struct containerv_event          event;
    size_t cmdLength = sizeof(struct containerv_command_spawn);
    size_t flatEnvironmentLength;
    char*  flatEnvironment = __flatten_environment(options->environment, &flatEnvironmentLength);
    char*  data;
    int    status;

    // consider length of args and env
    cmdLength += strlen(path) + 1;
    cmdLength += (options->arguments != NULL) ? (strlen(options->arguments) + 1) : 0;
    cmdLength += (flatEnvironment != NULL) ? flatEnvironmentLength : 0;

    data = calloc(cmdLength, 1);
    if (data == NULL) {
        return -1;
    }

    // initialize command
    cmd = (struct containerv_command_spawn*)data;
    cmd->flags = options->flags;
    cmd->path_length = strlen(path) + 1;
    cmd->argument_length = (options->arguments != NULL) ? (strlen(options->arguments) + 1) : 0;
    cmd->environment_length = (flatEnvironment != NULL) ? flatEnvironmentLength : 0;

    // setup data pointer to point beyond struct
    data += sizeof(struct containerv_command_spawn);

    // write path, and then skip over including zero terminator
    memcpy(&data[0], path, strlen(path));
    data += strlen(path) + 1;

    // write arguments
    if (options->arguments != NULL) {
        memcpy(&data[0], options->arguments, strlen(options->arguments));
        data += strlen(options->arguments) + 1;
    }

    // write environment
    if (flatEnvironment != NULL) {
        memcpy(&data[0], flatEnvironment, flatEnvironmentLength);
        data += flatEnvironmentLength;
    }

    status = __execute_command(container, CV_COMMAND_SPAWN, cmdLength, cmd);
    if (status) {
        return status;
    }
    
    status = __wait_for_container_event(container, &event);
    if (status) {
        return status;
    }
    *pidOut = event.data.spawn.process_id;
    return event.data.spawn.status;
}

int container_kill(struct containerv_container* container, pid_t pid)
{
    struct containerv_command_kill cmd = {
        .process_id = pid
    };
    return __execute_command(container, CV_COMMAND_KILL, sizeof(struct containerv_command_kill), (void*)&cmd);
}

int container_script(struct containerv_container* container, const char* script)
{
    return __execute_command(container, CV_COMMAND_SCRIPT, strlen(script) + 1, script);
}

int container_destroy(struct containerv_container* container)
{
    int status;

    status = __execute_command(container, CV_COMMAND_DESTROY, 0, NULL);
    if (status) {
        return status;
    }

    // cleanup container
    __container_delete(container);

    // done
    return 0;
}
