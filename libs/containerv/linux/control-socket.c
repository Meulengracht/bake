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

#include <chef/platform.h>
#include "private.h"
#include <libgen.h> // dirname
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
    __SOCKET_COMMAND_GETROOT,
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
    int fd;
    VLOG_TRACE("containerv[child]", "containerv_open_socket()\n");

    strcpy(&namesock.sun_path[0], container->runtime_dir);
    strcat(&namesock.sun_path[0], "/control");

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

static int __send_command_maybe_fds(int socket, struct sockaddr_un* to, int* fdset, int count, void* payload, size_t payloadLength)
{
    char          fdsBuffer[CMSG_SPACE(sizeof(int) * 16)];
    struct msghdr msg = { 0 };
    struct iovec  io = { 
        .iov_base = payload,
        .iov_len = payloadLength
    };
    int           status;
    VLOG_TRACE("containerv", "__send_command_maybe_fds(len=%zu)\n", payloadLength);

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    if (to != NULL) {
        msg.msg_name = to;
        msg.msg_namelen = sizeof(struct sockaddr_un);
    }
    
    if (fdset != NULL && count > 0) {
        struct cmsghdr* cmsg;

        if (count > 16) {
            VLOG_ERROR("containerv", "__send_command_maybe_fds: trying to send more than 16 descriptors is not allowed\n");
            return -1;
        }

        memset(fdsBuffer, '\0', sizeof(fdsBuffer));

        msg.msg_control = fdsBuffer;
        msg.msg_controllen = sizeof(fdsBuffer);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);
        memcpy(CMSG_DATA(cmsg), fdset, sizeof(int) * count);

        msg.msg_controllen = CMSG_SPACE(sizeof(int) * count);
    }

    status = (int)sendmsg(socket, &msg, MSG_DONTWAIT);
    if (status < 0) {
        VLOG_ERROR("containerv", "__send_command_maybe_fds: failed to send command: %i\n", status);
        return -1;
    }
    return 0;
}

static int __receive_command_maybe_fds(int socket, struct sockaddr_un* from, int* fdset, void* payload, size_t payloadLength)
{
    struct cmsghdr* cmsg;
    char            fdsBuffer[CMSG_SPACE(sizeof(int) * 16)];
    struct msghdr   msg = { 0 };
    int             status;
    struct iovec    io = { 
        .iov_base = payload,
        .iov_len = payloadLength 
    };
    VLOG_TRACE("containerv", "__receive_command_maybe_fds(len=%zu)\n", payloadLength);
    
    memset(fdsBuffer, '\0', sizeof(fdsBuffer));
    
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = fdsBuffer;
    msg.msg_controllen = sizeof(fdsBuffer);
    if (from != NULL) {
        msg.msg_name = from;
        msg.msg_namelen = sizeof(struct sockaddr_un);
    }

    status = (int)recvmsg(socket, &msg, MSG_WAITALL);
    if (status < 0) {
        VLOG_ERROR("containerv", "__receive_command_maybe_fds: failed to retrieve file-descriptors\n");
        return -1;
    }
    VLOG_TRACE("containerv", "__receive_command_maybe_fds: recvmsg: %i\n", status);
    
    if (fdset != NULL && CMSG_FIRSTHDR(&msg) != NULL) {
        cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg->cmsg_level != SOL_SOCKET) {
            VLOG_ERROR("containerv", "__receive_command_maybe_fds: invalid cmsg level\n");
        }
        if (cmsg->cmsg_type != SCM_RIGHTS) {
            VLOG_ERROR("containerv", "__receive_command_maybe_fds: invalid cmsg type\n");
        }
        memcpy(fdset, CMSG_DATA(cmsg), (cmsg->cmsg_len - sizeof(struct cmsghdr)));
        return (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
    }
    return 0;
}

static void __handle_getroot_command(struct containerv_container* container, struct sockaddr_un* from)
{
    VLOG_TRACE("containerv[child]", "__handle_getroot_command()\n");

    if (__send_command_maybe_fds(container->socket_fd, from, NULL, 0, container->rootfs, strlen(container->rootfs) + 1)) {
        VLOG_ERROR("containerv[child]", "__handle_getroot_command: failed to send response\n");
    }
}

static void __handle_getfds_command(struct containerv_container* container, struct sockaddr_un* from)
{
    int                      fds[CV_NS_COUNT];
    struct __socket_response response = {
        .type = __SOCKET_COMMAND_GETFDS,
        .data.getfds = {
            .count = 0,
            .types = { 0 }
        }
    };
    VLOG_TRACE("containerv[child]", "__handle_getfds_command()\n");

    for (int i = 0, j = 0; i < CV_NS_COUNT; i++) {
        if (container->ns_fds[i] < 0) {
            continue;
        }
        response.data.getfds.types[j] = i;
        response.data.getfds.count++;
        fds[j++] = container->ns_fds[i];
    }

    if (__send_command_maybe_fds(container->socket_fd, from, &fds[0], response.data.getfds.count, &response, sizeof(struct __socket_response))) {
        VLOG_ERROR("containerv[child]", "__handle_getfds_command: failed to send response\n");
    }
}

void containerv_socket_event(struct containerv_container* container)
{
    struct __socket_command command;
    int                     status;
    struct sockaddr_un      from;
    VLOG_TRACE("containerv[child]", "containerv_socket_event()\n");

    status = __receive_command_maybe_fds(container->socket_fd, &from, NULL, &command, sizeof(struct __socket_command));
    if (status < 0) {
        VLOG_ERROR("containerv[child]", "containerv_socket_event: failed to read socket command\n");
        return;
    }

    VLOG_TRACE("containerv[child]", "containerv_socket_event: event from %s\n", &from.sun_path[0]);

    switch (command.type) {
        case __SOCKET_COMMAND_GETROOT: {
            __handle_getroot_command(container, &from);
        } break;
        case __SOCKET_COMMAND_GETFDS: {
            __handle_getfds_command(container, &from);
        } break;
    }
}

struct containerv_socket_client {
    char* socket_path;
    int   socket_fd;
};

char* __get_client_socket_name(const char* commSocket)
{
    char buffer[PATH_MAX] = { 0 };
    strcpy(&buffer[0], commSocket);
    return strpathcombine(dirname(&buffer[0]), "client"); // randomized?
}

static struct containerv_socket_client* __containerv_socket_client_new(const char* commSocket)
{
    struct containerv_socket_client* client;
    
    char* socketPath = __get_client_socket_name(commSocket);
    if (socketPath == NULL) {
        VLOG_ERROR("containerv", "__containerv_socket_client_new: failed to create client socket address\n");
        return NULL;
    }

    client = malloc(sizeof(struct containerv_socket_client));
    if (client == NULL) {
        free(socketPath);
        return NULL;
    }
    client->socket_path = socketPath;
    client->socket_fd = -1;

    return client;
}

static void __containerv_socket_client_delete(struct containerv_socket_client* client)
{
    if (client == NULL) {
        return;
    }
    free(client->socket_path);
    free(client);
}

struct containerv_socket_client* containerv_socket_client_open(const char* commSocket)
{
    struct containerv_socket_client* client;
    struct sockaddr_un namesock = {
        .sun_family = AF_UNIX,
        .sun_path = { 0 }
    };
    int status;
    VLOG_TRACE("containerv[host]", "__open_unix_socket(path=%s)\n", commSocket);

    client = __containerv_socket_client_new(commSocket);
    if (client == NULL) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to create socket client\n");
        return NULL;
    }

    client->socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (client->socket_fd < 0) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to create socket\n");
        __containerv_socket_client_delete(client);
        return NULL;
    }

    memcpy(&namesock.sun_path[0], client->socket_path, strlen(client->socket_path));
    status = bind(client->socket_fd, (struct sockaddr*)&namesock, sizeof(struct sockaddr_un));
    if (status) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to bind to %s\n", client->socket_path);
        close(client->socket_fd);
        __containerv_socket_client_delete(client);
        return NULL;
    }

    memset(&namesock.sun_path[0], 0, sizeof(namesock.sun_path));
    memcpy(&namesock.sun_path[0], commSocket, strlen(commSocket));
    status = connect(client->socket_fd, (struct sockaddr*)&namesock, sizeof(struct sockaddr_un));
    if (status) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to connect to %s\n", commSocket);
        containerv_socket_client_close(client);
        return NULL;
    }
    return client;
}

void containerv_socket_client_close(struct containerv_socket_client* client)
{
    int status;
    VLOG_TRACE("containerv[host]", "containerv_socket_client_close(client=%s)\n", client->socket_path);

    status = close(client->socket_fd);
    if (status) {
        VLOG_ERROR("containerv", "__close_unix_socket: failed to close client socket\n");
        __containerv_socket_client_delete(client);
        return;
    }
    
    status = unlink(client->socket_path);
    if (status) {
        VLOG_ERROR("containerv", "__close_unix_socket: failed to remove client socket\n");
    }
    __containerv_socket_client_delete(client);
}

int containerv_socket_client_get_root(struct containerv_socket_client* client, char* buffer, size_t length)
{
    int                     status;
    struct __socket_command command = {
        .type = __SOCKET_COMMAND_GETROOT
    };
    VLOG_TRACE("containerv[host]", "containerv_get_ns_sockets()\n");

    status = __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, &command, sizeof(struct __socket_command));
    if (status < 0) {
        return status;
    }

    status = __receive_command_maybe_fds(client->socket_fd, NULL, NULL, buffer, length);
    if (status < 0) {
        return status;
    }
    return 0;
}

int containerv_socket_client_get_nss(struct containerv_socket_client* client, struct containerv_ns_fd fds[CV_NS_COUNT], int* count)
{
    int                     status;
    int                     fdset[16];
    struct __socket_command command = {
        .type = __SOCKET_COMMAND_GETFDS
    };
    struct __socket_response response;
    VLOG_TRACE("containerv[host]", "containerv_get_ns_sockets()\n");

    status = __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, &command, sizeof(struct __socket_command));
    if (status < 0) {
        return status;
    }

    status = __receive_command_maybe_fds(client->socket_fd, NULL, &fdset[0], &response, sizeof(struct __socket_response));
    if (status < 0) {
        return status;
    }

    for (int i = 0; i < response.data.getfds.count; i++) {
        fds[i].type = response.data.getfds.types[i];
        fds[i].fd = fdset[i];
    }
    *count = response.data.getfds.count;
    return 0;
}
