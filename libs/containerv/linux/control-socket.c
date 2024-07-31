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

#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vlog.h>

enum __socket_command_type {
    __SOCKET_COMMAND_GETFDS,
};

struct __socket_response_getfds {
    enum containerv_namespace_type types[CV_NS_COUNT];
    int                            count;
};

struct __socket_command {
    enum __socket_command_type type;
};

struct __socket_response {
    enum __socket_command_type type;
    union {
        struct __socket_response_getfds getfds;
    } data;
};

int containerv_open_socket(struct containerv_container* container)
{
    struct sockaddr_un namesock = {
        .sun_family = AF_UNIX,
        .sun_path = { 0 }
    };
    char* directory;
    int   fd;

    memcpy(&namesock.sun_path[0], "/run/containerv/c-XXXXXX", 25);
    
    directory = mkdtemp(&namesock.sun_path[0]);
    if (directory == NULL) {
        VLOG_ERROR("containerv[child]", "containerv_open_socket: failed to create: %s\n", &namesock.sun_path[0]);
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        VLOG_ERROR("containerv[child]", "containerv_open_socket: failed to create socket\n");
        return -1;
    }

    VLOG_TRACE("containerv[child]", "listening on %s\n", &namesock.sun_path[0]);
    if (bind(fd, (struct sockaddr*)&namesock, sizeof(struct sockaddr_un))) {
        VLOG_ERROR("containerv[child]", "containerv_open_socket: failed to bind socket to address %s\n", &namesock.sun_path[0]);
        return -1;
    }
    return fd;
}

static int __send_command_maybe_fds(int socket, int* fdset, int count, void* payload, size_t payloadLength)
{
    char          fdsBuffer[CMSG_SPACE(sizeof(int) * 16)];
    struct msghdr msg = { 0 };
    struct iovec  io = { 
        .iov_base = payload,
        .iov_len = payloadLength
    };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    
    if (count > 0) {
        struct cmsghdr* cmsg;
        int  i;
        memset(fdsBuffer, '\0', sizeof(fdsBuffer));

        msg.msg_control = fdsBuffer;
        msg.msg_controllen = sizeof(fdsBuffer);

        for (i = 0, cmsg = CMSG_FIRSTHDR(&msg); i < count; i++) {
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            *((int *) CMSG_DATA(cmsg)) = fdset[i];

            cmsg = CMSG_NXTHDR(&msg, cmsg);
        }
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * i);
    }

    if (sendmsg(socket, &msg, 0) < 0) {
        VLOG_ERROR("containerv[child]", "__send_fds: failed to send file-descriptors\n");
        return -1;
    }
    return 0;
}

static int __receive_command_maybe_fds(int socket, int* fdset, void* payload, size_t payloadLength)
{
    struct cmsghdr* cmsg;
    char            fdsBuffer[256];
    struct msghdr   msg = { 0 };
    int             i;
    struct iovec    io = { 
        .iov_base = payload,
        .iov_len = sizeof(payloadLength) 
    };
    
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = fdsBuffer;
    msg.msg_controllen = sizeof(fdsBuffer);
    if (recvmsg(socket, &msg, 0) < 0) {
        VLOG_ERROR("containerv[child]", "__receive_fds: failed to retrieve file-descriptors\n");
        return -1;
    }
    
    if (fdset != NULL) {
        for (i = 0, cmsg = CMSG_FIRSTHDR(&msg); cmsg && i < 16; i++) {
            unsigned char* data = CMSG_DATA(cmsg);
            fdset[i] = *((int*)data);
            cmsg = CMSG_NXTHDR(&msg, cmsg);
        }
        return i;
    }
    return 0;
}

static void __handle_getfds_command(struct containerv_container* container)
{
    int                      fds[CV_NS_COUNT];
    struct __socket_response response = {
        .type = __SOCKET_COMMAND_GETFDS,
        .data.getfds = {
            .count = 0,
            .types = { 0 }
        }
    };

    for (int i = 0, j = 0; i < CV_NS_COUNT; i++) {
        if (container->ns_fds[i] < 0) {
            continue;
        }

        response.data.getfds.types[j] = i;
        response.data.getfds.count++;
        fds[j++] = container->ns_fds[i];
    }

    if (__send_command_maybe_fds(container->socket_fd, &fds[0], response.data.getfds.count, &response, sizeof(struct __socket_response))) {
        VLOG_ERROR("containerv[child]", "__handle_getfds_command: failed to read send response\n");
    }
}

void containerv_socket_event(struct containerv_container* container)
{
    struct __socket_command command;
    int                     status;

    status = __receive_command_maybe_fds(container->socket_fd, NULL, &command, sizeof(struct __socket_command));
    if (status < 0) {
        VLOG_ERROR("containerv[child]", "containerv_socket_event: failed to read socket command\n");
        return;
    }

    switch (command.type) {
        case __SOCKET_COMMAND_GETFDS: {
            __handle_getfds_command(container);
        };
    }
}

int __open_unix_socket(const char* commSocket)
{
    struct sockaddr_un namesock = {
        .sun_family = AF_UNIX,
        .sun_path = { 0 }
    };
    int fd;
    int status;

    memcpy(&namesock.sun_path[0], commSocket, strlen(commSocket));

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to create socket\n");
        return -1;
    }

    status = connect(fd, (struct sockaddr*)&namesock, sizeof(struct sockaddr_un));
    if (status) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to connect to %s\n", commSocket);
        return status;
    }
    return fd;
}

int containerv_get_ns_sockets(const char* commSocket, struct containerv_ns_fd fds[CV_NS_COUNT], int* count)
{
    int                     status;
    int                     socket;
    int                     fdset[16];
    struct __socket_command command = {
        .type = __SOCKET_COMMAND_GETFDS
    };
    struct __socket_response response;

    socket = __open_unix_socket(commSocket);
    if (socket < 0) {
        return -1;
    }

    status = __send_command_maybe_fds(socket, NULL, 0, &command, sizeof(struct __socket_command));
    if (status < 0) {
        close(socket);
        return status;
    }

    status = __receive_command_maybe_fds(socket, &fdset[0], &response, sizeof(struct __socket_response));
    close(socket);
    if (status < 0) {
        return status;
    }

    for (int i = 0; i < status; i++) {
        fds[i].type = response.data.getfds.types[i];
        fds[i].fd = fdset[i];
    }
    *count = status;
    return 0;
}
