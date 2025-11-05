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

#ifdef _WIN32

#include <chef/containerv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <sddl.h>
#include <userenv.h>
#include <winnt.h>
#include <aclapi.h>

// Windows-specific security implementation

// Windows privilege name mapping
static const struct {
    enum containerv_windows_privilege cv_priv;
    const wchar_t* name;
    const char* description;
} privilege_map[] = {
    {CV_PRIV_DEBUG, SE_DEBUG_NAME, "Debug programs"},
    {CV_PRIV_BACKUP, SE_BACKUP_NAME, "Back up files and directories"},
    {CV_PRIV_RESTORE, SE_RESTORE_NAME, "Restore files and directories"},
    {CV_PRIV_SHUTDOWN, SE_SHUTDOWN_NAME, "Shut down the system"},
    {CV_PRIV_LOAD_DRIVER, SE_LOAD_DRIVER_NAME, "Load and unload device drivers"},
    {CV_PRIV_SYSTEM_TIME, SE_SYSTEMTIME_NAME, "Change the system time"},
    {CV_PRIV_TAKE_OWNERSHIP, SE_TAKE_OWNERSHIP_NAME, "Take ownership of files or other objects"},
    {CV_PRIV_TCB, SE_TCB_NAME, "Act as part of the operating system"},
    {CV_PRIV_SECURITY, SE_SECURITY_NAME, "Manage auditing and security log"},
    {CV_PRIV_INCREASE_QUOTA, SE_INCREASE_QUOTA_NAME, "Adjust memory quotas for a process"}
};

static const size_t privilege_map_size = sizeof(privilege_map) / sizeof(privilege_map[0]);

static const wchar_t* get_privilege_name(enum containerv_windows_privilege priv) {
    for (size_t i = 0; i < privilege_map_size; i++) {
        if (privilege_map[i].cv_priv == priv) {
            return privilege_map[i].name;
        }
    }
    return NULL;
}

/**
 * @brief Create an AppContainer for container isolation
 * @param profile Security profile
 * @param appcontainer_sid Output pointer for AppContainer SID
 * @return 0 on success, -1 on failure
 */
int windows_create_appcontainer(const struct containerv_security_profile* profile,
                               PSID* appcontainer_sid) {
    if (!profile || !appcontainer_sid) {
        return -1;
    }
    
    if (!profile->use_app_container) {
        *appcontainer_sid = NULL;
        return 0;
    }
    
    // Convert profile name to wide string
    wchar_t app_container_name[256];
    if (MultiByteToWideChar(CP_UTF8, 0, profile->name, -1, 
                           app_container_name, sizeof(app_container_name) / sizeof(wchar_t)) == 0) {
        return -1;
    }
    
    wchar_t display_name[512];
    if (profile->description) {
        MultiByteToWideChar(CP_UTF8, 0, profile->description, -1,
                           display_name, sizeof(display_name) / sizeof(wchar_t));
    } else {
        wcscpy_s(display_name, sizeof(display_name) / sizeof(wchar_t), L"Chef Container");
    }
    
    // Create capability array if specified
    PSID_AND_ATTRIBUTES capabilities = NULL;
    DWORD capability_count = 0;
    
    if (profile->capability_sids && profile->win_cap_count > 0) {
        capabilities = calloc(profile->win_cap_count, sizeof(SID_AND_ATTRIBUTES));
        if (!capabilities) {
            return -1;
        }
        
        capability_count = profile->win_cap_count;
        for (int i = 0; i < profile->win_cap_count; i++) {
            if (ConvertStringSidToSidA(profile->capability_sids[i], &capabilities[i].Sid)) {
                capabilities[i].Attributes = SE_GROUP_ENABLED;
            } else {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    LocalFree(capabilities[j].Sid);
                }
                free(capabilities);
                return -1;
            }
        }
    }
    
    // Create the AppContainer
    HRESULT hr = CreateAppContainerProfile(app_container_name, display_name, display_name,
                                          capabilities, capability_count, appcontainer_sid);
    
    // Cleanup capabilities
    if (capabilities) {
        for (DWORD i = 0; i < capability_count; i++) {
            LocalFree(capabilities[i].Sid);
        }
        free(capabilities);
    }
    
    if (SUCCEEDED(hr)) {
        return 0;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        // AppContainer already exists, derive SID
        return DeriveAppContainerSidFromAppContainerName(app_container_name, appcontainer_sid) == S_OK ? 0 : -1;
    } else {
        return -1;
    }
}

/**
 * @brief Create a restricted token with limited privileges
 * @param profile Security profile
 * @param restricted_token Output handle to restricted token
 * @return 0 on success, -1 on failure
 */
int windows_create_restricted_token(const struct containerv_security_profile* profile,
                                   HANDLE* restricted_token) {
    if (!profile || !restricted_token) {
        return -1;
    }
    
    HANDLE current_token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &current_token)) {
        return -1;
    }
    
    // For high security levels, create a very restricted token
    if (profile->level >= CV_SECURITY_STRICT) {
        // Remove most privileges
        TOKEN_PRIVILEGES token_privs;
        token_privs.PrivilegeCount = 0;
        
        // Restrict SIDs - remove admin groups
        SID_AND_ATTRIBUTES restricted_sids[2];
        DWORD restricted_sid_count = 0;
        
        // Add Administrators group to restricted SIDs (effectively removes admin access)
        PSID admin_sid;
        SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
        if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                    DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_sid)) {
            restricted_sids[restricted_sid_count].Sid = admin_sid;
            restricted_sids[restricted_sid_count].Attributes = 0;
            restricted_sid_count++;
        }
        
        // Create restricted token
        if (!CreateRestrictedToken(current_token, DISABLE_MAX_PRIVILEGE,
                                  restricted_sid_count, restricted_sids,
                                  0, NULL, // No disabled privileges (already disabled by DISABLE_MAX_PRIVILEGE)
                                  0, NULL, // No restricted SIDs for now
                                  restricted_token)) {
            CloseHandle(current_token);
            if (admin_sid) FreeSid(admin_sid);
            return -1;
        }
        
        if (admin_sid) FreeSid(admin_sid);
    } else {
        // For lower security levels, just duplicate the token
        if (!DuplicateToken(current_token, SecurityImpersonation, restricted_token)) {
            CloseHandle(current_token);
            return -1;
        }
    }
    
    CloseHandle(current_token);
    return 0;
}

/**
 * @brief Set integrity level for a token
 * @param token Token handle
 * @param integrity_level Integrity level string ("low", "medium", "high", "system")
 * @return 0 on success, -1 on failure
 */
int windows_set_integrity_level(HANDLE token, const char* integrity_level) {
    if (!token || !integrity_level) {
        return -1;
    }
    
    DWORD integrity_rid;
    if (strcmp(integrity_level, "low") == 0) {
        integrity_rid = SECURITY_MANDATORY_LOW_RID;
    } else if (strcmp(integrity_level, "medium") == 0) {
        integrity_rid = SECURITY_MANDATORY_MEDIUM_RID;
    } else if (strcmp(integrity_level, "high") == 0) {
        integrity_rid = SECURITY_MANDATORY_HIGH_RID;
    } else if (strcmp(integrity_level, "system") == 0) {
        integrity_rid = SECURITY_MANDATORY_SYSTEM_RID;
    } else {
        return -1; // Unknown integrity level
    }
    
    // Create integrity SID
    PSID integrity_sid;
    SID_IDENTIFIER_AUTHORITY mandatory_label_authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (!AllocateAndInitializeSid(&mandatory_label_authority, 1, integrity_rid,
                                 0, 0, 0, 0, 0, 0, 0, &integrity_sid)) {
        return -1;
    }
    
    // Set up token mandatory label
    TOKEN_MANDATORY_LABEL token_label;
    token_label.Label.Attributes = SE_GROUP_INTEGRITY;
    token_label.Label.Sid = integrity_sid;
    
    BOOL result = SetTokenInformation(token, TokenIntegrityLevel, &token_label, sizeof(token_label));
    
    FreeSid(integrity_sid);
    return result ? 0 : -1;
}

/**
 * @brief Remove a privilege from a token
 * @param token Token handle
 * @param privilege Privilege to remove
 * @return 0 on success, -1 on failure
 */
int windows_drop_privilege(HANDLE token, enum containerv_windows_privilege privilege) {
    if (!token) return -1;
    
    const wchar_t* priv_name = get_privilege_name(privilege);
    if (!priv_name) return -1;
    
    LUID privilege_luid;
    if (!LookupPrivilegeValueW(NULL, priv_name, &privilege_luid)) {
        return -1;
    }
    
    TOKEN_PRIVILEGES token_privs;
    token_privs.PrivilegeCount = 1;
    token_privs.Privileges[0].Luid = privilege_luid;
    token_privs.Privileges[0].Attributes = 0; // Remove privilege
    
    return AdjustTokenPrivileges(token, FALSE, &token_privs, 0, NULL, NULL) ? 0 : -1;
}

/**
 * @brief Add a privilege to a token
 * @param token Token handle
 * @param privilege Privilege to add
 * @return 0 on success, -1 on failure
 */
int windows_add_privilege(HANDLE token, enum containerv_windows_privilege privilege) {
    if (!token) return -1;
    
    const wchar_t* priv_name = get_privilege_name(privilege);
    if (!priv_name) return -1;
    
    LUID privilege_luid;
    if (!LookupPrivilegeValueW(NULL, priv_name, &privilege_luid)) {
        return -1;
    }
    
    TOKEN_PRIVILEGES token_privs;
    token_privs.PrivilegeCount = 1;
    token_privs.Privileges[0].Luid = privilege_luid;
    token_privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    return AdjustTokenPrivileges(token, FALSE, &token_privs, 0, NULL, NULL) ? 0 : -1;
}

/**
 * @brief Apply job object security restrictions
 * @param job_handle Job object handle
 * @param profile Security profile
 * @return 0 on success, -1 on failure
 */
int windows_apply_job_security(HANDLE job_handle, const struct containerv_security_profile* profile) {
    if (!job_handle || !profile) {
        return -1;
    }
    
    // Set basic UI restrictions for security
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui_restrictions = {0};
    
    if (profile->level >= CV_SECURITY_RESTRICTED) {
        ui_restrictions.UIRestrictionsClass = 
            JOB_OBJECT_UILIMIT_DESKTOP |         // Prevent desktop access
            JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | // Prevent display changes
            JOB_OBJECT_UILIMIT_GLOBALATOMS |     // Prevent global atom manipulation
            JOB_OBJECT_UILIMIT_HANDLES |         // Prevent handle inheritance
            JOB_OBJECT_UILIMIT_READCLIPBOARD |   // Prevent clipboard read
            JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | // Prevent system parameter changes
            JOB_OBJECT_UILIMIT_WRITECLIPBOARD;    // Prevent clipboard write
    }
    
    if (profile->level >= CV_SECURITY_STRICT) {
        ui_restrictions.UIRestrictionsClass |=
            JOB_OBJECT_UILIMIT_EXITWINDOWS;      // Prevent system shutdown
    }
    
    if (!SetInformationJobObject(job_handle, JobObjectBasicUIRestrictions,
                                &ui_restrictions, sizeof(ui_restrictions))) {
        return -1;
    }
    
    // Set security limit information
    JOBOBJECT_SECURITY_LIMIT_INFORMATION security_limits = {0};
    security_limits.SecurityLimitFlags = JOB_OBJECT_SECURITY_NO_ADMIN;
    
    if (profile->level >= CV_SECURITY_STRICT) {
        security_limits.SecurityLimitFlags |= 
            JOB_OBJECT_SECURITY_RESTRICTED_TOKEN; // Use restricted token
    }
    
    if (!SetInformationJobObject(job_handle, JobObjectSecurityLimitInformation,
                                &security_limits, sizeof(security_limits))) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Apply comprehensive Windows security profile
 * @param profile Security profile to apply
 * @param process_token Output handle to restricted process token
 * @param appcontainer_sid Output AppContainer SID
 * @return 0 on success, -1 on failure
 */
int windows_apply_security_profile(const struct containerv_security_profile* profile,
                                  HANDLE* process_token,
                                  PSID* appcontainer_sid) {
    if (!profile) return -1;
    
    // 1. Create restricted token
    HANDLE restricted_token;
    if (windows_create_restricted_token(profile, &restricted_token) != 0) {
        return -1;
    }
    
    // 2. Set integrity level
    if (profile->integrity_level) {
        if (windows_set_integrity_level(restricted_token, profile->integrity_level) != 0) {
            CloseHandle(restricted_token);
            return -1;
        }
    }
    
    // 3. Remove dangerous privileges
    const enum containerv_windows_privilege dangerous_privs[] = {
        CV_PRIV_DEBUG, CV_PRIV_LOAD_DRIVER, CV_PRIV_TCB,
        CV_PRIV_SECURITY, CV_PRIV_SYSTEM_TIME, CV_PRIV_SHUTDOWN
    };
    
    for (size_t i = 0; i < sizeof(dangerous_privs) / sizeof(dangerous_privs[0]); i++) {
        windows_drop_privilege(restricted_token, dangerous_privs[i]);
        // Continue on failure - privilege might not be present
    }
    
    // 4. Create AppContainer if requested
    PSID app_sid = NULL;
    if (profile->use_app_container) {
        if (windows_create_appcontainer(profile, &app_sid) != 0) {
            CloseHandle(restricted_token);
            return -1;
        }
    }
    
    if (process_token) {
        *process_token = restricted_token;
    } else {
        CloseHandle(restricted_token);
    }
    
    if (appcontainer_sid) {
        *appcontainer_sid = app_sid;
    } else if (app_sid) {
        LocalFree(app_sid);
    }
    
    return 0;
}

/**
 * @brief Verify that current process has expected security restrictions
 * @param profile Expected security profile
 * @return 0 if compliant, -1 if not
 */
int windows_verify_security_profile(const struct containerv_security_profile* profile) {
    if (!profile) return -1;
    
    HANDLE current_token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current_token)) {
        return -1;
    }
    
    // Check integrity level
    if (profile->integrity_level) {
        DWORD length = 0;
        GetTokenInformation(current_token, TokenIntegrityLevel, NULL, 0, &length);
        
        if (length > 0) {
            TOKEN_MANDATORY_LABEL* label = malloc(length);
            if (label && GetTokenInformation(current_token, TokenIntegrityLevel, 
                                           label, length, &length)) {
                
                DWORD* rid_ptr = GetSidSubAuthority(label->Label.Sid,
                                                   *GetSidSubAuthorityCount(label->Label.Sid) - 1);
                
                DWORD expected_rid;
                if (strcmp(profile->integrity_level, "low") == 0) {
                    expected_rid = SECURITY_MANDATORY_LOW_RID;
                } else if (strcmp(profile->integrity_level, "medium") == 0) {
                    expected_rid = SECURITY_MANDATORY_MEDIUM_RID;
                } else if (strcmp(profile->integrity_level, "high") == 0) {
                    expected_rid = SECURITY_MANDATORY_HIGH_RID;
                } else {
                    expected_rid = SECURITY_MANDATORY_MEDIUM_RID; // Default
                }
                
                bool integrity_ok = (rid_ptr && *rid_ptr == expected_rid);
                free(label);
                
                CloseHandle(current_token);
                return integrity_ok ? 0 : -1;
            }
            free(label);
        }
    }
    
    CloseHandle(current_token);
    return 0; // Basic verification passed
}

/**
 * @brief Create process with security profile applied
 * @param profile Security profile
 * @param command_line Command to execute
 * @param process_info Output process information
 * @return 0 on success, -1 on failure
 */
int windows_create_secure_process(const struct containerv_security_profile* profile,
                                 wchar_t* command_line,
                                 PROCESS_INFORMATION* process_info) {
    if (!profile || !command_line || !process_info) {
        return -1;
    }
    
    HANDLE restricted_token = NULL;
    PSID appcontainer_sid = NULL;
    
    // Apply security profile to get restricted token and AppContainer
    if (windows_apply_security_profile(profile, &restricted_token, &appcontainer_sid) != 0) {
        return -1;
    }
    
    STARTUPINFOEXW startup_info = {0};
    startup_info.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    
    DWORD creation_flags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
    
    // Set up attribute list for AppContainer
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    
    startup_info.lpAttributeList = malloc(attr_size);
    if (!startup_info.lpAttributeList) {
        if (restricted_token) CloseHandle(restricted_token);
        if (appcontainer_sid) LocalFree(appcontainer_sid);
        return -1;
    }
    
    if (!InitializeProcThreadAttributeList(startup_info.lpAttributeList, 1, 0, &attr_size)) {
        free(startup_info.lpAttributeList);
        if (restricted_token) CloseHandle(restricted_token);
        if (appcontainer_sid) LocalFree(appcontainer_sid);
        return -1;
    }
    
    // Add AppContainer attribute if we have one
    if (appcontainer_sid) {
        UpdateProcThreadAttribute(startup_info.lpAttributeList, 0,
                                PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                appcontainer_sid, GetLengthSid(appcontainer_sid),
                                NULL, NULL);
    }
    
    // Create the process
    BOOL result = CreateProcessAsUserW(restricted_token, NULL, command_line,
                                      NULL, NULL, FALSE, creation_flags,
                                      NULL, NULL, &startup_info.StartupInfo, process_info);
    
    // Cleanup
    DeleteProcThreadAttributeList(startup_info.lpAttributeList);
    free(startup_info.lpAttributeList);
    if (restricted_token) CloseHandle(restricted_token);
    if (appcontainer_sid) LocalFree(appcontainer_sid);
    
    return result ? 0 : -1;
}

#endif // _WIN32