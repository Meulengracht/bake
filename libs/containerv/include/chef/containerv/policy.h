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

#ifndef __CONTAINERV_POLICY_H__
#define __CONTAINERV_POLICY_H__

#include <chef/list.h>

/**
 * @brief Security policy for containers using eBPF
 * 
 * Policies control what syscalls and filesystem paths a container can access.
 * Without any policy extensions, containers have minimal permissions suitable
 * for basic CLI applications.
 */
struct containerv_policy;

/**
 * @brief Generic security strength levels.
 *
 * This is a cross-platform concept. Not all backends enforce all levels.
 */
enum containerv_security_level {
    CV_SECURITY_DEFAULT = 0,
    CV_SECURITY_RESTRICTED = 1,
    CV_SECURITY_STRICT = 2
};

struct containerv_policy_plugin {
    struct list_item header;
    const char*      name;
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
 * @brief Network access modes
 */
enum containerv_net_access {
    CV_NET_CREATE  = 0x1,
    CV_NET_BIND    = 0x2,
    CV_NET_CONNECT = 0x4,
    CV_NET_LISTEN  = 0x8,
    CV_NET_ACCEPT  = 0x10,
    CV_NET_SEND    = 0x20,
    CV_NET_ALL     = (CV_NET_CREATE | CV_NET_BIND | CV_NET_CONNECT | CV_NET_LISTEN | CV_NET_ACCEPT | CV_NET_SEND)
};

/**
 * @brief Network allow rule (tuple-based)
 *
 * Use AF_* for family, SOCK_* for type, and IPPROTO_* for protocol.
 * For AF_UNIX, set unix_path. For AF_INET/AF_INET6, set addr/addr_len and port.
 * For INET, addr is in network byte order; port is in host byte order.
 */
struct containerv_net_rule {
    int                 family;
    int                 type;
    int                 protocol;
    unsigned short      port;
    const unsigned char* addr;
    unsigned int        addr_len;
    const char*         unix_path;
    unsigned int        allow_mask;
};

/**
 * @brief Set/get the generic security level for a policy.
 */
extern int containerv_policy_set_security_level(struct containerv_policy* policy, enum containerv_security_level level);
extern enum containerv_security_level containerv_policy_get_security_level(const struct containerv_policy* policy);

/**
 * @brief Configure Windows-specific isolation parameters.
 *
 * On non-Windows platforms this is a no-op.
 */
extern int containerv_policy_set_windows_isolation(
    struct containerv_policy* policy,
    int                       use_app_container,
    const char*               integrity_level,
    const char* const*        capability_sids,
    int                       capability_sid_count
);

extern int containerv_policy_get_windows_isolation(
    const struct containerv_policy* policy,
    int*                            use_app_container,
    const char**                    integrity_level,
    const char* const**             capability_sids,
    int*                            capability_sid_count
);

/**
 * @brief Create a new security policy
 * @param plugins List of base policy plugins to start with
 * @return Newly created policy, or NULL on error
 */
extern struct containerv_policy* containerv_policy_new(struct list* plugins);

/**
 * @brief Delete a security policy
 * @param policy The policy to delete
 */
extern void containerv_policy_delete(struct containerv_policy* policy);

/**
 * @brief Add network allow rules
 * @param policy The policy to update
 * @param rules Array of network rules
 * @param count Number of rules
 * @return 0 on success, -1 on error
 */
extern int containerv_policy_add_net_rules(
    struct containerv_policy*     policy,
    const struct containerv_net_rule* rules,
    int                           count
);

#endif //!__CONTAINERV_POLICY_H__
