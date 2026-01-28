# PID 1 Service for Container Runtime

This directory contains the PID 1 service implementation for container process management. The PID 1 service is responsible for:

- Spawning and monitoring processes within containers
- Reaping zombie processes (Linux)
- Handling shutdown signals and graceful termination
- Resource management and lifecycle control

## Architecture

The PID 1 service follows a modular design with platform-independent and platform-specific components:

```
containerv/pid1/
├── shared/              # Platform-independent code
│   ├── pid1_common.h    # Common interface definition
│   ├── pid1_common.c    # Common utilities and validation
│   ├── logging.h        # Logging interface
│   └── logging.c        # Logging implementation
├── linux/               # Linux-specific implementation
│   ├── pid1_linux.h     # Linux-specific interface
│   └── pid1_linux.c     # fork/execve-based implementation
└── windows/             # Windows-specific implementation
    ├── pid1_windows.h   # Windows-specific interface
    └── pid1_windows.c   # CreateProcess/Job Object-based implementation
```

## Common Interface

All platform implementations provide the following common interface:

### Initialization and Cleanup
- `pid1_init()` - Initialize the PID 1 service
- `pid1_cleanup()` - Clean up and terminate all child processes

### Process Management
- `pid1_spawn_process()` - Spawn a new process
- `pid1_wait_process()` - Wait for a process to exit
- `pid1_kill_process()` - Terminate a process
- `pid1_reap_zombies()` - Reap terminated processes (Linux only)
- `pid1_get_process_count()` - Get count of active processes

### Configuration
Process spawning is configured using `pid1_process_options_t`:
```c
typedef struct pid1_process_options {
    const char*        command;           // Path to executable
    const char* const* args;              // Null-terminated argument array
    const char* const* environment;       // Null-terminated environment array
    const char*        working_directory; // Working directory (NULL for default)
    const char*        log_path;          // Path for logging (NULL for stderr)
    
    // Resource limits
    uint64_t           memory_limit_bytes;
    uint32_t           cpu_percent;
    uint32_t           process_limit;
    
    // User/Group
    uint32_t           uid;
    uint32_t           gid;
    
    // Flags
    int                wait_for_exit;
    int                forward_signals;
} pid1_process_options_t;
```

## Platform-Specific Details

### Linux Implementation

The Linux PID 1 service (`linux/pid1_linux.c`) uses:

- **fork() + execve()** for process creation
- **Signal handlers** for SIGCHLD (child termination) and SIGTERM/SIGINT (shutdown)
- **waitpid()** for zombie process reaping
- **kill()** for process termination

Key features:
- Automatic zombie reaping via SIGCHLD handler
- Process tracking with linked list
- UID/GID switching for non-root execution
- Working directory and environment variable support

### Windows Implementation

The Windows PID 1 service (`windows/pid1_windows.c`) uses:

- **CreateProcess()** for process spawning
- **Job Objects** for automatic process cleanup and resource management
- **Console Control Handlers** for CTRL+C, CTRL+BREAK, shutdown events
- **WaitForSingleObject()** for process synchronization

Key features:
- Job Object ensures all child processes are terminated when container exits
- Process tracking with linked list and critical section synchronization
- UTF-16 command line and environment block building
- Working directory support

## Usage Example

```c
#include <containerv/pid1/shared/pid1_common.h>
#include <containerv/pid1/shared/logging.h>

int main() {
    // Initialize logging
    pid1_log_init("/var/log/pid1.log", PID1_LOG_DEBUG);
    
    // Initialize PID 1 service
    if (pid1_init() != 0) {
        PID1_ERROR("Failed to initialize PID 1 service");
        return 1;
    }
    
    // Spawn a process
    pid1_process_options_t opts = {
        .command = "/bin/bash",
        .args = (const char*[]){"bash", "-c", "echo Hello", NULL},
        .environment = NULL,
        .working_directory = NULL,
        .log_path = NULL,
        .memory_limit_bytes = 0,
        .cpu_percent = 0,
        .process_limit = 0,
        .uid = 0,
        .gid = 0,
        .wait_for_exit = 0,
        .forward_signals = 1
    };
    
    pid1_process_handle_t handle;
    if (pid1_spawn_process(&opts, &handle) != 0) {
        PID1_ERROR("Failed to spawn process");
        pid1_cleanup();
        return 1;
    }
    
    // Wait for process to exit
    int exit_code;
    pid1_wait_process(handle, &exit_code);
    PID1_INFO("Process exited with code %d", exit_code);
    
    // Cleanup
    pid1_cleanup();
    pid1_log_close();
    
    return 0;
}
```

## Integration with Container Runtime

The PID 1 service is designed to be integrated with the existing containerv implementation:

- **Linux**: Can be used within the container's PID namespace to replace the current fork/execve logic in `containerv/linux/container.c`
- **Windows**: Can be used to manage processes spawned via HCS within Hyper-V VMs

## Future Enhancements

Potential improvements for the PID 1 service:

1. **Resource Limits**: Full integration with cgroups (Linux) and Job Objects (Windows)
2. **Process Groups**: Support for process group management
3. **I/O Redirection**: Capture and redirect stdout/stderr
4. **Health Checks**: Monitor process health and restart on failure
5. **Metrics**: Collect and report process resource usage statistics
6. **Signal Forwarding**: Forward specific signals to child processes

## Testing

To test the PID 1 service:

1. Build the containerv library with PID 1 support
2. Create a simple test program that uses the PID 1 API
3. Verify process spawning, waiting, and termination
4. Test signal handling and graceful shutdown
5. Verify zombie process reaping (Linux)
6. Test resource limits (if implemented)

## License

Copyright, Philip Meulengracht

This program is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
