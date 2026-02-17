# BPF LSM Programs for Container Filesystem and Network Enforcement

This directory contains BPF LSM programs for enforcing container-specific filesystem and network access policies.

## Overview

The BPF programs in this directory provide kernel-level enforcement of filesystem and network access restrictions for containerized processes. They hook into the Linux Security Module (LSM) framework to intercept file and socket operations and enforce per-container policies based on cgroup identity and per-container policy maps.

## Files

- **fs-lsm.bpf.c**: BPF LSM program that hooks file operations and evaluates paths against a per-container DFA profile
- **net-lsm.bpf.c**: BPF LSM program that hooks socket operations and evaluates tuples against per-container network maps
- **CMakeLists.txt**: Build system for compiling BPF programs and generating skeletons

## Requirements

### Build Requirements

- **clang**: LLVM C compiler for BPF bytecode generation
- **bpftool**: Tool for BPF program inspection and skeleton generation
- **libbpf-dev**: BPF CO-RE (Compile Once - Run Everywhere) support
- **Linux headers**: Kernel headers for BPF helper definitions

Install on Ubuntu/Debian:
```bash
sudo apt-get install clang libbpf-dev bpftool linux-headers-$(uname -r)
```

### Runtime Requirements

- **Kernel 5.7+**: BPF LSM support
- **CONFIG_BPF_LSM=y**: Kernel compiled with BPF LSM support
- **LSM list**: "bpf" must be in `/sys/kernel/security/lsm`

## Building

BPF programs are automatically built when you build the containerv library:

```bash
cmake -B build
cmake --build build
```

The build process:
1. Compiles `fs_lsm.bpf.c` to BPF bytecode (`fs_lsm.bpf.o`)
2. Generates a C skeleton header (`fs_lsm.skel.h`) using bpftool
3. Includes the skeleton in the containerv library build

If clang or bpftool are not found, the build continues without BPF programs (seccomp fallback only).

## Architecture

### Policy Maps

Filesystem enforcement uses a serialized DFA profile per cgroup:

```c
struct profile_value {
   __u32 size;
   __u8  data[PROTECC_BPF_MAX_PROFILE_SIZE];
};
```

Network enforcement uses three maps keyed by cgroup + socket metadata:

```c
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
   __u8  addr[16];
};

struct net_unix_key {
   __u64 cgroup_id;
   __u32 type;
   __u32 protocol;
   char  path[108];
};

struct net_policy_value {
   __u32 allow_mask;  // Bitmask: CREATE/BIND/CONNECT/LISTEN/ACCEPT/SEND
};
```

### Enforcement Flow

1. **Container Creation**:
   - Container is placed in a cgroup (e.g., `/sys/fs/cgroup/container-01`)
   - Filesystem policy is compiled into a DFA profile and stored in `profile_map`
   - Network policy is stored in the net maps using cgroup + tuple keys

2. **Filesystem Access**:
   - Process in container performs a file operation
   - BPF LSM hook resolves a path and evaluates it against the DFA profile
   - If the DFA indicates a match for the required access: allow
   - If not: deny and emit a ring buffer event

3. **Network Access**:
   - Process in container performs a socket operation
   - BPF LSM hook builds a key (create/tuple/unix) and looks up policy
   - If the allow mask satisfies the required permission: allow
   - If not: deny and emit a ring buffer event

4. **Container Cleanup**:
   - Map entries for the container are removed
   - BPF programs remain loaded for other containers

### Default Behavior

- If a cgroup has no filesystem profile, filesystem access is allowed.
- If a cgroup has no network policy entry, the operation is allowed.

## Limitations

- DFA matching uses bounded loops and fixed-size buffers to satisfy verifier constraints.
- Enforcement requires BPF LSM support; when unavailable, the system falls back to seccomp filtering.

## Future Enhancements

### Near Term
- Add policy-driven telemetry and counters
- Expand test coverage for fs/net enforcement

### Long Term
- Exec permission enforcement
- Wildcard pattern matching for paths
- Audit event generation
- Performance profiling hooks
- Dynamic policy updates without container restart

## Debugging

### Check BPF LSM Availability

```bash
# Check kernel config
zgrep CONFIG_BPF_LSM /boot/config-$(uname -r)

# Check LSM list
cat /sys/kernel/security/lsm

# List loaded BPF programs
sudo bpftool prog list

# Dump policy maps
sudo bpftool map dump name profile_map
sudo bpftool map dump name net_create_map
sudo bpftool map dump name net_tuple_map
sudo bpftool map dump name net_unix_map
```

### Enable BPF LSM

Add to kernel command line (requires reboot):
```bash
# /etc/default/grub
GRUB_CMDLINE_LINUX="lsm=lockdown,capability,yama,apparmor,bpf"

sudo update-grub
sudo reboot
```

### Verify Build

```bash
# Check if BPF programs were built
ls -la build/libs/containerv/linux/bpf/bpf/

# Should see:
# fs_lsm.bpf.o      - Compiled BPF bytecode
# fs_lsm.skel.h     - Generated skeleton header
```

## References

- [BPF LSM Documentation](https://www.kernel.org/doc/html/latest/bpf/bpf_lsm.html)
- [libbpf Documentation](https://libbpf.readthedocs.io/)
- [BPF CO-RE](https://nakryiko.com/posts/bpf-portability-and-co-re/)
- [bpftool Man Page](https://man7.org/linux/man-pages/man8/bpftool.8.html)
