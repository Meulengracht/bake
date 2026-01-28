/**
 * Copyright, Philip Meulengracht
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
#include <chef/containerv/bpf-manager.h>
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
#include <sys/sysmacros.h> // makedev

#include <unistd.h>
#include "private.h"
#include "cgroups.h"
#include "network.h"
#include <vlog.h>

#define __FD_READ  0
#define __FD_WRITE 1

// Network interface naming constants - veth names are derived from container ID
#define __CONTAINER_VETH_HOST_OFFSET 0    // Use full ID for host-side veth
#define __CONTAINER_VETH_CONT_OFFSET 4    // Use partial ID for container-side veth

struct __child_mount {
    char*                       what;
    char*                       where;
    char*                       fstype;
    enum containerv_mount_flags flags;
};

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

static struct containerv_container* __container_new(const char* containerId)
{
    struct containerv_container* container;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    if (containerId == NULL) {
        container->runtime_dir = __container_create_runtime_dir();
        if (container->runtime_dir == NULL) {
            free(container);
            return NULL;
        }
    } else {
        container->runtime_dir = strpathcombine(__CONTAINER_SOCKET_RUNTIME_BASE, containerId);
        if (container->runtime_dir == NULL) {
            VLOG_ERROR("containerv", "__container_new: failed to allocate runtime dir path\n");
            free(container);
            return NULL;
        }
        if (platform_mkdir(container->runtime_dir)) {
            VLOG_ERROR("containerv", "__container_new: failed to create runtime dir %s\n", container->runtime_dir);
            free(container->runtime_dir);
            free(container);
            return NULL;
        }
    }
    
    // get last part of directory path, the last token is the id
    container->id = strrchr(container->runtime_dir, CHEF_PATH_SEPARATOR) + 1;

    // Use container ID as hostname
    container->hostname = strdup(&container->id[0]);
    if (container->hostname == NULL) {
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    // create the resources that we need immediately
    if (pipe(container->host) || pipe(container->child) ||
        pipe(container->stdout) || pipe(container->stderr)) {
        free(container->hostname);
        free(container->runtime_dir);
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

    __close_safe(&container->host[0]);
    __close_safe(&container->host[1]);
    __close_safe(&container->child[0]);
    __close_safe(&container->child[1]);
    __close_safe(&container->stdout[0]);
    __close_safe(&container->stdout[1]);
    __close_safe(&container->stderr[0]);
    __close_safe(&container->stderr[1]);
    __close_safe(&container->socket_fd);
    free(container->hostname);
    free(container->runtime_dir);
    free(container->rootfs);
    free(container);
}

enum containerv_event_type {
    CV_CONTAINER_WAITING_FOR_NS_SETUP,
    CV_CONTAINER_WAITING_FOR_CGROUPS_SETUP,
    CV_CONTAINER_WAITING_FOR_NETWORK_SETUP,
    CV_CONTAINER_WAITING_FOR_POLICY_SETUP,
    CV_CONTAINER_UP,
    CV_CONTAINER_DOWN
};

struct containerv_event {
    enum containerv_event_type type;
    int                        status;
};

static void __send_container_event(int fds[2], enum containerv_event_type type, int status)
{
    struct containerv_event event = {
        .type = type,
        .status = status
    };
    if (write(fds[__FD_WRITE], &event, sizeof(struct containerv_event)) != sizeof(struct containerv_event)) {
        // log this
    }
}

static int __wait_for_container_event(int fds[2], struct containerv_event* event)
{
    int bytesRead;

    bytesRead = __INTSAFE_CALL(read(fds[__FD_READ], event, sizeof(struct containerv_event)));
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

    if (options->uid != (gid_t)-1) {
        VLOG_DEBUG("containerv[child]", "switching user (%i)\n", options->uid);
        status = setuid(options->uid);
        if (status) {
            VLOG_ERROR("containerv[child]", "failed to switch user: %i (uid=%i)\n", status, options->uid);
            _Exit(-EPERM);
        }
    }

    if (options->gid != (gid_t)-1) {
        VLOG_DEBUG("containerv[child]", "switching group (%i)\n", options->gid);
        status = setgid(options->gid);
        if (status) {
            VLOG_ERROR("containerv[child]", "failed to switch group: %i (gid=%i)\n", status, options->gid);
            _Exit(-EPERM);
        }
    }

    status = execve(options->path, (char* const*)options->argv, (char* const*)options->envv);
    if (status) {
        fprintf(stderr, "[%s]: failed to execute: %i\n", options->path, status);
    }

    _Exit(status);
    return 0; // never reached
}

static void __print(const char* line, int error) {
    if (error) {
        VLOG_ERROR("containerv[child]", line);
    } else {
        VLOG_TRACE("containerv[child]", line);
    }
}

static void __report(char* line, int error)
{
    const char* s = line;
    char*       p = line;
    char        tmp[2048];

    while (*p) {
        if (*p == '\n') {
            // include the \n
            size_t count = (size_t)(p - s) + 1;
            strncpy(&tmp[0], s, count);

            // zero terminate the string and report
            tmp[count] = '\0';
            __print(&tmp[0], error);

            // update new start
            s = ++p;
        } else {
            p++;
        }
    }
    
    // only do a final report if the line didn't end with a newline
    if (s != p) {
        __print(s, error);
    }
}

// 0 => stdout
// 1 => stderr
static int __wait_and_read_stds(void* context)
{
    struct containerv_container* container = context;
    char line[2048];
    
    struct pollfd fds[2] = { 
        { 
            .fd = container->stdout[0],
            .events = POLLIN
        },
        {
            .fd = container->stderr[0],
            .events = POLLIN
        }
    };

    container->log_running = 1;
    while (container->log_running == 1) {
        int status = poll(fds, 2, -1);
        if (status <= 0) {
            return -1;
        }
        if (fds[0].revents & POLLIN) {
            status = read(fds[0].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], 0);
        } else if (fds[1].revents & POLLIN) {
            status = read(fds[1].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], 1);
        } else {
            break;
        }
    }
    container->log_running = 0;
    return 0;
}

int __containerv_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, pid_t* pidOut)
{
    struct containerv_container_process* proc;
    pid_t                                processId;
    int                                  status;
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

    if (options->flags & CV_SPAWN_WAIT) {
        if (waitpid(processId, &status, 0) != processId) {
            VLOG_ERROR("containerv[child]", "__containerv_spawn: failed to wait for pid %u\n", processId);
            return -1;
        }
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
    if (container->child[__FD_WRITE] != -1) {
        __send_container_event(container->child, CV_CONTAINER_DOWN, 0);
    }

    // cleanup container
    __container_delete(container);
}

static int __container_spawn_log(int fd, int error)
{
    char line[2048];
    int  status;

    status = read(fd, &line[0], sizeof(line));
    if (status < 0) {
        return status;
    }

    line[status] = 0;
    __report(&line[0], error);
    return 0;
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
        const char*           root,
        struct __child_mount* mounts,
        int                   mountsCount)
{
    int status = 0;
    VLOG_DEBUG("containerv[child]", "__container_map_mounts(root=%s)\n", root);

    if (!mountsCount) {
        return 0;
    }

    for (int i = 0; i < mountsCount; i++) {
        char* destination = strpathcombine(root, mounts[i].where);
        VLOG_DEBUG("containerv[child]", "__container_map_mounts: mapping %s => %s (%s)\n",
            mounts[i].what, destination, mounts[i].fstype);
        if (mounts[i].flags & CV_MOUNT_CREATE) {
            status = containerv_mkdir(root, mounts[i].where, 0755);
            if (status) {
                VLOG_ERROR("containerv[child]", "__container_map_mounts: could not create %s\n", destination);
                free(destination);
                return -1;
            }
        }

        status = mount(
            mounts[i].what,
            destination,
            mounts[i].fstype,
            __convert_cv_mount_flags(mounts[i].flags),
            NULL
        );
        free(destination);
        if (status) {
            break;
        }
    }
    return status;
}

static int __write_user_namespace_maps(
    struct containerv_container* container,
    struct containerv_options*   options)
{
    int  mapFd, status;
    char tmp[128];
    VLOG_DEBUG("containerv[host]", "__write_user_namespace_maps()\n");

    // write users first
    snprintf(&tmp[0], sizeof(tmp), "/proc/%u/uid_map", container->pid);
    mapFd = open(&tmp[0], O_WRONLY);
    if (mapFd < 0) {
        VLOG_ERROR("containerv[host]", "__write_user_namespace_maps: failed to open %s\n", &tmp[0]);
        return -1;
    }

    // the trick here is, we can only write ONCE, and at the maximum of
    // up to 5 lines.
    status = dprintf(mapFd, "%d %d %d\n",
        options->uid_range.host_start,
        options->uid_range.child_start,
        options->uid_range.count
    );
    if (status < 0) {
        VLOG_ERROR("containerv[host]", "__write_user_namespace_maps: failed to write user map\n");
        close(mapFd);
        return status;
    }
    close(mapFd);

    // write groups
    snprintf(&tmp[0], sizeof(tmp), "/proc/%u/gid_map", container->pid);
    mapFd = open(&tmp[0], O_WRONLY);
    if (mapFd < 0) {
        VLOG_ERROR("containerv[host]", "__write_user_namespace_maps: failed to open %s\n", &tmp[0]);
        return status;
    }

    // the trick here is, we can only write ONCE, and at the maximum of
    // up to 5 lines.
    status = dprintf(mapFd, "%d %d %d\n",
        options->gid_range.host_start,
        options->gid_range.child_start,
        options->gid_range.count
    );
    if (status < 0) {
        VLOG_ERROR("containerv[host]", "__write_user_namespace_maps: failed to write group map\n");
        close(mapFd);
        return status;
    }
    close(mapFd);
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

static int __populate_minimal_dev(void)
{
    int    status;
    char   tmp[PATH_MAX];
    mode_t um;
    VLOG_DEBUG("cvd", "__fixup_dev()\n");

    struct {
        const char* path;
        mode_t      mod;
        dev_t       dev;
    } devices[] = {
        { "null", S_IFCHR | 0666, makedev(1, 3) },
        { "zero", S_IFCHR | 0666, makedev(1, 5) },
        { "random", S_IFCHR | 0666, makedev(1, 8) },
        { "urandom", S_IFCHR | 0666, makedev(1, 9) },
        { NULL, 0, 0 },
    };

    um = umask(0);

    for (int i = 0; devices[i].path != NULL; i++) {
        snprintf(
            &tmp[0],
            sizeof(tmp),
            "/dev/%s", 
            devices[i].path
        );

        status = mknod(&tmp[0], devices[i].mod, devices[i].dev);
        if (status) {
            VLOG_ERROR("cvd", "__fixup_dev: failed to create %s\n", &tmp[0]);
            return status;
        }
    }

    umask(um);
    return 0;
}

static int __container_run(
    struct containerv_container* container,
    struct containerv_options*   options,
    uid_t                        realUid)
{
    int status;
    int flags = CLONE_NEWUTS;
    VLOG_DEBUG("containerv[child]", "__container_run()\n");

    // immediately switch to real root for the rest of the cycle, but
    // at the end of container setup we drop as many privs as possible
    if (realUid != 0) {
        status = setgid(0);
        if (status) {
            VLOG_ERROR("containerv[child]", "failed to switch group: %i (gid=0)\n", status);
            return status;
        }

        status = setuid(0);
        if (status) {
            VLOG_ERROR("containerv[child]", "failed to switch user: %i (uid=0)\n", status);
            return status;
        }
    }

    if (options->capabilities & CV_CAP_FILESYSTEM) {
        flags |= CLONE_NEWNS;
    }

    if (options->capabilities & CV_CAP_NETWORK) {
        flags |= CLONE_NEWNET;
    }

    if (options->capabilities & CV_CAP_PROCESS_CONTROL) {
        flags |= CLONE_NEWPID;
    }

    if (options->capabilities & CV_CAP_IPC) {
        flags |= CLONE_NEWIPC;
    }

    if (options->capabilities & CV_CAP_CGROUPS) {
        flags |= CLONE_NEWCGROUP;
    }

    if (options->capabilities & CV_CAP_USERS) {
        flags |= CLONE_NEWUSER;
    }

    status = unshare(flags);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to unshare the current namespaces\n");
        return status;
    }

    // perform sync with host regarding user namespace setup
    if (options->capabilities & CV_CAP_USERS) {
        struct containerv_event event;

        // notify the host that we need user setup
        VLOG_DEBUG("containerv[child]", "notifying host that we need external assistance\n");
        __send_container_event(container->child, CV_CONTAINER_WAITING_FOR_NS_SETUP, 0);

        // wait for ack
        status = __wait_for_container_event(container->host, &event);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to receive ack from ns setup\n");
            return status;
        }

        // check status
        if (event.status) {
            VLOG_ERROR("containerv[child]", "__container_run: host failed to setup ns, aborting\n");
            return event.status;
        }
    }

    // Set the hostname of the new UTS
    VLOG_TRACE("containerv[child]", "__container_run: setting hostname to %s\n", container->hostname);
    status = sethostname(container->hostname, 15);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to set a new hostname\n");
        return status;
    }

    // Sync with host that we are ready to have cgroups setup
    if (options->capabilities & CV_CAP_CGROUPS) {
        struct containerv_event event;

        // notify the host that we need cgroups setup
        VLOG_DEBUG("containerv[child]", "notifying host that we need external assistance\n");
        __send_container_event(container->child, CV_CONTAINER_WAITING_FOR_CGROUPS_SETUP, 0);

        // wait for ack
        status = __wait_for_container_event(container->host, &event);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to receive ack from cgroups setup\n");
            return status;
        }

        // check status
        if (event.status) {
            VLOG_ERROR("containerv[child]", "__container_run: host failed to setup cgroups, aborting\n");
            return event.status;
        }
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
    if (options->capabilities & CV_CAP_FILESYSTEM) {
        static const int premountsCount = 1;
        struct __child_mount mnts[] = {
            {
                .what  = container->runtime_dir,
                .where = container->runtime_dir,
                .fstype = NULL,
                .flags = CV_MOUNT_BIND | CV_MOUNT_RECURSIVE | CV_MOUNT_CREATE
            }
        };

        if (container->layers != NULL) {
            const char* composedRoot = NULL;

            status = containerv_layers_mount_in_namespace(container->layers);
            if (status) {
                VLOG_ERROR("containerv[child]", "__container_run: failed to mount layers in namespace\n");
                return status;
            }

            // If rootfs is supposed to be the composed rootfs, update it here.
            composedRoot = containerv_layers_get_rootfs(container->layers);
            if (composedRoot == NULL) {
                VLOG_ERROR("containerv[child]", "__container_run: failed to compose final rootfs\n");
                return -ENOMEM;
            }

            // Take ownership of the composed root path in the container
            container->rootfs = strdup(composedRoot);
            if (container->rootfs == NULL) {
                VLOG_ERROR("containerv[child]", "__container_run: failed to duplicate composed root path\n");
                return -ENOMEM;
            }
        }

        status = __container_map_mounts(container->rootfs, &mnts[0], premountsCount);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to map system mounts\n");
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

    // After the chroot we can do now do special mounts
    if (options->capabilities & CV_CAP_FILESYSTEM) {
        static const int postmountsCount = 4;
        struct __child_mount mnts[] = {
            {
                .what = "sysfs",
                .where = "/sys",
                .fstype = "sysfs",
                .flags = CV_MOUNT_CREATE
            },
            {
                .what = "proc",
                .where = "/proc",
                .fstype = "proc",
                .flags = CV_MOUNT_CREATE
            },
            {
                .what = "tmpfs",
                .where = "/tmp",
                .fstype = "tmpfs",
                .flags = CV_MOUNT_CREATE
            },
            {
                .what = "tmpfs",
                .where = "/dev",
                .fstype = "tmpfs",
                .flags = CV_MOUNT_CREATE
            },
        };
        
        status = __container_map_mounts("", &mnts[0], postmountsCount);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to map system mounts\n");
            return status;
        }

        // we could use devtmpfs here, but that requires kernel support, which it most likely
        // already is, but just to be sure we populate a minimal /dev, to have more control
        status = __populate_minimal_dev();
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to populate /dev\n");
            return status;
        }
    }

    // Open the public communication channel after chroot
    container->socket_fd = containerv_open_socket(container);
    if (container->socket_fd < 0) {
        return -1;
    }

    // get a handle on all the ns fd's
    status = __container_open_ns_fds(container);
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to get a handle on NS file descriptors\n");
        return status;
    }
    
    // Setup network interface inside container if enabled
    if (options->capabilities & CV_CAP_NETWORK && options->network.enable && options->network.container_ip) {
        struct containerv_event event;
        char                    container_veth[16];

        // notify the host that we need network setup
        VLOG_DEBUG("containerv[child]", "notifying host that we need external assistance\n");
        __send_container_event(container->child, CV_CONTAINER_WAITING_FOR_NETWORK_SETUP, 0);

        // wait for ack
        status = __wait_for_container_event(container->host, &event);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to receive ack from network setup\n");
            return status;
        }

        // check status
        if (event.status) {
            VLOG_ERROR("containerv[child]", "__container_run: host failed to setup network, aborting\n");
            return event.status;
        }

        snprintf(container_veth, sizeof(container_veth), "veth%sc", &container->id[__CONTAINER_VETH_CONT_OFFSET]);
        
        VLOG_DEBUG("containerv[child]", "__container_run: bringing up container network interface %s\n", container_veth);
        status = if_up(container_veth, (char*)options->network.container_ip, (char*)options->network.container_netmask);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to bring up container veth interface\n");
            return status;
        }
        
        // Bring up loopback interface
        VLOG_DEBUG("containerv[child]", "__container_run: bringing up loopback interface\n");
        status = if_up("lo", "127.0.0.1", "255.0.0.0");
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to bring up loopback interface\n");
            return status;
        }
    }

    // Apply eBPF policy (needs caps, so do it before dropping)
    if (options->policy != NULL) {
        struct containerv_event event;
        VLOG_DEBUG("containerv[child]", "__container_run: applying security policy\n");

        // notify the host that we need policy setup
        VLOG_DEBUG("containerv[child]", "notifying host that we need external assistance\n");
        __send_container_event(container->child, CV_CONTAINER_WAITING_FOR_POLICY_SETUP, 0);

        // wait for ack
        status = __wait_for_container_event(container->host, &event);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to receive ack from policy setup\n");
            return status;
        }

        // check status
        if (event.status) {
            VLOG_ERROR("containerv[child]", "__container_run: host failed to setup policy, aborting\n");
            return event.status;
        }
    } else {
        VLOG_DEBUG("containerv[child]", "__container_run: no security policy configured\n");
    }

    // Drop capabilities that we no longer need
    status = containerv_drop_capabilities();
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to drop capabilities\n");
        return status;
    }

    // Make this process take the role of init(1) before we go into
    // the main loop
    status = containerv_set_init_process();
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_run: failed to assume the PID of 1\n");
        return status;
    }

    // Apply seccomp-bpf for syscall filtering.
    // This must happen after capability dropping and other prctl-based setup,
    // otherwise the filter can block those operations and break container bring-up.
    if (options->policy != NULL) {
        status = policy_seccomp_apply(options->policy);
        if (status) {
            VLOG_ERROR("containerv[child]", "__container_run: failed to apply seccomp policy\n");
            return status;
        }
    }

    // Container is now up and running
    __send_container_event(container->child, CV_CONTAINER_UP, 0);
    return __container_idle_loop(container);
}

static uid_t __real_user(void)
{
    uid_t euid, suid, ruid;
    getresuid(&ruid, &euid, &suid);
    return ruid;
}

static void __container_entry(
    struct containerv_container* container,
    struct containerv_options*   options)
{
    int status;
    VLOG_DEBUG("containerv[child]", "__container_entry(id=%s)\n", container->id);

    // lets not leak the host fds
    if (__close_safe(&container->child[__FD_READ]) || __close_safe(&container->host[__FD_WRITE])) {
        VLOG_ERROR("containerv[child]", "__container_entry: failed to close host status file descriptor\n");
        _Exit(EXIT_FAILURE);
    }

    // This is the primary run function, it initializes the container
    status = __container_run(container, options, __real_user());
    if (status) {
        VLOG_ERROR("containerv[child]", "__container_entry: failed to execute: %i\n", status);
        __send_container_event(container->child, CV_CONTAINER_DOWN, status);
    }
    _Exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

int containerv_create(
    const char*                   containerId,
    struct containerv_options*    options,
    struct containerv_container** containerOut)
{
    struct containerv_container* container;
    int                          status;
    VLOG_DEBUG("containerv[host]", "containerv_create(caps=0x%x)\n", options->capabilities);

    // ensure the containerv runtime path exists
    status = platform_mkdir(__CONTAINER_SOCKET_RUNTIME_BASE);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to create %s\n", __CONTAINER_SOCKET_RUNTIME_BASE);
        return -1;
    }

    container = __container_new(containerId);
    if (container == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to allocate memory for container\n");
        return -1;
    }
    
    container->layers = options->layers;
    container->pid = fork();
    if (container->pid == (pid_t)-1) {
        VLOG_ERROR("containerv[host]", "containerv_create: failed to fork container process\n");
        return -1;
    } else if (container->pid) {
        VLOG_DEBUG("containerv[host]", "cleaning up and waiting for container to get up and running\n");
        
        // cleanup the fds we don't use
        __close_safe(&container->host[__FD_READ]);
        __close_safe(&container->child[__FD_WRITE]);
        __close_safe(&container->stdout[__FD_WRITE]);
        __close_safe(&container->stderr[__FD_WRITE]);

        // spawn log thread
        if (thrd_create(&container->log_tid, __wait_and_read_stds, container) != thrd_success) {
            VLOG_ERROR("containerv[host]", "failed to spawn thread for log monitoring\n");
        }
        
        // wait for container to come up
        for (int waiting = 1; waiting;) {
            struct containerv_event event = { 0 };
            status = __wait_for_container_event(container->child, &event);
            if (status) {
                VLOG_ERROR("containerv[host]", "containerv_create: failed to read container event: %i\n", status);
                __container_delete(container);
                return status;
            }

            switch (event.type) {
                case CV_CONTAINER_WAITING_FOR_NS_SETUP: {
                    VLOG_DEBUG("containerv[host]", "setting up namespace configuration\n");
                    status = __write_user_namespace_maps(container, options);
                    if (status) {
                        VLOG_ERROR("containerv[host]", "containerv_create: failed to write user namespace maps: %i\n", status);
                    }
                    
                    __send_container_event(container->host, CV_CONTAINER_WAITING_FOR_NS_SETUP, status);
                } break;

                case CV_CONTAINER_WAITING_FOR_CGROUPS_SETUP:
                    struct containerv_cgroup_limits limits = {
                        .memory_max = options->cgroup.memory_max,
                        .cpu_weight = options->cgroup.cpu_weight,
                        .pids_max = options->cgroup.pids_max,
                        .enable_devices = 0
                    };
                    
                    VLOG_DEBUG("containerv[host]", "setting up cgroups for %s (pid=%d)\n", container->hostname, container->pid);
                    status = cgroups_init(container->hostname, container->pid, &limits);
                    if (status) {
                        VLOG_ERROR("containerv[host]", "containerv_create: failed to setup cgroups: %i\n", status);
                    }
                    
                    __send_container_event(container->host, CV_CONTAINER_WAITING_FOR_CGROUPS_SETUP, status);
                    break;

                case CV_CONTAINER_WAITING_FOR_NETWORK_SETUP: {
                    char host_veth[16];
                    char container_veth[16];
                    int netns_fd;
                    int sock_fd;
                    
                    // Create unique veth pair names from container ID
                    // Host side uses full ID, container side uses partial ID for brevity
                    snprintf(host_veth, sizeof(host_veth), "veth%s", &container->id[__CONTAINER_VETH_HOST_OFFSET]);
                    snprintf(container_veth, sizeof(container_veth), "veth%sc", &container->id[__CONTAINER_VETH_CONT_OFFSET]);
                    
                    VLOG_DEBUG("containerv[host]", "setting up network for %s\n", container->hostname);
                    
                    // Create netlink socket
                    sock_fd = create_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
                    if (sock_fd < 0) {
                        VLOG_ERROR("containerv[host]", "containerv_create: failed to create netlink socket\n");
                        status = -1;
                    }
                    
                    // Create veth pair
                    if (!status) {
                        status = create_veth(sock_fd, host_veth, container_veth);
                        if (status) {
                            VLOG_ERROR("containerv[host]", "containerv_create: failed to create veth pair\n");
                        }
                    }
                    
                    // Get container's network namespace
                    if (!status) {
                        netns_fd = get_netns_fd(container->pid);
                        if (netns_fd < 0) {
                            VLOG_ERROR("containerv[host]", "containerv_create: failed to get netns fd\n");
                            status = -1;
                        }
                    }
                    
                    // Move container veth to container namespace
                    if (!status) {
                        status = move_if_to_pid_netns(sock_fd, container_veth, netns_fd);
                        if (status) {
                            VLOG_ERROR("containerv[host]", "containerv_create: failed to move veth to container namespace\n");
                        }
                        close(netns_fd);
                    }
                    
                    // Bring up host side veth interface
                    if (!status && options->network.host_ip) {
                        status = if_up(host_veth, (char*)options->network.host_ip, (char*)options->network.container_netmask);
                        if (status) {
                            VLOG_ERROR("containerv[host]", "containerv_create: failed to bring up host veth interface\n");
                        }
                    }
                    
                    if (sock_fd >= 0) {
                        close(sock_fd);
                    }
                    
                    __send_container_event(container->host, CV_CONTAINER_WAITING_FOR_NETWORK_SETUP, status);
                } break;

                case CV_CONTAINER_WAITING_FOR_POLICY_SETUP: {
                    // Populate BPF policy if BPF manager is available
                    if (containerv_bpf_manager_is_available() && options->policy != NULL) {
                        const char* rootfs = containerv_layers_get_rootfs(options->layers);
                        VLOG_DEBUG("containerv[host]", "populating BPF policy for container %s\n", container->id);
                        status = containerv_bpf_manager_populate_policy(container->id, rootfs, options->policy);
                        if (status < 0) {
                            VLOG_ERROR("containerv[host]", "failed to populate BPF policy for %s\n", container->id);
                        }
                    }

                    __send_container_event(container->host, CV_CONTAINER_WAITING_FOR_POLICY_SETUP, status);
                } break;

                case CV_CONTAINER_DOWN: {
                    VLOG_ERROR("containerv[host]", "containerv_create: child reported error: %i\n", event.status);
                    // Best-effort cleanup of any BPF policy entries that may have been populated.
                    // This ensures containerv does not rely on external callers to clean per-container policy state.
                    if (containerv_bpf_manager_is_available()) {
                        (void)containerv_bpf_manager_cleanup_policy(container->id);
                    }
                    __container_delete(container);
                    return event.status;
                } break;

                case CV_CONTAINER_UP: {
                    VLOG_DEBUG("containerv[host]", "child container successfully running\n");
                    waiting = 0;
                } break;
            }
        }
        *containerOut = container;
        return 0;
    }

    // close pipes we don't need on the child side
    __close_safe(&container->stdout[__FD_READ]);
    __close_safe(&container->stderr[__FD_READ]);

    // switch output, from this point forward we do not use VLOG_*
    dup2(container->stdout[__FD_WRITE], STDOUT_FILENO);
    __close_safe(&container->stdout[__FD_WRITE]);
    dup2(container->stderr[__FD_WRITE], STDERR_FILENO);
    __close_safe(&container->stderr[__FD_WRITE]);

    // The entry function does not return
    __container_entry(container, options);
    
    // But still, keep a catch all
    _Exit(-EINVAL);
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct containerv_socket_client* client;
    int                              status = -1;
    VLOG_DEBUG("containerv[host]", "containerv_spawn()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_spawn: failed to connect to server\n");
        return status;
    }

    status = containerv_socket_client_spawn(client, path, options, pidOut);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_spawn: %s failed with %i\n", path, status);
    }

    containerv_socket_client_close(client);
    return status;
}

int containerv_kill(struct containerv_container* container, pid_t pid)
{
    struct containerv_socket_client* client;
    int                              status = -1;
    VLOG_DEBUG("containerv[host]", "containerv_kill()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
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

int containerv_wait(struct containerv_container* container, pid_t pid, int* exit_code_out)
{
    struct containerv_socket_client* client;
    int                              status = -1;
    VLOG_DEBUG("containerv[host]", "containerv_wait()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_wait: failed to connect to server\n");
        return status;
    }

    status = containerv_socket_client_wait(client, pid, exit_code_out);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_wait: wait failed (%i)\n", status);
    }

    containerv_socket_client_close(client);
    return status;
}

int containerv_upload(struct containerv_container* container, const char* const* hostPaths, const char* const* containerPaths, int count)
{
    int                              fds[__CONTAINER_MAX_FD_COUNT] = { -1 };
    int                              results[__CONTAINER_MAX_FD_COUNT];
    struct containerv_socket_client* client;
    int                              status = -1;

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_upload: failed to connect to server\n");
        return status;
    }

    for (int i = 0; i < count; i++) {
        fds[i] = open(hostPaths[i], O_RDONLY);
        if (fds[i] < 0) {
            VLOG_ERROR("containerv[host]", "containerv_upload: failed to open %s for upload\n", hostPaths[i]);
            status = -1;
            goto cleanup;
        }
    }

    status = containerv_socket_client_send_files(client, &fds[0], containerPaths, &results[0], count);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_upload: failed to read namespace sockets from container\n");
        containerv_socket_client_close(client);
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        if (results[i]) {
            VLOG_ERROR("containerv[host]", "containerv_upload: failed to upload %s: %i\n", hostPaths[i], results[i]);
            status = -1;
        }
    }

cleanup:
    for (int i = 0; i < count; i++) {
        if (fds[i] > 0) {
            close(fds[i]);
        }
    }
    containerv_socket_client_close(client);
    return status;
}

int containerv_download(struct containerv_container* container, const char* const* containerPaths, const char* const* hostPaths, int count)
{
    int                              fds[__CONTAINER_MAX_FD_COUNT] = { -1 };
    int                              results[__CONTAINER_MAX_FD_COUNT];
    struct containerv_socket_client* client;
    int                              status = -1;
    char                             xbuf[4096];

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
    if (client == NULL) {
        VLOG_ERROR("containerv[host]", "containerv_download: failed to connect to server\n");
        return status;
    }

    status = containerv_socket_client_recv_files(client, containerPaths, &fds[0], &results[0], count);
    if (status) {
        VLOG_ERROR("containerv[host]", "containerv_download: failed to read namespace sockets from container\n");
        containerv_socket_client_close(client);
        return status;
    }
    containerv_socket_client_close(client);

    for (int i = 0, j = 0; i < count; i++) {
        struct stat st;
        int         infd;
        int         outfd;
        long        n;

        if (results[i]) {
            VLOG_ERROR("containerv[host]", "containerv_download: failed to open %s: %i (skipping)\n", containerPaths[i], results[i]);
            continue;
        }

        infd = fds[j++];
        if (fstat(infd, &st)) {
            VLOG_ERROR("containerv[host]", "containerv_download: failed to stat container file: %s - skipping\n", containerPaths[i]);
            close(infd);
            continue;
        }
        
        outfd = open(hostPaths[i], O_CREAT | O_WRONLY | O_TRUNC, st.st_mode);
        if (outfd < 0) {
            VLOG_ERROR("containerv[host]", "containerv_download: failed to create: %s - skipping\n", hostPaths[i]);
            close(infd);
            continue;
        }

        while ((n = read(infd, xbuf, sizeof(xbuf))) > 0) {
            if (write(outfd, xbuf, n) != n) {
                VLOG_ERROR("containerv[host]", "containerv_download: failed to write %s\n", hostPaths[i]);
                close(outfd);
                close(infd);
                continue;
            }
        }
        close(outfd);
        close(infd);
    }
    return 0;
}

int containerv_destroy(struct containerv_container* container)
{
    struct containerv_socket_client* client;
    struct containerv_event          event = { 0 };
    int                              status = -1;
    VLOG_DEBUG("containerv[host]", "containerv_destroy()\n");

    VLOG_DEBUG("containerv[host]", "connecting to %s\n", container->id);
    client = containerv_socket_client_open(container->id);
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
    status = __wait_for_container_event(container->child, &event);
    if (status) {
        VLOG_ERROR("containerv[host]", "waiting for container event returned: %i\n", status);
        return status;
    }

    VLOG_DEBUG("containerv[host]", "waiting for log monitor...\n");
    if (container->log_running) {
        // do not wait more than 2s, otherwise just shutdown
        size_t maxWaiting = 2000;
        container->log_running = 2;
        while (container->log_running && maxWaiting > 0) {
            platform_sleep(100);
            maxWaiting -= 100;
        }
    }

    // Best-effort cleanup of any BPF policy entries for this container.
    // Policy is keyed by cgroup id; cleaning it up avoids long-lived pinned-map growth.
    if (containerv_bpf_manager_is_available()) {
        int bpf_status = containerv_bpf_manager_cleanup_policy(container->id);
        if (bpf_status < 0) {
            VLOG_WARNING("containerv[host]", "failed to cleanup BPF policy for %s\n", container->id);
        }
    }

    // Clean up cgroups if they exist
    VLOG_DEBUG("containerv[host]", "cleaning up cgroups for %s...\n", container->hostname);
    cgroups_free(container->hostname);

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
    int                              status = -1;
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

const char* containerv_id(struct containerv_container* container)
{
    if (container == NULL) {
        return NULL;
    }
    return container->id;
}
