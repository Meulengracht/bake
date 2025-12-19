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

#ifndef __PRIVATE_H__
#define __PRIVATE_H__

#include <errno.h>
#include <chef/containerv.h>
#include <chef/list.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#define __CONTAINER_SOCKET_RUNTIME_BASE "/run/containerv"

#define __CONTAINER_MAX_FD_COUNT 16


enum containerv_namespace_type {
    CV_NS_CGROUP = 0,
    CV_NS_IPC,
    CV_NS_MNT,
    CV_NS_NET,
    CV_NS_PID,
    CV_NS_TIME,
    CV_NS_USER,
    CV_NS_UTS,

    CV_NS_COUNT
};

struct containerv_options_user_range {
    unsigned int host_start;
    unsigned int child_start;
    int          count;
};

struct containerv_options_network {
    int         enable;             // whether to enable network isolation
    const char* container_ip;       // IP for container interface (e.g., "10.0.0.2")
    const char* container_netmask;  // Netmask (e.g., "255.255.255.0")
    const char* host_ip;            // IP for host-side veth interface (e.g., "10.0.0.1")
};

struct containerv_options_cgroup {
    const char* memory_max;      // e.g., "1G", "512M", or "max" for no limit
    const char* cpu_weight;      // 1-10000, default is 100
    const char* pids_max;        // maximum number of processes, or "max"
};

struct containerv_options {
    enum containerv_capabilities           capabilities;
    struct containerv_layer_context*       layers;
    struct containerv_policy*              policy;
    
    struct containerv_options_user_range   uid_range;
    struct containerv_options_user_range   gid_range;
    struct containerv_options_network      network;
    struct containerv_options_cgroup       cgroup;
};

struct containerv_container {
    // host
    pid_t        pid;
    thrd_t       log_tid;
    volatile int log_running;
    char*        hostname;        // hostname for cgroups/network

    // child
    char*       rootfs;
    int         socket_fd;
    int         ns_fds[CV_NS_COUNT];
    struct list processes;

    // shared
    const char*                      id;
    struct containerv_layer_context* layers;
    int                              host[2];
    int                              child[2];
    int                              stdout[2];
    int                              stderr[2];
    char*                            runtime_dir;
};

#define __INTSAFE_CALL(__expr) \
({                                            \
    int __status;                             \
    do {                                      \
        __status = (int)(__expr);             \
    } while (__status < 0 && errno == EINTR); \
    __status;                                 \
})

static inline int __close_safe(int *fd)
{
    int status = 0;
    if (*fd >= 0) {
        status = __INTSAFE_CALL(close(*fd));
        if (status == 0) {
            *fd = -1;
        }
    }
    return status;
}

/**
 *
 * @return
 */
extern int containerv_drop_capabilities(void);

/**
 * @brief 
 * 
 * @param uid 
 * @param gid 
 * @return int 
 */
extern int containerv_switch_user_with_capabilities(uid_t uid, gid_t gid);

/**
 * 
 * @return
 */
extern int containerv_set_init_process(void);

/**
 * @brief
 */
extern int containerv_mkdir(const char* root, const char* path, unsigned int mode);

/**
 * 
 */
extern int containerv_open_socket(struct containerv_container* container);

/**
 * 
 */
extern int containerv_socket_event(struct containerv_container* container);

struct containerv_socket_client;

struct containerv_ns_fd {
    enum containerv_namespace_type type;
    int                            fd;
};

extern struct containerv_socket_client* containerv_socket_client_open(const char* containerId);
extern void containerv_socket_client_close(struct containerv_socket_client* client);

extern int containerv_socket_client_spawn(struct containerv_socket_client* client, const char* path, struct containerv_spawn_options* options, process_handle_t* pidOut);
extern int containerv_socket_client_script(struct containerv_socket_client* client, const char* script);
extern int containerv_socket_client_kill(struct containerv_socket_client* client, pid_t processId);
extern int containerv_socket_client_get_root(struct containerv_socket_client* client, char* buffer, size_t length);
extern int containerv_socket_client_get_nss(struct containerv_socket_client* client, struct containerv_ns_fd fds[CV_NS_COUNT], int* count);
extern int containerv_socket_client_send_files(struct containerv_socket_client* client, int* fds, const char* const* filepaths, int* statuses, int count);
extern int containerv_socket_client_recv_files(struct containerv_socket_client* client, const char* const* filepaths, int* fds, int* statuses, int count);
extern int containerv_socket_client_destroy(struct containerv_socket_client* client);

struct __containerv_spawn_options {
    const char*                path;
    const char* const*         argv;
    const char* const*         envv;
    uid_t                      uid;
    gid_t                      gid;
    enum container_spawn_flags flags;
};

extern int __containerv_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, pid_t* pidOut);
extern int __containerv_kill(struct containerv_container* container, pid_t processId);
extern void __containerv_destroy(struct containerv_container* container);

#endif //!__PRIVATE_H__