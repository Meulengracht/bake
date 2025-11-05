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

#include <chef/containerv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Example security audit callback
static void security_audit_callback(const struct containerv_audit_event* event, void* user_data) {
    const char* severity = (const char*)user_data;
    
    printf("[%s] Security Event: %s - %s", severity, 
           event->message, event->context ? event->context : "");
    
    if (event->type == CV_AUDIT_SECURITY_VIOLATION ||
        event->type == CV_AUDIT_PRIVILEGE_ESCALATION) {
        printf(" *** SECURITY ALERT ***");
    }
    printf("\n");
}

// Example: Create and run a web server container with restricted security
int example_web_server_container(void) {
    printf("=== Web Server Container Example ===\n");
    
    // Get a predefined web server security profile
    struct containerv_security_profile* profile = containerv_get_predefined_profile("web-server");
    if (!profile) {
        printf("Error: Failed to get web-server profile\n");
        return -1;
    }
    
    // Customize the profile for our needs
    profile->level = CV_SECURITY_RESTRICTED;
    
    // Enable security auditing
    struct containerv_audit_options audit_options = {
        .log_file = "/tmp/chef-security.log",
        .callback = security_audit_callback,
        .callback_data = (void*)"WEB-SERVER"
    };
    
    if (containerv_enable_security_audit(&audit_options) != 0) {
        printf("Warning: Could not enable security audit\n");
    }
    
    // Set up container options with security
    struct containerv_options options = {
        .image_name = "nginx:alpine",
        .security_profile = profile,
        .mount_tmpfs_on_tmp = true,
        .read_only_rootfs = true
    };
    
    // Create the secure container
    struct containerv_container* container = NULL;
    if (containerv_create_secure_container(&options, &container) != 0) {
        printf("Error: Failed to create secure web server container\n");
        containerv_free_security_profile(profile);
        return -1;
    }
    
    printf("Web server container created with restricted security profile\n");
    
    // Start the container
    if (containerv_start_secure_container(container) != 0) {
        printf("Error: Failed to start secure container\n");
        containerv_destroy(container);
        containerv_free_security_profile(profile);
        return -1;
    }
    
    printf("Web server container started successfully\n");
    printf("Container is running with:\n");
    
#ifdef __linux__
    printf("  - Dropped capabilities (no CAP_SYS_ADMIN, CAP_NET_ADMIN, etc.)\n");
    printf("  - Seccomp syscall filtering active\n");
    printf("  - Read-only root filesystem\n");
    if (access("/sys/kernel/security/apparmor", F_OK) == 0) {
        printf("  - AppArmor profile enforced\n");
    }
#endif

#ifdef _WIN32
    printf("  - Running in AppContainer\n");
    printf("  - Low integrity level\n");
    printf("  - Restricted token with limited privileges\n");
    printf("  - Job Object security restrictions\n");
#endif

    // Let it run for a bit
    printf("Container running... (sleeping 5 seconds)\n");
    sleep(5);
    
    // Stop the container
    containerv_stop(container);
    containerv_destroy(container);
    containerv_free_security_profile(profile);
    
    printf("Web server container stopped and cleaned up\n\n");
    return 0;
}

// Example: Create a highly restricted untrusted container
int example_untrusted_container(void) {
    printf("=== Untrusted Code Container Example ===\n");
    
    // Get the untrusted profile (most restrictive)
    struct containerv_security_profile* profile = containerv_get_predefined_profile("untrusted");
    if (!profile) {
        printf("Error: Failed to get untrusted profile\n");
        return -1;
    }
    
    // Make it even more restrictive
    profile->level = CV_SECURITY_PARANOID;
    
#ifdef __linux__
    // Add more capability restrictions
    profile->drop_capabilities[profile->drop_cap_count++] = CV_CAP_SETUID;
    profile->drop_capabilities[profile->drop_cap_count++] = CV_CAP_SETGID;
    
    // Custom seccomp filter (more restrictive)
    const char* blocked_syscalls[] = {
        "execve", "execveat", "ptrace", "process_vm_readv", 
        "process_vm_writev", "mount", "umount2", "pivot_root"
    };
    profile->seccomp_syscalls = blocked_syscalls;
    profile->seccomp_syscall_count = sizeof(blocked_syscalls) / sizeof(blocked_syscalls[0]);
#endif

#ifdef _WIN32
    // Use low integrity and AppContainer
    profile->integrity_level = "low";
    profile->use_app_container = true;
#endif
    
    struct containerv_options options = {
        .image_name = "alpine:latest",
        .security_profile = profile,
        .network_mode = "none",  // No network access
        .mount_tmpfs_on_tmp = true,
        .read_only_rootfs = true,
        .memory_limit = 128 * 1024 * 1024,  // 128MB limit
        .cpu_quota = 50000  // 50% CPU
    };
    
    struct containerv_container* container = NULL;
    if (containerv_create_secure_container(&options, &container) != 0) {
        printf("Error: Failed to create untrusted container\n");
        containerv_free_security_profile(profile);
        return -1;
    }
    
    printf("Untrusted container created with paranoid security\n");
    
    // Verify security before starting
    if (containerv_verify_security_profile(profile) != 0) {
        printf("Warning: Security profile verification failed\n");
    }
    
    if (containerv_start_secure_container(container) != 0) {
        printf("Error: Failed to start untrusted container\n");
        containerv_destroy(container);
        containerv_free_security_profile(profile);
        return -1;
    }
    
    printf("Untrusted container started with maximum security restrictions:\n");
    printf("  - No network access\n");
    printf("  - Read-only filesystem\n");
    printf("  - Memory limited to 128MB\n");
    printf("  - CPU limited to 50%%\n");
    
#ifdef __linux__
    printf("  - All dangerous capabilities dropped\n");
    printf("  - Strict seccomp filtering (blocks exec*, mount, ptrace, etc.)\n");
    printf("  - Isolated namespaces\n");
#endif

#ifdef _WIN32
    printf("  - Low integrity level\n");
    printf("  - AppContainer isolation\n");
    printf("  - Restricted token\n");
#endif

    sleep(3);
    
    containerv_stop(container);
    containerv_destroy(container);
    containerv_free_security_profile(profile);
    
    printf("Untrusted container stopped\n\n");
    return 0;
}

// Example: Check current security context
int example_security_context_check(void) {
    printf("=== Current Security Context Check ===\n");
    
    struct containerv_security_context context;
    if (containerv_get_current_security_context(&context) == 0) {
        printf("Running in secure container environment:\n");
        
        if (context.in_container) {
            printf("  - Container environment detected\n");
        }
        
#ifdef __linux__
        if (context.has_capabilities) {
            printf("  - Capabilities restrictions active\n");
        }
        if (context.in_namespace) {
            printf("  - Running in isolated namespaces\n");
        }
#endif

#ifdef _WIN32
        if (context.in_appcontainer) {
            printf("  - Running in AppContainer\n");
        }
        printf("  - Integrity level: %u\n", context.integrity_level);
#endif
    } else {
        printf("Not running in a secure container\n");
    }
    
    // Check platform security capabilities
    struct containerv_security_capabilities caps;
    if (containerv_get_security_capabilities(&caps) == 0) {
        printf("\nPlatform Security Capabilities (%s):\n", caps.platform_name);
        
#ifdef __linux__
        printf("  - Linux Capabilities: %s\n", caps.has_capabilities ? "Yes" : "No");
        printf("  - Seccomp-BPF: %s\n", caps.has_seccomp ? "Yes" : "No");
        printf("  - Namespaces: %s\n", caps.has_namespaces ? "Yes" : "No");
        printf("  - Cgroups: %s\n", caps.has_cgroups ? "Yes" : "No");
        printf("  - AppArmor: %s\n", caps.has_apparmor ? "Yes" : "No");
        printf("  - SELinux: %s\n", caps.has_selinux ? "Yes" : "No");
#endif

#ifdef _WIN32
        printf("  - AppContainer: %s\n", caps.has_appcontainer ? "Yes" : "No");
        printf("  - Job Objects: %s\n", caps.has_job_objects ? "Yes" : "No");
        printf("  - Integrity Levels: %s\n", caps.has_integrity_levels ? "Yes" : "No");
        printf("  - Privileges: %s\n", caps.has_privileges ? "Yes" : "No");
        printf("  - Process Mitigation: %s\n", caps.has_process_mitigation ? "Yes" : "No");
#endif
    }
    
    printf("\n");
    return 0;
}

// Example: List available security profiles
int example_list_security_profiles(void) {
    printf("=== Available Security Profiles ===\n");
    
    const char* profile_names[] = {
        "default", "web-server", "database", "untrusted"
    };
    
    for (size_t i = 0; i < sizeof(profile_names) / sizeof(profile_names[0]); i++) {
        struct containerv_security_profile* profile = containerv_get_predefined_profile(profile_names[i]);
        if (profile) {
            printf("\nProfile: %s\n", profile->name);
            printf("  Description: %s\n", profile->description ? profile->description : "N/A");
            printf("  Security Level: ");
            
            switch (profile->level) {
                case CV_SECURITY_PERMISSIVE: printf("Permissive\n"); break;
                case CV_SECURITY_RESTRICTED: printf("Restricted\n"); break;
                case CV_SECURITY_STRICT: printf("Strict\n"); break;
                case CV_SECURITY_PARANOID: printf("Paranoid\n"); break;
                default: printf("Unknown\n"); break;
            }
            
#ifdef __linux__
            printf("  Capabilities to drop: %d\n", profile->drop_cap_count);
            printf("  Seccomp syscalls: %d\n", profile->seccomp_syscall_count);
            if (profile->apparmor_profile) {
                printf("  AppArmor profile: %s\n", profile->apparmor_profile);
            }
#endif

#ifdef _WIN32
            if (profile->use_app_container) {
                printf("  Uses AppContainer: Yes\n");
            }
            if (profile->integrity_level) {
                printf("  Integrity Level: %s\n", profile->integrity_level);
            }
#endif
            
            containerv_free_security_profile(profile);
        }
    }
    
    printf("\n");
    return 0;
}

int main(int argc, char* argv[]) {
    printf("Chef Container Security and Sandboxing Demo\n");
    printf("==========================================\n\n");
    
    // Initialize security subsystem
    if (containerv_security_init() != 0) {
        printf("Error: Failed to initialize security subsystem\n");
        return 1;
    }
    
    // Run examples
    example_security_context_check();
    example_list_security_profiles();
    
    if (argc > 1 && strcmp(argv[1], "--run-containers") == 0) {
        printf("Running container examples (requires root/admin privileges)...\n\n");
        example_web_server_container();
        example_untrusted_container();
    } else {
        printf("Use --run-containers to run container examples (requires privileges)\n\n");
    }
    
    // Cleanup
    containerv_disable_security_audit();
    containerv_security_cleanup();
    
    printf("Security demo completed successfully!\n");
    return 0;
}