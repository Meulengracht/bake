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

#ifndef __POLICY_INTERNAL_H__
#define __POLICY_INTERNAL_H__

#include <chef/containerv/policy.h>

// Maximum policy entries
#define MAX_SYSCALLS 256
#define MAX_PATHS 256

struct containerv_syscall_entry {
    char* name;
};

struct containerv_policy_path {
    char*                     path;
    enum containerv_fs_access access;
};

/* Internal structure to track loaded eBPF programs */
struct policy_ebpf_context {
    int policy_map_fd;
    int dir_policy_map_fd;
    int basename_policy_map_fd;
    unsigned long long cgroup_id;

#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf* skel;
#endif

    unsigned int map_entries;
};

struct containerv_policy {
    void* backend_context;

    // Generic security level (best-effort; backend-dependent)
    enum containerv_security_level security_level;

#ifdef _WIN32
    // Windows isolation settings (optional)
    int   win_use_app_container;
    char* win_integrity_level;
    char** win_capability_sids;
    int   win_capability_sid_count;
#endif

    // Syscall whitelist
    struct containerv_syscall_entry syscalls[MAX_SYSCALLS];
    int                             syscall_count;
    
    // Filesystem path whitelist
    struct containerv_policy_path paths[MAX_PATHS];
    int                           path_count;
};

struct containerv_policy_handler {
    const char* name;
    int       (*apply)(struct containerv_policy* policy, struct containerv_policy_plugin* plugin);
};

extern int policy_seccomp_build(struct containerv_policy* policy, struct containerv_policy_plugin* plugin);
extern int policy_ebpf_build(struct containerv_policy* policy, struct containerv_policy_plugin* plugin);

#if defined(__linux__) || defined(__unix__)
static const struct containerv_policy_handler g_policy_handlers[] = {
    { "seccomp", policy_seccomp_build },
    { "ebpf",    policy_ebpf_build },
    { NULL,      NULL }
};
#else
static const struct containerv_policy_handler g_policy_handlers[] = {
    { NULL, NULL }
};
#endif

#endif //!__POLICY_INTERNAL_H__
