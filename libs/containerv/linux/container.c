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
#include <fcntl.h>
#include <poll.h>

// mount and pid stuff
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <unistd.h>
#include "private.h"
#include <vlog.h>

#define __FD_READ  0
#define __FD_WRITE 1

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

static char* __container_create_runtime_dir(void)
{
    char template[] = __CONTAINER_SOCKET_RUNTIME_BASE "/c-XXXXXX";
    char* directory;
    
    directory = mkdtemp(&template[0]);
    if (directory == NULL) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create: %s\n", &template[0]);
        return NULL;
    }
    return strdup(directory);
}

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    int                          status;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    container->runtime_dir = __container_create_runtime_dir();
    if (container->runtime_dir == NULL) {
        free(container);
        return NULL;
    }
    memcpy(
        &container->id[0],
        &container->runtime_dir[strlen(container->runtime_dir) - __CONTAINER_ID_LENGTH],
        __CONTAINER_ID_LENGTH
    );

    // create the resources that we need immediately
    status = pipe(container->status_fds);
    if (status) {
        free(container);
        return NULL;
    }

    container->pid = -1;
    container->socket_fd = -1;
    for (int i = 0; i < CV_NS_COUNT; i++) {
        container->ns_fds[i] = -1;
    }

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

    for (int i = 0; i < CV_NS_COUNT; i++) {
        __close_safe(&container->ns_fds[i]);
    }
    __close_safe(&container->status_fds[0]);
    __close_safe(&container->status_fds[1]);
    __close_safe(&container->socket_fd);
    free(container->runtime_dir);
    free(container->rootfs);
    free(container);
}

enum containerv_event_type {
    CV_CONTAINER_UP,
    CV_CONTAINER_DOWN
};

struct containerv_event {
    enum containerv_event_type type;
    int                        status;
};

static void __send_container_event(struct containerv_container* container, enum containerv_event_type type, int status)
{
    struct containerv_event event = {
        .type = type,
        .status = status
    };
    if (write(container->status_fds[__FD_WRITE], &event, sizeof(struct containerv_event)) != sizeof(struct containerv_event)) {
        // log this
    }
}

static int __wait_for_container_event(struct containerv_container* container, struct containerv_event* event)
{
    int bytesRead;

    bytesRead = __INTSAFE_CALL(read(container->status_fds[__FD_READ], event, sizeof(struct containerv_event)));
    if (bytesRead != sizeof(struct containerv_event)) {
        return -1;
    }
    return 0;
}

static pid_t __exec(struct __containerv_spawn_options* options)
{
    pid_t processId;
    int   status;

    // fork a new child, all daemons/root programs are spawned from this code
    processId = fork();
    if (processId != (pid_t)0) {
        return processId;
    }

    if (options->gid != (gid_t)-1) {
        VLOG_DEBUG("containerv[child]", "switching group (%i)\n", options->gid);
        status = setgid(options->gid);
        if (status) {
            VLOG_ERROR("containerv[child]", "[%s]: failed to switch group: %i (gid=%i)\n", status, options->gid);
            _Exit(-EPERM);
        }
    }

    if (options->uid != (gid_t)-1) {
        VLOG_DEBUG("containerv[child]", "switching user (%i)\n", options->uid);
        status = setuid(options->uid);
        if (status) {
            VLOG_ERROR("containerv[child]", "[%s]: failed to switch user: %i (uid=%i)\n", status, options->uid);
            _Exit(-EPERM);
        }
    }

    status = execve(options->path, (char* const*)options->argv, (char* const*)options->envv);
    if (status) {
        VLOG_ERROR("containerv[child]", "[%s]: failed to execute: %i\n", options->path, status);
    }
    _Exit(status);
    return 0; // never reached
}

int __containerv_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, pid_t* pidOut)
{
    struct containerv_container_process* proc;
    pid_t                                processId;
    VLOG_DEBUG("containerv[child]", "__containerv_spawn(path=%s)\n", options->path);

    processId = __exec(options);
    if (processId == (pid_t)-1) {
        VLOG_ERROR("containerv[child]", "__containerv_spawn: failed to exec %s\n", options->path);
        return -1;
    }

    proc = containerv_container_process_new(processId);
    if (proc) {
        list_add(&container->processes, &proc->list_header);
    }
    *pidOut = processId;
    return 0;
}

int __containerv_kill(struct containerv_container* container, pid_t processId)
{
    struct list_item* i;

    list_foreach (&container->processes, i) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->pid == processId) {
            kill(processId, SIGTERM);
            list_remove(&container->processes, i);
            containerv_container_process_delete(proc);
            return 0;
        }
    }
    return -1;
}

void __containerv_destroy(struct containerv_container* container)
{
    struct list_item* i;
    VLOG_DEBUG("containerv[child]", "__destroy_container()\n");

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

static int __container_idle_loop(struct containerv_container* container)
{
    int           status;
    struct pollfd fds[1] = {
        {
            .fd = container->socket_fd,
            .events = POLLIN
        }
    };
    VLOG_DEBUG("containerv[child]", "__container_idle_loop()\n");

    for (;;) {
        status = poll(fds, 1, -1);
        if (status <= 0) {
            return -1;
        }
        
        if (fds[0].revents & POLLIN) {
            status = containerv_socket_event(container);
            if (status != 0) {
                break;
            }
        }
    }
    return 0;
}

static int __container_map_specialfs(
        const char* fsType,
        const char* path)
{
    if (mkdir(path, 0755) && errno != EEXIST) {
        return -1;
    }

    if (mount(fsType, path, fsType, 0, NULL)) {
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
    VLOG_DEBUG("containerv[child]", "__container_map_mounts()\n");

    if (!mountsCount) {
        return 0;
    }

    destination = malloc(PATH_MAX);
    if (destination == NULL) {
        return -1;
    }

    for (int i = 0; i < mountsCount; i++) {
        //snprintf(destination, PATH_MAX, "%s/%s", container->mountfs, mounts[i].destination);
        VLOG_DEBUG("containerv[child]", "__container_map_mounts: mapping %s => %s (%s)\n",
            mounts[i].what, mounts[i].where, mounts[i].fstype);
        if (mounts[i].flags & CV_MOUNT_CREATE) {
            status = platform_mkdir(mounts[i].where);
            if (status) {
                VLOG_ERROR("containerv[child]", "__container_map_mounts: could not create %s\n", mounts[i].where);
                return -1;
            }
        }

        status = mount(
            mounts[i].what,
            //destination,
            mounts[i].where,
            mounts[i].fstype,
            __convert_cv_mount_flags(mounts[i].flags),
            NULL
        );
        if (status) {
            break;
        }
    }
    free(destination);
    return status;
}

static int __container_map_capabilities(
        struct containerv_container* container,
        enum containerv_capabilities capabilities)
{
    int status;
    VLOG_DEBUG("containerv[child]", "__container_map_capabilities()\n");

    status = __container_map_specialfs("sysfs", "/sys");
    if (status) {
        return -1;
    }

    status =  __container_map_specialfs("proc", "/proc");
    if (status) {
        return -1;
    }

    status =  __container_map_specialfs("tmpfs", "/tmp");
    if (status) {
        return -1;
    }
    return 0;
}

static int __container_open_ns_fds(
        struct containerv_container* container)
{
    struct {
        const char*                    path;
        enum containerv_namespace_type type;
    } nsPaths[] = {
        { "/proc/self/ns/cgroup", CV_NS_CGROUP },
        { "/proc/self/ns/ipc",    CV_NS_IPC },
        { "/proc/self/ns/mnt",    CV_NS_MNT },
        { "/proc/self/ns/net",    CV_NS_NET },
        { "/proc/self/ns/pid",    CV_NS_PID },
        { "/proc/self/ns/time",   CV_NS_TIME },
        { "/proc/self/ns/user",   CV_NS_USER },
        { "/proc/self/ns/uts",    CV_NS_UTS },
        { NULL,                   CV_NS_COUNT }
    };

    for (int i = 0; nsPaths[i].path != NULL; i++) {
        if (access(nsPaths[i].path, 0) == 0) {
            container->ns_fds[nsPaths[i].type] = open(nsPaths[i].path, O_RDONLY | O_CLOEXEC);
            if (container->ns_fds[nsPaths[i].type] == -1) {
                VLOG_ERROR("containerv[child]", "__container_open_ns_fds: could not open %s\n", nsPaths[i].path);
                return container->ns_fds[nsPaths[i].type];
            }
        }
    }
    return 0;
}

static char* __host_container_dir(struct containerv_container* container, const char* path)
{
    return strpathcombine(container->rootfs, path);
}

static int __container_run(
        struct containerv_container* container,
        enum containerv_capabilities capabilities,
        struct containerv_mount*     mounts,
        int                          mountsCount)
{
    struct containerv_event event;
    int                     status;
    int                     flags = CLONE_NEWUTS;
    VLOG_DEBUG("containerv[child]", "__container_run()\n");

    if (capabilities & CV_CAP_FILESYSTEM) {
        flags |= CLONE_NEWNS;
    }

    if (capabilities & CV_CAP_NETWORK) {
        flags |= CLONE_NEWNET;
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

    status = unshare(flags);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to unshare the current namespaces\n");
        return status;
    }

    // Make this process take the role of init(1)
    status = containerv_set_init_process();
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to assume the PID of 1\n");
        return status;
    }

    // Set the hostname of the new UTS
    status = sethostname("containerv-host", 15);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to set a new hostname\n");
        return status;
    }

    // MS_PRIVATE makes the bind mount invisible outside of the namespace
    // MS_REC makes the mount recursive
    status = mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to remount root\n");
        return status;
    }

    // After the unshare we are now running in separate namespaces, this means
    // we can start doing mount operations that still require the host file system
    // before we chroot
    if (capabilities & CV_CAP_FILESYSTEM) {
        struct containerv_mount mnts[] = {
            {
                .what = container->runtime_dir,
                .where = __host_container_dir(container, container->runtime_dir),
                .fstype = NULL,
                .flags = CV_MOUNT_BIND | CV_MOUNT_RECURSIVE | CV_MOUNT_CREATE
            }
        };

        status = __container_map_mounts(container, &mnts[0], 1);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to map system mounts\n");
            return status;
        }

        // cleanup
        free(mnts[0].where);

        // bind mount all additional mounts requested by the caller
        status = __container_map_mounts(container, mounts, mountsCount);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to map requested mounts\n");
            return status;
        }
    }

    // change the working directory so we don't accidentally lock any paths
    status = chdir(container->rootfs);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to change directory to the new root\n");
        return status;
    }

    // change root to the containers base path
    status = chroot(container->rootfs);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to chroot into new root (%s)\n", container->rootfs);
        return status;
    }

    // after chroot, we want to change to the root
    status = chdir("/");
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to change directory to root\n");
        return status;
    }

    // Open the public communication channel after chroot
    container->socket_fd = containerv_open_socket(container);
    if (container->socket_fd < 0) {
        return -1;
    }

    // after the chroot we can start setting up the unique bind-mounts
    status = __container_map_capabilities(container, capabilities);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to map capability specific mounts\n");
        return status;
    }

    // get a handle on all the ns fd's
    status = __container_open_ns_fds(container);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to get a handle on NS file descriptors\n");
        return status;
    }
    
    // Drop capabilities that we no longer need
    status = containerv_drop_capabilities();
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to drop capabilities\n");
        return status;
    }
    
    __send_container_event(container, CV_CONTAINER_UP, 0);
    return __container_idle_loop(container);
}

static int __container_entry(
        struct containerv_container* container,
        enum containerv_capabilities capabilities,
        struct containerv_mount*     mounts,
        int                          mountsCount)
{
    int status;
    VLOG_DEBUG("containerv[child]", "__container_entry(id=%s)\n", &container->id[0]);

    // lets not leak the host fd
    status = __close_safe(&container->status_fds[__FD_READ]);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_entry: failed to close host status file descriptor\n");
        _Exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    // This is the primary run function, it initializes the container
    status = __container_run(container, capabilities, mounts, mountsCount);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_entry: failed to execute: %i\n", status);
        __send_container_event(container, CV_CONTAINER_DOWN, status);
    }
    _Exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

int containerv_create(
        const char*                   rootFs,
        enum containerv_capabilities  capabilities,
        struct containerv_mount*      mounts,
        int                           mountsCount,
        struct containerv_container** containerOut)
{
    struct containerv_container* container;
    struct containerv_event      event = { 0 };
    int                          status;
    VLOG_DEBUG("containerv[host]", "containerv_create(root=%s, caps=0x%x)\n", rootFs, capabilities);

    status = platform_mkdir(__CONTAINER_SOCKET_RUNTIME_BASE);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to create %s\n", __CONTAINER_SOCKET_RUNTIME_BASE);
        return -1;
    }

    container = __container_new();
    if (container == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to allocate memory for container\n");
        return -1;
    }

    container->rootfs = strdup(rootFs);
    container->pid = fork();
    if (container->pid == (pid_t)-1) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to fork container process\n");
        return -1;
    } else if (container->pid) {
        VLOG_DEBUG("containerv[host]", "cleaning up and waiting for container to get up and running\n");
        __close_safe(&container->status_fds[__FD_WRITE]);
        status = __wait_for_container_event(container, &event);
        if (status) {
            VLOG_ERROR("containerv[host]", "containerv_create: failed to read container event: %i\n", status);
            __container_delete(container);
            return status;
        }
        if (event.status) {
            VLOG_ERROR("containerv[host]", "containerv_create: child reported error: %i\n", event.status);
            __container_delete(container);
            return event.status;
        }
        *containerOut = container;
        return 0;
    }
    exit(__container_entry(container, capabilities, mounts, mountsCount));
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct containerv_socket_client* client;
    int                              status;
    VLOG_DEBUG("containerv[host]", "containerv_spawn()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", &container->id[0]);
    client = containerv_socket_client_open(&container->id[0]);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_spawn: failed to connect to server\n");
        return status;
    }

    status = containerv_socket_client_spawn(client, path, options, pidOut);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_spawn: failed to spawn %s\n", path);
    }

    containerv_socket_client_close(client);
    return status;
}

int containerv_kill(struct containerv_container* container, pid_t pid)
{
    struct containerv_socket_client* client;
    int                              status;
    VLOG_DEBUG("containerv[host]", "containerv_kill()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", &container->id[0]);
    client = containerv_socket_client_open(&container->id[0]);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_kill: failed to connect to server\n");
        return status;
    }

    status = containerv_socket_client_kill(client, pid);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_kill: failed to execute kill\n");
    }

    containerv_socket_client_close(client);
    return status;
}

int containerv_destroy(struct containerv_container* container)
{
    struct containerv_socket_client* client;
    struct containerv_event          event = { 0 };
    int                              status;
    VLOG_DEBUG("containerv[host]", "containerv_destroy()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", &container->id[0]);
    client = containerv_socket_client_open(&container->id[0]);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_destroy: failed to connect to server\n");
        return status;
    }

    VLOG_DEBUG("containerv[host]", "sending destroy command\n");
    status = containerv_socket_client_destroy(client);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_destroy: failed to execute command\n");
    }

    VLOG_DEBUG("containerv[host]", "waiting for container to shutdown...\n");
    status = __wait_for_container_event(container, &event);
    if (status) {
        VLOG_ERROR("containerv[host]", "waiting for container event returned: %i\n", status);
        return status;
    }

    status = platform_rmdir(container->runtime_dir);
    if (status) {
        VLOG_ERROR("containerv[host]", "could not remove runtime data %s: %i\n",
            container->runtime_dir, status);
        return status;
    }

    VLOG_DEBUG("containerv[host]", "cleaning up\n");
    __container_delete(container);
    return 0;
}

int containerv_join(const char* containerId)
{
    struct containerv_ns_fd          fds[CV_NS_COUNT] = { 0 };
    struct containerv_socket_client* client;
    char                             chrPath[PATH_MAX] = { 0 };
    int                              status;
    int                              count;

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", containerId);
    client = containerv_socket_client_open(containerId);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to connect to server\n");
        return status;
    }

    VLOG_DEBUG("containerv[host]", "reading container configuration\n");
    status = containerv_socket_client_get_root(client, &chrPath[0], PATH_MAX);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to read container configuration\n");
        containerv_socket_client_close(client);
        return status;
    }

    status = containerv_socket_client_get_nss(client, fds, &count);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to read namespace sockets from container\n");
        containerv_socket_client_close(client);
        return status;
    }

    containerv_socket_client_close(client);

    // change the directory to the chroot - so we don't lock down
    // any paths before
    status = chdir(&chrPath[0]);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to change directory to the chroot\n");
        return status;
    }

    VLOG_DEBUG("containerv[host]", "preparing environment\n");
    for (int i = 0; i < count; i++) {
        if (setns(fds[i].fd, 0))  {
            VLOG_WARNING("containerv[host]", "containerv_join: failed to join container namespace %i of type %i\n",
                fds[i].fd, fds[i].type);
        }
    }

    VLOG_DEBUG("containerv[host]", "joining container\n");
    status = chroot(&chrPath[0]);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to chroot into container root %s\n", &chrPath[0]);
        return status;
    }

    // after chroot, we want to change to the root
    status = chdir("/");
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_join: failed to change directory to root\n");
        return status;
    }

    VLOG_DEBUG("containerv[host]", "dropping capabilities\n");
    status = containerv_drop_capabilities();
    if (status) {
        VLOG_ERROR("containerv[child]", "containerv_join: failed to drop capabilities\n");
        return status;
    }
    VLOG_DEBUG("containerv[child]", "successfully joined container\n");
    return 0;
}
