/**
 * BPF LSM program for filesystem access enforcement
 * 
 * This program hooks file_open LSM hook to enforce container-specific
 * filesystem read/write restrictions by inode/device identity.
 * 
 * Copyright 2024, Philip Meulengracht
 * Licensed under GPLv3
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* Permission bits */
#define PERM_READ  0x1
#define PERM_WRITE 0x2
#define PERM_EXEC  0x4

/* File open flags - matching O_ACCMODE */
#define O_RDONLY   00000000
#define O_WRONLY   00000001
#define O_RDWR     00000002

/* Policy key: (cgroup_id, dev, ino) */
struct policy_key {
    __u64 cgroup_id;
    __u64 dev;
    __u64 ino;
};

/* Policy value: permission mask (bit flags for deny) */
struct policy_value {
    __u32 deny_mask;  /* Bitmask of denied permissions */
};

/* BPF map: policy enforcement map */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct policy_value);
    __uint(max_entries, 10240);
} policy_map SEC(".maps");

/* Helper to get current cgroup ID */
static __always_inline __u64 get_current_cgroup_id(void)
{
    return bpf_get_current_cgroup_id();
}

/**
 * LSM hook for file_open
 * 
 * Called when a file is opened. We check if the operation (read/write)
 * should be denied based on the container's policy.
 * 
 * Note: This is a foundational implementation. The BPF_PROG macro handles
 * the LSM hook signature. Full enforcement requires kernel structures
 * that are not yet accessible without vmlinux.h generation.
 * 
 * The 'ret' parameter is part of the LSM hook mechanism for checking
 * the result of previous security checks before adding additional enforcement.
 * 
 * Return: 0 to allow, negative error code to deny
 */
SEC("lsm/file_open")
int BPF_PROG(file_open_restrict, struct file *file, int ret)
{
    __u64 cgroup_id;
    
    /* Only enforce on successful opens (ret == 0) */
    if (ret != 0) {
        return 0;
    }
    
    /* Get current cgroup ID */
    cgroup_id = get_current_cgroup_id();
    if (cgroup_id == 0) {
        /* Not in a cgroup, allow */
        return 0;
    }
    
    /* TODO: Once vmlinux.h is available:
     * 1. Extract inode from file->f_inode
     * 2. Get dev from inode->i_sb->s_dev
     * 3. Get ino from inode->i_ino
     * 4. Look up policy in policy_map
     * 5. Check file->f_flags against deny_mask
     * 6. Return -EACCES if denied
     * 
     * For now, this is a placeholder that always allows.
     * The infrastructure is in place for when vmlinux.h is generated.
     */
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
