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

#include <chef/platform.h>
#include <chef/runtime.h>
#include <stdlib.h>

// The format of the base can be either of 
// ubuntu:24
// windows:servercore-ltsc2022
// windows:nanoserver-ltsc2022
// windows:ltsc2022
// From this, derive the guest type
static enum chef_target_runtime __runtime_target(const char* type)
{
    if (strncmp(type, "ubuntu", 6) == 0) {
        return CHEF_RUNTIME_LINUX;
    } else if (strncmp(type, "windows", 7) == 0) {
        return CHEF_RUNTIME_WINDOWS;
    }
    return CHEF_RUNTIME_UNSUPPORTED;
}

static int __split_name_into_type_and_version(const char* name, char** typeOut, char** versionOut)
{
    const char* split;

    split = strchr(name, ':');
    if (split == NULL) {
        return -1;
    }
    
    *typeOut = platform_strndup(name, (size_t)(split - name));
    *versionOut = platform_strdup(split + 1);
    if (*typeOut == NULL || *versionOut == NULL) {
        free(*typeOut);
        free(*versionOut);
        return -1;
    }
    return 0;
}

struct chef_runtime_info* chef_runtime_info_parse(const char* name)
{
    struct chef_runtime_info* info;

    if (name == NULL) {
        return NULL;
    }

    info = calloc(1, sizeof(struct chef_runtime_info));
    if (info == NULL) {
        return NULL;
    }
    
    if (__split_name_into_type_and_version(name, (char**)&info->name, (char**)&info->version)) {
        free(info);
        return NULL;
    }
    
    info->runtime = __runtime_target(info->name);
    return info;
}

void chef_runtime_info_delete(struct chef_runtime_info* info)
{
    if (info == NULL) {
        return;
    }
    free(info->name);
    free(info->version);
    free(info);
}

static int __has_drive_letter(const char* path)
{
    return (path != NULL && isalpha((unsigned char)path[0]) && path[1] == ':');
}

// Build an absolute, normalized POSIX path first.
// - Strip drive letters (C:)
// - Remove leading slashes/backslashes
// - Replace '\\' with '/'
static int __normalize_to_linux_path(const char* path, const char* prefix, char** normalizedPathOut)
{
    const char* s = path;
    const char* pf = prefix;
    size_t      len;
    char*       norm;
    
    // If converting from a windows path, 
    // strip drive letter and leading slashes
    if (__has_drive_letter(s)) {
        s += 2;
    }
    while (*s == '/' || *s == '\\') {
        s++;
    }

    // If the prefix is empty, then we make it '/'
    if (pf == NULL) {
        pf = "/";
    }

    norm = strpathcombine(pf, s);
    if (norm == NULL) {
        return -1;
    }

    // Replace backslashes with slashes
    for (char* p = norm; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    *normalizedPathOut = norm;
    return 0;
}

static int __normalize_to_windows_path(const char* path, const char* prefix, char** normalizedPathOut)
{
    const char* s = path;
    char*       norm;
    size_t      len;
    
    // If it has a drive letter, then we assume it's already normalized
    // as a Windows path and just return it.
    if (__has_drive_letter(path)) {
        *normalizedPathOut = platform_strdup(path);
        return 0;
    }
    
    // Skip leading slashes/backslashes
    while (*s == '/' || *s == '\\') {
        s++;
    }
    
    norm = strpathcombine(prefix, s);
    if (norm == NULL) {
        return -1;
    }
    
    // Replace slashes with backslashes
    for (char* p = norm; *p; ++p) {
        if (*p == '/') {
            *p = '\\';
        }
    }

    *normalizedPathOut = norm;
    return 0;
}

int chef_runtime_normalize_path(
    const char*                     path,
    const char*                     prefix,
    const struct chef_runtime_info* runtimeInfo,
    char**                          normalizedPathOut)
{
    if (path == NULL || runtimeInfo == NULL || normalizedPathOut == NULL) {
        return -1;
    }

    switch (runtimeInfo->runtime) {
        case CHEF_RUNTIME_LINUX:
            return __normalize_to_linux_path(path, prefix, normalizedPathOut);
        case CHEF_RUNTIME_WINDOWS:
            return __normalize_to_windows_path(path, prefix, normalizedPathOut);
    }
    return -1;
}
