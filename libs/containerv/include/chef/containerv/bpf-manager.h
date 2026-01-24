/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef __CONTAINERV_BPF_MANAGER_H__
#define __CONTAINERV_BPF_MANAGER_H__

/**
 * @brief Initialize the BPF manager for centralized eBPF enforcement
 * 
 * This function loads and pins BPF LSM programs to /sys/fs/bpf for
 * container security policy enforcement. It should be called once
 * during application startup (e.g., by cvd daemon).
 * 
 * If BPF LSM is not available, this function will log a warning and
 * return success to allow fallback to seccomp-based enforcement.
 * 
 * @return 0 on success or if BPF LSM unavailable, -1 on critical error
 */
extern int containerv_bpf_manager_initialize(void);

/**
 * @brief Shutdown the BPF manager and clean up resources
 * 
 * This function unpins and destroys BPF programs and maps.
 * Should be called during application shutdown.
 */
extern void containerv_bpf_manager_shutdown(void);

/**
 * @brief Check if BPF LSM enforcement is available
 * 
 * @return 1 if BPF LSM is available and loaded, 0 otherwise
 */
extern int containerv_bpf_manager_is_available(void);

/**
 * @brief Get the file descriptor for the policy map
 * 
 * This returns the FD for the pinned policy map that can be used
 * to populate per-container policies.
 * 
 * @return Map file descriptor, or -1 if BPF unavailable
 */
extern int containerv_bpf_manager_get_policy_map_fd(void);

/**
 * @brief Populate BPF policy for a container
 * 
 * After container rootfs and cgroup setup, this function resolves
 * configured allowed paths to (dev, ino) within the container's
 * filesystem view and populates the BPF policy map.
 * 
 * @param container_id Container ID (hostname) - must not be NULL
 * @param rootfs_path Path to the container's rootfs - must not be NULL
 * @param policy Security policy with path rules - must not be NULL
 * @return 0 on success, -1 on error
 */
struct containerv_policy;
extern int containerv_bpf_manager_populate_policy(
    const char* container_id,
    const char* rootfs_path,
    struct containerv_policy* policy
);

/**
 * @brief Remove BPF policy entries for a container
 * 
 * Cleans up all cgroup-specific data in BPF maps when a container
 * is destroyed.
 * 
 * @param container_id Container ID (hostname) - must not be NULL
 * @return 0 on success, -1 on error
 */
extern int containerv_bpf_manager_cleanup_policy(const char* container_id);

/**
 * @brief Container-specific BPF policy metrics
 */
struct containerv_bpf_container_metrics {
    char container_id[256];           // Container identifier
    unsigned long long cgroup_id;     // Cgroup ID for this container
    int policy_entry_count;           // Number of policy entries in map
    unsigned long long populate_time_us; // Time taken to populate policy (microseconds)
    unsigned long long cleanup_time_us;  // Time taken to cleanup policy (microseconds)
};

/**
 * @brief Global BPF policy enforcement metrics
 */
struct containerv_bpf_metrics {
    int available;                       // Whether BPF LSM is available
    int total_containers;                // Total number of containers with policies
    int total_policy_entries;            // Total policy entries across all containers
    int max_map_capacity;                // Maximum capacity of policy map
    unsigned long long total_populate_ops; // Total populate operations performed
    unsigned long long total_cleanup_ops;  // Total cleanup operations performed
    unsigned long long failed_populate_ops; // Failed populate operations
    unsigned long long failed_cleanup_ops;  // Failed cleanup operations
};

/**
 * @brief Get global BPF policy enforcement metrics
 * 
 * Retrieves aggregate metrics about BPF policy enforcement across
 * all containers. Useful for monitoring, capacity planning, and debugging.
 * 
 * @param metrics Pointer to metrics structure to fill
 * @return 0 on success, -1 on error
 */
extern int containerv_bpf_manager_get_metrics(struct containerv_bpf_metrics* metrics);

/**
 * @brief Get BPF policy metrics for a specific container
 * 
 * Retrieves metrics about policy enforcement for a specific container.
 * Returns error if container not found or has no policy.
 * 
 * @param container_id Container ID (hostname) - must not be NULL
 * @param metrics Pointer to container metrics structure to fill
 * @return 0 on success, -1 on error (container not found or invalid params)
 */
extern int containerv_bpf_manager_get_container_metrics(
    const char* container_id,
    struct containerv_bpf_container_metrics* metrics
);

#endif //!__CONTAINERV_BPF_MANAGER_H__
