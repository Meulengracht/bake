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
#include <glob.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <threads.h>
#include <unistd.h>
#include <vlog.h>

#include "private.h"

// import the private.h from the policies dir
#include "../policies/private.h"

#ifdef __linux__
#include <ftw.h>
#include <linux/bpf.h>
#include <sys/socket.h>
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
#define DIR_POLICY_MAP_PIN_PATH BPF_PIN_PATH "/dir_policy_map"
#define BASENAME_POLICY_MAP_PIN_PATH BPF_PIN_PATH "/basename_policy_map"
#define POLICY_LINK_PIN_PATH BPF_PIN_PATH "/fs_lsm_link"
#define EXEC_LINK_PIN_PATH BPF_PIN_PATH "/fs_lsm_exec_link"
#define MAX_TRACKED_ENTRIES 10240

/* Per-container entry tracking for efficient cleanup */
struct container_entry_tracker {
    char* container_id;
    unsigned long long cgroup_id;
    struct bpf_policy_key* file_keys;
    int file_key_count;
    int file_key_capacity;
    struct bpf_policy_key* dir_keys;
    int dir_key_count;
    int dir_key_capacity;
    struct bpf_policy_key* basename_keys;
    int basename_key_count;
    int basename_key_capacity;
    struct bpf_net_create_key* net_create_keys;
    int net_create_key_count;
    int net_create_key_capacity;
    struct bpf_net_tuple_key* net_tuple_keys;
    int net_tuple_key_count;
    int net_tuple_key_capacity;
    struct bpf_net_unix_key* net_unix_keys;
    int net_unix_key_count;
    int net_unix_key_capacity;
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
    int dir_policy_map_fd;
    int basename_policy_map_fd;
    int net_create_map_fd;
    int net_tuple_map_fd;
    int net_unix_map_fd;
#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf* skel;
    struct ring_buffer* deny_ring;
    thrd_t deny_thread;
    int deny_thread_running;
    volatile int deny_thread_stop;
#endif
    struct container_entry_tracker* trackers;
    struct bpf_manager_metrics metrics;
} g_bpf_manager = {
    .available = 0,
    .policy_map_fd = -1,
    .dir_policy_map_fd = -1,
    .basename_policy_map_fd = -1,
    .net_create_map_fd = -1,
    .net_tuple_map_fd = -1,
    .net_unix_map_fd = -1,
#ifdef HAVE_BPF_SKELETON
    .skel = NULL,
    .deny_ring = NULL,
    .deny_thread = {0},
    .deny_thread_running = 0,
    .deny_thread_stop = 0,
#endif
    .trackers = NULL,
    .metrics = {0},
};

#ifdef __linux__

#ifdef HAVE_BPF_SKELETON
static const char* __deny_hook_name(unsigned int hook_id)
{
    switch (hook_id) {
        case BPF_DENY_HOOK_FILE_OPEN: return "file_open";
        case BPF_DENY_HOOK_BPRM_CHECK: return "bprm_check_security";
        case BPF_DENY_HOOK_INODE_CREATE: return "inode_create";
        case BPF_DENY_HOOK_INODE_MKDIR: return "inode_mkdir";
        case BPF_DENY_HOOK_INODE_MKNOD: return "inode_mknod";
        case BPF_DENY_HOOK_INODE_UNLINK: return "inode_unlink";
        case BPF_DENY_HOOK_INODE_RMDIR: return "inode_rmdir";
        case BPF_DENY_HOOK_INODE_RENAME: return "inode_rename";
        case BPF_DENY_HOOK_INODE_LINK: return "inode_link";
        case BPF_DENY_HOOK_INODE_SYMLINK: return "inode_symlink";
        case BPF_DENY_HOOK_INODE_SETATTR: return "inode_setattr";
        case BPF_DENY_HOOK_PATH_TRUNCATE: return "path_truncate";
        default: return "unknown";
    }
}

static int __deny_event_cb(void* ctx, void* data, size_t size)
{
    (void)ctx;
    if (size < sizeof(struct bpf_deny_event)) {
        return 0;
    }

    const struct bpf_deny_event* ev = (const struct bpf_deny_event*)data;
    const char* hook = __deny_hook_name(ev->hook_id);

    VLOG_DEBUG(
        "cvd",
        "bpf_manager: deny hook=%s cgroup=%llu dev=%llu ino=%llu mask=0x%x comm=%s name=%.*s\n",
        hook,
        ev->cgroup_id,
        ev->dev,
        ev->ino,
        ev->required_mask,
        ev->comm,
        (int)ev->name_len,
        ev->name);

    return 0;
}

static int __deny_event_thread(void* arg)
{
    (void)arg;
    while (!g_bpf_manager.deny_thread_stop) {
        int rc = ring_buffer__poll(g_bpf_manager.deny_ring, 1000);
        if (rc < 0 && rc != -EINTR) {
            VLOG_WARNING("cvd", "bpf_manager: deny ring poll failed: %d\n", rc);
            break;
        }
    }
    return 0;
}

static int __reuse_pinned_map_or_unpin(
    struct bpf_map* map,
    const char*     pin_path,
    __u32           expected_type,
    __u32           expected_key_size,
    __u32           expected_value_size)
{
    int fd;
    struct bpf_map_info info = {};
    __u32 info_len = sizeof(info);

    if (!map || !pin_path) {
        return 0;
    }

    fd = bpf_obj_get(pin_path);
    if (fd < 0) {
        if (errno != ENOENT) {
            VLOG_WARNING("cvd", "bpf_manager: failed to open pinned map %s: %s\n", pin_path, strerror(errno));
        }
        return 0;
    }

    if (bpf_obj_get_info_by_fd(fd, &info, &info_len) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to read pinned map info %s: %s\n", pin_path, strerror(errno));
        close(fd);
        return 0;
    }

    if (info.type != expected_type ||
        info.key_size != expected_key_size ||
        info.value_size != expected_value_size) {
        VLOG_WARNING(
            "cvd",
            "bpf_manager: pinned map ABI mismatch for %s (type=%u key=%u val=%u, expected type=%u key=%u val=%u); unpinning\n",
            pin_path,
            info.type,
            info.key_size,
            info.value_size,
            expected_type,
            expected_key_size,
            expected_value_size);
        close(fd);
        (void)unlink(pin_path);
        return 0;
    }

    if (bpf_map__reuse_fd(map, fd) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to reuse pinned map %s: %s\n", pin_path, strerror(errno));
        close(fd);
        (void)unlink(pin_path);
        return 0;
    }

    VLOG_DEBUG("cvd", "bpf_manager: reusing pinned map %s\n", pin_path);
    // fd is now owned by libbpf object/map
    return 1;
}

static int __pin_map_best_effort(int map_fd, const char* pin_path, int was_reused)
{
    int status;
    if (!pin_path || map_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    // If we didn't reuse an existing pinned map, remove any stale pin first.
    if (!was_reused) {
        (void)unlink(pin_path);
    }

    status = bpf_obj_pin(map_fd, pin_path);
    if (status < 0 && errno == EEXIST) {
        // Expected when reusing an already-pinned map.
        return 0;
    }
    return status;
}
#endif

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
    tracker->file_key_capacity = 256; // Initial capacity
    tracker->file_keys = malloc(sizeof(struct bpf_policy_key) * tracker->file_key_capacity);
    if (!tracker->file_keys) {
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }

    tracker->dir_key_capacity = 64;
    tracker->dir_keys = malloc(sizeof(struct bpf_policy_key) * tracker->dir_key_capacity);
    if (!tracker->dir_keys) {
        free(tracker->file_keys);
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }

    tracker->basename_key_capacity = 32;
    tracker->basename_keys = malloc(sizeof(struct bpf_policy_key) * tracker->basename_key_capacity);
    if (!tracker->basename_keys) {
        free(tracker->dir_keys);
        free(tracker->file_keys);
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }

    tracker->net_create_key_capacity = 16;
    tracker->net_create_keys = malloc(sizeof(struct bpf_net_create_key) * tracker->net_create_key_capacity);
    if (!tracker->net_create_keys) {
        free(tracker->basename_keys);
        free(tracker->dir_keys);
        free(tracker->file_keys);
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }

    tracker->net_tuple_key_capacity = 32;
    tracker->net_tuple_keys = malloc(sizeof(struct bpf_net_tuple_key) * tracker->net_tuple_key_capacity);
    if (!tracker->net_tuple_keys) {
        free(tracker->net_create_keys);
        free(tracker->basename_keys);
        free(tracker->dir_keys);
        free(tracker->file_keys);
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }

    tracker->net_unix_key_capacity = 16;
    tracker->net_unix_keys = malloc(sizeof(struct bpf_net_unix_key) * tracker->net_unix_key_capacity);
    if (!tracker->net_unix_keys) {
        free(tracker->net_tuple_keys);
        free(tracker->net_create_keys);
        free(tracker->basename_keys);
        free(tracker->dir_keys);
        free(tracker->file_keys);
        free(tracker->container_id);
        free(tracker);
        return NULL;
    }
    
    tracker->file_key_count = 0;
    tracker->dir_key_count = 0;
    tracker->basename_key_count = 0;
    tracker->net_create_key_count = 0;
    tracker->net_tuple_key_count = 0;
    tracker->net_unix_key_count = 0;
    tracker->next = g_bpf_manager.trackers;
    g_bpf_manager.trackers = tracker;
    
    return tracker;
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

static int __add_tracked_file_entry(struct container_entry_tracker* tracker,
                                    unsigned long long cgroup_id,
                                    dev_t dev,
                                    ino_t ino)
{
    struct bpf_policy_key* key;
    
    if (!tracker) {
        return -1;
    }
    
    if (__ensure_capacity_sized((void**)&tracker->file_keys, &tracker->file_key_count, &tracker->file_key_capacity,
                                sizeof(*tracker->file_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked file key capacity\n");
        return -1;
    }
    
    // Add the key
    key = &tracker->file_keys[tracker->file_key_count];
    key->cgroup_id = cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    tracker->file_key_count++;
    
    return 0;
}

static int __add_tracked_dir_entry(struct container_entry_tracker* tracker,
                                   unsigned long long cgroup_id,
                                   dev_t dev,
                                   ino_t ino)
{
    struct bpf_policy_key* key;

    if (!tracker) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&tracker->dir_keys, &tracker->dir_key_count, &tracker->dir_key_capacity,
                                sizeof(*tracker->dir_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked dir key capacity\n");
        return -1;
    }

    key = &tracker->dir_keys[tracker->dir_key_count];
    key->cgroup_id = cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    tracker->dir_key_count++;
    return 0;
}

static int __add_tracked_basename_entry(struct container_entry_tracker* tracker,
                                        unsigned long long cgroup_id,
                                        dev_t dev,
                                        ino_t ino)
{
    struct bpf_policy_key* key;

    if (!tracker) {
        return -1;
    }

    // Avoid duplicates (we delete per-dir key as a whole)
    for (int i = 0; i < tracker->basename_key_count; i++) {
        if (tracker->basename_keys[i].cgroup_id == cgroup_id &&
            tracker->basename_keys[i].dev == (unsigned long long)dev &&
            tracker->basename_keys[i].ino == (unsigned long long)ino) {
            return 0;
        }
    }

    if (__ensure_capacity_sized((void**)&tracker->basename_keys, &tracker->basename_key_count, &tracker->basename_key_capacity,
                                sizeof(*tracker->basename_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked basename key capacity\n");
        return -1;
    }

    key = &tracker->basename_keys[tracker->basename_key_count];
    key->cgroup_id = cgroup_id;
    key->dev = (unsigned long long)dev;
    key->ino = (unsigned long long)ino;
    tracker->basename_key_count++;
    return 0;
}

static int __add_tracked_net_create_entry(struct container_entry_tracker* tracker,
                                          const struct bpf_net_create_key* key)
{
    if (!tracker || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&tracker->net_create_keys,
                                &tracker->net_create_key_count,
                                &tracker->net_create_key_capacity,
                                sizeof(*tracker->net_create_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net create key capacity\n");
        return -1;
    }

    tracker->net_create_keys[tracker->net_create_key_count++] = *key;
    return 0;
}

static int __add_tracked_net_tuple_entry(struct container_entry_tracker* tracker,
                                         const struct bpf_net_tuple_key* key)
{
    if (!tracker || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&tracker->net_tuple_keys,
                                &tracker->net_tuple_key_count,
                                &tracker->net_tuple_key_capacity,
                                sizeof(*tracker->net_tuple_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net tuple key capacity\n");
        return -1;
    }

    tracker->net_tuple_keys[tracker->net_tuple_key_count++] = *key;
    return 0;
}

static int __add_tracked_net_unix_entry(struct container_entry_tracker* tracker,
                                        const struct bpf_net_unix_key* key)
{
    if (!tracker || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&tracker->net_unix_keys,
                                &tracker->net_unix_key_count,
                                &tracker->net_unix_key_capacity,
                                sizeof(*tracker->net_unix_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net unix key capacity\n");
        return -1;
    }

    tracker->net_unix_keys[tracker->net_unix_key_count++] = *key;
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
            free(tracker->file_keys);
            free(tracker->dir_keys);
            free(tracker->basename_keys);
            free(tracker->net_create_keys);
            free(tracker->net_tuple_keys);
            free(tracker->net_unix_keys);
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
        free(tracker->file_keys);
        free(tracker->dir_keys);
        free(tracker->basename_keys);
        free(tracker->net_create_keys);
        free(tracker->net_tuple_keys);
        free(tracker->net_unix_keys);
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
        total += tracker->file_key_count;
        total += tracker->dir_key_count;
        total += tracker->basename_key_count;
        total += tracker->net_create_key_count;
        total += tracker->net_tuple_key_count;
        total += tracker->net_unix_key_count;
        tracker = tracker->next;
    }
    return total;
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
    struct bpf_policy_context* ctx,
    struct container_entry_tracker* tracker,
    unsigned long long cgroup_id,
    const char* resolved_path,
    unsigned int allow_mask,
    unsigned int dir_flags)
{
    struct stat st;
    if (stat(resolved_path, &st) < 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (bpf_dir_policy_map_allow_dir(ctx, st.st_dev, st.st_ino, allow_mask, dir_flags) < 0) {
            return -1;
        }
        (void)__add_tracked_dir_entry(tracker, cgroup_id, st.st_dev, st.st_ino);
        return 0;
    }

    if (bpf_policy_map_allow_inode(ctx, st.st_dev, st.st_ino, allow_mask) < 0) {
        return -1;
    }
    (void)__add_tracked_file_entry(tracker, cgroup_id, st.st_dev, st.st_ino);
    return 0;
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


struct __path_walk_ctx {
    struct bpf_policy_context* bpf_ctx;
    unsigned int              allowMask;
};

static struct __path_walk_ctx* __g_walk_ctx;

static int __walk_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    struct __path_walk_ctx* ctx = __g_walk_ctx;
    struct stat             st;
    int                     status;

    (void)sb;
    (void)typeflag;
    (void)ftwbuf;

    if (!ctx || !ctx->bpf_ctx) {
        return 1;
    }

    status = stat(fpath, &st);
    if (status < 0) {
        return 0;
    }

    status = bpf_policy_map_allow_inode(ctx->bpf_ctx, st.st_dev, st.st_ino, ctx->allowMask);
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
    struct bpf_policy_context* bpf_ctx,
    const char*               rootPath,
    unsigned int              allowMask)
{
    struct __path_walk_ctx ctx = {
        .bpf_ctx   = bpf_ctx,
        .allowMask = allowMask,
    };

    __g_walk_ctx = &ctx;
    int status = nftw(rootPath, __walk_cb, 16, FTW_PHYS | FTW_MOUNT);
    __g_walk_ctx = NULL;
    return status;
}

static int __allow_single_path(
    struct bpf_policy_context* bpf_ctx,
    const char*               path,
    unsigned int              allowMask,
    unsigned int              dirFlags)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    if (S_ISDIR(st.st_mode)) {
        if (bpf_ctx->dir_map_fd < 0) {
            errno = ENOTSUP;
            return -1;
        }
        return bpf_dir_policy_map_allow_dir(bpf_ctx, st.st_dev, st.st_ino, allowMask, dirFlags);
    }
    return bpf_policy_map_allow_inode(bpf_ctx, st.st_dev, st.st_ino, allowMask);
}

static int __allow_path_or_tree(
    struct bpf_policy_context* bpf_ctx,
    const char*               path,
    unsigned int              allowMask)
{
    struct stat st;
    int status;

    status = stat(path, &st);
    if (status < 0) {
        return status;
    }

    if (S_ISDIR(st.st_mode)) {
        // Prefer scalable directory rules when available
        status = __allow_single_path(bpf_ctx, path, allowMask, BPF_DIR_RULE_RECURSIVE);
        if (status == 0) {
            return 0;
        }
        // Fallback for older kernels/programs: enumerate all inodes
        return __allow_path_recursive(bpf_ctx, path, allowMask);
    }

    return __allow_single_path(bpf_ctx, path, allowMask, 0);
}

int bpf_manager_add_allow_pattern(
    struct bpf_policy_context* bpf_ctx,
    const char*                pattern,
    unsigned int               allowMask)
{
    size_t plen;
    char base[PATH_MAX];
    char glob_pattern[PATH_MAX];
    glob_t g;
    int status;

    // Handle special scalable forms: /dir/* and /dir/**
    plen = strlen(pattern);
    if (__ends_with(pattern, "/**") && plen >= 3) {
        snprintf(base, sizeof(base), "%.*s", (int)(plen - 3), pattern);
        return __allow_single_path(bpf_ctx, base, allowMask, BPF_DIR_RULE_RECURSIVE);
    }
    if (__ends_with(pattern, "/*") && plen >= 2) {
        snprintf(base, sizeof(base), "%.*s", (int)(plen - 2), pattern);
        return __allow_single_path(bpf_ctx, base, allowMask, BPF_DIR_RULE_CHILDREN_ONLY);
    }

    // Basename-only globbing: allow pattern under parent directory inode, without requiring files to exist.
    // Only applies when the parent path has no glob chars.
    if (bpf_ctx->basename_map_fd >= 0 && __has_glob_chars_range(pattern, strlen(pattern))) {
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
                    return __allow_single_path(bpf_ctx, parent_path, allowMask, BPF_DIR_RULE_CHILDREN_ONLY);
                }

                struct bpf_basename_rule rule = {};
                if (__parse_basename_rule(base_pat, allowMask, &rule) == 0) {
                    if (stat(parent_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                        if (bpf_basename_policy_map_allow_rule(bpf_ctx, st.st_dev, st.st_ino, &rule) == 0) {
                            return 0;
                        }
                    }
                }
            }
        }
    }

    memset(&g, 0, sizeof(g));
    __glob_translate_plus(pattern, glob_pattern, sizeof(glob_pattern));
    status = glob(glob_pattern, GLOB_NOSORT, NULL, &g);
    if (status == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            (void)__allow_path_or_tree(bpf_ctx, g.gl_pathv[i], allowMask);
        }
        globfree(&g);
        return 0;
    }

    globfree(&g);
    /* If no glob matches, treat it as a literal path */
    return __allow_path_or_tree(bpf_ctx, pattern, allowMask);
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

    // Reuse pinned maps when ABI matches; if ABI mismatches, auto-unpin to avoid upgrade wedging.
    int reused_policy = __reuse_pinned_map_or_unpin(
        g_bpf_manager.skel->maps.policy_map,
        POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_policy_value));
    int reused_dir = __reuse_pinned_map_or_unpin(
        g_bpf_manager.skel->maps.dir_policy_map,
        DIR_POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_dir_policy_value));
    int reused_basename = __reuse_pinned_map_or_unpin(
        g_bpf_manager.skel->maps.basename_policy_map,
        BASENAME_POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_basename_policy_value));
    
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

    // Get directory policy map FD
    g_bpf_manager.dir_policy_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.dir_policy_map);
    if (g_bpf_manager.dir_policy_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get dir_policy_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        g_bpf_manager.policy_map_fd = -1;
        return -1;
    }

    // Get basename policy map FD
    g_bpf_manager.basename_policy_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.basename_policy_map);
    if (g_bpf_manager.basename_policy_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get basename_policy_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        g_bpf_manager.policy_map_fd = -1;
        g_bpf_manager.dir_policy_map_fd = -1;
        return -1;
    }

    // Get network policy map FDs
    g_bpf_manager.net_create_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.net_create_map);
    if (g_bpf_manager.net_create_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get net_create_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        g_bpf_manager.policy_map_fd = -1;
        g_bpf_manager.dir_policy_map_fd = -1;
        g_bpf_manager.basename_policy_map_fd = -1;
        return -1;
    }

    g_bpf_manager.net_tuple_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.net_tuple_map);
    if (g_bpf_manager.net_tuple_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get net_tuple_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        g_bpf_manager.policy_map_fd = -1;
        g_bpf_manager.dir_policy_map_fd = -1;
        g_bpf_manager.basename_policy_map_fd = -1;
        g_bpf_manager.net_create_map_fd = -1;
        return -1;
    }

    g_bpf_manager.net_unix_map_fd = bpf_map__fd(g_bpf_manager.skel->maps.net_unix_map);
    if (g_bpf_manager.net_unix_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get net_unix_map FD\n");
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
        g_bpf_manager.policy_map_fd = -1;
        g_bpf_manager.dir_policy_map_fd = -1;
        g_bpf_manager.basename_policy_map_fd = -1;
        g_bpf_manager.net_create_map_fd = -1;
        g_bpf_manager.net_tuple_map_fd = -1;
        return -1;
    }
    
    // Pin the policy map for persistence and sharing
    status = __pin_map_best_effort(g_bpf_manager.policy_map_fd, POLICY_MAP_PIN_PATH, reused_policy);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin policy map to %s: %s\n",
                    POLICY_MAP_PIN_PATH, strerror(errno));
        // Continue anyway - map is still usable via FD
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: policy map pinned to %s\n", POLICY_MAP_PIN_PATH);
    }

    // Pin the directory policy map for persistence and sharing
    status = __pin_map_best_effort(g_bpf_manager.dir_policy_map_fd, DIR_POLICY_MAP_PIN_PATH, reused_dir);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin dir policy map to %s: %s\n",
                    DIR_POLICY_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: dir policy map pinned to %s\n", DIR_POLICY_MAP_PIN_PATH);
    }

    // Pin the basename policy map for persistence and sharing
    status = __pin_map_best_effort(g_bpf_manager.basename_policy_map_fd, BASENAME_POLICY_MAP_PIN_PATH, reused_basename);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin basename policy map to %s: %s\n",
                    BASENAME_POLICY_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: basename policy map pinned to %s\n", BASENAME_POLICY_MAP_PIN_PATH);
    }

    // Pin the LSM link so other processes can verify enforcement is active.
    // Without this, a stale pinned map could exist without any program attached.
    if (g_bpf_manager.skel->links.file_open_restrict != NULL) {
        (void)unlink(POLICY_LINK_PIN_PATH);
        status = bpf_link__pin(g_bpf_manager.skel->links.file_open_restrict, POLICY_LINK_PIN_PATH);
    }

    if (g_bpf_manager.skel->links.file_open_restrict == NULL) {
        VLOG_WARNING("cvd", "bpf_manager: no BPF link for file_open_restrict; cannot pin enforcement link\n");
    } else if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin enforcement link to %s: %s\n",
                     POLICY_LINK_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: enforcement link pinned to %s\n", POLICY_LINK_PIN_PATH);
    }

    // Pin exec enforcement link if available (best-effort)
    if (g_bpf_manager.skel->links.bprm_check_security_restrict != NULL) {
        (void)unlink(EXEC_LINK_PIN_PATH);
        status = bpf_link__pin(g_bpf_manager.skel->links.bprm_check_security_restrict, EXEC_LINK_PIN_PATH);
        if (status < 0) {
            VLOG_WARNING("cvd", "bpf_manager: failed to pin exec enforcement link to %s: %s\n",
                         EXEC_LINK_PIN_PATH, strerror(errno));
        } else {
            VLOG_DEBUG("cvd", "bpf_manager: exec enforcement link pinned to %s\n", EXEC_LINK_PIN_PATH);
        }
    }

    // Setup deny ring buffer for debug logging (best-effort)
    if (g_bpf_manager.skel->maps.deny_events != NULL) {
        int deny_fd = bpf_map__fd(g_bpf_manager.skel->maps.deny_events);
        if (deny_fd >= 0) {
            g_bpf_manager.deny_ring = ring_buffer__new(deny_fd, __deny_event_cb, NULL, NULL);
            if (g_bpf_manager.deny_ring) {
                g_bpf_manager.deny_thread_stop = 0;
                if (thrd_create(&g_bpf_manager.deny_thread, __deny_event_thread, NULL) == thrd_success) {
                    g_bpf_manager.deny_thread_running = 1;
                    VLOG_DEBUG("cvd", "bpf_manager: deny ring buffer logging enabled\n");
                } else {
                    ring_buffer__free(g_bpf_manager.deny_ring);
                    g_bpf_manager.deny_ring = NULL;
                    VLOG_WARNING("cvd", "bpf_manager: failed to start deny ring thread\n");
                }
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to create deny ring buffer\n");
            }
        }
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

    if (unlink(DIR_POLICY_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin dir policy map: %s\n",
                     strerror(errno));
    }

    if (unlink(BASENAME_POLICY_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin basename policy map: %s\n",
                     strerror(errno));
    }

    // Unpin link (best-effort)
    if (unlink(POLICY_LINK_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin enforcement link: %s\n",
                     strerror(errno));
    }

    if (unlink(EXEC_LINK_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin exec enforcement link: %s\n",
                     strerror(errno));
    }
    
    if (g_bpf_manager.deny_thread_running) {
        g_bpf_manager.deny_thread_stop = 1;
        (void)thrd_join(g_bpf_manager.deny_thread, NULL);
        g_bpf_manager.deny_thread_running = 0;
    }
    if (g_bpf_manager.deny_ring) {
        ring_buffer__free(g_bpf_manager.deny_ring);
        g_bpf_manager.deny_ring = NULL;
    }

    // Destroy skeleton (this detaches programs)
    if (g_bpf_manager.skel) {
        fs_lsm_bpf__destroy(g_bpf_manager.skel);
        g_bpf_manager.skel = NULL;
    }
    
    g_bpf_manager.policy_map_fd = -1;
    g_bpf_manager.dir_policy_map_fd = -1;
    g_bpf_manager.basename_policy_map_fd = -1;
    g_bpf_manager.net_create_map_fd = -1;
    g_bpf_manager.net_tuple_map_fd = -1;
    g_bpf_manager.net_unix_map_fd = -1;
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
    
    struct bpf_policy_context bpf_ctx = {
        .map_fd = g_bpf_manager.policy_map_fd,
        .dir_map_fd = g_bpf_manager.dir_policy_map_fd,
        .basename_map_fd = g_bpf_manager.basename_policy_map_fd,
        .cgroup_id = cgroup_id,
    };

    // Populate policy entries for each configured path
    for (int i = 0; i < policy->path_count; i++) {
        const char* path = policy->paths[i].path;
        unsigned int allow_mask = (unsigned int)policy->paths[i].access &
                                  (BPF_PERM_READ | BPF_PERM_WRITE | BPF_PERM_EXEC);
        char full_path[PATH_MAX];
        size_t root_len, path_len;
        int status;

        if (!path) {
            continue;
        }

        root_len = strlen(rootfs_path);
        path_len = strlen(path);
        if (root_len + path_len >= sizeof(full_path)) {
            VLOG_WARNING("cvd",
                         "bpf_manager: combined rootfs path and policy path too long, skipping (rootfs=\"%s\", path=\"%s\")\n",
                         rootfs_path, path);
            continue;
        }

        // Special scalable forms: /dir/* and /dir/**
        if (__ends_with(path, "/**")) {
            char base[PATH_MAX];
            snprintf(base, sizeof(base), "%.*s", (int)(path_len - 3), path);
            snprintf(full_path, sizeof(full_path), "%s%s", rootfs_path, base);
            status = __apply_single_path(&bpf_ctx, tracker, cgroup_id, full_path, allow_mask, BPF_DIR_RULE_RECURSIVE);
            if (status == 0) {
                entries_added++;
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply dir recursive rule for %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        if (__ends_with(path, "/*")) {
            char base[PATH_MAX];
            snprintf(base, sizeof(base), "%.*s", (int)(path_len - 2), path);
            snprintf(full_path, sizeof(full_path), "%s%s", rootfs_path, base);
            status = __apply_single_path(&bpf_ctx, tracker, cgroup_id, full_path, allow_mask, BPF_DIR_RULE_CHILDREN_ONLY);
            if (status == 0) {
                entries_added++;
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply dir children rule for %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        // Any globbing chars: expand to concrete paths; for dirs use recursive dir rules
        snprintf(full_path, sizeof(full_path), "%s%s", rootfs_path, path);
        if (__has_glob_chars(path)) {
            // If globbing only affects the basename, install a basename rule under the parent dir inode
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

                    // "*" is equivalent to children-only directory rule
                    if (strcmp(base_pat, "*") == 0) {
                        snprintf(parent_abs, sizeof(parent_abs), "%s%s", rootfs_path, parent_rel);
                        if (__apply_single_path(&bpf_ctx, tracker, cgroup_id, parent_abs, allow_mask, BPF_DIR_RULE_CHILDREN_ONLY) == 0) {
                            entries_added++;
                            continue;
                        }
                    } else {
                        struct bpf_basename_rule rule = {};
                        if (__parse_basename_rule(base_pat, allow_mask, &rule) == 0) {
                            snprintf(parent_abs, sizeof(parent_abs), "%s%s", rootfs_path, parent_rel);
                            if (stat(parent_abs, &st) == 0 && S_ISDIR(st.st_mode)) {
                                if (bpf_basename_policy_map_allow_rule(&bpf_ctx, st.st_dev, st.st_ino, &rule) == 0) {
                                    (void)__add_tracked_basename_entry(tracker, cgroup_id, st.st_dev, st.st_ino);
                                    entries_added++;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }

            char glob_path[PATH_MAX];
            __glob_translate_plus(full_path, glob_path, sizeof(glob_path));
            glob_t g;
            memset(&g, 0, sizeof(g));
            int gstatus = glob(glob_path, GLOB_NOSORT, NULL, &g);
            if (gstatus == 0) {
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    if (__apply_single_path(&bpf_ctx, tracker, cgroup_id, g.gl_pathv[j], allow_mask, BPF_DIR_RULE_RECURSIVE) == 0) {
                        entries_added++;
                    }
                }
                globfree(&g);
                continue;
            }
            globfree(&g);
            // No matches -> treat as literal
        }

        // Literal path: if directory, allow subtree; else allow inode
        status = __apply_single_path(&bpf_ctx, tracker, cgroup_id, full_path, allow_mask, BPF_DIR_RULE_RECURSIVE);
        if (status == 0) {
            entries_added++;
        } else {
            VLOG_WARNING("cvd", "bpf_manager: failed to apply rule for %s: %s\n", path, strerror(errno));
        }
    }

    if (policy->net_rule_count > 0) {
        if (policy->net_rule_count > MAX_NET_RULES) {
            VLOG_ERROR("cvd", "bpf_manager: policy net_rule_count (%d) exceeds MAX_NET_RULES (%d)\n",
                       policy->net_rule_count, MAX_NET_RULES);
            errno = EINVAL;
            g_bpf_manager.metrics.failed_populate_ops++;
            return -1;
        }

        for (int i = 0; i < policy->net_rule_count; i++) {
            const struct containerv_policy_net_rule* rule = &policy->net_rules[i];
            unsigned int create_mask = rule->allow_mask & BPF_NET_CREATE;
            unsigned int tuple_mask = rule->allow_mask & ~BPF_NET_CREATE;

            if (create_mask) {
                struct bpf_net_create_key ckey = {
                    .cgroup_id = cgroup_id,
                    .family = (unsigned int)rule->family,
                    .type = (unsigned int)rule->type,
                    .protocol = (unsigned int)rule->protocol,
                };
                if (bpf_net_create_map_allow(g_bpf_manager.net_create_map_fd, &ckey, create_mask) == 0) {
                    (void)__add_tracked_net_create_entry(tracker, &ckey);
                    entries_added++;
                } else {
                    VLOG_WARNING("cvd", "bpf_manager: failed to apply net create rule (family=%d type=%d proto=%d): %s\n",
                                 rule->family, rule->type, rule->protocol, strerror(errno));
                }
            }

            if (tuple_mask == 0) {
                continue;
            }

#ifdef AF_UNIX
            if (rule->family == AF_UNIX) {
                struct bpf_net_unix_key ukey = {0};
                if (rule->unix_path == NULL || rule->unix_path[0] == '\0') {
                    VLOG_WARNING("cvd", "bpf_manager: net unix rule missing path (family=AF_UNIX)\n");
                    continue;
                }
                ukey.cgroup_id = cgroup_id;
                ukey.type = (unsigned int)rule->type;
                ukey.protocol = (unsigned int)rule->protocol;
                snprintf(ukey.path, sizeof(ukey.path), "%s", rule->unix_path);

                if (bpf_net_unix_map_allow(g_bpf_manager.net_unix_map_fd, &ukey, tuple_mask) == 0) {
                    (void)__add_tracked_net_unix_entry(tracker, &ukey);
                    entries_added++;
                } else {
                    VLOG_WARNING("cvd", "bpf_manager: failed to apply net unix rule (%s): %s\n",
                                 rule->unix_path, strerror(errno));
                }
                continue;
            }
#endif

            if (rule->addr_len > BPF_NET_ADDR_MAX) {
                VLOG_WARNING("cvd", "bpf_manager: net rule addr_len too large (%u)\n", rule->addr_len);
                continue;
            }

            struct bpf_net_tuple_key tkey = {0};
            tkey.cgroup_id = cgroup_id;
            tkey.family = (unsigned int)rule->family;
            tkey.type = (unsigned int)rule->type;
            tkey.protocol = (unsigned int)rule->protocol;
            tkey.port = rule->port;
            if (rule->addr_len > 0) {
                memcpy(tkey.addr, rule->addr, rule->addr_len);
            }

            if (bpf_net_tuple_map_allow(g_bpf_manager.net_tuple_map_fd, &tkey, tuple_mask) == 0) {
                (void)__add_tracked_net_tuple_entry(tracker, &tkey);
                entries_added++;
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply net tuple rule (family=%d type=%d proto=%d): %s\n",
                             rule->family, rule->type, rule->protocol, strerror(errno));
            }
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
    
    if (tracker->file_key_count == 0 &&
        tracker->dir_key_count == 0 &&
        tracker->basename_key_count == 0 &&
        tracker->net_create_key_count == 0 &&
        tracker->net_tuple_key_count == 0 &&
        tracker->net_unix_key_count == 0) {
        VLOG_DEBUG("cvd", "bpf_manager: no entries to clean up for container %s\n", container_id);
        __remove_tracker(container_id);
        return 0;
    }
    
    struct bpf_policy_context delete_files_ctx = {
        .map_fd = g_bpf_manager.policy_map_fd,
        .dir_map_fd = g_bpf_manager.dir_policy_map_fd,
        .basename_map_fd = g_bpf_manager.basename_policy_map_fd,
        .cgroup_id = tracker->cgroup_id,
    };
    struct bpf_policy_context delete_dirs_ctx = {
        .map_fd = g_bpf_manager.dir_policy_map_fd,
        .dir_map_fd = g_bpf_manager.dir_policy_map_fd,
        .basename_map_fd = g_bpf_manager.basename_policy_map_fd,
        .cgroup_id = tracker->cgroup_id,
    };
    struct bpf_policy_context delete_basename_ctx = {
        .map_fd = g_bpf_manager.basename_policy_map_fd,
        .dir_map_fd = g_bpf_manager.dir_policy_map_fd,
        .basename_map_fd = g_bpf_manager.basename_policy_map_fd,
        .cgroup_id = tracker->cgroup_id,
    };

    if (tracker->file_key_count > 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d file entries (cgroup_id=%llu)\n",
                   tracker->file_key_count, tracker->cgroup_id);
        deleted_count = bpf_policy_map_delete_batch(&delete_files_ctx, tracker->file_keys, tracker->file_key_count);
        if (deleted_count < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (file map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
    }

    if (tracker->dir_key_count > 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d dir entries (cgroup_id=%llu)\n",
                   tracker->dir_key_count, tracker->cgroup_id);
        int deleted_dirs = bpf_policy_map_delete_batch(&delete_dirs_ctx, tracker->dir_keys, tracker->dir_key_count);
        if (deleted_dirs < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (dir map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
        deleted_count += deleted_dirs;
    }

    if (tracker->basename_key_count > 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d basename entries (cgroup_id=%llu)\n",
                   tracker->basename_key_count, tracker->cgroup_id);
        int deleted_base = bpf_policy_map_delete_batch(&delete_basename_ctx, tracker->basename_keys, tracker->basename_key_count);
        if (deleted_base < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (basename map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
        deleted_count += deleted_base;
    }

    if (tracker->net_create_key_count > 0 && g_bpf_manager.net_create_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net create entries (cgroup_id=%llu)\n",
                   tracker->net_create_key_count, tracker->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            g_bpf_manager.net_create_map_fd,
            tracker->net_create_keys,
            tracker->net_create_key_count,
            sizeof(*tracker->net_create_keys));
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net create map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
        deleted_count += deleted_net;
    }

    if (tracker->net_tuple_key_count > 0 && g_bpf_manager.net_tuple_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net tuple entries (cgroup_id=%llu)\n",
                   tracker->net_tuple_key_count, tracker->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            g_bpf_manager.net_tuple_map_fd,
            tracker->net_tuple_keys,
            tracker->net_tuple_key_count,
            sizeof(*tracker->net_tuple_keys));
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net tuple map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
        deleted_count += deleted_net;
    }

    if (tracker->net_unix_key_count > 0 && g_bpf_manager.net_unix_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net unix entries (cgroup_id=%llu)\n",
                   tracker->net_unix_key_count, tracker->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            g_bpf_manager.net_unix_map_fd,
            tracker->net_unix_keys,
            tracker->net_unix_key_count,
            sizeof(*tracker->net_unix_keys));
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net unix map) for container %s\n", container_id);
            g_bpf_manager.metrics.failed_cleanup_ops++;
            __remove_tracker(container_id);
            return -1;
        }
        deleted_count += deleted_net;
    }
    
    // End timing and update metrics
    end_time = __get_time_microseconds();
    tracker->cleanup_time_us = end_time - start_time;
    
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
    metrics->policy_entry_count = tracker->file_key_count + tracker->dir_key_count + tracker->basename_key_count;
    metrics->populate_time_us = tracker->populate_time_us;
    metrics->cleanup_time_us = tracker->cleanup_time_us;
    
    return 0;
#endif
}
