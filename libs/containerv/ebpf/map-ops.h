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
    int                profile_map_fd;
    int                net_create_map_fd;
    int                net_tuple_map_fd;
    int                net_unix_map_fd;
};

/**
 * @brief Set's a new protecc profile for the container cgroup
 * @param context BPF profile context
 * @param profile Serialized protecc profile blob
 * @param profileSize Size of the profile blob
 * @return 0 on success, -1 on error
 */
extern int bpf_profile_map_set_profile(
    struct bpf_map_context* context,
    uint8_t*                profile,
    size_t                  profileSize);

/**
 * @brief Deletes the protecc profile associated with the group_id in the context
 * @param context BPF profile context
 * @return 0 on success, -1 on error
 */
extern int bpf_profile_map_clear_profile(
    struct bpf_map_context* context);

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
