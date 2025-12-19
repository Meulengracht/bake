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

#endif //!__POLICY_EBPF_H__
