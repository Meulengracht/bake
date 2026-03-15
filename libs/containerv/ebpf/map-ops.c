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

int __set_profile_for_fd(
    int                     mapFd,
    unsigned long long      cgroupId,
    uint8_t*                profile,
    size_t                  profileSize)
{
    uint64_t                 key = cgroupId;
    struct bpf_profile_value value = {};
    union bpf_attr           attr = {};

    memcpy(&value.data[0], profile, profileSize);
    value.size = (unsigned int)profileSize;

    attr.map_fd = mapFd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;

    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_profile_map_set_profile(
    struct bpf_map_context* context,
    uint8_t*                profile,
    size_t                  profileSize)
{
    return __set_profile_for_fd(context->profile_map_fd, context->cgroup_id, profile, profileSize);
}

int bpf_profile_map_set_net_profile(
    struct bpf_map_context* context,
    uint8_t*                profile,
    size_t                  profileSize)
{
    return __set_profile_for_fd(context->net_profile_map_fd, context->cgroup_id, profile, profileSize);
}

int bpf_profile_map_set_mount_profile(
    struct bpf_map_context* context,
    uint8_t*                profile,
    size_t                  profileSize)
{
    return __set_profile_for_fd(context->mount_profile_map_fd, context->cgroup_id, profile, profileSize);
}

int __clear_profile_for_fd(
    int                mapFd,
    unsigned long long cgroupId)
{
    union bpf_attr attr = {};
    uint64_t       key = cgroupId;

    attr.map_fd = mapFd;
    attr.key = (uintptr_t)&key;

    return bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_profile_map_clear_profile(
    struct bpf_map_context* context)
{
    return __clear_profile_for_fd(context->profile_map_fd, context->cgroup_id);
}

int bpf_profile_map_clear_net_profile(
    struct bpf_map_context* context)
{
    return __clear_profile_for_fd(context->net_profile_map_fd, context->cgroup_id);
}

int bpf_profile_map_clear_mount_profile(
    struct bpf_map_context* context)
{
    return __clear_profile_for_fd(context->mount_profile_map_fd, context->cgroup_id);
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
