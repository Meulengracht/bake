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

#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <stdio.h>
#include <sys/un.h>

// server protocol
#include "chef_served_service_server.h"
#include "startup.h"

static const char* g_servedUnPath = "/tmp/served";

static void init_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    unlink(g_servedUnPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, g_servedUnPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

int __register_server_links(gracht_server_t* server)
{
    struct gracht_link_socket* clientLink;
    int                        code;

    gracht_link_socket_create(&clientLink);
    init_link_config(clientLink);

    code = gracht_server_add_link(server, (struct gracht_link*)clientLink);
    if (code) {
        printf("__register_server_links failed to add link: %i (%i)\n", code, errno);
    }
    return code;
}

int __init_server(gracht_server_t** serverOut)
{
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
    gracht_server_configuration_init(&serverConfiguration);
    
    code = gracht_server_create(&serverConfiguration, serverOut);
    if (code) {
        printf("__init_server: error initializing server library %i\n", errno);
        return code;
    }

    return __register_server_links(*serverOut);
}

int main(int argc, char** argv)
{
    gracht_server_t* server;
    int              code;

    code = served_startup();
    if (code) {
        return code;
    }

    code = __init_server(&server);
    if (code) {
        return code;
    }

    gracht_server_register_protocol(server, &chef_served_server_protocol);
    return gracht_server_main_loop(server);
}
