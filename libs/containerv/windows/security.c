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
    {CV_PRIV_DEBUG, L"SeDebugPrivilege", "Debug programs"},
    {CV_PRIV_BACKUP, L"SeBackupPrivilege", "Back up files and directories"},
    {CV_PRIV_RESTORE, L"SeRestorePrivilege", "Restore files and directories"},
    {CV_PRIV_SHUTDOWN, L"SeShutdownPrivilege", "Shut down the system"},
    {CV_PRIV_LOAD_DRIVER, L"SeLoadDriverPrivilege", "Load and unload device drivers"},
    {CV_PRIV_SYSTEM_TIME, L"SeSystemtimePrivilege", "Change the system time"},
    {CV_PRIV_TAKE_OWNERSHIP, L"SeTakeOwnershipPrivilege", "Take ownership of files or other objects"},
    {CV_PRIV_TCB, L"SeTcbPrivilege", "Act as part of the operating system"},
    {CV_PRIV_SECURITY, L"SeSecurityPrivilege", "Manage auditing and security log"},
    {CV_PRIV_INCREASE_QUOTA, L"SeIncreaseQuotaPrivilege", "Adjust memory quotas for a process"}
};

static const size_t privilege_map_size = sizeof(privilege_map) / sizeof(privilege_map[0]);

// Map containerv privilege enum to Windows privilege name.
static const wchar_t* get_privilege_name(enum containerv_windows_privilege priv)
{
    size_t i;

    for (i = 0; i < privilege_map_size; i++) {
        if (privilege_map[i].cv_priv == priv) {
            return privilege_map[i].name;
        }
    }
    return NULL;
}

int windows_apply_security_profile(const struct containerv_policy* policy,
                                  HANDLE* process_token,
                                  PSID* appcontainer_sid);

/**
 * @brief Create an AppContainer for container isolation
 * @param policy Security policy
 * @param appcontainer_sid Output pointer for AppContainer SID
 * @return 0 on success, -1 on failure
 */
int windows_create_appcontainer(
    const struct containerv_policy* policy,
    PSID*                           appcontainerSid)
{
    int         useAppContainer;
    const char* integrityLevel;
    const char* const* capabilitySids;
    int         capabilitySidCount;
    const char* appContainerNameUtf8;
    const char* displayNameUtf8;
    wchar_t     appContainerName[256];
    wchar_t     displayName[512];
    PSID_AND_ATTRIBUTES capabilities;
    DWORD       capabilityCount;
    int         i;
    int         j;
    HRESULT     hr;

    if (!policy || !appcontainerSid) {
        return -1;
    }

    useAppContainer = 0;
    integrityLevel = NULL;
    capabilitySids = NULL;
    capabilitySidCount = 0;
    appContainerNameUtf8 = NULL;
    displayNameUtf8 = NULL;
    memset(appContainerName, 0, sizeof(appContainerName));
    memset(displayName, 0, sizeof(displayName));
    capabilities = NULL;
    capabilityCount = 0;
    i = 0;
    j = 0;
    hr = S_OK;

    (void)integrityLevel;
    if (containerv_policy_get_windows_isolation(
            policy,
            &useAppContainer,
            &integrityLevel,
            &capabilitySids,
            &capabilitySidCount) != 0) {
        return -1;
    }

    if (!useAppContainer) {
        *appcontainerSid = NULL;
        return 0;
    }

    // Use a stable AppContainer name. Callers that need per-container isolation should
    // integrate container IDs into this name.
    appContainerNameUtf8 = "chef.container";
    displayNameUtf8 = "Chef Container";

    if (MultiByteToWideChar(CP_UTF8, 0, appContainerNameUtf8, -1,
                            appContainerName, sizeof(appContainerName) / sizeof(wchar_t)) == 0) {
        return -1;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, displayNameUtf8, -1,
                            displayName, sizeof(displayName) / sizeof(wchar_t)) == 0) {
        return -1;
    }
    
    // Create capability array if specified
    if (capabilitySids && capabilitySidCount > 0) {
        capabilities = calloc((size_t)capabilitySidCount, sizeof(SID_AND_ATTRIBUTES));
        if (!capabilities) {
            return -1;
        }

        capabilityCount = (DWORD)capabilitySidCount;
        for (i = 0; i < capabilitySidCount; i++) {
            if (capabilitySids[i] != NULL && ConvertStringSidToSidA(capabilitySids[i], &capabilities[i].Sid)) {
                capabilities[i].Attributes = SE_GROUP_ENABLED;
            } else {
                // Cleanup on failure
                for (j = 0; j < i; j++) {
                    LocalFree(capabilities[j].Sid);
                }
                free(capabilities);
                return -1;
            }
        }
    }
    
    // Create the AppContainer
    hr = CreateAppContainerProfile(appContainerName, displayName, displayName,
                                  capabilities, capabilityCount, appcontainerSid);
    
    // Cleanup capabilities
    if (capabilities) {
        for (i = 0; i < (int)capabilityCount; i++) {
            LocalFree(capabilities[i].Sid);
        }
        free(capabilities);
    }
    
    if (SUCCEEDED(hr)) {
        return 0;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        // AppContainer already exists, derive SID
        return DeriveAppContainerSidFromAppContainerName(appContainerName, appcontainerSid) == S_OK ? 0 : -1;
    } else {
        return -1;
    }
}

/**
 * @brief Create a restricted token with limited privileges
 * @param policy Security policy
 * @param restricted_token Output handle to restricted token
 * @return 0 on success, -1 on failure
 */
int windows_create_restricted_token(const struct containerv_policy* policy,
                                   HANDLE* restricted_token) {
    if (!policy || !restricted_token) {
        return -1;
    }
    
    HANDLE current_token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &current_token)) {
        return -1;
    }
    
    enum containerv_security_level level = containerv_policy_get_security_level(policy);

    // For high security levels, create a very restricted token
    if (level >= CV_SECURITY_STRICT) {
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
        // For lower security levels, duplicate as a PRIMARY token for CreateProcessAsUser.
        if (!DuplicateTokenEx(
                current_token,
                TOKEN_ALL_ACCESS,
                NULL,
                SecurityImpersonation,
                TokenPrimary,
                restricted_token)) {
            CloseHandle(current_token);
            return -1;
        }
    }
    
    CloseHandle(current_token);
    return 0;
}

int windows_create_secure_process_ex(
    const struct containerv_policy* policy,
    wchar_t*                        command_line,
    const wchar_t*                  current_directory,
    void*                           environment,
    PROCESS_INFORMATION*            process_info)
{
    if (!policy || !command_line || !process_info) {
        return -1;
    }

    HANDLE restricted_token = NULL;
    PSID appcontainer_sid = NULL;

    int use_app_container = 0;
    const char* integrity_level = NULL;
    const char* const* capability_sids = NULL;
    int capability_sid_count = 0;
    if (containerv_policy_get_windows_isolation(
            policy,
            &use_app_container,
            &integrity_level,
            &capability_sids,
            &capability_sid_count) != 0) {
        return -1;
    }

    if (windows_apply_security_profile(policy, &restricted_token, &appcontainer_sid) != 0) {
        return -1;
    }

    STARTUPINFOEXW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.StartupInfo.cb = sizeof(startup_info);

    DWORD creation_flags = CREATE_SUSPENDED;
    if (environment != NULL) {
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    // Build SECURITY_CAPABILITIES when AppContainer isolation is enabled.
    SECURITY_CAPABILITIES sec_caps;
    ZeroMemory(&sec_caps, sizeof(sec_caps));
    sec_caps.AppContainerSid = appcontainer_sid;

    SID_AND_ATTRIBUTES* cap_attrs = NULL;
    DWORD cap_attr_count = 0;
    if (use_app_container && capability_sids && capability_sid_count > 0) {
        cap_attrs = calloc((size_t)capability_sid_count, sizeof(SID_AND_ATTRIBUTES));
        if (!cap_attrs) {
            CloseHandle(restricted_token);
            if (appcontainer_sid) LocalFree(appcontainer_sid);
            return -1;
        }

        cap_attr_count = (DWORD)capability_sid_count;
        for (int i = 0; i < capability_sid_count; i++) {
            PSID sid = NULL;
            if (capability_sids[i] == NULL || !ConvertStringSidToSidA(capability_sids[i], &sid)) {
                for (int j = 0; j < i; j++) {
                    if (cap_attrs[j].Sid) {
                        LocalFree(cap_attrs[j].Sid);
                    }
                }
                free(cap_attrs);
                CloseHandle(restricted_token);
                if (appcontainer_sid) LocalFree(appcontainer_sid);
                return -1;
            }
            cap_attrs[i].Sid = sid;
            cap_attrs[i].Attributes = SE_GROUP_ENABLED;
        }
    }

    if (use_app_container) {
        sec_caps.Capabilities = cap_attrs;
        sec_caps.CapabilityCount = cap_attr_count;
        sec_caps.Reserved = 0;
    }

    SIZE_T attr_size = 0;
    if (use_app_container && appcontainer_sid) {
        creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        startup_info.lpAttributeList = malloc(attr_size);
        if (!startup_info.lpAttributeList) {
            if (cap_attrs) {
                for (DWORD i = 0; i < cap_attr_count; i++) {
                    if (cap_attrs[i].Sid) LocalFree(cap_attrs[i].Sid);
                }
                free(cap_attrs);
            }
            CloseHandle(restricted_token);
            if (appcontainer_sid) LocalFree(appcontainer_sid);
            return -1;
        }

        if (!InitializeProcThreadAttributeList(startup_info.lpAttributeList, 1, 0, &attr_size)) {
            free(startup_info.lpAttributeList);
            startup_info.lpAttributeList = NULL;
            if (cap_attrs) {
                for (DWORD i = 0; i < cap_attr_count; i++) {
                    if (cap_attrs[i].Sid) LocalFree(cap_attrs[i].Sid);
                }
                free(cap_attrs);
            }
            CloseHandle(restricted_token);
            if (appcontainer_sid) LocalFree(appcontainer_sid);
            return -1;
        }

        if (!UpdateProcThreadAttribute(
                startup_info.lpAttributeList,
                0,
                PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                &sec_caps,
                sizeof(sec_caps),
                NULL,
                NULL)) {
            DeleteProcThreadAttributeList(startup_info.lpAttributeList);
            free(startup_info.lpAttributeList);
            startup_info.lpAttributeList = NULL;
            if (cap_attrs) {
                for (DWORD i = 0; i < cap_attr_count; i++) {
                    if (cap_attrs[i].Sid) LocalFree(cap_attrs[i].Sid);
                }
                free(cap_attrs);
            }
            CloseHandle(restricted_token);
            if (appcontainer_sid) LocalFree(appcontainer_sid);
            return -1;
        }
    }

    BOOL ok = CreateProcessAsUserW(
        restricted_token,
        NULL,
        command_line,
        NULL,
        NULL,
        FALSE,
        creation_flags,
        environment,
        current_directory,
        &startup_info.StartupInfo,
        process_info);

    if (startup_info.lpAttributeList) {
        DeleteProcThreadAttributeList(startup_info.lpAttributeList);
        free(startup_info.lpAttributeList);
        startup_info.lpAttributeList = NULL;
    }

    if (cap_attrs) {
        for (DWORD i = 0; i < cap_attr_count; i++) {
            if (cap_attrs[i].Sid) {
                LocalFree(cap_attrs[i].Sid);
            }
        }
        free(cap_attrs);
    }

    if (restricted_token) CloseHandle(restricted_token);
    if (appcontainer_sid) LocalFree(appcontainer_sid);

    return ok ? 0 : -1;
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
 * @param policy Security policy
 * @return 0 on success, -1 on failure
 */
int windows_apply_job_security(HANDLE job_handle, const struct containerv_policy* policy) {
    if (!job_handle || !policy) {
        return -1;
    }

    enum containerv_security_level level = containerv_policy_get_security_level(policy);
    
    // Set basic UI restrictions for security
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui_restrictions = {0};
    
    if (level >= CV_SECURITY_RESTRICTED) {
        ui_restrictions.UIRestrictionsClass = 
            JOB_OBJECT_UILIMIT_DESKTOP |         // Prevent desktop access
            JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | // Prevent display changes
            JOB_OBJECT_UILIMIT_GLOBALATOMS |     // Prevent global atom manipulation
            JOB_OBJECT_UILIMIT_HANDLES |         // Prevent handle inheritance
            JOB_OBJECT_UILIMIT_READCLIPBOARD |   // Prevent clipboard read
            JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | // Prevent system parameter changes
            JOB_OBJECT_UILIMIT_WRITECLIPBOARD;    // Prevent clipboard write
    }
    
    if (level >= CV_SECURITY_STRICT) {
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
    
    if (level >= CV_SECURITY_STRICT) {
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
 * @brief Apply Windows security policy
 * @param policy Security policy to apply
 * @param process_token Output handle to restricted process token
 * @param appcontainer_sid Output AppContainer SID
 * @return 0 on success, -1 on failure
 */
int windows_apply_security_profile(const struct containerv_policy* policy,
                                  HANDLE* process_token,
                                  PSID* appcontainer_sid) {
    if (!policy) return -1;

    int use_app_container = 0;
    const char* integrity_level = NULL;
    if (containerv_policy_get_windows_isolation(policy, &use_app_container, &integrity_level, NULL, NULL) != 0) {
        return -1;
    }
    
    // 1. Create restricted token
    HANDLE restricted_token;
    if (windows_create_restricted_token(policy, &restricted_token) != 0) {
        return -1;
    }
    
    // 2. Set integrity level
    if (integrity_level) {
        if (windows_set_integrity_level(restricted_token, integrity_level) != 0) {
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
    if (use_app_container) {
        if (windows_create_appcontainer(policy, &app_sid) != 0) {
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
 * @param policy Expected security policy
 * @return 0 if compliant, -1 if not
 */
int windows_verify_security_profile(const struct containerv_policy* policy) {
    if (!policy) return -1;

    const char* integrity_level = NULL;
    if (containerv_policy_get_windows_isolation(policy, NULL, &integrity_level, NULL, NULL) != 0) {
        return -1;
    }
    
    HANDLE current_token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current_token)) {
        return -1;
    }
    
    // Check integrity level
    if (integrity_level) {
        DWORD length = 0;
        GetTokenInformation(current_token, TokenIntegrityLevel, NULL, 0, &length);
        
        if (length > 0) {
            TOKEN_MANDATORY_LABEL* label = malloc(length);
            if (label && GetTokenInformation(current_token, TokenIntegrityLevel, 
                                           label, length, &length)) {
                
                DWORD* rid_ptr = GetSidSubAuthority(label->Label.Sid,
                                                   *GetSidSubAuthorityCount(label->Label.Sid) - 1);
                
                DWORD expected_rid;
                if (strcmp(integrity_level, "low") == 0) {
                    expected_rid = SECURITY_MANDATORY_LOW_RID;
                } else if (strcmp(integrity_level, "medium") == 0) {
                    expected_rid = SECURITY_MANDATORY_MEDIUM_RID;
                } else if (strcmp(integrity_level, "high") == 0) {
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
 * @brief Create process with security policy applied
 * @param policy Security policy
 * @param command_line Command to execute
 * @param process_info Output process information
 * @return 0 on success, -1 on failure
 */
int windows_create_secure_process(const struct containerv_policy* policy,
                                 wchar_t* command_line,
                                 PROCESS_INFORMATION* process_info) {
    if (!policy || !command_line || !process_info) {
        return -1;
    }
    
    HANDLE restricted_token = NULL;
    PSID appcontainer_sid = NULL;
    
    // Apply security profile to get restricted token and AppContainer
    if (windows_apply_security_profile(policy, &restricted_token, &appcontainer_sid) != 0) {
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
