# BPF LSM Programs for Container Filesystem and Network Enforcement

This directory contains BPF LSM programs for enforcing container-specific filesystem and network access policies.

## Overview

The BPF programs in this directory provide kernel-level enforcement of filesystem and network access restrictions for containerized processes. They hook into the Linux Security Module (LSM) framework to intercept file and socket operations and enforce per-container policies based on cgroup identity and inode numbers.

## Files

- **fs-lsm.bpf.c**: BPF LSM program that hooks file operations for path mediation
- **net-lsm.bpf.c**: BPF LSM program that hooks socket operations for tuple-based network mediation
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

### Policy Map

The BPF program uses a hash map to store allow rules:

```c
struct policy_key {
    __u64 cgroup_id;  // Container's cgroup inode number
    __u64 dev;        // Device number of the filesystem
    __u64 ino;        // Inode number of the file
};

struct policy_value {
   __u32 allow_mask;  // Bitmask: 0x1=READ, 0x2=WRITE, 0x4=EXEC
};
```

### Enforcement Flow

1. **Container Creation**:
   - Container is placed in a cgroup (e.g., `/sys/fs/cgroup/container-01`)
   - Policy allow rules are added for specific paths
   - Paths are resolved to (dev, ino) tuples
   - Map entries are created: `(cgroup_id, dev, ino) → allow_mask`

2. **File Open**:
   - Process in container opens a file
   - BPF LSM `file_open` hook is triggered
   - Program extracts:
     - Cgroup ID from calling process
     - Device and inode from file being opened
   - Looks up `(cgroup_id, dev, ino)` in policy map
   - If found and requested operation is allowed: returns 0
   - Otherwise: denies the operation (default-deny)

3. **Container Cleanup**:
   - Map entries for the container are removed
   - BPF programs remain loaded for other containers

## Current Implementation Status

### Implemented ✅
- BPF LSM program loading/attaching (fs + net)
- Runtime map population and per-container cleanup
- Allow-mask enforcement for read/write/exec and network operations
- Deny event ring buffer logging

### In Progress 🚧
- Audit/telemetry expansion
- Extended pattern support for basename matching

## Limitations

- Basename matching supports a restricted pattern subset to stay verifier-friendly.
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

# Dump policy map
sudo bpftool map dump name policy_map
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
