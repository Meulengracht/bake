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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vlog.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>

#include "map-ops.h"

int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int bpf_dir_policy_map_allow_dir(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino,
    unsigned int            allowMask,
    unsigned int            flags)
{
    struct bpf_policy_key       key = {};
    struct bpf_dir_policy_value value = {};
    union bpf_attr              attr = {};

    if (context == NULL || context->dir_map_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    key.cgroup_id = context->cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    value.allow_mask = allowMask;
    value.flags = flags;

    attr.map_fd = context->dir_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_basename_policy_map_allow_rule(
    struct bpf_map_context*         context,
    dev_t                           dev,
    ino_t                           ino,
    const struct bpf_basename_rule* rule)
{
    struct bpf_policy_key key = {};
    struct bpf_basename_policy_value value = {};
    union bpf_attr attr = {};

    if (context == NULL || context->basename_map_fd < 0 || rule == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (rule->token_count == 0) {
        errno = EINVAL;
        return -1;
    }

    key.cgroup_id = context->cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    // Try lookup existing rule array
    memset(&attr, 0, sizeof(attr));
    attr.map_fd = context->basename_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    int status = bpf_syscall(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
    if (status < 0) {
        if (errno != ENOENT) {
            return -1;
        }
        memset(&value, 0, sizeof(value));
    }

    // If an identical rule exists, merge allow_mask
    for (int i = 0; i < BPF_BASENAME_RULE_MAX; i++) {
        struct bpf_basename_rule* slot = &value.rules[i];
        if (slot->token_count == rule->token_count &&
            slot->tail_wildcard == rule->tail_wildcard &&
            memcmp(slot->token_type, rule->token_type, sizeof(rule->token_type)) == 0 &&
            memcmp(slot->token_len, rule->token_len, sizeof(rule->token_len)) == 0 &&
            memcmp(slot->token, rule->token, sizeof(rule->token)) == 0) {
            slot->allow_mask |= rule->allow_mask;
            goto do_update;
        }
    }

    // Otherwise insert into an empty slot
    for (int i = 0; i < BPF_BASENAME_RULE_MAX; i++) {
        struct bpf_basename_rule* slot = &value.rules[i];
        if (slot->token_count == 0) {
            *slot = *rule;
            goto do_update;
        }
    }

    errno = ENOSPC;
    return -1;

do_update:
    memset(&attr, 0, sizeof(attr));
    attr.map_fd = context->basename_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_policy_map_allow_inode(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino,
    unsigned int            allowMask)
{
    struct bpf_policy_key   key = {};
    struct bpf_policy_value value = {};
    union bpf_attr          attr = {};

    key.cgroup_id = context->cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    value.allow_mask = allowMask;

    attr.map_fd = context->map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_policy_map_delete_entry(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino)
{
    struct bpf_policy_key key = {};
    union bpf_attr        attr = {};

    key.cgroup_id = context->cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    attr.map_fd = context->map_fd;
    attr.key = (uintptr_t)&key;

    return bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_net_create_map_allow(
    struct bpf_map_context*          context,
    const struct bpf_net_create_key* key,
    unsigned int                     allowMask)
{
    struct bpf_net_policy_value value = {};
    union bpf_attr               attr = {};

    if (context->net_create_map_fd < 0 || key == NULL) {
        errno = EINVAL;
        return -1;
    }

    value.allow_mask = allowMask;
    attr.map_fd = context->net_create_map_fd;
    attr.key = (uintptr_t)key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_net_tuple_map_allow(
    struct bpf_map_context*         context,
    const struct bpf_net_tuple_key* key,
    unsigned int                    allowMask)
{
    struct bpf_net_policy_value value = {};
    union bpf_attr               attr = {};

    if (context->net_tuple_map_fd < 0 || key == NULL) {
        errno = EINVAL;
        return -1;
    }

    value.allow_mask = allowMask;
    attr.map_fd = context->net_tuple_map_fd;
    attr.key = (uintptr_t)key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_net_unix_map_allow(
    struct bpf_map_context*        context,
    const struct bpf_net_unix_key* key,
    unsigned int                   allowMask)
{
    struct bpf_net_policy_value value = {};
    union bpf_attr               attr = {};

    if (context->net_unix_map_fd < 0 || key == NULL) {
        errno = EINVAL;
        return -1;
    }

    value.allow_mask = allowMask;
    attr.map_fd = context->net_unix_map_fd;
    attr.key = (uintptr_t)key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_map_delete_batch_by_fd(int mapFd, void* keys, int count, size_t keySize)
{
    union bpf_attr attr = {};
    int deleted = 0;
    int saved_errno;
    
    if (mapFd < 0 || keys == NULL || count <= 0 || keySize == 0) {
        errno = EINVAL;
        return -1;
    }
    
    // Use BPF_MAP_DELETE_BATCH for efficient batch deletion
    // This requires kernel 5.6+, but we already require 5.7+ for BPF LSM
    memset(&attr, 0, sizeof(attr));
    attr.batch.map_fd = mapFd;
    attr.batch.keys = (uintptr_t)keys;
    attr.batch.count = count;
    attr.batch.elem_flags = 0;
    
    // Try batch deletion first
    int status = bpf_syscall(BPF_MAP_DELETE_BATCH, &attr, sizeof(attr));
    if (status == 0) {
        // Success - all entries deleted
        return count;
    }
    
    // Save errno for debugging
    saved_errno = errno;
    
    // If batch delete is not supported or fails, fall back to individual deletions
    // Check for common error codes indicating lack of support
    if (saved_errno == EINVAL || saved_errno == ENOTSUP || saved_errno == ENOSYS) {
        VLOG_DEBUG("containerv", "bpf_helpers: BPF_MAP_DELETE_BATCH not supported (errno=%d), falling back to individual deletions\n", saved_errno);
        
        for (int i = 0; i < count; i++) {
            memset(&attr, 0, sizeof(attr));
            attr.map_fd = mapFd;
            attr.key = (uintptr_t)((unsigned char*)keys + (i * keySize));
            
            if (bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr)) == 0) {
                deleted++;
            } else if (errno != ENOENT) {
                // Ignore ENOENT (entry doesn't exist), but log other errors
                VLOG_TRACE("containerv", "bpf_helpers: failed to delete entry %d: %s\n", i, strerror(errno));
            }
        }
        return deleted;
    }
    
    // Some other error occurred, restore errno for caller
    errno = saved_errno;
    VLOG_ERROR("containerv", "bpf_helpers: batch delete failed: %s\n", strerror(errno));
    return -1;
}
