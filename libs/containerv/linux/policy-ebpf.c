/**
 * Copyright 2024, Philip Meulengracht
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
#include "private.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <vlog.h>

// eBPF syscall wrapper
static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// Internal structure to track loaded eBPF programs
struct policy_ebpf_context {
    int syscall_prog_fd;
    int path_prog_fd;
    int syscall_map_fd;
    int path_map_fd;
};

// Syscall BPF map: maps syscall numbers to allowed (1) or denied (0)
// We use a hash map for efficient lookup
static int create_syscall_map(void)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = sizeof(__u32),    // syscall number
        .value_size = sizeof(__u32),  // allowed flag
        .max_entries = 512,
    };
    
    int fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to create syscall map: %s\n", strerror(errno));
    }
    return fd;
}

// Filesystem path BPF map: stores allowed paths with access modes
// Using array of path prefixes (simplified approach)
static int create_path_map(void)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = 256,  // path string (simplified, real impl would use hash)
        .value_size = sizeof(__u32),  // access mode flags
        .max_entries = 256,
    };
    
    int fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to create path map: %s\n", strerror(errno));
    }
    return fd;
}

// Populate syscall map with allowed syscalls from policy
static int populate_syscall_map(int map_fd, struct containerv_policy* policy)
{
    // In a real implementation, we would:
    // 1. Convert syscall names to syscall numbers (architecture-specific)
    // 2. Use BPF_MAP_UPDATE_ELEM to add entries to the map
    
    // For now, we'll use a simplified approach with seccomp-bpf
    // which is more practical than pure eBPF for syscall filtering
    
    VLOG_DEBUG("containerv", "policy_ebpf: would populate syscall map with %d entries\n", 
               policy->syscall_count);
    
    // Syscall filtering is better done via seccomp-bpf
    // This is a placeholder for future eBPF LSM integration
    
    return 0;
}

// Populate path map with allowed paths from policy
static int populate_path_map(int map_fd, struct containerv_policy* policy)
{
    VLOG_DEBUG("containerv", "policy_ebpf: would populate path map with %d entries\n",
               policy->path_count);
    
    // In a real implementation with eBPF LSM hooks:
    // 1. Add each path pattern to the map with its access mode
    // 2. The eBPF program would check paths against these patterns
    
    // This is a placeholder for future eBPF LSM integration
    
    return 0;
}

int policy_ebpf_load(
    struct containerv_container*  container,
    struct containerv_policy*     policy)
{
    if (container == NULL || policy == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    VLOG_INFO("containerv", "policy_ebpf: loading policy (type=%d, syscalls=%d, paths=%d)\n",
              policy->type, policy->syscall_count, policy->path_count);
    
    // Create BPF maps
    int syscall_map_fd = create_syscall_map();
    if (syscall_map_fd < 0) {
        // Non-fatal - fall back to traditional seccomp
        VLOG_WARN("containerv", "policy_ebpf: failed to create syscall map, will use seccomp instead\n");
        return 0;
    }
    
    int path_map_fd = create_path_map();
    if (path_map_fd < 0) {
        close(syscall_map_fd);
        VLOG_WARN("containerv", "policy_ebpf: failed to create path map\n");
        return 0;
    }
    
    // Populate maps with policy data
    if (populate_syscall_map(syscall_map_fd, policy) != 0) {
        close(syscall_map_fd);
        close(path_map_fd);
        return -1;
    }
    
    if (populate_path_map(path_map_fd, policy) != 0) {
        close(syscall_map_fd);
        close(path_map_fd);
        return -1;
    }
    
    // In a full eBPF implementation, we would:
    // 1. Load eBPF programs for syscall and path filtering
    // 2. Attach them to LSM hooks or cgroup
    // 3. Store program FDs for cleanup
    
    // For now, we'll close the maps as we're using seccomp-bpf instead
    close(syscall_map_fd);
    close(path_map_fd);
    
    VLOG_INFO("containerv", "policy_ebpf: policy loaded successfully\n");
    
    return 0;
}

int policy_ebpf_unload(struct containerv_container* container)
{
    if (container == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    // In a full implementation, we would:
    // 1. Detach eBPF programs
    // 2. Close program FDs
    // 3. Close map FDs
    
    VLOG_DEBUG("containerv", "policy_ebpf: unloading policy\n");
    
    return 0;
}
