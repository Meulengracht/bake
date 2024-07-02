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
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils.h"

#define __FD_CONTAINER 0
#define __FD_HOST      1

struct containerv_container {
    pid_t pid;
    char* rootfs;
    char* mountfs;
    int   status_fds[2];
    int   event_fd;

    pid_t processes[64];
    int   process_count;
};

struct containerv_command_spawn {
    char path[32];
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
    union {
        struct containerv_command_spawn  spawn;
        struct containerv_command_kill   kill;
        struct containerv_command_script script;
    } command;
};

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    int                          status;

    container = malloc(sizeof(struct containerv_container));
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

}

static void __spawn_process(struct containerv_container* container, struct containerv_command_spawn* spawn)
{
    pid_t processId;
    int   status;

    // fork a new child, all daemons/root programs are spawned from this code
    processId = fork();
    if (processId) {
        container->processes[container->process_count++] = processId;
        return;
    }

    status = execve(spawn->path, NULL, NULL);
    if (status) {

    }
}

static void __kill_process(struct containerv_container* container, struct containerv_command_kill* kill)
{

}

static void __destroy_container(struct containerv_container* container)
{

}

static int __container_command_handler(struct containerv_container* container, struct containerv_command* command)
{
    switch (command->type) {
        case CV_COMMAND_SPAWN: {
            __spawn_process(container, &command->command.spawn);
        } break;
        case CV_COMMAND_KILL: {
            __kill_process(container, &command->command.kill);
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

    while (1) {
        result = (int)read(container->event_fd, &command, sizeof(struct containerv_command));
        if (result <= 0) {
            continue;
        }

        result = __container_command_handler(container, &command);
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
    int status;
    int flags = 0;

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
        return -1;
    }

    status = unshare(flags | SIGCHLD);
    if (status) {
        return -1;
    }

    // MS_PRIVATE makes the bind mount invisible outside of the namespace
    // MS_REC makes the mount recursive
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        return -1;
    }
  
    // After the unshare we are now running in separate namespaces, this means
    // we can start doing mount operations that still require the host file system
    // before we chroot
    if (capabilities & CV_CAP_FILESYSTEM) {
        // bind mount all additional mounts requested by the caller
        status = __container_map_mounts(container, mounts, mountsCount);
        if (status) {
            return -1;
        }
    }

    // change root to the containers base path
    status = chroot(container->mountfs);
    if (status) {
        return -1;
    }

    // after the chroot we can start setting up the unique bind-mounts
    status = __container_map_capabilities(container, capabilities);
    if (status) {
        return -1;
    }
    write(container->status_fds[__FD_CONTAINER], &status, sizeof (status));
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
    __close_safe(&container->status_fds[__FD_HOST]);

    // This is the primary run function, it initializes the container
    status = __container_run(container, capabilities, mounts, mountsCount);
    if (status) {
        write(container->status_fds[__FD_CONTAINER], &status, sizeof (status));
    }
    exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int __wait_for_container_ready(struct containerv_container* container)
{
    int status;
    int exitCode;

    status = __INTSAFE_CALL(read(container->status_fds[__FD_HOST], &exitCode, sizeof (exitCode)));
    if (status <= 0) {
        return -1;
    }
    return exitCode;
}

int containerv_create(
        const char*                   rootFs,
        enum containerv_capabilities  capabilities,
        struct containerv_mount*      mounts,
        int                           mountsCount,
        struct containerv_container** containerOut)
{
    struct containerv_container* container;
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
        status = __wait_for_container_ready(container);
        if (status) {
            __container_delete(container);
            return status;
        }
        *containerOut = container;
        return 0;
    }
    exit(__container_entry(container, capabilities, mounts, mountsCount));
}

int container_exec(struct containerv_container* container, const char* path, pid_t* pidOut)
{

}

int container_kill(struct containerv_container* container, pid_t pid)
{

}

int container_destroy(struct containerv_container* container)
{

}
