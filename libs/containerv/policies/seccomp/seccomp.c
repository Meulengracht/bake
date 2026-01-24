/**
 * Copyright, Philip Meulengracht
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

#include <errno.h>
#include <seccomp.h>
#include <string.h>
#include <vlog.h>

#include "../private.h"

// Minimal syscall set for basic CLI applications
static const char* g_baseSyscalls[] = {
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
static const char* g_buildSyscalls[] = {
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
static const char* g_networkSyscalls[] = {
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

int policy_seccomp_build(struct containerv_policy* policy, struct containerv_policy_plugin* plugin)
{
    if (policy == NULL || plugin == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (strcmp(plugin->name, "minimal") == 0) {
        return add_syscalls_to_policy(policy, g_baseSyscalls);
    } else if (strcmp(plugin->name, "build") == 0) {
        return add_syscalls_to_policy(policy, g_buildSyscalls);
    } else if (strcmp(plugin->name, "network") == 0) {
        return add_syscalls_to_policy(policy, g_networkSyscalls);
    } else {
        VLOG_ERROR("containerv", "policy_seccomp: unknown plugin '%s'\n", plugin->name);
        errno = EINVAL;
        return -1;
    }
}
