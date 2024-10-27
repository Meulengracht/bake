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

#include <chef/config.h>
#include <chef/dirs.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/un.h>

static int __local_size(void) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    // ensure it doesn't exist
    if (unlink(address) && errno != ENOENT) {
        return -1;
    }

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

    // ensure it doesn't exist
    if (unlink(address) && errno != ENOENT) {
        return -1;
    }

    local->sun_family = AF_LOCAL;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}
#endif

static void __configure_inet4(struct sockaddr_storage* storage, struct chef_config_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct chef_config_address* config)
{
    struct sockaddr_storage addr_storage = { 0 };
    socklen_t               size;
    int                     domain = 0;
    int                     status;

    if (!strcmp(config->type, "local")) {
        status = __configure_local(&addr_storage, config->address);
        if (status) {
            fprintf(stderr, "init_link_config failed to configure local link\n");
            return status;
        }
        domain = AF_LOCAL;
        size = __local_size();

        printf("listening at %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        printf("listening on %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        fprintf(stderr, "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int remote_client_create(gracht_client_t** clientOut)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client = NULL;
    struct chef_config*                config;
    struct chef_config_address         apiAddress;
    int                                code;

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        fprintf(stderr, "remote_client_create: failed to load configuration\n");
        return -1;
    }
    chef_config_remote_address(config, &apiAddress);

    code = gracht_link_socket_create(&link);
    if (code) {
        fprintf(stderr, "remote_client_create: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, &apiAddress);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        printf("remote_client_create: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(client);
    if (code) {
        printf("remote_client_create: failed to connect client %i, %i\n", errno, code);
    }

    *clientOut = client;
    return code;
}
