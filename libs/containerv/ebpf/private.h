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

#ifndef __BPF_HELPERS_H__
#define __BPF_HELPERS_H__

#include <sys/types.h>

#ifdef __linux__

/* Forward declaration */
union bpf_attr;

/* Permission bits - matching BPF program definitions */
#define BPF_PERM_READ  0x1
#define BPF_PERM_WRITE 0x2
#define BPF_PERM_EXEC  0x4

/* Network permission bits - matching BPF program definitions */
#define BPF_NET_CREATE  0x1
#define BPF_NET_BIND    0x2
#define BPF_NET_CONNECT 0x4
#define BPF_NET_LISTEN  0x8
#define BPF_NET_ACCEPT  0x10
#define BPF_NET_SEND    0x20

/* Policy key: (cgroup_id, dev, ino) - must match BPF program */
struct bpf_policy_key {
    unsigned long long cgroup_id;
    unsigned long long dev;
    unsigned long long ino;
};

/* Policy value: permission mask */
struct bpf_policy_value {
    unsigned int allow_mask;
};

/* Directory policy flags */
#define BPF_DIR_RULE_CHILDREN_ONLY 0x1
#define BPF_DIR_RULE_RECURSIVE     0x2

/* Directory policy value */
struct bpf_dir_policy_value {
    unsigned int allow_mask;
    unsigned int flags;
};

/* Basename matcher support (must match BPF program) */
#define BPF_BASENAME_RULE_MAX 8
#define BPF_BASENAME_MAX_STR  32

#define BPF_BASENAME_TOKEN_MAX 6

enum bpf_basename_token_type {
    BPF_BASENAME_TOKEN_EMPTY      = 0,
    BPF_BASENAME_TOKEN_LITERAL    = 1,
    BPF_BASENAME_TOKEN_DIGIT1     = 2,
    BPF_BASENAME_TOKEN_DIGITSPLUS = 3,
};

struct bpf_basename_rule {
    unsigned int allow_mask;
    unsigned char token_count;
    unsigned char tail_wildcard;
    unsigned char _pad[2];
    unsigned char token_type[BPF_BASENAME_TOKEN_MAX];
    unsigned char token_len[BPF_BASENAME_TOKEN_MAX];
    char token[BPF_BASENAME_TOKEN_MAX][BPF_BASENAME_MAX_STR];
};

struct bpf_basename_policy_value {
    struct bpf_basename_rule rules[BPF_BASENAME_RULE_MAX];
};

/* Network policy keys/values (must match BPF program) */
#define BPF_NET_ADDR_MAX 16
#define BPF_NET_UNIX_PATH_MAX 108

struct bpf_net_create_key {
    unsigned long long cgroup_id;
    unsigned int       family;
    unsigned int       type;
    unsigned int       protocol;
};

struct bpf_net_tuple_key {
    unsigned long long cgroup_id;
    unsigned int       family;
    unsigned int       type;
    unsigned int       protocol;
    unsigned short     port;
    unsigned short     _pad;
    unsigned char      addr[BPF_NET_ADDR_MAX];
};

struct bpf_net_unix_key {
    unsigned long long cgroup_id;
    unsigned int       type;
    unsigned int       protocol;
    char               path[BPF_NET_UNIX_PATH_MAX];
};

struct bpf_net_policy_value {
    unsigned int allow_mask;
};

struct bpf_policy_context {
    int                map_fd;
    int                dir_map_fd;
    int                basename_map_fd;
    unsigned long long cgroup_id;
};

enum bpf_deny_hook_id {
    BPF_DENY_HOOK_FILE_OPEN = 1,
    BPF_DENY_HOOK_BPRM_CHECK = 2,
    BPF_DENY_HOOK_INODE_CREATE = 3,
    BPF_DENY_HOOK_INODE_MKDIR = 4,
    BPF_DENY_HOOK_INODE_MKNOD = 5,
    BPF_DENY_HOOK_INODE_UNLINK = 6,
    BPF_DENY_HOOK_INODE_RMDIR = 7,
    BPF_DENY_HOOK_INODE_RENAME = 8,
    BPF_DENY_HOOK_INODE_LINK = 9,
    BPF_DENY_HOOK_INODE_SYMLINK = 10,
    BPF_DENY_HOOK_INODE_SETATTR = 11,
    BPF_DENY_HOOK_PATH_TRUNCATE = 12,
};

struct bpf_deny_event {
    unsigned long long cgroup_id;
    unsigned long long dev;
    unsigned long long ino;
    unsigned int       required_mask;
    unsigned int       hook_id;
    unsigned int       name_len;
    char               comm[16];
    char               name[BPF_BASENAME_MAX_STR];
};

/**
 * @brief Wrapper for the BPF system call
 * @param cmd BPF command
 * @param attr BPF attributes
 * @param size Size of attributes
 * @return Result from syscall
 */
extern int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size);

/**
 * @brief Check if BPF LSM is available in the kernel
 * @return 1 if available, 0 otherwise
 */
extern int bpf_check_lsm_available(void);

/**
 * @brief Get the cgroup ID for a given hostname
 * @param hostname The container hostname
 * @return Cgroup ID, or 0 on error
 */
extern unsigned long long bpf_get_cgroup_id(const char* hostname);

/**
 * @brief Increase the memlock rlimit for BPF operations
 * @return 0 on success, -1 on error
 */
extern int bpf_bump_memlock_rlimit(void);

/**
 * @brief Add an inode to the BPF policy map with specified permissions
 * @param context BPF policy context
 * @param dev Device number
 * @param ino Inode number
 * @param allow_mask Permission mask
 * @return 0 on success, -1 on error
 */
extern int bpf_policy_map_allow_inode(
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino,
    unsigned int               allow_mask
);

/**
 * @brief Allow a directory via the directory policy map
 * @param context BPF policy context
 * @param dev Device number of the directory inode
 * @param ino Inode number of the directory inode
 * @param allow_mask Permission mask
 * @param flags Directory rule flags (BPF_DIR_RULE_*)
 * @return 0 on success, -1 on error
 */
extern int bpf_dir_policy_map_allow_dir(
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino,
    unsigned int               allow_mask,
    unsigned int               flags
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
    struct bpf_policy_context*     context,
    dev_t                          dev,
    ino_t                          ino,
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
    struct bpf_policy_context* context,
    dev_t                      dev,
    ino_t                      ino
);

/**
 * @brief Delete multiple entries from the BPF policy map in a single syscall
 * @param context BPF policy context
 * @param keys Array of keys to delete
 * @param count Number of keys in the array
 * @return Number of successfully deleted entries, or -1 on error
 */
extern int bpf_policy_map_delete_batch(
    struct bpf_policy_context* context,
    struct bpf_policy_key*     keys,
    int                        count
);

extern int bpf_net_create_map_allow(
    int                         map_fd,
    const struct bpf_net_create_key* key,
    unsigned int                allow_mask
);

extern int bpf_net_tuple_map_allow(
    int                         map_fd,
    const struct bpf_net_tuple_key* key,
    unsigned int                allow_mask
);

extern int bpf_net_unix_map_allow(
    int                         map_fd,
    const struct bpf_net_unix_key* key,
    unsigned int                allow_mask
);

extern int bpf_map_delete_batch_by_fd(
    int     map_fd,
    void*   keys,
    int     count,
    size_t  key_size
);

#endif // __linux__

#endif //!__BPF_HELPERS_H__
