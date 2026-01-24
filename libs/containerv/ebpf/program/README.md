# BPF LSM Programs for Container Filesystem Enforcement

This directory contains BPF LSM programs for enforcing container-specific filesystem access policies.

## Overview

The BPF programs in this directory provide kernel-level enforcement of filesystem access restrictions for containerized processes. They hook into the Linux Security Module (LSM) framework to intercept file operations and enforce per-container policies based on cgroup identity and inode numbers.

## Files

- **fs_lsm.bpf.c**: BPF LSM program that hooks `file_open` to enforce read/write deny rules
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

The BPF program uses a hash map to store deny rules:

```c
struct policy_key {
    __u64 cgroup_id;  // Container's cgroup inode number
    __u64 dev;        // Device number of the filesystem
    __u64 ino;        // Inode number of the file
};

struct policy_value {
    __u32 deny_mask;  // Bitmask: 0x1=READ, 0x2=WRITE, 0x4=EXEC
};
```

### Enforcement Flow

1. **Container Creation**:
   - Container is placed in a cgroup (e.g., `/sys/fs/cgroup/container-01`)
   - Policy deny rules are added for specific paths
   - Paths are resolved to (dev, ino) tuples
   - Map entries are created: `(cgroup_id, dev, ino) → deny_mask`

2. **File Open**:
   - Process in container opens a file
   - BPF LSM `file_open` hook is triggered
   - Program extracts:
     - Cgroup ID from calling process
     - Device and inode from file being opened
   - Looks up `(cgroup_id, dev, ino)` in policy map
   - If found and requested operation is denied: returns -EACCES
   - Otherwise: allows the operation (default-allow)

3. **Container Cleanup**:
   - Map entries for the container are removed
   - BPF programs remain loaded for other containers

## Current Implementation Status

### Implemented ✅
- BPF program structure and policy map
- Build system integration
- Skeleton generation
- Policy map key/value definitions
- Cgroup ID-based isolation

### In Progress 🚧
- Full vmlinux.h generation for accessing kernel structures
- Actual enforcement logic in BPF program (currently placeholder)
- BPF program loading and attaching in policy-ebpf.c
- Runtime map population

### TODO 📋
- Complete kernel struct access via vmlinux.h or BTF
- Implement exec permission enforcement
- Add audit logging support
- Performance counters and statistics
- Dynamic policy updates

## Limitations

### First Slice Limitations

This is the initial implementation focusing on infrastructure:

1. **Placeholder Enforcement**: The BPF program compiles but doesn't yet enforce policies (requires vmlinux.h for accessing `struct file` and `struct inode`)

2. **No Runtime Loading**: The skeleton is generated but not yet used to load/attach programs (coming in next commits)

3. **Read/Write Only**: Exec enforcement will be added in a future update

### Workarounds

The system is designed to gracefully handle missing BPF LSM:
- Falls back to seccomp for syscall filtering
- Logs warning when BPF LSM is unavailable
- No application code changes needed

## Future Enhancements

### Near Term
- Complete vmlinux.h generation for CO-RE
- Wire up BPF program loading in policy-ebpf.c
- Add per-container map population
- Test enforcement with real workloads

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
