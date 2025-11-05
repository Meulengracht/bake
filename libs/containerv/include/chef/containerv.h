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

#endif //!__CONTAINERV_H__
