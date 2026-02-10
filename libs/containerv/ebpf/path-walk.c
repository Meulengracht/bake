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

struct __path_walk_ctx {
    struct bpf_map_context* mapContext;
    unsigned int              allowMask;
};

static struct __path_walk_ctx* __g_walk_ctx;

static int __ends_with(const char* s, const char* suffix)
{
    size_t slen, suflen;
    if (!s || !suffix) {
        return 0;
    }
    slen = strlen(s);
    suflen = strlen(suffix);
    if (slen < suflen) {
        return 0;
    }
    return memcmp(s + (slen - suflen), suffix, suflen) == 0;
}

static int __has_glob_chars_range(const char* s, size_t n)
{
    if (!s) {
        return 0;
    }
    for (size_t i = 0; i < n && s[i]; i++) {
        char c = s[i];
        if (c == '*' || c == '?' || c == '[' || c == '+') {
            return 1;
        }
    }
    return 0;
}

static int __walk_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    struct __path_walk_ctx* ctx = __g_walk_ctx;
    struct stat             st;
    int                     status;

    (void)sb;
    (void)typeflag;
    (void)ftwbuf;

    if (!ctx || !ctx->mapContext) {
        return 1;
    }

    status = stat(fpath, &st);
    if (status < 0) {
        return 0;
    }

    status = bpf_policy_map_allow_inode(ctx->mapContext, st.st_dev, st.st_ino, ctx->allowMask);
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
    struct __path_walk_ctx ctx = {
        .mapContext   = mapContext,
        .allowMask = allowMask,
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

int bpf_manager_add_allow_pattern(
    struct bpf_map_context* mapContext,
    const char*             pattern,
    unsigned int            allowMask)
{
    size_t plen;
    char   base[PATH_MAX];
    char   globBuffer[PATH_MAX];
    glob_t g;
    int    status;

    // Handle special scalable forms: /dir/* and /dir/**
    plen = strlen(pattern);
    if (__ends_with(pattern, "/**") && plen >= 3) {
        snprintf(base, sizeof(base), "%.*s", (int)(plen - 3), pattern);
        return __allow_single_path(mapContext, base, allowMask, BPF_DIR_RULE_RECURSIVE);
    }
    if (__ends_with(pattern, "/*") && plen >= 2) {
        snprintf(base, sizeof(base), "%.*s", (int)(plen - 2), pattern);
        return __allow_single_path(mapContext, base, allowMask, BPF_DIR_RULE_CHILDREN_ONLY);
    }

    // Basename-only globbing: allow pattern under parent directory inode, without requiring files to exist.
    // Only applies when the parent path has no glob chars.
    if (mapContext->basename_map_fd >= 0 && __has_glob_chars_range(pattern, strlen(pattern))) {
        const char* last = strrchr(pattern, '/');
        if (last && last[1] != 0) {
            size_t parent_len = (size_t)(last - pattern);
            if (!__has_glob_chars_range(pattern, parent_len)) {
                char parent_path[PATH_MAX];
                char base_pat[PATH_MAX];
                struct stat st;

                if (parent_len == 0) {
                    snprintf(parent_path, sizeof(parent_path), "/");
                } else {
                    snprintf(parent_path, sizeof(parent_path), "%.*s", (int)parent_len, pattern);
                }
                snprintf(base_pat, sizeof(base_pat), "%s", last + 1);

                if (strcmp(base_pat, "*") == 0) {
                    // equivalent to children-only dir rule
                    return __allow_single_path(mapContext, parent_path, allowMask, BPF_DIR_RULE_CHILDREN_ONLY);
                }

                struct bpf_basename_rule rule = {};
                if (__parse_basename_rule(base_pat, allowMask, &rule) == 0) {
                    if (stat(parent_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                        if (bpf_basename_policy_map_allow_rule(mapContext, st.st_dev, st.st_ino, &rule) == 0) {
                            return 0;
                        }
                    }
                }
            }
        }
    }
    
    memset(&g, 0, sizeof(g));
    __glob_translate_plus(pattern, globBuffer, sizeof(globBuffer));
    status = glob(globBuffer, GLOB_NOSORT, NULL, &g);
    if (status == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            (void)__allow_path_or_tree(mapContext, g.gl_pathv[i], allowMask);
        }
        globfree(&g);
        return 0;
    }

    globfree(&g);
    /* If no glob matches, treat it as a literal path */
    return __allow_path_or_tree(mapContext, pattern, allowMask);
}
