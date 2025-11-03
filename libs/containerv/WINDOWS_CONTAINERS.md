# Windows Container Support

This document describes the Windows container implementation using the Host Compute Service (HCS) layer.

## Overview

The containerv library now supports Windows containers through the Windows Host Compute Service (HCS) API. This provides isolated build environments on Windows similar to the Linux namespace-based implementation.

## Requirements

### Windows Version
- Windows 10 version 1709 (Fall Creators Update) or later
- Windows Server 2016 or later
- Windows 11 (recommended)

### System Requirements
- Windows containers feature must be enabled
- Administrator privileges for container operations
- HCS libraries (`vmcompute.dll` or `computecore.dll`) must be available

### Enabling Windows Containers

On Windows 10/11:
```powershell
# Enable Containers feature
Enable-WindowsOptionalFeature -Online -FeatureName Containers -All

# Reboot may be required
Restart-Computer
```

On Windows Server:
```powershell
# Install Containers feature
Install-WindowsFeature -Name Containers

# Reboot may be required
Restart-Computer
```

## Architecture

The Windows implementation uses the following components:

1. **HCS (Host Compute Service)**: Microsoft's container management API
2. **Container Layers**: Windows uses layered filesystem for containers
3. **Job Objects**: Process isolation within containers
4. **HNS (Host Network Service)**: Network isolation (planned)

## Key Differences from Linux

### Filesystem Isolation
- **Linux**: Uses overlayfs and bind mounts
- **Windows**: Uses container layers and volume mounting
- Windows requires pre-existing base image layers

### Process Isolation
- **Linux**: Uses PID namespaces
- **Windows**: Uses Windows Job Objects and process trees

### Networking
- **Linux**: Uses network namespaces and veth pairs
- **Windows**: Uses HNS (Host Network Service) with NAT (planned)

### Resource Limits
- **Linux**: Uses cgroups v2
- **Windows**: Uses Job Objects and HCS resource limits (planned)

## Usage

### Basic Container Creation

```c
#include <chef/containerv.h>

// Create container options
struct containerv_options* options = containerv_options_new();

// Set capabilities
containerv_options_set_caps(options, 
    CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL
);

// Create container with base image layer
struct containerv_container* container;
int status = containerv_create("C:\\ContainerLayers\\base", options, &container);

if (status == 0) {
    printf("Container created with ID: %s\n", containerv_id(container));
    
    // Use container...
    
    // Cleanup
    containerv_destroy(container);
}

containerv_options_delete(options);
```

### Spawning Processes

```c
// Spawn a process in the container
struct containerv_spawn_options spawn_opts = {
    .arguments = "cmd.exe /c dir",
    .environment = NULL,
    .as_user = NULL,
    .flags = CV_SPAWN_WAIT
};

HANDLE process_handle;
status = containerv_spawn(container, "cmd.exe", &spawn_opts, &process_handle);

if (status == 0) {
    printf("Process spawned successfully\n");
    if (!(spawn_opts.flags & CV_SPAWN_WAIT)) {
        // If not waiting, handle needs to be managed
        CloseHandle(process_handle);
    }
}
```

### Container Lifecycle

```c
// 1. Create container
struct containerv_container* container;
containerv_create(rootfs, options, &container);

// 2. Spawn processes
HANDLE handle;
containerv_spawn(container, "cmd.exe", &spawn_opts, &handle);

// 3. Kill specific process (if needed)
containerv_kill(container, handle);

// 4. Destroy container
containerv_destroy(container);
```

## CVD Daemon on Windows

The cvd (container daemon) supports Windows with platform-specific behavior:

### Starting cvd
```powershell
# Run cvd daemon (requires administrator privileges)
cvd.exe -v
```

### Rootfs Requirements

On Windows, the rootfs must be a pre-existing Windows container base image layer. You can:

1. Download a Windows Server Core base image
2. Use Docker to export a base layer
3. Create a custom base layer with required tools

Example with Docker:
```powershell
# Pull Windows Server Core
docker pull mcr.microsoft.com/windows/servercore:ltsc2022

# Export to a directory (this is simplified - actual process varies)
docker export $(docker create mcr.microsoft.com/windows/servercore:ltsc2022) -o servercore.tar
mkdir C:\ContainerLayers\base
tar -xf servercore.tar -C C:\ContainerLayers\base
```

## Current Limitations

The Windows implementation is functional but has some planned features:

### Not Yet Implemented
- **Network Isolation**: HNS integration for container networking
- **Volume Mounting**: Advanced bind mount support
- **File Upload/Download**: Transfer files between host and container
- **Container Join**: Joining an existing container namespace
- **Resource Limits**: CPU and memory limits via HCS
- **User Management**: Running processes as specific users

### Planned Features
These features are marked with TODO in the implementation:
- Full HNS network isolation with NAT
- Volume mounting with proper path translation
- File transfer operations (upload/download)
- User privilege management
- Resource limit configuration via HCS JSON

## Implementation Details

### HCS API Loading

The implementation dynamically loads HCS functions from system libraries:

```c
// Tries vmcompute.dll first (Windows Server 2016+)
// Falls back to computecore.dll (Windows 10+)
containerv_hcs_initialize();
```

### Container Configuration

Containers are created with a JSON configuration:
```json
{
  "SchemaVersion": {
    "Major": 2,
    "Minor": 1
  },
  "Owner": "chef-containerv",
  "HostName": "<container-id>",
  "Storage": {
    "Layers": [
      {
        "Path": "<rootfs-path>"
      }
    ]
  }
}
```

### Process Spawning

Processes use JSON parameters:
```json
{
  "CommandLine": "<command>",
  "WorkingDirectory": "\\",
  "CreateStdInPipe": false,
  "CreateStdOutPipe": false,
  "CreateStdErrPipe": false
}
```

## Error Handling

HCS operations return HRESULT values. Check for errors:

```c
if (containerv_create(rootfs, options, &container) != 0) {
    fprintf(stderr, "Failed to create container\n");
    // Check cvd log for detailed HCS error messages
}
```

Detailed error messages are logged to the cvd log file.

## Troubleshooting

### Container Creation Fails
- Ensure Windows Containers feature is enabled
- Verify administrator privileges
- Check that rootfs path exists and contains valid layer
- Review cvd log for HCS error codes

### Process Spawn Fails
- Verify the executable path exists in the container layer
- Check that container is in running state
- Ensure sufficient system resources

### Permission Denied
- Run cvd and applications with administrator privileges
- Check Windows User Account Control (UAC) settings

## Platform Detection

The code uses platform detection macros:

```c
#if defined(CHEF_ON_WINDOWS)
    // Windows-specific code
#elif defined(__linux__) || defined(__unix__)
    // Linux-specific code
#endif
```

## Building on Windows

### With Visual Studio
```powershell
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

### With MinGW
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Security Considerations

- Windows containers require administrator privileges
- HCS operations can affect system-wide container infrastructure
- Always validate rootfs paths to prevent unauthorized access
- Process handles should be properly managed and closed

## Future Enhancements

Planned improvements for Windows container support:
1. Full HNS network isolation with NAT and port mapping
2. Volume mounting with read/write permissions
3. File transfer operations between host and container
4. User context switching for spawned processes
5. Resource limits (CPU, memory, disk I/O)
6. Container checkpoint and restore
7. Windows Server Core and Nano Server image templates

## References

- [Microsoft HCS Documentation](https://docs.microsoft.com/en-us/virtualization/windowscontainers/)
- [Windows Containers Overview](https://docs.microsoft.com/en-us/virtualization/windowscontainers/about/)
- [Host Compute Service API](https://docs.microsoft.com/en-us/virtualization/api/)

## Contributing

When adding Windows-specific features:
1. Use `CHEF_ON_WINDOWS` macro for platform-specific code
2. Provide feature parity with Linux implementation where possible
3. Document Windows-specific behavior in this file
4. Add appropriate error handling for HCS operations
5. Test on multiple Windows versions (10, 11, Server 2019/2022)
