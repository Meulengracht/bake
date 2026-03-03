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

#ifndef __BPF_NET_HELPERS_H__
#define __BPF_NET_HELPERS_H__

#include <vmlinux.h>

#include <bpf/bpf_endian.h>

#include "common.h"

#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif

/* Network permission bits */
#define NET_PERM_CREATE  0x1
#define NET_PERM_BIND    0x2
#define NET_PERM_CONNECT 0x4
#define NET_PERM_LISTEN  0x8
#define NET_PERM_ACCEPT  0x10
#define NET_PERM_SEND    0x20

/* Protecc action values (must match protecc_action_t). */
#define PROTECC_ACTION_ALLOW 0u
#define PROTECC_ACTION_DENY  1u

/* Protecc net protocol values (must match protecc_net_protocol_t). */
#define PROTECC_NET_PROTOCOL_TCP  1u
#define PROTECC_NET_PROTOCOL_UDP  2u
#define PROTECC_NET_PROTOCOL_UNIX 3u

/* Protecc net family values (must match protecc_net_family_t). */
#define PROTECC_NET_FAMILY_IPV4 1u
#define PROTECC_NET_FAMILY_IPV6 2u
#define PROTECC_NET_FAMILY_UNIX 3u

/* Network policy keys/values */
#define NET_UNIX_PATH_MAX 108

static __always_inline int __sock_get_meta(struct socket* sock, __u16* family, __u16* type, __u32* protocol)
{
    struct sock* sk = (struct sock*)NULL;

    CORE_READ_INTO(&sk, sock, sk);
    if (sk == NULL) {
        return -EACCES;
    }

    CORE_READ_INTO(family, sk, __sk_common.skc_family);
    CORE_READ_INTO(protocol, sk, sk_protocol);
    CORE_READ_INTO(type, sock, type);
    return 0;
}

#endif /* __BPF_NET_HELPERS_H__ */
