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

#include "pipe.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

static void __pipe_sleep_ms(long milliseconds)
{
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    thrd_sleep(&ts, NULL);
}

int vlog_pipe_open(struct vlog_pipe* pipe)
{
    SECURITY_ATTRIBUTES attributes = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE
    };
    char name[128];

    if (pipe == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(pipe, 0, sizeof(struct vlog_pipe));
    snprintf(&name[0], sizeof(name), "\\\\.\\pipe\\chef-vlog-%lu-%p", GetCurrentProcessId(), (void*)pipe);

    pipe->read_handle = CreateNamedPipeA(
        &name[0],
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        VLOG_PIPE_MESSAGE_MAX,
        VLOG_PIPE_MESSAGE_MAX,
        0,
        &attributes
    );
    if (pipe->read_handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    SetHandleInformation(pipe->read_handle, HANDLE_FLAG_INHERIT, 0);

    pipe->write_handle = CreateFileA(
        &name[0],
        GENERIC_WRITE,
        0,
        &attributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (pipe->write_handle == INVALID_HANDLE_VALUE) {
        CloseHandle(pipe->read_handle);
        memset(pipe, 0, sizeof(struct vlog_pipe));
        return -1;
    }

    if (!ConnectNamedPipe(pipe->read_handle, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        vlog_pipe_close(pipe);
        return -1;
    }
    return 0;
}

void vlog_pipe_close(struct vlog_pipe* pipe)
{
    if (pipe == NULL) {
        return;
    }
    if (pipe->read_handle != NULL && pipe->read_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe->read_handle);
    }
    if (pipe->write_handle != NULL && pipe->write_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe->write_handle);
    }
    memset(pipe, 0, sizeof(struct vlog_pipe));
}

int vlog_pipe_send(struct vlog_pipe* pipe, const void* buffer, size_t length)
{
    DWORD written;

    if (pipe == NULL || buffer == NULL || length > VLOG_PIPE_MESSAGE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (!WriteFile(pipe->write_handle, buffer, (DWORD)length, &written, NULL) || written != length) {
        return -1;
    }
    return 0;
}

int vlog_pipe_recv(struct vlog_pipe* pipe, void* buffer, size_t capacity, size_t* length, const struct timespec* deadline)
{
    DWORD available;
    DWORD readBytes;

    if (pipe == NULL || buffer == NULL || length == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        if (!PeekNamedPipe(pipe->read_handle, NULL, 0, NULL, &available, NULL)) {
            return -1;
        }
        if (available > 0) {
            break;
        }
        if (deadline != NULL) {
            struct timespec now;
            timespec_get(&now, TIME_UTC);
            if (now.tv_sec > deadline->tv_sec ||
                (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec)) {
                return 0;
            }
        }
        __pipe_sleep_ms(10);
    }

    if (available > capacity) {
        errno = EMSGSIZE;
        return -1;
    }
    if (!ReadFile(pipe->read_handle, buffer, (DWORD)capacity, &readBytes, NULL)) {
        return -1;
    }
    *length = readBytes;
    return 1;
}

#else

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static int __set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int __deadline_to_timeout_ms(const struct timespec* deadline)
{
    struct timespec now;
    long long       seconds;
    long long       nanoseconds;
    long long       milliseconds;

    if (deadline == NULL) {
        return -1;
    }

    timespec_get(&now, TIME_UTC);
    seconds = (long long)deadline->tv_sec - (long long)now.tv_sec;
    nanoseconds = (long long)deadline->tv_nsec - (long long)now.tv_nsec;
    milliseconds = (seconds * 1000) + (nanoseconds / 1000000);
    if (milliseconds <= 0) {
        return 0;
    }
    if (milliseconds > INT32_MAX) {
        return INT32_MAX;
    }
    return (int)milliseconds;
}

int vlog_pipe_open(struct vlog_pipe* pipe)
{
    int fds[2];

    if (pipe == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(pipe, 0, sizeof(struct vlog_pipe));
    pipe->read_fd = -1;
    pipe->write_fd = -1;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0) {
        return -1;
    }
    if (__set_cloexec(fds[0]) != 0 || __set_cloexec(fds[1]) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    pipe->read_fd = fds[0];
    pipe->write_fd = fds[1];
    return 0;
}

void vlog_pipe_close(struct vlog_pipe* pipe)
{
    if (pipe == NULL) {
        return;
    }
    if (pipe->read_fd >= 0) {
        close(pipe->read_fd);
    }
    if (pipe->write_fd >= 0) {
        close(pipe->write_fd);
    }
    pipe->read_fd = -1;
    pipe->write_fd = -1;
}

int vlog_pipe_send(struct vlog_pipe* pipe, const void* buffer, size_t length)
{
    ssize_t written;

    if (pipe == NULL || buffer == NULL || length > VLOG_PIPE_MESSAGE_MAX) {
        errno = EINVAL;
        return -1;
    }

    do {
        written = send(pipe->write_fd, buffer, length, MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    if (written < 0 || (size_t)written != length) {
        return -1;
    }
    return 0;
}

int vlog_pipe_recv(struct vlog_pipe* pipe, void* buffer, size_t capacity, size_t* length, const struct timespec* deadline)
{
    struct pollfd pfd;
    ssize_t       bytesRead;
    int           status;

    if (pipe == NULL || buffer == NULL || length == NULL) {
        errno = EINVAL;
        return -1;
    }

    pfd.fd = pipe->read_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    do {
        status = poll(&pfd, 1, __deadline_to_timeout_ms(deadline));
    } while (status < 0 && errno == EINTR);
    if (status <= 0) {
        return status;
    }

    do {
        bytesRead = recv(pipe->read_fd, buffer, capacity, 0);
    } while (bytesRead < 0 && errno == EINTR);
    if (bytesRead < 0) {
        return -1;
    }

    *length = (size_t)bytesRead;
    return 1;
}

#endif