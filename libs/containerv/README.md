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
Uses Windows HyperV technology:
- **HyperV VMs**: Lightweight virtual machines for isolation
- **Process Isolation**: Windows process isolation within VMs
- **Filesystem Isolation**: Isolated rootfs per container
- **Network Isolation**: VM-level network stack isolation

See [windows/](windows/) for implementation details.

## Features

- **Container Lifecycle Management**: Create, start, stop, destroy containers
- **Process Management**: Spawn and control processes within containers
- **File Transfer**: Upload/download files to/from containers
- **Resource Limits**: Configure CPU, memory, and process limits (Linux)
- **Network Isolation**: Isolated network stacks per container
- **User Namespaces**: UID/GID mapping for security (Linux)

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
