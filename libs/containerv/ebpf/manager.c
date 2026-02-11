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

#include <chef/containerv/bpf.h>
#include <chef/platform.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>
#include <vlog.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "container-context.h"
#include "map-ops.h"

// import the private.h from the policies dir
#include "../policies/private.h"

#ifdef HAVE_BPF_SKELETON
#include "fs-lsm.skel.h"
#include "net-lsm.skel.h"
#endif

struct __manager_metrics {
    unsigned long long total_populate_ops;
    unsigned long long total_cleanup_ops;
    unsigned long long failed_populate_ops;
    unsigned long long failed_cleanup_ops;
};

static struct {
    enum containerv_bpf_status status;
    
    // Policy map file descriptors
    int policy_map_fd;
    int dir_policy_map_fd;
    int basename_policy_map_fd;
    int net_create_map_fd;
    int net_tuple_map_fd;
    int net_unix_map_fd;

    // Denial queues
    struct ring_buffer* fs_denials;
    struct ring_buffer* net_denials;

    // Thread for processing deny events from BPF programs
    thrd_t       deny_thread;
    int          deny_thread_running;
    volatile int deny_thread_stop;
    
    // Trackers for each active container (linked list)
    struct list trackers; //list<bpf_container_context>;

    // Metrics for monitoring performance and failures
    struct __manager_metrics      metrics;

#ifdef HAVE_BPF_SKELETON
    struct fs_lsm_bpf*  fs_skel;
    struct net_lsm_bpf* net_skel;
#endif
} g_bpf = {
    .status = CV_BPF_UNINITIALIZED,
    
    .policy_map_fd = -1,
    .dir_policy_map_fd = -1,
    .basename_policy_map_fd = -1,
    .net_create_map_fd = -1,
    .net_tuple_map_fd = -1,
    .net_unix_map_fd = -1,

    .fs_denials = NULL,
    .net_denials = NULL,

    .deny_thread = 0,
    .deny_thread_running = 0,
    .deny_thread_stop = 0,

    .trackers = {0},
    .metrics = {0},

#ifdef HAVE_BPF_SKELETON
    .fs_skel = NULL,
    .net_skel = NULL,
#endif
};

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
        case BPF_DENY_HOOK_SOCKET_CREATE: return "socket_create";
        case BPF_DENY_HOOK_SOCKET_BIND: return "socket_bind";
        case BPF_DENY_HOOK_SOCKET_CONNECT: return "socket_connect";
        case BPF_DENY_HOOK_SOCKET_LISTEN: return "socket_listen";
        case BPF_DENY_HOOK_SOCKET_ACCEPT: return "socket_accept";
        case BPF_DENY_HOOK_SOCKET_SENDMSG: return "socket_sendmsg";
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
    int epfd = -1;
    struct epoll_event ev = {0};
    struct epoll_event events[2];

    if (g_bpf.fs_denials == NULL && g_bpf.net_denials == NULL) {
        return 0;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        VLOG_WARNING("cvd", "bpf_manager: epoll_create1 failed: %s\n", strerror(errno));
        return 0;
    }

    if (g_bpf.fs_denials != NULL) {
        int fd = ring_buffer__epoll_fd(g_bpf.fs_denials);
        if (fd < 0) {
            VLOG_WARNING("cvd", "bpf_manager: fs deny ring epoll fd failed: %d\n", fd);
            close(epfd);
            return 0;
        }
        ev.events = EPOLLIN;
        ev.data.ptr = g_bpf.fs_denials;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            VLOG_WARNING("cvd", "bpf_manager: epoll_ctl add fs ring failed: %s\n", strerror(errno));
            close(epfd);
            return 0;
        }
    }

    if (g_bpf.net_denials != NULL) {
        int fd = ring_buffer__epoll_fd(g_bpf.net_denials);
        if (fd < 0) {
            VLOG_WARNING("cvd", "bpf_manager: net deny ring epoll fd failed: %d\n", fd);
            close(epfd);
            return 0;
        }
        ev.events = EPOLLIN;
        ev.data.ptr = g_bpf.net_denials;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            VLOG_WARNING("cvd", "bpf_manager: epoll_ctl add net ring failed: %s\n", strerror(errno));
            close(epfd);
            return 0;
        }
    }

    while (!g_bpf.deny_thread_stop) {
        int n = epoll_wait(epfd, events, (int)(sizeof(events) / sizeof(events[0])), 1000);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            VLOG_WARNING("cvd", "bpf_manager: epoll_wait failed: %s\n", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            struct ring_buffer* ring = (struct ring_buffer*)events[i].data.ptr;
            int rc = ring_buffer__consume(ring);
            if (rc < 0 && rc != -EINTR) {
                VLOG_WARNING("cvd", "bpf_manager: deny ring consume failed: %d\n", rc);
                g_bpf.deny_thread_stop = 1;
                break;
            }
        }
    }

    close(epfd);
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

/* Helper functions for entry tracking */
static struct bpf_container_context* __container_context_lookup(const char* containerId)
{
    struct list_item* i;
    list_foreach(&g_bpf.trackers, i) {
        struct bpf_container_context* tracker = (struct bpf_container_context*)i;
        if (strcmp(tracker->container_id, containerId) == 0) {
            return tracker;
        }
    }
    return NULL;
}

static void __container_context_remove(const char* containerId)
{
    struct bpf_container_context* tracker = __container_context_lookup(containerId);
    
    if (tracker == NULL) {
        return;
    }
    
    list_remove(&g_bpf.trackers, &tracker->header);
    bpf_container_context_delete(tracker);
}

static unsigned long long __get_time_microseconds(void)
{
    struct timespec ts;
    // Use CLOCK_MONOTONIC for timing measurements to avoid issues with
    // system clock adjustments (NTP, manual changes, etc.)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)(ts.tv_sec) * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
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

#ifdef HAVE_BPF_SKELETON
static int __load_fs_program(void)
{
    int status;

    g_bpf.fs_skel = fs_lsm_bpf__open();
    if (!g_bpf.fs_skel) {
        VLOG_ERROR("cvd", "bpf_manager: failed to open fs BPF skeleton\n");
        return -1;
    }

    VLOG_DEBUG("cvd", "bpf_manager: fs BPF skeleton opened\n");

    int reused_policy = __reuse_pinned_map_or_unpin(
        g_bpf.fs_skel->maps.policy_map,
        POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_policy_value));
    int reused_dir = __reuse_pinned_map_or_unpin(
        g_bpf.fs_skel->maps.dir_policy_map,
        DIR_POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_dir_policy_value));
    int reused_basename = __reuse_pinned_map_or_unpin(
        g_bpf.fs_skel->maps.basename_policy_map,
        BASENAME_POLICY_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_policy_key),
        sizeof(struct bpf_basename_policy_value));

    status = fs_lsm_bpf__load(g_bpf.fs_skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to load fs BPF skeleton: %d\n", status);
        fs_lsm_bpf__destroy(g_bpf.fs_skel);
        g_bpf.fs_skel = NULL;
        return -1;
    }

    status = fs_lsm_bpf__attach(g_bpf.fs_skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to attach fs BPF LSM program: %d\n", status);
        fs_lsm_bpf__destroy(g_bpf.fs_skel);
        g_bpf.fs_skel = NULL;
        return -1;
    }

    g_bpf.policy_map_fd = bpf_map__fd(g_bpf.fs_skel->maps.policy_map);
    g_bpf.dir_policy_map_fd = bpf_map__fd(g_bpf.fs_skel->maps.dir_policy_map);
    g_bpf.basename_policy_map_fd = bpf_map__fd(g_bpf.fs_skel->maps.basename_policy_map);
    if (g_bpf.policy_map_fd < 0 ||
        g_bpf.dir_policy_map_fd < 0 ||
        g_bpf.basename_policy_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get fs policy map fds\n");
        fs_lsm_bpf__destroy(g_bpf.fs_skel);
        g_bpf.fs_skel = NULL;
        g_bpf.policy_map_fd = -1;
        g_bpf.dir_policy_map_fd = -1;
        g_bpf.basename_policy_map_fd = -1;
        return -1;
    }

    status = __pin_map_best_effort(g_bpf.policy_map_fd, POLICY_MAP_PIN_PATH, reused_policy);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin policy map to %s: %s\n",
                    POLICY_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: policy map pinned to %s\n", POLICY_MAP_PIN_PATH);
    }

    status = __pin_map_best_effort(g_bpf.dir_policy_map_fd, DIR_POLICY_MAP_PIN_PATH, reused_dir);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin dir policy map to %s: %s\n",
                    DIR_POLICY_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: dir policy map pinned to %s\n", DIR_POLICY_MAP_PIN_PATH);
    }

    status = __pin_map_best_effort(g_bpf.basename_policy_map_fd, BASENAME_POLICY_MAP_PIN_PATH, reused_basename);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin basename policy map to %s: %s\n",
                    BASENAME_POLICY_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: basename policy map pinned to %s\n", BASENAME_POLICY_MAP_PIN_PATH);
    }

    if (g_bpf.fs_skel->maps.deny_events != NULL) {
        int deny_fd = bpf_map__fd(g_bpf.fs_skel->maps.deny_events);
        if (deny_fd >= 0) {
            g_bpf.fs_denials = ring_buffer__new(deny_fd, __deny_event_cb, NULL, NULL);
            if (g_bpf.fs_denials == NULL) {
                VLOG_WARNING("cvd", "bpf_manager: failed to create fs deny ring buffer\n");
            }
        }
    }
    return 0;
}

static int __load_net_program(void)
{
    int status;

    g_bpf.net_skel = net_lsm_bpf__open();
    if (!g_bpf.net_skel) {
        VLOG_ERROR("cvd", "bpf_manager: failed to open net BPF skeleton\n");
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: net BPF skeleton opened\n");

    int reused_create = __reuse_pinned_map_or_unpin(
        g_bpf.net_skel->maps.net_create_map,
        NET_CREATE_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_net_create_key),
        sizeof(struct bpf_net_policy_value));
    int reused_tuple = __reuse_pinned_map_or_unpin(
        g_bpf.net_skel->maps.net_tuple_map,
        NET_TUPLE_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_net_tuple_key),
        sizeof(struct bpf_net_policy_value));
    int reused_unix = __reuse_pinned_map_or_unpin(
        g_bpf.net_skel->maps.net_unix_map,
        NET_UNIX_MAP_PIN_PATH,
        BPF_MAP_TYPE_HASH,
        sizeof(struct bpf_net_unix_key),
        sizeof(struct bpf_net_policy_value));

    status = net_lsm_bpf__load(g_bpf.net_skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to load net BPF skeleton: %d\n", status);
        net_lsm_bpf__destroy(g_bpf.net_skel);
        g_bpf.net_skel = NULL;
        return -1;
    }

    status = net_lsm_bpf__attach(g_bpf.net_skel);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to attach net BPF LSM program: %d\n", status);
        net_lsm_bpf__destroy(g_bpf.net_skel);
        g_bpf.net_skel = NULL;
        return -1;
    }

    g_bpf.net_create_map_fd = bpf_map__fd(g_bpf.net_skel->maps.net_create_map);
    g_bpf.net_tuple_map_fd = bpf_map__fd(g_bpf.net_skel->maps.net_tuple_map);
    g_bpf.net_unix_map_fd = bpf_map__fd(g_bpf.net_skel->maps.net_unix_map);
    if (g_bpf.net_create_map_fd < 0 ||
        g_bpf.net_tuple_map_fd < 0 ||
        g_bpf.net_unix_map_fd < 0) {
        VLOG_ERROR("cvd", "bpf_manager: failed to get net policy map fds\n");
        net_lsm_bpf__destroy(g_bpf.net_skel);
        g_bpf.net_skel = NULL;
        g_bpf.net_create_map_fd = -1;
        g_bpf.net_tuple_map_fd = -1;
        g_bpf.net_unix_map_fd = -1;
        return -1;
    }

    status = __pin_map_best_effort(g_bpf.net_create_map_fd, NET_CREATE_MAP_PIN_PATH, reused_create);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin net create map to %s: %s\n",
                     NET_CREATE_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: net create map pinned to %s\n", NET_CREATE_MAP_PIN_PATH);
    }

    status = __pin_map_best_effort(g_bpf.net_tuple_map_fd, NET_TUPLE_MAP_PIN_PATH, reused_tuple);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin net tuple map to %s: %s\n",
                     NET_TUPLE_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: net tuple map pinned to %s\n", NET_TUPLE_MAP_PIN_PATH);
    }

    status = __pin_map_best_effort(g_bpf.net_unix_map_fd, NET_UNIX_MAP_PIN_PATH, reused_unix);
    if (status < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to pin net unix map to %s: %s\n",
                     NET_UNIX_MAP_PIN_PATH, strerror(errno));
    } else {
        VLOG_DEBUG("cvd", "bpf_manager: net unix map pinned to %s\n", NET_UNIX_MAP_PIN_PATH);
    }

    if (g_bpf.net_skel->maps.deny_events != NULL) {
        int deny_fd = bpf_map__fd(g_bpf.net_skel->maps.deny_events);
        if (deny_fd >= 0) {
            g_bpf.net_denials = ring_buffer__new(deny_fd, __deny_event_cb, NULL, NULL);
            if (g_bpf.net_denials == NULL) {
                VLOG_WARNING("cvd", "bpf_manager: failed to create net deny ring buffer\n");
            }
        }
    }
    return 0;
}

#endif /* HAVE_BPF_SKELETON */

static void __start_denial_thread(void)
{
    if (g_bpf.fs_denials == NULL && g_bpf.net_denials == NULL) {
        return;
    }

    g_bpf.deny_thread_stop = 0;
    if (thrd_create(&g_bpf.deny_thread, __deny_event_thread, NULL) == thrd_success) {
        g_bpf.deny_thread_running = 1;
        VLOG_DEBUG("cvd", "bpf_manager: deny ring buffer logging enabled\n");
        return;
    }

    VLOG_WARNING("cvd", "bpf_manager: failed to start deny ring thread\n");
    if (g_bpf.fs_denials) {
        ring_buffer__free(g_bpf.fs_denials);
        g_bpf.fs_denials = NULL;
    }
    if (g_bpf.net_denials) {
        ring_buffer__free(g_bpf.net_denials);
        g_bpf.net_denials = NULL;
    }
}

static int __verify_net_map_writable(int map_fd, const void* key, const char* name)
{
    struct bpf_net_policy_value value = { .allow_mask = BPF_NET_CREATE };

    if (map_fd < 0 || key == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (bpf_map_update_elem(map_fd, key, &value, BPF_ANY) < 0) {
        VLOG_ERROR("cvd", "bpf_manager: net map %s not writable: %s\n", name, strerror(errno));
        return -1;
    }

    if (bpf_map_delete_elem(map_fd, key) < 0) {
        VLOG_ERROR("cvd", "bpf_manager: net map %s delete failed: %s\n", name, strerror(errno));
        return -1;
    }

    return 0;
}

static int __verify_net_maps_writable(void)
{
    struct bpf_net_create_key create_key = {0};
    struct bpf_net_tuple_key tuple_key = {0};
    struct bpf_net_unix_key unix_key = {0};

    create_key.cgroup_id = ~0ULL;
    tuple_key.cgroup_id = ~0ULL;
    unix_key.cgroup_id = ~0ULL;
    snprintf(unix_key.path, sizeof(unix_key.path), "cvd_probe");

    if (__verify_net_map_writable(g_bpf.net_create_map_fd, &create_key, "net_create_map") < 0) {
        return -1;
    }
    if (__verify_net_map_writable(g_bpf.net_tuple_map_fd, &tuple_key, "net_tuple_map") < 0) {
        return -1;
    }
    if (__verify_net_map_writable(g_bpf.net_unix_map_fd, &unix_key, "net_unix_map") < 0) {
        return -1;
    }

    return 0;
}

int containerv_bpf_initialize(void)
{
#ifndef HAVE_BPF_SKELETON
    VLOG_TRACE("cvd", "bpf_manager: BPF skeleton not available, using seccomp fallback\n");
    return 0;
#else
    int status;
    const char* env;

    VLOG_TRACE("cvd", "bpf_manager: initializing BPF manager\n");

    if (!bpf_check_lsm_available()) {
        VLOG_TRACE("cvd", "bpf_manager: BPF LSM not available, using seccomp fallback\n");
        return 0;
    }

    if (bpf_bump_memlock_rlimit() < 0) {
        VLOG_WARNING(
            "cvd",
            "bpf_manager: failed to increase memlock limit: %s\n",
            strerror(errno)
        );
    }

    if (__create_bpf_pin_directory() < 0) {
        return -1;
    }

    status = __load_fs_program();
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to load fs BPF program\n");
        return status;
    }
    
    status = __load_net_program();
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to load net BPF program\n");
        return status;
    }

    status = __verify_net_maps_writable();
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: net policy maps are not writable\n");
        __cleanup_fs_program();
        __cleanup_net_program();
        if (g_bpf.fs_denials) {
            ring_buffer__free(g_bpf.fs_denials);
            g_bpf.fs_denials = NULL;
        }
        if (g_bpf.net_denials) {
            ring_buffer__free(g_bpf.net_denials);
            g_bpf.net_denials = NULL;
        }
        if (g_bpf.fs_skel) {
            fs_lsm_bpf__destroy(g_bpf.fs_skel);
            g_bpf.fs_skel = NULL;
        }
        if (g_bpf.net_skel) {
            net_lsm_bpf__destroy(g_bpf.net_skel);
            g_bpf.net_skel = NULL;
        }
        g_bpf.policy_map_fd = -1;
        g_bpf.dir_policy_map_fd = -1;
        g_bpf.basename_policy_map_fd = -1;
        g_bpf.net_create_map_fd = -1;
        g_bpf.net_tuple_map_fd = -1;
        g_bpf.net_unix_map_fd = -1;
        return status;
    }
    
    __start_denial_thread();

    g_bpf.status = CV_BPF_AVAILABLE;
    VLOG_TRACE("cvd", "bpf_manager: initialization complete, BPF LSM enforcement active\n");
    return 0;
#endif // HAVE_BPF_SKELETON
}

void __stop_denial_thread(void)
{
    if (g_bpf.deny_thread_running == 0) {
        return;
    }

    g_bpf.deny_thread_stop = 1;
    (void)thrd_join(g_bpf.deny_thread, NULL);
    g_bpf.deny_thread_running = 0;
}

void __cleanup_fs_program(void)
{
    if (g_bpf.fs_skel == NULL) {
        return;
    }

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
}

void __cleanup_net_program(void)
{
    if (g_bpf.net_skel == NULL) {
        return;
    }

    if (unlink(NET_CREATE_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin net create map: %s\n",
                     strerror(errno));
    }

    if (unlink(NET_TUPLE_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin net tuple map: %s\n",
                     strerror(errno));
    }

    if (unlink(NET_UNIX_MAP_PIN_PATH) < 0 && errno != ENOENT) {
        VLOG_WARNING("cvd", "bpf_manager: failed to unpin net unix map: %s\n",
                     strerror(errno));
    }
}

void containerv_bpf_shutdown(void)
{
    if (g_bpf.status != CV_BPF_AVAILABLE) {
        return;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: shutting down BPF manager\n");
    
    // Clean up all entry trackers
    list_destroy(&g_bpf.trackers, (void(*)(void*))bpf_container_context_delete);
    
    // Cleanup programs
    __cleanup_fs_program();
    __cleanup_net_program();
    
    __stop_denial_thread();
    if (g_bpf.fs_denials) {
        ring_buffer__free(g_bpf.fs_denials);
        g_bpf.fs_denials = NULL;
    }
    if (g_bpf.net_denials) {
        ring_buffer__free(g_bpf.net_denials);
        g_bpf.net_denials = NULL;
    }

#ifdef HAVE_BPF_SKELETON
    if (g_bpf.fs_skel) {
        fs_lsm_bpf__destroy(g_bpf.fs_skel);
        g_bpf.fs_skel = NULL;
    }
    if (g_bpf.net_skel) {
        net_lsm_bpf__destroy(g_bpf.net_skel);
        g_bpf.net_skel = NULL;
    }
#endif
    
    g_bpf.policy_map_fd = -1;
    g_bpf.dir_policy_map_fd = -1;
    g_bpf.basename_policy_map_fd = -1;
    g_bpf.net_create_map_fd = -1;
    g_bpf.net_tuple_map_fd = -1;
    g_bpf.net_unix_map_fd = -1;
    g_bpf.status = CV_BPF_UNINITIALIZED;
    
    VLOG_TRACE("cvd", "bpf_manager: shutdown complete\n");
}

enum containerv_bpf_status containerv_bpf_is_available(void)
{
    return g_bpf.status;
}

int containerv_bpf_get_policy_map_fd(void)
{
    return g_bpf.policy_map_fd;
}

static void __map_context_from_container_context(
    struct bpf_container_context* containerContext,
    struct bpf_map_context*       mapContext)
{
    mapContext->map_fd = g_bpf.policy_map_fd;
    mapContext->dir_map_fd = g_bpf.dir_policy_map_fd;
    mapContext->basename_map_fd = g_bpf.basename_policy_map_fd;
    mapContext->net_create_map_fd = g_bpf.net_create_map_fd;
    mapContext->net_tuple_map_fd = g_bpf.net_tuple_map_fd;
    mapContext->net_unix_map_fd = g_bpf.net_unix_map_fd;
    mapContext->cgroup_id = containerContext->cgroup_id;
}

int containerv_bpf_populate_policy(
    const char*               containerId,
    const char*               rootfsPath,
    struct containerv_policy* policy)
{
    struct bpf_container_context* containerContext = NULL;
    struct bpf_map_context        mapContext;
    unsigned long long            startTime, endTime;
    int                           fsEnabled, netEnabled;
    
    if (g_bpf.status != CV_BPF_AVAILABLE) {
        VLOG_DEBUG("cvd", "bpf_manager: BPF not available, skipping policy population\n");
        return 0;
    }
    
    if (containerId == NULL || rootfsPath == NULL || policy == NULL) {
        errno = EINVAL;
        g_bpf.metrics.failed_populate_ops++;
        return -1;
    }
    
    fsEnabled = (g_bpf.policy_map_fd >= 0);
    netEnabled = (g_bpf.net_create_map_fd >= 0 ||
                       g_bpf.net_tuple_map_fd >= 0 ||
                       g_bpf.net_unix_map_fd >= 0);

    if (!fsEnabled && !netEnabled) {
        VLOG_DEBUG("cvd", "bpf_manager: no active BPF LSM programs for container %s\n", containerId);
        return 0;
    }
    
    // Defensive bounds check to prevent out-of-bounds reads
    if (fsEnabled && policy->path_count > MAX_PATHS) {
        VLOG_ERROR("cvd", "bpf_manager: policy path_count (%d) exceeds MAX_PATHS (%d)\n",
                   policy->path_count, MAX_PATHS);
        errno = EINVAL;
        g_bpf.metrics.failed_populate_ops++;
        return -1;
    }
    
    // Create or find entry tracker for this container
    containerContext = __container_context_lookup(containerId);
    if (!containerContext) {
        unsigned long long cgroupId;
        
        cgroupId = bpf_get_cgroup_id(containerId);
        if (cgroupId == 0) {
            VLOG_ERROR("cvd", "bpf_manager: failed to resolve cgroup ID for %s\n", containerId);
            g_bpf.metrics.failed_populate_ops++;
            return -1;
        }
        
        containerContext = bpf_container_context_new(containerId, cgroupId);
        if (!containerContext) {
            VLOG_ERROR("cvd", "bpf_manager: failed to create entry tracker for %s\n", containerId);
            g_bpf.metrics.failed_populate_ops++;
            return -1;
        }
        list_add(&g_bpf.trackers, &containerContext->header);
    }

    VLOG_DEBUG("cvd",
        "bpf_manager: populating policy for container %s (cgroup_id=%llu)\n",
        containerId, containerContext->cgroup_id
    );

    // Start timing
    startTime = __get_time_microseconds();
    
    __map_context_from_container_context(containerContext, &mapContext);
    if (fsEnabled) {
        VLOG_DEBUG("cvd", "bpf_manager: %u paths configured for container %s\n", policy->path_count, containerId);
        bpf_container_context_apply_paths(containerContext, policy, &mapContext, rootfsPath);
    }

    if (netEnabled) {
        VLOG_DEBUG("cvd", "bpf_manager: %u network rules configured for container %s\n", policy->net_rule_count, containerId);
        bpf_container_context_apply_net(containerContext, policy, &mapContext, rootfsPath); 
    }
    
    // End timing and update metrics
    endTime = __get_time_microseconds();
    
    containerContext->metrics_time.policy_population_time_us = endTime - startTime;
    g_bpf.metrics.total_populate_ops++;
    
    VLOG_DEBUG(
        "cvd",
        "bpf_manager: populated policy entries for container %s in %llu us\n",
        containerId,
        containerContext->metrics_time.policy_population_time_us
    );
    return 0;
}

int containerv_bpf_cleanup_policy(const char* containerId)
{
    struct bpf_container_context* containerContext;
    struct bpf_map_context        mapContext;
    unsigned long long            startTime, endTime;
    int                           status;
    
    if (g_bpf.status != CV_BPF_AVAILABLE) {
        return 0;
    }
    
    if (containerId == NULL) {
        errno = EINVAL;
        g_bpf.metrics.failed_cleanup_ops++;
        return -1;
    }
    
    VLOG_DEBUG("cvd", "bpf_manager: cleaning up policy for container %s\n", containerId);
    
    // Start timing
    startTime = __get_time_microseconds();
    
    // Find the entry tracker for this container
    containerContext = __container_context_lookup(containerId);
    if (!containerContext) {
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
                   containerId);
        return 0;
    }
    
    // Cleanup the container context policies
    __map_context_from_container_context(containerContext, &mapContext);
    status = bpf_container_context_cleanup(containerContext, &mapContext);
    if (status) {
        VLOG_ERROR("cvd", "bpf_manager: failed to clean up policy for %s: %d\n",
                   containerId, status);
        g_bpf.metrics.failed_cleanup_ops++;
        __container_context_remove(containerId);
        return -1;
    }
    
    // End timing and update metrics
    endTime = __get_time_microseconds();
    containerContext->metrics_time.policy_cleanup_time_us = endTime - startTime;
    g_bpf.metrics.total_cleanup_ops++;
    
    VLOG_DEBUG(
        "cvd",
        "bpf_manager: deleted policy entries for container %s in %llu us\n",
        containerId,
        containerContext->metrics_time.policy_cleanup_time_us
    );
    
    __container_context_remove(containerId);
    return status;
}

static int __count_total_entries(void)
{
    struct list_item* i;
    int               total = 0;
    list_foreach(&g_bpf.trackers, i) {
        struct bpf_container_context* context = (struct bpf_container_context*)i;
        total += context->file.file_key_count;
        total += context->file.dir_key_count;
        total += context->file.basename_key_count;
        total += context->net.create_key_count;
        total += context->net.tuple_key_count;
        total += context->net.unix_key_count;
    }
    return total;
}

int containerv_bpf_get_metrics(struct containerv_bpf_metrics* metrics)
{
    if (metrics == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (g_bpf.status != CV_BPF_AVAILABLE) {
        memset(metrics, 0, sizeof(struct containerv_bpf_metrics));
        metrics->status = g_bpf.status;
        return 0;
    }
    
    metrics->status = g_bpf.status;
    metrics->container_count = g_bpf.trackers.count;
    metrics->policy_entry_count = __count_total_entries();
    metrics->max_map_capacity = MAX_TRACKED_ENTRIES;
    metrics->total_populate_ops = g_bpf.metrics.total_populate_ops;
    metrics->total_cleanup_ops = g_bpf.metrics.total_cleanup_ops;
    metrics->failed_populate_ops = g_bpf.metrics.failed_populate_ops;
    metrics->failed_cleanup_ops = g_bpf.metrics.failed_cleanup_ops;
    
    return 0;
}

int containerv_bpf_get_container_metrics(
    const char*                              containerId,
    struct containerv_bpf_container_metrics* metrics)
{
    struct bpf_container_context* context;

    if (g_bpf.status != CV_BPF_AVAILABLE) {
        return 0;
    }
    
    if (containerId == NULL || metrics == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Find the entry tracker for this container
    context = __container_context_lookup(containerId);
    if (context == NULL) {
        errno = ENOENT;
        return -1;
    }

    metrics->cgroup_id = context->cgroup_id;
    
    memcpy(
        &metrics->time_metrics,
        &context->metrics_time,
        sizeof(struct containerv_bpf_container_time_metrics)
    );
    
    return 0;
}
