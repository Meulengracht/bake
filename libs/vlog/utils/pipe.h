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

#ifndef __VLOG_PIPE_H__
#define __VLOG_PIPE_H__

#include <stddef.h>
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#else
#include <sys/types.h>
#endif

#define VLOG_PIPE_MESSAGE_MAX (64 * 1024)

struct vlog_pipe {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    HANDLE read_handle;
    HANDLE write_handle;
#else
    int read_fd;
    int write_fd;
#endif
};

extern int vlog_pipe_open(struct vlog_pipe* pipe);
extern void vlog_pipe_close(struct vlog_pipe* pipe);
extern int vlog_pipe_send(struct vlog_pipe* pipe, const void* buffer, size_t length);
extern int vlog_pipe_recv(struct vlog_pipe* pipe, void* buffer, size_t capacity, size_t* length, const struct timespec* deadline);

#endif //!__VLOG_PIPE_H__