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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    const char* gateway_ip;         // Optional default gateway
    const char* dns;                // Optional DNS servers (space/comma/semicolon separated)
    const char* switch_name;        // HyperV switch name (Windows-specific)
};

// Windows container isolation selection (HCS container compute system)
enum windows_container_isolation {
    WINDOWS_CONTAINER_ISOLATION_PROCESS = 0,
    WINDOWS_CONTAINER_ISOLATION_HYPERV = 1
};

// Windows container type selection (HCS container compute system)
enum windows_container_type {
    WINDOWS_CONTAINER_TYPE_WINDOWS = 0,
    WINDOWS_CONTAINER_TYPE_LINUX = 1
};

struct containerv_options_windows_container {
    enum windows_container_isolation isolation;
    // Utility VM image path for Hyper-V isolated containers (schema1 HvRuntime.ImagePath).
    // If NULL, containerv may try to derive it from the base layer path.
    const char* utilityvm_path;
};

// LCOW (Linux Containers on Windows) HvRuntime configuration.
// The file fields are expected to be file names under `image_path`.
struct containerv_options_windows_lcow {
    const char* image_path;
    const char* kernel_file;
    const char* initrd_file;
    const char* boot_parameters;
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
    struct containerv_resource_limits        limits;          // Resource limits

    struct containerv_options_windows_container windows_container;
    enum windows_container_type              windows_container_type;
    struct containerv_options_windows_lcow   windows_lcow;
    const char* const*                       windows_wcow_parent_layers;
    int                                      windows_wcow_parent_layer_count;
};

struct containerv_container_process {
    struct list_item list_header;
    HANDLE           handle;
    DWORD            pid;

    // VM guest process representation when using pid1d.
    // `handle` is an opaque token owned by containerv; it is not a Win32 process handle.
    int              is_guest;
    uint64_t         guest_id;
};

// Forward declarations for HCS types
typedef HANDLE HCS_SYSTEM;
typedef HANDLE HCS_PROCESS;
typedef HANDLE HCS_OPERATION;

// Process info returned from HcsWaitForOperationResultAndProcessInfo.
// Defined here to avoid depending on a specific Windows SDK version.
typedef struct
{
    DWORD ProcessId;
    DWORD Reserved;
    HANDLE StdInput;
    HANDLE StdOutput;
    HANDLE StdError;
} HCS_PROCESS_INFORMATION;

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

// ComputeCore.dll helpers for synchronous waits
typedef HRESULT (WINAPI *HcsWaitForOperationResult_t)(
    HCS_OPERATION Operation,
    DWORD timeoutMs,
    PWSTR* resultDocument
);

typedef HRESULT (WINAPI *HcsWaitForOperationResultAndProcessInfo_t)(
    HCS_OPERATION Operation,
    DWORD timeoutMs,
    HCS_PROCESS_INFORMATION* processInformation,
    PWSTR* resultDocument
);

typedef HRESULT (WINAPI *HcsCloseOperation_t)(HCS_OPERATION Operation);
typedef HRESULT (WINAPI *HcsCloseComputeSystem_t)(HCS_SYSTEM ComputeSystem);
typedef HRESULT (WINAPI *HcsCloseProcess_t)(HCS_PROCESS Process);

// HCS function pointers (loaded at runtime)
struct hcs_api {
    HMODULE hVmCompute;
    HMODULE hComputeCore;
    HcsCreateComputeSystem_t    HcsCreateComputeSystem;
    HcsStartComputeSystem_t     HcsStartComputeSystem;
    HcsShutdownComputeSystem_t  HcsShutdownComputeSystem;
    HcsTerminateComputeSystem_t HcsTerminateComputeSystem;
    HcsCreateProcess_t          HcsCreateProcess;
    HcsCreateOperation_t        HcsCreateOperation;
    HcsCloseOperation_t         HcsCloseOperation;
    HcsCloseComputeSystem_t     HcsCloseComputeSystem;
    HcsCloseProcess_t           HcsCloseProcess;

    HcsWaitForOperationResult_t                HcsWaitForOperationResult;
    HcsWaitForOperationResultAndProcessInfo_t  HcsWaitForOperationResultAndProcessInfo;
};

extern struct hcs_api g_hcs;

struct containerv_container {
    // HCS compute system handle and configuration
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

    // Security policy (owned by container once created)
    struct containerv_policy*            policy;
    
    // VM state
    int          vm_started;

    // Runtime flags
    int          network_configured;

    // HCS container-mode networking (HNS endpoint attached to this compute system).
    char*        hns_endpoint_id;

    // Guest OS selection (used for in-VM helpers like pid1d)
    int          guest_is_windows;

    // pid1d session (legacy VM containers only)
    HCS_PROCESS  pid1d_process;
    HANDLE       pid1d_stdin;
    HANDLE       pid1d_stdout;
    HANDLE       pid1d_stderr;
    int          pid1d_started;

    // PID1 integration
    int          pid1_acquired;
};

// Windows security helpers
extern int windows_apply_job_security(HANDLE job_handle, const struct containerv_policy* policy);
extern int windows_create_secure_process_ex(
    const struct containerv_policy* policy,
    wchar_t*                        command_line,
    const wchar_t*                  current_directory,
    void*                           environment,
    PROCESS_INFORMATION*            process_info
);

/**
 * @brief Generate a unique container ID
 */
extern void containerv_generate_id(char* buffer, size_t length);

/**
 * @brief Internal spawn implementation
 */
struct __containerv_spawn_options {
    const char*                path;
    const char* const*         argv;
    const char* const*         envv;
    enum container_spawn_flags flags;

    // When true, request HCS stdio pipe handles for this process (VM path only).
    int                        create_stdio_pipes;
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
 * @brief Create and start an HCS container compute system (schema1 container config).
 *
 * @param layer_folder_path Path to the container's writable layer folder (windowsfilter container folder).
 * @param parent_layers Array of parent layer folder paths (as found in layerchain.json).
 * @param utilityvm_path UtilityVM image path (required for Hyper-V isolation).
 * @param linux_container Non-zero for Linux containers on Windows (LCOW). Not fully supported yet.
 */
extern int __hcs_create_container_system(
    struct containerv_container* container,
    struct containerv_options* options,
    const char* layer_folder_path,
    const char* const* parent_layers,
    int parent_layer_count,
    const char* utilityvm_path,
    int linux_container
);

/**
 * @brief Stop and destroy Hyper-V VM (legacy VM-backed mode)
 */
extern int __hcs_destroy_compute_system(struct containerv_container* container);

/**
 * @brief Execute process inside an HCS compute system
 */
extern int __hcs_create_process(
    struct containerv_container* container,
    struct __containerv_spawn_options* options,
    HCS_PROCESS* processOut,
    HCS_PROCESS_INFORMATION* processInfoOut
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

// HCS container compute system (WCOW/LCOW) networking.
// Best-effort: creates and attaches an HNS endpoint (typically DHCP on the selected switch).
extern int __windows_configure_hcs_container_network(
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
 * @brief Execute a command inside a VM guest via pid1d (legacy VM containers only).
 *
 * This is a thin internal wrapper used by subsystems like networking.
 */
extern int __windows_exec_in_vm_via_pid1d(
    struct containerv_container*       container,
    struct __containerv_spawn_options* options,
    int*                               exit_code_out
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
