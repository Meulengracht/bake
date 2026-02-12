/**
 * Copyright, Philip Meulengracht
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

#ifndef __LSM_DENY_H__
#define __LSM_DENY_H__

#include <bpf/bpf_helpers.h>

#ifndef LSM_DENY_NAME_MAX
#define LSM_DENY_NAME_MAX 64
#endif

enum deny_hook_id {
    DENY_HOOK_FILE_OPEN = 1,
    DENY_HOOK_BPRM_CHECK = 2,
    DENY_HOOK_INODE_CREATE = 3,
    DENY_HOOK_INODE_MKDIR = 4,
    DENY_HOOK_INODE_MKNOD = 5,
    DENY_HOOK_INODE_UNLINK = 6,
    DENY_HOOK_INODE_RMDIR = 7,
    DENY_HOOK_INODE_RENAME = 8,
    DENY_HOOK_INODE_LINK = 9,
    DENY_HOOK_INODE_SYMLINK = 10,
    DENY_HOOK_INODE_SETATTR = 11,
    DENY_HOOK_PATH_TRUNCATE = 12,
    DENY_HOOK_SOCKET_CREATE = 20,
    DENY_HOOK_SOCKET_BIND = 21,
    DENY_HOOK_SOCKET_CONNECT = 22,
    DENY_HOOK_SOCKET_LISTEN = 23,
    DENY_HOOK_SOCKET_ACCEPT = 24,
    DENY_HOOK_SOCKET_SENDMSG = 25,
};

struct deny_event {
    __u64 cgroup_id;
    __u64 dev;
    __u64 ino;
    __u32 required_mask;
    __u32 hook_id;
    __u32 name_len;
    char  comm[16];
    char  name[LSM_DENY_NAME_MAX];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} deny_events SEC(".maps");

static __always_inline void __emit_deny_event_basic(__u64 cgroup_id, __u32 required, __u32 hook_id)
{
    struct deny_event* ev;

    ev = bpf_ringbuf_reserve(&deny_events, sizeof(*ev), 0);
    if (!ev) {
        return;
    }

    ev->cgroup_id = cgroup_id;
    ev->dev = 0;
    ev->ino = 0;
    ev->required_mask = required;
    ev->hook_id = hook_id;
    ev->name_len = 0;
    bpf_get_current_comm(&ev->comm, sizeof(ev->comm));
    ev->name[0] = 0;

    bpf_ringbuf_submit(ev, 0);
}

#endif /* __LSM_DENY_H__ */
