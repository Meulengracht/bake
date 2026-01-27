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

/**
 * @brief Max tokens in a basename pattern.
 * Example supported:
 *   nvme[0-9]+n[0-9]+p[0-9]+     -> LIT, DIGITS+, LIT, DIGITS+, LIT, DIGITS+
 */
#define BASENAME_TOKEN_MAX 6

enum basename_token_type {
    BASENAME_TOKEN_EMPTY      = 0,
    BASENAME_TOKEN_LITERAL    = 1,
    BASENAME_TOKEN_DIGIT1     = 2,
    BASENAME_TOKEN_DIGITSPLUS = 3,
};

struct basename_rule {
    __u32 allow_mask;
    __u8  token_count;
    __u8  tail_wildcard;   /* if set, the last literal token only needs to match as a prefix */
    __u8  _pad[2];
    __u8  token_type[BASENAME_TOKEN_MAX];
    __u8  token_len[BASENAME_TOKEN_MAX];
    char  token[BASENAME_TOKEN_MAX][BASENAME_MAX_STR];
};

struct basename_policy_value {
    struct basename_rule rules[BASENAME_RULE_MAX];
};

/**
 * @brief BPF map: policy enforcement map
 * The key is (cgroup_id, dev, ino) identifying a file/inode within a container.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct policy_value);
    __uint(max_entries, 10240);
} policy_map SEC(".maps");

/** 
 * @brief  Directory policy map: rules keyed by directory inode (dev,ino) + cgroup 
 * Examples:
 *  - allow all files under /var/log (recursive)
 *  - allow children of /tmp only
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct policy_key);
    __type(value, struct dir_policy_value);
    __uint(max_entries, 10240);
} dir_policy_map SEC(".maps");

/**
 * @brief Basename policy map: rules keyed by parent directory inode (dev,ino) + cgroup 
 * Examples include
 *   /var/log/app-*.log
 *   /dev/nvme[0-9]+n[0-9]+p[0-9]+
 */
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
    for (int i = 0; i < BASENAME_MAX_STR; i++) {
        if ((__u32)i >= n) {
            break;
        }
        char pc = pattern[i];
        if (pc != '?' && pc != s[i]) {
            return 1;
        }
    }
    return 0;
}

static __always_inline int __match_basename_rule(const struct basename_rule* rule, const char name[BASENAME_MAX_STR], __u32 name_len)
{
    if (!rule || rule->token_count == 0) {
        return 0;
    }

    __u32 pos = 0;

#pragma unroll
    for (int t = 0; t < BASENAME_TOKEN_MAX; t++) {
        if ((__u32)t >= rule->token_count) {
            continue;
        }

        __u8 tt = rule->token_type[t];
        if (tt == BASENAME_TOKEN_LITERAL) {
            __u32 len = (__u32)rule->token_len[t];
            if (len >= BASENAME_MAX_STR) {
                return 0;
            }

            if (pos + len > name_len) {
                return 0;
            }

            // If this is the final token and tail_wildcard is set, allow extra suffix.
            if (rule->tail_wildcard && ((__u32)t + 1U == rule->token_count)) {
                if (__match_qmark_bounded(rule->token[t], &name[pos], len) != 0) {
                    return 0;
                }
                return 1;
            }

            if (__match_qmark_bounded(rule->token[t], &name[pos], len) != 0) {
                return 0;
            }
            pos += len;
            continue;
        }

        if (tt == BASENAME_TOKEN_DIGIT1) {
            if (pos >= name_len) {
                return 0;
            }
            char c = name[pos];
            if (c < '0' || c > '9') {
                return 0;
            }
            pos += 1;
            continue;
        }

        if (tt == BASENAME_TOKEN_DIGITSPLUS) {
            if (pos >= name_len) {
                return 0;
            }
            if (name[pos] < '0' || name[pos] > '9') {
                return 0;
            }

            __u32 digit_count = 0;
            for (int i = 0; i < 32; i++) {
                __u32 idx = pos + (__u32)i;
                if (idx >= name_len) {
                    break;
                }
                char dc = name[idx];
                if (dc >= '0' && dc <= '9') {
                    digit_count++;
                    continue;
                }
                break;
            }
            if (digit_count < 1) {
                return 0;
            }
            pos += digit_count;
            continue;
        }
    }

    return pos == name_len;
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
                    if (rule->token_count == 0) {
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
