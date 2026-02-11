/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
#include <ftw.h>
#include <glob.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vlog.h>

#include "map-ops.h"

// import the private.h from the policies dir
#include "../policies/private.h"

struct __path_walk_context {
    struct bpf_map_context* map_context;
    unsigned int            allow_mask;
};

static struct __path_walk_context* __g_walk_ctx;

static int __walk_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    struct __path_walk_context* ctx = __g_walk_ctx;
    struct stat                 st;
    int                         status;

    (void)sb;
    (void)typeflag;
    (void)ftwbuf;

    if (!ctx || !ctx->map_context) {
        return 1;
    }

    status = stat(fpath, &st);
    if (status < 0) {
        return 0;
    }

    status = bpf_policy_map_allow_inode(ctx->map_context, st.st_dev, st.st_ino, ctx->allow_mask);
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
    struct bpf_map_context* mapContext,
    const char*               rootPath,
    unsigned int              allowMask)
{
    struct __path_walk_context ctx = {
        .map_context   = mapContext,
        .allow_mask = allowMask,
    };

    __g_walk_ctx = &ctx;
    int status = nftw(rootPath, __walk_cb, 16, FTW_PHYS | FTW_MOUNT);
    __g_walk_ctx = NULL;
    return status;
}

static int __allow_single_path(
    struct bpf_map_context* mapContext,
    const char*             path,
    unsigned int            allowMask,
    unsigned int            dirFlags)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    if (S_ISDIR(st.st_mode)) {
        if (mapContext->dir_map_fd < 0) {
            errno = ENOTSUP;
            return -1;
        }
        return bpf_dir_policy_map_allow_dir(mapContext, st.st_dev, st.st_ino, allowMask, dirFlags);
    }
    return bpf_policy_map_allow_inode(mapContext, st.st_dev, st.st_ino, allowMask);
}

static int __allow_path_or_tree(
    struct bpf_map_context* mapContext,
    const char*             path,
    unsigned int            allowMask)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    if (S_ISDIR(st.st_mode)) {
        // Prefer scalable directory rules when available
        status = __allow_single_path(mapContext, path, allowMask, BPF_DIR_RULE_RECURSIVE);
        if (status == 0) {
            return 0;
        }
        // Fallback for older kernels/programs: enumerate all inodes
        return __allow_path_recursive(mapContext, path, allowMask);
    }

    return __allow_single_path(mapContext, path, allowMask, 0);
}
