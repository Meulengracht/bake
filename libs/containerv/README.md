# Containerv - Cross-Platform Container Library

Containerv is a cross-platform container management library that provides isolated execution environments for build and runtime processes.

## Platform Support

### Linux
Uses native Linux container technologies:
- **Namespaces**: Process, network, filesystem, IPC, UTS isolation
- **Cgroups**: Resource limiting (CPU, memory, PIDs)
- **OverlayFS**: Efficient filesystem layering
- **Virtual Ethernet**: Network isolation with veth pairs

See [linux/](linux/) for implementation details.

### Windows
Windows support uses **HCS containers (true Windows containers)** with HCS "Container" compute systems (process isolation or Hyper-V isolation).

See [windows/](windows/) for implementation details.

## Windows Rootfs Contracts

### HCS container mode (true containers)

When using the HCS container backend, `BASE_ROOTFS` is expected to point at a pre-prepared **windowsfilter container folder** (i.e., a writable layer folder that contains `layerchain.json` describing its parent layers).

- Hyper-V isolation additionally requires a UtilityVM image (typically `...\<base-layer>\UtilityVM`).
- This backend uses HCS mapped directories for host bind mounts and a built-in staging folder.

#### LCOW (Linux containers on Windows)

LCOW support is being implemented using the standard **OCI-in-UVM** approach:

- Container type selection: use the HCS container backend and set container type to Linux.
- Requires a Linux utility VM (UVM) image directory plus (optionally) kernel/initrd/boot parameters.

In `cvd` (Windows host), these are currently wired via environment variables:
- `CHEF_WINDOWS_CONTAINER_TYPE=linux`
- `CHEF_LCOW_UVM_IMAGE_PATH` (required)
- `CHEF_LCOW_KERNEL_FILE` (optional; file name under `CHEF_LCOW_UVM_IMAGE_PATH`)
- `CHEF_LCOW_INITRD_FILE` (optional; file name under `CHEF_LCOW_UVM_IMAGE_PATH`)
- `CHEF_LCOW_BOOT_PARAMETERS` (optional)

Current status: LCOW compute-system bring-up is present and containerv will emit a minimal OCI spec for LCOW processes; rootfs mapping and full OCI bundle semantics are still evolving.

Notes:
- If a host rootfs directory is provided, it is mapped into the LCOW UVM at `/chef/rootfs`.
- When that rootfs mapping is present, all bind mounts (including Chef staging) are rebased under `/chef/rootfs` so they remain visible after the OCI process pivots into the container root.

## Windows Networking (HCS container mode)

For **true Windows containers** (WCOW/LCOW) using the HCS container backend, networking is managed via the Windows Host Networking Service (HNS): containerv creates an HNS endpoint on the host and attaches it to the container compute system.

### Selecting the HNS network

When `options->network.enable` is set (e.g. via `containerv_options_set_network[_ex]()`), containerv selects an HNS network primarily based on `options->network.switch_name` (set via `containerv_options_set_vm_switch()`). The selection is deterministic and uses this priority:

- Highest priority: `HnsNetwork.SwitchName == switch_name` (exact match)
- Next: `HnsNetwork.Name == switch_name` (exact match)
- Next: partial match on either `SwitchName` or `Name` (`*switch_name*`)
- Tie-break preference: networks with `Type == NAT` are preferred, then `Type == ICS`
- Final fallback: if nothing matches, the highest-scoring network is used; if there are no HNS networks at all, container networking cannot be configured

Recommendation: set `switch_name` explicitly if your host has multiple HNS networks (common on developer machines with WSL/Default Switch/NAT networks) to ensure containers attach to the intended network.

### Static IP / DNS

If `container_ip/netmask/gateway/dns` are provided, containerv will first attempt to apply these at the HNS endpoint layer (when supported by the installed `New-HnsEndpoint` cmdlets on the host). If endpoint policies are not supported/available, containerv falls back to a best-effort in-container configuration step.

### VM-backed mode (legacy)

VM-backed containers are no longer supported in containerv. Only HCS container compute systems are supported on Windows.

## Features

- **Container Lifecycle Management**: Create, start, stop, destroy containers
- **Process Management**: Spawn and control processes within containers
- **File Transfer**: Upload/download files to/from containers
- **Resource Limits**: Configure CPU, memory, and process limits (Linux)
- **Network Isolation**: Isolated network stacks per container
- **User Namespaces**: UID/GID mapping for security (Linux)
- **Security Policies**: eBPF-based syscall and filesystem access control (Linux)

## Security Policies (Linux)

Containerv provides a comprehensive security policy system for controlling what containers can do:

### Policy Types

1. **Minimal** (`CV_POLICY_MINIMAL`): For basic CLI applications
   - Essential syscalls: read, write, open, close, exit, memory management
   - Read-only access to system libraries
   - Access to basic device files (/dev/null, /dev/urandom, etc.)

2. **Build** (`CV_POLICY_BUILD`): For build environments
   - All minimal syscalls plus process creation (fork, exec, clone)
   - File manipulation (create, delete, rename, chmod)
   - IPC primitives (pipe, socketpair)

3. **Network** (`CV_POLICY_NETWORK`): For network applications
   - All minimal syscalls plus socket operations
   - Network I/O (send, recv, connect, bind)

4. **Custom** (`CV_POLICY_CUSTOM`): Build your own from scratch

### Using Policies

```c
#include <chef/containerv/policy.h>

// Create a build policy
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);

// Add custom paths with specific access modes
const char* paths[] = {"/workspace", "/tmp", NULL};
containerv_policy_add_paths(policy, paths, CV_FS_ALL);

// Add additional syscalls
const char* syscalls[] = {"setrlimit", NULL};
containerv_policy_add_syscalls(policy, syscalls);

// Apply to container
struct containerv_options* options = containerv_options_new();
containerv_options_set_policy(options, policy);
```

### Implementation

The policy system uses:
- **seccomp-bpf**: Efficient syscall filtering in the kernel
- **eBPF infrastructure**: Future integration with BPF LSM hooks for comprehensive security
- **Default-deny model**: Only explicitly allowed operations are permitted

This provides strong security boundaries without the complexity of traditional mandatory access control systems like SELinux or AppArmor.

### Seccomp Logging and Debugging

By default, the seccomp filter uses `SCMP_ACT_ERRNO(EPERM)`, which silently denies unauthorized syscalls by returning an error code. This is the most secure option for production environments as it doesn't generate audit logs or kernel messages that could reveal information about attempted syscalls.

For debugging and troubleshooting seccomp-related issues, you can enable logging mode by setting the `CONTAINERV_SECCOMP_LOG` environment variable:

```bash
# Enable seccomp logging (for debugging only)
export CONTAINERV_SECCOMP_LOG=1

# Start cvd or run bake
cvd -vv
```

When logging is enabled:
- The seccomp filter uses `SCMP_ACT_LOG` instead of `SCMP_ACT_ERRNO`
- Denied syscalls are logged to the kernel audit system (if auditd is running) or kernel log
- Logs can be viewed with:
  - `ausearch -m SECCOMP` (if auditd is running)
  - `journalctl -k | grep seccomp`
  - `dmesg | grep seccomp`

**Important**: Seccomp logging should only be enabled for debugging purposes, as it can:
- Generate significant log volume in production environments
- Potentially leak information about application behavior
- Impact performance slightly due to logging overhead

**Note**: The `SCMP_ACT_LOG` mode allows all syscalls to proceed normally after logging violations. This means syscalls that would normally be blocked are allowed to execute, making this mode unsuitable for security enforcement. Use logging mode only in development/debugging environments to discover which syscalls your application needs, then disable it for production use.

## API Overview

The API is platform-agnostic and works consistently across Linux and Windows:

```c
// Create container
struct containerv_options* options = containerv_options_new();
struct containerv_container* container;
containerv_create("/path/to/rootfs", options, &container);

// Spawn process
struct containerv_spawn_options spawn_opts = { .flags = CV_SPAWN_WAIT };
process_handle_t pid;
containerv_spawn(container, "/bin/sh", &spawn_opts, &pid);

// Transfer files
const char* host_paths[] = {"/local/file"};
const char* container_paths[] = {"/container/file"};
containerv_upload(container, host_paths, container_paths, 1);

// Cleanup
containerv_destroy(container);
containerv_options_delete(options);
```

## Building

The library automatically selects the correct implementation based on the target platform:

### Linux
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows
```powershell
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

## Usage in Chef

The containerv library is used by:
- **cvd (Container Daemon)**: Manages containers for build isolation
- **bake**: Local build tool that uses containers for reproducible builds
- **cook**: Remote build server that uses containers for build environments

## Platform Differences

| Feature | Linux | Windows |
|---------|-------|---------|
| Isolation Mechanism | Namespaces | HCS Containers |
| Resource Limits | Cgroups | Job Objects / HCS |
| Filesystem | OverlayFS | windowsfilter layers |
| Network | Veth pairs | HNS |
| Performance | Native | Near-native |
| Setup | Kernel features | HyperV feature |

## Requirements

### Linux
- Linux kernel 4.4+ with namespace support
- Root privileges or user namespaces enabled
- Cgroups v2 support (optional, for resource limits)
- CAP_NET_ADMIN capability (for network isolation)

### Windows
- Windows 10 version 1709 or later
- HyperV feature enabled
- Administrator privileges
- Windows SDK for building

## Architecture

```
containerv/
├── include/          # Public API headers
├── shared.c          # Platform-independent code
├── linux/            # Linux implementation
│   ├── container.c   # Container lifecycle
│   ├── cgroups.c     # Resource management
│   ├── network.c     # Network isolation
│   └── ...
└── windows/          # Windows implementation
    ├── container.c   # Container lifecycle
    └── ...
```

## Contributing

When contributing platform-specific features:
1. Maintain API compatibility across platforms
2. Add platform-specific implementations in the respective directories
3. Update documentation for platform differences
4. Add tests for the new functionality

## License

GNU General Public License v3.0 - See LICENSE file for details

## TODO

serve-exec on Windows
Implement Windows execution version of main.c as main_win32.c (currently Linux only).
Rename main.c to main_linux.c
Ensure it can join container (HCS) and spawn inside LCOW/WCOW, or switch to using cvd spawn for Windows.

Windows wrapper execution flow
Validate that Windows wrappers (.cmd) invoke serve-exec.exe correctly and pass arguments, and ensure container path mapping is correct for both LCOW (/chef/rootfs) and WCOW (C:).
Touch points: generate-wrappers.c:20-160, paths.c:40-136.

Windows path normalization for container paths
Normalize container command paths (C:\ vs /) when passing to serve-exec / spawn, especially for LCOW rootfs path rebasing.
Touch points: generate-wrappers.c:90-150, hcs.c:360-520.

WCOW parent layer handling
Ensure parent layer chain is fully validated and propagated for WCOW packages (including windowsfilter import + UtilityVM discovery).
Touch points: layers.c:520-760, container.c:1672-1748.

Windows networking validation
Confirm HNS endpoint policies are applied for LCOW and WCOW, and verify fallback behavior for static IP/DNS.
Touch points: network.c.
