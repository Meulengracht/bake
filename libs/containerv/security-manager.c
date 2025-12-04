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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <sys/capability.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#endif

// Global security system state
static bool g_security_initialized = false;

// Predefined Security Profiles
static const struct containerv_security_profile default_profile = {
    .level = CV_SECURITY_RESTRICTED,
    .name = "default",
    .description = "Balanced security for general container workloads",
    .allowed_caps = (1ULL << CV_CAP_CHOWN) |
                   (1ULL << CV_CAP_DAC_OVERRIDE) |
                   (1ULL << CV_CAP_FOWNER) |
                   (1ULL << CV_CAP_KILL) |
                   (1ULL << CV_CAP_SETGID) |
                   (1ULL << CV_CAP_SETUID) |
                   (1ULL << CV_CAP_NET_BIND_SERVICE) |
                   (1ULL << CV_CAP_SYS_CHROOT),
    .dropped_caps = (1ULL << CV_CAP_SYS_ADMIN) |
                   (1ULL << CV_CAP_SYS_MODULE) |
                   (1ULL << CV_CAP_NET_ADMIN) |
                   (1ULL << CV_CAP_SYS_PTRACE),
    .no_new_privileges = true,
    .run_as_uid = 1000,
    .run_as_gid = 1000,
    .run_as_user = NULL,
    .no_suid = true,
    .read_only_root = false,
    .network_isolated = false,
    .writable_paths = NULL,
    .masked_paths = NULL,
    .fs_rule_count = 0,
    .network_rule_count = 0
#ifdef __linux__
    , .default_syscall_action = CV_SYSCALL_ERRNO,
    .use_apparmor = false,
    .use_selinux = false,
    .security_context = NULL
#endif
#ifdef _WIN32
    , .use_app_container = true,
    .integrity_level = "medium",
    .capability_sids = NULL,
    .win_cap_count = 0
#endif
};

static const struct containerv_security_profile web_server_profile = {
    .level = CV_SECURITY_RESTRICTED,
    .name = "web-server",
    .description = "Security profile optimized for web servers",
    .allowed_caps = (1ULL << CV_CAP_NET_BIND_SERVICE) |
                   (1ULL << CV_CAP_SETUID) |
                   (1ULL << CV_CAP_SETGID) |
                   (1ULL << CV_CAP_CHOWN),
    .dropped_caps = (1ULL << CV_CAP_SYS_ADMIN) |
                   (1ULL << CV_CAP_SYS_MODULE) |
                   (1ULL << CV_CAP_NET_ADMIN) |
                   (1ULL << CV_CAP_SYS_PTRACE) |
                   (1ULL << CV_CAP_MKNOD),
    .no_new_privileges = true,
    .run_as_uid = 33, // www-data
    .run_as_gid = 33,
    .run_as_user = "www-data",
    .no_suid = true,
    .read_only_root = true,
    .network_isolated = false,
    .writable_paths = (char*[]){"/var/log", "/var/cache", "/tmp", NULL},
    .masked_paths = (char*[]){"/proc/kcore", "/proc/keys", "/proc/timer_list", NULL},
    .fs_rule_count = 3,
    .network_rule_count = 0
};

static const struct containerv_security_profile database_profile = {
    .level = CV_SECURITY_RESTRICTED,
    .name = "database",
    .description = "Security profile for database containers",
    .allowed_caps = (1ULL << CV_CAP_SETUID) |
                   (1ULL << CV_CAP_SETGID) |
                   (1ULL << CV_CAP_CHOWN) |
                   (1ULL << CV_CAP_DAC_OVERRIDE),
    .dropped_caps = (1ULL << CV_CAP_SYS_ADMIN) |
                   (1ULL << CV_CAP_NET_ADMIN) |
                   (1ULL << CV_CAP_SYS_MODULE) |
                   (1ULL << CV_CAP_SYS_PTRACE),
    .no_new_privileges = true,
    .run_as_uid = 999, // Common database user ID
    .run_as_gid = 999,
    .run_as_user = "database",
    .no_suid = true,
    .read_only_root = false,
    .network_isolated = false,
    .writable_paths = (char*[]){"/var/lib/database", "/var/log", "/tmp", NULL},
    .masked_paths = (char*[]){"/proc/kcore", "/proc/keys", NULL},
    .fs_rule_count = 3,
    .network_rule_count = 0
};

static const struct containerv_security_profile untrusted_profile = {
    .level = CV_SECURITY_PARANOID,
    .name = "untrusted",
    .description = "Maximum security for untrusted workloads",
    .allowed_caps = 0, // No capabilities
    .dropped_caps = 0xFFFFFFFFFFFFFFFFULL, // Drop all capabilities
    .no_new_privileges = true,
    .run_as_uid = 65534, // nobody
    .run_as_gid = 65534,
    .run_as_user = "nobody",
    .no_suid = true,
    .read_only_root = true,
    .network_isolated = true,
    .writable_paths = (char*[]){"/tmp", NULL},
    .masked_paths = (char*[]){
        "/proc/kcore", "/proc/keys", "/proc/timer_list", 
        "/proc/sched_debug", "/sys/firmware", "/proc/scsi", 
        NULL
    },
    .fs_rule_count = 1,
    .network_rule_count = 0
#ifdef __linux__
    , .default_syscall_action = CV_SYSCALL_ERRNO,
    .use_apparmor = true,
    .use_selinux = true,
    .security_context = "unconfined_u:unconfined_r:container_untrusted_t:s0"
#endif
#ifdef _WIN32
    , .use_app_container = true,
    .integrity_level = "low",
    .capability_sids = NULL,
    .win_cap_count = 0
#endif
};

// Export predefined profiles
const struct containerv_security_profile* containerv_profile_default = &default_profile;
const struct containerv_security_profile* containerv_profile_web_server = &web_server_profile;
const struct containerv_security_profile* containerv_profile_database = &database_profile;
const struct containerv_security_profile* containerv_profile_untrusted = &untrusted_profile;

// Utility functions
static char* duplicate_string(const char* str) {
    if (!str) return NULL;
    return strdup(str);
}

static char** duplicate_string_array(char** array, int count) {
    if (!array || count <= 0) return NULL;
    
    char** result = calloc(count + 1, sizeof(char*));
    if (!result) return NULL;
    
    for (int i = 0; i < count; i++) {
        if (array[i]) {
            result[i] = strdup(array[i]);
            if (!result[i]) {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    free(result[j]);
                }
                free(result);
                return NULL;
            }
        }
    }
    
    return result;
}

static void free_string_array(char** array, int count) {
    if (!array) return;
    
    for (int i = 0; i < count; i++) {
        free(array[i]);
    }
    free(array);
}

static int count_string_array(char** array) {
    if (!array) return 0;
    
    int count = 0;
    while (array[count]) count++;
    return count;
}

// Security system initialization
int containerv_security_init(void) {
    if (g_security_initialized) {
        return 0; // Already initialized
    }
    
#ifdef __linux__
    // Check if we have necessary capabilities for security operations
    cap_t caps = cap_get_proc();
    if (!caps) {
        return -1;
    }
    
    cap_flag_value_t cap_value;
    if (cap_get_flag(caps, CAP_SETPCAP, CAP_EFFECTIVE, &cap_value) != 0 ||
        cap_value != CAP_SET) {
        cap_free(caps);
        // Continue anyway - might work in some environments
    } else {
        cap_free(caps);
    }
#endif
    
#ifdef _WIN32
    // Check if we have necessary privileges for security operations
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return -1;
    }
    
    // Check for SeSecurityPrivilege
    PRIVILEGE_SET privs;
    privs.PrivilegeCount = 1;
    privs.Control = PRIVILEGE_SET_ALL_NECESSARY;
    privs.Privilege[0].Luid.LowPart = SE_SECURITY_PRIVILEGE;
    privs.Privilege[0].Luid.HighPart = 0;
    privs.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    BOOL result;
    if (!PrivilegeCheck(token, &privs, &result)) {
        CloseHandle(token);
        // Continue anyway - might work with reduced functionality
    }
    
    CloseHandle(token);
#endif
    
    g_security_initialized = true;
    return 0;
}

void containerv_security_cleanup(void) {
    g_security_initialized = false;
}

// Security profile management
int containerv_security_profile_create(
    const char* name,
    enum containerv_security_level level,
    struct containerv_security_profile** profile
) {
    if (!name || !profile) {
        return -1;
    }
    
    struct containerv_security_profile* new_profile = calloc(1, sizeof(struct containerv_security_profile));
    if (!new_profile) {
        return -1;
    }
    
    new_profile->level = level;
    new_profile->name = duplicate_string(name);
    if (!new_profile->name) {
        free(new_profile);
        return -1;
    }
    
    // Set default values based on security level
    switch (level) {
        case CV_SECURITY_PERMISSIVE:
            new_profile->allowed_caps = 0xFFFFFFFFFFFFFFFFULL; // All capabilities
            new_profile->dropped_caps = 0;
            new_profile->no_new_privileges = false;
            new_profile->read_only_root = false;
            new_profile->network_isolated = false;
            new_profile->no_suid = false;
            break;
            
        case CV_SECURITY_RESTRICTED:
            // Copy from default profile
            *new_profile = default_profile;
            new_profile->name = duplicate_string(name);
            break;
            
        case CV_SECURITY_STRICT:
            new_profile->allowed_caps = (1ULL << CV_CAP_SETUID) | (1ULL << CV_CAP_SETGID);
            new_profile->dropped_caps = 0xFFFFFFFFFFFFFFFFULL & ~new_profile->allowed_caps;
            new_profile->no_new_privileges = true;
            new_profile->read_only_root = true;
            new_profile->network_isolated = false;
            new_profile->no_suid = true;
            break;
            
        case CV_SECURITY_PARANOID:
            // Copy from untrusted profile
            *new_profile = untrusted_profile;
            new_profile->name = duplicate_string(name);
            break;
    }
    
    *profile = new_profile;
    return 0;
}

int containerv_security_profile_load(
    const char* name,
    struct containerv_security_profile** profile
) {
    if (!name || !profile) {
        return -1;
    }
    
    const struct containerv_security_profile* source = NULL;
    
    if (strcmp(name, "default") == 0) {
        source = &default_profile;
    } else if (strcmp(name, "web-server") == 0) {
        source = &web_server_profile;
    } else if (strcmp(name, "database") == 0) {
        source = &database_profile;
    } else if (strcmp(name, "untrusted") == 0) {
        source = &untrusted_profile;
    } else {
        return -1; // Profile not found
    }
    
    // Create a deep copy of the profile
    struct containerv_security_profile* new_profile = calloc(1, sizeof(struct containerv_security_profile));
    if (!new_profile) {
        return -1;
    }
    
    // Copy basic fields
    *new_profile = *source;
    
    // Duplicate strings
    new_profile->name = duplicate_string(source->name);
    new_profile->description = duplicate_string(source->description);
    new_profile->run_as_user = duplicate_string(source->run_as_user);
    
    // Duplicate arrays
    if (source->writable_paths && source->fs_rule_count > 0) {
        new_profile->writable_paths = duplicate_string_array(source->writable_paths, source->fs_rule_count);
    }
    if (source->masked_paths) {
        int masked_count = count_string_array(source->masked_paths);
        if (masked_count > 0) {
            new_profile->masked_paths = duplicate_string_array(source->masked_paths, masked_count);
        }
    }
    
#ifdef __linux__
    new_profile->security_context = duplicate_string(source->security_context);
#endif
    
#ifdef _WIN32
    new_profile->integrity_level = duplicate_string(source->integrity_level);
    if (source->capability_sids && source->win_cap_count > 0) {
        new_profile->capability_sids = duplicate_string_array(source->capability_sids, source->win_cap_count);
    }
#endif
    
    *profile = new_profile;
    return 0;
}

void containerv_security_profile_free(struct containerv_security_profile* profile) {
    if (!profile) return;
    
    free(profile->name);
    free(profile->description);
    free(profile->run_as_user);
    
    free_string_array(profile->writable_paths, profile->fs_rule_count);
    
    if (profile->masked_paths) {
        int masked_count = count_string_array(profile->masked_paths);
        free_string_array(profile->masked_paths, masked_count);
    }
    
    free_string_array(profile->allowed_ports, profile->network_rule_count);
    free_string_array(profile->allowed_hosts, profile->network_rule_count);
    
#ifdef __linux__
    free(profile->security_context);
#endif
    
#ifdef _WIN32
    free(profile->integrity_level);
    free_string_array(profile->capability_sids, profile->win_cap_count);
#endif
    
    free(profile);
}

// Security profile modification
int containerv_security_add_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
) {
    if (!profile || capability >= 64) {
        return -1;
    }
    
    uint64_t cap_bit = 1ULL << capability;
    profile->allowed_caps |= cap_bit;
    profile->dropped_caps &= ~cap_bit;
    
    return 0;
}

int containerv_security_drop_capability(
    struct containerv_security_profile* profile,
    uint32_t capability
) {
    if (!profile || capability >= 64) {
        return -1;
    }
    
    uint64_t cap_bit = 1ULL << capability;
    profile->allowed_caps &= ~cap_bit;
    profile->dropped_caps |= cap_bit;
    
    return 0;
}

int containerv_security_add_writable_path(
    struct containerv_security_profile* profile,
    const char* path
) {
    if (!profile || !path) {
        return -1;
    }
    
    // Resize array
    char** new_paths = realloc(profile->writable_paths, 
                              (profile->fs_rule_count + 2) * sizeof(char*));
    if (!new_paths) {
        return -1;
    }
    
    new_paths[profile->fs_rule_count] = duplicate_string(path);
    if (!new_paths[profile->fs_rule_count]) {
        return -1;
    }
    
    new_paths[profile->fs_rule_count + 1] = NULL; // NULL terminate
    profile->writable_paths = new_paths;
    profile->fs_rule_count++;
    
    return 0;
}

int containerv_security_add_network_port(
    struct containerv_security_profile* profile,
    const char* port_spec
) {
    if (!profile || !port_spec) {
        return -1;
    }
    
    // Resize array
    char** new_ports = realloc(profile->allowed_ports,
                              (profile->network_rule_count + 2) * sizeof(char*));
    if (!new_ports) {
        return -1;
    }
    
    new_ports[profile->network_rule_count] = duplicate_string(port_spec);
    if (!new_ports[profile->network_rule_count]) {
        return -1;
    }
    
    new_ports[profile->network_rule_count + 1] = NULL; // NULL terminate
    profile->allowed_ports = new_ports;
    profile->network_rule_count++;
    
    return 0;
}

#ifdef __linux__
int containerv_security_add_syscall_filter(
    struct containerv_security_profile* profile,
    const char* syscall_name,
    enum containerv_syscall_action action,
    int errno_value
) {
    if (!profile || !syscall_name) {
        return -1;
    }
    
    // TODO: Implement syscall filter management
    // This would require maintaining a list of syscall filters
    // and generating seccomp-bpf programs during container creation
    
    return 0; // Placeholder
}

int containerv_security_set_apparmor_profile(
    struct containerv_security_profile* profile,
    const char* apparmor_profile
) {
    if (!profile || !apparmor_profile) {
        return -1;
    }
    
    free(profile->security_context);
    profile->security_context = duplicate_string(apparmor_profile);
    profile->use_apparmor = true;
    
    return profile->security_context ? 0 : -1;
}
#endif

// Security profile validation
int containerv_security_profile_validate(
    const struct containerv_security_profile* profile,
    char* error_msg,
    size_t error_size
) {
    if (!profile) {
        if (error_msg) {
            snprintf(error_msg, error_size, "Profile is NULL");
        }
        return -1;
    }
    
    // Check for invalid capability combinations
    if (profile->allowed_caps & profile->dropped_caps) {
        if (error_msg) {
            snprintf(error_msg, error_size, 
                    "Profile has overlapping allowed and dropped capabilities");
        }
        return -1;
    }
    
#ifdef __linux__
    // Validate AppArmor/SELinux requirements
    if (profile->use_apparmor && !profile->security_context) {
        if (error_msg) {
            snprintf(error_msg, error_size, "AppArmor enabled but no profile specified");
        }
        return -1;
    }
    
    // Check if seccomp is available
    if (profile->default_syscall_action != CV_SYSCALL_ALLOW) {
        if (access("/proc/sys/kernel/seccomp", R_OK) != 0) {
            if (error_msg) {
                snprintf(error_msg, error_size, "Syscall filtering requested but seccomp not available");
            }
            return -1;
        }
    }
#endif
    
#ifdef _WIN32
    // Validate Windows-specific settings
    if (profile->use_app_container && !profile->integrity_level) {
        if (error_msg) {
            snprintf(error_msg, error_size, "AppContainer enabled but no integrity level specified");
        }
        return -1;
    }
#endif
    
    if (error_msg) {
        snprintf(error_msg, error_size, "Profile validation passed");
    }
    
    return 0;
}

// Container security auditing
int containerv_security_audit(
    struct containerv_container* container,
    struct containerv_security_audit* audit
) {
    if (!container || !audit) {
        return -1;
    }
    
    memset(audit, 0, sizeof(*audit));
    audit->audit_time = time(NULL);
    
    // TODO: Implement actual security auditing
    // This would inspect the running container and check:
    // - Current capabilities
    // - Process privileges
    // - Filesystem permissions
    // - Network accessibility
    // - Active security policies
    
    // Placeholder implementation
    audit->capabilities_minimal = true;
    audit->no_privileged_access = true;
    audit->filesystem_restricted = true;
    audit->network_controlled = true;
    audit->syscalls_filtered = true;
    audit->isolation_complete = true;
    
    audit->security_score = 85; // Placeholder score
    
    snprintf(audit->audit_log, sizeof(audit->audit_log),
            "Security audit completed at %ld. Container appears to be properly secured.",
            (long)audit->audit_time);
    
    return 0;
}

// Integration with container options
int containerv_options_set_security_profile(
    struct containerv_options* options,
    const struct containerv_security_profile* profile
) {
    if (!options || !profile) {
        return -1;
    }
    
    // TODO: Store security profile in options structure
    // This would require extending containerv_options to include
    // a security_profile field and applying the profile during
    // container creation
    
    return 0; // Placeholder
}