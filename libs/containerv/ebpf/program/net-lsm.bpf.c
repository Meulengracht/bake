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

#ifndef PROTECC_PROFILE_MAP_MAX_ENTRIES
#define PROTECC_PROFILE_MAP_MAX_ENTRIES 1024u
#endif

struct profile_value {
    __u32 size;
    __u8  data[PROTECC_BPF_MAX_PROFILE_SIZE];
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
    __u32 key = 0;
    return bpf_map_lookup_elem(&per_cpu_data_map, &key);
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

static int __request_from_address(
    struct per_cpu_data*       scratch,
    __u16                      family,
    struct sockaddr*           addr,
    int                        addrlen,
    protecc_bpf_net_request_t* request)
{
    __u32 i;
    __u32 n = 0;

    request->port = 0;
    request->ip.data = NULL;
    request->ip.len = 0;
    request->unix_path.data = NULL;
    request->unix_path.len = 0;

    switch (family) {
        case AF_INET: {
            struct sockaddr_in sin = {};
            __u32              addr4;
            
            if (addrlen < (int)sizeof(struct sockaddr_in)) {
                return -EACCES;
            }

            bpf_core_read(&sin, sizeof(sin), addr);
            addr4 = bpf_ntohl(sin.sin_addr.s_addr);

            #pragma unroll
            for (i = 0; i < 4; i++) {
                __u32 remaining;
                __u32 written;
                __u8 octet = (__u8)((addr4 >> (24 - (i * 8))) & 0xFF);

                if (n >= PATH_BUFFER_SIZE) {
                    return -EACCES;
                }

                remaining = PATH_BUFFER_SIZE - n;
                written = __append_u8_dec(&scratch->text[n], remaining, octet);
                if (written == 0) {
                    return -EACCES;
                }
                n += written;

                if (i != 3) {
                    if (n >= PATH_BUFFER_SIZE) {
                        return -EACCES;
                    }
                    scratch->text[n++] = '.';
                }
            }

            request->port = bpf_ntohs(sin.sin_port);
            request->ip.data = (const __u8*)&scratch->text[0];
            request->ip.len = n;
        } break;
        case AF_INET6: {
            struct sockaddr_in6 sin6 = {};

            if (addrlen < (int)sizeof(struct sockaddr_in6)) {
                return -EACCES;
            }
            
            bpf_core_read(&sin6, sizeof(sin6), addr);

            #pragma unroll
            for (i = 0; i < 8; i++) {
                __u8 hi = sin6.sin6_addr.in6_u.u6_addr8[(i * 2) + 0];
                __u8 lo = sin6.sin6_addr.in6_u.u6_addr8[(i * 2) + 1];

                scratch->text[n++] = (char)__to_hex((__u8)((hi >> 4) & 0xF));
                scratch->text[n++] = (char)__to_hex((__u8)(hi & 0xF));
                scratch->text[n++] = (char)__to_hex((__u8)((lo >> 4) & 0xF));
                scratch->text[n++] = (char)__to_hex((__u8)(lo & 0xF));

                if (i != 7) {
                    scratch->text[n++] = ':';
                }
            }
            
            request->port = bpf_ntohs(sin6.sin6_port);
            request->ip.data = (const __u8*)&scratch->text[0];
            request->ip.len = n;
        } break;
        case AF_UNIX: {
            struct sockaddr_un sun = {};
            __u32              baseSize = (__u32)offsetof(struct sockaddr_un, sun_path);
            __u32              pathLength;

            if (addrlen < (int)baseSize) {
                return -EACCES;
            }

            pathLength = (__u32)addrlen - baseSize;
            if (pathLength > (NET_UNIX_PATH_MAX - 1)) {
                pathLength = NET_UNIX_PATH_MAX - 1;
            }

            bpf_core_read(&sun, sizeof(sun), addr);
            if (sun.sun_path[0] == 0) {
                scratch->text[n++] = '@';
                if (pathLength > 0) {
                    bpf_core_read(&scratch->text[n], pathLength, &sun.sun_path[1]);
                    n += pathLength;
                }
            } else {
                if (pathLength > 0) {
                    bpf_core_read(&scratch->text[n], pathLength, &sun.sun_path[0]);
                    n += pathLength;
                }
            }
            request->unix_path.data = (const __u8*)&scratch->text[0];
            request->unix_path.len = n;
        } break;
        default:
            return -EACCES;
    }
    return 0;
}

static __noinline int __net_allow_request(
    __u64                            cgroupId,
    const protecc_bpf_net_request_t* request,
    __u32                            required,
    __u32                            hookId)
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
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data*      scratch;
    __u64                     cgroupId;
    __u16                     family, type;
    __u32                     protocol;

    if (ret) {
        return ret;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    
    scratch = __cpu_data();
    if (!scratch) {
        __emit_deny_event_basic(cgroupId, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }

    (void)type;

    if (__request_from_address(scratch, family, address, addrlen, &request)) {
        __emit_deny_event_basic(cgroupId, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
        return -EACCES;
    }
    return __net_allow_request(cgroupId, &request, NET_PERM_BIND, DENY_HOOK_SOCKET_BIND);
}

SEC("lsm/socket_connect")
int BPF_PROG(socket_connect_restrict, struct socket *sock, struct sockaddr *address, int addrlen, int ret)
{
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data*      scratch;
    __u64                     cgroupId;
    __u16                     family, type;
    __u32                     protocol;

    if (ret) {
        return ret;
    }

    if (__sock_get_meta(sock, &family, &type, &protocol)) {
        __emit_deny_event_basic(get_current_cgroup_id(), NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }

    cgroupId = get_current_cgroup_id();
    request.family = __to_protecc_net_family(family);
    request.protocol = __to_protecc_net_protocol(family, protocol);
    
    scratch = __cpu_data();
    if (!scratch) {
        __emit_deny_event_basic(cgroupId, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }

    (void)type;

    if (__request_from_address(scratch, family, address, addrlen, &request)) {
        __emit_deny_event_basic(cgroupId, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
        return -EACCES;
    }
    return __net_allow_request(cgroupId, &request, NET_PERM_CONNECT, DENY_HOOK_SOCKET_CONNECT);
}

SEC("lsm/socket_listen")
int BPF_PROG(socket_listen_restrict, struct socket *sock, int backlog, int ret)
{
    protecc_bpf_net_request_t request = {};
    struct sock* sk = NULL;
    __u64 cgroupId;
    __u16 family, type;
    __u32 protocol;
    __u16 port = 0;
    (void)backlog;
    
    if (ret) {
        return ret;
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
int BPF_PROG(socket_accept_restrict, struct socket *sock, struct socket *newsock, int flags)
{
    protecc_bpf_net_request_t request = {};
    struct sock* sk = NULL;
    __u64 cgroupId;
    __u16 family, type;
    __u32 protocol;
    __u16 port = 0;
    (void)newsock;
    (void)flags;

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
    protecc_bpf_net_request_t request = {};
    struct per_cpu_data*      scratch;
    __u64                     cgroupId;
    __u16                     family, type;
    __u32                     protocol;
    struct sockaddr*          addr = NULL;
    __u32                     addrlen = 0;
    (void)size;
    
    if (ret) {
        return ret;
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
        if (__request_from_address(scratch, family, addr, addrlen, &request)) {
            __emit_deny_event_basic(cgroupId, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
            return -EACCES;
        }
        return __net_allow_request(cgroupId, &request, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
    }

    return __net_allow_request(cgroupId, &request, NET_PERM_SEND, DENY_HOOK_SOCKET_SENDMSG);
}

char LICENSE[] SEC("license") = "GPL";
