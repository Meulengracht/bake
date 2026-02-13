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

#ifndef __BPF_PROGRAM_COMMON_H__
#define __BPF_PROGRAM_COMMON_H__

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

#ifndef EACCES
#define EACCES 13
#endif

#ifndef CORE_READ_INTO
#define CORE_READ_INTO(dst, src, field) \
    bpf_core_read((dst), sizeof(*(dst)), &((src)->field))
#endif

static __always_inline __u64 get_current_cgroup_id(void)
{
    return bpf_get_current_cgroup_id();
}

/**
 * @brief Helper to get full path; uses new kfunc on >=6.8 or falls back 
 * Supported by LSM programs: https://docs.ebpf.io/linux/kfuncs/bpf_path_d_path/
 * */
static __always_inline int __resolve_file_path(struct file *f, char *buf, int len) {
#ifdef bpf_path_d_path
    // kernel >= 6.8
    return bpf_path_d_path(&f->f_path, buf, len);
#else
    // Automatic fallback to the older kernel
    return bpf_d_path(&f->f_path, buf, len);
#endif
}

#define PATH_MAX_DEPTH   32
#define PATH_BUFFER_SIZE 1024
#define PATH_NAME_MAX    127

// get_dentry_inode - Returns the inode structure designated by the provided dentry
static __always_inline struct inode *get_dentry_inode(struct dentry *dentry)
{
    struct inode* inode;
    CORE_READ_INTO(&inode, dentry, d_inode);
    return inode;
}

/**
 * @brief Resolves the full path of a dentry into the provided buffer, using a loop to traverse up the dentry tree.
 * The function also uses a cache to avoid redundant path resolution for inodes we've already seen.
 */
static __always_inline u32 __resolve_dentry_path(char* buffer, struct dentry* dentry, __u32* pathStart)
{
    struct qstr    qstr;
    struct dentry* dparent;
    __u32          copied = 0;
    __u32          pathLength = 0;
    int            i;

    // NULL terminate the buffer to be safe
    buffer[PATH_BUFFER_SIZE - 1] = '\0';

    bpf_for (i, 0, PATH_MAX_DEPTH) {
        __u32 ti = PATH_BUFFER_SIZE - pathLength - 1;

        // Exit if the path buffer is already full
        if (pathLength >= (PATH_BUFFER_SIZE - 1)) {
            *pathStart = PATH_BUFFER_SIZE - pathLength;
            return pathLength;
        }

        CORE_READ_INTO(&qstr, dentry, d_name);
        CORE_READ_INTO(&dparent, dentry, d_parent);

        //  & (PATH_BUFFER_SIZE - PATH_NAME_MAX - 1) is required by the verifier. It ensures that we will never start copying a path
        // that could be as big as PATH_NAME_MAX at an index that is in the last PATH_NAME_MAX positions in the buffer.
        copied = bpf_core_read_str(
            &buffer[ti & (PATH_BUFFER_SIZE - PATH_NAME_MAX - 1)], 
            PATH_NAME_MAX, 
            (void *)qstr.name
        );
        
        // & PATH_NAME_MAX is required by the verifier. It ensures that we will always add to the cursor a positive value,
        // that is below PATH_NAME_MAX (the maximum theoretical value, see the previous line).
        pathLength += (copied & PATH_NAME_MAX);

        if (get_dentry_inode(dentry) == get_dentry_inode(dparent)) {
            *pathStart = PATH_BUFFER_SIZE - pathLength;
            return pathLength;
        }
        dentry = dparent;
    }
    *pathStart = PATH_BUFFER_SIZE - pathLength;
    return pathLength;
}

#endif // !__BPF_PROGRAM_COMMON_H__
