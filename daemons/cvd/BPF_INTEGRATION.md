# BPF Manager for Container Security

## Overview

The containerv library includes a centralized BPF manager that loads and manages eBPF LSM programs for container security policy enforcement. This provides fine-grained filesystem access control at the kernel level.

The BPF manager is part of the containerv library (`libs/containerv/linux/bpf-manager.c`) and can be used by any application (such as the cvd daemon) to provide centralized eBPF enforcement.

## Architecture

```
┌──────────────────────────────────────────┐
│      Application (e.g., cvd daemon)       │
│  Uses containerv_bpf_manager_*() API     │
└──────────────────────────────────────────┘
         ↓
┌──────────────────────────────────────────┐
│    libs/containerv/linux/bpf-manager.c    │
│  ┌────────────────────────────────┐      │
│  │     BPF Manager                 │      │
│  │  - Load BPF LSM programs once   │      │
│  │  - Pin to /sys/fs/bpf/cvd       │      │
│  │  - Manage policy_map globally   │      │
│  └────────────────────────────────┘      │
│          ↓                  ↓             │
│    Container 1       Container 2          │
│    (cgroup_id=1)     (cgroup_id=2)        │
└──────────────────────────────────────────┘
         ↓                  ↓
┌────────────────────────────────────────────┐
│      BPF LSM Hook (file_open)              │
│  Policy Map: (cgroup_id, dev, ino) → mask  │
└────────────────────────────────────────────┘
```

## API

The BPF manager provides a clean API in `chef/containerv/bpf_manager.h`:

```c
// Initialize BPF manager (call once at startup)
int containerv_bpf_manager_initialize(void);

// Check if BPF LSM is available
int containerv_bpf_manager_is_available(void);

// Populate policy for a container
int containerv_bpf_manager_populate_policy(
    const char* container_id,
    const char* rootfs_path,  // Must not be NULL
    struct containerv_policy* policy
);

// Clean up policy for a container
int containerv_bpf_manager_cleanup_policy(const char* container_id);

// Shutdown BPF manager (call at exit)
void containerv_bpf_manager_shutdown(void);
```

## Lifecycle

### Daemon Startup

1. **Initialization** (`containerv_bpf_manager_initialize`)
   - Check if BPF LSM is available in kernel
   - Load BPF LSM programs (fs-lsm.bpf.o)
   - Attach programs to LSM hooks
   - Pin policy_map to `/sys/fs/bpf/cvd/policy_map`
   - Log initialization status

2. **Fallback Handling**
   - If BPF LSM unavailable: Log info and continue
   - Containers will use seccomp-only enforcement
   - No application code changes needed

### Container Create

1. **Container Creation** (in cvd daemon or other application)
   - Create container with containerv
   - Compose rootfs layers
   - Get cgroup ID for container

2. **Policy Population** (`containerv_bpf_manager_populate_policy`)
   - Resolve configured paths to (dev, ino) in container's rootfs
   - Populate BPF policy_map with entries:
     - Key: (cgroup_id, dev, ino)
     - Value: allow_mask (read/write/exec permissions)
   - Log number of policy entries added

### Container Destroy

1. **Policy Cleanup** (`containerv_bpf_manager_cleanup_policy`)
   - Iterate through policy_map using BPF_MAP_GET_NEXT_KEY
   - Remove policy entries for container's cgroup_id
   - Log number of entries deleted

2. **Container Destruction**
   - Destroy container resources
   - Clean up layer context

### Daemon Shutdown

1. **Cleanup** (`containerv_bpf_manager_shutdown`)
   - Unpin policy_map from `/sys/fs/bpf/cvd/policy_map`
   - Destroy BPF skeleton (detaches programs)
   - Log shutdown status

## BPF Map Layout

### policy_map Structure

**Type**: `BPF_MAP_TYPE_HASH`

**Key**: `struct policy_key`
```c
struct policy_key {
    __u64 cgroup_id;  // Container's cgroup inode number
    __u64 dev;        // File's device number
    __u64 ino;        // File's inode number
};
```

**Value**: `struct policy_value`
```c
struct policy_value {
    __u32 allow_mask;  // Permission bits: 0x1=READ, 0x2=WRITE, 0x4=EXEC
};
```

**Max Entries**: 10240

### Example Entries

Container with cgroup_id=12345, allowed to read/execute `/bin/bash`:
```
Key:   {cgroup_id: 12345, dev: 2049, ino: 67890}
Value: {allow_mask: 0x5}  // READ | EXEC
```

## Kernel Requirements

- **Minimum Kernel**: Linux 5.7+
- **Required Config**:
  ```
  CONFIG_BPF=y
  CONFIG_BPF_SYSCALL=y
  CONFIG_BPF_LSM=y
  ```
- **LSM Configuration**: Add "bpf" to kernel boot parameter
  ```bash
  # /etc/default/grub
  GRUB_CMDLINE_LINUX="lsm=...,bpf"
  sudo update-grub && sudo reboot
  ```

## Logging and Instrumentation

### Startup Logging

```
[INFO] cvd: Initializing BPF manager for container security...
[DEBUG] cvd: bpf_manager: BPF skeleton opened
[DEBUG] cvd: bpf_manager: BPF programs loaded
[INFO] cvd: bpf_manager: BPF LSM programs attached successfully
[DEBUG] cvd: bpf_manager: policy map pinned to /sys/fs/bpf/cvd/policy_map
[INFO] cvd: bpf_manager: initialization complete, BPF LSM enforcement active
[INFO] cvd: BPF LSM enforcement is active
```

### Fallback Logging

```
[DEBUG] cvd: bpf_manager: BPF LSM not enabled in kernel (add 'bpf' to LSM list)
[INFO] cvd: bpf_manager: BPF LSM not available, using seccomp fallback
[INFO] cvd: BPF LSM not available, containers will use seccomp fallback
```

### Container Lifecycle Logging

```
[DEBUG] cvd: cvd_create: populating BPF policy for container abc123
[DEBUG] cvd: bpf_manager: populating policy for container abc123 (cgroup_id=12345)
[TRACE] cvd: bpf_manager: added policy for /bin (dev=2049, ino=1234, mask=0x5)
[DEBUG] cvd: bpf_manager: populated 15 policy entries for container abc123

[DEBUG] cvd: cvd_destroy: cleaning up BPF policy for container abc123
[DEBUG] cvd: bpf_manager: policy cleanup complete for container abc123
```

### Error Logging

```
[ERROR] cvd: bpf_manager: failed to open BPF skeleton
[ERROR] cvd: bpf_manager: failed to load BPF skeleton: -1
[ERROR] cvd: bpf_manager: failed to attach BPF LSM program: -13
[WARNING] cvd: cvd_create: failed to populate BPF policy for abc123
```

## Integration with containerv Library

The containerv library automatically detects if BPF programs are managed centrally by cvd:

1. Check for pinned policy_map at `/sys/fs/bpf/cvd/policy_map`
2. If found: Use global BPF enforcement (cvd manages policies)
3. If not found: Load BPF programs locally (standalone mode)

This maintains backward compatibility for non-cvd use cases.

## Debugging

### Check BPF LSM Status

```bash
# Verify BPF LSM is active
cat /sys/kernel/security/lsm | grep bpf

# List loaded BPF programs
sudo bpftool prog list | grep lsm

# List BPF maps
sudo bpftool map list

# Show pinned objects
ls -la /sys/fs/bpf/cvd/
```

### Dump Policy Map

```bash
# Dump all policy entries
sudo bpftool map dump pinned /sys/fs/bpf/cvd/policy_map

# Example output:
key:
00 00 00 00 00 00 30 39  // cgroup_id: 12345
01 08 00 00 00 00 00 00  // dev: 2049
52 0a 01 00 00 00 00 00  // ino: 67890
value:
05 00 00 00              // allow_mask: 0x5 (READ|EXEC)
```

### Monitor cvd Logs

```bash
# Tail cvd daemon logs
tail -f /var/log/chef/cvd.log

# Or check via vlog output
journalctl -u cvd -f
```

## Fallback Behavior

When BPF LSM is not available:

1. cvd daemon logs info message about fallback
2. Containers continue to use seccomp-bpf for syscall filtering
3. Path-based access control is not enforced
4. No functional changes to container behavior
5. Graceful degradation ensures system stability

## Security Considerations

### Advantages of Centralized BPF Management

1. **Single Program Load**: BPF programs loaded once, shared across all containers
2. **Consistent Enforcement**: All containers use the same LSM hooks
3. **Lower Resource Usage**: No per-container BPF program overhead
4. **Centralized Policy**: cvd daemon has full visibility of all policies

### Security Model

- **Per-Container Isolation**: Policies keyed by cgroup_id
- **Inode-Based Enforcement**: Immune to symlink attacks
- **Kernel-Level Enforcement**: Cannot be bypassed from userspace
- **Default-Deny**: Files without policy entries are denied

## Future Enhancements

1. **Dynamic Policy Updates**: Update policies without container restart
2. **Policy Configuration API**: Client-side policy specification
3. **Audit Logging**: Record policy violations
4. **Performance Metrics**: Track map size and lookup performance
5. **Wildcard Path Support**: Pattern-based policy rules

## References

- [BPF LSM Documentation](https://www.kernel.org/doc/html/latest/bpf/bpf_lsm.html)
- [Container Security Policies](../../docs/CONTAINER_SECURITY_POLICIES.md)
- [BPF LSM Implementation](../../docs/BPF_LSM_IMPLEMENTATION.md)
