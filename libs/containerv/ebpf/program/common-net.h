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

/* Network policy keys/values */
#define NET_ADDR_MAX 16
#define NET_UNIX_PATH_MAX 108

struct net_tuple_key {
    __u64 cgroup_id;
    __u32 family;
    __u32 type;
    __u32 protocol;
    __u16 port;
    __u16 _pad;
    __u8  addr[NET_ADDR_MAX];
};

struct net_unix_key {
    __u64 cgroup_id;
    __u32 type;
    __u32 protocol;
    __u32 path_len;
    __u8  is_abstract;
    __u8  _pad[3];
    char  path[NET_UNIX_PATH_MAX];
};

static __always_inline int __sock_get_meta(struct socket* sock, __u32* family, __u32* type, __u32* protocol)
{
    struct sock* sk = (struct sock*)NULL;
    __u16 fam = 0;
    __u16 proto = 0;
    __u32 stype = 0;

    if (!sock || !family || !type || !protocol) {
        return -EACCES;
    }

    CORE_READ_INTO(&sk, sock, sk);
    if (!sk) {
        return -EACCES;
    }

    CORE_READ_INTO(&fam, sk, __sk_common.skc_family);
    CORE_READ_INTO(&proto, sk, sk_protocol);
    CORE_READ_INTO(&stype, sock, type);

    *family = (__u32)fam;
    *protocol = (__u32)proto;
    *type = stype;
    return 0;
}

static __always_inline int __sockaddr_to_tuple(
    struct sockaddr* addr,
    int addrlen,
    struct net_tuple_key* key)
{
    struct sockaddr sa = {};
    __u16 family = 0;

    if (!addr || !key || addrlen < (int)sizeof(sa)) {
        return -EACCES;
    }

    bpf_core_read(&sa, sizeof(sa), addr);
    family = sa.sa_family;
    key->family = family;

    if (family == AF_INET) {
        struct sockaddr_in sin = {};
        __u32 addr4 = 0;
        bpf_core_read(&sin, sizeof(sin), addr);
        addr4 = sin.sin_addr.s_addr;
        key->port = bpf_ntohs(sin.sin_port);
        __builtin_memcpy(key->addr, &addr4, sizeof(addr4));
        return 0;
    }

    if (family == AF_INET6) {
        struct sockaddr_in6 sin6 = {};
        bpf_core_read(&sin6, sizeof(sin6), addr);
        key->port = bpf_ntohs(sin6.sin6_port);
        bpf_core_read(&key->addr, sizeof(key->addr), &sin6.sin6_addr);
        return 0;
    }

    return -EACCES;
}

static __always_inline int __sockaddr_to_unix(
    struct sockaddr* addr,
    int addrlen,
    struct net_unix_key* key)
{
    struct sockaddr sa = {};

    if (!addr || !key || addrlen < (int)sizeof(sa)) {
        return -EACCES;
    }

    bpf_core_read(&sa, sizeof(sa), addr);
    if (sa.sa_family != AF_UNIX) {
        return -EACCES;
    }

    struct sockaddr_un sun = {};
    bpf_core_read(&sun, sizeof(sun), addr);
    __u32 max_len = NET_UNIX_PATH_MAX;
    __u32 copy_len = (__u32)addrlen;
    if (copy_len > sizeof(sun)) {
        copy_len = sizeof(sun);
    }

    if (copy_len > (__builtin_offsetof(struct sockaddr_un, sun_path) + max_len)) {
        copy_len = __builtin_offsetof(struct sockaddr_un, sun_path) + max_len;
    }

    key->path_len = 0;
    key->is_abstract = 0;

    if (copy_len > __builtin_offsetof(struct sockaddr_un, sun_path)) {
        __u32 path_len = copy_len - __builtin_offsetof(struct sockaddr_un, sun_path);
        if (path_len > max_len) {
            path_len = max_len;
        }

        if (path_len > 0 && sun.sun_path[0] == 0) {
            key->is_abstract = 1;
            path_len -= 1;
            key->path_len = path_len;
            if (path_len > 0) {
                bpf_core_read(&key->path, path_len, &sun.sun_path[1]);
            }
        } else {
            key->path_len = path_len;
            if (path_len > 0) {
                bpf_core_read(&key->path, path_len, &sun.sun_path[0]);
            }
            if (path_len < NET_UNIX_PATH_MAX) {
                key->path[path_len] = 0;
            }
        }
    } else {
        key->path[0] = 0;
    }

    return 0;
}

#endif /* __BPF_NET_HELPERS_H__ */
