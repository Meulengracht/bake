# Container Security & Sandboxing Analysis for Chef Containerv

## Executive Summary

Security & Sandboxing provides comprehensive container isolation and privilege management for Chef's containerv library. This system implements security profiles, capability management, and platform-specific sandboxing technologies to ensure containers cannot escape their boundaries or access unauthorized resources.

## Security Architecture Overview

### Linux Security Model
```
┌─────────────────────────────────────────┐
│ Host Linux System                       │
│                                         │
│ ┌─────────────────────────────────────┐ │
│ │ Container Namespace                 │ │
│ │                                     │ │
│ │ ┌─────────────┐ ┌─────────────────┐ │ │
│ │ │ seccomp-bpf │ │ Linux           │ │ │
│ │ │ syscall     │ │ Capabilities    │ │ │
│ │ │ filtering   │ │ CAP_NET_ADMIN   │ │ │
│ │ └─────────────┘ │ CAP_SYS_ADMIN   │ │ │
│ │                 │ etc.            │ │ │
│ │ ┌─────────────┐ └─────────────────┘ │ │
│ │ │ AppArmor/   │                     │ │
│ │ │ SELinux     │ ┌─────────────────┐ │ │
│ │ │ Profile     │ │ cgroup limits   │ │ │
│ │ └─────────────┘ │ memory, cpu     │ │ │
│ │                 │ pids, devices   │ │ │
│ └─────────────────┴─────────────────────┘ │
└─────────────────────────────────────────┘
```

### Windows Security Model
```
┌─────────────────────────────────────────┐
│ Host Windows System                     │
│                                         │
│ ┌─────────────────────────────────────┐ │
│ │ HyperV Virtual Machine             │ │
│ │                                     │ │
│ │ ┌─────────────┐ ┌─────────────────┐ │ │
│ │ │ AppContainer│ │ Windows         │ │ │
│ │ │ Isolation   │ │ Privileges      │ │ │
│ │ │ Boundaries  │ │ Tokens          │ │ │
│ │ └─────────────┘ └─────────────────┘ │ │
│ │                                     │ │
│ │ ┌─────────────┐ ┌─────────────────┐ │ │
│ │ │ WDAC Policy │ │ Job Object      │ │ │
│ │ │ Code        │ │ Security        │ │ │
│ │ │ Integrity   │ │ Limits          │ │ │
│ │ └─────────────┘ └─────────────────┘ │ │
│ └─────────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

## Security Features Comparison

| Security Feature | Linux Implementation | Windows Implementation | Purpose |
|------------------|---------------------|----------------------|---------|
| **Syscall Filtering** | seccomp-bpf | AppContainer syscall restrictions | Block dangerous system calls |
| **Privilege Management** | Linux Capabilities | Windows Privileges & Tokens | Minimize process privileges |
| **Mandatory Access Control** | AppArmor/SELinux | WDAC (Windows Defender Application Control) | Enforce security policies |
| **Process Isolation** | PID/User namespaces | Job Objects + VM isolation | Isolate process trees |
| **Network Isolation** | Network namespaces | HyperV virtual networking | Control network access |
| **Filesystem Isolation** | Mount namespaces + chroot | VM filesystem boundaries | Restrict file access |
| **Device Access** | cgroup device controller | Windows device security | Control hardware access |
| **Memory Protection** | ASLR + stack canaries | Windows CFG + CET | Prevent code injection |

## API Design

### Security Profile Structure
```c
// Security profile levels
enum containerv_security_level {
    CV_SECURITY_PERMISSIVE = 0,     // Minimal restrictions
    CV_SECURITY_RESTRICTED = 1,     // Standard container security
    CV_SECURITY_STRICT = 2,         // High security applications
    CV_SECURITY_PARANOID = 3        // Maximum security isolation
};

// Platform-agnostic security profile
struct containerv_security_profile {
    enum containerv_security_level level;
    char*     name;                 // Profile name for identification
    char*     description;          // Human-readable description
    
    // Capability management
    uint64_t  allowed_caps;         // Bitmask of allowed capabilities
    uint64_t  dropped_caps;         // Bitmask of explicitly dropped capabilities
    bool      no_new_privileges;    // Prevent privilege escalation
    
    // Syscall filtering
    char**    allowed_syscalls;     // Array of allowed syscall names
    char**    blocked_syscalls;     // Array of blocked syscall names
    int       syscall_count;        // Number of syscall entries
    
    // Network security
    bool      network_isolated;     // Isolate from host network
    char**    allowed_ports;        // Array of "proto:port" strings
    char**    allowed_hosts;        // Array of allowed hostname/IP patterns
    int       network_rule_count;   // Number of network rules
    
    // Filesystem security
    bool      read_only_root;       // Make root filesystem read-only
    char**    writable_paths;       // Array of writable path exceptions
    char**    masked_paths;         // Array of paths to mask/hide
    int       fs_rule_count;        // Number of filesystem rules
    
    // Process security
    uid_t     run_as_uid;           // User ID to run as (Linux)
    gid_t     run_as_gid;           // Group ID to run as (Linux)
    char*     run_as_user;          // Username to run as
    bool      no_suid;              // Disable setuid/setgid programs
    
    // Advanced security
    bool      apparmor_profile;     // Use AppArmor profile (Linux)
    bool      selinux_context;      // Use SELinux context (Linux)
    char*     security_context;     // Security context string
    
    // Windows-specific
    bool      app_container;        // Use AppContainer isolation (Windows)
    char*     integrity_level;      // Windows integrity level
    char**    capabilities_sid;     // Windows capability SIDs
    int       win_cap_count;        // Number of Windows capabilities
};

// Predefined security profiles
extern const struct containerv_security_profile CV_PROFILE_DEFAULT;
extern const struct containerv_security_profile CV_PROFILE_WEB_SERVER;
extern const struct containerv_security_profile CV_PROFILE_DATABASE;
extern const struct containerv_security_profile CV_PROFILE_BUILD_ENVIRONMENT;
extern const struct containerv_security_profile CV_PROFILE_UNTRUSTED;
```

### Security Management APIs
```c
/**
 * @brief Initialize the container security subsystem
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_init(void);

/**
 * @brief Cleanup the container security subsystem
 */
extern void containerv_security_cleanup(void);

/**
 * @brief Create a new security profile
 * @param name Profile name (must be unique)
 * @param level Base security level
 * @param profile Output pointer to created profile
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_profile_create(
    const char* name,
    enum containerv_security_level level,
    struct containerv_security_profile** profile
);

/**
 * @brief Load a security profile by name
 * @param name Profile name
 * @param profile Output pointer to loaded profile
 * @return 0 on success, -1 if not found
 */
extern int containerv_security_profile_load(
    const char* name,
    struct containerv_security_profile** profile
);

/**
 * @brief Save a security profile for reuse
 * @param profile Profile to save
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_profile_save(
    const struct containerv_security_profile* profile
);

/**
 * @brief Delete a security profile
 * @param profile Profile to delete
 */
extern void containerv_security_profile_delete(
    struct containerv_security_profile* profile
);

/**
 * @brief Apply security profile to container options
 * @param options Container options to modify
 * @param profile Security profile to apply
 * @return 0 on success, -1 on failure
 */
extern int containerv_options_set_security_profile(
    struct containerv_options* options,
    const struct containerv_security_profile* profile
);

/**
 * @brief Validate that a security profile is supported on current platform
 * @param profile Profile to validate
 * @param error_msg Output buffer for error description (can be NULL)
 * @param error_size Size of error message buffer
 * @return 0 if valid, -1 if unsupported
 */
extern int containerv_security_profile_validate(
    const struct containerv_security_profile* profile,
    char* error_msg,
    size_t error_size
);
```

### Capability Management APIs
```c
// Linux capabilities enum (subset of most important ones)
enum containerv_linux_capability {
    CV_CAP_CHOWN = 0,               // Change file ownership
    CV_CAP_DAC_OVERRIDE = 1,        // Bypass file read/write/execute permission checks
    CV_CAP_DAC_READ_SEARCH = 2,     // Bypass file/directory read permission checks
    CV_CAP_FOWNER = 3,              // Bypass permission checks on operations that normally require filesystem UID
    CV_CAP_FSETID = 4,              // Don't clear set-user-ID and set-group-ID mode bits
    CV_CAP_KILL = 5,                // Bypass permission checks for sending signals
    CV_CAP_SETGID = 6,              // Make arbitrary manipulations of process GIDs
    CV_CAP_SETUID = 7,              // Make arbitrary manipulations of process UIDs
    CV_CAP_SETPCAP = 8,             // Transfer and remove capabilities from other processes
    CV_CAP_LINUX_IMMUTABLE = 9,     // Set the FS_IMMUTABLE_FL and FS_APPEND_FL i-node flags
    CV_CAP_NET_BIND_SERVICE = 10,   // Bind a socket to Internet domain privileged ports
    CV_CAP_NET_BROADCAST = 11,      // Make socket broadcasts, and listen to multicasts
    CV_CAP_NET_ADMIN = 12,          // Perform various network-related operations
    CV_CAP_NET_RAW = 13,            // Use RAW and PACKET sockets
    CV_CAP_IPC_LOCK = 14,           // Lock memory
    CV_CAP_IPC_OWNER = 15,          // Bypass permission checks for operations on System V IPC objects
    CV_CAP_SYS_MODULE = 16,         // Load and unload kernel modules
    CV_CAP_SYS_RAWIO = 17,          // Perform I/O port operations and access /proc/kcore
    CV_CAP_SYS_CHROOT = 18,         // Use chroot()
    CV_CAP_SYS_PTRACE = 19,         // Trace arbitrary processes using ptrace()
    CV_CAP_SYS_PACCT = 20,          // Use acct()
    CV_CAP_SYS_ADMIN = 21,          // Perform a range of system administration operations
    CV_CAP_SYS_BOOT = 22,           // Use reboot() and kexec_load()
    CV_CAP_SYS_NICE = 23,           // Raise process nice value and change nice value for arbitrary processes
    CV_CAP_SYS_RESOURCE = 24,       // Override resource limits
    CV_CAP_SYS_TIME = 25,           // Set system clock and real-time clock
    CV_CAP_SYS_TTY_CONFIG = 26,     // Use vhangup() and employ various privileged ioctl() operations on virtual terminals
    CV_CAP_MKNOD = 27,              // Create special files using mknod()
    CV_CAP_LEASE = 28,              // Establish leases on arbitrary files
    CV_CAP_AUDIT_WRITE = 29,        // Write records to kernel auditing log
    CV_CAP_AUDIT_CONTROL = 30,      // Enable and disable kernel auditing
    CV_CAP_SETFCAP = 31,            // Set file capabilities
    CV_CAP_MAC_OVERRIDE = 32,       // Override Mandatory Access Control (MAC)
    CV_CAP_MAC_ADMIN = 33,          // Allow MAC configuration or state changes
    CV_CAP_SYSLOG = 34,             // Perform privileged syslog() operations
    CV_CAP_WAKE_ALARM = 35,         // Trigger something that will wake up the system
    CV_CAP_BLOCK_SUSPEND = 36,      // Employ features that can block system suspend
    CV_CAP_AUDIT_READ = 37,         // Allow reading the audit log via multicast netlink socket
    CV_CAP_PERFMON = 38,            // Allow system performance and observability privileged operations
    CV_CAP_BPF = 39,                // Employ privileged BPF operations
    CV_CAP_CHECKPOINT_RESTORE = 40  // Allow checkpoint/restore related operations
};

// Windows privileges (subset of most important ones)
enum containerv_windows_privilege {
    CV_PRIV_ASSIGN_PRIMARY_TOKEN,      // Replace a process-level token
    CV_PRIV_AUDIT,                     // Generate security audit log entries
    CV_PRIV_BACKUP,                    // Back up files and directories
    CV_PRIV_CHANGE_NOTIFY,             // Bypass traverse checking
    CV_PRIV_CREATE_GLOBAL,             // Create global objects
    CV_PRIV_CREATE_PAGEFILE,           // Create a pagefile
    CV_PRIV_CREATE_PERMANENT,          // Create permanent shared objects
    CV_PRIV_CREATE_SYMBOLIC_LINK,      // Create symbolic links
    CV_PRIV_CREATE_TOKEN,              // Create a token object
    CV_PRIV_DEBUG,                     // Debug programs
    CV_PRIV_ENABLE_DELEGATION,         // Enable computer and user accounts to be trusted for delegation
    CV_PRIV_IMPERSONATE,               // Impersonate a client after authentication
    CV_PRIV_INC_BASE_PRIORITY,         // Increase scheduling priority
    CV_PRIV_INCREASE_QUOTA,            // Adjust memory quotas for a process
    CV_PRIV_INC_WORKING_SET,           // Increase a process working set
    CV_PRIV_LOAD_DRIVER,               // Load and unload device drivers
    CV_PRIV_LOCK_MEMORY,               // Lock pages in memory
    CV_PRIV_MACHINE_ACCOUNT,           // Add workstations to domain
    CV_PRIV_MANAGE_VOLUME,             // Perform volume maintenance tasks
    CV_PRIV_PROF_SINGLE_PROCESS,       // Profile single process
    CV_PRIV_RELABEL,                   // Modify an object label
    CV_PRIV_REMOTE_SHUTDOWN,           // Force shutdown from a remote system
    CV_PRIV_RESTORE,                   // Restore files and directories
    CV_PRIV_SECURITY,                  // Manage auditing and security log
    CV_PRIV_SHUTDOWN,                  // Shut down the system
    CV_PRIV_SYNC_AGENT,                // Synchronize directory service data
    CV_PRIV_SYSTEM_ENVIRONMENT,        // Modify firmware environment values
    CV_PRIV_SYSTEM_PROFILE,            // Profile system performance
    CV_PRIV_SYSTEM_TIME,               // Change the system time
    CV_PRIV_TAKE_OWNERSHIP,            // Take ownership of files or other objects
    CV_PRIV_TCB,                       // Act as part of the operating system
    CV_PRIV_TIME_ZONE,                 // Change the time zone
    CV_PRIV_TRUSTED_CREDMAN_ACCESS,    // Access Credential Manager as a trusted caller
    CV_PRIV_UNDOCK,                    // Remove computer from docking station
    CV_PRIV_UNSOLICITED_INPUT          // Read unsolicited input from a terminal device
};

/**
 * @brief Add a capability to a security profile
 * @param profile Security profile to modify
 * @param capability Capability to add (platform-specific)
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
);

/**
 * @brief Drop a capability from a security profile
 * @param profile Security profile to modify  
 * @param capability Capability to drop (platform-specific)
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_drop_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
);

/**
 * @brief Check if a capability is allowed in a profile
 * @param profile Security profile to check
 * @param capability Capability to check (platform-specific)
 * @return 1 if allowed, 0 if not allowed
 */
extern int containerv_security_has_capability(
    const struct containerv_security_profile* profile,
    uint32_t capability
);
```

### Syscall Filtering APIs (Linux)
```c
// Syscall action types
enum containerv_syscall_action {
    CV_SYSCALL_ALLOW = 0,           // Allow syscall
    CV_SYSCALL_ERRNO = 1,           // Return specific errno
    CV_SYSCALL_KILL = 2,            // Kill process
    CV_SYSCALL_TRAP = 3,            // Send SIGSYS to process
    CV_SYSCALL_TRACE = 4,           // Notify tracing process
    CV_SYSCALL_LOG = 5              // Log syscall attempt
};

struct containerv_syscall_filter {
    char* name;                     // Syscall name (e.g., "open", "execve")
    enum containerv_syscall_action action; // Action to take
    int   errno_value;              // errno to return (if action is CV_SYSCALL_ERRNO)
    char* args_filter;              // Optional argument-based filter expression
};

/**
 * @brief Add a syscall filter rule to security profile
 * @param profile Security profile to modify
 * @param syscall_name Name of syscall to filter
 * @param action Action to take when syscall is attempted
 * @param errno_value errno value to return (for CV_SYSCALL_ERRNO action)
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_add_syscall_filter(
    struct containerv_security_profile* profile,
    const char* syscall_name,
    enum containerv_syscall_action action,
    int errno_value
);

/**
 * @brief Set default syscall action for profile
 * @param profile Security profile to modify
 * @param default_action Default action for unspecified syscalls
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_set_default_syscall_action(
    struct containerv_security_profile* profile,
    enum containerv_syscall_action default_action
);

/**
 * @brief Load syscall filter from seccomp policy file
 * @param profile Security profile to modify
 * @param policy_file Path to seccomp policy file
 * @return 0 on success, -1 on failure
 */
extern int containerv_security_load_seccomp_policy(
    struct containerv_security_profile* profile,
    const char* policy_file
);
```

## Predefined Security Profiles

### Default Profile (Balanced Security)
```c
const struct containerv_security_profile CV_PROFILE_DEFAULT = {
    .level = CV_SECURITY_RESTRICTED,
    .name = "default",
    .description = "Balanced security for general container workloads",
    
    // Allow common capabilities, drop dangerous ones
    .allowed_caps = (1ULL << CV_CAP_CHOWN) |
                   (1ULL << CV_CAP_DAC_OVERRIDE) |
                   (1ULL << CV_CAP_FOWNER) |
                   (1ULL << CV_CAP_FSETID) |
                   (1ULL << CV_CAP_KILL) |
                   (1ULL << CV_CAP_SETGID) |
                   (1ULL << CV_CAP_SETUID) |
                   (1ULL << CV_CAP_NET_BIND_SERVICE) |
                   (1ULL << CV_CAP_SYS_CHROOT),
                   
    .dropped_caps = (1ULL << CV_CAP_SYS_ADMIN) |
                   (1ULL << CV_CAP_SYS_MODULE) |
                   (1ULL << CV_CAP_SYS_RAWIO) |
                   (1ULL << CV_CAP_SYS_BOOT) |
                   (1ULL << CV_CAP_MAC_ADMIN),
                   
    .no_new_privileges = true,
    .network_isolated = false,
    .read_only_root = false,
    .no_suid = true,
    .run_as_uid = 1000,  // Non-root user
    .run_as_gid = 1000
};
```

### Web Server Profile
```c
const struct containerv_security_profile CV_PROFILE_WEB_SERVER = {
    .level = CV_SECURITY_RESTRICTED,
    .name = "web-server", 
    .description = "Security profile optimized for web servers",
    
    // Minimal capabilities for web serving
    .allowed_caps = (1ULL << CV_CAP_NET_BIND_SERVICE) | // Bind to port 80/443
                   (1ULL << CV_CAP_SETUID) |           // Drop privileges after bind
                   (1ULL << CV_CAP_SETGID) |
                   (1ULL << CV_CAP_CHOWN),             // Change log file ownership
                   
    .no_new_privileges = true,
    .network_isolated = false,
    .read_only_root = true,   // Read-only root filesystem
    .no_suid = true,
    
    // Writable paths for web server operations
    .writable_paths = (char*[]){"/var/log", "/var/cache", "/tmp", NULL},
    .fs_rule_count = 3,
    
    // Network restrictions
    .allowed_ports = (char*[]){"tcp:80", "tcp:443", "tcp:8080", NULL},
    .network_rule_count = 3,
    
    .run_as_user = "www-data"
};
```

### Untrusted Workload Profile
```c
const struct containerv_security_profile CV_PROFILE_UNTRUSTED = {
    .level = CV_SECURITY_PARANOID,
    .name = "untrusted",
    .description = "Maximum security for untrusted or unknown workloads",
    
    // Minimal capabilities - almost none
    .allowed_caps = 0,  // No special capabilities
    
    .no_new_privileges = true,
    .network_isolated = true,     // Complete network isolation
    .read_only_root = true,       // Read-only filesystem
    .no_suid = true,
    
    // Very limited writable areas
    .writable_paths = (char*[]){"/tmp", NULL},
    .fs_rule_count = 1,
    
    // Mask sensitive paths
    .masked_paths = (char*[]){
        "/proc/kcore", "/proc/keys", "/proc/timer_list",
        "/proc/sched_debug", "/sys/firmware", "/proc/scsi",
        NULL
    },
    
    .run_as_uid = 65534,  // nobody user
    .run_as_gid = 65534,
    
    // Strict syscall filtering (would be defined elsewhere)
    .syscall_count = 0,  // Use default restrictive policy
    
#ifdef __linux__
    .apparmor_profile = true,
    .security_context = "unconfined_u:unconfined_r:container_untrusted_t:s0"
#endif
};
```

## Platform-Specific Implementation

### Linux Security Implementation
```c
// Linux-specific security functions
int linux_apply_capabilities(pid_t pid, const struct containerv_security_profile* profile);
int linux_apply_seccomp_filter(pid_t pid, const struct containerv_security_profile* profile);
int linux_setup_apparmor_profile(const char* profile_name);
int linux_setup_selinux_context(const char* context);
int linux_drop_privileges(uid_t uid, gid_t gid);
int linux_setup_no_new_privs(void);

// seccomp-bpf filter generation
struct sock_filter* generate_seccomp_filter(const struct containerv_security_profile* profile, 
                                           size_t* filter_len);
```

### Windows Security Implementation  
```c
// Windows-specific security functions
int windows_create_appcontainer(const struct containerv_security_profile* profile,
                               PSID* appcontainer_sid);
int windows_apply_job_security(HANDLE job_handle, const struct containerv_security_profile* profile);
int windows_create_restricted_token(const struct containerv_security_profile* profile, 
                                   HANDLE* restricted_token);
int windows_set_integrity_level(HANDLE token, const char* integrity_level);
int windows_apply_wdac_policy(const char* policy_file);

// Windows privilege management
int windows_drop_privilege(HANDLE token, enum containerv_windows_privilege privilege);
int windows_add_privilege(HANDLE token, enum containerv_windows_privilege privilege);
```

## Security Profile Examples

### Database Container Profile
```json
{
  "name": "database",
  "level": "restricted",
  "description": "Security profile for database containers",
  "capabilities": {
    "allowed": ["SETUID", "SETGID", "CHOWN", "DAC_OVERRIDE"],
    "dropped": ["SYS_ADMIN", "NET_ADMIN", "SYS_MODULE"]
  },
  "filesystem": {
    "read_only_root": false,
    "writable_paths": ["/var/lib/database", "/var/log", "/tmp"],
    "masked_paths": ["/proc/kcore", "/proc/keys"]
  },
  "network": {
    "isolated": false,
    "allowed_ports": ["tcp:5432", "tcp:3306", "tcp:27017"]
  },
  "process": {
    "run_as_user": "database",
    "no_new_privileges": true,
    "no_suid": true
  },
  "syscalls": {
    "default_action": "errno",
    "allowed": [
      "read", "write", "open", "close", "stat", "fstat",
      "mmap", "munmap", "brk", "ioctl", "select", "poll",
      "socket", "bind", "listen", "accept", "connect",
      "setsockopt", "getsockopt", "clone", "execve", "exit"
    ],
    "blocked": [
      "ptrace", "process_vm_readv", "process_vm_writev",
      "mount", "umount", "swapon", "swapoff", "reboot"
    ]
  }
}
```

### Build Environment Profile
```json
{
  "name": "build-environment",
  "level": "permissive",
  "description": "Relaxed security for build containers that need more access",
  "capabilities": {
    "allowed": [
      "SETUID", "SETGID", "CHOWN", "DAC_OVERRIDE", "FOWNER", 
      "MKNOD", "SETFCAP", "SYS_CHROOT"
    ]
  },
  "filesystem": {
    "read_only_root": false,
    "writable_paths": ["/*"],
    "no_suid": false
  },
  "network": {
    "isolated": false,
    "allowed_hosts": ["*"]
  },
  "process": {
    "run_as_user": "builder",
    "no_new_privileges": false
  },
  "syscalls": {
    "default_action": "allow",
    "blocked": [
      "reboot", "kexec_load", "mount", "umount2",
      "delete_module", "init_module"
    ]
  }
}
```

## Security Validation and Compliance

### Security Checklist
```c
struct containerv_security_audit {
    bool capabilities_minimal;      // Only necessary capabilities granted
    bool no_privileged_access;     // No root or admin access
    bool filesystem_restricted;    // Filesystem access limited  
    bool network_controlled;       // Network access controlled
    bool syscalls_filtered;        // Dangerous syscalls blocked
    bool resources_limited;        // Resource usage limited
    bool isolation_complete;       // Process isolation enforced
    bool secrets_protected;        // Secrets properly handled
    
    char audit_log[1024];          // Detailed audit information
    time_t audit_time;             // When audit was performed
    char auditor[256];             // Who performed the audit
};

/**
 * @brief Perform security audit on a container or profile
 * @param container Container to audit (NULL to audit profile only)
 * @param profile Security profile to audit
 * @param audit Output audit results
 * @return 0 if secure, -1 if security issues found
 */
extern int containerv_security_audit(
    struct containerv_container* container,
    const struct containerv_security_profile* profile,
    struct containerv_security_audit* audit
);

/**
 * @brief Generate security compliance report
 * @param profile Security profile to analyze
 * @param standard Compliance standard ("CIS", "NIST", "PCI-DSS")
 * @param report_file Output file for compliance report
 * @return 0 on success, -1 on failure
 */
extern int containerv_generate_compliance_report(
    const struct containerv_security_profile* profile,
    const char* standard,
    const char* report_file
);
```

This comprehensive Security & Sandboxing system provides Chef's containerv with enterprise-grade security capabilities that can be tailored to specific workload requirements while maintaining compatibility across Linux and Windows platforms.