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

#ifndef __CONTAINERV_BPF_H__
#define __CONTAINERV_BPF_H__

// Forward declarations
struct containerv_policy;

enum containerv_bpf_status {
    CV_BPF_UNINITIALIZED = 0,
    CV_BPF_AVAILABLE = 1,
    CV_BPF_NOT_SUPPORTED = 2
};

struct containerv_bpf_container_time_metrics {
    // Time taken to populate policy in microseconds
    unsigned long long policy_population_time_us;
    // Time taken to cleanup policy in microseconds
    unsigned long long policy_cleanup_time_us;
};

/**
 * @brief Container-specific BPF policy metrics
 */
struct containerv_bpf_container_metrics {
    unsigned long long cgroup_id;

    struct containerv_bpf_container_time_metrics time_metrics;
};

/**
 * @brief Global BPF policy enforcement metrics
 */
struct containerv_bpf_metrics {
    enum containerv_bpf_status status;
    int                        container_count;

    // Total populate operations performed
    unsigned long long total_populate_ops;
    // Total cleanup operations performed
    unsigned long long total_cleanup_ops;
    // Failed populate operations
    unsigned long long failed_populate_ops;
    // Failed cleanup operations
    unsigned long long failed_cleanup_ops;
};

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
extern int containerv_bpf_initialize(void);

/**
 * @brief Shutdown the BPF manager and clean up resources
 * 
 * This function unpins and destroys BPF programs and maps.
 * Should be called during application shutdown.
 */
extern void containerv_bpf_shutdown(void);

/**
 * @brief Check if BPF LSM enforcement is available
 * 
 * Returns whether the BPF manager successfully initialized and is ready to
 * enforce policies. This is a best-effort check and may return false negatives
 * in certain environments (e.g., older kernels, missing features).
 */
extern enum containerv_bpf_status containerv_bpf_is_available(void);

/**
 * @brief Get the file descriptor for the policy map
 * 
 * This returns the FD for the pinned policy map that can be used
 * to populate per-container policies.
 * 
 * @return Map file descriptor, or -1 if BPF unavailable
 */
extern int containerv_bpf_get_profile_map_fd(void);

/**
 * @brief Populate BPF policy for a container
 * 
 * After container rootfs and cgroup setup, this function resolves
 * configured allowed paths to (dev, ino) within the container's
 * filesystem view and populates the BPF policy map.
 * 
 * @param containerId Container ID (hostname)
 * @param rootfsPath Path to the container's rootfs
 * @param policy Security policy with path rules
 * @return 0 on success, -1 on error
 */
extern int containerv_bpf_populate_policy(
    const char*               containerId,
    const char*               rootfsPath,
    struct containerv_policy* policy
);

/**
 * @brief Remove BPF policy entries for a container
 * 
 * Cleans up all cgroup-specific data in BPF maps when a container
 * is destroyed.
 * 
 * @param containerId Container ID (hostname)
 * @return 0 on success, -1 on error
 */
extern int containerv_bpf_cleanup_policy(const char* containerId);

/**
 * @brief Get global BPF policy enforcement metrics
 * 
 * Retrieves aggregate metrics about BPF policy enforcement across
 * all containers. Useful for monitoring, capacity planning, and debugging.
 * 
 * @param metrics Pointer to metrics structure to fill
 * @return 0 on success, -1 on error
 */
extern int containerv_bpf_get_metrics(struct containerv_bpf_metrics* metrics);

/**
 * @brief Get BPF policy metrics for a specific container
 * 
 * Retrieves metrics about policy enforcement for a specific container.
 * Returns error if container not found or has no policy.
 * 
 * @param containerId Container ID (hostname) - must not be NULL
 * @param metrics Pointer to container metrics structure to fill
 * @return 0 on success, -1 on error (container not found or invalid params)
 */
extern int containerv_bpf_get_container_metrics(
    const char*                              containerId,
    struct containerv_bpf_container_metrics* metrics
);

/**
 * @brief Sanity check pinned BPF enforcement artifacts
 *
 * Validates that both the pinned policy map and the pinned enforcement link
 * exist under /sys/fs/bpf/cvd. A pinned map alone can be stale (e.g., daemon
 * crash/restart after pinning), so callers should use this to confirm that
 * enforcement is actually active.
 *
 * On non-Linux builds or when BPF skeleton support is not compiled in, this
 * is a no-op and returns success.
 *
 * @return 0 if both are present (or not applicable), -1 if missing
 */
extern int containerv_bpf_sanity_check_pins(void);

#endif //!__CONTAINERV_BPF_H__
