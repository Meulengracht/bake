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

#ifndef __BPF_INODE_KEY_H__
#define __BPF_INODE_KEY_H__

#include <vmlinux.h>

#include "common.h"

/* Policy key: (cgroup_id, dev, ino) */
struct policy_key {
    __u64 cgroup_id;
    __u64 dev;
    __u64 ino;
};

static __always_inline int __populate_key(struct policy_key* key, struct file *file, __u64 cgroupId)
{
    struct inode*       inode;
    struct super_block* sb;
    __u64               dev;
    __u64               ino;

    // Extract inode identity (dev, ino) for policy lookup
    inode = NULL;
    CORE_READ_INTO(&inode, file, f_inode);
    if (!inode) {
        struct dentry *dentry = NULL;
        CORE_READ_INTO(&dentry, file, f_path.dentry);
        if (dentry) {
            CORE_READ_INTO(&inode, dentry, d_inode);
        }
    }
    if (!inode) {
        return -EACCES;
    }

    sb = NULL;
    CORE_READ_INTO(&sb, inode, i_sb);
    if (!sb) {
        return -EACCES;
    }

    {
        dev_t devt = 0;
        unsigned long inode_no = 0;

        CORE_READ_INTO(&devt, sb, s_dev);
        CORE_READ_INTO(&inode_no, inode, i_ino);

        dev = (__u64)devt;
        ino = (__u64)inode_no;
    }

    key->cgroup_id = cgroupId;
    key->dev = dev;
    key->ino = ino;
    return 0;
}

#endif /* __BPF_INODE_KEY_H__ */
