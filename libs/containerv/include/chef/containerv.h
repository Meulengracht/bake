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

#ifndef __CONTAINERV_H__
#define __CONTAINERV_H__

#include <stdint.h>

#include <chef/containerv/layers.h>
#include <chef/containerv/policy.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
typedef HANDLE process_handle_t;
#elif defined(__linux__) || defined(__unix__)
#include <sys/types.h>
typedef pid_t process_handle_t;
#endif

struct containerv_options;
struct containerv_container;
struct containerv_user;

/**
 * @brief Container resource usage snapshot.
 *
 * Timestamp is a monotonic clock value in nanoseconds.
 */
struct containerv_stats {
    uint64_t timestamp;              // Timestamp in nanoseconds since epoch
    uint64_t memory_usage;           // Current memory usage in bytes
    uint64_t memory_peak;            // Peak memory usage in bytes
    uint64_t cpu_time_ns;            // Total CPU time in nanoseconds
    double   cpu_percent;            // Current CPU usage percentage
    uint64_t read_bytes;             // Total bytes read from storage
    uint64_t write_bytes;            // Total bytes written to storage
    uint64_t read_ops;               // Total read I/O operations
    uint64_t write_ops;              // Total write I/O operations
    uint64_t network_rx_bytes;       // Network bytes received
    uint64_t network_tx_bytes;       // Network bytes transmitted
    uint64_t network_rx_packets;     // Network packets received
    uint64_t network_tx_packets;     // Network packets transmitted
    uint32_t active_processes;       // Number of active processes
    uint32_t total_processes;        // Total processes created (lifetime)
};

struct containerv_process_info {
    process_handle_t pid;            // Process ID or handle
    char             name[64];       // Process name
    uint64_t         memory_kb;      // Memory usage in KB
    double           cpu_percent;    // CPU usage percentage
};

enum containerv_capabilities {
    CV_CAP_NETWORK = 0x1,
    CV_CAP_PROCESS_CONTROL = 0x2,
    CV_CAP_IPC = 0x4,
    CV_CAP_FILESYSTEM = 0x8,
    CV_CAP_CGROUPS = 0x10,
    CV_CAP_USERS = 0x20
};

extern struct containerv_options* containerv_options_new(void);
extern void containerv_options_delete(struct containerv_options* options);

extern void containerv_options_set_caps(struct containerv_options* options, enum containerv_capabilities caps);

/**
 * @brief Set the security policy for the container
 * @param options The container options to configure
 * @param policy The security policy to apply (ownership is transferred to options)
 */
extern void containerv_options_set_policy(struct containerv_options* options, struct containerv_policy* policy);

// Mount structures and flags - common across platforms
enum containerv_mount_flags {
    CV_MOUNT_BIND = 0x1,
    CV_MOUNT_RECURSIVE = 0x2,
    CV_MOUNT_READONLY = 0x4,
    CV_MOUNT_CREATE = 0x100
};

extern void containerv_options_set_layers(struct containerv_options* options, struct containerv_layer_context* layers);

/**
 * @brief Configure network isolation for the container
 *
 * On Linux this configures a virtual ethernet pair/bridge setup.
 * On Windows this configures the equivalent container networking.
 *
 * @param options The container options to configure
 * @param container_ip IP address for the container interface (e.g., "10.0.0.2")
 * @param container_netmask Netmask for the container (e.g., "255.255.255.0")
 * @param host_ip IP address for the host-side interface (e.g., "10.0.0.1")
 */
extern void containerv_options_set_network(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip
);

/**
 * @brief Configure network isolation for the container (extended)
 *
 * Adds optional gateway and DNS configuration.
 *
 * @param options The container options to configure
 * @param container_ip IP address for the container interface (e.g., "10.0.0.2")
 * @param container_netmask Netmask for the container (e.g., "255.255.255.0" or "24")
 * @param host_ip IP address for the host-side interface (e.g., "10.0.0.1")
 * @param gateway_ip Default gateway for the container/guest (optional; may be NULL)
 * @param dns Space/comma/semicolon separated DNS servers (optional; may be NULL)
 */
extern void containerv_options_set_network_ex(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip,
    const char*                gateway_ip,
    const char*                dns
);

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

// Windows privileges (subset of most critical ones)
enum containerv_windows_privilege {
    CV_PRIV_DEBUG = 0,              // Debug programs
    CV_PRIV_BACKUP = 1,             // Back up files and directories
    CV_PRIV_RESTORE = 2,            // Restore files and directories
    CV_PRIV_SHUTDOWN = 3,           // Shut down the system
    CV_PRIV_LOAD_DRIVER = 4,        // Load and unload device drivers
    CV_PRIV_SYSTEM_TIME = 5,        // Change the system time
    CV_PRIV_TAKE_OWNERSHIP = 6,     // Take ownership of files or other objects
    CV_PRIV_TCB = 7,                // Act as part of the operating system
    CV_PRIV_SECURITY = 8,           // Manage auditing and security log
    CV_PRIV_INCREASE_QUOTA = 9      // Adjust memory quotas for a process
};

// Windows container isolation mode (only meaningful for CV_WIN_RUNTIME_HCS_CONTAINER)
enum containerv_windows_container_isolation {
    // Windows Server container / process isolation
    CV_WIN_CONTAINER_ISOLATION_PROCESS = 0,
    // Hyper-V isolated container
    CV_WIN_CONTAINER_ISOLATION_HYPERV = 1
};

// Windows container type (only meaningful for CV_WIN_RUNTIME_HCS_CONTAINER)
enum containerv_windows_container_type {
    // Windows container on Windows (WCOW)
    CV_WIN_CONTAINER_TYPE_WINDOWS = 0,
    // Linux container on Windows (LCOW)
    CV_WIN_CONTAINER_TYPE_LINUX = 1
};

/**
 * @brief Select the Windows container isolation mode (process vs Hyper-V).
 */
extern void containerv_options_set_windows_container_isolation(
    struct containerv_options*                    options,
    enum containerv_windows_container_isolation   isolation
);

/**
 * @brief Set the UtilityVM image path for Hyper-V isolated Windows containers.
 *
 * This should typically point at a base image's `UtilityVM` directory.
 */
extern void containerv_options_set_windows_container_utilityvm_path(
    struct containerv_options* options,
    const char*                utilityvm_path
);

/**
 * @brief Select container type for the HCS container backend (WCOW vs LCOW).
 */
extern void containerv_options_set_windows_container_type(
    struct containerv_options*              options,
    enum containerv_windows_container_type  type
);

/**
 * @brief Configure Linux Containers on Windows (LCOW) Hyper-V runtime settings.
 *
 * These paths are interpreted as:
 * - `uvm_image_path`: host directory that contains the UVM assets.
 * - `kernel_file`/`initrd_file`: file names under `uvm_image_path`.
 * - `boot_parameters`: additional kernel cmdline parameters.
 */
extern void containerv_options_set_windows_lcow_hvruntime(
    struct containerv_options* options,
    const char*                uvm_image_path,
    const char*                kernel_file,
    const char*                initrd_file,
    const char*                boot_parameters
);

/**
 * @brief Configure WCOW parent layers (windowsfilter folders) for HCS container mode.
 */
extern void containerv_options_set_windows_wcow_parent_layers(
    struct containerv_options* options,
    const char* const*         parent_layers,
    int                        parent_layer_count
);

/**
 * @brief Configure resource limits for the Windows container using Job Objects
 * @param options The container options to configure
 * @param memory_max Maximum memory (e.g., "1G", "512M", "max" for no limit), or NULL for default (1G)
 * @param cpu_percent CPU percentage (1-100), or NULL for default (50)
 * @param process_count Maximum number of processes (e.g., "256", "max"), or NULL for default (256)
 */
extern void containerv_options_set_resource_limits(
    struct containerv_options* options,
    const char*                memory_max,
    const char*                cpu_percent,
    const char*                process_count
);

/**
 * @brief Create a named persistent volume (Windows VHD-based)
 * @param name Volume name (must be unique)
 * @param size_mb Size in megabytes (minimum 1MB)
 * @param filesystem Filesystem type ("NTFS", "ReFS", or NULL for NTFS)
 * @return 0 on success, -1 on failure
 */
extern int containerv_volume_create(
    const char* name,
    uint64_t    size_mb,
    const char* filesystem
);

#elif defined(__linux__) || defined(__unix__)
extern void containerv_options_set_users(struct containerv_options* options, uid_t hostUidStart, uid_t childUidStart, int count);
extern void containerv_options_set_groups(struct containerv_options* options, gid_t hostGidStart, gid_t childGidStart, int count);

/**
 * @brief Configure cgroup resource limits for the container
 * @param options The container options to configure
 * @param memory_max Maximum memory (e.g., "1G", "512M", "max" for no limit), or NULL for default (1G)
 * @param cpu_weight CPU weight (1-10000, default 100), or NULL for default
 * @param pids_max Maximum number of processes (e.g., "256", "max"), or NULL for default (256)
 */
extern void containerv_options_set_cgroup_limits(
    struct containerv_options* options,
    const char*                memory_max,
    const char*                cpu_weight,
    const char*                pids_max
);

#endif

/**
 * @brief Creates a new container.
 * @param containerId The unique identifier for the container.
 * @param options The container configuration options.
 * @param containerOut Pointer to receive the created container instance.
 * @return int Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int containerv_create(
    const char*                   containerId,
    struct containerv_options*    options,
    struct containerv_container** containerOut
);

enum container_spawn_flags {
    CV_SPAWN_WAIT = 0x1
};

struct containerv_spawn_options {
    const char*                arguments;
    const char* const*         environment;
    struct containerv_user*    as_user;
    enum container_spawn_flags flags;
};

extern int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut
);

extern int containerv_kill(struct containerv_container* container, process_handle_t pid);

/**
 * @brief Wait for a previously spawned process to exit and retrieve its exit code.
 *
 * On Linux, the wait is performed inside the container via the control socket.
 * On Windows, the wait uses the process/HCS handle returned from containerv_spawn.
 *
 * @param container The container that owns the process.
 * @param pid The process handle (Windows) or process id (Linux) returned from containerv_spawn.
 * @param exit_code_out Optional pointer to receive the process exit code.
 * @return 0 on success, -1 on error.
 */
extern int containerv_wait(struct containerv_container* container, process_handle_t pid, int* exit_code_out);

extern int containerv_upload(struct containerv_container* container, const char* const* hostPaths, const char* const* containerPaths, int count);

extern int containerv_download(struct containerv_container* container, const char* const* containerPaths, const char* const* hostPaths, int count);

extern int containerv_destroy(struct containerv_container* container);

/**
 * @brief Returns non-zero if the VM guest OS is Windows.
 *
 * For non-VM containers this will return 0.
 */
extern int containerv_guest_is_windows(struct containerv_container* container);

/**
 * @brief Query a best-effort resource usage snapshot for a running container.
 *
 * @param container The container to query.
 * @param stats Output stats.
 * @return 0 on success, -1 on error.
 */
extern int containerv_get_stats(struct containerv_container* container, struct containerv_stats* stats);

/**
 * @brief Get list of processes running in container
 * @param container Container to get processes for
 * @param processes Output array of process information
 * @param maxProcesses Maximum number of processes to return
 * @return Number of processes returned, or -1 on error
 */
extern int containerv_get_processes(struct containerv_container* container, struct containerv_process_info* processes, int maxProcesses);

extern int containerv_join(const char* containerId);

/**
 * @brief Returns the container ID of the given container.
 * @param container The container to get the ID from.
 * @return A read-only string containing the container ID.
 */
extern const char* containerv_id(struct containerv_container* container);

#endif //!__CONTAINERV_H__
