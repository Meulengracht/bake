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

#define __SC_ENTRY_BASIC(name) { name, NULL, 0 }
#define __SC_ENTRY_ARGS(name, args) { name, args, 0 }
#define __SC_ENTRY_ARGS_FLAGS(name, args, flags) { name, args, flags }

// Only support negative args for syscalls where we understand the glibc/kernel
// prototypes and behavior. This lists all the syscalls that support negative
// arguments where we want to ignore the high 32 bits (ie, we'll mask it since
// the arg is known to be 32 bit (uid_t/gid_t) and the kernel accepts one
// or both of uint32(-1) and uint64(-1) and does its own masking).
static const char* g_syscallsWithNegArgsMaskHi32[] = {
	"copy_file_range",
};

// Minimal syscall set for basic CLI applications
static const struct containerv_syscall_entry g_baseSyscalls[] = {
    // ARM private syscalls
    __SC_ENTRY_BASIC("breakpoint"),
    __SC_ENTRY_BASIC("cacheflush"),
    __SC_ENTRY_BASIC("get_tls"),
    __SC_ENTRY_BASIC("set_tls"),
    __SC_ENTRY_BASIC("usr26"),
    __SC_ENTRY_BASIC("usr32"),

    // Process management
    __SC_ENTRY_BASIC("_exit"),
    __SC_ENTRY_BASIC("exit"),
    __SC_ENTRY_BASIC("exit_group"),
    __SC_ENTRY_BASIC("wait4"),
    __SC_ENTRY_BASIC("waitid"),
    __SC_ENTRY_BASIC("brk"),

    // Process creation / execution (PID1 needs this to spawn workloads)
    // Note: libc may use clone/vfork under the hood.
    __SC_ENTRY_BASIC("kill"),
    __SC_ENTRY_BASIC("fork"),
    __SC_ENTRY_BASIC("vfork"),
    __SC_ENTRY_BASIC("clone"),
    __SC_ENTRY_BASIC("clone3"),
    __SC_ENTRY_BASIC("execve"),
    __SC_ENTRY_BASIC("execveat"),

    // File I/O
    __SC_ENTRY_BASIC("access"),
    __SC_ENTRY_BASIC("faccessat"),
    __SC_ENTRY_BASIC("faccessat2"),
    __SC_ENTRY_BASIC("read"),
    __SC_ENTRY_BASIC("write"),
    __SC_ENTRY_BASIC("open"),
    __SC_ENTRY_BASIC("openat"),
    __SC_ENTRY_BASIC("close"),
    __SC_ENTRY_BASIC("close_range"),
    __SC_ENTRY_BASIC("lseek"),
    __SC_ENTRY_BASIC("llseek"),
    __SC_ENTRY_BASIC("_llseek"),
    __SC_ENTRY_BASIC("dup"),
    __SC_ENTRY_BASIC("dup2"),
    __SC_ENTRY_BASIC("dup3"),
    
    // File information
    __SC_ENTRY_BASIC("stat"),
    __SC_ENTRY_BASIC("stat64"),
    __SC_ENTRY_BASIC("fstat"),
    __SC_ENTRY_BASIC("fstat64"),
    __SC_ENTRY_BASIC("fstatat64"),
    __SC_ENTRY_BASIC("lstat"),
    __SC_ENTRY_BASIC("lstat64"),
    __SC_ENTRY_BASIC("newfstatat"),
    __SC_ENTRY_BASIC("oldfstat"),
    __SC_ENTRY_BASIC("oldlstat"),
    __SC_ENTRY_BASIC("oldstat"),
    __SC_ENTRY_BASIC("statx"),
    __SC_ENTRY_BASIC("access"),
    __SC_ENTRY_BASIC("faccessat"),
    __SC_ENTRY_BASIC("faccessat2"),
    __SC_ENTRY_BASIC("readlink"),
    __SC_ENTRY_BASIC("readlinkat"),
    __SC_ENTRY_BASIC("readahead"),
    __SC_ENTRY_BASIC("readdir"),

    // Filesystem information
    __SC_ENTRY_BASIC("statfs"),
    __SC_ENTRY_BASIC("statfs64"),
    __SC_ENTRY_BASIC("fstatfs"),
    __SC_ENTRY_BASIC("fstatfs64"),
    __SC_ENTRY_BASIC("statvfs"),
    __SC_ENTRY_BASIC("fstatvfs"),
    __SC_ENTRY_BASIC("ustat"),

    // Directory operations
    __SC_ENTRY_BASIC("getcwd"),
    __SC_ENTRY_BASIC("chdir"),
    __SC_ENTRY_BASIC("fchdir"),
    __SC_ENTRY_BASIC("getdents"),
    __SC_ENTRY_BASIC("getdents64"),
    
    // Memory management
    __SC_ENTRY_BASIC("brk"),
    __SC_ENTRY_BASIC("mmap"),
    __SC_ENTRY_BASIC("mmap2"),
    __SC_ENTRY_BASIC("munmap"),
    __SC_ENTRY_BASIC("mremap"),
    __SC_ENTRY_BASIC("mprotect"),
    __SC_ENTRY_BASIC("madvise"),
    __SC_ENTRY_BASIC("msync"),
    __SC_ENTRY_BASIC("mincore"),
    __SC_ENTRY_BASIC("msgctl"),
    __SC_ENTRY_BASIC("msgget"),
    __SC_ENTRY_BASIC("msgrcv"),
    __SC_ENTRY_BASIC("msgsnd"),
    __SC_ENTRY_BASIC("munlock"),
    __SC_ENTRY_BASIC("munlockall"),
    __SC_ENTRY_BASIC("mbind"),
    __SC_ENTRY_BASIC("membarrier"),
    __SC_ENTRY_BASIC("memfd_create"),
    __SC_ENTRY_BASIC("mlock"),
    __SC_ENTRY_BASIC("mlock2"),
    __SC_ENTRY_BASIC("mlockall"),
    __SC_ENTRY_BASIC("set_mempolicy"),
    // mseal - - 0

    // Shared memory managment
    __SC_ENTRY_BASIC("shmat"),
    __SC_ENTRY_BASIC("shmctl"),
    __SC_ENTRY_BASIC("shmdt"),
    __SC_ENTRY_BASIC("shmget"),

    // Process information
    __SC_ENTRY_BASIC("getpid"),
    __SC_ENTRY_BASIC("gettid"),
    __SC_ENTRY_BASIC("getppid"),
    __SC_ENTRY_BASIC("getpgid"),
    __SC_ENTRY_BASIC("getpgrp"),
    __SC_ENTRY_BASIC("getsid"),
    __SC_ENTRY_BASIC("getitimer"),
    __SC_ENTRY_BASIC("getpriority"),
    __SC_ENTRY_BASIC("get_thread_area"),
    __SC_ENTRY_BASIC("get_mempolicy"),
    __SC_ENTRY_BASIC("pread"),
    __SC_ENTRY_BASIC("pread64"),
    __SC_ENTRY_BASIC("preadv"),
    __SC_ENTRY_BASIC("map_shadow_stack"),

    // Process management
    __SC_ENTRY_BASIC("prctl"),
    __SC_ENTRY_BASIC("sched_yield"),
    __SC_ENTRY_BASIC("setsid"),
    __SC_ENTRY_BASIC("tgkill"),
    __SC_ENTRY_BASIC("tkill"),

    // Landlock management
    __SC_ENTRY_BASIC("landlock_create_ruleset"),
    __SC_ENTRY_BASIC("landlock_add_rule"),
    __SC_ENTRY_BASIC("landlock_restrict_self"),

    // Kernel only allows tightening the filter
    __SC_ENTRY_BASIC("seccomp"),
    
    // User and group management
    __SC_ENTRY_BASIC("getegid"),
    __SC_ENTRY_BASIC("getegid32"),
    __SC_ENTRY_BASIC("geteuid"),
    __SC_ENTRY_BASIC("geteuid32"),
    __SC_ENTRY_BASIC("getgid"),
    __SC_ENTRY_BASIC("getgid32"),
    __SC_ENTRY_BASIC("getuid"),
    __SC_ENTRY_BASIC("getuid32"),
    __SC_ENTRY_BASIC("getgroups"),
    __SC_ENTRY_BASIC("getgroups32"),
    __SC_ENTRY_BASIC("getresgid"),
    __SC_ENTRY_BASIC("getresgid32"),
    __SC_ENTRY_BASIC("getresuid"),
    __SC_ENTRY_BASIC("getresuid32"),

    // Signal handling
    __SC_ENTRY_BASIC("signal"),
    __SC_ENTRY_BASIC("sigaction"),
    __SC_ENTRY_BASIC("signalfd"),
    __SC_ENTRY_BASIC("signalfd4"),
    __SC_ENTRY_BASIC("sigaltstack"),
    __SC_ENTRY_BASIC("sigpending"),
    __SC_ENTRY_BASIC("sigprocmask"),
    __SC_ENTRY_BASIC("sigreturn"),
    __SC_ENTRY_BASIC("sigsuspend"),
    __SC_ENTRY_BASIC("sigtimedwait"),
    __SC_ENTRY_BASIC("sigwaitinfo"),
    __SC_ENTRY_BASIC("rt_sigaction"),
    __SC_ENTRY_BASIC("rt_sigprocmask"),
    __SC_ENTRY_BASIC("rt_sigreturn"),
    __SC_ENTRY_BASIC("sigaltstack"),
    __SC_ENTRY_BASIC("rt_sigpending"),
    __SC_ENTRY_BASIC("rt_sigqueueinfo"),
    __SC_ENTRY_BASIC("rt_sigsuspend"),
    __SC_ENTRY_BASIC("rt_sigtimedwait"),
    __SC_ENTRY_BASIC("rt_sigtimedwait_time64"),
    __SC_ENTRY_BASIC("rt_tgsigqueueinfo"),
    
    // Time
    __SC_ENTRY_BASIC("alarm"),
    __SC_ENTRY_BASIC("time"),
    __SC_ENTRY_BASIC("gettimeofday"),
    __SC_ENTRY_BASIC("clock_getres"),
    __SC_ENTRY_BASIC("clock_getres_time64"),
    __SC_ENTRY_BASIC("clock_gettime"),
    __SC_ENTRY_BASIC("clock_gettime64"),
    __SC_ENTRY_BASIC("clock_nanosleep"),
    __SC_ENTRY_BASIC("clock_nanosleep_time64"),
    __SC_ENTRY_BASIC("nanosleep"),
    __SC_ENTRY_BASIC("timer_create"),
    __SC_ENTRY_BASIC("timer_delete"),
    __SC_ENTRY_BASIC("timer_getoverrun"),
    __SC_ENTRY_BASIC("timer_gettime"),
    __SC_ENTRY_BASIC("timer_gettime64"),
    __SC_ENTRY_BASIC("timer_settime"),
    __SC_ENTRY_BASIC("timer_settime64"),
    __SC_ENTRY_BASIC("timerfd"),
    __SC_ENTRY_BASIC("timerfd_create"),
    __SC_ENTRY_BASIC("timerfd_gettime"),
    __SC_ENTRY_BASIC("timerfd_gettime64"),
    __SC_ENTRY_BASIC("timerfd_settime"),
    __SC_ENTRY_BASIC("timerfd_settime64"),
    
    // System info
    __SC_ENTRY_BASIC("uname"),
    __SC_ENTRY_BASIC("olduname"),
    __SC_ENTRY_BASIC("oldolduname"),
    __SC_ENTRY_BASIC("prlimit64"),
    __SC_ENTRY_BASIC("sysinfo"),
    __SC_ENTRY_BASIC("syslog"),
    __SC_ENTRY_BASIC("getrandom"),
    __SC_ENTRY_BASIC("getcpu"),
    __SC_ENTRY_BASIC("getrlimit"),
    __SC_ENTRY_BASIC("ugetrlimit"),
    __SC_ENTRY_BASIC("getrusage"),
    
    // Architecture-specific
    __SC_ENTRY_BASIC("arch_prctl"),
    __SC_ENTRY_BASIC("set_tid_address"),
    __SC_ENTRY_BASIC("set_robust_list"),
    
    // I/O multiplexing (needed for many CLI tools)
    __SC_ENTRY_BASIC("select"),
    __SC_ENTRY_BASIC("_newselect"),
    __SC_ENTRY_BASIC("pselect"),
    __SC_ENTRY_BASIC("pselect6"),
    __SC_ENTRY_BASIC("pselect6_time64"),
    __SC_ENTRY_BASIC("poll"),
    __SC_ENTRY_BASIC("ppoll"),
    __SC_ENTRY_BASIC("ppoll_time64"),
    __SC_ENTRY_BASIC("epoll_create"),
    __SC_ENTRY_BASIC("epoll_create1"),
    __SC_ENTRY_BASIC("epoll_ctl"),
    __SC_ENTRY_BASIC("epoll_ctl_old"),
    __SC_ENTRY_BASIC("epoll_wait"),
    __SC_ENTRY_BASIC("epoll_wait_old"),
    __SC_ENTRY_BASIC("epoll_pwait"),
    __SC_ENTRY_BASIC("epoll_pwait2"),
    __SC_ENTRY_BASIC("eventfd"),
    __SC_ENTRY_BASIC("eventfd2"),

    // glibc 2.35 unconditionally calls rseq for all threads
    __SC_ENTRY_BASIC("rseq"),

    // Scheduler information
    __SC_ENTRY_BASIC("sched_getaffinity"),
    __SC_ENTRY_BASIC("sched_getattr"),
    __SC_ENTRY_BASIC("sched_getparam"),
    __SC_ENTRY_BASIC("sched_get_priority_max"),
    __SC_ENTRY_BASIC("sched_get_priority_min"),
    __SC_ENTRY_BASIC("sched_getscheduler"),
    __SC_ENTRY_BASIC("sched_rr_get_interval"),
    __SC_ENTRY_BASIC("sched_rr_get_interval_time64"),

    // Terminal I/O
    __SC_ENTRY_BASIC("ioctl"),
    
    // Futex (for threading support in libc)
    __SC_ENTRY_BASIC("futex"),
    __SC_ENTRY_BASIC("futex_requeue"),
    __SC_ENTRY_BASIC("futex_time64"),
    __SC_ENTRY_BASIC("futex_wait"),
    __SC_ENTRY_BASIC("futex_waitv"),
    __SC_ENTRY_BASIC("futex_wake"),
    __SC_ENTRY_BASIC("get_robust_list"),
    
    // File control
    __SC_ENTRY_BASIC("fcntl"),
    __SC_ENTRY_BASIC("fcntl64"),
    __SC_ENTRY_BASIC("flock"),
    __SC_ENTRY_BASIC("ftime"),
    __SC_ENTRY_BASIC("umask"),
    __SC_ENTRY_BASIC("fadvise64"),
    __SC_ENTRY_BASIC("fadvise64_64"),
    __SC_ENTRY_BASIC("arm_fadvise64_64"),

    // Extended attributes
    __SC_ENTRY_BASIC("getxattr"),
    __SC_ENTRY_BASIC("fgetxattr"),
    __SC_ENTRY_BASIC("lgetxattr"),
    __SC_ENTRY_BASIC("getxattrat"),
    __SC_ENTRY_BASIC("listxattr"),
    __SC_ENTRY_BASIC("llistxattr"),
    __SC_ENTRY_BASIC("flistxattr"),
    
    // Filesystem information
    __SC_ENTRY_BASIC("chroot"),
    __SC_ENTRY_BASIC("sync"),
    __SC_ENTRY_BASIC("syncfs"),
    __SC_ENTRY_BASIC("fsync"),
    __SC_ENTRY_BASIC("fdatasync"),
    __SC_ENTRY_BASIC("sync_file_range"),
    __SC_ENTRY_BASIC("sync_file_range2"),
    __SC_ENTRY_BASIC("arm_sync_file_range"),

    // Capabilities
    __SC_ENTRY_BASIC("capget"),
    __SC_ENTRY_BASIC("capset"),

    // Socket operations
    // Unix-domain control socket IPC (containerv PID1 <-> manager)
    __SC_ENTRY_BASIC("socket"),
    __SC_ENTRY_BASIC("connect"),
    __SC_ENTRY_BASIC("getsockname"),
    __SC_ENTRY_BASIC("getpeername"),
    __SC_ENTRY_BASIC("setsockopt"),
    __SC_ENTRY_BASIC("getsockopt"),
    __SC_ENTRY_BASIC("shutdown"),
    __SC_ENTRY_BASIC("sendto"),
    __SC_ENTRY_BASIC("recvfrom"),
    __SC_ENTRY_BASIC("sendmmsg"),
    __SC_ENTRY_BASIC("recvmmsg"),
    __SC_ENTRY_BASIC("sendmsg"),
    __SC_ENTRY_BASIC("recvmsg"),

    // IPC
    __SC_ENTRY_BASIC("pipe"),
    __SC_ENTRY_BASIC("pipe2"),
    // !pipe2 - |O_NOTIFICATION_PIPE
    __SC_ENTRY_BASIC("socketpair"),
    
    // allow use of setgroups(0, ...). Note: while the setgroups() man page states
    // that 'setgroups(0, NULL) should be used to clear all supplementary groups,
    // the kernel will not consult the group list when size is '0', so we allow it
    // to be anything for compatibility with (arguably buggy) programs that expect
    // to clear the groups with 'setgroups(0, <non-null>).
    __SC_ENTRY_ARGS_FLAGS("setgroups", "0 -", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setgroups32", "0 -", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setgid to root
    __SC_ENTRY_ARGS_FLAGS("setgid", "g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setgid32", "g:root", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setuid to root
    __SC_ENTRY_ARGS_FLAGS("setuid", "u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setuid32", "u:root", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setregid to root
    __SC_ENTRY_ARGS_FLAGS("setregid", "g:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setregid32", "g:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setregid", "-1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setregid32", "-1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setregid", "g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setregid32", "g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setresgid to root
    // (permanent drop)
    __SC_ENTRY_ARGS_FLAGS("setresgid", "g:root g:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "g:root g:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    
    // (setegid)
    __SC_ENTRY_ARGS_FLAGS("setresgid", "-1 g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "-1 g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    
    // (setgid equivalent)
    __SC_ENTRY_ARGS_FLAGS("setresgid", "g:root g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "g:root g:root -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setreuid to root
    __SC_ENTRY_ARGS_FLAGS("setreuid", "u:root u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setreuid32", "u:root u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setreuid", "-1 u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setreuid32", "-1 u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setreuid", "u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setreuid32", "u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setresuid to root
    // (permanent drop)
    __SC_ENTRY_ARGS_FLAGS("setresuid", "u:root u:root u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "u:root u:root u:root", SYSCALL_FLAG_NEGATIVE_ARG),
    
    // (seteuid)
    __SC_ENTRY_ARGS_FLAGS("setresuid", "-1 u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "-1 u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    
    // (setuid equivalent)
    __SC_ENTRY_ARGS_FLAGS("setresuid", "u:root u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "u:root u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    
    NULL
};

// Additional filesystem calls modifying files
static const struct containerv_syscall_entry g_fileControlSyscalls[] = {
    // More file operations
    __SC_ENTRY_BASIC("creat"),
    __SC_ENTRY_BASIC("fallocate"),
    __SC_ENTRY_BASIC("write"),
    __SC_ENTRY_BASIC("writev"),
    __SC_ENTRY_BASIC("rename"),
    __SC_ENTRY_BASIC("renameat"),
    __SC_ENTRY_BASIC("renameat2"),
    __SC_ENTRY_BASIC("unlink"),
    __SC_ENTRY_BASIC("unlinkat"),
    __SC_ENTRY_BASIC("mkdir"),
    __SC_ENTRY_BASIC("mkdirat"),
    __SC_ENTRY_BASIC("rmdir"),
    __SC_ENTRY_BASIC("link"),
    __SC_ENTRY_BASIC("linkat"),
    __SC_ENTRY_BASIC("symlink"),
    __SC_ENTRY_BASIC("symlinkat"),
    __SC_ENTRY_BASIC("chmod"),
    __SC_ENTRY_BASIC("fchmod"),
    __SC_ENTRY_BASIC("fchmodat"),
    __SC_ENTRY_BASIC("chown"),
    __SC_ENTRY_BASIC("fchown"),
    __SC_ENTRY_BASIC("fchownat"),
    __SC_ENTRY_BASIC("lchown"),
    __SC_ENTRY_BASIC("truncate"),
    __SC_ENTRY_BASIC("ftruncate"),
    __SC_ENTRY_BASIC("utime"),
    __SC_ENTRY_BASIC("utimes"),
    __SC_ENTRY_BASIC("utimensat"),
    __SC_ENTRY_BASIC("utimensat_time64"),
    __SC_ENTRY_BASIC("futimesat"),

    // Extended attributes
    __SC_ENTRY_BASIC("setxattr"),
    __SC_ENTRY_BASIC("lsetxattr"),
    __SC_ENTRY_BASIC("fsetxattr"),
    __SC_ENTRY_BASIC("removexattr"),
    __SC_ENTRY_BASIC("lremovexattr"),
    __SC_ENTRY_BASIC("fremovexattr"),

    // We can't effectively block file perms due to open() with O_CREAT
    __SC_ENTRY_BASIC("chmod"),
    __SC_ENTRY_BASIC("fchmod"),
    __SC_ENTRY_BASIC("fchmodat"),
    __SC_ENTRY_BASIC("fchmodat2"),

    // Daemons typically run as 'root' so allow chown to 'root'. DAC will prevent
    // non-root from chowning to root.
    // (chown root:root)
    __SC_ENTRY_ARGS_FLAGS("chown", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("chown32", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown32", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchownat", "- - u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown32", "- u:root g:root", SYSCALL_FLAG_NEGATIVE_ARG),

    // (chown root)
    __SC_ENTRY_ARGS_FLAGS("chown", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("chown32", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown32", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchownat", "- - u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown32", "- u:root -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // (chgrp root)
    __SC_ENTRY_ARGS_FLAGS("chown", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("chown32", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchown32", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("fchownat", "- - -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("lchown32", "- -1 g:root", SYSCALL_FLAG_NEGATIVE_ARG),

    NULL
};

// Additional syscalls for package management (apt/gpgv drop privileges to _apt/nogroup)
static const struct containerv_syscall_entry g_packageManagementSyscalls[] = {
    // setgroups(size, list) with size=1, list pointer ignored
    __SC_ENTRY_ARGS_FLAGS("setgroups", "1 -", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setgroups32", "1 -", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setresgid to nogroup
    __SC_ENTRY_ARGS_FLAGS("setresgid", "g:nogroup g:nogroup g:nogroup", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "g:nogroup g:nogroup g:nogroup", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid", "-1 g:nogroup -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "-1 g:nogroup -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid", "g:nogroup g:nogroup -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresgid32", "g:nogroup g:nogroup -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // allow setresuid to _apt
    __SC_ENTRY_ARGS_FLAGS("setresuid", "u:_apt u:_apt u:_apt", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "u:_apt u:_apt u:_apt", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid", "-1 u:_apt -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "-1 u:_apt -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid", "u:_apt u:_apt -1", SYSCALL_FLAG_NEGATIVE_ARG),
    __SC_ENTRY_ARGS_FLAGS("setresuid32", "u:_apt u:_apt -1", SYSCALL_FLAG_NEGATIVE_ARG),

    // Apt tries to bind a socket
    __SC_ENTRY_BASIC("bind"),
    __SC_ENTRY_BASIC("listen"),
    __SC_ENTRY_BASIC("accept"),
    __SC_ENTRY_BASIC("accept4"),
    NULL
};

// Additional syscalls for process control
static const struct containerv_syscall_entry g_processControlSyscalls[] = {
    // Process management
    __SC_ENTRY_BASIC("pwrite"),
    __SC_ENTRY_BASIC("pwrite64"),
    __SC_ENTRY_BASIC("pwritev2"),
    __SC_ENTRY_BASIC("pwritev"),
    
    NULL
};

// Additional syscalls for mount operations
static const struct containerv_syscall_entry g_mountSyscalls[] = {
    __SC_ENTRY_BASIC("mount"),
    __SC_ENTRY_BASIC("umount"),
    __SC_ENTRY_BASIC("umount2"),

    NULL
};

// Additional syscalls for network operations
static const struct containerv_syscall_entry g_networkSyscalls[] = {
    __SC_ENTRY_BASIC("bind"),
    __SC_ENTRY_BASIC("listen"),
    __SC_ENTRY_BASIC("accept"),
    __SC_ENTRY_BASIC("accept4"),
    
    NULL
};

static int add_syscalls_to_policy(struct containerv_policy* policy, const struct containerv_syscall_entry* syscalls)
{
    for (int i = 0; syscalls[i].name != NULL; i++) {
        if (policy->syscall_count >= MAX_SYSCALLS) {
            VLOG_ERROR("containerv", "policy: too many syscalls\n");
            errno = ENOMEM;
            return -1;
        }
        
        policy->syscalls[policy->syscall_count].name = syscalls[i].name;
        policy->syscalls[policy->syscall_count].args = syscalls[i].args;
        policy->syscalls[policy->syscall_count].flags = syscalls[i].flags;

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
    } else if (strcmp(plugin->name, "network-bind") == 0) {
        return add_syscalls_to_policy(policy, g_networkSyscalls);
    } else if (strcmp(plugin->name, "process-control") == 0) {
        return add_syscalls_to_policy(policy, g_processControlSyscalls);
    } else if (strcmp(plugin->name, "file-control") == 0) {
        return add_syscalls_to_policy(policy, g_fileControlSyscalls);
    } else if (strcmp(plugin->name, "package-management") == 0) {
        return add_syscalls_to_policy(policy, g_packageManagementSyscalls);
    } else {
        VLOG_ERROR("containerv", "policy_seccomp: unknown plugin '%s'\n", plugin->name);
        errno = EINVAL;
        return -1;
    }
}
