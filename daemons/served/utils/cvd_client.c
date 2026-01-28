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

#include <chef/bits/package.h>
#include <chef/config.h>
#include <chef/environment.h>
#include <chef/platform.h>
#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <vlog.h>

#include <utils.h>

#include "chef_cvd_service_client.h"

static gracht_client_t* g_containerClient = NULL;

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
        VLOG_ERROR("served", "__configure_local: address too long for local socket: %s\n", address);
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
#include <ws2ipdef.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __local_size(const char* address) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_UNIX;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_UNIX;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
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
    VLOG_DEBUG("served", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local_bind(link);
        if (status) {
            VLOG_ERROR("served", "__init_link_config failed to configure local bind address\n");
            return status;
        }

        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("served", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = AF_LOCAL;
        size = __local_size(config->address);

        VLOG_DEBUG("served", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_DEBUG("served", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("served", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int container_client_initialize(struct chef_config_address* config)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    int                                code;
    VLOG_DEBUG("served", "container_client_initialize()\n");

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, config);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &g_containerClient);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(g_containerClient);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(g_containerClient);
        return code;
    }

    return code;
}

void container_client_shutdown(void)
{
    VLOG_DEBUG("served", "container_client_shutdown()\n");
    if (g_containerClient == NULL) {
        return;
    }
    gracht_client_shutdown(g_containerClient);
    g_containerClient = NULL;
}

static int __to_errno_code(enum chef_status status)
{
    switch (status) {
        case CHEF_STATUS_SUCCESS:
            return 0;
        case CHEF_STATUS_CONTAINER_EXISTS:
            errno = EEXIST;
            return -1;
        case CHEF_STATUS_INTERNAL_ERROR:
            errno = EFAULT;
            return -1;
        case CHEF_STATUS_FAILED_ROOTFS_SETUP:
            errno = EIO;
            return -1;
        case CHEF_STATUS_INVALID_MOUNTS:
            errno = EINVAL;
            return -1;
        case CHEF_STATUS_INVALID_CONTAINER_ID:
            errno = ENOENT;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
}

static enum chef_status __create_container(
    gracht_client_t* client,
    const char*      id,
    const char*      rootfs,
    const char*      package)
{
    struct gracht_message_context context;
    struct chef_create_parameters params;
    struct chef_layer_descriptor* layer;
    int                           status;
    enum chef_status              chstatus;
    char                          cvdid[CHEF_PACKAGE_ID_LENGTH_MAX];
    VLOG_DEBUG("served", "__create_container(id=%s)\n", id);

    chef_create_parameters_init(&params);
    params.id = (char*)id;

    chef_create_parameters_layers_add(&params, 3);
    
    // initialize the base rootfs layer, this is a layer from
    // the base package
    layer = chef_create_parameters_layers_get(&params, 0);
    layer->type = CHEF_LAYER_TYPE_VAFS_PACKAGE;
    layer->source = platform_strdup(rootfs);
    layer->target = platform_strdup("/");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // initialize the application layer, this is a layer from
    // the application package
    layer = chef_create_parameters_layers_get(&params, 1);
    layer->type = CHEF_LAYER_TYPE_VAFS_PACKAGE;
    layer->source = platform_strdup(package);
    layer->target = platform_strdup("/");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // initialize the overlay layer, this is an writable layer
    // on top of the base and application layers
    layer = chef_create_parameters_layers_get(&params, 2);
    layer->type = CHEF_LAYER_TYPE_OVERLAY;
    
    status = chef_cvd_create(client, &context, &params);
    if (status) {
        chef_create_parameters_destroy(&params);
        VLOG_ERROR("served", "__create_container failed to create client\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_create_result(client, &context, &cvdid[0], sizeof(cvdid) - 1, &chstatus);
    chef_create_parameters_destroy(&params);
    return chstatus;
}

int container_client_create_container(struct container_options* options)
{
    VLOG_DEBUG("served", "container_client_create_container(id=%s, rootfs=%s)\n", options->id, options->rootfs);
    return __to_errno_code(__create_container(
        g_containerClient,
        options->id,
        options->rootfs,
        options->package
    ));
}

static enum chef_status __container_spawn(
    gracht_client_t*             client,
    const char*                  id,
    const char* const*           environment,
    const char*                  command,
    enum chef_spawn_options      options,
    unsigned int*                pidOut)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    uint8_t*                      flatenv = NULL;
    size_t                        flatenvLength = 0;
    VLOG_DEBUG("served", "__container_spawn(cmd=%s)\n", command);

    if (environment != NULL) {
        flatenv = environment_flatten(environment, &flatenvLength);
        if (flatenv == NULL) {
            return CHEF_STATUS_INTERNAL_ERROR;
        }
    }

    status = chef_cvd_spawn(
        client,
        &context,
        &(struct chef_spawn_parameters) {
            .container_id = (char*)id,
            .command = (char*)command,
            .options = options,
            .environment = flatenv,
            .environment_count = flatenvLength
            /* .user = */
        }
    );
    if (status != 0) {
        VLOG_ERROR("served", "__container_spawn: failed to execute %s\n", command);
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_spawn_result(client, &context, pidOut, &chstatus);
    return chstatus;
}

int container_client_spawn(
    const char*        id,
    const char* const* environment,
    const char*        command,
    unsigned int*      pidOut)
{
    VLOG_DEBUG("served", "container_client_spawn(id=%s, cmd=%s)\n", id, command);
    return __to_errno_code(__container_spawn(
        g_containerClient,
        id,
        environment,
        command,
        0,
        pidOut
    ));
}

enum chef_status __container_kill(
    gracht_client_t*             client,
    const char*                  id,
    unsigned int                 pid)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("served", "__container_kill()\n");

    status = chef_cvd_kill(client, &context, id, pid);
    if (status != 0) {
        VLOG_ERROR("served", "__container_kill: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_kill_result(client, &context, &chstatus);
    return chstatus;
}

int container_client_kill(const char*  id, unsigned int pid)
{
    VLOG_DEBUG("served", "container_client_kill(id=%s, pid=%u)\n", id, pid);
    return __to_errno_code(__container_kill(
        g_containerClient,
        id,
        pid
    ));
}

enum chef_status __container_destroy(
    gracht_client_t*             client,
    const char*                  id)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("served", "__container_destroy()\n");

    status = chef_cvd_destroy(client, &context, id);
    if (status != 0) {
        VLOG_ERROR("served", "__container_destroy: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(client, &context, &chstatus);
    return chstatus;
}

int container_client_destroy_container(const char* id)
{
    VLOG_DEBUG("served", "container_client_destroy_container(id=%s)\n", id);
    return __to_errno_code(__container_destroy(
        g_containerClient,
        id
    ));
}
