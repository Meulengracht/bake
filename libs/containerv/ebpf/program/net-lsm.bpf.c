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

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "common.h"
#include "tracing.h"

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

/* Network policy keys/values */
#define NET_ADDR_MAX 16
#define NET_UNIX_PATH_MAX 108

struct net_create_key {
    __u64 cgroup_id;
    __u32 family;
    __u32 type;
    __u32 protocol;
};

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
    char  path[NET_UNIX_PATH_MAX];
};

struct net_policy_value {
    __u32 allow_mask;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct net_create_key);
    __type(value, struct net_policy_value);
    __uint(max_entries, 4096);
} net_create_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct net_tuple_key);
    __type(value, struct net_policy_value);
    __uint(max_entries, 8192);
} net_tuple_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, struct net_unix_key);
    __type(value, struct net_policy_value);
    __uint(max_entries, 4096);
} net_unix_map SEC(".maps");

static __always_inline int __sock_get_meta(struct socket* sock, __u32* family, __u32* type, __u32* protocol)
{
    struct sock* sk = NULL;
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
    __u32 max_len = NET_UNIX_PATH_MAX - 1;
    __u32 copy_len = (__u32)addrlen;
    if (copy_len > sizeof(sun)) {
        copy_len = sizeof(sun);
    }

    if (copy_len > (__builtin_offsetof(struct sockaddr_un, sun_path) + max_len)) {
        copy_len = __builtin_offsetof(struct sockaddr_un, sun_path) + max_len;
    }

    if (copy_len > __builtin_offsetof(struct sockaddr_un, sun_path)) {
        __u32 path_len = copy_len - __builtin_offsetof(struct sockaddr_un, sun_path);
        if (path_len > max_len) {
            path_len = max_len;
        }
        bpf_core_read(&key->path, path_len, &sun.sun_path);
        key->path[path_len] = 0;
    } else {
        key->path[0] = 0;
    }

    return 0;
}

static __always_inline int __net_allow_create(__u32 family, __u32 type, __u32 protocol, __u32 hook_id)
{
    struct net_create_key key = {};
    struct net_policy_value* val;

    key.cgroup_id = get_current_cgroup_id();
    key.family = family;
    key.type = type;
    key.protocol = protocol;

    val = bpf_map_lookup_elem(&net_create_map, &key);
    if (val && (NET_PERM_CREATE & ~val->allow_mask) == 0) {
        return 0;
    }

    __emit_deny_event_basic(key.cgroup_id, NET_PERM_CREATE, hook_id);
    return -EACCES;
}

static __always_inline int __net_allow_tuple(struct net_tuple_key* key, __u32 required, __u32 hook_id)
{
    struct net_policy_value* val;

    if (!key) {
        return -EACCES;
    }

    val = bpf_map_lookup_elem(&net_tuple_map, key);
    if (val && (required & ~val->allow_mask) == 0) {
        return 0;
    }

    __emit_deny_event_basic(key->cgroup_id, required, hook_id);
    return -EACCES;
}

static __always_inline int __net_allow_unix(struct net_unix_key* key, __u32 required, __u32 hook_id)
{
    struct net_policy_value* val;

    if (!key) {
        return -EACCES;
    }

    val = bpf_map_lookup_elem(&net_unix_map, key);
    if (val && (required & ~val->allow_mask) == 0) {
        return 0;
    }

    __emit_deny_event_basic(key->cgroup_id, required, hook_id);
    return -EACCES;
}

SEC("lsm/socket_create")
int BPF_PROG(socket_create_restrict, int family, int type, int protocol, int kern, int ret)
{
    (void)kern;
    if (ret) {
        return ret;
    }
    return __net_allow_create((__u32)family, (__u32)type, (__u32)protocol, DENY_HOOK_SOCKET_CREATE);
}

SEC("lsm/socket_bind")
int BPF_PROG(socket_bind_restrict, struct socket *sock, struct sockaddr *address, int addrlen, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    __u32 family, type, protocol;

    if (ret) {
        return ret;
    }
    if (!sock || !address) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }
    (void)family;

    if (__sockaddr_to_unix(address, addrlen, &ukey) == 0) {
        ukey.cgroup_id = get_current_cgroup_id();
        ukey.type = type;
        ukey.protocol = protocol;
        return __net_allow_unix(&ukey, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    }

    tkey.cgroup_id = get_current_cgroup_id();
    tkey.type = type;
    tkey.protocol = protocol;
    if (__sockaddr_to_tuple(address, addrlen, &tkey) == 0) {
        return __net_allow_tuple(&tkey, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    }

    __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    return -EACCES;
}

SEC("lsm/socket_connect")
int BPF_PROG(socket_connect_restrict, struct socket *sock, struct sockaddr *address, int addrlen, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    __u32 family, type, protocol;

    if (ret) {
        return ret;
    }
    if (!sock || !address) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }
    (void)family;

    if (__sockaddr_to_unix(address, addrlen, &ukey) == 0) {
        ukey.cgroup_id = get_current_cgroup_id();
        ukey.type = type;
        ukey.protocol = protocol;
        return __net_allow_unix(&ukey, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    }

    tkey.cgroup_id = get_current_cgroup_id();
    tkey.type = type;
    tkey.protocol = protocol;
    if (__sockaddr_to_tuple(address, addrlen, &tkey) == 0) {
        return __net_allow_tuple(&tkey, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    }

    __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    return -EACCES;
}

SEC("lsm/socket_listen")
int BPF_PROG(socket_listen_restrict, struct socket *sock, int backlog, int ret)
{
    struct net_tuple_key tkey = {};
    struct sock* sk = NULL;
    __u32 family, type, protocol;
    __u16 port = 0;

    (void)backlog;
    if (ret) {
        return ret;
    }
    if (!sock) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_LISTEN, DENY_HOOK_SOCKET_LISTEN);
        return -EACCES;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_LISTEN, DENY_HOOK_SOCKET_LISTEN);
        return -EACCES;
    }

    CORE_READ_INTO(&sk, sock, sk);
    if (!sk) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_LISTEN, DENY_HOOK_SOCKET_LISTEN);
        return -EACCES;
    }
    CORE_READ_INTO(&port, sk, __sk_common.skc_num);

    tkey.cgroup_id = get_current_cgroup_id();
    tkey.family = family;
    tkey.type = type;
    tkey.protocol = protocol;
    tkey.port = port;
    return __net_allow_tuple(&tkey, NET_PERM_LISTEN, DENY_HOOK_SOCKET_LISTEN);
}

SEC("lsm/socket_accept")
int BPF_PROG(socket_accept_restrict, struct socket *sock, struct socket *newsock, int flags, int ret)
{
    struct net_tuple_key tkey = {};
    struct sock* sk = NULL;
    __u32 family, type, protocol;
    __u16 port = 0;

    (void)newsock;
    (void)flags;
    if (ret) {
        return ret;
    }
    if (!sock) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_ACCEPT, DENY_HOOK_SOCKET_ACCEPT);
        return -EACCES;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_ACCEPT, DENY_HOOK_SOCKET_ACCEPT);
        return -EACCES;
    }

    CORE_READ_INTO(&sk, sock, sk);
    if (!sk) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_ACCEPT, DENY_HOOK_SOCKET_ACCEPT);
        return -EACCES;
    }
    CORE_READ_INTO(&port, sk, __sk_common.skc_num);

    tkey.cgroup_id = get_current_cgroup_id();
    tkey.family = family;
    tkey.type = type;
    tkey.protocol = protocol;
    tkey.port = port;
    return __net_allow_tuple(&tkey, NET_PERM_ACCEPT, DENY_HOOK_SOCKET_ACCEPT);
}

SEC("lsm/socket_sendmsg")
int BPF_PROG(socket_sendmsg_restrict, struct socket *sock, struct msghdr *msg, int size, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    __u32 family, type, protocol;
    struct sockaddr* addr = NULL;
    __u32 addrlen = 0;

    (void)size;
    if (ret) {
        return ret;
    }
    if (!sock) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        return -EACCES;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        return -EACCES;
    }

    if (msg) {
        CORE_READ_INTO(&addr, msg, msg_name);
        CORE_READ_INTO(&addrlen, msg, msg_namelen);
    }

    if (addr) {
        if (__sockaddr_to_unix(addr, (int)addrlen, &ukey) == 0) {
            ukey.cgroup_id = get_current_cgroup_id();
            ukey.type = type;
            ukey.protocol = protocol;
            return __net_allow_unix(&ukey, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        }

        tkey.cgroup_id = get_current_cgroup_id();
        tkey.type = type;
        tkey.protocol = protocol;
        if (__sockaddr_to_tuple(addr, (int)addrlen, &tkey) == 0) {
            return __net_allow_tuple(&tkey, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        }
    }

    // No explicit destination: allow only if a wildcard tuple exists
    tkey.cgroup_id = get_current_cgroup_id();
    tkey.family = family;
    tkey.type = type;
    tkey.protocol = protocol;
    tkey.port = 0;
    return __net_allow_tuple(&tkey, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
}

char LICENSE[] SEC("license") = "GPL";
