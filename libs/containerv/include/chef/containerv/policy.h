/**
 * Copyright 2024, Philip Meulengracht
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

#ifndef __CONTAINERV_POLICY_H__
#define __CONTAINERV_POLICY_H__

/**
 * @brief Security policy for containers using eBPF
 * 
 * Policies control what syscalls and filesystem paths a container can access.
 * Without any policy extensions, containers have minimal permissions suitable
 * for basic CLI applications.
 */

struct containerv_policy;

/**
 * @brief Policy types for predefined security profiles
 */
enum containerv_policy_type {
    CV_POLICY_MINIMAL,      // Basic CLI apps (read, write, open, close, exit, etc.)
    CV_POLICY_BUILD,        // Build tools (adds fork, exec, pipe, etc.)
    CV_POLICY_NETWORK,      // Network operations (adds socket, bind, connect, etc.)
    CV_POLICY_CUSTOM        // Custom policy built from scratch
};

/**
 * @brief Filesystem access modes
 */
enum containerv_fs_access {
    CV_FS_READ = 0x1,
    CV_FS_WRITE = 0x2,
    CV_FS_EXEC = 0x4,
    CV_FS_ALL = (CV_FS_READ | CV_FS_WRITE | CV_FS_EXEC)
};

/**
 * @brief Create a new security policy
 * @param type The base policy type to start with
 * @return Newly created policy, or NULL on error
 */
extern struct containerv_policy* containerv_policy_new(enum containerv_policy_type type);

/**
 * @brief Delete a security policy
 * @param policy The policy to delete
 */
extern void containerv_policy_delete(struct containerv_policy* policy);

/**
 * @brief Add allowed syscalls to the policy
 * @param policy The policy to modify
 * @param syscalls Array of syscall names to allow (NULL-terminated)
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_add_syscalls(
    struct containerv_policy* policy,
    const char* const*        syscalls
);

/**
 * @brief Add allowed filesystem path with specific access mode
 * @param policy The policy to modify
 * @param path Filesystem path pattern (supports wildcards)
 * @param access Bitwise OR of containerv_fs_access flags
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_add_path(
    struct containerv_policy* policy,
    const char*               path,
    enum containerv_fs_access access
);

/**
 * @brief Add multiple filesystem paths with the same access mode
 * @param policy The policy to modify
 * @param paths Array of filesystem path patterns (NULL-terminated)
 * @param access Bitwise OR of containerv_fs_access flags
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_add_paths(
    struct containerv_policy* policy,
    const char* const*        paths,
    enum containerv_fs_access access
);

/**
 * @brief Add a deny rule for a filesystem path
 * @param policy The policy to modify
 * @param path Filesystem path to deny
 * @param access Bitwise OR of containerv_fs_access flags to deny
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_deny_path(
    struct containerv_policy* policy,
    const char*               path,
    enum containerv_fs_access access
);

/**
 * @brief Add multiple deny rules for filesystem paths
 * @param policy The policy to modify
 * @param paths Array of filesystem path patterns (NULL-terminated)
 * @param access Bitwise OR of containerv_fs_access flags to deny
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_deny_paths(
    struct containerv_policy* policy,
    const char* const*        paths,
    enum containerv_fs_access access
);

#endif //!__CONTAINERV_POLICY_H__
