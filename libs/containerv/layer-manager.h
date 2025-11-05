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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef __LAYER_MANAGER_H__
#define __LAYER_MANAGER_H__

#include <chef/containerv.h>

#ifdef __linux__
// Linux OverlayFS layer management
struct linux_layer_mount;

/**
 * @brief Mount image layers using OverlayFS
 * @param layers Array of layers to mount (bottom to top order)
 * @param layer_count Number of layers
 * @param container_id Container identifier for isolation
 * @param cache_dir Base cache directory
 * @param mount_info Output mount information structure
 * @return 0 on success, -1 on failure
 */
extern int linux_mount_overlay_layers(
    const struct containerv_layer* layers,
    int layer_count,
    const char* container_id,
    const char* cache_dir,
    struct linux_layer_mount* mount_info
);

/**
 * @brief Clean up overlay mount and free resources
 * @param mount_info Mount information to clean up
 */
extern void linux_cleanup_overlay_mount(struct linux_layer_mount* mount_info);

/**
 * @brief Get the merged directory path from overlay mount
 * @param mount_info Mount information structure
 * @return Merged directory path or NULL
 */
extern const char* linux_get_merged_path(const struct linux_layer_mount* mount_info);

#endif // __linux__

#ifdef _WIN32
#include <windows.h>

// Windows VHD layer management  
struct windows_layer_mount;

/**
 * @brief Mount image layers using VHD differencing disks
 * @param layers Array of layers to mount (bottom to top order)
 * @param layer_count Number of layers  
 * @param container_id Container identifier for isolation
 * @param cache_dir Base cache directory
 * @param mount_info Output mount information structure
 * @return 0 on success, -1 on failure
 */
extern int windows_mount_vhd_layers(
    const struct containerv_layer* layers,
    int layer_count,
    const char* container_id,
    const char* cache_dir,
    struct windows_layer_mount* mount_info
);

/**
 * @brief Clean up VHD mount and free resources
 * @param mount_info Mount information to clean up  
 */
extern void windows_cleanup_vhd_mount(struct windows_layer_mount* mount_info);

/**
 * @brief Get the VHD file path for HyperV VM attachment
 * @param mount_info Mount information structure
 * @return VHD file path or NULL
 */
extern const char* windows_get_vhd_path(const struct windows_layer_mount* mount_info);

/**
 * @brief Get the mount path within the HyperV VM
 * @param mount_info Mount information structure  
 * @return VM mount path or NULL
 */
extern const char* windows_get_mount_path(const struct windows_layer_mount* mount_info);

#endif // _WIN32

#endif // __LAYER_MANAGER_H__