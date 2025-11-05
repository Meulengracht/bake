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

#ifndef __CONTAINERV_H__
#define __CONTAINERV_H__

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
typedef HANDLE process_handle_t;
#elif defined(__linux__) || defined(__unix__)
#include <sys/types.h>
typedef pid_t process_handle_t;
#endif

struct containerv_options;
struct containerv_container;
struct containerv_user;

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

// Mount structures and flags - common across platforms
enum containerv_mount_flags {
    CV_MOUNT_BIND = 0x1,
    CV_MOUNT_RECURSIVE = 0x2,
    CV_MOUNT_READONLY = 0x4,
    CV_MOUNT_CREATE = 0x100
};

struct containerv_mount {
    char*                       what;
    char*                       where;
    char*                       fstype;
    enum containerv_mount_flags flags;
};

extern void containerv_options_set_mounts(struct containerv_options* options, struct containerv_mount* mounts, int count);

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
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

/**
 * @brief Configure network isolation for the container with a virtual ethernet bridge
 * @param options The container options to configure
 * @param container_ip IP address for the container interface (e.g., "10.0.0.2")
 * @param container_netmask Netmask for the container (e.g., "255.255.255.0")
 * @param host_ip IP address for the host-side veth interface (e.g., "10.0.0.1")
 */
extern void containerv_options_set_network(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip
);
#endif

/**
 * @brief Creates a new container.
 * @param rootFs The absolute path of where the chroot root is.
 * @param capabilities
 */
extern int containerv_create(
    const char*                   rootFs,
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

extern int containerv_upload(struct containerv_container* container, const char* const* hostPaths, const char* const* containerPaths, int count);

extern int containerv_download(struct containerv_container* container, const char* const* containerPaths, const char* const* hostPaths, int count);

extern int containerv_destroy(struct containerv_container* container);

extern int containerv_join(const char* containerId);

/**
 * @brief Returns the container ID of the given container.
 * @param container The container to get the ID from.
 * @return A read-only string containing the container ID.
 */
extern const char* containerv_id(struct containerv_container* container);

// Container monitoring and statistics structures
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

/**
 * @brief Get comprehensive container statistics
 * @param container Container to get statistics for
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
extern int containerv_get_stats(struct containerv_container* container, 
                               struct containerv_stats* stats);

/**
 * @brief Get list of processes running in container
 * @param container Container to get processes for
 * @param processes Output array of process information
 * @param max_processes Maximum number of processes to return
 * @return Number of processes returned, or -1 on error
 */
extern int containerv_get_processes(struct containerv_container* container,
                                   struct containerv_process_info* processes,
                                   int max_processes);

// Container Image System - OCI-compatible image management
struct containerv_image_ref {
    char* registry;      // "docker.io", "mcr.microsoft.com", NULL for local
    char* namespace;     // "library", "windows", NULL for default  
    char* repository;    // "ubuntu", "servercore" (required)
    char* tag;          // "22.04", "ltsc2022", NULL for "latest"
    char* digest;       // "sha256:abc123..." (optional, overrides tag)
};

struct containerv_image {
    struct containerv_image_ref ref;
    char*    id;                    // Full image ID (sha256:...)
    char*    parent_id;             // Parent image ID (NULL if base)
    uint64_t size;                  // Compressed image size in bytes
    uint64_t virtual_size;          // Total size including all layers
    time_t   created;               // Creation timestamp
    char**   tags;                  // Array of tag strings
    int      tag_count;             // Number of tags
    char*    os;                    // "linux", "windows"
    char*    architecture;          // "amd64", "arm64", "386"
    char*    author;                // Image author
    char*    comment;               // Image comment/description
};

struct containerv_layer {
    char*    digest;                // Layer digest (sha256:...)
    uint64_t size;                  // Compressed layer size
    uint64_t uncompressed_size;     // Uncompressed layer size
    char*    media_type;            // Layer media type
    char*    cache_path;            // Local cache file path
    bool     available;             // Is layer available locally
    time_t   last_used;             // Last access time for GC
};

/**
 * @brief Initialize the container image system
 * @param cache_dir Directory for image cache (NULL for default: /var/lib/chef/images or C:\ProgramData\chef\images)
 * @return 0 on success, -1 on failure
 */
extern int containerv_images_init(const char* cache_dir);

/**
 * @brief Cleanup and shutdown the image system
 */
extern void containerv_images_cleanup(void);

/**
 * @brief Pull an image from a registry
 * @param image_ref Image reference to pull (registry, repository, tag required)
 * @param progress_callback Optional progress callback function
 * @param callback_data User data passed to progress callback
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_pull(
    const struct containerv_image_ref* image_ref,
    void (*progress_callback)(const char* status, int percent, void* data),
    void* callback_data
);

/**
 * @brief List locally cached images
 * @param images Output array to fill with image information
 * @param max_images Maximum number of images to return
 * @return Number of images returned, or -1 on error
 */
extern int containerv_image_list(
    struct containerv_image* images,
    int max_images
);

/**
 * @brief Get detailed information about a specific image
 * @param image_ref Image reference to inspect
 * @param image Output structure to fill with image details
 * @return 0 on success, -1 if image not found or error
 */
extern int containerv_image_inspect(
    const struct containerv_image_ref* image_ref,
    struct containerv_image* image
);

/**
 * @brief Remove an image from local cache
 * @param image_ref Image reference to remove
 * @param force Force removal even if containers are using the image
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_remove(
    const struct containerv_image_ref* image_ref,
    bool force
);

/**
 * @brief Create container from an OCI image
 * @param image_ref Image reference to create container from
 * @param options Container configuration options
 * @param container_out Output pointer to created container
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_from_image(
    const struct containerv_image_ref* image_ref,
    struct containerv_options*         options,
    struct containerv_container**      container_out
);

/**
 * @brief Set image reference for container options (alternative to rootFs path)
 * @param options Container options to configure
 * @param image_ref Image reference to use as container base
 */
extern void containerv_options_set_image(
    struct containerv_options*         options,
    const struct containerv_image_ref* image_ref
);

/**
 * @brief Get the image reference used to create a container
 * @param container Container to get image info from
 * @param image_ref Output structure to fill with image reference
 * @return 0 on success, -1 if container wasn't created from image
 */
extern int containerv_get_image(
    struct containerv_container*       container,
    struct containerv_image_ref*       image_ref
);

// Image cache management
struct containerv_cache_stats {
    uint64_t total_size;            // Current cache size in bytes
    uint64_t available_space;       // Available disk space
    int      image_count;           // Number of cached images
    int      layer_count;           // Number of cached layers
    time_t   last_gc;               // Last garbage collection time
};

/**
 * @brief Get image cache statistics
 * @param stats Output structure to fill with cache statistics
 * @return 0 on success, -1 on failure
 */
extern int containerv_cache_get_stats(struct containerv_cache_stats* stats);

/**
 * @brief Run garbage collection on image cache
 * @param force Force cleanup even if cache size is acceptable
 * @return Number of items cleaned up, or -1 on error
 */
extern int containerv_cache_gc(bool force);

/**
 * @brief Remove unused images and layers from cache
 * @param max_age_days Remove items older than this many days (0 for all unused)
 * @return Number of items pruned, or -1 on error
 */
extern int containerv_cache_prune(int max_age_days);

// Security & Sandboxing - Enhanced container isolation and privilege management
enum containerv_security_level {
    CV_SECURITY_PERMISSIVE = 0,     // Minimal restrictions for development
    CV_SECURITY_RESTRICTED = 1,     // Standard container security (default)
    CV_SECURITY_STRICT = 2,         // High security for sensitive workloads
    CV_SECURITY_PARANOID = 3        // Maximum security for untrusted code
};

// Linux capabilities (subset of most critical ones)
enum containerv_linux_capability {
    CV_CAP_CHOWN = 0,               // Change file ownership
    CV_CAP_DAC_OVERRIDE = 1,        // Bypass file permission checks
    CV_CAP_FOWNER = 3,              // Bypass permission checks on operations requiring filesystem UID match
    CV_CAP_KILL = 5,                // Bypass permission checks for sending signals
    CV_CAP_SETGID = 6,              // Make arbitrary manipulations of process GIDs
    CV_CAP_SETUID = 7,              // Make arbitrary manipulations of process UIDs
    CV_CAP_NET_BIND_SERVICE = 10,   // Bind socket to privileged ports (<1024)
    CV_CAP_NET_ADMIN = 12,          // Perform various network-related operations
    CV_CAP_NET_RAW = 13,            // Use RAW and PACKET sockets
    CV_CAP_SYS_CHROOT = 18,         // Use chroot()
    CV_CAP_SYS_PTRACE = 19,         // Trace arbitrary processes using ptrace()
    CV_CAP_SYS_ADMIN = 21,          // Perform system administration operations
    CV_CAP_SYS_MODULE = 16,         // Load and unload kernel modules
    CV_CAP_MKNOD = 27,              // Create special files using mknod()
    CV_CAP_SETFCAP = 31             // Set file capabilities
};

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

// Syscall filtering actions (Linux)
enum containerv_syscall_action {
    CV_SYSCALL_ALLOW = 0,           // Allow syscall execution
    CV_SYSCALL_ERRNO = 1,           // Return errno without execution
    CV_SYSCALL_KILL = 2,            // Terminate process
    CV_SYSCALL_TRAP = 3,            // Send SIGSYS signal
    CV_SYSCALL_LOG = 4              // Log syscall attempt and allow
};

// Security profile structure
struct containerv_security_profile {
    enum containerv_security_level level;
    char* name;                     // Profile identifier
    char* description;              // Human-readable description
    
    // Capability management
    uint64_t allowed_caps;          // Bitmask of allowed capabilities
    uint64_t dropped_caps;          // Bitmask of explicitly dropped capabilities
    bool     no_new_privileges;     // Prevent privilege escalation
    
    // Process security
    uint32_t run_as_uid;            // User ID to run as (0 = current user)
    uint32_t run_as_gid;            // Group ID to run as (0 = current group)
    char*    run_as_user;           // Username to run as (overrides UID/GID)
    bool     no_suid;               // Disable setuid/setgid execution
    
    // Filesystem security
    bool     read_only_root;        // Make root filesystem read-only
    char**   writable_paths;        // Array of writable path exceptions
    char**   masked_paths;          // Array of paths to mask/hide
    int      fs_rule_count;         // Number of filesystem rules
    
    // Network security
    bool     network_isolated;      // Isolate from host network stack
    char**   allowed_ports;         // Array of "proto:port" allowed bindings
    char**   allowed_hosts;         // Array of hostname/IP patterns for outbound
    int      network_rule_count;    // Number of network rules
    
    // Platform-specific extensions
#ifdef __linux__
    bool     use_apparmor;          // Apply AppArmor profile
    bool     use_selinux;           // Apply SELinux context  
    char*    security_context;      // SELinux security context
    enum containerv_syscall_action default_syscall_action; // Default for unlisted syscalls
#endif
    
#ifdef _WIN32
    bool     use_app_container;     // Enable AppContainer isolation
    char*    integrity_level;       // Windows integrity level ("low", "medium", "high")
    char**   capability_sids;       // Windows capability SIDs
    int      win_cap_count;         // Number of Windows capabilities
#endif
};

// Security audit results
struct containerv_security_audit {
    bool     capabilities_minimal;  // Only necessary capabilities granted
    bool     no_privileged_access; // No root/admin access
    bool     filesystem_restricted; // Filesystem access properly limited
    bool     network_controlled;   // Network access controlled
    bool     syscalls_filtered;    // Dangerous syscalls blocked (Linux)
    bool     isolation_complete;   // Process isolation enforced
    
    char     audit_log[1024];      // Detailed audit information
    time_t   audit_time;           // When audit was performed
    int      security_score;       // Security score (0-100, higher is better)
};

/**
 * @brief Initialize the container security subsystem
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_init(void);

/**
 * @brief Cleanup the container security subsystem
 */
extern void containerv_security_cleanup(void);

/**
 * @brief Create a new security profile
 * @param name Unique profile name
 * @param level Base security level 
 * @param profile Output pointer to created profile (caller must free)
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_profile_create(
    const char* name,
    enum containerv_security_level level,
    struct containerv_security_profile** profile
);

/**
 * @brief Load a predefined security profile
 * @param name Profile name ("default", "web-server", "database", "untrusted")
 * @param profile Output pointer to loaded profile (caller must free)
 * @return 0 on success, -1 if not found
 */
extern int containerv_security_profile_load(
    const char* name,
    struct containerv_security_profile** profile
);

/**
 * @brief Free a security profile and its resources
 * @param profile Profile to free
 */
extern void containerv_security_profile_free(
    struct containerv_security_profile* profile
);

/**
 * @brief Apply security profile to container options
 * @param options Container options to enhance with security settings
 * @param profile Security profile to apply
 * @return 0 on success, -1 on failure
 */
extern int containerv_options_set_security_profile(
    struct containerv_options* options,
    const struct containerv_security_profile* profile
);

/**
 * @brief Add a capability to security profile
 * @param profile Security profile to modify
 * @param capability Platform-specific capability to allow
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
);

/**
 * @brief Drop a capability from security profile
 * @param profile Security profile to modify
 * @param capability Platform-specific capability to deny
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_drop_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
);

/**
 * @brief Add filesystem path to writable exceptions
 * @param profile Security profile to modify
 * @param path Filesystem path to allow writes (e.g., "/var/log", "/tmp")
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_writable_path(
    struct containerv_security_profile* profile,
    const char* path
);

/**
 * @brief Add network port to allowed bindings
 * @param profile Security profile to modify  
 * @param port_spec Port specification (e.g., "tcp:80", "udp:53", "tcp:8080-8090")
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_network_port(
    struct containerv_security_profile* profile,
    const char* port_spec
);

#ifdef __linux__
/**
 * @brief Add syscall filter rule (Linux only)
 * @param profile Security profile to modify
 * @param syscall_name Name of syscall (e.g., "open", "execve")
 * @param action Action to take when syscall is attempted
 * @param errno_value Errno to return (for CV_SYSCALL_ERRNO action, 0 otherwise)
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_syscall_filter(
    struct containerv_security_profile* profile,
    const char* syscall_name,
    enum containerv_syscall_action action,
    int errno_value
);

/**
 * @brief Load AppArmor profile for container (Linux only)
 * @param profile Security profile to modify
 * @param apparmor_profile AppArmor profile name or path
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_set_apparmor_profile(
    struct containerv_security_profile* profile,
    const char* apparmor_profile
);
#endif

/**
 * @brief Validate security profile compatibility with current platform
 * @param profile Security profile to validate
 * @param error_msg Output buffer for error message (can be NULL)
 * @param error_size Size of error message buffer
 * @return 0 if valid, -1 if incompatible
 */
extern int containerv_security_profile_validate(
    const struct containerv_security_profile* profile,
    char* error_msg,
    size_t error_size
);

/**
 * @brief Perform security audit on a running container
 * @param container Container to audit
 * @param audit Output audit results structure
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_audit(
    struct containerv_container* container,
    struct containerv_security_audit* audit
);

// Predefined security profiles (read-only)
extern const struct containerv_security_profile* containerv_profile_default;
extern const struct containerv_security_profile* containerv_profile_web_server;
extern const struct containerv_security_profile* containerv_profile_database;
extern const struct containerv_security_profile* containerv_profile_untrusted;

// =============================================================================
// CONTAINER ORCHESTRATION API
// =============================================================================

// Forward declarations
struct containerv_application;
struct containerv_service_instance;
struct containerv_load_balancer;

// Restart policies for services
enum containerv_restart_policy {
    CV_RESTART_NO,           // Never restart
    CV_RESTART_ALWAYS,       // Always restart on exit
    CV_RESTART_ON_FAILURE,   // Restart only on non-zero exit
    CV_RESTART_UNLESS_STOPPED // Always restart unless manually stopped
};

// Service instance states
enum containerv_instance_state {
    CV_INSTANCE_CREATED,     // Created but not started
    CV_INSTANCE_STARTING,    // In process of starting
    CV_INSTANCE_RUNNING,     // Running normally
    CV_INSTANCE_STOPPING,    // In process of stopping
    CV_INSTANCE_STOPPED,     // Stopped (not running)
    CV_INSTANCE_FAILED,      // Failed to start or crashed
    CV_INSTANCE_RESTARTING   // Restarting after failure
};

// Health check status
enum containerv_health_status {
    CV_HEALTH_UNKNOWN,       // Health not checked yet
    CV_HEALTH_STARTING,      // Service is starting up
    CV_HEALTH_HEALTHY,       // Service is healthy
    CV_HEALTH_UNHEALTHY,     // Service failed health check
    CV_HEALTH_NONE          // No health check configured
};

// Load balancing algorithms
enum containerv_lb_algorithm {
    CV_LB_ROUND_ROBIN,       // Round-robin distribution
    CV_LB_LEAST_CONNECTIONS, // Route to endpoint with fewest connections
    CV_LB_WEIGHTED_ROUND_ROBIN, // Weighted round-robin
    CV_LB_IP_HASH,           // Hash client IP for consistent routing
    CV_LB_RANDOM             // Random selection
};

// Network driver types
enum containerv_network_driver {
    CV_NET_BRIDGE,           // Bridge network (default)
    CV_NET_HOST,            // Use host networking
    CV_NET_NONE,            // No networking
    CV_NET_OVERLAY          // Overlay network for multi-host
};

// Volume driver types  
enum containerv_volume_driver {
    CV_VOL_LOCAL,           // Local filesystem volume
    CV_VOL_NFS,             // NFS network volume
    CV_VOL_TMPFS,           // Temporary filesystem
    CV_VOL_BIND             // Bind mount
};

// Port mapping for services
struct containerv_port_mapping {
    char* host_ip;          // Host IP to bind (NULL for all interfaces)
    int host_port;          // Host port (0 for dynamic allocation)
    int container_port;     // Container port
    char* protocol;         // "tcp" or "udp" (default: "tcp")
};

// Volume mount configuration
struct containerv_volume_mount {
    char* source;           // Volume name or host path
    char* target;           // Container mount path
    char* type;             // "volume", "bind", "tmpfs"
    bool read_only;         // Mount as read-only
};

// Service dependency
struct containerv_service_dependency {
    char* service_name;     // Name of service to depend on
    bool required;          // Whether dependency is required to start
    int timeout_seconds;    // Timeout waiting for dependency
};

// Health check configuration
struct containerv_healthcheck {
    char** test_command;    // Health check command (NULL terminated array)
    int interval_seconds;   // Interval between checks
    int timeout_seconds;    // Timeout for each check
    int retries;           // Number of retries before marking unhealthy
    int start_period_seconds; // Grace period before first check
};

// Service configuration
struct containerv_service {
    char* name;                                    // Service name (unique within app)
    char* image;                                   // Container image reference
    char** command;                                // Override image CMD (NULL terminated)
    char** environment;                            // Environment variables (KEY=VALUE)
    struct containerv_port_mapping* ports;         // Port mappings
    int port_count;                               // Number of port mappings
    struct containerv_volume_mount* volumes;       // Volume mounts
    int volume_count;                             // Number of volume mounts
    struct containerv_service_dependency* depends_on; // Service dependencies
    int dependency_count;                         // Number of dependencies
    struct containerv_healthcheck* healthcheck;    // Health check config (NULL if none)
    enum containerv_restart_policy restart;       // Restart policy
    int replicas;                                 // Desired number of replicas
    struct containerv_security_profile* security_profile; // Security profile
    char** networks;                              // Networks to connect to
    int network_count;                           // Number of networks
    char** secrets;                              // Secret names to mount
    int secret_count;                            // Number of secrets
    bool privileged;                             // Run in privileged mode
    char* user;                                  // User to run as
    char* working_dir;                           // Working directory
    uint64_t memory_limit;                       // Memory limit in bytes
    double cpu_limit;                            // CPU limit (1.0 = 1 core)
};

// Network configuration
struct containerv_network_config {
    char* name;                                  // Network name
    enum containerv_network_driver driver;      // Network driver
    char* subnet;                               // Network subnet (CIDR)
    char* gateway;                              // Gateway IP
    char** dns_servers;                         // DNS servers
    int dns_count;                              // Number of DNS servers
    bool internal;                              // Internal-only network
    bool enable_ipv6;                           // Enable IPv6
};

// Volume configuration
struct containerv_volume_config {
    char* name;                                 // Volume name
    enum containerv_volume_driver driver;      // Volume driver
    char* driver_opts;                         // Driver-specific options (JSON string)
    char* mountpoint;                          // Volume mountpoint on host
    bool external;                             // External volume (don't create/destroy)
};

// Secret configuration
struct containerv_secret_config {
    char* name;                                // Secret name
    char* file;                               // File containing secret data
    char* external_name;                      // External secret name
    bool external;                            // External secret (don't create/destroy)
};

// Service instance
struct containerv_service_instance {
    char* id;                                 // Unique instance ID
    char* service_name;                       // Parent service name
    char* container_id;                       // Container ID
    enum containerv_instance_state state;    // Current state
    enum containerv_health_status health;    // Health status
    time_t created_at;                       // Creation timestamp
    time_t started_at;                       // Start timestamp
    int restart_count;                       // Number of restarts
    char* ip_address;                        // Instance IP address
    struct containerv_port_mapping* ports;   // Actual port mappings
    int port_count;                          // Number of ports
};

// Service endpoint for discovery
struct containerv_service_endpoint {
    char* service_name;                      // Service name
    char* instance_id;                       // Instance ID
    char* ip_address;                        // IP address
    int port;                               // Port number
    bool healthy;                           // Is endpoint healthy
    time_t last_health_check;               // Last health check time
    int weight;                             // Load balancing weight
};

// Application definition
struct containerv_application {
    char* name;                             // Application name
    char* version;                          // Application version
    struct containerv_service* services;    // Service definitions
    int service_count;                     // Number of services
    struct containerv_network_config* networks; // Network configurations
    int network_count;                     // Number of networks
    struct containerv_volume_config* volumes;   // Volume configurations
    int volume_count;                      // Number of volumes
    struct containerv_secret_config* secrets;   // Secret configurations
    int secret_count;                      // Number of secrets
    
    // Runtime state
    struct containerv_service_instance** instances; // Running instances
    int* instance_counts;                          // Instances per service
    bool running;                                  // Is application running
    time_t deployed_at;                           // Deployment timestamp
};

// Orchestration event types
enum containerv_orchestration_event {
    CV_ORCH_SERVICE_STARTING,           // Service is starting
    CV_ORCH_SERVICE_STARTED,            // Service started successfully
    CV_ORCH_SERVICE_STOPPING,           // Service is stopping
    CV_ORCH_SERVICE_STOPPED,            // Service stopped
    CV_ORCH_SERVICE_FAILED,             // Service failed to start/crashed
    CV_ORCH_SERVICE_UNHEALTHY,          // Service health check failed
    CV_ORCH_SERVICE_HEALTHY,            // Service became healthy
    CV_ORCH_APPLICATION_DEPLOYED,       // Application deployed
    CV_ORCH_APPLICATION_STOPPED,        // Application stopped
    CV_ORCH_SCALING_STARTED,           // Service scaling started
    CV_ORCH_SCALING_COMPLETED,         // Service scaling completed
    CV_ORCH_DEPENDENCY_TIMEOUT         // Service dependency timeout
};

// Orchestration event callback
typedef void (*containerv_orchestration_callback)(
    enum containerv_orchestration_event event,
    const char* service_name,
    const char* message,
    void* user_data
);

// =============================================================================
// APPLICATION LIFECYCLE MANAGEMENT
// =============================================================================

/**
 * @brief Parse application configuration from YAML file
 * @param config_file Path to YAML configuration file
 * @param app Output application structure
 * @return 0 on success, -1 on failure
 */
extern int containerv_parse_application_config(
    const char* config_file,
    struct containerv_application** app
);

/**
 * @brief Deploy an application with all its services
 * @param app Application to deploy
 * @return 0 on success, -1 on failure
 */
extern int containerv_deploy_application(struct containerv_application* app);

/**
 * @brief Stop a running application
 * @param app Application to stop
 * @return 0 on success, -1 on failure
 */
extern int containerv_stop_application(struct containerv_application* app);

/**
 * @brief Destroy application and clean up resources
 * @param app Application to destroy
 */
extern void containerv_destroy_application(struct containerv_application* app);

/**
 * @brief Scale a service to specified number of replicas
 * @param app Application containing the service
 * @param service_name Name of service to scale
 * @param replicas Target number of replicas
 * @return 0 on success, -1 on failure
 */
extern int containerv_scale_service(
    struct containerv_application* app,
    const char* service_name,
    int replicas
);

/**
 * @brief Update service configuration with rolling update
 * @param app Application containing the service
 * @param service_name Name of service to update
 * @param new_config New service configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_update_service(
    struct containerv_application* app,
    const char* service_name,
    const struct containerv_service* new_config
);

/**
 * @brief Get current status of all services in application
 * @param app Application to check
 * @param instances Output array for service instances
 * @param max_instances Maximum number of instances to return
 * @return Number of instances returned, or -1 on error
 */
extern int containerv_get_application_status(
    struct containerv_application* app,
    struct containerv_service_instance* instances,
    int max_instances
);

// =============================================================================
// SERVICE DISCOVERY
// =============================================================================

/**
 * @brief Initialize service discovery system
 * @return 0 on success, -1 on failure
 */
extern int containerv_service_discovery_init(void);

/**
 * @brief Cleanup service discovery system
 */
extern void containerv_service_discovery_cleanup(void);

/**
 * @brief Register a service endpoint
 * @param endpoint Service endpoint to register
 * @return 0 on success, -1 on failure
 */
extern int containerv_register_service_endpoint(
    const struct containerv_service_endpoint* endpoint
);

/**
 * @brief Unregister a service endpoint
 * @param service_name Service name
 * @param instance_id Instance ID to remove
 * @return 0 on success, -1 on failure
 */
extern int containerv_unregister_service_endpoint(
    const char* service_name,
    const char* instance_id
);

/**
 * @brief Discover all endpoints for a service
 * @param service_name Service name to discover
 * @param endpoints Output array for endpoints
 * @param max_endpoints Maximum number of endpoints to return
 * @return Number of endpoints found, or -1 on error
 */
extern int containerv_discover_service_endpoints(
    const char* service_name,
    struct containerv_service_endpoint* endpoints,
    int max_endpoints
);

/**
 * @brief Resolve service name to IP address and port
 * @param service_name Service name to resolve
 * @param ip_address Output buffer for IP address (minimum 16 bytes for IPv4)
 * @param port Output port number
 * @return 0 on success, -1 if service not found
 */
extern int containerv_resolve_service_address(
    const char* service_name,
    char* ip_address,
    int* port
);

// =============================================================================
// HEALTH MONITORING
// =============================================================================

/**
 * @brief Start health monitoring for an application
 * @param app Application to monitor
 * @param callback Optional callback for health events
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on failure
 */
extern int containerv_start_health_monitoring(
    struct containerv_application* app,
    containerv_orchestration_callback callback,
    void* user_data
);

/**
 * @brief Stop health monitoring for an application
 * @param app Application to stop monitoring
 */
extern void containerv_stop_health_monitoring(struct containerv_application* app);

/**
 * @brief Get health status for a specific service
 * @param service_name Service name
 * @param health_status Output health status
 * @return 0 on success, -1 if service not found
 */
extern int containerv_get_service_health(
    const char* service_name,
    enum containerv_health_status* health_status
);

/**
 * @brief Manually trigger health check for service
 * @param service_name Service name
 * @param instance_id Instance ID (NULL for all instances)
 * @return 0 on success, -1 on failure
 */
extern int containerv_trigger_health_check(
    const char* service_name,
    const char* instance_id
);

// =============================================================================
// LOAD BALANCING
// =============================================================================

/**
 * @brief Create a load balancer for a service
 * @param service_name Service name
 * @param algorithm Load balancing algorithm
 * @param lb Output load balancer handle
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_load_balancer(
    const char* service_name,
    enum containerv_lb_algorithm algorithm,
    struct containerv_load_balancer** lb
);

/**
 * @brief Destroy a load balancer
 * @param lb Load balancer to destroy
 */
extern void containerv_destroy_load_balancer(struct containerv_load_balancer* lb);

/**
 * @brief Get next endpoint for request using load balancing algorithm
 * @param lb Load balancer
 * @param client_ip Client IP address for sticky sessions (NULL if not needed)
 * @param endpoint Output endpoint
 * @return 0 on success, -1 if no healthy endpoints available
 */
extern int containerv_get_load_balanced_endpoint(
    struct containerv_load_balancer* lb,
    const char* client_ip,
    struct containerv_service_endpoint* endpoint
);

/**
 * @brief Update endpoint weight for weighted algorithms
 * @param lb Load balancer
 * @param instance_id Instance ID
 * @param weight New weight value (1-100)
 * @return 0 on success, -1 on failure
 */
extern int containerv_update_endpoint_weight(
    struct containerv_load_balancer* lb,
    const char* instance_id,
    int weight
);

// =============================================================================
// PERFORMANCE OPTIMIZATION API
// =============================================================================

// Forward declarations for performance optimization
struct containerv_pool;
struct containerv_startup_optimizer;
struct containerv_memory_pool;
struct containerv_performance_metrics;

// Container pool policies
enum containerv_pool_policy {
    CV_POOL_PREALLOC,        // Pre-allocate containers on startup
    CV_POOL_ON_DEMAND,       // Create containers on demand
    CV_POOL_HYBRID           // Hybrid approach with minimum pool
};

// Startup optimization strategies
enum containerv_startup_strategy {
    CV_STARTUP_SEQUENTIAL,   // Start containers sequentially
    CV_STARTUP_PARALLEL,     // Start containers in parallel
    CV_STARTUP_PRIORITY,     // Priority-based startup order
    CV_STARTUP_SMART         // Smart startup with dependency analysis
};

// Memory optimization techniques
enum containerv_memory_optimization {
    CV_MEM_COPY_ON_WRITE,    // Enable copy-on-write optimization
    CV_MEM_SHARED_LIBS,      // Share common libraries between containers
    CV_MEM_DEDUPLICATION,    // Enable memory deduplication
    CV_MEM_COMPRESSION       // Enable memory compression
};

// CPU optimization techniques
enum containerv_cpu_optimization {
    CV_CPU_AFFINITY,         // Set CPU affinity for containers
    CV_CPU_NUMA_AWARE,       // NUMA-aware CPU allocation
    CV_CPU_PRIORITY,         // Set container priority levels
    CV_CPU_THROTTLING        // Enable intelligent CPU throttling
};

// I/O optimization techniques
enum containerv_io_optimization {
    CV_IO_DIRECT,            // Use direct I/O when possible
    CV_IO_ASYNC,             // Enable asynchronous I/O
    CV_IO_READAHEAD,         // Enable read-ahead optimization
    CV_IO_WRITE_CACHE        // Enable write caching
};

// Performance metrics structure
struct containerv_performance_metrics {
    // Startup metrics
    uint64_t container_startup_time_ns;     // Average container startup time
    uint64_t image_pull_time_ns;            // Average image pull time
    uint64_t filesystem_setup_time_ns;      // Average filesystem setup time
    uint64_t network_setup_time_ns;         // Average network setup time
    
    // Runtime metrics
    uint64_t memory_overhead_bytes;         // Memory overhead per container
    double   cpu_overhead_percent;          // CPU overhead percentage
    uint64_t io_throughput_bytes_per_sec;   // I/O throughput
    uint32_t concurrent_containers;         // Number of concurrent containers
    
    // Pool metrics
    uint32_t pool_hit_rate_percent;         // Container pool hit rate
    uint32_t pool_size_current;             // Current pool size
    uint32_t pool_size_maximum;             // Maximum pool size reached
    uint32_t pool_allocations_total;        // Total pool allocations
    
    // System metrics
    uint64_t total_memory_usage_bytes;      // Total system memory usage
    double   system_cpu_usage_percent;      // System CPU usage
    uint32_t file_descriptor_count;         // Open file descriptors
    uint32_t thread_count;                  // Active thread count
    
    // Optimization effectiveness
    double   startup_improvement_percent;   // Startup time improvement
    double   memory_savings_percent;        // Memory usage reduction
    double   throughput_improvement_percent; // Throughput improvement
    
    time_t   measurement_timestamp;         // When metrics were collected
    uint64_t measurement_duration_ns;       // Measurement duration
};

// Container pool configuration
struct containerv_pool_config {
    enum containerv_pool_policy policy;     // Pool management policy
    uint32_t min_size;                      // Minimum pool size
    uint32_t max_size;                      // Maximum pool size
    uint32_t warm_count;                    // Number of warm containers to maintain
    uint32_t idle_timeout_seconds;          // Idle container timeout
    bool     enable_prewarming;             // Enable container prewarming
    char**   prewarmed_images;              // Images to prewarm
    int      prewarmed_image_count;         // Number of prewarmed images
};

// Startup optimization configuration
struct containerv_startup_config {
    enum containerv_startup_strategy strategy;    // Startup strategy
    uint32_t parallel_limit;                     // Max parallel startups
    uint32_t dependency_timeout_seconds;         // Dependency wait timeout
    bool     enable_fast_clone;                  // Enable fast container cloning
    bool     enable_lazy_loading;                // Enable lazy resource loading
    bool     skip_health_check_on_startup;       // Skip initial health checks
    char**   priority_services;                  // High-priority services
    int      priority_service_count;             // Number of priority services
};

// Memory optimization configuration
struct containerv_memory_config {
    uint64_t optimization_flags;            // Bitmask of memory optimizations
    uint32_t deduplication_window_mb;       // Memory deduplication window size
    uint32_t compression_threshold_mb;      // Compression threshold
    uint32_t shared_library_cache_mb;       // Shared library cache size
    bool     enable_memory_ballooning;      // Enable memory ballooning
    double   memory_overcommit_ratio;       // Memory overcommit ratio (1.0-2.0)
};

// CPU optimization configuration
struct containerv_cpu_config {
    uint64_t optimization_flags;            // Bitmask of CPU optimizations
    uint32_t cpu_affinity_mask;             // CPU affinity mask
    int      priority_adjustment;           // Priority adjustment (-20 to 19)
    bool     enable_numa_balancing;         // Enable NUMA balancing
    uint32_t throttle_threshold_percent;    // CPU throttling threshold
    uint32_t boost_duration_seconds;        // CPU boost duration
};

// I/O optimization configuration
struct containerv_io_config {
    uint64_t optimization_flags;            // Bitmask of I/O optimizations
    uint32_t readahead_kb;                  // Read-ahead size in KB
    uint32_t write_cache_mb;                // Write cache size in MB
    uint32_t queue_depth;                   // I/O queue depth
    char*    io_scheduler;                  // I/O scheduler ("mq-deadline", "bfq", etc.)
    bool     enable_io_uring;               // Enable io_uring (Linux)
};

// Comprehensive performance configuration
struct containerv_performance_config {
    struct containerv_pool_config    pool;      // Container pool configuration
    struct containerv_startup_config startup;  // Startup optimization configuration
    struct containerv_memory_config  memory;   // Memory optimization configuration
    struct containerv_cpu_config     cpu;      // CPU optimization configuration
    struct containerv_io_config      io;       // I/O optimization configuration
    
    // Global performance settings
    bool     enable_performance_monitoring;    // Enable performance monitoring
    uint32_t metrics_collection_interval_ms;   // Metrics collection interval
    char*    performance_profile;              // Performance profile name
    bool     auto_tune_enabled;                // Enable automatic tuning
    uint32_t tuning_interval_seconds;          // Auto-tuning interval
};

// Performance optimization engine
struct containerv_performance_engine {
    struct containerv_performance_config config;
    struct containerv_pool*              container_pool;
    struct containerv_startup_optimizer* startup_optimizer;
    struct containerv_memory_pool*       memory_pool;
    
    // Performance monitoring
    struct containerv_performance_metrics current_metrics;
    struct containerv_performance_metrics baseline_metrics;
    bool                                  monitoring_active;
    
    // Auto-tuning state
    bool                                  auto_tuning_active;
    time_t                               last_tuning_time;
    uint32_t                             tuning_iterations;
};

// =============================================================================
// PERFORMANCE OPTIMIZATION FUNCTIONS
// =============================================================================

/**
 * @brief Initialize the performance optimization engine
 * @param config Performance configuration (NULL for defaults)
 * @param engine Output performance engine handle
 * @return 0 on success, -1 on failure
 */
extern int containerv_performance_init(
    const struct containerv_performance_config* config,
    struct containerv_performance_engine** engine
);

/**
 * @brief Cleanup and shutdown the performance optimization engine
 * @param engine Performance engine to cleanup
 */
extern void containerv_performance_cleanup(
    struct containerv_performance_engine* engine
);

/**
 * @brief Create a container pool for faster container startup
 * @param engine Performance engine
 * @param config Pool configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_container_pool(
    struct containerv_performance_engine* engine,
    const struct containerv_pool_config* config
);

/**
 * @brief Get a pre-allocated container from the pool
 * @param engine Performance engine
 * @param image_ref Image reference for container
 * @param options Container options
 * @param container Output container (may be pre-allocated)
 * @return 0 on success, -1 on failure
 */
extern int containerv_get_pooled_container(
    struct containerv_performance_engine* engine,
    const struct containerv_image_ref* image_ref,
    struct containerv_options* options,
    struct containerv_container** container
);

/**
 * @brief Return a container to the pool for reuse
 * @param engine Performance engine
 * @param container Container to return to pool
 * @return 0 on success, -1 if container cannot be reused
 */
extern int containerv_return_to_pool(
    struct containerv_performance_engine* engine,
    struct containerv_container* container
);

/**
 * @brief Optimize startup sequence for multiple containers
 * @param engine Performance engine
 * @param containers Array of containers to start
 * @param container_count Number of containers
 * @return 0 on success, -1 on failure
 */
extern int containerv_optimize_startup_sequence(
    struct containerv_performance_engine* engine,
    struct containerv_container** containers,
    int container_count
);

/**
 * @brief Enable memory optimization for containers
 * @param engine Performance engine
 * @param optimization_flags Bitmask of memory optimizations to enable
 * @return 0 on success, -1 on failure
 */
extern int containerv_enable_memory_optimization(
    struct containerv_performance_engine* engine,
    uint64_t optimization_flags
);

/**
 * @brief Set CPU affinity and optimization for containers
 * @param engine Performance engine
 * @param cpu_mask CPU affinity mask (bitmask of CPU cores)
 * @param optimization_flags CPU optimization flags
 * @return 0 on success, -1 on failure
 */
extern int containerv_set_cpu_optimization(
    struct containerv_performance_engine* engine,
    uint32_t cpu_mask,
    uint64_t optimization_flags
);

/**
 * @brief Configure I/O optimization settings
 * @param engine Performance engine
 * @param io_config I/O optimization configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_configure_io_optimization(
    struct containerv_performance_engine* engine,
    const struct containerv_io_config* io_config
);

/**
 * @brief Start performance monitoring and metrics collection
 * @param engine Performance engine
 * @return 0 on success, -1 on failure
 */
extern int containerv_start_performance_monitoring(
    struct containerv_performance_engine* engine
);

/**
 * @brief Stop performance monitoring
 * @param engine Performance engine
 */
extern void containerv_stop_performance_monitoring(
    struct containerv_performance_engine* engine
);

/**
 * @brief Get current performance metrics
 * @param engine Performance engine
 * @param metrics Output metrics structure
 * @return 0 on success, -1 on failure
 */
extern int containerv_get_performance_metrics(
    struct containerv_performance_engine* engine,
    struct containerv_performance_metrics* metrics
);

/**
 * @brief Set baseline performance metrics for comparison
 * @param engine Performance engine
 * @param baseline Baseline metrics (NULL to measure current as baseline)
 * @return 0 on success, -1 on failure
 */
extern int containerv_set_performance_baseline(
    struct containerv_performance_engine* engine,
    const struct containerv_performance_metrics* baseline
);

/**
 * @brief Enable automatic performance tuning
 * @param engine Performance engine
 * @param enable True to enable, false to disable
 * @return 0 on success, -1 on failure
 */
extern int containerv_enable_auto_tuning(
    struct containerv_performance_engine* engine,
    bool enable
);

/**
 * @brief Manually trigger performance tuning optimization
 * @param engine Performance engine
 * @return 0 on success, -1 on failure
 */
extern int containerv_trigger_performance_tuning(
    struct containerv_performance_engine* engine
);

/**
 * @brief Load a predefined performance profile
 * @param profile_name Profile name ("high-throughput", "low-latency", "balanced", "memory-efficient")
 * @param config Output performance configuration
 * @return 0 on success, -1 if profile not found
 */
extern int containerv_load_performance_profile(
    const char* profile_name,
    struct containerv_performance_config* config
);

/**
 * @brief Save current performance configuration as a named profile
 * @param engine Performance engine
 * @param profile_name Profile name to save as
 * @return 0 on success, -1 on failure
 */
extern int containerv_save_performance_profile(
    struct containerv_performance_engine* engine,
    const char* profile_name
);

// Platform-specific performance optimization
#ifdef __linux__
/**
 * @brief Enable Linux-specific optimizations (OverlayFS, namespace sharing)
 * @param engine Performance engine
 * @param enable_overlayfs_tuning Enable OverlayFS performance tuning
 * @param enable_namespace_sharing Enable namespace sharing between containers
 * @return 0 on success, -1 on failure
 */
extern int containerv_enable_linux_optimizations(
    struct containerv_performance_engine* engine,
    bool enable_overlayfs_tuning,
    bool enable_namespace_sharing
);
#endif

#ifdef _WIN32
/**
 * @brief Enable Windows-specific optimizations (Hyper-V, dynamic memory)
 * @param engine Performance engine
 * @param enable_hyperv_optimization Enable Hyper-V container optimization
 * @param enable_dynamic_memory Enable dynamic memory allocation
 * @return 0 on success, -1 on failure
 */
extern int containerv_enable_windows_optimizations(
    struct containerv_performance_engine* engine,
    bool enable_hyperv_optimization,
    bool enable_dynamic_memory
);
#endif

// Performance benchmarking and validation
/**
 * @brief Run performance benchmark suite
 * @param engine Performance engine
 * @param benchmark_type Type of benchmark ("startup", "throughput", "memory", "all")
 * @param results Output benchmark results (implementation defined)
 * @return 0 on success, -1 on failure
 */
extern int containerv_run_performance_benchmark(
    struct containerv_performance_engine* engine,
    const char* benchmark_type,
    void* results
);

/**
 * @brief Validate performance improvements against baseline
 * @param engine Performance engine
 * @param improvement_threshold Minimum improvement percentage required
 * @param validation_report Output validation report (implementation defined)
 * @return 1 if improvements meet threshold, 0 if not, -1 on error
 */
extern int containerv_validate_performance_improvements(
    struct containerv_performance_engine* engine,
    double improvement_threshold,
    char* validation_report
);

// Predefined performance profiles (read-only)
extern const struct containerv_performance_config* containerv_perf_profile_balanced;
extern const struct containerv_performance_config* containerv_perf_profile_high_throughput;
extern const struct containerv_performance_config* containerv_perf_profile_low_latency;
extern const struct containerv_performance_config* containerv_perf_profile_memory_efficient;

/**
 * @brief Get next endpoint from load balancer
 * @param lb Load balancer
 * @param client_info Optional client information for algorithms like IP hash
 * @param endpoint Output endpoint
 * @return 0 on success, -1 if no healthy endpoints
 */
extern int containerv_lb_get_endpoint(
    struct containerv_load_balancer* lb,
    const char* client_info,
    struct containerv_service_endpoint* endpoint
);

/**
 * @brief Update endpoint health status in load balancer
 * @param lb Load balancer
 * @param instance_id Instance ID
 * @param healthy Health status
 * @return 0 on success, -1 on failure
 */
extern int containerv_lb_update_health(
    struct containerv_load_balancer* lb,
    const char* instance_id,
    bool healthy
);

// =============================================================================
// NETWORK MANAGEMENT
// =============================================================================

/**
 * @brief Create an orchestration network
 * @param network_config Network configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_network(const struct containerv_network_config* network_config);

/**
 * @brief Connect container to a network
 * @param container_id Container ID
 * @param network_name Network name
 * @param ip_address Optional static IP address
 * @return 0 on success, -1 on failure
 */
extern int containerv_connect_network(
    const char* container_id,
    const char* network_name,
    const char* ip_address
);

/**
 * @brief Disconnect container from network
 * @param container_id Container ID
 * @param network_name Network name
 * @return 0 on success, -1 on failure
 */
extern int containerv_disconnect_network(
    const char* container_id,
    const char* network_name
);

/**
 * @brief Remove an orchestration network
 * @param network_name Network name
 * @return 0 on success, -1 on failure
 */
extern int containerv_remove_network(const char* network_name);

// =============================================================================
// VOLUME ORCHESTRATION
// =============================================================================

/**
 * @brief Create an orchestration volume
 * @param volume_config Volume configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_orchestration_volume(const struct containerv_volume_config* volume_config);

/**
 * @brief Remove an orchestration volume
 * @param volume_name Volume name
 * @param force Force removal even if in use
 * @return 0 on success, -1 on failure
 */
extern int containerv_remove_orchestration_volume(const char* volume_name, bool force);

// =============================================================================
// CONFIGURATION MANAGEMENT
// =============================================================================

/**
 * @brief Create a secret
 * @param secret_config Secret configuration
 * @param secret_data Secret data buffer
 * @param data_size Size of secret data
 * @return 0 on success, -1 on failure
 */
extern int containerv_create_secret(
    const struct containerv_secret_config* secret_config,
    const void* secret_data,
    size_t data_size
);

/**
 * @brief Remove a secret
 * @param secret_name Secret name
 * @return 0 on success, -1 on failure
 */
extern int containerv_remove_secret(const char* secret_name);

#endif //!__CONTAINERV_H__
