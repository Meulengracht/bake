#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

#include <protecc/bpf/path.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u8[PROTECC_BPF_MAX_PROFILE_SIZE]);
} protecc_profile_map SEC(".maps");

static const __u8 g_empty_path[PROTECC_BPF_MAX_PATH] = {0};

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int protecc_verifier_path(void *ctx)
{
    __u32 key = 0;
    __u8* profile;

    (void)ctx;

    profile = bpf_map_lookup_elem(&protecc_profile_map, &key);
    if (profile == NULL) {
        return 0;
    }

    return protecc_bpf_match(profile, g_empty_path, 0u, 0u, 0u) ? 1 : 0;
}

char LICENSE[] SEC("license") = "GPL";
