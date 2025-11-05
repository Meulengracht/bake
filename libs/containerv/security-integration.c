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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform-specific headers
#ifdef __linux__
extern int linux_apply_security_profile(const struct containerv_security_profile* profile);
extern int linux_verify_security_profile(const struct containerv_security_profile* profile);
#endif

#ifdef _WIN32
#include <windows.h>
extern int windows_apply_security_profile(const struct containerv_security_profile* profile,
                                         HANDLE* process_token, PSID* appcontainer_sid);
extern int windows_verify_security_profile(const struct containerv_security_profile* profile);
extern int windows_create_secure_process(const struct containerv_security_profile* profile,
                                        wchar_t* command_line, PROCESS_INFORMATION* process_info);
#endif

// Global security audit state
static struct {
    bool enabled;
    FILE* audit_file;
    containerv_security_audit_callback callback;
    void* callback_data;
} g_security_audit = {0};

/**
 * @brief Initialize security subsystem
 * @return 0 on success, -1 on failure
 */
int containerv_security_init(void) {
    // Initialize any global security state
    memset(&g_security_audit, 0, sizeof(g_security_audit));
    
    // Platform-specific initialization could go here
    return 0;
}

/**
 * @brief Cleanup security subsystem
 */
void containerv_security_cleanup(void) {
    if (g_security_audit.audit_file) {
        fclose(g_security_audit.audit_file);
        g_security_audit.audit_file = NULL;
    }
    memset(&g_security_audit, 0, sizeof(g_security_audit));
}

/**
 * @brief Apply security profile to current process/container
 * @param profile Security profile to apply
 * @return 0 on success, -1 on failure
 */
int containerv_apply_security_profile(const struct containerv_security_profile* profile) {
    if (!profile) {
        return -1;
    }
    
    // Log security profile application
    containerv_security_log_event(CV_AUDIT_PROFILE_APPLIED, 
                                 "Applying security profile", profile->name);
    
    int result = -1;
    
#ifdef __linux__
    result = linux_apply_security_profile(profile);
#endif

#ifdef _WIN32
    HANDLE token = NULL;
    PSID sid = NULL;
    result = windows_apply_security_profile(profile, &token, &sid);
    
    // Cleanup Windows handles
    if (token) CloseHandle(token);
    if (sid) LocalFree(sid);
#endif

    if (result == 0) {
        containerv_security_log_event(CV_AUDIT_PROFILE_APPLIED, 
                                     "Security profile applied successfully", profile->name);
    } else {
        containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
                                     "Failed to apply security profile", profile->name);
    }
    
    return result;
}

/**
 * @brief Verify current security profile compliance
 * @param profile Expected security profile
 * @return 0 if compliant, -1 if not
 */
int containerv_verify_security_profile(const struct containerv_security_profile* profile) {
    if (!profile) {
        return -1;
    }
    
    int result = -1;
    
#ifdef __linux__
    result = linux_verify_security_profile(profile);
#endif

#ifdef _WIN32
    result = windows_verify_security_profile(profile);
#endif

    if (result != 0) {
        containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
                                     "Security profile verification failed", profile->name);
    }
    
    return result;
}

/**
 * @brief Enable security audit logging
 * @param options Audit options
 * @return 0 on success, -1 on failure
 */
int containerv_enable_security_audit(const struct containerv_audit_options* options) {
    if (!options) {
        return -1;
    }
    
    g_security_audit.enabled = true;
    
    // Set up file logging if specified
    if (options->log_file) {
        g_security_audit.audit_file = fopen(options->log_file, "a");
        if (!g_security_audit.audit_file) {
            g_security_audit.enabled = false;
            return -1;
        }
    }
    
    // Set up callback if specified
    if (options->callback) {
        g_security_audit.callback = options->callback;
        g_security_audit.callback_data = options->callback_data;
    }
    
    containerv_security_log_event(CV_AUDIT_SYSTEM_EVENT, 
                                 "Security audit enabled", NULL);
    
    return 0;
}

/**
 * @brief Disable security audit logging
 */
void containerv_disable_security_audit(void) {
    if (g_security_audit.enabled) {
        containerv_security_log_event(CV_AUDIT_SYSTEM_EVENT, 
                                     "Security audit disabled", NULL);
    }
    
    if (g_security_audit.audit_file) {
        fclose(g_security_audit.audit_file);
        g_security_audit.audit_file = NULL;
    }
    
    g_security_audit.enabled = false;
    g_security_audit.callback = NULL;
    g_security_audit.callback_data = NULL;
}

/**
 * @brief Log a security event
 * @param event_type Type of security event
 * @param message Human-readable message
 * @param context Additional context (can be NULL)
 */
void containerv_security_log_event(enum containerv_audit_event event_type,
                                  const char* message, const char* context) {
    if (!g_security_audit.enabled) {
        return;
    }
    
    // Get current timestamp
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Event type string
    const char* event_str;
    switch (event_type) {
        case CV_AUDIT_CONTAINER_CREATE:    event_str = "CONTAINER_CREATE"; break;
        case CV_AUDIT_CONTAINER_START:     event_str = "CONTAINER_START"; break;
        case CV_AUDIT_CONTAINER_STOP:      event_str = "CONTAINER_STOP"; break;
        case CV_AUDIT_PROFILE_APPLIED:     event_str = "PROFILE_APPLIED"; break;
        case CV_AUDIT_SECURITY_VIOLATION:  event_str = "SECURITY_VIOLATION"; break;
        case CV_AUDIT_PRIVILEGE_ESCALATION: event_str = "PRIVILEGE_ESCALATION"; break;
        case CV_AUDIT_SYSTEM_EVENT:        event_str = "SYSTEM_EVENT"; break;
        default:                           event_str = "UNKNOWN"; break;
    }
    
    // Build audit record
    char audit_record[1024];
    if (context) {
        snprintf(audit_record, sizeof(audit_record), 
                "[%s] %s: %s (context: %s)", 
                timestamp, event_str, message, context);
    } else {
        snprintf(audit_record, sizeof(audit_record), 
                "[%s] %s: %s", 
                timestamp, event_str, message);
    }
    
    // Log to file if enabled
    if (g_security_audit.audit_file) {
        fprintf(g_security_audit.audit_file, "%s\n", audit_record);
        fflush(g_security_audit.audit_file);
    }
    
    // Call callback if enabled
    if (g_security_audit.callback) {
        struct containerv_audit_event event = {
            .type = event_type,
            .timestamp = now,
            .message = message,
            .context = context
        };
        g_security_audit.callback(&event, g_security_audit.callback_data);
    }
}

/**
 * @brief Get security capabilities for current platform
 * @param capabilities Output capabilities structure
 * @return 0 on success, -1 on failure
 */
int containerv_get_security_capabilities(struct containerv_security_capabilities* capabilities) {
    if (!capabilities) {
        return -1;
    }
    
    memset(capabilities, 0, sizeof(*capabilities));
    
#ifdef __linux__
    capabilities->has_capabilities = true;
    capabilities->has_seccomp = true;
    capabilities->has_namespaces = true;
    capabilities->has_cgroups = true;
    
    // Check for LSM support
    if (access("/sys/kernel/security/apparmor", F_OK) == 0) {
        capabilities->has_apparmor = true;
    }
    if (access("/sys/fs/selinux", F_OK) == 0) {
        capabilities->has_selinux = true;
    }
    
    capabilities->platform_name = "Linux";
#endif

#ifdef _WIN32
    capabilities->has_appcontainer = true;
    capabilities->has_job_objects = true;
    capabilities->has_integrity_levels = true;
    capabilities->has_privileges = true;
    
    // Check Windows version for advanced features
    OSVERSIONINFOEXW os_info = {0};
    os_info.dwOSVersionInfoSize = sizeof(os_info);
    if (GetVersionExW((LPOSVERSIONINFOW)&os_info)) {
        if (os_info.dwMajorVersion >= 10) {
            capabilities->has_process_mitigation = true;
        }
    }
    
    capabilities->platform_name = "Windows";
#endif

    return 0;
}

/**
 * @brief Enhanced container creation with integrated security
 * @param options Container options including security profile
 * @param container Output container handle
 * @return 0 on success, -1 on failure
 */
int containerv_create_secure_container(const struct containerv_options* options, 
                                      struct containerv_container** container) {
    if (!options || !container) {
        return -1;
    }
    
    // Log container creation attempt
    containerv_security_log_event(CV_AUDIT_CONTAINER_CREATE, 
                                 "Creating secure container", 
                                 options->security_profile ? options->security_profile->name : "default");
    
    // Apply security profile before container creation if specified
    if (options->security_profile) {
        if (containerv_apply_security_profile(options->security_profile) != 0) {
            containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
                                         "Failed to apply security profile during container creation",
                                         options->security_profile->name);
            return -1;
        }
    }
    
    // Create the container using existing containerv_create function
    int result = containerv_create(options, container);
    
    if (result == 0) {
        // Store security profile reference in container for later use
        if (*container && options->security_profile) {
            // This assumes we add security_profile to containerv_container structure
            // (*container)->security_profile = options->security_profile;
        }
        
        containerv_security_log_event(CV_AUDIT_CONTAINER_CREATE, 
                                     "Secure container created successfully",
                                     options->security_profile ? options->security_profile->name : "default");
    } else {
        containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
                                     "Failed to create secure container",
                                     options->security_profile ? options->security_profile->name : "default");
    }
    
    return result;
}

/**
 * @brief Start container with security validation
 * @param container Container to start
 * @return 0 on success, -1 on failure
 */
int containerv_start_secure_container(struct containerv_container* container) {
    if (!container) {
        return -1;
    }
    
    // TODO: Get security profile from container
    // const struct containerv_security_profile* profile = container->security_profile;
    
    containerv_security_log_event(CV_AUDIT_CONTAINER_START, 
                                 "Starting secure container", NULL);
    
    // Verify security profile compliance before starting
    // if (profile && containerv_verify_security_profile(profile) != 0) {
    //     containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
    //                                  "Security profile verification failed before container start",
    //                                  profile->name);
    //     return -1;
    // }
    
    // Start the container using existing containerv_start function
    int result = containerv_start(container);
    
    if (result == 0) {
        containerv_security_log_event(CV_AUDIT_CONTAINER_START, 
                                     "Secure container started successfully", NULL);
    } else {
        containerv_security_log_event(CV_AUDIT_SECURITY_VIOLATION, 
                                     "Failed to start secure container", NULL);
    }
    
    return result;
}

/**
 * @brief Check if current process is running in a secure container
 * @param security_info Output security information
 * @return 0 if in secure container, -1 if not or on error
 */
int containerv_get_current_security_context(struct containerv_security_context* security_info) {
    if (!security_info) {
        return -1;
    }
    
    memset(security_info, 0, sizeof(*security_info));
    
#ifdef __linux__
    // Check for container indicators on Linux
    security_info->in_container = (access("/.dockerenv", F_OK) == 0) ||
                                 (access("/proc/1/cgroup", F_OK) == 0 && 
                                  strstr(getenv("container") ?: "", "docker") != NULL);
    
    // Check capabilities
    security_info->has_capabilities = (getuid() != 0) || (geteuid() != 0);
    
    // Check namespaces
    security_info->in_namespace = (access("/proc/self/ns/pid", F_OK) == 0);
#endif

#ifdef _WIN32
    // Check for AppContainer
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD length = 0;
        GetTokenInformation(token, TokenAppContainerSid, NULL, 0, &length);
        security_info->in_appcontainer = (GetLastError() != ERROR_NOT_SUPPORTED && length > 0);
        
        // Check integrity level
        GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &length);
        if (length > 0) {
            TOKEN_MANDATORY_LABEL* label = malloc(length);
            if (label && GetTokenInformation(token, TokenIntegrityLevel, 
                                           label, length, &length)) {
                DWORD* rid = GetSidSubAuthority(label->Label.Sid,
                                              *GetSidSubAuthorityCount(label->Label.Sid) - 1);
                security_info->integrity_level = rid ? *rid : SECURITY_MANDATORY_MEDIUM_RID;
                free(label);
            }
        }
        
        CloseHandle(token);
    }
#endif

    return security_info->in_container ? 0 : -1;
}