# Security Policy System for Containerv

## Overview

The containerv security policy system provides fine-grained control over what processes running inside containers can do. It uses eBPF infrastructure with seccomp-bpf for efficient syscall filtering.

## Architecture

```
┌─────────────────────────────────────────────────┐
│          Container Application                  │
└─────────────────┬───────────────────────────────┘
                  │
                  │ System Calls
                  ↓
┌─────────────────────────────────────────────────┐
│          seccomp-bpf Filter                     │
│  (Enforces allowed syscalls from policy)        │
└─────────────────┬───────────────────────────────┘
                  │
                  │ Allowed Calls Only
                  ↓
┌─────────────────────────────────────────────────┐
│          Linux Kernel                           │
└─────────────────────────────────────────────────┘
```

## Design Principles

### 1. Default-Deny Security Model
Without any policy, containers have **no permissions**. Policies explicitly grant capabilities.

### 2. Minimal Base Policies
Predefined policies start with minimal permissions:
- **Minimal**: Only what's needed for basic CLI tools
- **Build**: Minimal + process creation and file manipulation
- **Network**: Minimal + socket operations

### 3. Composable Policies
Custom policies can be built by:
- Starting from a predefined base
- Adding specific syscalls
- Adding filesystem paths with access modes

### 4. Performance
- Syscall filtering happens in the kernel (seccomp-bpf)
- No userspace overhead per syscall
- eBPF provides near-zero overhead

## Policy Types

### Minimal Policy

**Use case**: Simple CLI applications, data processing tools

**Allowed syscalls**:
- File I/O: `read`, `write`, `open`, `openat`, `close`, `lseek`
- File info: `stat`, `fstat`, `access`, `readlink`
- Directory: `getcwd`, `chdir`, `getdents`
- Memory: `brk`, `mmap`, `munmap`, `mprotect`
- Process: `getpid`, `getuid`, `exit`
- Time: `time`, `clock_gettime`, `nanosleep`
- I/O multiplexing: `select`, `poll`, `epoll_*`

**Filesystem access**:
- `/lib`, `/lib64`, `/usr/lib` (read + execute)
- `/dev/null`, `/dev/zero`, `/dev/urandom` (read + write)
- `/proc/self` (read)

**Example**:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_MINIMAL);
struct containerv_options* opts = containerv_options_new();
containerv_options_set_policy(opts, policy);
```

### Build Policy

**Use case**: Compilation, build systems, CI/CD pipelines

**Additional syscalls** (beyond Minimal):
- Process creation: `fork`, `vfork`, `clone`, `execve`, `wait4`
- IPC: `pipe`, `pipe2`, `socketpair`
- File operations: `rename`, `unlink`, `mkdir`, `rmdir`, `link`, `symlink`
- Permissions: `chmod`, `chown`, `truncate`
- Extended attributes: `getxattr`, `setxattr`
- Mount: `mount`, `umount2`

**Example**:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);

// Add workspace with full access
const char* paths[] = {"/workspace", "/tmp", NULL};
containerv_policy_add_paths(policy, paths, CV_FS_ALL);

struct containerv_options* opts = containerv_options_new();
containerv_options_set_policy(opts, policy);
```

### Network Policy

**Use case**: Web servers, network services, API clients

**Additional syscalls** (beyond Minimal):
- Socket creation: `socket`, `socketpair`
- Connection: `bind`, `connect`, `listen`, `accept`, `accept4`
- I/O: `sendto`, `recvfrom`, `sendmsg`, `recvmsg`, `sendmmsg`, `recvmmsg`
- Configuration: `setsockopt`, `getsockopt`
- Shutdown: `shutdown`

**Example**:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_NETWORK);

// Add SSL certificate paths
const char* paths[] = {
    "/etc/ssl",
    "/etc/ca-certificates",
    NULL
};
containerv_policy_add_paths(policy, paths, CV_FS_READ);

struct containerv_options* opts = containerv_options_new();
containerv_options_set_policy(opts, policy);
```

### Custom Policy

**Use case**: Specialized applications with specific requirements

**Example** - Database container:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_CUSTOM);

// Essential syscalls
const char* syscalls[] = {
    "read", "write", "open", "openat", "close",
    "pread64", "pwrite64", "fsync", "fdatasync",
    "mmap", "munmap", "madvise",
    "flock", "fcntl",
    "exit", "exit_group",
    NULL
};
containerv_policy_add_syscalls(policy, syscalls);

// Database files
containerv_policy_add_path(policy, "/var/lib/database", CV_FS_ALL);
containerv_policy_add_path(policy, "/etc/database", CV_FS_READ);

struct containerv_options* opts = containerv_options_new();
containerv_options_set_policy(opts, policy);
```

## Filesystem Access Control

### Access Modes

```c
enum containerv_fs_access {
    CV_FS_READ  = 0x1,  // Read files and list directories
    CV_FS_WRITE = 0x2,  // Modify files and create/delete
    CV_FS_EXEC  = 0x4,  // Execute files
    CV_FS_ALL   = 0x7   // Full access
};
```

### Path Wildcards (Future)

Currently paths are exact matches. Future versions will support:
- `/*` - Match any file in directory
- `/app/**` - Match recursively
- `/lib/*.so` - Pattern matching

## Integration with Container Lifecycle

### When Policies Are Applied

```
Container Creation
       ↓
Namespace Setup
       ↓
Capability Drop
       ↓
Policy Application  ← seccomp-bpf filter loaded here
       ↓
Init Process
       ↓
Container Running
```

Policies are applied **after** dropping capabilities but **before** the container enters its main loop. This ensures:
1. The container setup has necessary privileges
2. User processes run with restricted permissions
3. No race conditions during policy application

### Example: Complete Container with Policy

```c
#include <chef/containerv.h>
#include <chef/containerv/policy.h>

int main(void) {
    // Create build policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);
    
    // Add build workspace
    const char* paths[] = {"/workspace", "/tmp", NULL};
    containerv_policy_add_paths(policy, paths, CV_FS_ALL);
    
    // Configure container options
    struct containerv_options* opts = containerv_options_new();
    containerv_options_set_caps(opts, 
        CV_CAP_FILESYSTEM | CV_CAP_NETWORK | CV_CAP_PROCESS_CONTROL);
    containerv_options_set_policy(opts, policy);
    
    // Create container
    struct containerv_container* container;
    int result = containerv_create("build-001", opts, &container);
    if (result != 0) {
        fprintf(stderr, "Failed to create container\n");
        return 1;
    }
    
    // Spawn build process (policy will be enforced)
    struct containerv_spawn_options spawn_opts = {0};
    process_handle_t pid;
    result = containerv_spawn(container, "/usr/bin/make", &spawn_opts, &pid);
    
    // Cleanup
    containerv_destroy(container);
    containerv_options_delete(opts);
    
    return result;
}
```

## Security Considerations

### What Policies Protect Against

✅ **Unauthorized system calls**: Process cannot call blocked syscalls
✅ **Privilege escalation**: No setuid/setgid binaries can be created
✅ **Namespace escape**: Cannot create new user namespaces
✅ **Kernel exploits**: Many attack vectors blocked (ptrace, perf_event_open, userfaultfd)
✅ **Container breakout**: Limited syscall surface reduces attack vectors

### What Policies Don't Protect Against

❌ **Resource exhaustion**: Use cgroups for CPU/memory limits
❌ **Application vulnerabilities**: Code bugs still exist
❌ **Data exfiltration**: Network policies needed for egress control
❌ **Side channels**: CPU timing attacks, etc.

### Best Practices

1. **Start with minimal policy** and add permissions as needed
2. **Test in development** before deploying to production
3. **Monitor denied syscalls** to identify missing permissions
4. **Combine with other security**: cgroups, network policies, user namespaces
5. **Regular updates**: Review and update policies as needs change

## Troubleshooting

### Container Fails with "Operation not permitted"

The process tried to use a syscall not in the policy.

**Solution**: Add the required syscall to your policy:
```c
const char* additional[] = {"needed_syscall", NULL};
containerv_policy_add_syscalls(policy, additional);
```

**Finding the syscall**: Use `strace` outside container to identify needed syscalls:
```bash
strace -c your_program
```

### Build Fails Inside Container

Build tools need more syscalls than minimal policy provides.

**Solution**: Use `CV_POLICY_BUILD` instead of `CV_POLICY_MINIMAL`:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);
```

### Network Application Cannot Connect

Network operations need network policy.

**Solution**: Use `CV_POLICY_NETWORK`:
```c
struct containerv_policy* policy = containerv_policy_new(CV_POLICY_NETWORK);
```

## Future Enhancements

### Full eBPF LSM Integration

Current implementation uses seccomp-bpf. Future versions will leverage BPF LSM (Linux 5.7+) for:
- Dynamic path filtering in kernel
- Real-time policy updates
- Detailed audit logging
- Network egress control

### Performance Monitoring

```c
struct containerv_policy_stats {
    uint64_t allowed_calls;
    uint64_t denied_calls;
    uint64_t most_denied_syscall;
};

containerv_policy_get_stats(container, &stats);
```

### Policy Profiles

Save and load policy configurations:
```c
containerv_policy_save(policy, "/etc/containerv/policies/webapp.policy");
struct containerv_policy* policy = containerv_policy_load("/etc/containerv/policies/webapp.policy");
```

## References

- [seccomp(2)](https://man7.org/linux/man-pages/man2/seccomp.2.html) - Linux seccomp syscall
- [BPF LSM](https://www.kernel.org/doc/html/latest/bpf/bpf_lsm.html) - BPF-based Linux Security Module
- [Docker seccomp profile](https://docs.docker.com/engine/security/seccomp/) - Inspiration for default policies
