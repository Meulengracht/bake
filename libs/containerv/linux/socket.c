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

static void __send_fd(int socket, int fd)
{
    struct msghdr msg = { 0 };
    char buf[CMSG_SPACE(sizeof(fd))];
    memset(buf, '\0', sizeof(buf));
    struct iovec io = { .iov_base = "ABC", .iov_len = 3 };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    *((int *) CMSG_DATA(cmsg)) = fd;

    msg.msg_controllen = CMSG_SPACE(sizeof(fd));

    if (sendmsg(socket, &msg, 0) < 0)
        err_syserr("Failed to send message\n");
}

static int __receive_fd(int socket)
{
    struct msghdr msg = {0};

    char m_buffer[256];
    struct iovec io = { .iov_base = m_buffer, .iov_len = sizeof(m_buffer) };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    char c_buffer[256];
    msg.msg_control = c_buffer;
    msg.msg_controllen = sizeof(c_buffer);

    if (recvmsg(socket, &msg, 0) < 0)
        err_syserr("Failed to receive message\n");

    struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);

    unsigned char * data = CMSG_DATA(cmsg);

    err_remark("About to extract fd\n");
    int fd = *((int*) data);
    err_remark("Extracted fd %d\n", fd);

    return fd;
}

void containerv_socket_event(int fd)
{

}
