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
 * https://github.com/torvalds/linux/blob/master/include/linux/lsm_hook_defs.h
 */

#include <vmlinux.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "common.h"
#include "tracing.h"

#include <protecc/bpf.h>

/* Permission bits */
#define PERM_READ  0x1
#define PERM_WRITE 0x2
#define PERM_EXEC  0x4

/* File open flags - matching O_ACCMODE */
#define O_RDONLY   00000000
#define O_WRONLY   00000001
#define O_RDWR     00000002

#ifndef PROTECC_PROFILE_MAP_MAX_ENTRIES
#define PROTECC_PROFILE_MAP_MAX_ENTRIES 1024u
#endif

struct profile_value {
    __u32 size;
    __u8  data[PROTECC_BPF_MAX_PROFILE_SIZE];
};

struct per_cpu_data {
    char path[PATH_BUFFER_SIZE];
};

/**
 * @brief BPF map: protecc profiles per cgroup
 * The key is cgroup_id, value is a serialized protecc profile blob.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u64);
    __type(value, struct profile_value);
    __uint(max_entries, PROTECC_PROFILE_MAP_MAX_ENTRIES);
} profile_map SEC(".maps");

/* Per-CPU scratch buffer to avoid large stack allocations. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);
    __type(value, struct per_cpu_data);
    __uint(max_entries, 1);
} per_cpu_data_map SEC(".maps");

static __always_inline void __emit_deny_event_dentry(struct dentry* dentry, __u32 required, __u32 hookId)
{
    struct deny_event* ev;
    __u32              name_len = 0;

    ev = bpf_ringbuf_reserve(&deny_events, sizeof(*ev), 0);
    if (!ev) {
        return;
    }
    
    __populate_event_from_dentry(ev, dentry);
    ev->cgroup_id = get_current_cgroup_id();
    ev->required_mask = required;
    ev->hook_id = hookId;
    bpf_get_current_comm(&ev->comm, sizeof(ev->comm));
    bpf_ringbuf_submit(ev, 0);
}

static __always_inline struct per_cpu_data* __cpu_data(void)
{
    __u32                key = 0;
    struct per_cpu_data* scratch = bpf_map_lookup_elem(&per_cpu_data_map, &key);
    if (!scratch) {
        return NULL;
    }
    return scratch;
}

static int __check_profile_match(
    struct dentry* dentry,
    __u64          cgroupId,
    __u32          required,
    __u32          hookId)
{
    struct profile_value* profile = NULL;
    struct per_cpu_data*  scratch = NULL;
    __u32                 pathLength = 0;
    __u32                 pathStart = 0;
    bool                  match;

    profile = bpf_map_lookup_elem(&profile_map, &cgroupId);
    if (profile == NULL) {
        return 0;
    }

    if (profile->size == 0 || profile->size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        __emit_deny_event_dentry(dentry, required, hookId);
        return -EACCES;
    }

    scratch = __cpu_data();
    if (scratch == NULL) {
        __emit_deny_event_dentry(dentry, required, hookId);
        return -EACCES;
    }

    pathLength = __resolve_dentry_path(scratch->path, dentry, &pathStart);
    match = protecc_bpf_match(profile->data, (const __u8*)&scratch->path[0], pathStart, pathLength);
    if (!match) {
        __emit_deny_event_dentry(dentry, required, hookId);
        return -EACCES;
    }
    return 0;
}

static __always_inline int __check_access_file(struct file* file, __u32 required, __u32 hookId)
{
    __u64          cgroupId;
    struct dentry* dentry = NULL;

    cgroupId = get_current_cgroup_id();
    if (cgroupId == 0) {
        return 0;
    }

    CORE_READ_INTO(&dentry, file, f_path.dentry);
    if (dentry == NULL) {
        return -EACCES;
    }
    return __check_profile_match(dentry, cgroupId, required, hookId);
}

static __always_inline int __check_access_dentry(struct dentry* dentry, __u32 required, __u32 hookId)
{
    __u64 cgroupId;

    cgroupId = get_current_cgroup_id();
    if (cgroupId == 0) {
        return 0;
    }
    return __check_profile_match(dentry, cgroupId, required, hookId);
}

/**
 * LSM hook for file_open
 * 
 * Called when a file is opened. We check if the operation (read/write)
 * should be allowed based on the container's policy.
 * 
 * The 'ret' parameter is part of the LSM hook mechanism for checking
 * the result of previous security checks before adding additional enforcement.
 * 
 * Return: 0 to allow, negative error code to deny
 */
SEC("lsm/file_open")
int BPF_PROG(file_open_restrict, struct file *file, int ret)
{
    __u64 flags64 = 0;
    __u32 required = 0;
    __u32 accmode;
    
    // If previous checks failed, propagate the error
    if (ret != 0) {
        return ret;
    }
    
    // Determine required permissions from open flags
    CORE_READ_INTO(&flags64, file, f_flags);
    accmode = (__u32)flags64 & 00000003; /* O_ACCMODE */

    if (accmode == O_WRONLY) {
        required = PERM_WRITE;
    } else if (accmode == O_RDWR) {
        required = (PERM_READ | PERM_WRITE);
    } else {
        required = PERM_READ;
    }

    return __check_access_file(file, required, DENY_HOOK_FILE_OPEN);
}

SEC("lsm/bprm_check_security")
int BPF_PROG(bprm_check_security_restrict, struct linux_binprm *bprm, int ret)
{
    struct file* file = NULL;

    if (ret) {
        return ret;
    }

    CORE_READ_INTO(&file, bprm, file);
    if (!file) {
        return -EACCES;
    }

    return __check_access_file(file, PERM_EXEC, DENY_HOOK_BPRM_CHECK);
}

SEC("lsm/inode_create")
int BPF_PROG(inode_create_restrict, struct inode *dir, struct dentry *dentry, umode_t mode, int ret)
{
    (void)dir;
    (void)mode;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_CREATE);
}

SEC("lsm/inode_mkdir")
int BPF_PROG(inode_mkdir_restrict, struct inode *dir, struct dentry *dentry, umode_t mode, int ret)
{
    (void)dir;
    (void)mode;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_MKDIR);
}

SEC("lsm/inode_mknod")
int BPF_PROG(inode_mknod_restrict, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev, int ret)
{
    (void)dir;
    (void)mode;
    (void)dev;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_MKNOD);
}

SEC("lsm/inode_unlink")
int BPF_PROG(inode_unlink_restrict, struct inode *dir, struct dentry *dentry, int ret)
{
    (void)dir;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_UNLINK);
}

SEC("lsm/inode_rmdir")
int BPF_PROG(inode_rmdir_restrict, struct inode *dir, struct dentry *dentry, int ret)
{
    (void)dir;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_RMDIR);
}

SEC("lsm/inode_rename")
int BPF_PROG(inode_rename_restrict, 
    struct inode *old_dir, struct dentry *old_dentry,
    struct inode *new_dir, struct dentry *new_dentry,
    int ret)
{
    (void)old_dir;
    (void)new_dir;
    if (ret) {
        return ret;
    }
    if (!old_dentry || !new_dentry) {
        return -EACCES;
    }

    if (__check_access_dentry(old_dentry, PERM_WRITE, DENY_HOOK_INODE_RENAME)) {
        return -EACCES;
    }
    if (__check_access_dentry(new_dentry, PERM_WRITE, DENY_HOOK_INODE_RENAME)) {
        return -EACCES;
    }
    return 0;
}

SEC("lsm/inode_link")
int BPF_PROG(inode_link_restrict, struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry, int ret)
{
    (void)old_dentry;
    (void)dir;
    if (ret) {
        return ret;
    }
    if (!new_dentry) {
        return -EACCES;
    }
    return __check_access_dentry(new_dentry, PERM_WRITE, DENY_HOOK_INODE_LINK);
}

SEC("lsm/inode_symlink")
int BPF_PROG(inode_symlink_restrict, struct inode *dir, struct dentry *dentry, const char *old_name, int ret)
{
    (void)dir;
    (void)old_name;
    if (ret) {
        return ret;
    }
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_SYMLINK);
}

SEC("lsm/inode_setattr")
int BPF_PROG(inode_setattr_restrict, struct dentry *dentry, struct iattr *attr)
{
    (void)attr;
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_INODE_SETATTR);
}

SEC("lsm/path_truncate")
int BPF_PROG(path_truncate_restrict, struct path *path, int ret)
{
    struct dentry* dentry = NULL;
    if (ret) {
        return ret;
    }

    CORE_READ_INTO(&dentry, path, dentry);
    if (!dentry) {
        return -EACCES;
    }
    return __check_access_dentry(dentry, PERM_WRITE, DENY_HOOK_PATH_TRUNCATE);
}

char LICENSE[] SEC("license") = "GPL";
