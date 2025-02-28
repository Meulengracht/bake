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

#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vlog.h>

#include "private.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/un.h>

static int __local_size(void) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;
    
    local->sun_family = AF_LOCAL;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}
#elif defined(_WIN32)
#include <windows.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __local_size(void) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_LOCAL;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}
#endif

static void __configure_inet4(struct sockaddr_storage* storage, struct cookd_config_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct cookd_config_address* config)
{
    struct sockaddr_storage addr_storage = { 0 };
    socklen_t               size;
    int                     domain = 0;
    int                     status;
    VLOG_DEBUG("cookd", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("cookd", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = AF_LOCAL;
        size = __local_size();

        VLOG_TRACE("cookd", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_TRACE("cookd", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("cookd", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int cookd_initialize_client(gracht_client_t** clientOut)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client = NULL;
    struct cookd_config_address        apiAddress;
    int                                code;
    VLOG_DEBUG("cookd", "cookd_initialize_client()\n");

    cookd_config_api_address(&apiAddress);

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("cookd", "cookd_initialize_client: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_stream_based, &apiAddress);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        VLOG_ERROR("cookd", "cookd_initialize_client: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(client);
    if (code) {
        VLOG_ERROR("cookd", "cookd_initialize_client: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(client);
        return code;
    }

    *clientOut = client;
    return code;
}
