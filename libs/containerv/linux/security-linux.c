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

#include <chef/containerv.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/capability.h>
#endif

// Linux-specific security implementation

#ifdef __linux__

// Capability name mapping
static const struct {
    enum containerv_linux_capability cv_cap;
    int linux_cap;
    const char* name;
} capability_map[] = {
    {CV_CAP_CHOWN, CAP_CHOWN, "chown"},
    {CV_CAP_DAC_OVERRIDE, CAP_DAC_OVERRIDE, "dac_override"},
    {CV_CAP_FOWNER, CAP_FOWNER, "fowner"},
    {CV_CAP_KILL, CAP_KILL, "kill"},
    {CV_CAP_SETGID, CAP_SETGID, "setgid"},
    {CV_CAP_SETUID, CAP_SETUID, "setuid"},
    {CV_CAP_NET_BIND_SERVICE, CAP_NET_BIND_SERVICE, "net_bind_service"},
    {CV_CAP_NET_ADMIN, CAP_NET_ADMIN, "net_admin"},
    {CV_CAP_NET_RAW, CAP_NET_RAW, "net_raw"},
    {CV_CAP_SYS_CHROOT, CAP_SYS_CHROOT, "sys_chroot"},
    {CV_CAP_SYS_PTRACE, CAP_SYS_PTRACE, "sys_ptrace"},
    {CV_CAP_SYS_ADMIN, CAP_SYS_ADMIN, "sys_admin"},
    {CV_CAP_SYS_MODULE, CAP_SYS_MODULE, "sys_module"},
    {CV_CAP_MKNOD, CAP_MKNOD, "mknod"},
    {CV_CAP_SETFCAP, CAP_SETFCAP, "setfcap"}
};

static const size_t capability_map_size = sizeof(capability_map) / sizeof(capability_map[0]);

static int get_linux_capability(enum containerv_linux_capability cv_cap) {
    for (size_t i = 0; i < capability_map_size; i++) {
        if (capability_map[i].cv_cap == cv_cap) {
            return capability_map[i].linux_cap;
        }
    }
    return -1;
}

static const char* get_capability_name(enum containerv_linux_capability cv_cap) {
    for (size_t i = 0; i < capability_map_size; i++) {
        if (capability_map[i].cv_cap == cv_cap) {
            return capability_map[i].name;
        }
    }
    return "unknown";
}

/**
 * @brief Apply capability restrictions to the current process
 * @param profile Security profile containing capability settings
 * @return 0 on success, -1 on failure
 */
int linux_apply_capabilities(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    cap_t caps = cap_get_proc();
    if (!caps) {
        return -1;
    }
    
    // Clear all capabilities first
    if (cap_clear(caps) != 0) {
        cap_free(caps);
        return -1;
    }
    
    // Add allowed capabilities
    for (int i = 0; i < 64; i++) {
        if (profile->allowed_caps & (1ULL << i)) {
            int linux_cap = get_linux_capability(i);
            if (linux_cap >= 0) {
                cap_value_t cap_value = linux_cap;
                if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap_value, CAP_SET) != 0 ||
                    cap_set_flag(caps, CAP_PERMITTED, 1, &cap_value, CAP_SET) != 0 ||
                    cap_set_flag(caps, CAP_INHERITABLE, 1, &cap_value, CAP_SET) != 0) {
                    cap_free(caps);
                    return -1;
                }
            }
        }
    }
    
    // Apply the capability set
    if (cap_set_proc(caps) != 0) {
        cap_free(caps);
        return -1;
    }
    
    cap_free(caps);
    return 0;
}

/**
 * @brief Set up no-new-privileges flag to prevent privilege escalation
 * @param profile Security profile
 * @return 0 on success, -1 on failure
 */
int linux_setup_no_new_privileges(const struct containerv_security_profile* profile) {
    if (!profile || !profile->no_new_privileges) {
        return 0;
    }
    
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Drop process privileges to specified user/group
 * @param profile Security profile containing user/group settings
 * @return 0 on success, -1 on failure
 */
int linux_drop_privileges(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    uid_t target_uid = profile->run_as_uid;
    gid_t target_gid = profile->run_as_gid;
    
    // If username is specified, look up UID/GID
    if (profile->run_as_user) {
        struct passwd* pwd = getpwnam(profile->run_as_user);
        if (pwd) {
            target_uid = pwd->pw_uid;
            target_gid = pwd->pw_gid;
        } else {
            // User not found, use numeric IDs if specified
            if (profile->run_as_uid == 0 && profile->run_as_gid == 0) {
                return -1; // No valid user specified
            }
        }
    }
    
    // Don't drop privileges if already running as target user
    if (getuid() == target_uid && getgid() == target_gid) {
        return 0;
    }
    
    // Initialize supplementary groups
    if (target_gid != 0) {
        if (setgroups(0, NULL) != 0) {
            return -1;
        }
        
        if (setgid(target_gid) != 0) {
            return -1;
        }
    }
    
    // Drop to target user
    if (target_uid != 0) {
        if (setuid(target_uid) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Generate a basic seccomp filter for syscall restrictions
 * @param profile Security profile
 * @param filter_len Output parameter for filter length
 * @return Allocated BPF filter program (caller must free)
 */
struct sock_filter* linux_generate_seccomp_filter(
    const struct containerv_security_profile* profile,
    size_t* filter_len
) {
    if (!profile || !filter_len) {
        return NULL;
    }
    
    // Basic seccomp filter that blocks dangerous syscalls
    // This is a simplified implementation - a production version would
    // be more sophisticated and configurable
    
    static struct sock_filter basic_filter[] = {
        // Load architecture
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        
        // Check architecture (x86_64)
#ifdef __x86_64__
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 0, 12),
#else
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 0, 12),
#endif
        
        // Load syscall number
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        
        // Block dangerous syscalls based on security level
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ptrace, 10, 0),        // Block ptrace
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_process_vm_readv, 9, 0), // Block process_vm_*
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_process_vm_writev, 8, 0),
        
        // For paranoid security, block more syscalls
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mount, 7, 0),          // Block mount
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_umount2, 6, 0),        // Block umount
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_swapon, 5, 0),         // Block swapon
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_swapoff, 4, 0),        // Block swapoff
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_reboot, 3, 0),         // Block reboot
        
#ifdef __NR_kexec_load
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_kexec_load, 2, 0),     // Block kexec_load
#endif
        
        // Allow everything else
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        
        // Kill process for blocked syscalls (paranoid mode)
        BPF_STMT(BPF_RET | BPF_K, profile->level >= CV_SECURITY_PARANOID ? 
                SECCOMP_RET_KILL : SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
        
        // Kill for wrong architecture
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL)
    };
    
    size_t filter_size = sizeof(basic_filter) / sizeof(basic_filter[0]);
    struct sock_filter* filter = malloc(filter_size * sizeof(struct sock_filter));
    if (!filter) {
        return NULL;
    }
    
    memcpy(filter, basic_filter, sizeof(basic_filter));
    *filter_len = filter_size;
    
    return filter;
}

/**
 * @brief Apply seccomp syscall filtering
 * @param profile Security profile
 * @return 0 on success, -1 on failure
 */
int linux_apply_seccomp_filter(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    // Skip seccomp for permissive mode
    if (profile->level == CV_SECURITY_PERMISSIVE) {
        return 0;
    }
    
    size_t filter_len;
    struct sock_filter* filter = linux_generate_seccomp_filter(profile, &filter_len);
    if (!filter) {
        return -1;
    }
    
    struct sock_fprog prog = {
        .len = filter_len,
        .filter = filter
    };
    
    // Apply the seccomp filter
    int result = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
    
    free(filter);
    
    if (result != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Set up AppArmor profile (if available)
 * @param profile_name AppArmor profile name
 * @return 0 on success, -1 on failure
 */
int linux_setup_apparmor_profile(const char* profile_name) {
    if (!profile_name) return 0;
    
    // Try to change to the specified AppArmor profile
    char apparmor_path[256];
    snprintf(apparmor_path, sizeof(apparmor_path), "/proc/self/attr/exec");
    
    int fd = open(apparmor_path, O_WRONLY);
    if (fd < 0) {
        // AppArmor not available or not enabled
        return -1;
    }
    
    char profile_spec[512];
    snprintf(profile_spec, sizeof(profile_spec), "exec %s", profile_name);
    
    ssize_t written = write(fd, profile_spec, strlen(profile_spec));
    close(fd);
    
    return (written > 0) ? 0 : -1;
}

/**
 * @brief Set up SELinux security context (if available)
 * @param context SELinux context string
 * @return 0 on success, -1 on failure
 */
int linux_setup_selinux_context(const char* context) {
    if (!context) return 0;
    
    // Try to set SELinux context for exec
    char selinux_path[256];
    snprintf(selinux_path, sizeof(selinux_path), "/proc/self/attr/exec");
    
    int fd = open(selinux_path, O_WRONLY);
    if (fd < 0) {
        // SELinux not available or not enabled
        return -1;
    }
    
    ssize_t written = write(fd, context, strlen(context));
    close(fd);
    
    return (written > 0) ? 0 : -1;
}

/**
 * @brief Apply comprehensive Linux security profile
 * @param profile Security profile to apply
 * @return 0 on success, -1 on failure
 */
int linux_apply_security_profile(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    // 1. Set up no-new-privileges first (must be done before other restrictions)
    if (linux_setup_no_new_privileges(profile) != 0) {
        fprintf(stderr, "Failed to set no-new-privileges\n");
        return -1;
    }
    
    // 2. Set up AppArmor profile
    if (profile->use_apparmor && profile->security_context) {
        if (linux_setup_apparmor_profile(profile->security_context) != 0) {
            fprintf(stderr, "Warning: Failed to set AppArmor profile: %s\n", profile->security_context);
            // Continue - AppArmor might not be available
        }
    }
    
    // 3. Set up SELinux context
    if (profile->use_selinux && profile->security_context) {
        if (linux_setup_selinux_context(profile->security_context) != 0) {
            fprintf(stderr, "Warning: Failed to set SELinux context: %s\n", profile->security_context);
            // Continue - SELinux might not be available
        }
    }
    
    // 4. Drop privileges
    if (linux_drop_privileges(profile) != 0) {
        fprintf(stderr, "Failed to drop privileges\n");
        return -1;
    }
    
    // 5. Apply capability restrictions
    if (linux_apply_capabilities(profile) != 0) {
        fprintf(stderr, "Failed to apply capabilities\n");
        return -1;
    }
    
    // 6. Apply seccomp filter (must be last due to restrictions)
    if (linux_apply_seccomp_filter(profile) != 0) {
        fprintf(stderr, "Warning: Failed to apply seccomp filter\n");
        // Continue - seccomp might not be available
    }
    
    return 0;
}

/**
 * @brief Verify that current process has expected security restrictions
 * @param profile Expected security profile
 * @return 0 if compliant, -1 if not
 */
int linux_verify_security_profile(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    // Check current UID/GID
    uid_t current_uid = getuid();
    gid_t current_gid = getgid();
    
    if (profile->run_as_uid != 0 && current_uid != profile->run_as_uid) {
        return -1;
    }
    
    if (profile->run_as_gid != 0 && current_gid != profile->run_as_gid) {
        return -1;
    }
    
    // Check capabilities
    cap_t caps = cap_get_proc();
    if (!caps) return -1;
    
    bool caps_valid = true;
    for (int i = 0; i < 64 && caps_valid; i++) {
        int linux_cap = get_linux_capability(i);
        if (linux_cap >= 0) {
            cap_value_t cap_value = linux_cap;
            cap_flag_value_t effective, permitted;
            
            if (cap_get_flag(caps, cap_value, CAP_EFFECTIVE, &effective) == 0 &&
                cap_get_flag(caps, cap_value, CAP_PERMITTED, &permitted) == 0) {
                
                bool should_have = (profile->allowed_caps & (1ULL << i)) != 0;
                bool actually_has = (effective == CAP_SET) || (permitted == CAP_SET);
                
                if (should_have != actually_has) {
                    caps_valid = false;
                }
            }
        }
    }
    
    cap_free(caps);
    
    return caps_valid ? 0 : -1;
}

#endif // __linux__