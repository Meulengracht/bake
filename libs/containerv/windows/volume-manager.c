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
 */

#include <vlog.h>
#include <stdio.h>
#include <string.h>
#include <shlwapi.h>
#include <virtdisk.h>

#pragma comment(lib, "virtdisk.lib")

#include "private.h"

// Default volume settings
#define WINDOWS_DEFAULT_VHD_SIZE_MB 1024    // 1GB default VHD size
#define WINDOWS_VOLUMES_DIR "containerv-volumes"

/**
 * @brief Windows volume types for containers
 */
enum windows_volume_type {
    WINDOWS_VOLUME_HOST_BIND,     // Host directory bind mount (Plan9/shared folder)
    WINDOWS_VOLUME_VHD,           // Virtual hard disk file  
    WINDOWS_VOLUME_TMPFS,         // Temporary in-memory filesystem
    WINDOWS_VOLUME_SMB_SHARE,     // Network SMB share
    WINDOWS_VOLUME_NAMED          // Named persistent volume
};

/**
 * @brief Windows volume configuration
 */
struct containerv_windows_volume {
    enum windows_volume_type type;
    char* source_path;            // Host path, VHD file, or SMB path
    char* target_path;            // Path inside container/VM
    char* volume_name;            // For named volumes
    uint64_t size_mb;            // Size for created volumes
    int read_only;               // Read-only access flag
    char* filesystem;            // NTFS, ReFS, etc.
    HANDLE vhd_handle;           // VHD handle for cleanup
};

/**
 * @brief Volume manager for Windows containers
 */
struct containerv_volume_manager {
    char* volumes_directory;      // Base directory for persistent volumes
    struct list volumes;          // List of managed volumes
    CRITICAL_SECTION lock;       // Thread safety
    int initialized;
};

static struct containerv_volume_manager g_volume_manager = {0};

/**
 * @brief Initialize the Windows volume manager
 * @return 0 on success, -1 on failure
 */
static int __windows_volume_manager_init(void)
{
    char volumes_path[MAX_PATH];
    DWORD result;
    
    if (g_volume_manager.initialized) {
        return 0;
    }
    
    VLOG_DEBUG("containerv[windows]", "initializing volume manager\n");
    
    // Get base directory for volumes (in temp directory)
    result = GetTempPathA(MAX_PATH - 32, volumes_path);
    if (result == 0 || result > MAX_PATH - 32) {
        VLOG_ERROR("containerv[windows]", "failed to get temp path for volumes\n");
        return -1;
    }
    
    // Append volumes subdirectory
    strcat_s(volumes_path, MAX_PATH, WINDOWS_VOLUMES_DIR);
    
    // Create volumes directory
    if (!CreateDirectoryA(volumes_path, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            VLOG_ERROR("containerv[windows]", "failed to create volumes directory: %lu\n", error);
            return -1;
        }
    }
    
    g_volume_manager.volumes_directory = _strdup(volumes_path);
    if (!g_volume_manager.volumes_directory) {
        return -1;
    }
    
    InitializeCriticalSection(&g_volume_manager.lock);
    list_init(&g_volume_manager.volumes);
    g_volume_manager.initialized = 1;
    
    VLOG_DEBUG("containerv[windows]", "volume manager initialized: %s\n", volumes_path);
    return 0;
}

/**
 * @brief Create a VHD file for container storage
 * @param vhd_path Path where VHD file should be created
 * @param size_mb Size in megabytes
 * @param filesystem Filesystem type (NTFS, ReFS, etc.)
 * @return VHD handle on success, INVALID_HANDLE_VALUE on failure
 */
static HANDLE __windows_create_vhd_file(const char* vhd_path, uint64_t size_mb, const char* filesystem)
{
    VIRTUAL_STORAGE_TYPE vst = {0};
    CREATE_VIRTUAL_DISK_PARAMETERS create_params = {0};
    HANDLE vhd_handle = INVALID_HANDLE_VALUE;
    DWORD result;
    wchar_t vhd_path_w[MAX_PATH];
    
    VLOG_DEBUG("containerv[windows]", "creating VHD: %s (%llu MB, %s)\n", 
              vhd_path, size_mb, filesystem ? filesystem : "NTFS");
    
    // Convert path to wide string
    if (MultiByteToWideChar(CP_UTF8, 0, vhd_path, -1, vhd_path_w, MAX_PATH) == 0) {
        VLOG_ERROR("containerv[windows]", "failed to convert VHD path to wide string\n");
        return INVALID_HANDLE_VALUE;
    }
    
    // Set virtual storage type for VHDx
    vst.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    vst.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    
    // Configure creation parameters
    create_params.Version = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION;
    create_params.Version1.MaximumSize = size_mb * 1024 * 1024; // Convert MB to bytes
    create_params.Version1.BlockSizeInBytes = 0; // Use default block size
    create_params.Version1.SectorSizeInBytes = 0; // Use default sector size
    
    // Create the VHD
    result = CreateVirtualDisk(
        &vst,
        vhd_path_w,
        VIRTUAL_DISK_ACCESS_ALL,
        NULL,                    // Security descriptor
        CREATE_VIRTUAL_DISK_FLAG_NONE,
        0,                       // Provider specific flags
        &create_params,
        NULL,                    // Overlapped
        &vhd_handle
    );
    
    if (result != ERROR_SUCCESS) {
        VLOG_ERROR("containerv[windows]", "failed to create VHD %s: %lu\n", vhd_path, result);
        return INVALID_HANDLE_VALUE;
    }
    
    VLOG_DEBUG("containerv[windows]", "VHD created successfully: %s\n", vhd_path);
    return vhd_handle;
}

/**
 * @brief Attach VHD to the system (make it available)
 * @param vhd_handle Handle to VHD file
 * @param read_only Whether to attach as read-only
 * @return 0 on success, -1 on failure
 */
static int __windows_attach_vhd(HANDLE vhd_handle, int read_only)
{
    ATTACH_VIRTUAL_DISK_PARAMETERS attach_params = {0};
    DWORD flags = ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME;
    DWORD result;
    
    if (read_only) {
        flags |= ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY;
    }
    
    attach_params.Version = ATTACH_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION;
    
    result = AttachVirtualDisk(
        vhd_handle,
        NULL,                    // Security descriptor
        flags,
        0,                       // Provider specific flags
        &attach_params,
        NULL                     // Overlapped
    );
    
    if (result != ERROR_SUCCESS) {
        VLOG_ERROR("containerv[windows]", "failed to attach VHD: %lu\n", result);
        return -1;
    }
    
    VLOG_DEBUG("containerv[windows]", "VHD attached successfully\n");
    return 0;
}

/**
 * @brief Configure HyperV shared folder for host bind mount
 * @param container Container to configure
 * @param host_path Host path to share
 * @param container_path Path inside the container/VM
 * @param readonly Read-only flag
 * @return 0 on success, -1 on failure
 */
static int __windows_configure_shared_folder(
    struct containerv_container* container,
    const char* host_path,
    const char* container_path,
    int         readonly)
{
    // For now, this is a placeholder for HyperV shared folder configuration
    // In a full implementation, this would:
    // 1. Configure HyperV Enhanced Session Mode
    // 2. Set up Plan9 filesystem sharing
    // 3. Add shared folder to HCS VM configuration
    
    VLOG_DEBUG("containerv[windows]", "configuring shared folder: %s -> %s (ro=%d)\n",
              host_path, container_path, readonly);
    
    // TODO: Implement HyperV shared folder configuration
    // This requires modifying the HCS VM configuration JSON to include:
    // "Plan9": { "Shares": [{"Name": "share_name", "Path": "host_path", "ReadOnly": false}] }
    
    return 0;
}

struct __windows_volume_iter_ctx {
    struct containerv_container* container;
    int                          status;
};

static int __windows_layers_hostdir_cb(
    const char* host_path,
    const char* container_path,
    int         readonly,
    void*       user)
{
    struct __windows_volume_iter_ctx* ctx = (struct __windows_volume_iter_ctx*)user;
    if (ctx == NULL || ctx->container == NULL) {
        return -1;
    }

    int rc = __windows_configure_shared_folder(ctx->container, host_path, container_path, readonly);
    if (rc != 0) {
        ctx->status = rc;
        return rc;
    }

    return 0;
}

/**
 * @brief Process and configure volumes for Windows container
 * @param container Container to configure volumes for
 * @param options Container options with mount configuration
 * @return 0 on success, -1 on failure
 */
int __windows_setup_volumes(
    struct containerv_container* container,
    const struct containerv_options* options)
{
    if (!options || !options->layers) {
        VLOG_DEBUG("containerv[windows]", "no layers/volumes to configure\n");
        return 0;
    }

    VLOG_DEBUG("containerv[windows]", "setting up volumes for container %s from layers\n", container->id);
    
    // Initialize volume manager if needed
    if (!g_volume_manager.initialized) {
        if (__windows_volume_manager_init() != 0) {
            return -1;
        }
    }
    
    struct __windows_volume_iter_ctx ctx = {
        .container = container,
        .status = 0,
    };

    int status = containerv_layers_iterate(
        options->layers,
        CONTAINERV_LAYER_HOST_DIRECTORY,
        __windows_layers_hostdir_cb,
        &ctx);

    if (status != 0) {
        VLOG_ERROR("containerv[windows]", "failed to configure one or more host-directory layers\n");
        return -1;
    }

    VLOG_DEBUG("containerv[windows]", "volume setup from layers completed\n");
    return 0;
}

/**
 * @brief Clean up volumes for container
 * @param container Container to clean up volumes for
 */
void __windows_cleanup_volumes(struct containerv_container* container)
{
    if (!container) {
        return;
    }
    
    VLOG_DEBUG("containerv[windows]", "cleaning up volumes for container %s\n", container->id);
    
    // TODO: Implement volume cleanup
    // 1. Detach VHDs from HyperV VM
    // 2. Close VHD handles
    // 3. Delete temporary VHD files
    // 4. Remove shared folder configurations
    
    VLOG_DEBUG("containerv[windows]", "volume cleanup completed\n");
}

/**
 * @brief Create a named persistent volume
 * @param name Volume name
 * @param size_mb Size in megabytes
 * @param filesystem Filesystem type
 * @return 0 on success, -1 on failure
 */
int containerv_volume_create(const char* name, uint64_t size_mb, const char* filesystem)
{
    char vhd_path[MAX_PATH];
    HANDLE vhd_handle;
    
    if (!name) {
        return -1;
    }
    
    VLOG_DEBUG("containerv[windows]", "creating named volume: %s (%llu MB, %s)\n", 
              name, size_mb, filesystem ? filesystem : "NTFS");
    
    // Initialize volume manager if needed
    if (!g_volume_manager.initialized) {
        if (__windows_volume_manager_init() != 0) {
            return -1;
        }
    }
    
    // Create VHD path
    snprintf(vhd_path, sizeof(vhd_path), "%s\\%s.vhdx", 
             g_volume_manager.volumes_directory, name);
    
    // Check if volume already exists
    if (PathFileExistsA(vhd_path)) {
        VLOG_ERROR("containerv[windows]", "volume %s already exists\n", name);
        return -1;
    }
    
    // Create VHD file
    vhd_handle = __windows_create_vhd_file(vhd_path, size_mb, filesystem);
    if (vhd_handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    CloseHandle(vhd_handle);
    
    VLOG_DEBUG("containerv[windows]", "named volume %s created successfully\n", name);
    return 0;
}
