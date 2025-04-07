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
#include <chef/environment.h>
#include <chef/containerv-user-linux.h>
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
    __SOCKET_COMMAND_SPAWN,
    __SOCKET_COMMAND_KILL,
    __SOCKET_COMMAND_GETROOT,
    __SOCKET_COMMAND_GETFDS,
    __SOCKET_COMMAND_DESTROY,
};

struct __socket_response_spawn {
    int   status;
    pid_t process_id;
};

struct __socket_response_getfds {
    enum containerv_namespace_type types[CV_NS_COUNT];
    int                            count;
};

struct __socket_command_spawn {
    enum container_spawn_flags flags;
    uid_t                      asUid;
    gid_t                      asGid;

    // lengths include zero terminator
    size_t path_length;
    size_t argument_length;
    size_t environment_length;
};

struct __socket_command_kill {
    pid_t process_id;
};

struct __socket_command {
    enum __socket_command_type type;
    union {
        struct __socket_command_spawn  spawn;
        struct __socket_command_kill   kill;
    } data;
};

struct __socket_response {
    enum __socket_command_type type;
    union {
        struct __socket_response_spawn  spawn;
        struct __socket_response_getfds getfds;
        int                             status;
    } data;
};

int containerv_open_socket(struct containerv_container* container)
{
    struct sockaddr_un namesock = {
        .sun_family = AF_UNIX,
        .sun_path = { 0 }
    };
    int fd;
    VLOG_DEBUG("containerv[child]", "containerv_open_socket()\n");

    strcpy(&namesock.sun_path[0], container->runtime_dir);
    strcat(&namesock.sun_path[0], "/control");

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        VLOG_ERROR("containerv[child]", "containerv_open_socket: failed to create socket\n");
        return -1;
    }

    VLOG_DEBUG("containerv[child]", "listening on %s\n", &namesock.sun_path[0]);
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

static int __spawn(struct containerv_container* container, struct __socket_command* command, void* payload, pid_t* pidOut)
{
    char*  data = payload;
    char*  path;
    char** argv = NULL;
    char** envv = NULL;
    int    status; 

    // get the path
    path = data;
    data += command->data.spawn.path_length;
    
    // get the arguments if any
    if (command->data.spawn.argument_length) {
        argv = strargv(data, path, NULL);
        if (argv == NULL) {
            return -1;
        }
        data += command->data.spawn.argument_length;
    }

    if (command->data.spawn.environment_length) {
        envv = environment_unflatten(data);
        if (envv == NULL) {
            return -1;
        }
        data += command->data.spawn.environment_length;
    }

    // perform the actual execution, only the primary process here actually returns
    status = __containerv_spawn(
        container,
        &(struct __containerv_spawn_options) {
            .path = path,
            .argv = (const char* const*)argv,
            .envv = (const char* const*)envv,
            .uid = command->data.spawn.asUid,
            .gid = command->data.spawn.asGid,
            .flags = command->data.spawn.flags
        },
        pidOut
    );

    // cleanup resources temporarily allocated
    environment_destroy(envv);
    strargv_free(argv);
    return status;
}

static void __handle_spawn_command(struct containerv_container* container, struct __socket_command* command, struct sockaddr_un* from)
{
    struct __socket_response response = {
        .type = __SOCKET_COMMAND_SPAWN,
        .data.spawn = { 0 }
    };

    char*  payload;
    size_t payloadLength;
    int    status;
    VLOG_DEBUG("containerv[child]", "__handle_spawn_command()\n");

    payloadLength = command->data.spawn.path_length + command->data.spawn.argument_length + command->data.spawn.environment_length;
    if (payloadLength >= (65 * 1024)) {
        VLOG_ERROR("containerv[child]", "__handle_spawn_command: unsupported payload size %zu > %zu", payloadLength, (65 * 1024));
        response.data.spawn.status = -1;
        goto respond;
    }

    payload = calloc(1, payloadLength);
    if (payload == NULL) {
        VLOG_ERROR("containerv[child]", "__handle_spawn_command: failed to allocate memory for payload");
        response.data.spawn.status = -1;
        goto respond;
    }

    status = __receive_command_maybe_fds(container->socket_fd, from, NULL, payload, payloadLength);
    if (status < 0) {
        VLOG_ERROR("containerv[child]", "__handle_spawn_command: failed to read spawn payload\n");
        response.data.spawn.status = -1;
        goto respond;
    }

    response.data.spawn.status = __spawn(container, command, payload, &response.data.spawn.process_id);
respond:
    if (__send_command_maybe_fds(container->socket_fd, from, NULL, 0, &response, sizeof(struct __socket_response))) {
        VLOG_ERROR("containerv[child]", "__handle_spawn_command: failed to send response\n");
    }
    free(payload);
}

static void __handle_kill_command(struct containerv_container* container, pid_t processId, struct sockaddr_un* from)
{
    struct __socket_response response = {
        .type = __SOCKET_COMMAND_KILL,
    };
    VLOG_DEBUG("containerv[child]", "__handle_kill_command()\n");

    response.data.status = __containerv_kill(container, processId);
    if (__send_command_maybe_fds(container->socket_fd, from, NULL, 0, &response, sizeof(struct __socket_response))) {
        VLOG_ERROR("containerv[child]", "__handle_kill_command: failed to send response\n");
    }
}

static void __handle_destroy_command(struct containerv_container* container, struct sockaddr_un* from)
{
    VLOG_DEBUG("containerv[child]", "__handle_destroy_command()\n");
    __containerv_destroy(container);
}

static void __handle_getroot_command(struct containerv_container* container, struct sockaddr_un* from)
{
    VLOG_DEBUG("containerv[child]", "__handle_getroot_command()\n");

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
    VLOG_DEBUG("containerv[child]", "__handle_getfds_command()\n");

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

int containerv_socket_event(struct containerv_container* container)
{
    struct __socket_command command;
    int                     status;
    struct sockaddr_un      from;
    VLOG_DEBUG("containerv[child]", "containerv_socket_event()\n");

    status = __receive_command_maybe_fds(container->socket_fd, &from, NULL, &command, sizeof(struct __socket_command));
    if (status < 0) {
        VLOG_ERROR("containerv[child]", "containerv_socket_event: failed to read socket command\n");
        return -1;
    }

    VLOG_DEBUG("containerv[child]", "containerv_socket_event: event from %s\n", &from.sun_path[0]);
    switch (command.type) {
        case __SOCKET_COMMAND_SPAWN: {
            __handle_spawn_command(container, &command, &from);
        } break;
        case __SOCKET_COMMAND_KILL: {
            __handle_kill_command(container, command.data.kill.process_id, &from);
        } break;
        case __SOCKET_COMMAND_GETROOT: {
            __handle_getroot_command(container, &from);
        } break;
        case __SOCKET_COMMAND_GETFDS: {
            __handle_getfds_command(container, &from);
        } break;
        case __SOCKET_COMMAND_DESTROY: {
            __handle_destroy_command(container, &from);
            return 1;
        } break;
    }
    return 0;
}

struct containerv_socket_client {
    char* socket_path;
    int   socket_fd;
};

char* __get_client_socket_name(const char* containerId)
{
    char buffer[PATH_MAX] = { 0 };
    snprintf(&buffer[0], sizeof(buffer), __CONTAINER_SOCKET_RUNTIME_BASE "/%s/client", containerId);
    return strdup(&buffer[0]);
}

static struct containerv_socket_client* __containerv_socket_client_new(const char* containerId)
{
    struct containerv_socket_client* client;
    
    char* socketPath = __get_client_socket_name(containerId);
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

struct containerv_socket_client* containerv_socket_client_open(const char* containerId)
{
    struct containerv_socket_client* client;
    struct sockaddr_un namesock = {
        .sun_family = AF_UNIX,
        .sun_path = { 0 }
    };
    int status;
    VLOG_DEBUG("containerv[host]", "__open_unix_socket(path=%s)\n", containerId);

    client = __containerv_socket_client_new(containerId);
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
    snprintf(&namesock.sun_path[0], sizeof(namesock.sun_path), __CONTAINER_SOCKET_RUNTIME_BASE "/%s/control", containerId);
    status = connect(client->socket_fd, (struct sockaddr*)&namesock, sizeof(struct sockaddr_un));
    if (status) {
        VLOG_ERROR("containerv", "__open_unix_socket: failed to connect to %s\n", &namesock.sun_path[0]);
        containerv_socket_client_close(client);
        return NULL;
    }
    return client;
}

void containerv_socket_client_close(struct containerv_socket_client* client)
{
    int status;
    VLOG_DEBUG("containerv[host]", "containerv_socket_client_close(client=%s)\n", client->socket_path);

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

int containerv_socket_client_spawn(
    struct containerv_socket_client* client,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct __socket_command  cmd;
    struct __socket_response rsp;

    size_t dataLength = 0;
    size_t flatEnvironmentLength;
    char*  flatEnvironment = environment_flatten(options->environment, &flatEnvironmentLength);
    char*  data;
    int    dataIndex = 0;
    int    status;
    VLOG_DEBUG("containerv", "containerv_socket_client_spawn(path=%s, args=%s)\n", path, options->arguments);

    // consider length of args and env
    dataLength += strlen(path) + 1;
    dataLength += (options->arguments != NULL) ? (strlen(options->arguments) + 1) : 0;
    dataLength += (flatEnvironment != NULL) ? flatEnvironmentLength : 0;

    data = calloc(dataLength, 1);
    if (data == NULL) {
        VLOG_ERROR("containerv", "containerv_spawn: failed to allocate %zu bytes\n", dataLength);
        return -1;
    }

    cmd.type = __SOCKET_COMMAND_SPAWN;
    cmd.data.spawn.flags = options->flags;
    cmd.data.spawn.asUid = (options->as_user != NULL) ? options->as_user->uid : (uid_t)-1;
    cmd.data.spawn.asGid = (options->as_user != NULL) ? options->as_user->gid : (gid_t)-1;
    cmd.data.spawn.path_length = strlen(path) + 1;
    cmd.data.spawn.argument_length = (options->arguments != NULL) ? (strlen(options->arguments) + 1) : 0;
    cmd.data.spawn.environment_length = (flatEnvironment != NULL) ? flatEnvironmentLength : 0;

    // write path, and then skip over including zero terminator
    memcpy(&data[dataIndex], path, strlen(path));
    dataIndex += strlen(path) + 1;

    // write arguments
    if (options->arguments != NULL) {
        memcpy(&data[dataIndex], options->arguments, strlen(options->arguments));
        dataIndex += strlen(options->arguments) + 1;
    }

    // write environment
    if (flatEnvironment != NULL) {
        memcpy(&data[dataIndex], flatEnvironment, flatEnvironmentLength);
        dataIndex += flatEnvironmentLength;
        free(flatEnvironment);
    }

    status = __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, &cmd, sizeof(struct __socket_command));
    if (status < 0) {
        VLOG_ERROR("containerv", "containerv_spawn: failed to send spawn command\n");
        free(data);
        return status;
    }

    status = __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, data, dataLength);
    if (status < 0) {
        VLOG_ERROR("containerv", "containerv_spawn: failed to send spawn data\n");
        free(data);
        return status;
    }
    free(data);

    status = __receive_command_maybe_fds(client->socket_fd, NULL, NULL, &rsp, sizeof(struct __socket_response));
    if (status < 0) {
        VLOG_ERROR("containerv", "containerv_spawn: failed to receive spawn response\n");
        return status;
    }

    if (pidOut != NULL) {
        *pidOut = rsp.data.spawn.process_id;
    }
    return rsp.data.spawn.status;
}

int containerv_socket_client_kill(struct containerv_socket_client* client, pid_t processId)
{
    struct __socket_command  cmd;
    struct __socket_response rsp;
    int                      status;
    VLOG_DEBUG("containerv", "containerv_socket_client_kill()\n");

    cmd.type = __SOCKET_COMMAND_KILL;
    cmd.data.kill.process_id = processId;

    status = __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, &cmd, sizeof(struct __socket_command));
    if (status < 0) {
        return status;
    }

    status = __receive_command_maybe_fds(client->socket_fd, NULL, NULL, &rsp, sizeof(struct __socket_response));
    if (status < 0) {
        return status;
    }
    return rsp.data.status;
}

int containerv_socket_client_destroy(struct containerv_socket_client* client)
{
    struct __socket_command  cmd;
    struct __socket_response rsp;
    int                      status;
    VLOG_DEBUG("containerv", "containerv_socket_client_destroy()\n");

    cmd.type = __SOCKET_COMMAND_DESTROY;
    return __send_command_maybe_fds(client->socket_fd, NULL, NULL, 0, &cmd, sizeof(struct __socket_command));
}

int containerv_socket_client_get_root(struct containerv_socket_client* client, char* buffer, size_t length)
{
    int                     status;
    struct __socket_command command = {
        .type = __SOCKET_COMMAND_GETROOT
    };
    VLOG_DEBUG("containerv[host]", "containerv_get_ns_sockets()\n");

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
    VLOG_DEBUG("containerv[host]", "containerv_get_ns_sockets()\n");

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
