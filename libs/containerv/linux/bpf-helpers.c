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

#include "bpf-helpers.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vlog.h>

#ifdef __linux__
#include <linux/bpf.h>
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

int bpf_check_lsm_available(void)
{
    FILE* fp;
    int   available = 0;
    
    // Check /sys/kernel/security/lsm for "bpf"
    fp = fopen("/sys/kernel/security/lsm", "r");
    if (!fp) {
        VLOG_DEBUG("cvd", "bpf_helpers: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    available = __find_in_file(fp, "bpf");
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("cvd", "bpf_helpers: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
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
            VLOG_ERROR("cvd", "bpf_helpers: invalid hostname: %s\n", hostname);
            errno = EINVAL;
            return 0;
        }
    }
    
    // Ensure hostname doesn't start with .
    if (hostname[0] == '.') {
        VLOG_ERROR("cvd", "bpf_helpers: invalid hostname starts with dot: %s\n", hostname);
        errno = EINVAL;
        return 0;
    }
    
    // Build cgroup path
    snprintf(cgroupPath, sizeof(cgroupPath), "/sys/fs/cgroup/%s", hostname);
    
    // Open cgroup directory
    fd = open(cgroupPath, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("cvd", "bpf_helpers: failed to open cgroup %s: %s\n", 
                   cgroupPath, strerror(errno));
        return 0;
    }
    
    // Get inode number which serves as cgroup ID
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("cvd", "bpf_helpers: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroupID = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("cvd", "bpf_helpers: cgroup %s has ID %llu\n", hostname, cgroupID);
    
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

int bpf_policy_map_allow_inode(
    int                policy_map_fd,
    unsigned long long cgroup_id,
    dev_t              dev,
    ino_t              ino,
    unsigned int       allow_mask)
{
    struct bpf_policy_key   key = {};
    struct bpf_policy_value value = {};
    union bpf_attr          attr = {};

    key.cgroup_id = cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    value.allow_mask = allow_mask;

    attr.map_fd = policy_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_policy_map_delete_entry(
    int                policy_map_fd,
    unsigned long long cgroup_id,
    dev_t              dev,
    ino_t              ino)
{
    struct bpf_policy_key key = {};
    union bpf_attr        attr = {};

    key.cgroup_id = cgroup_id;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    attr.map_fd = policy_map_fd;
    attr.key = (uintptr_t)&key;

    return bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_policy_map_delete_batch(
    int                    policy_map_fd,
    struct bpf_policy_key* keys,
    int                    count)
{
    union bpf_attr attr = {};
    int deleted = 0;
    int saved_errno;
    
    if (policy_map_fd < 0 || keys == NULL || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    
    // Use BPF_MAP_DELETE_BATCH for efficient batch deletion
    // This requires kernel 5.6+, but we already require 5.7+ for BPF LSM
    memset(&attr, 0, sizeof(attr));
    attr.batch.map_fd = policy_map_fd;
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
        VLOG_DEBUG("cvd", "bpf_helpers: BPF_MAP_DELETE_BATCH not supported (errno=%d), falling back to individual deletions\n", saved_errno);
        
        for (int i = 0; i < count; i++) {
            memset(&attr, 0, sizeof(attr));
            attr.map_fd = policy_map_fd;
            attr.key = (uintptr_t)&keys[i];
            
            if (bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr)) == 0) {
                deleted++;
            } else if (errno != ENOENT) {
                // Ignore ENOENT (entry doesn't exist), but log other errors
                VLOG_TRACE("cvd", "bpf_helpers: failed to delete entry %d: %s\n", i, strerror(errno));
            }
        }
        return deleted;
    }
    
    // Some other error occurred, restore errno for caller
    errno = saved_errno;
    VLOG_ERROR("cvd", "bpf_helpers: batch delete failed: %s\n", strerror(errno));
    return -1;
}

#endif // __linux__
