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
Windows support now has two distinct backends:

- **HCS containers (true Windows containers)**: Uses HCS "Container" compute systems (process isolation or Hyper-V isolation).
- **VM-backed (legacy)**: Uses an HCS "VirtualMachine" compute system (one VM per container) and boots a VHDX.

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

- `CHEF_WINDOWS_RUNTIME=container` (default)
- `CHEF_WINDOWS_CONTAINER_TYPE=linux`
- `CHEF_LCOW_UVM_IMAGE_PATH` (required)
- `CHEF_LCOW_KERNEL_FILE` (optional; file name under `CHEF_LCOW_UVM_IMAGE_PATH`)
- `CHEF_LCOW_INITRD_FILE` (optional; file name under `CHEF_LCOW_UVM_IMAGE_PATH`)
- `CHEF_LCOW_BOOT_PARAMETERS` (optional)

Current status: LCOW compute-system bring-up is present; OCI-spec/rootfs plumbing is the next step.

### VM-backed mode (legacy)

## VM Disk Contract (Windows Host, VM-backed legacy)

When running VM-backed containers on a Windows host, containerv attaches a single virtual disk to the VM at runtime: `%runtime_dir%\container.vhdx`.

This disk contract applies only to **Windows VM-backed** containers. On **Linux hosts**, containerv uses native Linux container mechanisms and expects a normal root filesystem directory tree (not a virtual disk image).

The *composed rootfs directory* (from layers) is used as the source material to produce that disk. The expected contract differs by guest OS:

- **Linux guests (WSL-based rootfs)**
   - Preferred: a prebuilt bootable disk at `<rootfs>\container.vhdx`.
   - Supported: an ext4 rootfs disk at `<rootfs>\ext4.vhdx` (e.g. from WSL2 import or built on Linux; will be copied into `%runtime_dir%\container.vhdx`).
   - Note: containerv does not currently generate an ext4 boot disk from a directory tree on Windows.

- **Windows guests (native rootfs)**
   - Required: a prebuilt bootable disk at `<rootfs>\container.vhdx`.
   - Note: a directory tree populated into a single NTFS partition is not sufficient for UEFI boot (no ESP/BCD).

### pid1d placement

containerv expects to start `pid1d` inside the guest as the initial supervisor for guest actions:
- Windows guest: `C:\pid1d.exe`
- Linux guest: `/usr/bin/pid1d`

Your base/rootfs pack should ensure the correct binary exists at those paths.

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
| Isolation Mechanism | Namespaces | HyperV VMs |
| Resource Limits | Cgroups | VM Configuration |
| Filesystem | OverlayFS | VM Disk |
| Network | Veth pairs | VM Network |
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
