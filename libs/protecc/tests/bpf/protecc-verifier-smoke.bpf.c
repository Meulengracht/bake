#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include <protecc/bpf/path.h>
#include <protecc/bpf/net.h>
#include <protecc/bpf/mount.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u8[PROTECC_BPF_MAX_PROFILE_SIZE]);
} protecc_profile_map SEC(".maps");

static const __u8 g_empty_path[PROTECC_BPF_MAX_PATH] = {0};

SEC("raw_tp/sys_enter")
int protecc_verifier_smoke(struct bpf_raw_tracepoint_args *ctx)
{
    __u32                       key = 0;
    __u8*                       profile;
    __u8                        action = 0;
    __u64                       score = 0;
    protecc_bpf_net_request_t   net_req = {0};
    protecc_bpf_mount_request_t mount_req = {0};

    (void)ctx;

    profile = bpf_map_lookup_elem(&protecc_profile_map, &key);
    if (profile == NULL) {
        return 0;
    }

    if (protecc_bpf_match(profile, g_empty_path, 0u, 0u, 0u)) {
        score++;
    }

    if (protecc_bpf_match_net(profile, &net_req, &action)) {
        score++;
    }

    if (protecc_bpf_match_mount(profile, &mount_req, &action)) {
        score++;
    }

    return (int)score;
}

char LICENSE[] SEC("license") = "GPL";
