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

#endif //!__CONTAINERV_BPF_MANAGER_H__
