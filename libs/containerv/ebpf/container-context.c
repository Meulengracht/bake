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
#include <glob.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vlog.h>

#include "container-context.h"
#include "map-ops.h"

// import the private.h from the policies dir
#include "../policies/private.h"

struct bpf_container_context* bpf_container_context_new(
    const char*        containerId,
    unsigned long long cgroupId)
{
    struct bpf_container_context* context;
    
    context = calloc(1, sizeof(struct bpf_container_context));
    if (context == NULL) {
        return NULL;
    }
    
    context->container_id = strdup(containerId);
    if (context->container_id == NULL) {
        free(context);
        return NULL;
    }
    
    // Set initial members and capacities
    context->cgroup_id = cgroupId;
    context->file.file_key_capacity = 256;
    context->file.dir_key_capacity = 64;
    context->file.basename_key_capacity = 32;
    context->net.create_key_capacity = 16;
    context->net.tuple_key_capacity = 32;
    context->net.unix_key_capacity = 16;

    context->file.file_keys = malloc(sizeof(struct bpf_policy_key) * context->file.file_key_capacity);
    context->file.dir_keys = malloc(sizeof(struct bpf_policy_key) * context->file.dir_key_capacity);
    context->file.basename_keys = malloc(sizeof(struct bpf_policy_key) * context->file.basename_key_capacity);
    context->net.create_keys = malloc(sizeof(struct bpf_net_create_key) * context->net.create_key_capacity);
    context->net.tuple_keys = malloc(sizeof(struct bpf_net_tuple_key) * context->net.tuple_key_capacity);
    context->net.unix_keys = malloc(sizeof(struct bpf_net_unix_key) * context->net.unix_key_capacity);

    if (context->file.file_keys == NULL || context->file.dir_keys == NULL||
        context->file.basename_keys == NULL || context->net.create_keys == NULL||
        context->net.tuple_keys == NULL|| context->net.unix_keys == NULL) {
        bpf_container_context_delete(context);
        return NULL;
    }
    return context;
}

void bpf_container_context_delete(struct bpf_container_context* context)
{
    if (context == NULL) {
        return;
    }

    free(context->net.tuple_keys);
    free(context->net.create_keys);
    free(context->net.unix_keys);
    free(context->file.basename_keys);
    free(context->file.dir_keys);
    free(context->file.file_keys);
    free(context->container_id);
    free(context);
}

static int __ensure_capacity_sized(void** keys, int* count, int* capacity, size_t elem_size)
{
    if (*count < *capacity) {
        return 0;
    }
    if (*capacity >= MAX_TRACKED_ENTRIES) {
        return -1;
    }
    int new_capacity = (*capacity * 2 < MAX_TRACKED_ENTRIES) ? (*capacity * 2) : MAX_TRACKED_ENTRIES;
    void* new_keys = realloc(*keys, elem_size * (size_t)new_capacity);
    if (!new_keys) {
        return -1;
    }
    *keys = new_keys;
    *capacity = new_capacity;
    return 0;
}

int bpf_container_context_add_tracked_file_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino)
{
    struct bpf_policy_key* key;
    
    if (!context) {
        return -1;
    }
    
    if (__ensure_capacity_sized((void**)&context->file.file_keys, &context->file.file_key_count, &context->file.file_key_capacity,
                                sizeof(*context->file.file_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked file key capacity\n");
        return -1;
    }
    
    // Add the key
    key = &context->file.file_keys[context->file.file_key_count];
    key->cgroup_id = context->cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    context->file.file_key_count++;
    
    return 0;
}

int bpf_container_context_add_tracked_dir_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino)
{
    struct bpf_policy_key* key;

    if (!context) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->file.dir_keys, &context->file.dir_key_count, &context->file.dir_key_capacity,
                                sizeof(*context->file.dir_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked dir key capacity\n");
        return -1;
    }

    key = &context->file.dir_keys[context->file.dir_key_count];
    key->cgroup_id = context->cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    context->file.dir_key_count++;
    return 0;
}

int bpf_container_context_add_tracked_basename_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino)
{
    struct bpf_policy_key* key;

    if (!context) {
        return -1;
    }

    // Avoid duplicates (we delete per-dir key as a whole)
    for (int i = 0; i < context->file.basename_key_count; i++) {
        if (context->file.basename_keys[i].cgroup_id == context->cgroup_id &&
            context->file.basename_keys[i].dev == (unsigned long long)dev &&
            context->file.basename_keys[i].ino == (unsigned long long)ino) {
            return 0;
        }
    }

    if (__ensure_capacity_sized((void**)&context->file.basename_keys, &context->file.basename_key_count, &context->file.basename_key_capacity,
                                sizeof(*context->file.basename_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked basename key capacity\n");
        return -1;
    }

    key = &context->file.basename_keys[context->file.basename_key_count];
    key->cgroup_id = context->cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    context->file.basename_key_count++;
    return 0;
}

int bpf_container_context_add_tracked_net_create_entry(
    struct bpf_container_context*    context,
    const struct bpf_net_create_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.create_keys,
                                &context->net.create_key_count,
                                &context->net.create_key_capacity,
                                sizeof(*context->net.create_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net create key capacity\n");
        return -1;
    }

    context->net.create_keys[context->net.create_key_count++] = *key;
    return 0;
}

int bpf_container_context_add_tracked_net_tuple_entry(
    struct bpf_container_context* context,
    const struct bpf_net_tuple_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.tuple_keys,
                                &context->net.tuple_key_count,
                                &context->net.tuple_key_capacity,
                                sizeof(*context->net.tuple_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net tuple key capacity\n");
        return -1;
    }

    context->net.tuple_keys[context->net.tuple_key_count++] = *key;
    return 0;
}

int bpf_container_context_add_tracked_net_unix_entry(
    struct bpf_container_context* context,
    const struct bpf_net_unix_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.unix_keys,
                                &context->net.unix_key_count,
                                &context->net.unix_key_capacity,
                                sizeof(*context->net.unix_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net unix key capacity\n");
        return -1;
    }

    context->net.unix_keys[context->net.unix_key_count++] = *key;
    return 0;
}

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

static int __has_glob_chars(const char* s)
{
    if (!s) {
        return 0;
    }
    for (const char* p = s; *p; p++) {
        if (*p == '*' || *p == '?' || *p == '[' || *p == '+') {
            return 1;
        }
    }
    return 0;
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

static int __has_disallowed_basename_chars_range(const char* s, size_t n)
{
    if (!s) {
        return 0;
    }
    for (size_t i = 0; i < n && s[i]; i++) {
        char c = s[i];
        // '?' is supported by basename matcher; everything else is disallowed in prefix/tail fragments
        if (c == '*' || c == '[' || c == '+') {
            return 1;
        }
    }
    return 0;
}

static int __basename_push_literal(
    struct bpf_basename_rule* out,
    const char*               s,
    size_t                    n)
{
    if (!out || !s) {
        errno = EINVAL;
        return -1;
    }

    if (n == 0) {
        return 0;
    }
    if (n >= BPF_BASENAME_MAX_STR) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (__has_disallowed_basename_chars_range(s, n)) {
        errno = EINVAL;
        return -1;
    }
    if (out->token_count >= BPF_BASENAME_TOKEN_MAX) {
        errno = ENOSPC;
        return -1;
    }

    unsigned char idx = out->token_count;
    out->token_type[idx] = BPF_BASENAME_TOKEN_LITERAL;
    out->token_len[idx] = (unsigned char)n;
    memcpy(out->token[idx], s, n);
    out->token[idx][n] = 0;
    out->token_count++;
    return 0;
}

static int __basename_push_digit(struct bpf_basename_rule* out, int one_or_more)
{
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    if (out->token_count >= BPF_BASENAME_TOKEN_MAX) {
        errno = ENOSPC;
        return -1;
    }

    unsigned char idx = out->token_count;
    out->token_type[idx] = one_or_more ? BPF_BASENAME_TOKEN_DIGITSPLUS : BPF_BASENAME_TOKEN_DIGIT1;
    out->token_len[idx] = 0;
    out->token[idx][0] = 0;
    out->token_count++;
    return 0;
}

static int __parse_basename_rule(const char* pattern, unsigned int allow_mask, struct bpf_basename_rule* out)
{
    if (!pattern || !out) {
        errno = EINVAL;
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->allow_mask = allow_mask;

    size_t plen = strlen(pattern);
    if (plen == 0) {
        errno = EINVAL;
        return -1;
    }

    // Only a trailing '*' is supported (tail wildcard).
    size_t n = plen;
    if (n > 0 && pattern[n - 1] == '*') {
        out->tail_wildcard = 1;
        n--;
        if (n == 0) {
            errno = EINVAL;
            return -1;
        }
    }

    // Tokenize into literals and digit segments. Supported segments:
    //   [0-9]   -> exactly one digit
    //   [0-9]+  -> one-or-more digits
    const char* lit_start = pattern;
    size_t i = 0;
    while (i < n) {
        if (pattern[i] == '*') {
            // internal '*' is not supported
            errno = EINVAL;
            return -1;
        }
        if (pattern[i] == '+') {
            // '+' is only supported as part of "[0-9]+"
            errno = EINVAL;
            return -1;
        }
        if (pattern[i] == '[') {
            if (i + strlen("[0-9]") <= n && memcmp(&pattern[i], "[0-9]", strlen("[0-9]")) == 0) {
                // push preceding literal
                size_t lit_len = (size_t)(&pattern[i] - lit_start);
                if (__basename_push_literal(out, lit_start, lit_len) < 0) {
                    return -1;
                }
                i += strlen("[0-9]");

                int one_or_more = 0;
                if (i < n && pattern[i] == '+') {
                    one_or_more = 1;
                    i++;
                }
                if (__basename_push_digit(out, one_or_more) < 0) {
                    return -1;
                }
                lit_start = &pattern[i];
                continue;
            }

            // Any other bracket expression isn't supported in basename rules
            errno = ENOTSUP;
            return -1;
        }
        i++;
    }

    // push final literal
    if (__basename_push_literal(out, lit_start, (size_t)(&pattern[n] - lit_start)) < 0) {
        return -1;
    }

    if (out->token_count == 0) {
        errno = EINVAL;
        return -1;
    }
    if (out->tail_wildcard) {
        unsigned char last = (unsigned char)(out->token_count - 1);
        if (out->token_type[last] != BPF_BASENAME_TOKEN_LITERAL || out->token_len[last] == 0) {
            // BPF matcher only treats a trailing wildcard as "last literal is prefix".
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

static void __glob_translate_plus(const char* in, char* out, size_t outSize)
{
    size_t i = 0;

    if (!out || outSize == 0) {
        return;
    }
    if (!in) {
        out[0] = 0;
        return;
    }

    for (; in[i] && i + 1 < outSize; i++) {
        out[i] = (in[i] == '+') ? '*' : in[i];
    }
    out[i] = 0;
}

static int __apply_single_path(
    struct bpf_map_context*       mapContext,
    struct bpf_container_context* containerContext,
    const char*                   resolvedPath,
    unsigned int                  allowMask,
    unsigned int                  directoryFlags)
{
    struct stat st;
    if (stat(resolvedPath, &st) < 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (bpf_dir_policy_map_allow_dir(mapContext, st.st_dev, st.st_ino, allowMask, directoryFlags) < 0) {
            return -1;
        }
        (void)bpf_container_context_add_tracked_dir_entry(containerContext, st.st_dev, st.st_ino);
        return 0;
    }

    if (bpf_policy_map_allow_inode(mapContext, st.st_dev, st.st_ino, allowMask) < 0) {
        return -1;
    }
    (void)bpf_container_context_add_tracked_file_entry(containerContext, st.st_dev, st.st_ino);
    return 0;
}

void bpf_container_context_apply_paths(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath)
{
    for (int i = 0; i < policy->path_count; i++) {
        const char*  path = policy->paths[i].path;
        unsigned int allowMask = (unsigned int)policy->paths[i].access &
                                    (BPF_PERM_READ | BPF_PERM_WRITE | BPF_PERM_EXEC);
        char         fullPath[PATH_MAX];
        size_t       rootLength, pathLength;
        int          status;

        if (!path) {
            continue;
        }

        rootLength = strlen(rootfsPath);
        pathLength = strlen(path);
        if (rootLength + pathLength >= sizeof(fullPath)) {
            VLOG_WARNING("cvd",
                            "bpf_manager: combined rootfs path and policy path too long, skipping (rootfs=\"%s\", path=\"%s\")\n",
                            rootfsPath, path);
            continue;
        }

        if (__ends_with(path, "/**")) {
            char base[PATH_MAX];
            snprintf(base, sizeof(base), "%.*s", (int)(pathLength - 3), path);
            snprintf(fullPath, sizeof(fullPath), "%s%s", rootfsPath, base);
            status = __apply_single_path(
                mapContext,
                containerContext,
                fullPath,
                allowMask,
                BPF_DIR_RULE_RECURSIVE
            );
            if (status) {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply dir recursive rule for %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        if (__ends_with(path, "/*")) {
            char base[PATH_MAX];
            snprintf(base, sizeof(base), "%.*s", (int)(pathLength - 2), path);
            snprintf(fullPath, sizeof(fullPath), "%s%s", rootfsPath, base);
            status = __apply_single_path(mapContext, containerContext, fullPath, allowMask, BPF_DIR_RULE_CHILDREN_ONLY);
            if (status) {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply dir children rule for %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        snprintf(fullPath, sizeof(fullPath), "%s%s", rootfsPath, path);
        if (__has_glob_chars(path)) {
            const char* last = strrchr(path, '/');
            if (last && last[1] != 0) {
                size_t parent_len = (size_t)(last - path);
                if (!__has_glob_chars_range(path, parent_len)) {
                    char parent_rel[PATH_MAX];
                    char parent_abs[PATH_MAX];
                    char base_pat[PATH_MAX];
                    struct stat st;

                    if (parent_len == 0) {
                        snprintf(parent_rel, sizeof(parent_rel), "/");
                    } else {
                        snprintf(parent_rel, sizeof(parent_rel), "%.*s", (int)parent_len, path);
                    }
                    snprintf(base_pat, sizeof(base_pat), "%s", last + 1);

                    if (strcmp(base_pat, "*") == 0) {
                        snprintf(parent_abs, sizeof(parent_abs), "%s%s", rootfsPath, parent_rel);
                        if (__apply_single_path(mapContext, containerContext, parent_abs, allowMask, BPF_DIR_RULE_CHILDREN_ONLY) == 0) {
                            continue;
                        }
                    } else {
                        struct bpf_basename_rule rule = {};
                        if (__parse_basename_rule(base_pat, allowMask, &rule) == 0) {
                            snprintf(parent_abs, sizeof(parent_abs), "%s%s", rootfsPath, parent_rel);
                            if (stat(parent_abs, &st) == 0 && S_ISDIR(st.st_mode)) {
                                if (bpf_basename_policy_map_allow_rule(mapContext, st.st_dev, st.st_ino, &rule) == 0) {
                                    (void)bpf_container_context_add_tracked_basename_entry(containerContext, st.st_dev, st.st_ino);
                                    continue;
                                }
                            }
                        }
                    }
                }
            }

            char glob_path[PATH_MAX];
            __glob_translate_plus(fullPath, glob_path, sizeof(glob_path));
            glob_t g;
            memset(&g, 0, sizeof(g));
            int gstatus = glob(glob_path, GLOB_NOSORT, NULL, &g);
            if (gstatus == 0) {
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    if (__apply_single_path(mapContext, containerContext, g.gl_pathv[j], allowMask, BPF_DIR_RULE_RECURSIVE) == 0) {
                    }
                }
                globfree(&g);
                continue;
            }
            globfree(&g);
        }

        status = __apply_single_path(mapContext, containerContext, fullPath, allowMask, BPF_DIR_RULE_RECURSIVE);
        if (status) {
            VLOG_WARNING("cvd", "bpf_manager: failed to apply rule for %s: %s\n", path, strerror(errno));
        }
    }
}

void bpf_container_context_apply_net(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath)
{
    for (int i = 0; i < policy->net_rule_count; i++) {
        const struct containerv_policy_net_rule* rule = &policy->net_rules[i];
        unsigned int create_mask = rule->allow_mask & BPF_NET_CREATE;
        unsigned int tuple_mask = rule->allow_mask & ~BPF_NET_CREATE;

        if (create_mask) {
            struct bpf_net_create_key ckey = {
                .cgroup_id = containerContext->cgroup_id,
                .family = (unsigned int)rule->family,
                .type = (unsigned int)rule->type,
                .protocol = (unsigned int)rule->protocol,
            };
            if (bpf_net_create_map_allow(mapContext, &ckey, create_mask) == 0) {
                (void)bpf_container_context_add_tracked_net_create_entry(containerContext, &ckey);
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply net create rule (family=%d type=%d proto=%d): %s\n",
                                rule->family, rule->type, rule->protocol, strerror(errno));
            }
        }

        if (tuple_mask == 0) {
            continue;
        }

        if (rule->family == AF_UNIX) {
            struct bpf_net_unix_key ukey = {0};
            if (rule->unix_path == NULL || rule->unix_path[0] == '\0') {
                VLOG_WARNING("cvd", "bpf_manager: net unix rule missing path (family=AF_UNIX)\n");
                continue;
            }
            ukey.cgroup_id = containerContext->cgroup_id;
            ukey.type = (unsigned int)rule->type;
            ukey.protocol = (unsigned int)rule->protocol;
            snprintf(ukey.path, sizeof(ukey.path), "%s", rule->unix_path);

            if (bpf_net_unix_map_allow(mapContext, &ukey, tuple_mask) == 0) {
                (void)bpf_container_context_add_tracked_net_unix_entry(containerContext, &ukey);
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply net unix rule (%s): %s\n",
                                rule->unix_path, strerror(errno));
            }
            continue;
        }

        if (rule->addr_len > BPF_NET_ADDR_MAX) {
            VLOG_WARNING("cvd", "bpf_manager: net rule addr_len too large (%u)\n", rule->addr_len);
            continue;
        }

        struct bpf_net_tuple_key tkey = {0};
        tkey.cgroup_id = containerContext->cgroup_id;
        tkey.family = (unsigned int)rule->family;
        tkey.type = (unsigned int)rule->type;
        tkey.protocol = (unsigned int)rule->protocol;
        tkey.port = rule->port;
        if (rule->addr_len > 0) {
            memcpy(tkey.addr, rule->addr, rule->addr_len);
        }

        if (bpf_net_tuple_map_allow(mapContext, &tkey, tuple_mask) == 0) {
            (void)bpf_container_context_add_tracked_net_tuple_entry(containerContext, &tkey);
        } else {
            VLOG_WARNING("cvd", "bpf_manager: failed to apply net tuple rule (family=%d type=%d proto=%d): %s\n",
                            rule->family, rule->type, rule->protocol, strerror(errno));
        }
    }
}

int bpf_container_context_cleanup(
    struct bpf_container_context* containerContext,
    struct bpf_map_context*       mapContext)
{    
    if (containerContext->file.file_key_count > 0 && mapContext->map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d file entries (cgroup_id=%llu)\n",
                   containerContext->file.file_key_count, containerContext->cgroup_id);
        int deleted_count = bpf_map_delete_batch_by_fd(
            mapContext->map_fd,
            containerContext->file.file_keys,
            containerContext->file.file_key_count,
            sizeof(struct bpf_policy_key)
        );
        if (deleted_count < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (file map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->file.dir_key_count > 0 && mapContext->dir_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d dir entries (cgroup_id=%llu)\n",
                   containerContext->file.dir_key_count, containerContext->cgroup_id);
        int deleted_dirs = bpf_map_delete_batch_by_fd(
            mapContext->dir_map_fd,
            containerContext->file.dir_keys,
            containerContext->file.dir_key_count,
            sizeof(struct bpf_policy_key)
        );
        if (deleted_dirs < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (dir map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->file.basename_key_count > 0 && mapContext->basename_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d basename entries (cgroup_id=%llu)\n",
                   containerContext->file.basename_key_count, containerContext->cgroup_id);
        int deleted_base = bpf_map_delete_batch_by_fd(
            mapContext->basename_map_fd,
            containerContext->file.basename_keys,
            containerContext->file.basename_key_count,
            sizeof(struct bpf_policy_key)
        );
        if (deleted_base < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (basename map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.create_key_count > 0 && mapContext->net_create_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net create entries (cgroup_id=%llu)\n",
                   containerContext->net.create_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_create_map_fd,
            containerContext->net.create_keys,
            containerContext->net.create_key_count,
            sizeof(struct bpf_net_create_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net create map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.tuple_key_count > 0 && mapContext->net_tuple_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net tuple entries (cgroup_id=%llu)\n",
                   containerContext->net.tuple_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_tuple_map_fd,
            containerContext->net.tuple_keys,
            containerContext->net.tuple_key_count,
            sizeof(struct bpf_net_tuple_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net tuple map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.unix_key_count > 0 && mapContext->net_unix_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net unix entries (cgroup_id=%llu)\n",
                   containerContext->net.unix_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_unix_map_fd,
            containerContext->net.unix_keys,
            containerContext->net.unix_key_count,
            sizeof(struct bpf_net_unix_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net unix map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }
    return 0;
}
