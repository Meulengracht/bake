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

#ifndef __MAP_OPS_PRIVATE_H__
#define __MAP_OPS_PRIVATE_H__

#include <sys/types.h>

#include "private.h"

struct bpf_map_context {
    unsigned long long cgroup_id;
    int                map_fd;
    int                dir_map_fd;
    int                basename_map_fd;
    int                net_create_map_fd;
    int                net_tuple_map_fd;
    int                net_unix_map_fd;
};

/**
 * @brief Add an inode to the BPF policy map with specified permissions
 * @param context BPF policy context
 * @param dev Device number
 * @param ino Inode number
 * @param allowMask Permission mask
 * @return 0 on success, -1 on error
 */
extern int bpf_policy_map_allow_inode(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino,
    unsigned int            allowMask
);

/**
 * @brief Allow a directory via the directory policy map
 * @param context BPF policy context
 * @param dev Device number of the directory inode
 * @param ino Inode number of the directory inode
 * @param allowMask Permission mask
 * @param flags Directory rule flags (BPF_DIR_RULE_*)
 * @return 0 on success, -1 on error
 */
extern int bpf_dir_policy_map_allow_dir(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino,
    unsigned int            allowMask,
    unsigned int            flags
);

/**
 * @brief Allow a basename pattern under a directory inode
 * @param context BPF policy context
 * @param dev Directory device number
 * @param ino Directory inode number
 * @param rule Basename rule to add (type/prefix/tail)
 * @return 0 on success, -1 on error
 */
extern int bpf_basename_policy_map_allow_rule(
    struct bpf_map_context*         context,
    dev_t                           dev,
    ino_t                           ino,
    const struct bpf_basename_rule* rule
);

/**
 * @brief Delete an entry from the BPF policy map
 * @param context BPF policy context
 * @param dev Device number
 * @param ino Inode number
 * @return 0 on success, -1 on error
 */
extern int bpf_policy_map_delete_entry(
    struct bpf_map_context* context,
    dev_t                   dev,
    ino_t                   ino
);

extern int bpf_net_create_map_allow(
    struct bpf_map_context*          context,
    const struct bpf_net_create_key* key,
    unsigned int                     allowMask
);

extern int bpf_net_tuple_map_allow(
    struct bpf_map_context*         context,
    const struct bpf_net_tuple_key* key,
    unsigned int                    allowMask
);

extern int bpf_net_unix_map_allow(
    struct bpf_map_context*        context,
    const struct bpf_net_unix_key* key,
    unsigned int                   allowMask
);

extern int bpf_map_delete_batch_by_fd(
    int                     mapFd,
    void*                   keys,
    int                     count,
    size_t                  keySize
);

#endif //!__MAP_OPS_PRIVATE_H__
