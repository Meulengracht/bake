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

// Only support negative args for syscalls where we understand the glibc/kernel
// prototypes and behavior. This lists all the syscalls that support negative
// arguments where we want to ignore the high 32 bits (ie, we'll mask it since
// the arg is known to be 32 bit (uid_t/gid_t) and the kernel accepts one
// or both of uint32(-1) and uint64(-1) and does its own masking).
static const char* g_syscallsWithNegArgsMaskHi32[] = {
	"chown",
	"chown32",
	"fchown",
	"fchown32",
	"fchownat",
	"lchown",
	"lchown32",
	"setgid",
	"setgid32",
	"setregid",
	"setregid32",
	"setresgid",
	"setresgid32",
	"setreuid",
	"setreuid32",
	"setresuid",
	"setresuid32",
	"setuid",
	"setuid32",
	"copy_file_range",
};

// Minimal syscall set for basic CLI applications
static const char* g_baseSyscalls[] = {
    // Process management
    "exit",
    "exit_group",
    "wait4",
    "waitid",

    // Process creation / execution (PID1 needs this to spawn workloads)
    // Note: libc may use clone/vfork under the hood.
    "kill",
    "fork",
    "vfork",
    "clone",
    "clone3",
    "execve",
    "execveat",
    
    // File I/O
    "read",
    "write",
    "open",
    "openat",
    "close",
    "lseek",
    "llseek",
    "_llseek",
    "dup",
    "dup2",
    "dup3",
    
    // File information
    "stat",
    "fstat",
    "lstat",
    "newfstatat",
    "statx",
    "access",
    "faccessat",
    "faccessat2",
    "readlink",
    "readlinkat",
    
    // Directory operations
    "getcwd",
    "chdir",
    "fchdir",
    "getdents",
    "getdents64",
    
    // Memory management
    "brk",
    "mmap",
    "mmap2",
    "munmap",
    "mremap",
    "mprotect",
    "madvise",
    
    // Process information
    "getpid",
    "gettid",
    "getppid",
    "getpgid",
    "getpgrp",
    "getsid",
    "getitimer",
    "getpriority",
    "get_thread_area",
    "get_mempolicy",
    "pread",
    "pread64",
    "preadv",
    
    // User and group management
    "getegid",
    "getegid32",
    "geteuid",
    "geteuid32",
    "getgid",
    "getgid32",
    "getuid",
    "getuid32",
    "getgroups",
    "getgroups32",
    "getresgid",
    "getresgid32",
    "getresuid",
    "getresuid32",

    // Signal handling
    "rt_sigaction",
    "rt_sigprocmask",
    "rt_sigreturn",
    "sigaltstack",
    
    // Time
    "time",
    "gettimeofday",
    "clock_gettime",
    "clock_nanosleep",
    "nanosleep",
    
    // System info
    "uname",
    "getrlimit",
    "prlimit64",
    "sysinfo",
    "getrandom",
    "getcpu",
    "getrlimit",
    "ugetrlimit",
    "getrusage",
    
    // Architecture-specific
    "arch_prctl",
    "set_tid_address",
    "set_robust_list",
    
    // I/O multiplexing (needed for many CLI tools)
    "select",
    "pselect6",
    "poll",
    "ppoll",
    "epoll_create",
    "epoll_create1",
    "epoll_ctl",
    "epoll_wait",
    "epoll_pwait",

    // Unix-domain control socket IPC (containerv PID1 <-> manager)
    "sendmsg",
    "recvmsg",
    
    // glibc 2.35 unconditionally calls rseq for all threads
    "rseq",

    // Terminal I/O
    "ioctl",
    
    // Futex (for threading support in libc)
    "futex",
    "futex_requeue",
    "futex_time64",
    "futex_wait",
    "futex_waitv",
    "futex_wake",
    "get_robust_list",
    
    // File control
    "fcntl",
    "fcntl64",
    "flock",
    "ftime",
    "umask",

    // Extended attributes
    "getxattr",
    "fgetxattr",
    "lgetxattr",
    "getxattrat",
    "listxattr",
    "llistxattr",
    "flistxattr",
    
    // IPC
    "pipe",
    "pipe2",
    "socketpair",
    
    NULL
};

// Additional syscalls for build operations
static const char* g_buildSyscalls[] = {
    // More file operations
    "write",
    "writev",
    "rename",
    "renameat",
    "renameat2",
    "unlink",
    "unlinkat",
    "mkdir",
    "mkdirat",
    "rmdir",
    "link",
    "linkat",
    "symlink",
    "symlinkat",
    "chmod",
    "fchmod",
    "fchmodat",
    "chown",
    "fchown",
    "fchownat",
    "lchown",
    "truncate",
    "ftruncate",
    "utimes",
    "utimensat",
    "futimesat",

    // Process management
    "pwrite",
    "pwrite64",
    "pwritev",
    "pwritev2",
    
    // Extended attributes
    "setxattr",
    "lsetxattr",
    "fsetxattr",
    "removexattr",
    "lremovexattr",
    "fremovexattr",
    
    // Capabilities
    "capget",
    "capset",
    
    // Filesystem
    "mount",
    "umount2",
    "statfs",
    "fstatfs",
    "sync",
    "syncfs",
    "fsync",
    "fdatasync",
    "sync_file_range",
    "sync_file_range2",
    "arm_sync_file_range",

    // Advanced memory
    "msync",
    "mincore",
    "madvise",
    NULL
};

// Additional syscalls for network operations
static const char* g_networkSyscalls[] = {
    // Socket operations
    "socket",
    "bind",
    "connect",
    "listen",
    "accept",
    "accept4",
    "getsockname",
    "getpeername",
    "sendto",
    "recvfrom",
    "sendmmsg",
    "recvmmsg",
    "setsockopt",
    "getsockopt",
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
