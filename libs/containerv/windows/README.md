# Windows Container Support

This directory contains the Windows implementation of the containerv library using HyperV technology.

## Overview

The Windows implementation provides container isolation using HyperV virtual machines, which provides similar isolation guarantees as Linux namespaces but using Windows-native technologies.

## Architecture

### Container Management
- **VM Creation**: Uses Windows HCS (Host Compute Service) APIs to create lightweight VMs
- **Process Isolation**: Processes run inside HyperV VMs with full isolation
- **Filesystem Isolation**: Rootfs is mounted and isolated within the VM
- **Network Isolation**: VMs have their own network stack

### Implementation Files
- `container.c` - Main container lifecycle management
- `container-options.c` - Container configuration options
- `private.h` - Internal data structures and APIs
- `rootfs/debootstrap.c` - Rootfs setup (stub for Windows)

## Requirements

### System Requirements
- Windows 10 version 1709 or later (with HyperV support)
- HyperV feature enabled
- Administrator privileges (for HyperV operations)

### Build Requirements
- Visual Studio 2019 or later, or MinGW-w64
- CMake 3.14.3 or later
- Windows SDK

## Current Status

### Implemented Features
- ✅ Container creation and destruction
- ✅ Process spawning and management
- ✅ File upload/download
- ✅ Container ID generation using Windows Crypto API
- ✅ Basic process lifecycle management

### Planned Features
- ⏳ Full HyperV VM integration using HCS APIs
- ⏳ Network isolation and configuration
- ⏳ Resource limits (CPU, memory, disk I/O)
- ⏳ Container image management
- ⏳ WSL2 integration for Linux containers on Windows

## API Compatibility

The Windows implementation maintains API compatibility with the Linux implementation, allowing the same client code to work on both platforms.

## Limitations

### Current Implementation
The current implementation is a foundational structure with HyperV integration stubs. A production implementation would require:

1. **Full HCS Integration**: Complete integration with Windows Host Compute Service
2. **WSL2 Support**: Option to use WSL2 for Linux containers
3. **Image Management**: Container image pull/push functionality
4. **Advanced Networking**: Virtual network configuration and isolation

### Platform Differences
- Windows containers require HyperV or process isolation (not available on all SKUs)
- Some Linux-specific features (like namespaces) don't have direct Windows equivalents
- Rootfs must be Windows-compatible or use WSL2 for Linux workloads

## Usage Example

```c
#include <chef/containerv.h>

// Create container options
struct containerv_options* options = containerv_options_new();
containerv_options_set_caps(options, CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL);

// Create container
struct containerv_container* container;
int status = containerv_create("C:\\containerfs", options, &container);

// Spawn process in container
struct containerv_spawn_options spawn_opts = {
    .arguments = "arg1 arg2",
    .flags = CV_SPAWN_WAIT
};
process_handle_t handle;
containerv_spawn(container, "C:\\Windows\\System32\\cmd.exe", &spawn_opts, &handle);

// Clean up
containerv_destroy(container);
containerv_options_delete(options);
```

## Building

The Windows implementation is automatically built when configuring on Windows:

```powershell
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

Or with MinGW:

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Testing

Integration tests can be run using:

```powershell
cd build
ctest -C Release
```

## Future Work

1. **HCS Integration**: Complete Windows Host Compute Service integration
2. **WSL2 Backend**: Add WSL2 as an alternative backend for Linux containers
3. **Windows Containers**: Support native Windows Server containers
4. **Networking**: Implement full network isolation and configuration
5. **Resource Management**: Add CPU, memory, and I/O limits
6. **Security**: Implement Windows-specific security features

## References

- [Windows Host Compute Service](https://docs.microsoft.com/en-us/virtualization/api/)
- [HyperV Documentation](https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/)
- [Windows Containers](https://docs.microsoft.com/en-us/virtualization/windowscontainers/)
