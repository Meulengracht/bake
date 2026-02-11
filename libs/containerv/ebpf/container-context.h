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

#ifndef __BPF_CONTAINER_CONTEXT_H__
#define __BPF_CONTAINER_CONTEXT_H__

#include <chef/containerv/bpf.h>
#include <chef/list.h>

#include "private.h"

struct bpf_map_context;

struct __bpf_container_file {
    struct bpf_policy_key* file_keys;
    int                    file_key_count;
    int                    file_key_capacity;
    
    struct bpf_policy_key* dir_keys;
    int                    dir_key_count;
    int                    dir_key_capacity;
    
    struct bpf_policy_key* basename_keys;
    int                    basename_key_count;
    int                    basename_key_capacity;
};

struct __bpf_container_net {
    // Creation-based network policies (socket type/protocol based)
    struct bpf_net_create_key* create_keys;
    int                        create_key_count;
    int                        create_key_capacity;

    // Tuple-based network policies (for non-UNIX sockets)
    struct bpf_net_tuple_key*  tuple_keys;
    int                        tuple_key_count;
    int                        tuple_key_capacity;
    
    // Unix socket policies are tracked separately since they are
    // path-based.
    struct bpf_net_unix_key*   unix_keys;
    int                        unix_key_count;
    int                        unix_key_capacity;
};

struct bpf_container_context {
    struct list_item               header;
    char*                          container_id;
    unsigned long long             cgroup_id;

    // policy supported features
    struct __bpf_container_file    file;
    struct __bpf_container_net     net;

    // metrics for this container
    struct containerv_bpf_container_time_metrics metrics_time;
};

extern struct bpf_container_context* bpf_container_context_new(
    const char*        containerId,
    unsigned long long cgroupId);


extern void bpf_container_context_delete(struct bpf_container_context* context);

extern int bpf_container_context_add_tracked_file_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino);
extern int bpf_container_context_add_tracked_dir_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino);
extern int bpf_container_context_add_tracked_basename_entry(
    struct bpf_container_context* context,
    dev_t                         dev,
    ino_t                         ino);

extern int bpf_container_context_add_tracked_net_create_entry(
    struct bpf_container_context*    context,
    const struct bpf_net_create_key* key);

extern int bpf_container_context_add_tracked_net_tuple_entry(
    struct bpf_container_context*   context,
    const struct bpf_net_tuple_key* key);

extern int bpf_container_context_add_tracked_net_unix_entry(
    struct bpf_container_context*  context,
    const struct bpf_net_unix_key* key);

extern void bpf_container_context_apply_paths(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath);

extern void bpf_container_context_apply_net(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath);

extern int bpf_container_context_cleanup(
    struct bpf_container_context* containerContext,
    struct bpf_map_context*       mapContext);

#endif // !__BPF_CONTAINER_CONTEXT_H__
