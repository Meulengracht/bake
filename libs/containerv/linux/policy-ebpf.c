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

#include "policy-ebpf.h"
#include "policy-internal.h"
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

/* Permission bits - matching BPF program definitions */
#define PERM_READ  0x1
#define PERM_WRITE 0x2
#define PERM_EXEC  0x4

/* Policy key: (cgroup_id, dev, ino) - must match BPF program */
struct policy_key {
    unsigned long long cgroup_id;
    unsigned long long dev;
    unsigned long long ino;
};

/* Policy value: permission mask (bit flags for deny) */
struct policy_value {
    unsigned int allow_mask;
};

/* Internal structure to track loaded eBPF programs */
struct policy_ebpf_context {
    int policy_map_fd;
    unsigned long long cgroup_id;

#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf* skel;
#endif

    unsigned int map_entries;
};

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int __find_in_file(FILE* fp, const char* target)
{
    char  buffer[1024];
    char* ptr = &buffer[0];
    
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        return 0;
    }

    // Look for "bpf" as a complete word (not substring)
    while (ptr) {
        // Find next occurrence of "bpf"
        ptr = strstr(ptr, "bpf");
        if (!ptr) {
            break;
        }
        
        // Check if it's a complete word (surrounded by comma, newline, or string boundaries)
        int isStart = (ptr == &buffer[0] || ptr[-1] == ',');
        int isEnd = (ptr[3] == '\0' || ptr[3] == ',' || ptr[3] == '\n');
        if (isStart && isEnd) {
            return 1;
        }
        
        // Move past "bpf" to continue searching
        ptr += 3;
    }
    return 0;
}

static int __check_bpf_lsm(void)
{
    FILE* fp;
    int   available = 0;
    
    // Check /sys/kernel/security/lsm for "bpf"
    fp = fopen("/sys/kernel/security/lsm", "r");
    if (!fp) {
        VLOG_DEBUG("containerv", "policy_ebpf: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    available = __find_in_file(fp, "bpf");
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("containerv", "policy_ebpf: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
    }
    
    return available;
}

static unsigned long long __get_cgroup_id(const char* hostname)
{
    char               cgroupPath[512];
    int                fd;
    struct stat        st;
    unsigned long long cgroupID;
    const char*        c;
    
    if (hostname == NULL) {
        errno = EINVAL;
        return 0;
    }
    
    // Validate hostname to prevent path traversal
    // Only allow alphanumeric, hyphen, underscore, and period
    for (c = hostname; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') ||
              (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') ||
              *c == '-' || *c == '_' || *c == '.')) {
            VLOG_ERROR(
                "containerv",
                "policy_ebpf: invalid hostname contains illegal character: %s\n", 
                hostname
            );
            errno = EINVAL;
            return 0;
        }
    }
    
    // Ensure hostname doesn't start with . or ..
    if (hostname[0] == '.') {
        VLOG_ERROR(
            "containerv",
            "policy_ebpf: invalid hostname starts with dot: %s\n",
            hostname
        );
        errno = EINVAL;
        return 0;
    }
    
    // Build cgroup path
    snprintf(cgroupPath, sizeof(cgroupPath), "/sys/fs/cgroup/%s", hostname);
    
    // Open cgroup directory
    fd = open(cgroupPath, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to open cgroup %s: %s\n", 
                   cgroupPath, strerror(errno));
        return 0;
    }
    
    // Get inode number which serves as cgroup ID
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroupID = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("containerv", "policy_ebpf: cgroup %s has ID %llu\n", hostname, 
               (unsigned long long)cgroupID);
    
    return cgroupID;
}

static int __bump_memlock_rlimit(void)
{
    struct rlimit rlim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

static int __policy_map_allow_inode(
    int                policyMapFD,
    unsigned long long cgroupID,
    dev_t              dev,
    ino_t              ino,
    unsigned int       allowMask)
{
    struct policy_key   key = {};
    struct policy_value value = {};
    union bpf_attr      attr = {};

    key.cgroup_id = cgroupID;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    value.allow_mask = allowMask;

    attr.map_fd = policyMapFD;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

struct __path_walk_ctx {
    struct policy_ebpf_context* ebpf;
    int                         policyMapFD;
    unsigned long long          cgroupID;
    unsigned int                allowMask;
};

static struct __path_walk_ctx* __g_walk_ctx;

static int __walk_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    struct __path_walk_ctx* ctx = __g_walk_ctx;
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;

    if (!ctx || !ctx->ebpf) {
        return 1;
    }

    if (ctx->ebpf->map_entries >= 10240) {
        return 1; /* stop walking */
    }

    /* Use stat() so symlinks resolve to their target inode */
    {
        struct stat st;
        int status = stat(fpath, &st);
        if (status < 0) {
            return 0;
        }

        status = __policy_map_allow_inode(ctx->policyMapFD, ctx->cgroupID, st.st_dev, st.st_ino, ctx->allowMask);
        if (status < 0) {
            return 0;
        }
        ctx->ebpf->map_entries++;
    }
    return 0;
}

static int __allow_path_recursive(
    struct policy_ebpf_context* ebpf,
    int                         policyMapFD,
    unsigned long long          cgroupID,
    const char*                 rootPath,
    unsigned int                allowMask)
{
    struct __path_walk_ctx ctx = {
        .ebpf = ebpf,
        .policyMapFD = policyMapFD,
        .cgroupID = cgroupID,
        .allowMask = allowMask,
    };

    __g_walk_ctx = &ctx;
    int status = nftw(rootPath, __walk_cb, 16, FTW_PHYS | FTW_MOUNT);
    __g_walk_ctx = NULL;
    return status;
}

static int __allow_path_or_tree(
    struct policy_ebpf_context* ebpf,
    int                         policyMapFD,
    unsigned long long          cgroupID,
    const char*                 path,
    unsigned int                allowMask)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    status = __policy_map_allow_inode(policyMapFD, cgroupID, st.st_dev, st.st_ino, allowMask);
    if (status < 0) {
        return status;
    }
    ebpf->map_entries++;

    if (S_ISDIR(st.st_mode)) {
        return __allow_path_recursive(ebpf, policyMapFD, cgroupID, path, allowMask);
    }
    return 0;
}

static int __allow_pattern(
    struct policy_ebpf_context* ebpf,
    int                         policyMapFD,
    unsigned long long          cgroupID,
    const char*                 pattern,
    unsigned int                allowMask)
{
    glob_t g;
    int status;

    memset(&g, 0, sizeof(g));
    status = glob(pattern, GLOB_NOSORT, NULL, &g);
    if (status == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            if (ebpf->map_entries >= 10240) {
                break;
            }
            (void)__allow_path_or_tree(ebpf, policyMapFD, cgroupID, g.gl_pathv[i], allowMask);
        }
        globfree(&g);
        return 0;
    }

    globfree(&g);
    /* If no glob matches, treat it as a literal path */
    return __allow_path_or_tree(ebpf, policyMapFD, cgroupID, pattern, allowMask);
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
    if (!__check_bpf_lsm()) {
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
        ctx->cgroup_id = __get_cgroup_id(container->hostname);
        
        // Note: Policy population is handled by cvd daemon, not here
        // We just store the context for cleanup
        container->ebpf_context = ctx;
        
        VLOG_DEBUG("containerv", "policy_ebpf: attached to global BPF LSM enforcement\n");
        return 0;
    }
    
    // Fallback: Load BPF programs locally if not managed by cvd daemon
    // This maintains backward compatibility for standalone containerv use
    VLOG_DEBUG("containerv", "policy_ebpf: no global BPF manager found, loading programs locally\n");

    (void)__bump_memlock_rlimit();

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return -1;
    }

    ctx->cgroup_id = __get_cgroup_id(container->hostname);
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
        unsigned int allowMask = (unsigned int)policy->paths[i].access & (PERM_READ | PERM_WRITE | PERM_EXEC);

        if (!path) {
            continue;
        }

        if (ctx->map_entries >= 10240) {
            VLOG_WARNING("containerv", "policy_ebpf: policy_map full; not all allow rules installed\n");
            break;
        }

        status = __allow_pattern(ctx, ctx->policy_map_fd, ctx->cgroup_id, path, allowMask);
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

int policy_ebpf_add_path_allow(
    int                policyMapFD,
    unsigned long long cgroupID,
    const char*        path,
    unsigned int       allowMask)
{
    struct stat         st;
    int                 status;
    
    if (policyMapFD < 0 || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    // Resolve path to (dev, ino)
    status = stat(path, &st);
    if (status < 0) {
        VLOG_ERROR(
            "containerv",
            "policy_ebpf_add_path_deny: failed to stat %s: %s\n",
            path, strerror(errno)
        );
        return status;
    }
    
    status = __policy_map_allow_inode(
        policyMapFD,
        cgroupID,
        st.st_dev,
        st.st_ino,
        allowMask
    );
    if (status < 0) {
        VLOG_ERROR(
            "containerv",
            "policy_ebpf_add_path_allow: failed to update map: %s\n",
            strerror(errno)
        );
        return status;
    }
    
    VLOG_DEBUG(
        "containerv",
        "policy_ebpf: added allow rule for %s (dev=%lu, ino=%lu, mask=0x%x)\n",
        path,
        (unsigned long)st.st_dev,
        (unsigned long)st.st_ino,
        allowMask
    );
    return 0;
}

int policy_ebpf_add_path_deny(
    int                policyMapFD,
    unsigned long long cgroupID,
    const char*        path,
    unsigned int       denyMask)
{
    const unsigned int all = (PERM_READ | PERM_WRITE | PERM_EXEC);
    return policy_ebpf_add_path_allow(policyMapFD, cgroupID, path, all & ~denyMask);
}
