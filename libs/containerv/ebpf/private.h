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

struct bpf_policy_context {
    int                map_fd;
    int                dir_map_fd;
    unsigned long long cgroup_id;
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

#endif // __linux__

#endif //!__BPF_HELPERS_H__
