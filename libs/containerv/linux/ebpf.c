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
#include <ftw.h>
#include <vlog.h>

#ifdef HAVE_BPF_SKELETON
#include "fs-lsm.skel.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#endif

// import the private.h from the policies dir
#include "../policies/private.h"

struct __path_walk_ctx {
    struct containerv_policy* policy;
    unsigned int              allowMask;
};

static struct __path_walk_ctx* __g_walk_ctx;

static int __walk_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    struct __path_walk_ctx* ctx = __g_walk_ctx;
    struct stat             st;
    int                     status;

    (void)sb;
    (void)typeflag;
    (void)ftwbuf;

    if (!ctx || !ctx->policy) {
        return 1;
    }

    status = stat(fpath, &st);
    if (status < 0) {
        return 0;
    }

    status = bpf_policy_map_allow_inode(ctx->policy->backend_context, st.st_dev, st.st_ino, ctx->allowMask);
    if (status < 0) {
        if (errno == ENOBUFS) {
            VLOG_ERROR("containerv", "policy_ebpf: BPF policy map full while allowing path '%s'\n", fpath);
            return 1; // Stop walking
        }
        VLOG_ERROR("containerv", "policy_ebpf: failed to allow path '%s'\n", fpath);
        return 0;
    }
    return 0;
}

static int __allow_path_recursive(
    struct containerv_policy* policy,
    const char*               rootPath,
    unsigned int              allowMask)
{
    struct __path_walk_ctx ctx = {
        .policy    = policy,
        .allowMask = allowMask,
    };

    __g_walk_ctx = &ctx;
    int status = nftw(rootPath, __walk_cb, 16, FTW_PHYS | FTW_MOUNT);
    __g_walk_ctx = NULL;
    return status;
}

static int __allow_path_or_tree(
    struct containerv_policy* policy,
    const char*               path,
    unsigned int              allowMask)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    status = bpf_policy_map_allow_inode(policy->backend_context, st.st_dev, st.st_ino, allowMask);
    if (status < 0) {
        if (errno == ENOBUFS) {
            VLOG_ERROR("containerv", "__allow_path_or_tree: BPF policy map full while allowing path '%s'\n", path);
        }
        VLOG_ERROR("containerv", "__allow_path_or_tree: failed to allow path '%s'\n", path);
        return status;
    }
    
    if (S_ISDIR(st.st_mode)) {
        return __allow_path_recursive(policy, path, allowMask);
    }
    return 0;
}

static int __allow_pattern(
    struct containerv_policy* policy,
    const char*               pattern,
    unsigned int              allowMask)
{
    glob_t g;
    int status;

    memset(&g, 0, sizeof(g));
    status = glob(pattern, GLOB_NOSORT, NULL, &g);
    if (status == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            (void)__allow_path_or_tree(policy, g.gl_pathv[i], allowMask);
        }
        globfree(&g);
        return 0;
    }

    globfree(&g);
    /* If no glob matches, treat it as a literal path */
    return __allow_path_or_tree(policy, pattern, allowMask);
}

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

    // Check if BPF programs are already loaded globally (by cvd daemon)
    // by checking if the policy map is already pinned
    int pinned_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/policy_map");
    if (pinned_map_fd >= 0) {
        VLOG_DEBUG("containerv", "policy_ebpf: using globally pinned BPF programs from cvd daemon\n");
        
        // cvd daemon is managing BPF programs centrally, we just need to
        // track the map FD for potential use
        ctx = calloc(1, sizeof(*ctx));
        if (!ctx) {
            close(pinned_map_fd);
            return -1;
        }
        
        ctx->policy_map_fd = pinned_map_fd;
        ctx->cgroup_id = bpf_get_cgroup_id(container->hostname);
        
        // Check if cgroup_id is valid
        if (ctx->cgroup_id == 0) {
            VLOG_WARNING("containerv", "policy_ebpf: failed to get cgroup_id for container '%s'\n", 
                        container->hostname);
            close(pinned_map_fd);
            free(ctx);
            return -1;
        }
        
        // Note: Policy population is handled by cvd daemon, not here
        // We just store the context for cleanup
        container->ebpf_context = ctx;
        
        VLOG_DEBUG("containerv", "policy_ebpf: attached to global BPF LSM enforcement\n");
        return 0;
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

    for (int i = 0; i < policy->path_count; i++) {
        const char* path = policy->paths[i].path;
        unsigned int allowMask = (unsigned int)policy->paths[i].access & (BPF_PERM_READ | BPF_PERM_WRITE | BPF_PERM_EXEC);

        if (!path) {
            continue;
        }

        status = __allow_pattern(policy, path, allowMask);
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
