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

#include "private.h"

#ifdef __linux__
#include <linux/bpf.h>

#ifdef HAVE_BPF_SKELETON
#include <bpf/bpf.h>
#endif
#endif

#ifdef __linux__

int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int __find_in_file(FILE* fp, const char* target)
{
    char  buffer[1024];
    char* ptr = &buffer[0];
    size_t target_len;
    
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        return 0;
    }

    if (target == NULL || target[0] == 0) {
        return 0;
    }
    target_len = strlen(target);

    // Look for the target as a complete token (not substring)
    while (ptr) {
        // Find next occurrence of the target
        ptr = strstr(ptr, target);
        if (!ptr) {
            break;
        }
        
        // Check if it's a complete token (surrounded by comma/whitespace/newline/boundaries)
        int isStart = (ptr == &buffer[0] || ptr[-1] == ',' || ptr[-1] == ' ' || ptr[-1] == '\t');
        int isEnd = (ptr[target_len] == '\0' || ptr[target_len] == ',' || ptr[target_len] == '\n' ||
                     ptr[target_len] == ' ' || ptr[target_len] == '\t');
        if (isStart && isEnd) {
            return 1;
        }
        
        // Move past match to continue searching
        ptr += target_len;
    }
    return 0;
}

int bpf_check_lsm_available(void)
{
    FILE* fp;
    int   available = 0;
    
    // Check /sys/kernel/security/lsm for "bpf"
    fp = fopen("/sys/kernel/security/lsm", "r");
    if (!fp) {
        VLOG_DEBUG("containerv", "bpf_helpers: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    available = __find_in_file(fp, "bpf");
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("containerv", "bpf_helpers: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
    }
    
    return available;
}

unsigned long long bpf_get_cgroup_id(const char* hostname)
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
    for (c = hostname; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') ||
              (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') ||
              *c == '-' || *c == '_' || *c == '.')) {
            VLOG_ERROR("containerv", "bpf_helpers: invalid hostname: %s\n", hostname);
            errno = EINVAL;
            return 0;
        }
    }
    
    // Ensure hostname doesn't start with .
    if (hostname[0] == '.') {
        VLOG_ERROR("containerv", "bpf_helpers: invalid hostname starts with dot: %s\n", hostname);
        errno = EINVAL;
        return 0;
    }
    
    // Build cgroup path
    snprintf(cgroupPath, sizeof(cgroupPath), "/sys/fs/cgroup/%s", hostname);
    
    // Open cgroup directory
    fd = open(cgroupPath, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("containerv", "bpf_helpers: failed to open cgroup %s: %s\n", 
                   cgroupPath, strerror(errno));
        return 0;
    }
    
    // Get inode number which serves as cgroup ID
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("containerv", "bpf_helpers: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroupID = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("containerv", "bpf_helpers: cgroup %s has ID %llu\n", hostname, cgroupID);
    
    return cgroupID;
}

int bpf_bump_memlock_rlimit(void)
{
    struct rlimit rlim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

int containerv_bpf_manager_sanity_check_pins(void)
{
#if !defined(__linux__) || !defined(HAVE_BPF_SKELETON)
    return 0;
#else
    int map_fd = bpf_obj_get("/sys/fs/bpf/cvd/policy_map");
    int dir_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/dir_policy_map");
    int basename_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/basename_policy_map");
    int link_fd = bpf_obj_get("/sys/fs/bpf/cvd/fs_lsm_link");
    int exec_link_fd = bpf_obj_get("/sys/fs/bpf/cvd/fs_lsm_exec_link");

    if (map_fd >= 0) {
        close(map_fd);
    }
    if (dir_map_fd >= 0) {
        close(dir_map_fd);
    }
    if (basename_map_fd >= 0) {
        close(basename_map_fd);
    }
    if (link_fd >= 0) {
        close(link_fd);
    }
    if (exec_link_fd >= 0) {
        close(exec_link_fd);
    }

    if (map_fd < 0 || dir_map_fd < 0 || link_fd < 0) {
        VLOG_WARNING("containerv",
                     "BPF LSM sanity check failed (pinned map=%s, pinned dir_map=%s, pinned link=%s, pinned basename_map=%s). "
                     "Enforcement may be misconfigured or stale pins exist.\n",
                     (map_fd >= 0) ? "ok" : "missing",
                     (dir_map_fd >= 0) ? "ok" : "missing",
                     (link_fd >= 0) ? "ok" : "missing",
                     (basename_map_fd >= 0) ? "ok" : "missing");
        errno = ENOENT;
        return -1;
    }

    VLOG_DEBUG("containerv", "BPF LSM sanity check ok (pinned map + link present)\n");
    return 0;
#endif
}

int bpf_dir_policy_map_allow_dir(
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino,
    unsigned int               allow_mask,
    unsigned int               flags)
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

    value.allow_mask = allow_mask;
    value.flags = flags;

    attr.map_fd = context->dir_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_basename_policy_map_allow_rule(
    struct bpf_policy_context*      context,
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
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino,
    unsigned int               allow_mask)
{
    struct bpf_policy_key   key = {};
    struct bpf_policy_value value = {};
    union bpf_attr          attr = {};

    key.cgroup_id = context->cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    value.allow_mask = allow_mask;

    attr.map_fd = context->map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_policy_map_delete_entry(
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino)
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

int bpf_policy_map_delete_batch(
    struct bpf_policy_context* context,
    struct bpf_policy_key*     keys,
    int                        count)
{
    union bpf_attr attr = {};
    int deleted = 0;
    int saved_errno;
    
    if (context->map_fd < 0 || keys == NULL || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    
    // Use BPF_MAP_DELETE_BATCH for efficient batch deletion
    // This requires kernel 5.6+, but we already require 5.7+ for BPF LSM
    memset(&attr, 0, sizeof(attr));
    attr.batch.map_fd = context->map_fd;
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
            attr.map_fd = context->map_fd;
            attr.key = (uintptr_t)&keys[i];
            
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

#endif // __linux__
