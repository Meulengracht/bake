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

#ifndef __POLICY_INTERNAL_H__
#define __POLICY_INTERNAL_H__

#include <chef/containerv/policy.h>

// Maximum policy entries
#define MAX_SYSCALLS 256
#define MAX_PATHS 256

struct containerv_syscall_entry {
    char* name;
};

struct containerv_path_entry {
    char*                 path;
    enum containerv_fs_access access;
};

struct containerv_policy {
    enum containerv_policy_type type;
    
    // Syscall whitelist
    struct containerv_syscall_entry syscalls[MAX_SYSCALLS];
    int                             syscall_count;
    
    // Filesystem path whitelist
    struct containerv_path_entry paths[MAX_PATHS];
    int                          path_count;
};

#endif //!__POLICY_INTERNAL_H__
