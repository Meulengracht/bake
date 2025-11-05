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
    
    return options;
}

void containerv_options_delete(struct containerv_options* options)
{
    if (options) {
        if (options->mounts) {
            free(options->mounts);
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

void containerv_options_set_mounts(struct containerv_options* options, struct containerv_mount* mounts, int count)
{
    if (options) {
        options->mounts = mounts;
        options->mounts_count = count;
    }
}

void containerv_options_set_network(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip)
{
    if (options) {
        options->network.enable = 1;
        options->network.container_ip = container_ip;
        options->network.container_netmask = container_netmask;
        options->network.host_ip = host_ip;
        // Use default internal switch for HyperV, can be customized later
        options->network.switch_name = "Default Switch";
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

void containerv_options_set_rootfs_updates(
    struct containerv_options* options,
    int                        enable_updates)
{
    if (options) {
        options->rootfs.enable_updates = enable_updates;
    }
}
