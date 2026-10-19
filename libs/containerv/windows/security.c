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
#include <errno.h>

#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <objbase.h>
#include <vlog.h>
#include "private.h"

// Host-side security helpers: ACLs for HCS-mapped files and job object restrictions.

struct windows_vm_access {
    struct windows_vm_access* next;
    HANDLE file;
    HANDLE mutex;
    BY_HANDLE_FILE_INFORMATION identity;
    PSID sid;
    DWORD rights;
};

int windows_vm_sid(const char* vm_id, PSID* sid_out)
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    GUID guid;
    DWORD words[4];
    wchar_t text[40];
    if (!vm_id || !sid_out || strlen(vm_id) != 36) { errno = EINVAL; return -1; }
    *sid_out = NULL;
    swprintf(text, 40, L"{%hs}", vm_id);
    if (FAILED(CLSIDFromString(text, &guid))) { errno = EINVAL; return -1; }
    // HCS uses S-1-5-83-1 followed by the four DWORDs of the VM GUID.
    // The regression compares this identity with HcsGrantVmAccess itself.
    memcpy(words, &guid, sizeof(words));
    if (!AllocateAndInitializeSid(&authority, 6, 83, 1,
            words[0], words[1], words[2], words[3], 0, 0, sid_out)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int __vm_access_update(struct windows_vm_access* grant, DWORD rights, int first)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PACL current = NULL, updated = NULL;
    EXPLICIT_ACCESSW access = {0};
    DWORD status;
    DWORD wait = WaitForSingleObject(grant->mutex, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { errno = EACCES; return -1; }
    status = GetSecurityInfo(grant->file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        NULL, NULL, &current, NULL, &descriptor);
    if (status != ERROR_SUCCESS) goto cleanup;
    // Never take ownership of an existing explicit grant (or denial). Each
    // runtime uses a fresh VM identity, and cleanup owns only its own entries.
    if (first && current) {
        for (DWORD i = 0; i < current->AceCount; ++i) {
            ACCESS_ALLOWED_ACE* ace;
            if (!GetAce(current, i, (void**)&ace)) { status = GetLastError(); goto cleanup; }
            if (!(ace->Header.AceFlags & INHERITED_ACE) &&
                (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceType == ACCESS_DENIED_ACE_TYPE) &&
                EqualSid(&ace->SidStart, grant->sid)) { status = ERROR_ALREADY_EXISTS; goto cleanup; }
        }
    }
    // A null DACL already grants everyone access. Do not replace it with a
    // restrictive ACL or claim ownership of access we did not add.
    if (!current) { status = ERROR_INVALID_ACL; goto cleanup; }
    access.grfAccessPermissions = rights;
    access.grfAccessMode = rights ? GRANT_ACCESS : REVOKE_ACCESS;
    access.grfInheritance = (grant->identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    access.Trustee.ptstrName = grant->sid;
    status = SetEntriesInAclW(1, &access, current, &updated);
    if (status == ERROR_SUCCESS) {
        status = SetSecurityInfo(grant->file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            NULL, NULL, updated, NULL);
    }
cleanup:
    LocalFree(updated);
    LocalFree(descriptor);
    ReleaseMutex(grant->mutex);
    if (status != ERROR_SUCCESS) {
        VLOG_ERROR("containerv[security]", "VM ACL update failed: %lu\n", status);
        errno = EACCES;
        return -1;
    }
    return 0;
}

int windows_grant_vm_access(struct containerv_container* container, const char* path, int readonly)
{
    struct windows_vm_access* grant;
    BY_HANDLE_FILE_INFORMATION identity;
    wchar_t mutex_name[100];
    wchar_t* wide;
    HANDLE file;
    DWORD rights = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    if (!container || !path || !*path) { errno = EINVAL; return -1; }
    if (!readonly) rights |= FILE_GENERIC_WRITE;
    wide = __windows_utf8_to_wide_alloc(path);
    if (!wide) return -1;
    file = CreateFileW(wide, READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (file == INVALID_HANDLE_VALUE) { errno = EACCES; return -1; }
    if (!GetFileInformationByHandle(file, &identity)) { CloseHandle(file); return -1; }
    for (grant = container->vm_access; grant; grant = grant->next) {
        if (grant->identity.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
            grant->identity.nFileIndexHigh == identity.nFileIndexHigh &&
            grant->identity.nFileIndexLow == identity.nFileIndexLow) {
            CloseHandle(file);
            if ((grant->rights & rights) == rights) return 0;
            if (__vm_access_update(grant, rights, 0) != 0) return -1;
            grant->rights |= rights;
            return 0;
        }
    }
    grant = calloc(1, sizeof(*grant));
    if (!grant) { CloseHandle(file); return -1; }
    grant->file = file;
    grant->identity = identity;
    swprintf(mutex_name, 100, L"Global\\ChefVmAcl-%08lx-%08lx%08lx",
        identity.dwVolumeSerialNumber, identity.nFileIndexHigh, identity.nFileIndexLow);
    grant->mutex = CreateMutexW(NULL, FALSE, mutex_name);
    if (!grant->mutex || windows_vm_sid(container->id, &grant->sid) != 0 ||
        __vm_access_update(grant, rights, 1) != 0) {
        if (grant->mutex) CloseHandle(grant->mutex);
        if (grant->sid) FreeSid(grant->sid);
        CloseHandle(file);
        free(grant);
        return -1;
    }
    grant->rights = rights;
    grant->next = container->vm_access;
    container->vm_access = grant;
    return 0;
}

void windows_revoke_vm_access(struct containerv_container* container)
{
    while (container->vm_access) {
        struct windows_vm_access* grant = container->vm_access;
        container->vm_access = grant->next;
        if (__vm_access_update(grant, 0, 0) != 0) {
            VLOG_ERROR("containerv[security]", "failed to revoke access for VM %s\n", container->id);
        }
        CloseHandle(grant->file);
        CloseHandle(grant->mutex);
        FreeSid(grant->sid);
        free(grant);
    }
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
