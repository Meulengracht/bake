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

#ifndef __POLICY_SECCOMP_H__
#define __POLICY_SECCOMP_H__

#include <chef/containerv/policy.h>

/**
 * @brief Apply seccomp-bpf filter based on policy
 * @param policy The security policy containing allowed syscalls
 * @return 0 on success, -1 on error
 */
extern int policy_seccomp_apply(struct containerv_policy* policy);

#endif //!__POLICY_SECCOMP_H__
