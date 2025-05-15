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

#include <chef/environment.h>
#include <chef/kitchen.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vlog.h>

#include "chef_cvd_service_client.h"

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

static void __configure_inet4(struct sockaddr_storage* storage, struct kitchen_cvd_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct kitchen_cvd_address* config)
{
    struct sockaddr_storage addr_storage = { 0 };
    socklen_t               size;
    int                     domain = 0;
    int                     status;
    VLOG_DEBUG("kitchen", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("kitchen", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = AF_LOCAL;
        size = __local_size();

        VLOG_TRACE("kitchen", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_TRACE("kitchen", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("kitchen", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int kitchen_client_initialize(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    int                                code;
    VLOG_DEBUG("kitchen", "kitchen_client_initialize()\n");

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("kitchen", "kitchen_client_initialize: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, &options->cvd_address);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &kitchen->cvd_client);
    if (code) {
        VLOG_ERROR("kitchen", "kitchen_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(kitchen->cvd_client);
    if (code) {
        VLOG_ERROR("kitchen", "kitchen_client_initialize: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(kitchen->cvd_client);
        return code;
    }

    return code;
}

enum chef_status kitchen_client_create_container(struct kitchen* kitchen, struct chef_container_mount* mounts, unsigned int count)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;

    status = chef_cvd_create(
        kitchen->cvd_client,
        &context,
        &(struct chef_create_parameters) {
            .type = CHEF_ROOTFS_TYPE_DEBOOTSTRAP,
            .rootfs = "",
            .mounts = mounts,
            .mounts_count = (uint32_t)count
        }
    );
    if (status != 0) {
        VLOG_ERROR("kitchen", "\n", strerror(status));
        return status;
    }
    gracht_client_wait_message(kitchen->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(kitchen->cvd_client, &context, &chstatus);
    return chstatus;
}

enum chef_status kitchen_client_spawn(
    struct kitchen*         kitchen,
    const char*             command,
    enum chef_spawn_options options,
    unsigned int*           pidOut)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    uint8_t*                      flatenv = NULL;
    size_t                        flatenvLength = 0;

    if (kitchen->base_environment != NULL) {
        flatenv = environment_flatten(kitchen->base_environment, &flatenvLength);
        if (flatenv == NULL) {
            return CHEF_STATUS_INTERNAL_ERROR;
        }
    }

    status = chef_cvd_spawn(
        kitchen->cvd_client,
        &context,
        &(struct chef_spawn_parameters) {
            .container_id = kitchen->cvd_id,
            .command = command,
            .options = options,
            .environment = flatenv
            /* .user = */
        }
    );
    if (status != 0) {
        VLOG_ERROR("kitchen", "\n", strerror(status));
        return status;
    }
    gracht_client_wait_message(kitchen->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_spawn_result(kitchen->cvd_client, &context, pidOut, &chstatus);
    return chstatus;
}

enum chef_status kitchen_client_upload(struct kitchen* kitchen, const char* hostPath, const char* containerPath)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;

    status = chef_cvd_upload(
        kitchen->cvd_client,
        &context,
        &(struct chef_file_parameters) {
            .container_id = kitchen->cvd_id,
            .source_path = hostPath,
            .destination_path = containerPath,
            .user.username = ""
        }
    );
    if (status != 0) {
        VLOG_ERROR("kitchen", "\n", strerror(status));
        return status;
    }
    gracht_client_wait_message(kitchen->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_upload_result(kitchen->cvd_client, &context, &chstatus);
    return chstatus;
}

enum chef_status kitchen_client_destroy_container(struct kitchen* kitchen)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;

    status = chef_cvd_destroy(kitchen->cvd_client, &context, kitchen->cvd_id);
    if (status != 0) {
        VLOG_ERROR("kitchen", "\n", strerror(status));
        return status;
    }
    gracht_client_wait_message(kitchen->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(kitchen->cvd_client, &context, &chstatus);
    return chstatus;
}
