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

#ifndef __POLICY_EBPF_H__
#define __POLICY_EBPF_H__

#include <chef/containerv/policy.h>

struct containerv_container;

/**
 * @brief Load and attach eBPF programs for the given policy
 * @param container The container to apply the policy to
 * @param policy The security policy to enforce
 * @return 0 on success, -1 on error
 */
extern int policy_ebpf_load(
    struct containerv_container*  container,
    struct containerv_policy*     policy
);

/**
 * @brief Unload and detach eBPF programs for the container
 * @param container The container to remove policy from
 * @return 0 on success, -1 on error
 */
extern int policy_ebpf_unload(struct containerv_container* container);

/**
 * @brief Add a path-based allow rule to the BPF policy map
 * @param policy_map_fd File descriptor of the policy BPF map
 * @param cgroup_id Cgroup ID for the container
 * @param path Filesystem path to allow
 * @param allow_mask Bitmask of allowed permissions (0x1=READ, 0x2=WRITE, 0x4=EXEC)
 * @return 0 on success, -1 on error
 */
extern int policy_ebpf_add_path_allow(int policy_map_fd, unsigned long long cgroup_id,
                                      const char* path, unsigned int allow_mask);

/**
 * @brief Add a path-based deny rule to the BPF policy map
 *
 * Compatibility wrapper: the underlying map is allow-list based.
 */
extern int policy_ebpf_add_path_deny(int policy_map_fd, unsigned long long cgroup_id,
                                     const char* path, unsigned int deny_mask);

#endif //!__POLICY_EBPF_H__
