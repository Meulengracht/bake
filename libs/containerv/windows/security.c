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

// Host-side security helpers: ACLs for HCS-mapped files and job object restrictions.

int windows_grant_vm_group_access(const char* path)
{
    static const char* vmGroupSidString = "S-1-5-83-0";
    PSECURITY_DESCRIPTOR securityDescriptor = NULL;
    PACL                 currentAcl = NULL;
    PACL                 updatedAcl = NULL;
    PSID                 vmGroupSid = NULL;
    EXPLICIT_ACCESSA     access = { 0 };
    DWORD                attributes;
    DWORD                status;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }

    if (!ConvertStringSidToSidA(vmGroupSidString, &vmGroupSid)) {
        errno = EACCES;
        return -1;
    }

    status = GetNamedSecurityInfoA(
        (LPSTR)path,
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        NULL,
        NULL,
        &currentAcl,
        NULL,
        &securityDescriptor);
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = (attributes & FILE_ATTRIBUTE_DIRECTORY)
        ? SUB_CONTAINERS_AND_OBJECTS_INHERIT
        : NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = (LPSTR)vmGroupSid;

    status = SetEntriesInAclA(1, &access, currentAcl, &updatedAcl);
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    status = SetNamedSecurityInfoA(
        (LPSTR)path,
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        NULL,
        NULL,
        updatedAcl,
        NULL);

cleanup:
    LocalFree(updatedAcl);
    LocalFree(securityDescriptor);
    LocalFree(vmGroupSid);
    if (status != ERROR_SUCCESS) {
        errno = EACCES;
        return -1;
    }
    return 0;
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
