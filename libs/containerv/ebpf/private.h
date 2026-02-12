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

#ifndef __BPF_PRIVATE_H__
#define __BPF_PRIVATE_H__

#include <sys/types.h>

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
#define BPF_BASENAME_MAX_STR  64

#define BPF_BASENAME_TOKEN_MAX 6

#define BPF_PIN_PATH "/sys/fs/bpf/cvd"
#define POLICY_MAP_PIN_PATH BPF_PIN_PATH "/policy_map"
#define DIR_POLICY_MAP_PIN_PATH BPF_PIN_PATH "/dir_policy_map"
#define BASENAME_POLICY_MAP_PIN_PATH BPF_PIN_PATH "/basename_policy_map"

#define NET_CREATE_MAP_PIN_PATH BPF_PIN_PATH "/net_create_map"
#define NET_TUPLE_MAP_PIN_PATH BPF_PIN_PATH "/net_tuple_map"
#define NET_UNIX_MAP_PIN_PATH BPF_PIN_PATH "/net_unix_map"

#define MAX_TRACKED_ENTRIES 10240

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
    unsigned int       path_len;
    unsigned char      is_abstract;
    unsigned char      _pad[3];
    char               path[BPF_NET_UNIX_PATH_MAX];
};

struct bpf_net_policy_value {
    unsigned int allow_mask;
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
    BPF_DENY_HOOK_SOCKET_CREATE = 20,
    BPF_DENY_HOOK_SOCKET_BIND = 21,
    BPF_DENY_HOOK_SOCKET_CONNECT = 22,
    BPF_DENY_HOOK_SOCKET_LISTEN = 23,
    BPF_DENY_HOOK_SOCKET_ACCEPT = 24,
    BPF_DENY_HOOK_SOCKET_SENDMSG = 25,
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

#endif //!__BPF_PRIVATE_H__
