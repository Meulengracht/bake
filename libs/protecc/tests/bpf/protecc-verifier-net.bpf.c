#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

/* net.h relies on these protocol/family values but only headers available to
 * BPF compilation should be used here (no stdint/stddef collisions). */
#define PROTECC_NET_PROTOCOL_UNIX 3u
#define PROTECC_NET_FAMILY_UNIX   3u
#include <protecc/bpf/net.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u8[PROTECC_BPF_MAX_PROFILE_SIZE]);
} protecc_profile_map SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int protecc_verifier_net(void *ctx)
{
    __u32                     key = 0;
    __u8*                     profile;
    __u8                      action = 0;
    protecc_bpf_net_request_t req = {0};

    (void)ctx;

    profile = bpf_map_lookup_elem(&protecc_profile_map, &key);
    if (profile == NULL) {
        return 0;
    }

    return protecc_bpf_match_net(profile, &req, &action) ? 1 : 0;
}

char LICENSE[] SEC("license") = "GPL";
