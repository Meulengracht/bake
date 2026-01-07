/**
 * Copyright 2024, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE

#include "policy-internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

// Minimal syscall set for basic CLI applications
static const char* minimal_syscalls[] = {
    // Process management
    "exit", "exit_group",
    
    // File I/O
    "read", "write", "open", "openat", "close",
    "lseek", "llseek", "_llseek",
    "dup", "dup2", "dup3",
    
    // File information
    "stat", "fstat", "lstat", "newfstatat", "statx",
    "access", "faccessat", "faccessat2",
    "readlink", "readlinkat",
    
    // Directory operations
    "getcwd", "chdir", "fchdir",
    "getdents", "getdents64",
    
    // Memory management
    "brk", "mmap", "mmap2", "munmap", "mremap",
    "mprotect", "madvise",
    
    // Process information
    "getpid", "gettid", "getuid", "getgid",
    "geteuid", "getegid", "getppid",
    "getpgid", "getpgrp", "getsid",
    
    // Signal handling
    "rt_sigaction", "rt_sigprocmask", "rt_sigreturn",
    "sigaltstack",
    
    // Time
    "time", "gettimeofday", "clock_gettime", "clock_nanosleep",
    "nanosleep",
    
    // System info
    "uname", "getrlimit", "prlimit64",
    "sysinfo", "getrandom",
    
    // Architecture-specific
    "arch_prctl", "set_tid_address", "set_robust_list",
    
    // I/O multiplexing (needed for many CLI tools)
    "select", "pselect6", "poll", "ppoll",
    "epoll_create", "epoll_create1", "epoll_ctl", "epoll_wait", "epoll_pwait",
    
    // Terminal I/O
    "ioctl",
    
    // Futex (for threading support in libc)
    "futex", "get_robust_list",
    
    // File control
    "fcntl", "fcntl64",
    
    NULL
};

// Additional syscalls for build operations
static const char* build_syscalls[] = {
    // Process creation
    "fork", "vfork", "clone", "clone3",
    "execve", "execveat",
    "wait4", "waitid",
    
    // IPC
    "pipe", "pipe2",
    "socketpair",
    
    // More file operations
    "rename", "renameat", "renameat2",
    "unlink", "unlinkat",
    "mkdir", "mkdirat",
    "rmdir",
    "link", "linkat",
    "symlink", "symlinkat",
    "chmod", "fchmod", "fchmodat",
    "chown", "fchown", "fchownat", "lchown",
    "truncate", "ftruncate",
    "utimes", "utimensat", "futimesat",
    
    // Extended attributes
    "getxattr", "lgetxattr", "fgetxattr",
    "setxattr", "lsetxattr", "fsetxattr",
    "listxattr", "llistxattr", "flistxattr",
    "removexattr", "lremovexattr", "fremovexattr",
    
    // Capabilities
    "capget", "capset",
    
    // Filesystem
    "mount", "umount2",
    "statfs", "fstatfs",
    "sync", "syncfs", "fsync", "fdatasync",
    
    // Advanced memory
    "msync", "mincore",
    
    NULL
};

// Additional syscalls for network operations
static const char* network_syscalls[] = {
    // Socket operations
    "socket", "socketpair",
    "bind", "connect", "listen", "accept", "accept4",
    "getsockname", "getpeername",
    "sendto", "recvfrom",
    "sendmsg", "recvmsg", "sendmmsg", "recvmmsg",
    "setsockopt", "getsockopt",
    "shutdown",
    
    NULL
};

// Minimal filesystem paths for basic CLI applications
static const char* minimal_paths[] = {
    "/lib",      // Shared libraries
    "/lib64",    // 64-bit libraries
    "/usr/lib",  // User libraries
    "/etc/ld.so.cache",  // Dynamic linker cache
    "/etc/ld.so.conf",   // Dynamic linker config
    "/etc/ld.so.conf.d", // Dynamic linker config directory
    "/dev/null",
    "/dev/zero",
    "/dev/urandom",
    "/dev/random",
    "/dev/tty",
    "/proc/self", // Process self information
    "/sys/devices/system/cpu", // CPU information (for runtime optimization)
    NULL
};

static int add_syscalls_to_policy(struct containerv_policy* policy, const char* const* syscalls)
{
    for (int i = 0; syscalls[i] != NULL; i++) {
        if (policy->syscall_count >= MAX_SYSCALLS) {
            VLOG_ERROR("containerv", "policy: too many syscalls\n");
            errno = ENOMEM;
            return -1;
        }
        
        policy->syscalls[policy->syscall_count].name = strdup(syscalls[i]);
        if (policy->syscalls[policy->syscall_count].name == NULL) {
            return -1;
        }
        policy->syscall_count++;
    }
    return 0;
}

static int add_paths_to_policy(
    struct containerv_policy* policy,
    const char* const*        paths,
    enum containerv_fs_access access)
{
    for (int i = 0; paths[i] != NULL; i++) {
        if (policy->path_count >= MAX_PATHS) {
            VLOG_ERROR("containerv", "policy: too many paths\n");
            errno = ENOMEM;
            return -1;
        }
        
        policy->paths[policy->path_count].path = strdup(paths[i]);
        if (policy->paths[policy->path_count].path == NULL) {
            return -1;
        }
        policy->paths[policy->path_count].access = access;
        policy->path_count++;
    }
    return 0;
}

struct containerv_policy* containerv_policy_new(enum containerv_policy_type type)
{
    struct containerv_policy* policy;
    
    policy = calloc(1, sizeof(struct containerv_policy));
    if (policy == NULL) {
        return NULL;
    }
    
    policy->type = type;
    
    // Add base syscalls depending on policy type
    switch (type) {
        case CV_POLICY_MINIMAL:
            if (add_syscalls_to_policy(policy, minimal_syscalls) != 0) {
                goto error;
            }
            if (add_paths_to_policy(policy, minimal_paths, CV_FS_READ | CV_FS_EXEC) != 0) {
                goto error;
            }
            break;
            
        case CV_POLICY_BUILD:
            if (add_syscalls_to_policy(policy, minimal_syscalls) != 0 ||
                add_syscalls_to_policy(policy, build_syscalls) != 0) {
                goto error;
            }
            if (add_paths_to_policy(policy, minimal_paths, CV_FS_READ | CV_FS_EXEC) != 0) {
                goto error;
            }
            // Build containers need write access to working directory (handled at container creation)
            break;
            
        case CV_POLICY_NETWORK:
            if (add_syscalls_to_policy(policy, minimal_syscalls) != 0 ||
                add_syscalls_to_policy(policy, network_syscalls) != 0) {
                goto error;
            }
            if (add_paths_to_policy(policy, minimal_paths, CV_FS_READ | CV_FS_EXEC) != 0) {
                goto error;
            }
            break;
            
        case CV_POLICY_CUSTOM:
            // Start with empty policy
            break;
    }
    
    return policy;
    
error:
    containerv_policy_delete(policy);
    return NULL;
}

void containerv_policy_delete(struct containerv_policy* policy)
{
    if (policy == NULL) {
        return;
    }
    
    // Free syscall names
    for (int i = 0; i < policy->syscall_count; i++) {
        free(policy->syscalls[i].name);
    }
    
    // Free path strings
    for (int i = 0; i < policy->path_count; i++) {
        free(policy->paths[i].path);
    }
    
    // Free deny path strings
    for (int i = 0; i < policy->deny_path_count; i++) {
        free(policy->deny_paths[i].path);
    }
    
    free(policy);
}

int containerv_policy_add_syscalls(
    struct containerv_policy* policy,
    const char* const*        syscalls)
{
    if (policy == NULL || syscalls == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    return add_syscalls_to_policy(policy, syscalls);
}

int containerv_policy_add_path(
    struct containerv_policy* policy,
    const char*               path,
    enum containerv_fs_access access)
{
    if (policy == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (policy->path_count >= MAX_PATHS) {
        VLOG_ERROR("containerv", "policy: too many paths\n");
        errno = ENOMEM;
        return -1;
    }
    
    policy->paths[policy->path_count].path = strdup(path);
    if (policy->paths[policy->path_count].path == NULL) {
        return -1;
    }
    policy->paths[policy->path_count].access = access;
    policy->path_count++;
    
    return 0;
}

int containerv_policy_add_paths(
    struct containerv_policy* policy,
    const char* const*        paths,
    enum containerv_fs_access access)
{
    if (policy == NULL || paths == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    return add_paths_to_policy(policy, paths, access);
}

int containerv_policy_deny_path(
    struct containerv_policy* policy,
    const char*               path,
    enum containerv_fs_access deny_mask)
{
    if (policy == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (policy->deny_path_count >= MAX_DENY_PATHS) {
        VLOG_ERROR("containerv", "policy: too many deny paths\n");
        errno = ENOMEM;
        return -1;
    }
    
    policy->deny_paths[policy->deny_path_count].path = strdup(path);
    if (policy->deny_paths[policy->deny_path_count].path == NULL) {
        return -1;
    }
    policy->deny_paths[policy->deny_path_count].deny_mask = deny_mask;
    policy->deny_path_count++;
    
    VLOG_DEBUG("containerv", "policy: added deny rule for %s (mask=0x%x)\n", 
               path, deny_mask);
    
    return 0;
}

int containerv_policy_deny_paths(
    struct containerv_policy* policy,
    const char* const*        paths,
    enum containerv_fs_access deny_mask)
{
    if (policy == NULL || paths == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    for (int i = 0; paths[i] != NULL; i++) {
        if (containerv_policy_deny_path(policy, paths[i], deny_mask) != 0) {
            return -1;
        }
    }
    
    return 0;
}

