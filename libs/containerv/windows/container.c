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

#include <windows.h>
#include <wincrypt.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <chef/platform.h>
#include <chef/containerv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include "private.h"

#define MIN_REMAINING_PATH_LENGTH 20  // Minimum space needed for "containerv-XXXXXX" + null

static char* __container_create_runtime_dir(void)
{
    char template[MAX_PATH];
    char* directory;
    DWORD result;
    
    // Get temp path
    result = GetTempPathA(MAX_PATH, template);
    if (result == 0 || result > MAX_PATH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to get temp path\n");
        return NULL;
    }
    
    // Create a unique subdirectory for the container
    // strcat_s second parameter is the total buffer size, not remaining space
    size_t remaining = MAX_PATH - strlen(template);
    if (remaining < MIN_REMAINING_PATH_LENGTH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: temp path too long\n");
        return NULL;
    }
    strcat_s(template, MAX_PATH, "containerv-XXXXXX");
    
    // Use _mktemp_s to create unique name
    if (_mktemp_s(template, strlen(template) + 1) != 0) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create unique name\n");
        return NULL;
    }
    
    // Create the directory
    if (!CreateDirectoryA(template, NULL)) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create directory: %s\n", template);
        return NULL;
    }
    
    directory = _strdup(template);
    return directory;
}

void containerv_generate_id(char* buffer, size_t length)
{
    const char charset[] = "0123456789abcdef";
    HCRYPTPROV hCryptProv;
    BYTE random_bytes[__CONTAINER_ID_LENGTH / 2];  // Each byte generates 2 hex chars
    
    if (length < __CONTAINER_ID_LENGTH + 1) {
        return;
    }
    
    // Use Windows Crypto API for random generation
    if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptGenRandom(hCryptProv, sizeof(random_bytes), random_bytes)) {
            for (size_t i = 0; i < sizeof(random_bytes); i++) {
                buffer[i * 2] = charset[(random_bytes[i] >> 4) & 0x0F];
                buffer[i * 2 + 1] = charset[random_bytes[i] & 0x0F];
            }
            buffer[__CONTAINER_ID_LENGTH] = '\0';
            CryptReleaseContext(hCryptProv, 0);
            return;
        }
        CryptReleaseContext(hCryptProv, 0);
    }
    
    // If crypto API fails, use GetTickCount64 + process ID as fallback
    // This is not cryptographically secure but better than rand()
    ULONGLONG tick = GetTickCount64();
    DWORD pid = GetCurrentProcessId();
    ULONGLONG combined = (tick << 32) | pid;
    
    for (size_t i = 0; i < __CONTAINER_ID_LENGTH; i++) {
        buffer[i] = charset[(combined >> (i * 4)) & 0x0F];
    }
    buffer[__CONTAINER_ID_LENGTH] = '\0';
}

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    container->runtime_dir = __container_create_runtime_dir();
    if (container->runtime_dir == NULL) {
        free(container);
        return NULL;
    }
    
    // Create staging directory for file transfers
    char staging_path[MAX_PATH];
    sprintf_s(staging_path, sizeof(staging_path), "%s\\staging", container->runtime_dir);
    if (!CreateDirectoryA(staging_path, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            VLOG_WARNING("containerv", "failed to create staging directory: %lu\n", error);
        }
    }
    
    // Generate container ID
    containerv_generate_id(container->id, sizeof(container->id));

    // Convert container ID to wide string for HCS
    size_t id_len = strlen(container->id);
    container->vm_id = calloc(id_len + 1, sizeof(wchar_t));
    if (container->vm_id == NULL) {
        free(container->runtime_dir);
        free(container);
        return NULL;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, container->id, -1, container->vm_id, (int)id_len + 1) == 0) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    // Use container ID as hostname
    container->hostname = _strdup(container->id);
    if (container->hostname == NULL) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    container->hcs_system = NULL;
    container->host_pipe = INVALID_HANDLE_VALUE;
    container->child_pipe = INVALID_HANDLE_VALUE;
    container->vm_started = 0;
    list_init(&container->processes);

    return container;
}

static void __container_delete(struct containerv_container* container)
{
    struct list_item* i;
    
    if (!container) {
        return;
    }
    
    // Clean up processes
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;
        
        if (proc->handle != NULL) {
            CloseHandle(proc->handle);
        }
        free(proc);
    }

    if (container->hcs_system != NULL) {
        __hcs_destroy_vm(container);
    }
    
    if (container->host_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(container->host_pipe);
    }
    
    if (container->child_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(container->child_pipe);
    }
    
    free(container->vm_id);
    free(container->hostname);
    free(container->runtime_dir);
    free(container->rootfs);
    free(container);
}

int containerv_create(
    const char*                   containerId,
    struct containerv_options*    options,
    struct containerv_container** containerOut
)
{
    struct containerv_container* container;
    const char*                  rootFs;
    HRESULT hr;
    
    VLOG_DEBUG("containerv", "containerv_create(containerId=%s)\n", containerId);
    
    if (containerId == NULL || containerOut == NULL) {
        return -1;
    }
    
    container = __container_new();
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_create: failed to allocate container\n");
        return -1;
    }
    
    rootFs = containerv_layers_get_rootfs(options->layers);
    container->rootfs = _strdup(rootFs);
    if (container->rootfs == NULL) {
        __container_delete(container);
        return -1;
    }

    // Set up rootfs if it doesn't exist or if specific rootfs type is requested
    BOOL rootfs_exists = PathFileExistsA(rootFs);
    if (!rootfs_exists || (options && options->rootfs.type != WINDOWS_ROOTFS_WSL_UBUNTU)) {
        VLOG_DEBUG("containerv", "setting up rootfs at %s\n", rootFs);
        
        struct containerv_options_rootfs* rootfs_opts = options ? &options->rootfs : NULL;
        if (__windows_setup_rootfs(rootFs, rootfs_opts) != 0) {
            VLOG_ERROR("containerv", "containerv_create: failed to setup rootfs\n");
            __container_delete(container);
            return -1;
        }
        
        VLOG_DEBUG("containerv", "rootfs setup completed at %s\n", rootFs);
    } else {
        VLOG_DEBUG("containerv", "using existing rootfs at %s\n", rootFs);
    }
    
    // Initialize COM for HyperV operations
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        VLOG_ERROR("containerv", "containerv_create: failed to initialize COM: 0x%lx\n", hr);
        __container_delete(container);
        return -1;
    }
    
    // Setup VM networking before creating the VM
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (__windows_configure_vm_network(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: VM network setup encountered issues\n");
            // Don't fail container creation, network might still work
        }
    }

    // Create HyperV VM using Windows HCS (Host Compute Service) API
    if (__hcs_create_vm(container, options) != 0) {
        VLOG_ERROR("containerv", "containerv_create: failed to create HyperV VM\n");
        __container_delete(container);
        return -1;
    }
    
    // Setup resource limits using Job Objects
    if (options && (options->capabilities & CV_CAP_CGROUPS)) {
        // Copy limits configuration to container
        if (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count) {
            container->resource_limits = options->limits;
            
            // Create job object for resource limits
            container->job_object = __windows_create_job_object(container, &options->limits);
            if (!container->job_object) {
                VLOG_WARNING("containerv", "containerv_create: failed to create job object for resource limits\n");
                // Continue anyway, container will work without limits
            } else {
                VLOG_DEBUG("containerv", "containerv_create: created job object for resource limits\n");
            }
        }
    }
    
    // Setup volumes and mounts for container
    if (options && (options->capabilities & CV_CAP_FILESYSTEM)) {
        if (__windows_setup_volumes(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: volume setup encountered issues\n");
            // Continue anyway, basic filesystem might still work
        }
    }
    
    // Configure host-side networking after VM is created
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (__windows_configure_host_network(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: host network setup encountered issues\n");
            // Continue anyway, VM networking might still work
        }
    }
    
    VLOG_DEBUG("containerv", "containerv_create: created container %s with HyperV VM\n", container->id);
    
    *containerOut = container;
    return 0;
}

int __containerv_spawn(
    struct containerv_container*     container,
    struct __containerv_spawn_options* options,
    HANDLE*                          handleOut
)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL result;
    struct containerv_container_process* proc;
    char cmdline[4096];
    
    if (!container || !options || !options->path) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_spawn(path=%s)\n", options->path);
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Build command line from path and arguments
    size_t cmdline_len = strlen(options->path);
    if (cmdline_len >= sizeof(cmdline)) {
        VLOG_ERROR("containerv", "__containerv_spawn: path too long\n");
        return -1;
    }
    strcpy_s(cmdline, sizeof(cmdline), options->path);
    
    if (options->argv) {
        for (int i = 1; options->argv[i] != NULL; i++) {
            size_t arg_len = strlen(options->argv[i]);
            // Check if adding " " + argument would overflow (current + space + arg + null)
            if (cmdline_len + 1 + arg_len + 1 > sizeof(cmdline)) {
                VLOG_ERROR("containerv", "__containerv_spawn: command line too long\n");
                return -1;
            }
            strcat_s(cmdline, sizeof(cmdline), " ");
            strcat_s(cmdline, sizeof(cmdline), options->argv[i]);
            cmdline_len += 1 + arg_len;
        }
    }
    
    // Check if we have a HyperV VM to run the process in
    if (container->hcs_system != NULL) {
        // Use HCS to execute process inside the HyperV VM
        HCS_PROCESS hcs_process = NULL;
        int status = __hcs_create_process(container, options, &hcs_process);
        
        if (status != 0) {
            VLOG_ERROR("containerv", "__containerv_spawn: failed to create process in VM\n");
            return -1;
        }

        // Configure container networking on first process spawn
        // This is similar to Linux where network is configured after container start
        static int network_configured = 0;  // TODO: Make this per-container
        if (!network_configured) {
            // We would need to pass options here, but they're not available in spawn
            // In a real implementation, this would be done during container startup
            // __windows_configure_container_network(container, options);
            network_configured = 1;
            VLOG_DEBUG("containerv", "__containerv_spawn: network setup deferred (would configure here)\n");
        }

        // Add process to container's process list
        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc) {
            proc->handle = (HANDLE)hcs_process;  // Store HCS_PROCESS as HANDLE
            proc->pid = 0; // HCS processes don't have traditional PIDs
            list_add(&container->processes, &proc->list_header);
        } else {
            if (g_hcs.HcsCloseProcess) {
                g_hcs.HcsCloseProcess(hcs_process);
            }
            return -1;
        }

        if (options->flags & CV_SPAWN_WAIT) {
            VLOG_DEBUG("containerv", "__containerv_spawn: waiting for HCS process completion\n");
            int wait_status = __hcs_wait_process(hcs_process, INFINITE);
            if (wait_status != 0) {
                VLOG_WARNING("containerv", "__containerv_spawn: process wait failed: %i\n", wait_status);
            }
            
            unsigned long exit_code = 0;
            if (__hcs_get_process_exit_code(hcs_process, &exit_code) == 0) {
                VLOG_DEBUG("containerv", "__containerv_spawn: process completed with exit code: %lu\n", exit_code);
            }
        }

        if (handleOut) {
            *handleOut = (HANDLE)hcs_process;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned HCS process in VM\n");
        return 0;
    } else {
        // Fallback to host process creation (for testing/debugging)
        VLOG_WARNING("containerv", "__containerv_spawn: no HyperV VM, creating host process as fallback\n");
        
        result = CreateProcessA(
            NULL,           // Application name
            cmdline,        // Command line
            NULL,           // Process security attributes
            NULL,           // Thread security attributes
            FALSE,          // Inherit handles
            0,              // Creation flags
            NULL,           // Environment
            container->rootfs, // Current directory
            &si,            // Startup info
            &pi             // Process information
        );

        if (!result) {
            VLOG_ERROR("containerv", "__containerv_spawn: CreateProcess failed: %lu\n", GetLastError());
            return -1;
        }

        // Close thread handle, we don't need it
        CloseHandle(pi.hThread);

        // Add process to container's process list
        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc) {
            proc->handle = pi.hProcess;
            proc->pid = pi.dwProcessId;
            list_add(&container->processes, &proc->list_header);
            
            // Apply job object resource limits if configured
            if (container->job_object) {
                if (AssignProcessToJobObject(container->job_object, pi.hProcess)) {
                    VLOG_DEBUG("containerv", "__containerv_spawn: assigned process %lu to job object\n", pi.dwProcessId);
                } else {
                    VLOG_WARNING("containerv", "__containerv_spawn: failed to assign process %lu to job: %lu\n", 
                               pi.dwProcessId, GetLastError());
                }
            }
        } else {
            CloseHandle(pi.hProcess);
            return -1;
        }
        
        if (options->flags & CV_SPAWN_WAIT) {
            WaitForSingleObject(pi.hProcess, INFINITE);
        }

        if (handleOut) {
            *handleOut = pi.hProcess;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned host process %lu\n", pi.dwProcessId);
        return 0;
    }
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut
)
{
    struct __containerv_spawn_options spawn_opts = {0};
    HANDLE handle;
    int status;
    
    if (!container || !path) {
        return -1;
    }
    
    // Validate and copy path
    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= MAX_PATH) {
        VLOG_ERROR("containerv", "containerv_spawn: invalid path length\n");
        return -1;
    }
    
    spawn_opts.path = path;
    if (options) {
        spawn_opts.flags = options->flags;
        
        // TODO: Parse arguments string into argv array
        // For now, arguments are not fully supported
        if (options->arguments && strlen(options->arguments) > 0) {
            VLOG_WARNING("containerv", "containerv_spawn: argument parsing not yet implemented\n");
        }
        
        // TODO: Parse environment array
        if (options->environment) {
            VLOG_WARNING("containerv", "containerv_spawn: environment setup not yet implemented\n");
        }
    }
    
    status = __containerv_spawn(container, &spawn_opts, &handle);
    if (status == 0 && pidOut) {
        *pidOut = handle;
    }
    
    return status;
}

int __containerv_kill(struct containerv_container* container, HANDLE handle)
{
    BOOL result;
    
    if (!container || handle == NULL) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_kill(handle=%p)\n", handle);
    
    result = TerminateProcess(handle, 1);
    if (!result) {
        VLOG_ERROR("containerv", "__containerv_kill: TerminateProcess failed: %lu\n", GetLastError());
        return -1;
    }
    
    // Remove from process list
    struct list_item* i;
    for (i = container->processes.head; i != NULL; i = i->next) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->handle == handle) {
            list_remove(&container->processes, i);
            CloseHandle(proc->handle);
            free(proc);
            break;
        }
    }
    
    return 0;
}

int containerv_kill(struct containerv_container* container, process_handle_t pid)
{
    return __containerv_kill(container, pid);
}

int containerv_upload(
    struct containerv_container* container,
    const char* const*           hostPaths,
    const char* const*           containerPaths,
    int                          count
)
{
    VLOG_DEBUG("containerv", "containerv_upload(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }
    
    // Enhanced file upload using VM-aware mechanisms
    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "uploading: %s -> %s\n", hostPaths[i], containerPaths[i]);
        
        if (container->hcs_system) {
            // VM-based container: use HCS to copy files into VM
            // TODO: In a full implementation, this would:
            // 1. Use PowerShell Direct to copy files into running VM
            // 2. Or use HCS file system operations if available
            // 3. Handle VM filesystem paths correctly
            
            // For now, copy to staging area that can be mounted in VM
            char stagingPath[MAX_PATH];
            sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging\\%s", 
                     container->runtime_dir, containerPaths[i]);
            
            // Create directory structure if needed
            char* lastSlash = strrchr(stagingPath, '\\');
            if (lastSlash) {
                *lastSlash = '\0';
                SHCreateDirectoryExA(NULL, stagingPath, NULL);
                *lastSlash = '\\';
            }
            
            if (!CopyFileA(hostPaths[i], stagingPath, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to stage file %s: %lu\n",
                          hostPaths[i], GetLastError());
                return -1;
            }
            
            VLOG_DEBUG("containerv", "staged file to: %s\n", stagingPath);
            
        } else {
            // Host process container: direct file copy
            char destPath[MAX_PATH];
            size_t rootfs_len = strlen(container->rootfs);
            size_t container_path_len = strlen(containerPaths[i]);
            
            if (rootfs_len + 1 + container_path_len + 1 > MAX_PATH) {
                VLOG_ERROR("containerv", "containerv_upload: combined path too long\n");
                return -1;
            }
            
            sprintf_s(destPath, sizeof(destPath), "%s\\%s", container->rootfs, containerPaths[i]);
            
            if (!CopyFileA(hostPaths[i], destPath, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to copy %s to %s: %lu\n",
                          hostPaths[i], destPath, GetLastError());
                return -1;
            }
        }
    }
    
    return 0;
}

int containerv_download(
    struct containerv_container* container,
    const char* const*           containerPaths,
    const char* const*           hostPaths,
    int                          count
)
{
    VLOG_DEBUG("containerv", "containerv_download(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }
    
    // Enhanced file download using VM-aware mechanisms
    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "downloading: %s -> %s\n", containerPaths[i], hostPaths[i]);
        
        if (container->hcs_system) {
            // VM-based container: retrieve files from VM
            // TODO: In a full implementation, this would:
            // 1. Use PowerShell Direct to copy files out of VM
            // 2. Or use HCS file system operations if available
            // 3. Handle VM filesystem paths correctly
            
            // For now, copy from staging area
            char stagingPath[MAX_PATH];
            sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging\\%s", 
                     container->runtime_dir, containerPaths[i]);
            
            if (!CopyFileA(stagingPath, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to retrieve file %s: %lu\n",
                          stagingPath, GetLastError());
                return -1;
            }
            
            VLOG_DEBUG("containerv", "retrieved file from: %s\n", stagingPath);
            
        } else {
            // Host process container: direct file copy
            char srcPath[MAX_PATH];
            size_t rootfs_len = strlen(container->rootfs);
            size_t container_path_len = strlen(containerPaths[i]);
            
            if (rootfs_len + 1 + container_path_len + 1 > MAX_PATH) {
                VLOG_ERROR("containerv", "containerv_download: combined path too long\n");
                return -1;
            }
            
            sprintf_s(srcPath, sizeof(srcPath), "%s\\%s", container->rootfs, containerPaths[i]);
            
            if (!CopyFileA(srcPath, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to copy %s to %s: %lu\n",
                          srcPath, hostPaths[i], GetLastError());
                return -1;
            }
        }
    }
    
    return 0;
}

void __containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return;
    }
    
    VLOG_DEBUG("containerv", "__containerv_destroy(id=%s)\n", container->id);
    
    // Terminate all running processes
    struct list_item* i;
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;
        
        if (proc->handle != NULL) {
            TerminateProcess(proc->handle, 0);
            CloseHandle(proc->handle);
        }
    }
    
    // Clean up job object for resource limits
    if (container->job_object) {
        __windows_cleanup_job_object(container->job_object);
        container->job_object = NULL;
    }
    
    // Clean up volumes and mounts
    __windows_cleanup_volumes(container);
    
    // Clean up network configuration
    __windows_cleanup_network(container, NULL);  // We don't have options here, but that's OK
    
    // Shut down and delete the HyperV VM using HCS
    if (container->hcs_system) {
        __hcs_destroy_vm(container);
    }
    
    // Clean up rootfs (optional - may want to preserve for reuse)
    // Note: In production, you might want to make this configurable
    if (container->rootfs) {
        VLOG_DEBUG("containerv", "cleaning up rootfs at %s\n", container->rootfs);
        __windows_cleanup_rootfs(container->rootfs, NULL);
    }
    
    // Remove runtime directory (recursively if needed)
    if (container->runtime_dir) {
        // RemoveDirectoryA only works on empty directories
        // For a complete implementation, we should recursively delete contents first
        // or use SHFileOperationA for recursive deletion
        if (!RemoveDirectoryA(container->runtime_dir)) {
            DWORD error = GetLastError();
            if (error != ERROR_DIR_NOT_EMPTY) {
                VLOG_WARNING("containerv", "__containerv_destroy: failed to remove runtime dir: %lu\n", error);
            } else {
                VLOG_DEBUG("containerv", "__containerv_destroy: runtime dir not empty, may need manual cleanup\n");
            }
        }
    }
}

int containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return -1;
    }
    
    __containerv_destroy(container);
    __container_delete(container);
    
    return 0;
}

int containerv_join(const char* containerId)
{
    VLOG_DEBUG("containerv", "containerv_join(id=%s)\n", containerId);
    
    // TODO: Implement joining an existing container
    // This would attach to an existing HyperV VM
    
    return -1; // Not implemented
}

const char* containerv_id(struct containerv_container* container)
{
    if (!container) {
        return NULL;
    }
    return container->id;
}
