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

#include "../private.h"

// import ebpf private header
#include "../../ebpf/private.h"

// Add default system paths (always needed for basic functionality)
static const struct containerv_policy_path g_basePolicyPaths[] = {
    { "/lib", CV_FS_READ | CV_FS_EXEC },
    { "/lib64", CV_FS_READ | CV_FS_EXEC },
    { "/usr/lib", CV_FS_READ | CV_FS_EXEC },
    { "/bin", CV_FS_READ | CV_FS_EXEC },
    { "/usr/bin", CV_FS_READ | CV_FS_EXEC },
    { "/dev/null", CV_FS_READ },
    { "/dev/zero", CV_FS_READ },
    { "/dev/urandom", CV_FS_READ },
    { "/dev/random", CV_FS_READ },
    { "/dev/tty", CV_FS_READ | CV_FS_WRITE },
    { "/etc/ld.so.cache", CV_FS_READ },  // Dynamic linker cache
    { "/etc/ld.so.conf", CV_FS_READ },   // Dynamic linker config
    { "/etc/ld.so.conf.d", CV_FS_READ }, // Dynamic linker config directory
    { "/proc/self", CV_FS_READ }, // Process self information
    { "/sys/devices/system/cpu", CV_FS_READ }, // CPU information (for runtime optimization)
    { NULL, 0 }
};

static const struct containerv_policy_path g_buildPolicyPaths[] = {
    { "/usr/include", CV_FS_READ | CV_FS_EXEC },
    { "/usr/share/pkgconfig", CV_FS_READ | CV_FS_EXEC },
    { "/usr/lib/pkgconfig", CV_FS_READ | CV_FS_EXEC },
    { NULL, 0 }
};

static const struct containerv_policy_path g_networkPolicyPaths[] = {
    { "/etc/ssl", CV_FS_READ | CV_FS_EXEC },
    { "/etc/ca-certificates", CV_FS_READ | CV_FS_EXEC },
    { "/etc/resolv.conf", CV_FS_READ | CV_FS_EXEC },
    { "/etc/hosts", CV_FS_READ | CV_FS_EXEC },
    { NULL, 0 }
};

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
        if (errno == ENOSPC) {
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
        if (errno == ENOSPC) {
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

static int add_paths_to_policy(
    struct containerv_policy*            policy,
    const struct containerv_policy_path* paths)
{
    int status;

    for (size_t i = 0; paths[i].path != NULL; i++) {
        status = __allow_pattern(policy, paths[i].path, (unsigned int)paths[i].access);
        if (status) {
            VLOG_ERROR("containerv", "policy_ebpf: failed to allow path '%s'\n", paths[i].path);
            return status;
        }
    }
    return 0;
}

int policy_ebpf_build(struct containerv_policy* policy, struct containerv_policy_plugin* plugin)
{
    if (policy == NULL || plugin == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (strcmp(plugin->name, "minimal") == 0) {
        return add_paths_to_policy(policy, &g_basePolicyPaths[0]);
    } else if (strcmp(plugin->name, "build") == 0) {
        return add_paths_to_policy(policy, &g_buildPolicyPaths[0]);
    } else if (strcmp(plugin->name, "network") == 0) {
        return add_paths_to_policy(policy, &g_networkPolicyPaths[0]);
    } else {
        VLOG_ERROR("containerv", "policy_ebpf: unknown plugin '%s'\n", plugin->name);
        errno = EINVAL;
        return -1;
    }
}
