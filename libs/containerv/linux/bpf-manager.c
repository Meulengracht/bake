/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, , either version 3 of the License, or
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

#include "bpf-manager.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vlog.h>

// Include internal policy structure for access to fields
// This is a temporary solution that creates tight coupling between modules.
// 
// TODO (High Priority): Refactor to use a public iterator API
// - Add containerv_policy_foreach_path(policy, callback, userdata) to policy.h
// - This will eliminate the need for direct access to internal structures
// - Will improve maintainability and module boundaries
//
// This is acceptable for now since bpf-manager is now part of containerv library
// and needs direct access to policy internals for BPF map population.
#include "policy-internal.h"

#ifdef __linux__
#include <linux/bpf.h>
#include <sys/sysmacros.h>

#ifdef HAVE_BPF_SKELETON
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "fs-lsm.skel.h"
#endif

#endif // __linux__

#define BPF_PIN_PATH "/sys/fs/bpf/cvd"
#define POLICY_MAP_PIN_PATH BPF_PIN_PATH "/policy_map"

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

/* Policy value: permission mask */
struct policy_value {
    unsigned int allow_mask;
};

/* Global BPF manager state */
static struct {
    int available;
    int policy_map_fd;
#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf* skel;
#endif
} g_bpf_manager = {
    .available = 0,
    .policy_map_fd = -1,
#ifdef HAVE_BPF_SKELETON
    .skel = NULL,
#endif
};

#ifdef __linux__

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
        VLOG_DEBUG("cvd", "bpf_manager: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    available = __find_in_file(fp, "bpf");
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("cvd", "bpf_manager: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
    }
    
    return available;
}

static int __bump_memlock_rlimit(void)
{
    struct rlimit rlim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

static int __create_bpf_pin_directory(void)
{
    struct stat st;
    
    // Check if /sys/fs/bpf exists
    if (stat("/sys/fs/bpf", &st) < 0) {
        VLOG_ERROR("cvd", "bpf_manager: /sys/fs/bpf not available - is BPF filesystem mounted?\n");
        return -1;
    }
    
    // Create our pin directory
    if (mkdir(BPF_PIN_PATH, 0755) < 0 && errno != EEXIST) {
        VLOG_ERROR("cvd", "bpf_manager: failed to create %s: %s\n", 
                   BPF_PIN_PATH, strerror(errno));
        return -1;
    }
    
    return 0;
}

static unsigned long long __get_cgroup_id(const char* hostname)
{
    char               cgroupPath[512];
    int                fd;
    struct stat        st;
    unsigned long long cgroupID;
    const char*        c;
    
    // TODO (Medium Priority): This function duplicates logic from
    // libs/containerv/linux/policy-ebpf.c:__get_cgroup_id()
    // Consider extracting to a shared utility function in containerv API
    // to ensure consistent validation and avoid code duplication.
    
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
            VLOG_ERROR("cvd", "bpf_manager: invalid hostname: %s\n", hostname);
            errno = EINVAL;
            return 0;
        }
    }
    
    // Ensure hostname doesn't start with .
    if (hostname[0] == '.') {
        VLOG_ERROR("cvd", "bpf_manager: invalid hostname starts with dot: %s\n", hostname);
        errno = EINVAL;
        return 0;
    }
    
    // Build cgroup path
    snprintf(cgroupPath, sizeof(cgroupPath), "/sys/fs/cgroup/%s", hostname);
    
    // Open cgroup directory
    fd = open(cgroupPath, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to open cgroup %s: %s\n", 
                   cgroupPath, strerror(errno));
        return 0;
    }
    
    // Get inode number which serves as cgroup ID
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroupID = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("cvd", "bpf_manager: cgroup %s has ID %llu\n", hostname, cgroupID);
    
    return cgroupID;
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

static int __delete_policy_entry(
    int                policyMapFD,
    unsigned long long cgroupID,
    dev_t              dev,
    ino_t              ino)
{
    struct policy_key key = {};
    union bpf_attr    attr = {};

    key.cgroup_id = cgroupID;
    key.dev = (unsigned long long)dev;
    key.ino = (unsigned long long)ino;

    attr.map_fd = policyMapFD;
    attr.key = (uintptr_t)&key;

    return bpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

#endif // __linux__

int containerv_bpf_manager_initialize(void)
{
#ifndef __linux__
    VLOG_TRACE("cvd", "bpf_manager: BPF LSM not supported on this platform\n");
    return 0;
#else
#ifndef HAVE_BPF_SKELETON
    VLOG_TRACE("cvd", "bpf_manager: BPF skeleton not available, using seccomp fallback\n");
    return 0;
#else
    int status;
    
    VLOG_TRACE("cvd", "bpf_manager: initializing BPF manager\n");
    
    // Check if BPF LSM is available
    if (!__check_bpf_lsm()) {
        VLOG_TRACE("cvd", "bpf_manager: BPF LSM not available, using seccomp fallback\n");
        return 0;
    }
    
    // Bump memory lock limit for BPF
    if (__bump_memlock_rlimit() < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to increase memlock limit: %s\n", 
                    strerror(errno));
    }
    
    // Create BPF pin directory
    if (__create_bpf_pin_directory() < 0) {
        return -1;
    }
    
    // Open BPF skeleton
    g_bpf_manager.skel = fs_lsm_bpf__open();
    if (!g_bpf_manager.skel) {
        VLOG_ERROR("cvd", "bpf_manager: failed to open BPF skeleton\n");
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: BPF skeleton opened\n");
    
    // Load BPF programs
    status = fs_lsm_bpf__load(g_bpf_manager.skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to load BPF skeleton: %d\n", status);
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: BPF programs loaded\n");
    
    // Attach BPF LSM programs
    status = fs_lsm_bpf__attach(g_bpf_manager.skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to attach BPF LSM program: %d\n", status);
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        return -1;
    }
    
    VLOG_TRACE("cvd", "bpf_manager: BPF LSM programs attached successfully\n");
    
    // Get policy map FD
    g_bpf_manager.policy_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.policy_map);
    if (g_bpf_manager.policy_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get policy_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        return -1;
    }
    
    // Pin the policy map for persistence and sharing
    status = bpf_obj_pin(g_bpf_manager.policy_map_fd, POLICY_MAP_PIN_PATH);
    if (status < 0 && errno != EEXIST) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin policy map to %s: %s\n",
                    POLICY_MAP_PIN_PATH, strerror(errno));
        // Continue anyway - map is still usable via FD
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: policy map pinned to %s\n", POLICY_MAP_PIN_PATH);
    }
    
    g_bpf_manager.available = 1;
    VLOG_TRACE("cvd", "bpf_manager: initialization complete, BPF LSM enforcement active\n");
    
    return 0;
#endif // HAVE_BPF_SKELETON
#endif // __linux__
}

void containerv_bpf_manager_shutdown(void)
{
#ifdef __linux__
#ifdef HAVE_BPF_SKELETON
    if (!g_bpf_manager.available) {
        return;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: shutting down BPF manager\n");
    
    // Unpin map
    if (unlink(POLICY_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin policy map: %s\n", 
                    strerror(errno));
    }
    
    // Destroy skeleton (this detaches programs)
    if (g_bpf_manager.skel) {
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
    }
    
    g_bpf_manager.policy_map_fd = -1;
    g_bpf_manager.available = 0;
    
    VLOG_TRACE("cvd", "bpf_manager: shutdown complete\n");
#endif
#endif
}

int containerv_bpf_manager_is_available(void)
{
    return g_bpf_manager.available;
}

int containerv_bpf_manager_get_policy_map_fd(void)
{
    return g_bpf_manager.policy_map_fd;
}

int containerv_bpf_manager_populate_policy(
    const char* container_id,
    const char* rootfs_path,
    struct containerv_policy* policy)
{
#ifndef __linux__
    (void)container_id;
    (void)rootfs_path;
    (void)policy;
    return 0;
#else
    unsigned long long cgroup_id;
    int entries_added = 0;
    
    if (!g_bpf_manager.available) {
        VLOG_DEBUG("cvd", "bpf_manager: BPF not available, skipping policy population\n");
        return 0;
    }
    
    if (container_id == NULL || rootfs_path == NULL || policy == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (policy->path_count == 0) {
        VLOG_DEBUG("cvd", "bpf_manager: no paths configured for container %s\n", container_id);
        return 0;
    }
    
    // Defensive bounds check to prevent out-of-bounds reads
    if (policy->path_count > MAX_PATHS) {
        VLOG_ERROR("cvd", "bpf_manager: policy path_count (%d) exceeds MAX_PATHS (%d)\n",
                   policy->path_count, MAX_PATHS);
        errno = EINVAL;
        return -1;
    }
    
    // Get cgroup ID for this container
    cgroup_id = __get_cgroup_id(container_id);
    if (cgroup_id == 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to resolve cgroup ID for %s\n", container_id);
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: populating policy for container %s (cgroup_id=%llu)\n",
               container_id, cgroup_id);
    
    // Populate policy entries for each configured path
    for (int i = 0; i < policy->path_count; i++) {
        const char* path = policy->paths[i].path;
        unsigned int allow_mask = (unsigned int)policy->paths[i].access & 
                                  (PERM_READ | PERM_WRITE | PERM_EXEC);
        char full_path[PATH_MAX];
        struct stat st;
        int status;
        size_t root_len, path_len;
        
        if (!path) {
            continue;
        }
        
        // Check for path length overflow before concatenation
        root_len = strlen(rootfs_path);
        path_len = strlen(path);
        
        if (root_len + path_len >= sizeof(full_path)) {
            VLOG_WARNING("cvd",
                         "bpf_manager: combined rootfs path and policy path too long, "
                         "skipping entry (rootfs=\"%s\", path=\"%s\")\n",
                         rootfs_path, path);
            continue;
        }
        
        // Resolve path within container's rootfs
        snprintf(full_path, sizeof(full_path), "%s%s", rootfs_path, path);
        
        // Get inode info
        status = stat(full_path, &st);
        if (status < 0) {
            VLOG_WARNING("cvd", "bpf_manager: failed to stat %s: %s\n", 
                        full_path, strerror(errno));
            continue;
        }
        
        // Add policy entry
        status = __policy_map_allow_inode(
            g_bpf_manager.policy_map_fd,
            cgroup_id,
            st.st_dev,
            st.st_ino,
            allow_mask
        );
        
        if (status < 0) {
            VLOG_WARNING("cvd", "bpf_manager: failed to add policy for %s: %s\n",
                        path, strerror(errno));
        } else {
            entries_added++;
            VLOG_TRACE("cvd", "bpf_manager: added policy for %s (dev=%lu, ino=%lu, mask=0x%x)\n",
                      path, (unsigned long)st.st_dev, (unsigned long)st.st_ino, allow_mask);
        }
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: populated %d policy entries for container %s\n",
               entries_added, container_id);
    
    return 0;
#endif // __linux__
}

int containerv_bpf_manager_cleanup_policy(const char* container_id)
{
#ifndef __linux__
    (void)container_id;
    return 0;
#else
    unsigned long long cgroup_id;
    struct policy_key key;
    struct policy_key next_key;
    union bpf_attr attr = {};
    int deleted_count = 0;
    int status;
    
    if (!g_bpf_manager.available) {
        return 0;
    }
    
    if (container_id == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    // Get cgroup ID for this container
    cgroup_id = __get_cgroup_id(container_id);
    if (cgroup_id == 0) {
        // Container's cgroup may already be gone, this is not an error
        VLOG_DEBUG("cvd", "bpf_manager: cgroup for %s not found, assuming already cleaned up\n",
                   container_id);
        return 0;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: cleaning up policy for container %s (cgroup_id=%llu)\n",
               container_id, cgroup_id);
    
    // Iterate through the map and delete all entries with this cgroup_id
    // Start with an empty key to get the first element
    memset(&key, 0, sizeof(key));
    memset(&next_key, 0, sizeof(next_key));
    
    attr.map_fd = g_bpf_manager.policy_map_fd;
    attr.key = 0; // Start from beginning
    attr.next_key = (uintptr_t)&next_key;
    
    // Get first key
    if (bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr)) < 0) {
        if (errno == ENOENT) {
            // Map is empty, nothing to clean up
            VLOG_DEBUG("cvd", "bpf_manager: policy map is empty\n");
            return 0;
        }
        VLOG_ERROR("cvd", "bpf_manager: failed to get first key: %s\n", strerror(errno));
        return -1;
    }
    
    // Check and delete first key if it matches
    if (next_key.cgroup_id == cgroup_id) {
        status = __delete_policy_entry(
            g_bpf_manager.policy_map_fd,
            next_key.cgroup_id,
            (dev_t)next_key.dev,
            (ino_t)next_key.ino
        );
        if (status == 0) {
            deleted_count++;
        }
    }
    
    // Iterate through rest of map
    key = next_key;
    while (1) {
        attr.key = (uintptr_t)&key;
        attr.next_key = (uintptr_t)&next_key;
        
        status = bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
        if (status < 0) {
            if (errno == ENOENT) {
                // Reached end of map
                break;
            }
            VLOG_WARNING("cvd", "bpf_manager: failed to get next key: %s\n", strerror(errno));
            break;
        }
        
        // Check if this entry belongs to our container
        if (next_key.cgroup_id == cgroup_id) {
            status = __delete_policy_entry(
                g_bpf_manager.policy_map_fd,
                next_key.cgroup_id,
                (dev_t)next_key.dev,
                (ino_t)next_key.ino
            );
            if (status == 0) {
                deleted_count++;
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to delete entry: %s\n", strerror(errno));
            }
        }
        
        key = next_key;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: deleted %d policy entries for container %s\n",
               deleted_count, container_id);
    
    return 0;
#endif // __linux__
}
