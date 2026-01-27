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

 #include <vmlinux.h>

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#ifndef EACCES
#define EACCES 13
#endif

#ifndef CORE_READ_INTO
#define CORE_READ_INTO(dst, src, field) \
    bpf_core_read((dst), sizeof(*(dst)), &((src)->field))
#endif

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
    __u32 allow_mask;  /* Bitmask of allowed permissions */
};

/* Directory policy flags (must match userspace) */
#define DIR_RULE_CHILDREN_ONLY 0x1
#define DIR_RULE_RECURSIVE     0x2

struct dir_policy_value {
    __u32 allow_mask;
    __u32 flags;
};

/* Basename rules: limited patterns to avoid full path parsing in BPF */
#define BASENAME_RULE_MAX 8
#define BASENAME_MAX_STR  32

// Exact: foo
// Prefix: foo*
// Digits: loop[0-9]+, nvme[0-9]p* (single digit if no +, one-or-more digits if +)
enum basename_rule_type {
    BASENAME_RULE_EMPTY  = 0,
    BASENAME_RULE_EXACT  = 1,
    BASENAME_RULE_PREFIX = 2,
    BASENAME_RULE_DIGITS = 3,
};

struct basename_rule {
    __u32 allow_mask;
    __u8  type;
    __u8  digits_max;      /* 1 = exactly one digit, 0 = one-or-more digits */
    __u8  prefix_len;
    __u8  tail_len;
    __u8  tail_wildcard;   /* if set, tail only needs to be a prefix */
    __u8  _pad[3];
    char  prefix[BASENAME_MAX_STR];
    char  tail[BASENAME_MAX_STR];
};

struct basename_policy_value {
    struct basename_rule rules[BASENAME_RULE_MAX];
};

/* BPF map: policy enforcement map */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct policy_value);
    __uint(max_entries, 10240);
} policy_map SEC(".maps");

/* Directory policy map: rules keyed by directory inode (dev,ino) + cgroup */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct dir_policy_value);
    __uint(max_entries, 10240);
} dir_policy_map SEC(".maps");

/* Basename policy map: rules keyed by parent directory inode (dev,ino) + cgroup */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct basename_policy_value);
    __uint(max_entries, 10240);
} basename_policy_map SEC(".maps");

static __always_inline __u64 get_current_cgroup_id(void)
{
    return bpf_get_current_cgroup_id();
}

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

static __always_inline int __populate_key_from_dentry(struct policy_key* key, struct dentry* dentry, __u64 cgroupId)
{
    struct inode* inode = NULL;
    struct super_block* sb = NULL;
    __u64 dev;
    __u64 ino;

    if (!dentry) {
        return -EACCES;
    }

    CORE_READ_INTO(&inode, dentry, d_inode);
    if (!inode) {
        return -EACCES;
    }

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

static __always_inline int __read_dentry_name(struct dentry* dentry, char out[BASENAME_MAX_STR], __u32* out_len)
{
    struct qstr d_name = {};
    const unsigned char* name_ptr = NULL;
    __u32 len = 0;

    if (!dentry || !out || !out_len) {
        return -EACCES;
    }

    CORE_READ_INTO(&d_name, dentry, d_name);
    CORE_READ_INTO(&name_ptr, &d_name, name);
    CORE_READ_INTO(&len, &d_name, len);

    if (!name_ptr) {
        return -EACCES;
    }

    if (len >= BASENAME_MAX_STR) {
        len = BASENAME_MAX_STR - 1;
    }
    if (len > 0) {
        bpf_core_read(out, len, name_ptr);
    }
    out[len] = 0;
    *out_len = len;
    return 0;
}

static __always_inline int __match_qmark_bounded(const char* pattern, const char* s, __u32 n)
{
#pragma unroll
    for (int i = 0; i < BASENAME_MAX_STR; i++) {
        if ((__u32)i < n) {
            char pc = pattern[i];
            if (pc != '?' && pc != s[i]) {
                return 1;
            }
        }
    }
    return 0;
}

static __always_inline int __match_basename_rule(const struct basename_rule* rule, const char name[BASENAME_MAX_STR], __u32 name_len)
{
    if (!rule || rule->type == BASENAME_RULE_EMPTY) {
        return 0;
    }

    if (rule->prefix_len >= BASENAME_MAX_STR || rule->tail_len >= BASENAME_MAX_STR) {
        return 0;
    }

    if (name_len < rule->prefix_len) {
        return 0;
    }

    if (__match_qmark_bounded(rule->prefix, name, rule->prefix_len) != 0) {
        return 0;
    }

    if (rule->type == BASENAME_RULE_EXACT) {
        return name_len == rule->prefix_len;
    }

    if (rule->type == BASENAME_RULE_PREFIX) {
        return 1;
    }

    if (rule->type == BASENAME_RULE_DIGITS) {
        __u32 pos = rule->prefix_len;
        __u32 digit_count = 0;

#pragma unroll
        for (int i = 0; i < 64; i++) {
            __u32 idx = pos + (__u32)i;
            if (idx >= name_len) {
                break;
            }
            char c = name[idx];
            if (c >= '0' && c <= '9') {
                digit_count++;
                continue;
            }
            break;
        }

        if (rule->digits_max == 1) {
            if (digit_count != 1) {
                return 0;
            }
        } else {
            if (digit_count < 1) {
                return 0;
            }
        }

        __u32 tail_start = pos + digit_count;
        if (tail_start > name_len) {
            return 0;
        }

        __u32 remaining = name_len - tail_start;
        if (rule->tail_len > remaining) {
            return 0;
        }

        if (rule->tail_len > 0) {
            if (__match_qmark_bounded(rule->tail, &name[tail_start], rule->tail_len) != 0) {
                return 0;
            }
        }

        if (rule->tail_wildcard) {
            return 1;
        }
        return remaining == rule->tail_len;
    }

    return 0;
}

static __always_inline int __check_access(struct file* file, __u32 required)
{
    __u64 cgroup_id;
    struct policy_key key = {};
    struct policy_value* policy;

    cgroup_id = get_current_cgroup_id();
    if (cgroup_id == 0) {
        return 0;
    }

    if (__populate_key(&key, file, cgroup_id)) {
        return -EACCES;
    }

    // Fast path: exact inode allow
    policy = bpf_map_lookup_elem(&policy_map, &key);
    if (policy) {
        if (required & ~policy->allow_mask) {
            return -EACCES;
        }
        return 0;
    }

    // Directory rules and basename rules
    struct dentry* dentry = NULL;
    struct dentry* parent = NULL;
    CORE_READ_INTO(&dentry, file, f_path.dentry);
    if (!dentry) {
        return -EACCES;
    }
    CORE_READ_INTO(&parent, dentry, d_parent);
    if (!parent) {
        return -EACCES;
    }

    // Basename rules: only check immediate parent directory
    if (__populate_key_from_dentry(&key, parent, cgroup_id) == 0) {
        struct basename_policy_value* bval = bpf_map_lookup_elem(&basename_policy_map, &key);
        if (bval) {
            char name[BASENAME_MAX_STR] = {};
            __u32 name_len = 0;
            if (__read_dentry_name(dentry, name, &name_len) == 0) {
#pragma unroll
                for (int i = 0; i < BASENAME_RULE_MAX; i++) {
                    const struct basename_rule* rule = &bval->rules[i];
                    if (rule->type == BASENAME_RULE_EMPTY) {
                        continue;
                    }
                    if (__match_basename_rule(rule, name, name_len)) {
                        if (required & ~rule->allow_mask) {
                            return -EACCES;
                        }
                        return 0;
                    }
                }
            }
        }
    }

    struct dentry* cur = parent;
    #pragma unroll
    for (int depth = 0; depth < 32; depth++) {
        struct dir_policy_value* dir_policy;
        if (__populate_key_from_dentry(&key, cur, cgroup_id)) {
            return -EACCES;
        }

        dir_policy = bpf_map_lookup_elem(&dir_policy_map, &key);
        if (dir_policy) {
            __u32 flags = dir_policy->flags;
            if (depth == 0 || (flags & DIR_RULE_RECURSIVE)) {
                if (required & ~dir_policy->allow_mask) {
                    return -EACCES;
                }
                return 0;
            }
        }

        // Move to next ancestor
        struct dentry* next = NULL;
        CORE_READ_INTO(&next, cur, d_parent);
        if (!next || next == cur) {
            break;
        }
        cur = next;
    }

    return -EACCES;
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
    __u64 flags64;
    __u32 accmode;
    __u32 required = 0;
    
    // If previous checks failed, propagate the error
    if (ret != 0) {
        return ret;
    }
    
    // Determine required permissions from open flags
    flags64 = 0;
    CORE_READ_INTO(&flags64, file, f_flags);
    accmode = (__u32)flags64 & 00000003; /* O_ACCMODE */

    if (accmode == O_WRONLY) {
        required = PERM_WRITE;
    } else if (accmode == O_RDWR) {
        required = (PERM_READ | PERM_WRITE);
    } else {
        required = PERM_READ;
    }

    return __check_access(file, required);
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

    return __check_access(file, PERM_EXEC);
}

char LICENSE[] SEC("license") = "GPL";
