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

#define _GNU_SOURCE

#include "layer-manager.h"
#include <chef/containerv.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/mount.h>
#include <sys/wait.h>

// Linux OverlayFS layer management
struct linux_layer_mount {
    char* lower_dirs;       // Colon-separated list of lower directories
    char* upper_dir;        // Writable upper directory  
    char* work_dir;         // OverlayFS work directory
    char* merged_dir;       // Final merged directory
    char** layer_paths;     // Array of individual layer paths
    int layer_count;        // Number of layers
};

static char* extract_tar_layer(const char* tar_path, const char* extract_dir) {
    // Create extraction directory
    if (mkdir(extract_dir, 0755) != 0 && errno != EEXIST) {
        return NULL;
    }
    
    // Extract tar file using system tar command (more reliable than libtar)
    char command[PATH_MAX * 2];
    snprintf(command, sizeof(command), 
            "tar -xzf \"%s\" -C \"%s\" 2>/dev/null", tar_path, extract_dir);
    
    int ret = system(command);
    if (ret != 0) {
        return NULL;
    }
    
    return strdup(extract_dir);
}

static char* build_lower_dirs_string(char** layer_paths, int layer_count) {
    if (!layer_paths || layer_count == 0) return NULL;
    
    // Calculate total length needed
    size_t total_len = 0;
    for (int i = 0; i < layer_count; i++) {
        if (layer_paths[i]) {
            total_len += strlen(layer_paths[i]) + 1; // +1 for colon separator
        }
    }
    
    if (total_len == 0) return NULL;
    
    char* lower_dirs = malloc(total_len);
    if (!lower_dirs) return NULL;
    
    lower_dirs[0] = '\0';
    for (int i = 0; i < layer_count; i++) {
        if (layer_paths[i]) {
            if (lower_dirs[0] != '\0') {
                strcat(lower_dirs, ":");
            }
            strcat(lower_dirs, layer_paths[i]);
        }
    }
    
    return lower_dirs;
}

int linux_mount_overlay_layers(const struct containerv_layer* layers,
                              int layer_count,
                              const char* container_id,
                              const char* cache_dir,
                              struct linux_layer_mount* mount_info) {
    if (!layers || layer_count == 0 || !container_id || !cache_dir || !mount_info) {
        return -1;
    }
    
    memset(mount_info, 0, sizeof(*mount_info));
    
    // Allocate layer paths array
    mount_info->layer_paths = calloc(layer_count, sizeof(char*));
    if (!mount_info->layer_paths) {
        return -1;
    }
    mount_info->layer_count = layer_count;
    
    // Extract all layers
    char extract_base[PATH_MAX];
    snprintf(extract_base, sizeof(extract_base), "%s/layers/extracted", cache_dir);
    
    for (int i = 0; i < layer_count; i++) {
        char layer_dir[PATH_MAX];
        snprintf(layer_dir, sizeof(layer_dir), "%s/%s", extract_base, layers[i].digest + 7); // Skip "sha256:"
        
        // Check if layer is already extracted
        struct stat st;
        if (stat(layer_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            mount_info->layer_paths[i] = strdup(layer_dir);
        } else if (layers[i].cache_path) {
            // Extract the layer
            mount_info->layer_paths[i] = extract_tar_layer(layers[i].cache_path, layer_dir);
        }
        
        if (!mount_info->layer_paths[i]) {
            linux_cleanup_overlay_mount(mount_info);
            return -1;
        }
    }
    
    // Create container-specific directories
    char container_base[PATH_MAX];
    snprintf(container_base, sizeof(container_base), "%s/containers/%s", cache_dir, container_id);
    
    char upper_dir[PATH_MAX];
    char work_dir[PATH_MAX]; 
    char merged_dir[PATH_MAX];
    
    snprintf(upper_dir, sizeof(upper_dir), "%s/upper", container_base);
    snprintf(work_dir, sizeof(work_dir), "%s/work", container_base);
    snprintf(merged_dir, sizeof(merged_dir), "%s/merged", container_base);
    
    // Create directories
    if (mkdir(container_base, 0755) != 0 && errno != EEXIST) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    if (mkdir(upper_dir, 0755) != 0 && errno != EEXIST) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    if (mkdir(work_dir, 0755) != 0 && errno != EEXIST) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    if (mkdir(merged_dir, 0755) != 0 && errno != EEXIST) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    // Store directory paths
    mount_info->upper_dir = strdup(upper_dir);
    mount_info->work_dir = strdup(work_dir);
    mount_info->merged_dir = strdup(merged_dir);
    
    // Build lower directories string (reverse order - bottom layer first)
    char** reversed_paths = malloc(layer_count * sizeof(char*));
    if (!reversed_paths) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    for (int i = 0; i < layer_count; i++) {
        reversed_paths[i] = mount_info->layer_paths[layer_count - 1 - i];
    }
    
    mount_info->lower_dirs = build_lower_dirs_string(reversed_paths, layer_count);
    free(reversed_paths);
    
    if (!mount_info->lower_dirs) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    // Mount overlayfs
    char mount_options[PATH_MAX * 4];
    snprintf(mount_options, sizeof(mount_options),
            "lowerdir=%s,upperdir=%s,workdir=%s,index=on,metacopy=on",
            mount_info->lower_dirs, mount_info->upper_dir, mount_info->work_dir);
    
    if (mount("overlay", mount_info->merged_dir, "overlay", MS_NOATIME, mount_options) != 0) {
        linux_cleanup_overlay_mount(mount_info);
        return -1;
    }
    
    return 0;
}

void linux_cleanup_overlay_mount(struct linux_layer_mount* mount_info) {
    if (!mount_info) return;
    
    // Unmount overlayfs
    if (mount_info->merged_dir) {
        umount2(mount_info->merged_dir, MNT_DETACH);
    }
    
    // Free allocated memory
    free(mount_info->lower_dirs);
    free(mount_info->upper_dir);
    free(mount_info->work_dir);
    free(mount_info->merged_dir);
    
    if (mount_info->layer_paths) {
        for (int i = 0; i < mount_info->layer_count; i++) {
            free(mount_info->layer_paths[i]);
        }
        free(mount_info->layer_paths);
    }
    
    memset(mount_info, 0, sizeof(*mount_info));
}

const char* linux_get_merged_path(const struct linux_layer_mount* mount_info) {
    return mount_info ? mount_info->merged_dir : NULL;
}

#endif // __linux__

#ifdef _WIN32
#include <windows.h>
#include <virtdisk.h>

// Windows VHD layer management
struct windows_layer_mount {
    HANDLE* layer_handles;      // Array of VHD handles
    int     layer_count;        // Number of layers
    HANDLE  rw_handle;          // Read-write differencing VHD
    char*   mount_path;         // Final mounted path in VM
    char*   base_vhd_path;      // Path to base VHD
    char*   diff_vhd_path;      // Path to differencing VHD
};

static int extract_vhd_layer(const char* tar_path, const char* vhd_path) {
    // For Windows, we need to:
    // 1. Extract the tar to a temporary directory
    // 2. Create a VHD file
    // 3. Mount the VHD
    // 4. Copy files to the mounted VHD
    // 5. Unmount the VHD
    
    // This is a simplified implementation - in practice, you'd want to
    // handle Windows container layer format (which may already be VHDs)
    
    char temp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_dir);
    strcat(temp_dir, "\\layer_extract");
    
    // Extract tar (assuming tar.exe is available)
    char command[MAX_PATH * 2];
    snprintf(command, sizeof(command),
            "tar -xzf \"%s\" -C \"%s\"", tar_path, temp_dir);
    
    if (system(command) != 0) {
        return -1;
    }
    
    // Create VHD (simplified - should use proper VHD creation)
    VIRTUAL_STORAGE_TYPE vst = {
        VIRTUAL_STORAGE_TYPE_DEVICE_VHD,
        VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT
    };
    
    CREATE_VIRTUAL_DISK_PARAMETERS params = {0};
    params.Version = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION;
    params.Version1.MaximumSize = 1024 * 1024 * 1024; // 1GB
    
    HANDLE vhd_handle;
    DWORD result = CreateVirtualDisk(&vst, vhd_path, VIRTUAL_DISK_ACCESS_ALL,
                                    NULL, CREATE_VIRTUAL_DISK_FLAG_NONE, 0,
                                    &params, NULL, &vhd_handle);
    
    if (result != ERROR_SUCCESS) {
        RemoveDirectoryA(temp_dir);
        return -1;
    }
    
    CloseHandle(vhd_handle);
    RemoveDirectoryA(temp_dir);
    
    return 0;
}

int windows_mount_vhd_layers(const struct containerv_layer* layers,
                            int layer_count,
                            const char* container_id,
                            const char* cache_dir,
                            struct windows_layer_mount* mount_info) {
    if (!layers || layer_count == 0 || !container_id || !cache_dir || !mount_info) {
        return -1;
    }
    
    memset(mount_info, 0, sizeof(*mount_info));
    
    // Allocate VHD handles array
    mount_info->layer_handles = calloc(layer_count, sizeof(HANDLE));
    if (!mount_info->layer_handles) {
        return -1;
    }
    mount_info->layer_count = layer_count;
    
    // Create VHDs for each layer
    char vhd_base[MAX_PATH];
    snprintf(vhd_base, sizeof(vhd_base), "%s\\layers\\vhds", cache_dir);
    CreateDirectoryA(vhd_base, NULL);
    
    for (int i = 0; i < layer_count; i++) {
        char vhd_path[MAX_PATH];
        snprintf(vhd_path, sizeof(vhd_path), "%s\\%s.vhd", 
                vhd_base, layers[i].digest + 7); // Skip "sha256:"
        
        // Check if VHD already exists
        if (GetFileAttributesA(vhd_path) == INVALID_FILE_ATTRIBUTES) {
            // Extract layer to VHD
            if (layers[i].cache_path && extract_vhd_layer(layers[i].cache_path, vhd_path) != 0) {
                windows_cleanup_vhd_mount(mount_info);
                return -1;
            }
        }
        
        // Open VHD handle
        VIRTUAL_STORAGE_TYPE vst = {
            VIRTUAL_STORAGE_TYPE_DEVICE_VHD,
            VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT
        };
        
        DWORD result = OpenVirtualDisk(&vst, vhd_path, VIRTUAL_DISK_ACCESS_READ,
                                      OPEN_VIRTUAL_DISK_FLAG_NONE, NULL,
                                      &mount_info->layer_handles[i]);
        
        if (result != ERROR_SUCCESS) {
            windows_cleanup_vhd_mount(mount_info);
            return -1;
        }
    }
    
    // Create differencing VHD for writable layer
    char container_dir[MAX_PATH];
    snprintf(container_dir, sizeof(container_dir), "%s\\containers\\%s", cache_dir, container_id);
    CreateDirectoryA(container_dir, NULL);
    
    char diff_vhd_path[MAX_PATH];
    snprintf(diff_vhd_path, sizeof(diff_vhd_path), "%s\\layer.vhd", container_dir);
    
    // Create differencing VHD based on top layer
    if (layer_count > 0) {
        char parent_vhd[MAX_PATH];
        snprintf(parent_vhd, sizeof(parent_vhd), "%s\\%s.vhd",
                vhd_base, layers[layer_count - 1].digest + 7);
        
        VIRTUAL_STORAGE_TYPE vst = {
            VIRTUAL_STORAGE_TYPE_DEVICE_VHD,
            VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT
        };
        
        CREATE_VIRTUAL_DISK_PARAMETERS params = {0};
        params.Version = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION;
        params.Version1.ParentPath = parent_vhd;
        
        DWORD result = CreateVirtualDisk(&vst, diff_vhd_path, VIRTUAL_DISK_ACCESS_ALL,
                                        NULL, CREATE_VIRTUAL_DISK_FLAG_NONE, 0,
                                        &params, NULL, &mount_info->rw_handle);
        
        if (result != ERROR_SUCCESS) {
            windows_cleanup_vhd_mount(mount_info);
            return -1;
        }
    }
    
    // Store paths for HyperV VM configuration
    mount_info->diff_vhd_path = strdup(diff_vhd_path);
    mount_info->mount_path = strdup("C:\\");  // Default mount point in VM
    
    return 0;
}

void windows_cleanup_vhd_mount(struct windows_layer_mount* mount_info) {
    if (!mount_info) return;
    
    // Close VHD handles
    if (mount_info->layer_handles) {
        for (int i = 0; i < mount_info->layer_count; i++) {
            if (mount_info->layer_handles[i] != INVALID_HANDLE_VALUE) {
                CloseHandle(mount_info->layer_handles[i]);
            }
        }
        free(mount_info->layer_handles);
    }
    
    if (mount_info->rw_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(mount_info->rw_handle);
    }
    
    free(mount_info->mount_path);
    free(mount_info->base_vhd_path);
    free(mount_info->diff_vhd_path);
    
    memset(mount_info, 0, sizeof(*mount_info));
}

const char* windows_get_vhd_path(const struct windows_layer_mount* mount_info) {
    return mount_info ? mount_info->diff_vhd_path : NULL;
}

const char* windows_get_mount_path(const struct windows_layer_mount* mount_info) {
    return mount_info ? mount_info->mount_path : NULL;
}

#endif // _WIN32