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
Windows support has two guest backends. **WCOW** uses HCS "Container" compute systems with process or Hyper-V isolation. **LCOW** hosts OCI containers inside a Linux utility VM and executes them through GCS. Guest execution never falls back to running the requested workload on the Windows host.

See [windows/](windows/) for implementation details.

## Windows Rootfs Contracts

### HCS container mode (true containers)

When using the HCS container backend, `BASE_ROOTFS` is expected to point at a pre-prepared **windowsfilter container folder** (i.e., a writable layer folder that contains `layerchain.json` describing its parent layers).

- Hyper-V isolation additionally requires a UtilityVM image (typically `...\<base-layer>\UtilityVM`).
- This backend uses HCS mapped directories for host bind mounts and a built-in staging folder.

#### LCOW (Linux containers on Windows)

LCOW uses the standard **OCI-in-UVM** approach:

- Container type selection: use the HCS container backend and set container type to Linux.
- Requires a Linux utility VM (UVM) image directory plus (optionally) kernel/initrd/boot parameters.

In `cvd` (Windows host), these are configured once in `cvd.json` or via `cvctl`.

Preferred setup flows:
- Produce or normalize a bundle offline: `mkuvm normalize --source <raw-dir> --output <bundle-dir>`
- Assemble a bundle offline from an explicit base archive + kernel: `mkuvm construct --base-archive <base.tar.gz> --kernel <kernel> --output <bundle-dir>`
- Import a locally built bundle into Chef's cache: `cvctl uvm import <bundle-dir>`
- Import a packaged os-base bundle: `cvctl uvm import-pack <bundle.pack>`
- Fetch a prebuilt bundle archive into Chef's cache: `cvctl uvm fetch <archive-url>`

The fetch flow uses host-native command line tools (`curl` and `tar`) in the same spirit as the Linux rootfs fetch path; it no longer relies on PowerShell.

These commands validate either the legacy `uvm.vhdx` layout or the newer boot-files layout, stage the bundle under Chef's cache, and auto-detect optional `kernel`, `initrd`, and `boot_parameters` bundle files.

Chef no longer constructs LCOW bundles inside the runtime tools. Bundle construction and normalization should happen offline, and the resulting directory or pack should then be imported.

The normalized bundle layout is usable in both places Chef needs it today:
- direct `cvd` build-container configuration via `cvctl uvm import <bundle-dir>`
- future packaging/repacking flows (zip/VaFS) that need a stable LCOW bundle directory to ingest

Low-level config keys remain available when needed:
- `lcow.uvm-image-path` or `lcow.uvm-url` (one is required)
- `lcow.kernel-file` (optional; file name under the configured UVM image path)
- `lcow.initrd-file` (optional; file name under the configured UVM image path)
- `lcow.boot-parameters` (optional)

Examples:

```powershell
 mkuvm construct --base-archive "C:\\inputs\\base.tar.gz" --kernel "C:\\inputs\\vmlinux" --output "C:\\temp\\lcow-uvm" --archive "C:\\temp\\lcow-uvm.tar.gz"
 mkuvm normalize --source "C:\\raw\\lcow-uvm" --output "C:\\temp\\lcow-uvm"
cvctl uvm import "C:\\ProgramData\\chef\\lcow\\uvm"
cvctl uvm import-pack "C:\\packages\\lcow-uvm.pack"
cvctl uvm fetch "https://example.invalid/lcow-uvm.tar.gz"
```

Or in `cvd.json`:

```json
{
   "api-address": {
      "type": "inet4",
      "address": "127.0.0.1",
      "port": 51003
   },
   "lcow": {
      "uvm-image-path": "C:/ProgramData/chef/lcow/uvm",
      "kernel-file": "kernel",
      "initrd-file": "initrd.img"
   }
}
```

LCOW seeds a persistent ext4 rootfs from the prepared host image and emits an OCI spec for each hosted process. On the schema2 path, HCS owns the utility VM while direct GCS requests create and execute hosted OCI containers. Direct GCS stdio pipes are not implemented, so every LCOW process redirects stdout and stderr to `/chef/staging/lcow-stdio-N.log` and reads stdin from `/dev/null`. Capture is unconditional and requires no environment setting. Logs are stored under the host's temporary `containerv-*` runtime directory in `staging/` and retained after container destruction; they must be removed when no longer needed. Live output forwarding and interactive stdin are not supported by this file-backed capture.

Notes:
- The host image is mapped into the UVM at `/chef/rootfs` for initial seeding, resolver updates, and host-share mount points. Workloads use `/run/chef-storage/rootfs` on ext4 as their OCI root.
- Host directory layers (including project, store, and staging) remain Plan9 shares and explicit OCI bind mounts. Project contents are never copied as part of source preparation.

### LCOW Native Storage

The cached host rootfs owns `.containerv-lcow/rootfs.vhdx`, a dynamically allocated disk with a 64 GiB capacity. The disk contains the writable Linux rootfs, including `/chef/source`, build/install trees, toolchains, and package-manager changes. It is shared by successive hosted processes and reused after utility VM recreation. An exclusive host lock prevents two containers from writing the same cached disk concurrently; callers needing concurrency must use separate rootfs caches.

On first initialization, containerv formats its new disk as ext4 and seeds it from the host image **before** mounting any project/store shares. The internal `.containerv-lcow` directory is excluded. `format.pending` permits retrying an interrupted first initialization; it is removed before any workload can run. Once published, an existing disk is never reformatted automatically. A failed mount aborts creation and preserves the disk for diagnosis. Changes made directly to the host seed after initialization do not update the native rootfs; use guest upload operations or a new rootfs cache instead.

The disk lives under the existing rootfs cache so deleting that cache also removes its native storage. Container destruction flushes the disk before stopping the VM and retains it for incremental builds. Temporary UVMs and staging logs keep their existing lifecycle.

The utility VM must provide Hyper-V SCSI, SCSI disk, and ext4 kernel support, `/bin/busybox`, and `mkfs.ext4` with its shared libraries. `tests/system/cases/build-lcow-arm64.ps1` includes these userspace tools through `lcow/repack-initrd.sh`. Older UVM bundles need rebuilding. Startup probes symlink creation on the mounted disk and treats storage and required share failures as fatal.

`bakectl source` creates a source alias on native writable storage pointing into the shared project. There is no compile-time Windows switch or silent copy fallback. This keeps host edits visible and preserves the existing read-only project semantics. Source trees already containing links still depend on the host share's ability to represent those links.

Validation: `containerv_windows_preflight_test` runs without Hyper-V and checks rejection of unenforced resource limits, disk creation/reuse, exclusive locking, and exclusion of disk state from image clones. Run `containerv_lcow_storage_test.exe <uvm-directory> <static-linux-busybox>` from an elevated terminal on an LCOW-capable host for cold/warm disk initialization, cross-command and cross-VM persistence, symlinks, executable permissions, read-only shares, and live host edits. `platform_copydir_test` on Linux covers relative/dangling/cyclic links, modes, and rejection of unsupported special files. The guest script can also be tested independently with real ext4 disks in WSL: `sudo unshare --mount --propagation private bash tests/system/cases/lcow/test-native-storage.sh <initrd> libs/containerv/windows/lcow-storage.sh`.

### Runtime Ownership

- `windows/container.c` coordinates container creation, process tracking, policy ownership, and destruction. WCOW preparation owns its temporary parent-layer list and UtilityVM path; LCOW preparation owns its temporary OCI bundle paths.
- `windows/hcs.c` owns HCS operations and configuration. Process creation dispatches to separate WCOW HCS and LCOW GCS paths; WCOW security-field retries do not apply to LCOW.
- `windows/gcs-bridge.c` owns framed GCS transport; `windows/gcs-process.c` owns guest requests. `common/oci-bundle.c` and `common/oci-spec.c` provide filesystem preparation and OCI serialization without introducing another dependency.
- Successful creation transfers the policy from options to the container. The layer context remains borrowed and must stay valid while the container uses it.
- Destruction releases tracked process resources, destroys the compute system, and then stops console capture before freeing container state. LCOW process tokens are heap objects, not waitable Windows process handles.

### LCOW Resolver Ownership

Without explicit DNS settings, OCI preparation preserves the image's existing `/etc/resolv.conf`; if it is missing, it creates an empty file. Explicit DNS settings replace that file using the existing space/comma/semicolon-separated server list parser.

LCOW then seeds `/chef/resolv.conf` from the prepared resolver. The OCI spec bind-mounts this backing file read-only at `/etc/resolv.conf`, because GCS otherwise generates an empty resolver for these host-networked containers. The utility VM's DHCP lease handler updates the backing file with lease-provided DNS, including on renewal.

There is no additional Windows-specific hardcoded public DNS address. The image resolver is a fallback, not a guarantee that its servers are reachable from the selected HNS network. DHCP can replace even an explicitly seeded resolver; a persistent explicit-DNS override would require coordination with the guest lease handler and is not currently guaranteed.

### Enforcement Limitations

The Windows runtime still creates a host Job Object, but HCS guest processes are not assigned to it. Explicit memory, CPU, process-count, and I/O limit requests are rejected with `ENOTSUP` at creation rather than silently accepted. Host Job Object security settings must not be treated as guest enforcement. Guest-enforced resource controls remain future work.

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
└── windows/          # Windows implementation (HCS)
    ├── container.c   # Container lifecycle, process tracking, file transfer
    ├── hcs.c         # HCS API, compute system documents, LCOW GCS bridge
    ├── layers.c      # windowsfilter/LCOW rootfs composition
    ├── network.c     # HNS endpoints
    ├── volume-manager.c # Plan9/mapped-directory shares for host layers
    ├── security.c    # VM group ACLs and job object restrictions
    └── utils.c       # Shared helpers (UTF-8/UTF-16 conversion)
```

## Contributing

When contributing platform-specific features:
1. Maintain API compatibility across platforms
2. Add platform-specific implementations in the respective directories
3. Update documentation for platform differences
4. Add tests for the new functionality

## License

GNU General Public License v3.0 - See LICENSE file for details
