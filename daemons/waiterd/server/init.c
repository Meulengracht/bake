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

#include <chef/platform.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <server.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(__linux__)
#include <sys/un.h>

static const char* dgramPath = "/run/chef/waiterd/data";
static const char* clientsPath = "/run/chef/waiterd/listen";

static void init_packet_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    // Setup path for dgram
    unlink(dgramPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, dgramPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_packet_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

static void init_client_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    // Setup path for serverAddr
    unlink(clientsPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, clientsPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

#elif defined(_WIN32)
#include <windows.h>

static void init_packet_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(55554);

    gracht_link_socket_set_type(link, gracht_link_packet_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
    gracht_link_socket_set_listen(link, 1);
}

static void init_client_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(55555);

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
    gracht_link_socket_set_listen(link, 1);
}
#endif

void register_server_links(gracht_server_t* server)
{
    struct waiterd_config_address apiAddress;
    struct gracht_link_socket*    apiLink;

    struct gracht_link_socket*    cookLink;
    struct waiterd_config_address cookAddress;

    int status;

    // create the links based on socket impl
    gracht_link_socket_create(&apiLink);
    gracht_link_socket_create(&cookLink);

    // get the config stuff
    waiterd_config_api_address(&apiAddress);
    waiterd_config_cook_address(&cookAddress);

    // configure links
    init_client_link_config(cookLink);
    init_packet_link_config(apiLink);

    status = gracht_server_add_link(server, (struct gracht_link*)cookLink);
    if (status) {
        fprintf(stderr, "register_server_links failed to add link: %i (%i)\n", status, errno);
    }

    status = gracht_server_add_link(server, (struct gracht_link*)apiLink);
    if (status) {
        fprintf(stderr, "register_server_links failed to add link: %i (%i)\n", status, errno);
    }
}

int waiterd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut)
{
    int status;

#ifdef _WIN32
    // initialize the WSA library
    gracht_link_socket_setup();
#endif

#if defined(__linux__)
    status = platform_mkdir("/run/chef/waiterd");
    if (status) {
        fprintf(stderr, "waiterd_initialize_server: error initializing server library %i\n", errno);
        return status;
    }
#endif

    status = gracht_server_create(config, serverOut);
    if (status) {
        fprintf(stderr, "waiterd_initialize_server: error initializing server library %i\n", errno);
        return status;
    }

    // register links
    register_server_links(*serverOut);
    return 0;
}
