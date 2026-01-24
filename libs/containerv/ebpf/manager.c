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

#include <chef/containerv/bpf-manager.h>
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

#include "private.h"

// import the private.h from the policies dir
#include "../policies/private.h"

#ifdef __linux__
#include <linux/bpf.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <time.h>

#ifdef HAVE_BPF_SKELETON
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "fs-lsm.skel.h"
#endif

#endif // __linux__

#define BPF_PIN_PATH "/sys/fs/bpf/cvd"
#define POLICY_MAP_PIN_PATH BPF_PIN_PATH "/policy_map"
#define MAX_TRACKED_ENTRIES 10240

/* Per-container entry tracking for efficient cleanup */
struct container_entry_tracker {
    char* container_id;
    unsigned long long cgroup_id;
    struct bpf_policy_key* keys;
    int key_count;
    int key_capacity;
    unsigned long long populate_time_us;  // Time taken to populate policy
    unsigned long long cleanup_time_us;   // Time taken to cleanup policy
    struct container_entry_tracker* next;
};

/* Global BPF manager metrics */
struct bpf_manager_metrics {
    unsigned long long total_populate_ops;
    unsigned long long total_cleanup_ops;
    unsigned long long failed_populate_ops;
    unsigned long long failed_cleanup_ops;
};

/* Global BPF manager state */
static struct {
    int available;
    int policy_map_fd;
#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf* skel;
#endif
    struct container_entry_tracker* trackers;
    struct bpf_manager_metrics metrics;
} g_bpf_manager = {
    .available = 0,
    .policy_map_fd = -1,
#ifdef HAVE_BPF_SKELETON
    .skel = NULL,
#endif
    .trackers = NULL,
    .metrics = {0},
};

#ifdef __linux__

/* Helper functions for entry tracking */
static struct container_entry_tracker* __find_tracker(const char* container_id)
{
    struct container_entry_tracker* tracker = g_bpf_manager.trackers;
    while (tracker) {
        if (strcmp(tracker->container_id, container_id) == 0) {
            return tracker;
        }
        tracker = tracker->next;
    }
    return NULL;
}

static struct container_entry_tracker* __create_tracker(const char* container_id, unsigned long long cgroup_id)
{
    struct container_entry_tracker* tracker;
    
    tracker = calloc(1, sizeof(*tracker));
    if (!tracker) {
        return NULL;
    }
    
    tracker->container_id = strdup(container_id);
    if (!tracker->container_id) {
        free(tracker);
        return NULL;
    }
    
    tracker->cgroup_id = cgroup_id;
    tracker->key_capacity = 256; // Initial capacity
    tracker->keys = malloc(sizeof(struct bpf_policy_key) * tracker->key_capacity);
    if (!tracker->keys) {
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }
    
    tracker->key_count = 0;
    tracker->next = g_bpf_manager.trackers;
    g_bpf_manager.trackers = tracker;
    
    return tracker;
}

static int __add_tracked_entry(struct container_entry_tracker* tracker, 
                               unsigned long long cgroup_id,
                               dev_t dev, 
                               ino_t ino)
{
    struct bpf_policy_key* key;
    
    if (!tracker) {
        return -1;
    }
    
    // Check capacity and expand if needed
    if (tracker->key_count >= tracker->key_capacity) {
        // Check if we're already at maximum
        if (tracker->key_capacity >= MAX_TRACKED_ENTRIES) {
            VLOG_WARNING("cvd", "bpf_manager: max tracked entries (%d) reached for container\n", 
                        MAX_TRACKED_ENTRIES);
            return -1;
        }
        
        // Calculate new capacity, capping at maximum
        int new_capacity = (tracker->key_capacity * 2 < MAX_TRACKED_ENTRIES) 
                          ? tracker->key_capacity * 2 
                          : MAX_TRACKED_ENTRIES;
        
        struct bpf_policy_key* new_keys = realloc(tracker->keys, 
                                                   sizeof(struct bpf_policy_key) * new_capacity);
        if (!new_keys) {
            VLOG_ERROR("cvd", "bpf_manager: failed to expand tracker capacity\n");
            return -1;
        }
        
        tracker->keys = new_keys;
        tracker->key_capacity = new_capacity;
    }
    
    // Add the key
    key = &tracker->keys[tracker->key_count];
    key->cgroup_id = cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    tracker->key_count++;
    
    return 0;
}

static void __remove_tracker(const char* container_id)
{
    struct container_entry_tracker* tracker = g_bpf_manager.trackers;
    struct container_entry_tracker* prev = NULL;
    
    while (tracker) {
        if (strcmp(tracker->container_id, container_id) == 0) {
            // Remove from list
            if (prev) {
                prev->next = tracker->next;
            } else {
                g_bpf_manager.trackers = tracker->next;
            }
            
            // Free resources
            free(tracker->container_id);
            free(tracker->keys);
            free(tracker);
            return;
        }
        prev = tracker;
        tracker = tracker->next;
    }
}

static void __cleanup_all_trackers(void)
{
    struct container_entry_tracker* tracker = g_bpf_manager.trackers;
    while (tracker) {
        struct container_entry_tracker* next = tracker->next;
        free(tracker->container_id);
        free(tracker->keys);
        free(tracker);
        tracker = next;
    }
    g_bpf_manager.trackers = NULL;
}

static unsigned long long __get_time_microseconds(void)
{
    struct timespec ts;
    // Use CLOCK_MONOTONIC for timing measurements to avoid issues with
    // system clock adjustments (NTP, manual changes, etc.)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)(ts.tv_sec) * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
}

static int __count_total_entries(void)
{
    int total = 0;
    struct container_entry_tracker* tracker = g_bpf_manager.trackers;
    while (tracker) {
        total += tracker->key_count;
        tracker = tracker->next;
    }
    return total;
}

static int __count_containers(void)
{
    int count = 0;
    struct container_entry_tracker* tracker = g_bpf_manager.trackers;
    while (tracker) {
        count++;
        tracker = tracker->next;
    }
    return count;
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
    if (!bpf_check_lsm_available()) {
        VLOG_TRACE("cvd", "bpf_manager: BPF LSM not available, using seccomp fallback\n");
        return 0;
    }
    
    // Bump memory lock limit for BPF
    if (bpf_bump_memlock_rlimit() < 0) {
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
    
    // Clean up all entry trackers
    __cleanup_all_trackers();
    
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
    const char*               container_id,
    const char*               rootfs_path,
    struct containerv_policy* policy)
{
#ifndef __linux__
    (void)container_id;
    (void)rootfs_path;
    (void)policy;
    return 0;
#else
    unsigned long long cgroup_id;
    struct container_entry_tracker* tracker = NULL;
    int entries_added = 0;
    unsigned long long start_time, end_time;
    
    if (!g_bpf_manager.available) {
        VLOG_DEBUG("cvd", "bpf_manager: BPF not available, skipping policy population\n");
        return 0;
    }
    
    if (container_id == NULL || rootfs_path == NULL || policy == NULL) {
        errno = EINVAL;
        g_bpf_manager.metrics.failed_populate_ops++;
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
        g_bpf_manager.metrics.failed_populate_ops++;
        return -1;
    }
    
    // Start timing
    start_time = __get_time_microseconds();
    
    // Get cgroup ID for this container
    cgroup_id = bpf_get_cgroup_id(container_id);
    if (cgroup_id == 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to resolve cgroup ID for %s\n", container_id);
        g_bpf_manager.metrics.failed_populate_ops++;
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: populating policy for container %s (cgroup_id=%llu)\n",
               container_id, cgroup_id);
    
    // Create or find entry tracker for this container
    tracker = __find_tracker(container_id);
    if (!tracker) {
        tracker = __create_tracker(container_id, cgroup_id);
        if (!tracker) {
            VLOG_ERROR("cvd", "bpf_manager: failed to create entry tracker for %s\n", container_id);
            g_bpf_manager.metrics.failed_populate_ops++;
            return -1;
        }
    }
    
    // Populate policy entries for each configured path
    for (int i = 0; i < policy->path_count; i++) {
        const char* path = policy->paths[i].path;
        unsigned int allow_mask = (unsigned int)policy->paths[i].access & 
                                  (BPF_PERM_READ | BPF_PERM_WRITE | BPF_PERM_EXEC);
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
        status = bpf_policy_map_allow_inode(
            policy->backend_context,
            st.st_dev,
            st.st_ino,
            allow_mask
        );
        
        if (status < 0) {
            VLOG_WARNING("cvd", "bpf_manager: failed to add policy for %s: %s\n",
                        path, strerror(errno));
        } else {
            // Track this entry for efficient cleanup later
            if (__add_tracked_entry(tracker, cgroup_id, st.st_dev, st.st_ino) < 0) {
                VLOG_WARNING("cvd", "bpf_manager: failed to track entry for %s\n", path);
            }
            entries_added++;
            VLOG_TRACE("cvd", "bpf_manager: added policy for %s (dev=%lu, ino=%lu, mask=0x%x)\n",
                      path, (unsigned long)st.st_dev, (unsigned long)st.st_ino, allow_mask);
        }
    }
    
    // End timing and update metrics
    end_time = __get_time_microseconds();
    tracker->populate_time_us = end_time - start_time;
    g_bpf_manager.metrics.total_populate_ops++;
    
    VLOG_DEBUG("cvd", "bpf_manager: populated %d policy entries for container %s in %llu us\n",
               entries_added, container_id, tracker->populate_time_us);
    
    return 0;
#endif // __linux__
}

int containerv_bpf_manager_cleanup_policy(const char* container_id)
{
#ifndef __linux__
    (void)container_id;
    return 0;
#else
    struct container_entry_tracker* tracker;
    int deleted_count = 0;
    unsigned long long start_time, end_time;
    
    if (!g_bpf_manager.available) {
        return 0;
    }
    
    if (container_id == NULL) {
        errno = EINVAL;
        g_bpf_manager.metrics.failed_cleanup_ops++;
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: cleaning up policy for container %s\n", container_id);
    
    // Start timing
    start_time = __get_time_microseconds();
    
    // Find the entry tracker for this container
    tracker = __find_tracker(container_id);
    if (!tracker) {
        // No tracker found - this could happen if:
        // 1. Container had no policy entries configured
        // 2. Policy population failed before any entries were added
        // 3. Container was created before entry tracking was implemented
        // 
        // In all cases, returning success is correct:
        // - Case 1 & 2: Nothing to clean up
        // - Case 3: The old iterative method would also find nothing in the map
        //           for this cgroup_id (if it was already cleaned up or never populated)
        // 
        // Note: This means we rely on the fact that entries are only added through
        // populate_policy which now creates trackers. Any orphaned entries from
        // pre-tracking versions would remain in the map, but this is acceptable as:
        // - The cgroup itself is destroyed, making entries ineffective
        // - The map has a finite size and entries will be overwritten as needed
        // - A full cleanup can be done by restarting the daemon
        VLOG_DEBUG("cvd", "bpf_manager: no entry tracker found for %s, nothing to clean up\n",
                   container_id);
        return 0;
    }
    
    if (tracker->key_count == 0) {
        VLOG_DEBUG("cvd", "bpf_manager: no entries to clean up for container %s\n", container_id);
        __remove_tracker(container_id);
        return 0;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: using batch deletion for %d entries (cgroup_id=%llu)\n",
               tracker->key_count, tracker->cgroup_id);
    
    // Use batch deletion to efficiently remove all entries
    deleted_count = bpf_policy_map_delete_batch(
        g_bpf_manager.policy_map_fd,
        tracker->keys,
        tracker->key_count
    );
    
    // End timing and update metrics
    end_time = __get_time_microseconds();
    tracker->cleanup_time_us = end_time - start_time;
    
    if (deleted_count < 0) {
        VLOG_ERROR("cvd", "bpf_manager: batch deletion failed for container %s\n", container_id);
        g_bpf_manager.metrics.failed_cleanup_ops++;
        // Clean up tracker anyway
        __remove_tracker(container_id);
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: deleted %d policy entries for container %s in %llu us\n",
               deleted_count, container_id, tracker->cleanup_time_us);
    
    // Update metrics
    g_bpf_manager.metrics.total_cleanup_ops++;
    
    // Remove the tracker now that cleanup is complete
    __remove_tracker(container_id);
    
    return 0;
#endif // __linux__
}

int containerv_bpf_manager_get_metrics(struct containerv_bpf_metrics* metrics)
{
    if (metrics == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    memset(metrics, 0, sizeof(*metrics));
    
#ifndef __linux__
    return 0;
#else
    metrics->available = g_bpf_manager.available;
    metrics->total_containers = __count_containers();
    metrics->total_policy_entries = __count_total_entries();
    metrics->max_map_capacity = MAX_TRACKED_ENTRIES;
    metrics->total_populate_ops = g_bpf_manager.metrics.total_populate_ops;
    metrics->total_cleanup_ops = g_bpf_manager.metrics.total_cleanup_ops;
    metrics->failed_populate_ops = g_bpf_manager.metrics.failed_populate_ops;
    metrics->failed_cleanup_ops = g_bpf_manager.metrics.failed_cleanup_ops;
    
    return 0;
#endif
}

int containerv_bpf_manager_get_container_metrics(
    const char* container_id,
    struct containerv_bpf_container_metrics* metrics)
{
    if (container_id == NULL || metrics == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    memset(metrics, 0, sizeof(*metrics));
    
#ifndef __linux__
    return 0;
#else
    if (!g_bpf_manager.available) {
        return 0;
    }
    
    // Find the entry tracker for this container
    struct container_entry_tracker* tracker = __find_tracker(container_id);
    if (!tracker) {
        errno = ENOENT;
        return -1;
    }
    
    // Copy container ID safely
    snprintf(metrics->container_id, sizeof(metrics->container_id), "%s", container_id);
    
    // Fill in metrics
    metrics->cgroup_id = tracker->cgroup_id;
    metrics->policy_entry_count = tracker->key_count;
    metrics->populate_time_us = tracker->populate_time_us;
    metrics->cleanup_time_us = tracker->cleanup_time_us;
    
    return 0;
#endif
}
