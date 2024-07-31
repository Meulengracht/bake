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
#include <unistd.h>

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

struct containerv_container {
    // host
    pid_t       pid;
    char*       rootfs;

    // child
    int         socket_fd;
    int         ns_fds[CV_NS_COUNT];
    struct list processes;

    // shared
    int         status_fds[2];
    int         event_fd;
    char*       runtime_dir;
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
 * 
 * @return
 */
extern int containerv_set_init_process(void);

/**
 * 
 */
extern int containerv_open_socket(struct containerv_container* container);

/**
 * 
 */
extern void containerv_socket_event(struct containerv_container* container);

struct containerv_ns_fd {
    enum containerv_namespace_type type;
    int                            fd;
};

extern int containerv_get_ns_sockets(const char* commSocket, struct containerv_ns_fd fds[CV_NS_COUNT], int* count);

#endif //!__PRIVATE_H__