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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <chef/containerv.h>
#include <stdlib.h>

#include "private.h"

struct containerv_options* containerv_options_new(void)
{
    struct containerv_options* options = calloc(1, sizeof(struct containerv_options));
    if (options == NULL) {
        return NULL;
    }
    
    // Set Windows-specific defaults
    options->vm.memory_mb = 1024;          // 1GB default memory
    options->vm.cpu_count = 2;             // 2 vCPUs default  
    options->vm.vm_generation = "2";       // Generation 2 VM (UEFI)
    
    // Default rootfs: WSL Ubuntu (cross-platform compatible)
    options->rootfs.type = WINDOWS_ROOTFS_WSL_UBUNTU;
    options->rootfs.version = "22.04";     // Ubuntu 22.04 LTS
    options->rootfs.enable_updates = 1;    // Enable updates by default

    // Default to legacy VM-backed mode for compatibility.
    options->windows_runtime = WINDOWS_RUNTIME_MODE_VM;
    options->windows_container.isolation = WINDOWS_CONTAINER_ISOLATION_PROCESS;
    options->windows_container.utilityvm_path = NULL;

    // Default to WCOW when using HCS container mode.
    options->windows_container_type = WINDOWS_CONTAINER_TYPE_WINDOWS;

    // LCOW defaults: unset (caller must configure).
    options->windows_lcow.image_path = NULL;
    options->windows_lcow.kernel_file = NULL;
    options->windows_lcow.initrd_file = NULL;
    options->windows_lcow.boot_parameters = NULL;
    
    return options;
}

void containerv_options_delete(struct containerv_options* options)
{
    if (options) {
        if (options->policy) {
            containerv_policy_delete(options->policy);
        }
        free(options);
    }
}

void containerv_options_set_caps(struct containerv_options* options, enum containerv_capabilities caps)
{
    if (options) {
        options->capabilities = caps;
    }
}

void containerv_options_set_policy(struct containerv_options* options, struct containerv_policy* policy)
{
    if (options) {
        options->policy = policy;
    }
}

void containerv_options_set_layers(struct containerv_options* options, struct containerv_layer_context* layers)
{
    options->layers = layers;
}

void containerv_options_set_network(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip)
{
    containerv_options_set_network_ex(options, container_ip, container_netmask, host_ip, NULL, NULL);
}

void containerv_options_set_network_ex(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip,
    const char*                gateway_ip,
    const char*                dns)
{
    if (options) {
        options->network.enable = 1;
        options->network.container_ip = container_ip;
        options->network.container_netmask = container_netmask;
        options->network.host_ip = host_ip;
        options->network.gateway_ip = gateway_ip;
        options->network.dns = dns;

        // Use default internal switch for HyperV, can be customized later
        if (options->network.switch_name == NULL) {
            options->network.switch_name = "Default Switch";
        }
    }
}

void containerv_options_set_vm_resources(
    struct containerv_options* options,
    unsigned int               memory_mb,
    unsigned int               cpu_count)
{
    if (options) {
        if (memory_mb > 0) {
            options->vm.memory_mb = memory_mb;
        }
        if (cpu_count > 0) {
            options->vm.cpu_count = cpu_count;
        }
    }
}

void containerv_options_set_vm_switch(
    struct containerv_options* options,
    const char*                switch_name)
{
    if (options && switch_name) {
        options->network.switch_name = switch_name;
    }
}

void containerv_options_set_rootfs_type(
    struct containerv_options* options,
    enum windows_rootfs_type   type,
    const char*                version)
{
    if (options) {
        options->rootfs.type = type;
        if (version) {
            options->rootfs.version = version;
        }
        // Clear custom URL when setting standard type
        options->rootfs.custom_image_url = NULL;
    }
}

void containerv_options_set_custom_rootfs(
    struct containerv_options* options,
    const char*                image_url)
{
    if (options && image_url) {
        options->rootfs.type = WINDOWS_ROOTFS_CUSTOM;
        options->rootfs.custom_image_url = image_url;
    }
}

void containerv_options_set_resource_limits(
    struct containerv_options* options,
    const char*                memory_max,
    const char*                cpu_percent,
    const char*                process_count)
{
    if (!options) {
        return;
    }
    
    options->limits.memory_max = memory_max;
    options->limits.cpu_percent = cpu_percent;
    options->limits.process_count = process_count;
    options->limits.io_bandwidth = NULL; // Not implemented yet
}

void containerv_options_set_windows_runtime_mode(
    struct containerv_options*            options,
    enum containerv_windows_runtime_mode  mode)
{
    if (options == NULL) {
        return;
    }

    switch (mode) {
        case CV_WIN_RUNTIME_VM:
            options->windows_runtime = WINDOWS_RUNTIME_MODE_VM;
            break;
        case CV_WIN_RUNTIME_HCS_CONTAINER:
            options->windows_runtime = WINDOWS_RUNTIME_MODE_HCS_CONTAINER;
            break;
        default:
            // Ignore unknown values
            break;
    }
}

void containerv_options_set_windows_container_isolation(
    struct containerv_options*                    options,
    enum containerv_windows_container_isolation   isolation)
{
    if (options == NULL) {
        return;
    }

    switch (isolation) {
        case CV_WIN_CONTAINER_ISOLATION_PROCESS:
            options->windows_container.isolation = WINDOWS_CONTAINER_ISOLATION_PROCESS;
            break;
        case CV_WIN_CONTAINER_ISOLATION_HYPERV:
            options->windows_container.isolation = WINDOWS_CONTAINER_ISOLATION_HYPERV;
            break;
        default:
            break;
    }
}

void containerv_options_set_windows_container_utilityvm_path(
    struct containerv_options* options,
    const char*                utilityvm_path)
{
    if (options == NULL) {
        return;
    }
    options->windows_container.utilityvm_path = utilityvm_path;
}

void containerv_options_set_windows_container_type(
    struct containerv_options*              options,
    enum containerv_windows_container_type  type)
{
    if (options == NULL) {
        return;
    }

    switch (type) {
        case CV_WIN_CONTAINER_TYPE_WINDOWS:
            options->windows_container_type = WINDOWS_CONTAINER_TYPE_WINDOWS;
            break;
        case CV_WIN_CONTAINER_TYPE_LINUX:
            options->windows_container_type = WINDOWS_CONTAINER_TYPE_LINUX;
            break;
        default:
            break;
    }
}

void containerv_options_set_windows_lcow_hvruntime(
    struct containerv_options* options,
    const char*                uvm_image_path,
    const char*                kernel_file,
    const char*                initrd_file,
    const char*                boot_parameters)
{
    if (options == NULL) {
        return;
    }

    options->windows_lcow.image_path = uvm_image_path;
    options->windows_lcow.kernel_file = kernel_file;
    options->windows_lcow.initrd_file = initrd_file;
    options->windows_lcow.boot_parameters = boot_parameters;
}

void containerv_options_set_rootfs_updates(
    struct containerv_options* options,
    int                        enable_updates)
{
    if (options) {
        options->rootfs.enable_updates = enable_updates;
    }
}
