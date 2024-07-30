/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <errno.h>

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
extern void containerv_socket_event(int fd);

#endif //!__UTILS_H__