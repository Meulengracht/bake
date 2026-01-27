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

#define _GNU_SOURCE

#include "private.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <glob.h>
#include <vlog.h>

#ifdef HAVE_BPF_SKELETON
#include "fs-lsm.skel.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#endif

// import the private.h from the policies dir
#include "../policies/private.h"

// eBPF helper APIs (policy map updates, cgroup id lookup)
#include "../ebpf/private.h"

int policy_ebpf_load(
    struct containerv_container* container,
    struct containerv_policy*    policy)
{
#ifndef HAVE_BPF_SKELETON
    (void)container;
    (void)policy;
    return 0;
#else
    struct policy_ebpf_context* ctx;
    int                         status;

    if (container == NULL || policy == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (container->ebpf_context != NULL) {
        return 0;
    }
    
    VLOG_TRACE("containerv", "policy_ebpf: loading policy (type=%d, syscalls=%d, paths=%d)\n",
              policy->type, policy->syscall_count, policy->path_count);
    
    // Check if BPF LSM is available, otherwise we fallback on seccomp
    if (!bpf_check_lsm_available()) {
        VLOG_DEBUG("containerv", "policy_ebpf: BPF LSM not available, using seccomp fallback\n");
        return 0;
    }

    if (policy->path_count == 0) {
        VLOG_DEBUG("containerv", "policy_ebpf: no filesystem paths configured; skipping BPF LSM attach\n");
        return 0;
    }

    // Check if BPF programs are already loaded globally (by cvd daemon).
    // We require both a pinned policy map and a pinned enforcement link.
    // A pinned map alone can be stale (e.g., daemon crash/restart).
    int pinned_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/policy_map");
    int pinned_link_fd = bpf_obj_get("/sys/fs/bpf/cvd/fs_lsm_link");
    if (pinned_map_fd >= 0 && pinned_link_fd >= 0) {
        VLOG_DEBUG("containerv", "policy_ebpf: using globally pinned BPF enforcement from cvd daemon\n");
        
        // cvd daemon is managing BPF programs centrally, we just need to
        // track the map FD for potential use
        ctx = calloc(1, sizeof(*ctx));
        if (!ctx) {
            close(pinned_map_fd);
            close(pinned_link_fd);
            return -1;
        }
        
        ctx->policy_map_fd = pinned_map_fd;
        ctx->cgroup_id = bpf_get_cgroup_id(container->hostname);
        
        // Check if cgroup_id is valid
        if (ctx->cgroup_id == 0) {
            VLOG_WARNING("containerv", "policy_ebpf: failed to get cgroup_id for container '%s'\n", 
                        container->hostname);
            close(pinned_map_fd);
            close(pinned_link_fd);
            free(ctx);
            return -1;
        }

        // We only need the link as a liveness/enforcement check.
        // Close it after validation.
        close(pinned_link_fd);
        
        // Note: Policy population is handled by cvd daemon, not here
        // We just store the context for cleanup
        container->ebpf_context = ctx;
        
        VLOG_DEBUG("containerv", "policy_ebpf: attached to global BPF LSM enforcement\n");
        return 0;
    }

    if (pinned_map_fd >= 0) {
        close(pinned_map_fd);
    }
    if (pinned_link_fd >= 0) {
        close(pinned_link_fd);
    }
    
    // Fallback: Load BPF programs locally if not managed by cvd daemon
    // This maintains backward compatibility for standalone containerv use
    VLOG_DEBUG("containerv", "policy_ebpf: no global BPF manager found, loading programs locally\n");

    (void)bpf_bump_memlock_rlimit();

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return -1;
    }

    ctx->cgroup_id = bpf_get_cgroup_id(container->hostname);
    if (ctx->cgroup_id == 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to resolve cgroup ID for %s\n", container->hostname);
        free(ctx);
        return -1;
    }

    ctx->skel = fs_lsm_bpf__open();
    if (!ctx->skel) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to open BPF skeleton\n");
        free(ctx);
        return -1;
    }

    status = fs_lsm_bpf__load(ctx->skel);
    if (status) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to load BPF skeleton: %d\n", status);
        fs_lsm_bpf__destroy(ctx->skel);
        free(ctx);
        return -1;
    }

    status = fs_lsm_bpf__attach(ctx->skel);
    if (status) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to attach BPF LSM program: %d\n", status);
        fs_lsm_bpf__destroy(ctx->skel);
        free(ctx);
        return -1;
    }

    ctx->policy_map_fd = bpf_map__fd(ctx->skel->maps.policy_map);
    if (ctx->policy_map_fd < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to get policy_map FD\n");
        fs_lsm_bpf__destroy(ctx->skel);
        free(ctx);
        return -1;
    }

    ctx->dir_policy_map_fd = bpf_map__fd(ctx->skel->maps.dir_policy_map);
    if (ctx->dir_policy_map_fd < 0) {
        VLOG_WARNING("containerv", "policy_ebpf: failed to get dir_policy_map FD; directory rules disabled\n");
    }

    ctx->basename_policy_map_fd = bpf_map__fd(ctx->skel->maps.basename_policy_map);
    if (ctx->basename_policy_map_fd < 0) {
        VLOG_WARNING("containerv", "policy_ebpf: failed to get basename_policy_map FD; basename rules disabled\n");
    }

    struct bpf_policy_context bpf_ctx = {
        .map_fd = ctx->policy_map_fd,
        .dir_map_fd = ctx->dir_policy_map_fd,
        .basename_map_fd = ctx->basename_policy_map_fd,
        .cgroup_id = ctx->cgroup_id,
    };

    for (int i = 0; i < policy->path_count; i++) {
        const char* path = policy->paths[i].path;
        unsigned int allowMask = (unsigned int)policy->paths[i].access & (BPF_PERM_READ | BPF_PERM_WRITE | BPF_PERM_EXEC);

        if (!path) {
            continue;
        }

        status = bpf_manager_add_allow_pattern(&bpf_ctx, path, allowMask);
        if (status < 0) {
            VLOG_WARNING("containerv", "policy_ebpf: failed to apply allow rule for %s: %s\n", path, strerror(errno));
        }
    }

    container->ebpf_context = ctx;
    VLOG_DEBUG("containerv", "policy_ebpf: attached BPF LSM and installed %u allow entries\n", ctx->map_entries);
    return 0;
#endif
}

void policy_ebpf_unload(struct containerv_container* container)
{
    struct policy_ebpf_context* ctx;

    if (container == NULL) {
        errno = EINVAL;
        return;
    }

    ctx = (struct policy_ebpf_context*)container->ebpf_context;
    if (ctx == NULL) {
        return;
    }
    
    VLOG_DEBUG("containerv", "policy_ebpf: unloading policy\n");

#ifdef HAVE_BPF_SKELETON
    // Close the map FD if it's a reference to a pinned map
    if (ctx->policy_map_fd >= 0 && ctx->skel == NULL) {
        close(ctx->policy_map_fd);
        ctx->policy_map_fd = -1;
    }
    
    // Only destroy skeleton if we loaded it locally
    if (ctx->skel) {
        fs_lsm_bpf__destroy(ctx->skel);
        ctx->skel = NULL;
    }
#endif

    free(ctx);
    container->ebpf_context = NULL;
}
