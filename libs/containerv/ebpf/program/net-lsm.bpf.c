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
 * https://github.com/torvalds/linux/blob/master/include/linux/lsm_hook_defs.h
 * socket_create
 * socket_socketpair
 * socket_bind
 * socket_connect
 * socket_listen
 * socket_accept
 * socket_sendmsg
 * socket_recvmsg
 * socket_getsockopt
 * socket_setsockopt
 * socket_sock_shutdown
 * socket_sock_rcv_skb
 */

#include <vmlinux.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "common-net.h"
#include "tracing.h"

#include <protecc/bpf/net.h>

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

#ifndef PROTECC_PROFILE_MAP_MAX_ENTRIES
#define PROTECC_PROFILE_MAP_MAX_ENTRIES 1024u
#endif

struct profile_value {
    __u32 size;
    __u8  data[PROTECC_BPF_MAX_PROFILE_SIZE];
};

struct net_create_key {
    __u64 cgroup_id;
    __u32 family;
    __u32 type;
    __u32 protocol;
};

struct net_policy_value {
    __u32 allow_mask;
};

struct per_cpu_data {
    char text[PATH_BUFFER_SIZE];
};

/**
 * @brief BPF map: protecc profiles per cgroup
 * The key is cgroup_id, value is a serialized protecc profile blob.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u64);
    __type(value, struct profile_value);
    __uint(max_entries, PROTECC_PROFILE_MAP_MAX_ENTRIES);
} net_profile_map SEC(".maps");

/* Per-CPU scratch buffer to avoid large stack allocations. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);
    __type(value, struct per_cpu_data);
    __uint(max_entries, 1);
} per_cpu_data_map SEC(".maps");

static __always_inline struct per_cpu_data* __cpu_data(void)
{
    __u32                key = 0;
    struct per_cpu_data* scratch = bpf_map_lookup_elem(&per_cpu_data_map, &key);
    if (!scratch) {
        return NULL;
    }
    return scratch;
}

static __always_inline __u8 __to_protecc_net_family(__u32 family)
{
    if (family == AF_INET) {
        return PROTECC_NET_FAMILY_IPV4;
    }
    if (family == AF_INET6) {
        return PROTECC_NET_FAMILY_IPV6;
    }
    if (family == AF_UNIX) {
        return PROTECC_NET_FAMILY_UNIX;
    }
    return PROTECC_BPF_NET_FAMILY_ANY;
}

static __always_inline __u8 __to_protecc_net_protocol(__u32 family, __u32 protocol)
{
    if (family == AF_UNIX) {
        return PROTECC_NET_PROTOCOL_UNIX;
    }

    if (protocol == IPPROTO_TCP) {
        return PROTECC_NET_PROTOCOL_TCP;
    }
    if (protocol == IPPROTO_UDP) {
        return PROTECC_NET_PROTOCOL_UDP;
    }

    return PROTECC_BPF_NET_PROTOCOL_ANY;
}

static __always_inline __u32 __append_u8_dec(char* out, __u8 value)
{
    __u32 n = 0;
    __u8 hundreds;
    __u8 tens;
    __u8 ones;

    if (value >= 100) {
        hundreds = value / 100;
        tens = (value / 10) % 10;
        ones = value % 10;
        out[n++] = (char)('0' + hundreds);
        out[n++] = (char)('0' + tens);
        out[n++] = (char)('0' + ones);
        return n;
    }

    if (value >= 10) {
        tens = value / 10;
        ones = value % 10;
        out[n++] = (char)('0' + tens);
        out[n++] = (char)('0' + ones);
        return n;
    }

    out[n++] = (char)('0' + value);
    return n;
}

static __always_inline __u8 __to_hex(__u8 value)
{
    if (value < 10) {
        return (__u8)('0' + value);
    }
    return (__u8)('a' + (value - 10));
}

static __always_inline int __tuple_to_ip_string(
    struct per_cpu_data*    scratch,
    const struct net_tuple_key* key,
    protecc_bpf_string_t*   ipOut)
{
    __u32 i;
    __u32 n = 0;

    if (!scratch || !key || !ipOut) {
        return -EACCES;
    }

    if (key->family == AF_INET) {
        bpf_for (i, 0, 4) {
            n += __append_u8_dec(&scratch->text[n], key->addr[i]);
            if (i != 3) {
                scratch->text[n++] = '.';
            }
        }
        ipOut->data = (const __u8*)&scratch->text[0];
        ipOut->len = n;
        return 0;
    }

    if (key->family == AF_INET6) {
        bpf_for (i, 0, 8) {
            __u8 hi = key->addr[(i * 2) + 0];
            __u8 lo = key->addr[(i * 2) + 1];

            scratch->text[n++] = (char)__to_hex((__u8)((hi >> 4) & 0xF));
            scratch->text[n++] = (char)__to_hex((__u8)(hi & 0xF));
            scratch->text[n++] = (char)__to_hex((__u8)((lo >> 4) & 0xF));
            scratch->text[n++] = (char)__to_hex((__u8)(lo & 0xF));

            if (i != 7) {
                scratch->text[n++] = ':';
            }
        }
        ipOut->data = (const __u8*)&scratch->text[0];
        ipOut->len = n;
        return 0;
    }

    ipOut->data = NULL;
    ipOut->len = 0;
    return 0;
}

static __noinline int __net_allow_request(
    __u64                           cgroupId,
    const protecc_bpf_net_request_t* request,
    __u32                           required,
    __u32                           hookId)
{
    struct profile_value* profile;
    __u8                  action = PROTECC_ACTION_ALLOW;
    bool                  match;

    if (cgroupId == 0) {
        return 0;
    }

    profile = bpf_map_lookup_elem(&net_profile_map, &cgroupId);
    if (profile == NULL) {
        return 0;
    }

    if (profile->size == 0 || profile->size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        __emit_deny_event_basic(cgroupId, required, hookId);
        return -EACCES;
    }

    match = protecc_bpf_match_net(profile->data, request, &action);
    if (!match) {
        return 0;
    }

    if (action != PROTECC_ACTION_DENY) {
        return 0;
    }

    __emit_deny_event_basic(cgroupId, required, hookId);
    return -EACCES;
}

SEC("lsm/socket_create")
int BPF_PROG(socket_create_restrict, int family, int type, int protocol, int kern, int ret)
{
    protecc_bpf_net_request_t request = {};
    __u64                     cgroupId;

    (void)kern;
    (void)type;
    if (ret) {
        return ret;
    }

    cgroupId = get_current_cgroup_id();
    if (cgroupId == 0) {
        return 0;
    }

    request.family = __to_protecc_net_family((__u32)family);
    request.protocol = __to_protecc_net_protocol((__u32)family, (__u32)protocol);
    request.port = 0;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    return __net_allow_request(cgroupId, &request, NET_PERM_CREATE, DENY_HOOK_SOCKET_CREATE);
}

SEC("lsm/socket_bind")
int BPF_PROG(socket_bind_restrict, struct socket *sock, struct sockaddr *address, int addrlen, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data* scratch;
    __u64 cgroupId;
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

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    request.port = 0;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    scratch = __cpu_data();
    if (!scratch) {
        __emit_deny_event_basic(cgroupId, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }

    (void)type;

    if (__sockaddr_to_unix(address, addrlen, &ukey) == 0) {
        request.unix_path.data = (const __u8*)ukey.path;
        request.unix_path.len = ukey.path_len;
        return __net_allow_request(cgroupId, &request, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    }

    if (__sockaddr_to_tuple(address, addrlen, &tkey) == 0) {
        request.port = tkey.port;
        if (__tuple_to_ip_string(scratch, &tkey, &request.ip) != 0) {
            __emit_deny_event_basic(cgroupId, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
            return -EACCES;
        }
        return __net_allow_request(cgroupId, &request, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    }

    __emit_deny_event_basic(cgroupId, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
    return -EACCES;
}

SEC("lsm/socket_connect")
int BPF_PROG(socket_connect_restrict, struct socket *sock, struct sockaddr *address, int addrlen, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data* scratch;
    __u64 cgroupId;
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

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    request.port = 0;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    scratch = __cpu_data();
    if (!scratch) {
        __emit_deny_event_basic(cgroupId, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }

    (void)type;

    if (__sockaddr_to_unix(address, addrlen, &ukey) == 0) {
        request.unix_path.data = (const __u8*)ukey.path;
        request.unix_path.len = ukey.path_len;
        return __net_allow_request(cgroupId, &request, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    }

    if (__sockaddr_to_tuple(address, addrlen, &tkey) == 0) {
        request.port = tkey.port;
        if (__tuple_to_ip_string(scratch, &tkey, &request.ip) != 0) {
            __emit_deny_event_basic(cgroupId, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
            return -EACCES;
        }
        return __net_allow_request(cgroupId, &request, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    }

    __emit_deny_event_basic(cgroupId, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
    return -EACCES;
}

SEC("lsm/socket_listen")
int BPF_PROG(socket_listen_restrict, struct socket *sock, int backlog, int ret)
{
    protecc_bpf_net_request_t request = {};
    struct sock* sk = NULL;
    __u64 cgroupId;
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

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    request.port = port;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    (void)type;
    return __net_allow_request(cgroupId, &request, NET_PERM_LISTEN, DENY_HOOK_SOCKET_LISTEN);
}

SEC("lsm/socket_accept")
int BPF_PROG(socket_accept_restrict, struct socket *sock, struct socket *newsock, int flags, int ret)
{
    protecc_bpf_net_request_t request = {};
    struct sock* sk = NULL;
    __u64 cgroupId;
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

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    request.port = port;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    (void)type;
    return __net_allow_request(cgroupId, &request, NET_PERM_ACCEPT, DENY_HOOK_SOCKET_ACCEPT);
}

SEC("lsm/socket_sendmsg")
int BPF_PROG(socket_sendmsg_restrict, struct socket *sock, struct msghdr *msg, int size, int ret)
{
    struct net_tuple_key tkey = {};
    struct net_unix_key  ukey = {};
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data* scratch;
    __u64 cgroupId;
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

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    request.port = 0;
    request.ip.data = NULL;
    request.ip.len = 0;
    request.unix_path.data = NULL;
    request.unix_path.len = 0;
    scratch = __cpu_data();
    if (!scratch) {
        __emit_deny_event_basic(cgroupId, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        return -EACCES;
    }

    (void)type;

    if (msg) {
        CORE_READ_INTO(&addr, msg, msg_name);
        CORE_READ_INTO(&addrlen, msg, msg_namelen);
    }

    if (addr) {
        if (__sockaddr_to_unix(addr, (int)addrlen, &ukey) == 0) {
            request.unix_path.data = (const __u8*)ukey.path;
            request.unix_path.len = ukey.path_len;
            return __net_allow_request(cgroupId, &request, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        }

        if (__sockaddr_to_tuple(addr, (int)addrlen, &tkey) == 0) {
            request.port = tkey.port;
            if (__tuple_to_ip_string(scratch, &tkey, &request.ip) != 0) {
                __emit_deny_event_basic(cgroupId, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
                return -EACCES;
            }
            return __net_allow_request(cgroupId, &request, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
        }
    }

    return __net_allow_request(cgroupId, &request, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
}

char LICENSE[] SEC("license") = "GPL";
