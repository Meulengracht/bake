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

#define _GNU_SOURCE

#include "policy-seccomp.h"
#include <errno.h>
#include <seccomp.h>
#include <string.h>
#include <vlog.h>

// Get syscall policy structure (defined in policy.c)
struct containerv_syscall_entry {
    char* name;
};

struct containerv_path_entry {
    char*                 path;
    enum containerv_fs_access access;
};

struct containerv_policy {
    enum containerv_policy_type type;
    struct containerv_syscall_entry syscalls[256];
    int                             syscall_count;
    struct containerv_path_entry paths[256];
    int                          path_count;
};

int policy_seccomp_apply(struct containerv_policy* policy)
{
    scmp_filter_ctx ctx = NULL;
    int status = -1;
    
    if (policy == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    VLOG_INFO("containerv", "policy_seccomp: applying policy with %d allowed syscalls\n",
              policy->syscall_count);
    
    // Create a seccomp filter with default deny
    ctx = seccomp_init(SCMP_ACT_ERRNO(EPERM));
    if (ctx == NULL) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to initialize seccomp context\n");
        return -1;
    }
    
    // Add all allowed syscalls from the policy
    for (int i = 0; i < policy->syscall_count; i++) {
        const char* syscall_name = policy->syscalls[i].name;
        int syscall_nr = seccomp_syscall_resolve_name(syscall_name);
        
        if (syscall_nr == __NR_SCMP_ERROR) {
            // Syscall might not exist on this architecture - log and continue
            VLOG_DEBUG("containerv", "policy_seccomp: syscall '%s' not found on this architecture\n",
                      syscall_name);
            continue;
        }
        
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0) != 0) {
            VLOG_ERROR("containerv", "policy_seccomp: failed to add rule for syscall '%s'\n",
                      syscall_name);
            goto cleanup;
        }
    }
    
    // Disable NO_NEW_PRIVS requirement (we handle privileges separately)
    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0) != 0) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to set NNP attribute\n");
        goto cleanup;
    }
    
    // Load the filter into the kernel
    if (seccomp_load(ctx) != 0) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to load seccomp filter: %s\n",
                  strerror(errno));
        goto cleanup;
    }
    
    VLOG_INFO("containerv", "policy_seccomp: policy applied successfully\n");
    status = 0;
    
cleanup:
    if (ctx != NULL) {
        seccomp_release(ctx);
    }
    
    return status;
}
