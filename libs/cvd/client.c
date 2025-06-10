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
#include <chef/cvd.h>
#include <chef/dirs.h>
#include <chef/platform.h>
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

static int __abstract_socket_size(const char* address) {
    return offsetof(struct sockaddr_un, sun_path) + strlen(address);
}

static int __local_size(const char* address) {
    // If the address starts with '@', it is an abstract socket path.
    // Abstract socket paths are not null-terminated, so we need to account for that.
    if (address[0] == '@') {
        return __abstract_socket_size(address);
    }
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_LOCAL;

    // sanitize the address length
    if (strlen(address) >= sizeof(local->sun_path)) {
        fprintf(stderr, "__configure_local: address too long for local socket: %s\n", address);
        return -1;
    }

    // handle abstract socket paths
    if (address[0] == '@') {
        local->sun_path[0] = '\0';
        strncpy(local->sun_path + 1, address + 1, sizeof(local->sun_path) - 1);
    } else {
        strncpy(local->sun_path, address, sizeof(local->sun_path));
    }
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_LOCAL;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
    return 0;
}
#elif defined(_WIN32)
#include <windows.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __local_size(const char* address) {
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
    VLOG_DEBUG("bake", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local_bind(link);
        if (status) {
            VLOG_ERROR("bake", "__init_link_config failed to configure local bind address\n");
            return status;
        }

        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("bake", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = AF_LOCAL;
        size = __local_size(config->address);

        VLOG_TRACE("bake", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_TRACE("bake", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("bake", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int bake_client_initialize(struct __bake_build_context* bctx)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    int                                code;
    VLOG_DEBUG("bake", "kitchen_client_initialize()\n");

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, &bctx->cvd_address);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &bctx->cvd_client);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(bctx->cvd_client);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(bctx->cvd_client);
        bctx->cvd_client = NULL;
        return code;
    }

    return code;
}

enum chef_status bake_client_create_container(struct __bake_build_context* bctx, struct chef_container_mount* mounts, unsigned int count)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    char*                         rootfs;
    char                          cvdid[64];
    VLOG_TRACE("bake", "bake_client_create_container()\n");
    
    rootfs = (char*)chef_dirs_rootfs(build_cache_uuid(bctx->build_cache));
    if (rootfs == NULL) {
        VLOG_ERROR("bake", "bake_client_create_container: failed to allocate memory for rootfs\n");
        return -1;
    }

    status = chef_cvd_create(
        bctx->cvd_client,
        &context,
        &(struct chef_create_parameters) {
            .type = CHEF_ROOTFS_TYPE_DEBOOTSTRAP,
            .rootfs = rootfs,
            .mounts = mounts,
            .mounts_count = (uint32_t)count
        }
    );
    free(rootfs);
    
    if (status) {
        VLOG_ERROR("bake", "bake_client_create_container failed to create client\n");
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_create_result(bctx->cvd_client, &context, &cvdid[0], sizeof(cvdid) - 1, &chstatus);
    if (chstatus == CHEF_STATUS_SUCCESS) {
        bctx->cvd_id = platform_strdup(&cvdid[0]);
        if (bctx->cvd_id == NULL) {
            VLOG_FATAL("bake", "failed to allocate memory for CVD id\n");
        }
    }
    return chstatus;
}

enum chef_status bake_client_spawn(
    struct __bake_build_context* bctx,
    const char*                  command,
    enum chef_spawn_options      options,
    unsigned int*                pidOut)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    uint8_t*                      flatenv = NULL;
    size_t                        flatenvLength = 0;
    VLOG_TRACE("bake", "bake_client_spawn(cmd=%s)\n", command);

    if (bctx->base_environment != NULL) {
        flatenv = environment_flatten(bctx->base_environment, &flatenvLength);
        if (flatenv == NULL) {
            return CHEF_STATUS_INTERNAL_ERROR;
        }
    }

    status = chef_cvd_spawn(
        bctx->cvd_client,
        &context,
        &(struct chef_spawn_parameters) {
            .container_id = bctx->cvd_id,
            .command = (char*)command,
            .options = options,
            .environment = flatenv
            /* .user = */
        }
    );
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_spawn: failed to execute %s\n", command);
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_spawn_result(bctx->cvd_client, &context, pidOut, &chstatus);
    return chstatus;
}

enum chef_status bake_client_upload(struct __bake_build_context* bctx, const char* hostPath, const char* containerPath)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_TRACE("bake", "bake_client_upload(host=%s, child=%s)\n", hostPath, containerPath);

    status = chef_cvd_upload(
        bctx->cvd_client,
        &context,
        &(struct chef_file_parameters) {
            .container_id = bctx->cvd_id,
            .source_path = (char*)hostPath,
            .destination_path = (char*)containerPath,
            .user.username = ""
        }
    );
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_upload: failed to upload %s\n", hostPath);
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_upload_result(bctx->cvd_client, &context, &chstatus);
    return chstatus;
}

enum chef_status bake_client_destroy_container(struct __bake_build_context* bctx)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_TRACE("bake", "bake_client_destroy_container()\n");

    if (bctx->cvd_id == NULL) {
        VLOG_DEBUG("bake", "bake_client_destroy_container: no container to destroy\n");
        return CHEF_STATUS_SUCCESS;
    }

    status = chef_cvd_destroy(bctx->cvd_client, &context, bctx->cvd_id);
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_destroy_container: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(bctx->cvd_client, &context, &chstatus);

    // make sure we do not retry destruction
    if (chstatus == CHEF_STATUS_SUCCESS) {
        free(bctx->cvd_id);
        bctx->cvd_id = NULL;
    }
    return chstatus;
}
