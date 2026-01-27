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
#include <chef/containerv/bpf-manager.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <server.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <vlog.h>

#include "private.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/un.h>

static int __local_size(const char* address) {
    // If the address starts with '@', it is an abstract socket path.
    // Abstract socket paths are not null-terminated, so we need to account for that.
    if (address[0] == '@') {
        return offsetof(struct sockaddr_un, sun_path) + strlen(address);
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
        // ensure it doesn't exist
        if (unlink(address) && errno != ENOENT) {
            return -1;
        }
        strncpy(local->sun_path, address, sizeof(local->sun_path));
    }
    return 0;
}
#elif defined(_WIN32)
#include <windows.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __local_size(const char* address) {
    (void)address;
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

static void __configure_inet4(struct sockaddr_storage* storage, struct cvd_config_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct cvd_config_address* config)
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
        size = __local_size(config->address);

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
    gracht_link_socket_set_bind_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int register_server_link(gracht_server_t* server)
{
    struct cvd_config_address  apiAddress;
    struct gracht_link_socket* apiLink;

    int status;

    // get configuration stuff for links
    cvd_config_api_address(&apiAddress);

    // initialize the api link
    status = gracht_link_socket_create(&apiLink);
    if (status) {
        fprintf(stderr, "register_server_link failed to create api link: %i (%i)\n", status, errno);
        return status;
    }

    status = init_link_config(apiLink, gracht_link_packet_based, &apiAddress);
    if (status) {
        fprintf(stderr, "register_server_link failed to initialize api link: %i (%i)\n", status, errno);
        return status;
    }

    status = gracht_server_add_link(server, (struct gracht_link*)apiLink);
    if (status) {
        fprintf(stderr, "register_server_link failed to add api link: %i (%i)\n", status, errno);
        return status;
    }
    return 0;
}

static void __initialize_bpf(void)
{
    struct containerv_bpf_metrics metrics;
    int                           status;
    VLOG_TRACE("cvd", "Initializing bpf manager\n");

    status = containerv_bpf_manager_initialize();
    if (status) {
        VLOG_WARNING("cvd", "Failed to initialize bpf manager: critical startup error\n");
        VLOG_WARNING("cvd", "BPF LSM may require kernel 5.7+ with CONFIG_BPF_LSM=y and 'bpf' in LSM list\n");
        VLOG_WARNING("cvd", "Container security enforcement (BPF/seccomp) failed to initialize\n");
        return;
    }

    if (!containerv_bpf_manager_is_available()) {
        VLOG_DEBUG("cvd", "BPF LSM is not available on this system, containers will use seccomp fallback\n");
        return;
    }

    
    VLOG_TRACE("cvd", "BPF LSM enforcement is active\n");

    // Sanity-check that global enforcement is actually pinned.
    (void)containerv_bpf_manager_sanity_check_pins();

    if (containerv_bpf_manager_get_metrics(&metrics) == 0) {
        VLOG_DEBUG("cvd", 
            "BPF Policy Metrics - Containers: %d, Total Entries: %d, Capacity: %d\n",
            metrics.total_containers,
            metrics.total_policy_entries,
            metrics.max_map_capacity
        );
    }

    atexit(containerv_bpf_manager_shutdown);
}

int cvd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut)
{
    int status;
    VLOG_TRACE("cvd", "Initializing server subsystems\n");

#ifdef _WIN32
    // initialize the WSA library
    gracht_link_socket_setup();
#endif

    // Initialize BPF manager for eBPF-based security enforcement
    __initialize_bpf();

    VLOG_TRACE("cvd", "Creating gracht server handler\n");
    status = gracht_server_create(config, serverOut);
    if (status) {
        VLOG_ERROR("cvd", "error initializing server library %i\n", errno);
        return status;
    }
    return register_server_link(*serverOut);
}
