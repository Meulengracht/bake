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

#ifndef __CONTAINERV_WINDOWS_PRIVATE_H__
#define __CONTAINERV_WINDOWS_PRIVATE_H__

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <chef/containerv.h>
#include <chef/list.h>

// HCS (Host Compute Service) includes
#ifndef NTDDI_WIN10_RS1
#define NTDDI_WIN10_RS1 0x0A000002
#endif

// HCS API definitions (for systems that may not have the latest SDK)
#ifndef HCS_E_SERVICE_NOT_AVAILABLE
#define HCS_E_SERVICE_NOT_AVAILABLE          0x800710DD
#define HCS_E_OPERATION_NOT_SUPPORTED        0x800710DE
#define HCS_E_INVALID_STATE                  0x800710DF
#define HCS_E_UNKNOWN_MESSAGE               0x800710E0
#define HCS_E_UNSUPPORTED_PROTOCOL_VERSION  0x800710E1
#endif

#define __CONTAINER_ID_LENGTH 8

// Windows-specific network configuration
struct containerv_options_network {
    int         enable;             // whether to enable network isolation
    const char* container_ip;       // IP for container interface (e.g., "10.0.0.2")
    const char* container_netmask;  // Netmask (e.g., "255.255.255.0")
    const char* host_ip;            // IP for host-side interface (e.g., "10.0.0.1")
    const char* switch_name;        // HyperV switch name (Windows-specific)
};

// Windows VM resource configuration
struct containerv_options_vm {
    unsigned int memory_mb;         // Memory allocation in MB (default: 1024)
    unsigned int cpu_count;         // Number of vCPUs (default: 2)
    const char*  vm_generation;     // VM generation ("1" or "2", default: "2")
};

// Windows rootfs types - direct choice, no fallback
enum windows_rootfs_type {
    WINDOWS_ROOTFS_WSL_UBUNTU,      // WSL2 Ubuntu distribution
    WINDOWS_ROOTFS_WSL_DEBIAN,      // WSL2 Debian distribution  
    WINDOWS_ROOTFS_WSL_ALPINE,      // WSL2 Alpine Linux (minimal)
    WINDOWS_ROOTFS_SERVERCORE,      // Windows Server Core (~1.5GB)
    WINDOWS_ROOTFS_NANOSERVER,      // Windows Nano Server (~100MB)
    WINDOWS_ROOTFS_WINDOWSCORE,     // Full Windows (~3GB)
    WINDOWS_ROOTFS_CUSTOM           // User-provided image URL
};

// Windows rootfs configuration
struct containerv_options_rootfs {
    enum windows_rootfs_type type;                    // WSL2 or Windows native
    const char*              custom_image_url;    // For CUSTOM type
    const char*              version;             // e.g., "ltsc2022", "22.04"
    int                      enable_updates;      // Enable Windows Update/apt updates
};

// Windows resource limits using Job Objects
struct containerv_resource_limits {
    const char*                     memory_max;          // e.g., "1G", "512M", "max" for unlimited
    const char*                     cpu_percent;         // CPU percentage (1-100)
    const char*                     process_count;       // Max processes, or "max" for unlimited
    const char*                     io_bandwidth;        // I/O bandwidth limit (future)
};

// Resource usage statistics
struct containerv_resource_stats {
    uint64_t                        cpu_time_ns;         // Total CPU time in nanoseconds
    uint64_t                        memory_usage;        // Current memory usage in bytes
    uint64_t                        memory_peak;         // Peak memory usage in bytes
    uint64_t                        read_bytes;          // Total bytes read
    uint64_t                        write_bytes;         // Total bytes written
    uint64_t                        read_ops;            // Total read operations
    uint64_t                        write_ops;           // Total write operations
    uint32_t                        active_processes;    // Currently active processes
    uint32_t                        total_processes;     // Total processes created
};

struct containerv_options {
    enum containerv_capabilities     capabilities;
    struct containerv_layer_context* layers;
    struct containerv_policy*        policy;

    struct containerv_options_network        network;
    struct containerv_options_vm             vm;
    struct containerv_options_rootfs         rootfs;
    struct containerv_resource_limits        limits;          // Resource limits
};

struct containerv_container_process {
    struct list_item list_header;
    HANDLE           handle;
    DWORD            pid;
};

// Forward declarations for HCS types
typedef HANDLE HCS_SYSTEM;
typedef HANDLE HCS_PROCESS;
typedef HANDLE HCS_OPERATION;

// HCS callback type
typedef void (CALLBACK *HCS_OPERATION_COMPLETION)(HCS_OPERATION operation, void* context);

// HCS function pointer types (dynamically loaded)
typedef HRESULT (WINAPI *HcsCreateComputeSystem_t)(
    PCWSTR Id,
    PCWSTR Configuration, 
    HCS_OPERATION Operation,
    const SECURITY_DESCRIPTOR* SecurityDescriptor,
    HCS_SYSTEM* ComputeSystem
);

typedef HRESULT (WINAPI *HcsStartComputeSystem_t)(
    HCS_SYSTEM ComputeSystem,
    HCS_OPERATION Operation,
    PCWSTR Options
);

typedef HRESULT (WINAPI *HcsShutdownComputeSystem_t)(
    HCS_SYSTEM ComputeSystem,
    HCS_OPERATION Operation,
    PCWSTR Options
);

typedef HRESULT (WINAPI *HcsTerminateComputeSystem_t)(
    HCS_SYSTEM ComputeSystem,
    HCS_OPERATION Operation,
    PCWSTR Options
);

typedef HRESULT (WINAPI *HcsCreateProcess_t)(
    HCS_SYSTEM ComputeSystem,
    PCWSTR ProcessParameters,
    HCS_OPERATION Operation,
    const SECURITY_DESCRIPTOR* SecurityDescriptor,
    HCS_PROCESS* Process
);

typedef HRESULT (WINAPI *HcsCreateOperation_t)(
    void* Context,
    HCS_OPERATION_COMPLETION CompletionCallback,
    HCS_OPERATION* Operation
);

typedef HRESULT (WINAPI *HcsCloseOperation_t)(HCS_OPERATION Operation);
typedef HRESULT (WINAPI *HcsCloseComputeSystem_t)(HCS_SYSTEM ComputeSystem);
typedef HRESULT (WINAPI *HcsCloseProcess_t)(HCS_PROCESS Process);

// HCS function pointers (loaded at runtime)
struct hcs_api {
    HMODULE hVmCompute;
    HcsCreateComputeSystem_t    HcsCreateComputeSystem;
    HcsStartComputeSystem_t     HcsStartComputeSystem;
    HcsShutdownComputeSystem_t  HcsShutdownComputeSystem;
    HcsTerminateComputeSystem_t HcsTerminateComputeSystem;
    HcsCreateProcess_t          HcsCreateProcess;
    HcsCreateOperation_t        HcsCreateOperation;
    HcsCloseOperation_t         HcsCloseOperation;
    HcsCloseComputeSystem_t     HcsCloseComputeSystem;
    HcsCloseProcess_t           HcsCloseProcess;
};

extern struct hcs_api g_hcs;

struct containerv_container {
    // HyperV VM handle and configuration
    HCS_SYSTEM   hcs_system;
    wchar_t*     vm_id;           // Wide char container ID for HCS
    char*        rootfs;
    char*        hostname;
    
    // Process management
    struct list  processes;
    
    // Container identification
    char         id[__CONTAINER_ID_LENGTH + 1];
    char*        runtime_dir;
    
    // Communication pipes
    HANDLE       host_pipe;
    HANDLE       child_pipe;
    
    // Resource management
    HANDLE                               job_object;         // Job Object for resource limits
    struct containerv_resource_limits    resource_limits;    // Current limits configuration
    
    // VM state
    int          vm_started;

    // Runtime flags
    int          network_configured;
};

/**
 * @brief Generate a unique container ID
 */
extern void containerv_generate_id(char* buffer, size_t length);

/**
 * @brief Create runtime directory for container
 */
extern char* containerv_create_runtime_dir(void);

/**
 * @brief Internal spawn implementation
 */
struct __containerv_spawn_options {
    const char*                path;
    const char* const*         argv;
    const char* const*         envv;
    enum container_spawn_flags flags;
};

extern int __containerv_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, HANDLE* handleOut);
extern int __containerv_kill(struct containerv_container* container, HANDLE handle);
extern void __containerv_destroy(struct containerv_container* container);

/**
 * @brief Initialize HCS API by loading vmcompute.dll
 */
extern int __hcs_initialize(void);

/**
 * @brief Clean up HCS API resources
 */
extern void __hcs_cleanup(void);

/**
 * @brief Create HyperV VM configuration JSON
 */
extern wchar_t* __hcs_create_vm_config(
    struct containerv_container* container,
    struct containerv_options* options
);

/**
 * @brief Create and start HyperV VM using HCS
 */
extern int __hcs_create_vm(
    struct containerv_container* container,
    struct containerv_options* options
);

/**
 * @brief Stop and destroy HyperV VM
 */
extern int __hcs_destroy_vm(struct containerv_container* container);

/**
 * @brief Execute process inside HyperV VM
 */
extern int __hcs_create_process(
    struct containerv_container* container,
    struct __containerv_spawn_options* options,
    HCS_PROCESS* processOut
);

/**
 * @brief Wait for HCS process completion
 */
extern int __hcs_wait_process(HCS_PROCESS process, unsigned int timeout_ms);

/**
 * @brief Get HCS process exit code
 */
extern int __hcs_get_process_exit_code(HCS_PROCESS process, unsigned long* exit_code);

/**
 * @brief Windows-specific VM resource configuration
 */
extern void containerv_options_set_vm_resources(
    struct containerv_options* options,
    unsigned int               memory_mb,
    unsigned int               cpu_count
);

/**
 * @brief Windows-specific HyperV switch configuration
 */
extern void containerv_options_set_vm_switch(
    struct containerv_options* options,
    const char*                switch_name
);

/**
 * @brief Windows network management functions
 */
extern int __windows_configure_vm_network(
    struct containerv_container* container,
    struct containerv_options* options
);

extern int __windows_configure_container_network(
    struct containerv_container* container,
    struct containerv_options* options
);

extern int __windows_configure_host_network(
    struct containerv_container* container,
    struct containerv_options* options
);

extern int __windows_cleanup_network(
    struct containerv_container* container,
    struct containerv_options* options
);

/**
 * @brief Windows rootfs management functions
 */
extern int __windows_setup_rootfs(
    const char* rootfs_path,
    struct containerv_options_rootfs* options
);

extern int __windows_setup_wsl_rootfs(
    const char* rootfs_path,
    enum windows_rootfs_type type,
    const char* version
);

extern int __windows_setup_native_rootfs(
    const char* rootfs_path,
    enum windows_rootfs_type type,
    const char* version
);

extern int __windows_cleanup_rootfs(
    const char* rootfs_path,
    struct containerv_options_rootfs* options
);

extern int __windows_is_wsl_available(void);

/**
 * @brief Windows rootfs configuration API
 */
extern void containerv_options_set_rootfs_type(
    struct containerv_options* options,
    enum windows_rootfs_type type,
    const char* version
);

extern void containerv_options_set_custom_rootfs(
    struct containerv_options* options,
    const char* image_url
);

/**
 * @brief Create Windows Job Object for resource limits
 * @param container Container to create job for
 * @param limits Resource limit configuration
 * @return Job handle, or NULL on failure
 */
extern HANDLE __windows_create_job_object(
    struct containerv_container* container,
    const struct containerv_resource_limits* limits
);

/**
 * @brief Apply job object to container processes
 * @param container Container with running processes
 * @param job_handle Job object to apply
 * @return 0 on success, -1 on failure
 */
extern int __windows_apply_job_to_processes(
    struct containerv_container* container,
    HANDLE job_handle
);

/**
 * @brief Get resource usage statistics from job object
 * @param job_handle Job object to query
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
extern int __windows_get_job_statistics(
    HANDLE job_handle,
    struct containerv_resource_stats* stats
);

/**
 * @brief Cleanup job object and associated resources
 * @param job_handle Job object to cleanup
 */
extern void __windows_cleanup_job_object(HANDLE job_handle);

/**
 * @brief Setup volumes for Windows container
 * @param container Container to configure volumes for
 * @param options Container options with volume configuration
 * @return 0 on success, -1 on failure
 */
extern int __windows_setup_volumes(
    struct containerv_container* container,
    const struct containerv_options* options
);

/**
 * @brief Clean up volumes for container
 * @param container Container to clean up volumes for
 */
extern void __windows_cleanup_volumes(struct containerv_container* container);

/**
 * @brief Create a named persistent volume
 * @param name Volume name
 * @param size_mb Size in megabytes
 * @param filesystem Filesystem type
 * @return 0 on success, -1 on failure
 */
extern int containerv_volume_create(const char* name, uint64_t size_mb, const char* filesystem);

#endif //!__CONTAINERV_WINDOWS_PRIVATE_H__
