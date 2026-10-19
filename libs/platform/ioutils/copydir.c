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

#include <errno.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winioctl.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

static int __path_is_directory(const char* path)
{
    struct platform_stat st;

    return (path != NULL && platform_stat(path, &st) == 0 && st.type == PLATFORM_FILETYPE_DIRECTORY) ? 1 : 0;
}

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_COPYDIR_BUFFER_SIZE (128 * 1024)

static DWORD       __copydir_last_error;
static const char* __copydir_last_operation;

static void __set_errno_from_windows_error(DWORD error);

struct __backup_privilege_scope {
    HANDLE           token;
    TOKEN_PRIVILEGES previous_state;
    DWORD            previous_state_size;
    int              restore;
};

static void __set_copydir_error(const char* operation, DWORD error)
{
    __copydir_last_operation = operation;
    __copydir_last_error = error;
    __set_errno_from_windows_error(error);
}

static void __clear_copydir_error(void)
{
    __copydir_last_operation = NULL;
    __copydir_last_error = ERROR_SUCCESS;
}

static void __set_errno_from_windows_error(DWORD error)
{
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_BAD_PATHNAME:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            errno = EEXIST;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            errno = ENOMEM;
            break;
        case ERROR_DIRECTORY:
            errno = ENOTDIR;
            break;
        case ERROR_NOT_SUPPORTED:
            errno = ENOTSUP;
            break;
        default:
            errno = EIO;
            break;
    }
}

static wchar_t* __utf8_to_wide(const char* text)
{
    int      needed;
    wchar_t* converted;

    if (text == NULL) {
        errno = EINVAL;
        return NULL;
    }

    needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (needed <= 0) {
        errno = EINVAL;
        return NULL;
    }

    converted = calloc((size_t)needed, sizeof(wchar_t));
    if (converted == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, converted, needed) == 0) {
        free(converted);
        errno = EINVAL;
        return NULL;
    }
    return converted;
}

static int __is_absolute_wide_path(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0') {
        return 0;
    }

    if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return 1;
    }

    if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        return 1;
    }
    return 0;
}

static int __is_extended_wide_path(const wchar_t* path)
{
    if (path == NULL) {
        return 0;
    }

    return ((path[0] == L'\\' || path[0] == L'/') &&
            (path[1] == L'\\' || path[1] == L'/') &&
            path[2] == L'?' &&
            (path[3] == L'\\' || path[3] == L'/'));
}

static wchar_t* __normalize_extended_wide_path(const wchar_t* path)
{
    size_t         length;
    const wchar_t* tail;
    const wchar_t* prefix;
    size_t         prefix_length;
    wchar_t*       normalized;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (__is_extended_wide_path(path) || !__is_absolute_wide_path(path)) {
        normalized = calloc(wcslen(path) + 1, sizeof(wchar_t));
        if (normalized == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        wcscpy(normalized, path);
        return normalized;
    }

    if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        prefix = L"\\\\?\\UNC\\";
        prefix_length = 8;
        tail = path + 2;
    } else {
        prefix = L"\\\\?\\";
        prefix_length = 4;
        tail = path;
    }

    length = wcslen(tail);
    normalized = calloc(prefix_length + length + 1, sizeof(wchar_t));
    if (normalized == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    wmemcpy(normalized, prefix, prefix_length);
    wmemcpy(normalized + prefix_length, tail, length + 1);
    return normalized;
}

static wchar_t* __path_to_normalized_wide(const char* path)
{
    wchar_t* converted = __utf8_to_wide(path);
    wchar_t* normalized;

    if (converted == NULL) {
        return NULL;
    }

    // Extended paths bypass Win32 slash normalization.
    for (wchar_t* p = converted; *p; ++p) {
        if (*p == L'/') *p = L'\\';
    }

    normalized = __normalize_extended_wide_path(converted);
    free(converted);
    return normalized;
}

static wchar_t* __join_wide_path(const wchar_t* base, const wchar_t* child, size_t child_length)
{
    size_t   base_length;
    int      needs_separator;
    wchar_t* combined;

    if (base == NULL || child == NULL) {
        errno = EINVAL;
        return NULL;
    }

    base_length = wcslen(base);
    needs_separator = base_length != 0 && base[base_length - 1] != L'\\' && base[base_length - 1] != L'/';

    combined = calloc(base_length + (size_t)needs_separator + child_length + 1, sizeof(wchar_t));
    if (combined == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    wmemcpy(combined, base, base_length);
    if (needs_separator) {
        combined[base_length++] = L'\\';
    }
    wmemcpy(combined + base_length, child, child_length);
    combined[base_length + child_length] = L'\0';
    return combined;
}

static wchar_t* __mkdir_start_wide(wchar_t* path)
{
    wchar_t* current;

    if (path == NULL) {
        return NULL;
    }

    if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':') {
        if (path[2] == L'\\' || path[2] == L'/') {
            return path + 3;
        }
        return path + 2;
    }

    if (path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\') {
        if ((path[4] == L'U' || path[4] == L'u') &&
            (path[5] == L'N' || path[5] == L'n') &&
            (path[6] == L'C' || path[6] == L'c') &&
            path[7] == L'\\') {
            current = path + 8;
            while (*current && *current != L'\\' && *current != L'/') {
                current++;
            }
            if (*current) {
                current++;
                while (*current && *current != L'\\' && *current != L'/') {
                    current++;
                }
                if (*current) {
                    return current + 1;
                }
            }
            return current;
        }
        return path + 7;
    }

    if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        current = path + 2;
        while (*current && *current != L'\\' && *current != L'/') {
            current++;
        }
        if (*current) {
            current++;
            while (*current && *current != L'\\' && *current != L'/') {
                current++;
            }
            if (*current) {
                return current + 1;
            }
        }
        return current;
    }

    if (path[0] == L'\\' || path[0] == L'/') {
        return path + 1;
    }

    return path + 1;
}

static int __mkdir_recursive_wide(const wchar_t* path)
{
    size_t   length;
    wchar_t* working;
    wchar_t* current;

    if (path == NULL || path[0] == L'\0') {
        errno = EINVAL;
        return -1;
    }

    working = calloc(wcslen(path) + 1, sizeof(wchar_t));
    if (working == NULL) {
        errno = ENOMEM;
        return -1;
    }
    wcscpy(working, path);

    length = wcslen(working);
    if (length != 0 && (working[length - 1] == L'\\' || working[length - 1] == L'/')) {
        working[length - 1] = L'\0';
    }

    current = __mkdir_start_wide(working);
    for (wchar_t* p = current; *p; ++p) {
        wchar_t separator;

        if (*p != L'\\' && *p != L'/') {
            continue;
        }

        separator = *p;
        *p = L'\0';
        if (!CreateDirectoryW(working, NULL)) {
            DWORD error = GetLastError();

            if (error != ERROR_ALREADY_EXISTS) {
                *p = separator;
                free(working);
                __set_copydir_error("create destination directory", error);
                return -1;
            }
        }
        *p = separator;
    }

    if (!CreateDirectoryW(working, NULL)) {
        DWORD error = GetLastError();

        if (error != ERROR_ALREADY_EXISTS) {
            free(working);
            __set_copydir_error("create destination directory", error);
            return -1;
        }
    }

    free(working);
    return 0;
}

static void __backup_privilege_end(struct __backup_privilege_scope* scope)
{
    if (scope == NULL) {
        return;
    }

    if (scope->restore && scope->token != NULL) {
        AdjustTokenPrivileges(scope->token, FALSE, &scope->previous_state, 0, NULL, NULL);
    }
    if (scope->token != NULL) {
        CloseHandle(scope->token);
    }

    scope->token = NULL;
    scope->restore = 0;
}

static void __privilege_begin(struct __backup_privilege_scope* scope, const wchar_t* name)
{
    TOKEN_PRIVILEGES token_privs;
    LUID             privilege_luid;
    DWORD            error;

    if (scope == NULL) {
        return;
    }
    memset(scope, 0, sizeof(*scope));

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &scope->token)) {
        return;
    }

    if (!LookupPrivilegeValueW(NULL, name, &privilege_luid)) {
        __backup_privilege_end(scope);
        return;
    }

    token_privs.PrivilegeCount = 1;
    token_privs.Privileges[0].Luid = privilege_luid;
    token_privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    scope->previous_state_size = sizeof(scope->previous_state);
    if (!AdjustTokenPrivileges(
            scope->token,
            FALSE,
            &token_privs,
            sizeof(scope->previous_state),
            &scope->previous_state,
            &scope->previous_state_size)) {
        __backup_privilege_end(scope);
        return;
    }

    error = GetLastError();
    if (error == ERROR_NOT_ALL_ASSIGNED) {
        __backup_privilege_end(scope);
        return;
    }
    scope->restore = 1;
}

static HANDLE __open_directory_handle(const wchar_t* path)
{
    return CreateFileW(
        path,
    GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
}

static int __copyfile_windows(const wchar_t* source, const wchar_t* destination)
{
    HANDLE         source_handle;
    HANDLE         destination_handle;
    unsigned char* buffer;
    int            status = -1;

    source_handle = CreateFileW(
        source,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (source_handle == INVALID_HANDLE_VALUE) {
        __set_copydir_error("open source file", GetLastError());
        return -1;
    }

    destination_handle = CreateFileW(
        destination,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (destination_handle == INVALID_HANDLE_VALUE) {
        __set_copydir_error("open destination file", GetLastError());
        CloseHandle(source_handle);
        return -1;
    }

    buffer = malloc(PLATFORM_COPYDIR_BUFFER_SIZE);
    if (buffer == NULL) {
        errno = ENOMEM;
        CloseHandle(source_handle);
        CloseHandle(destination_handle);
        return -1;
    }

    while (1) {
        DWORD bytes_read = 0;

        if (!ReadFile(source_handle, buffer, PLATFORM_COPYDIR_BUFFER_SIZE, &bytes_read, NULL)) {
            __set_copydir_error("read source file", GetLastError());
            goto cleanup;
        }
        if (bytes_read == 0) {
            break;
        }

        {
            DWORD total_written = 0;

            while (total_written < bytes_read) {
                DWORD bytes_written = 0;

                if (!WriteFile(
                        destination_handle,
                        buffer + total_written,
                        bytes_read - total_written,
                        &bytes_written,
                        NULL)) {
                    __set_copydir_error("write destination file", GetLastError());
                    goto cleanup;
                }
                total_written += bytes_written;
            }
        }
    }

    status = 0;

cleanup:
    free(buffer);
    CloseHandle(source_handle);
    CloseHandle(destination_handle);
    return status;
}

static int __is_dot_entry(const WCHAR* file_name, DWORD file_name_length)
{
    if (file_name_length == 1 && file_name[0] == L'.') {
        return 1;
    }
    if (file_name_length == 2 && file_name[0] == L'.' && file_name[1] == L'.') {
        return 1;
    }
    return 0;
}

/* Copy only known link tags, without resolving targets or creating dummy
 * targets. In particular, Linux links may be dangling or form cycles. */
static int __copylink_windows(const wchar_t* source, const wchar_t* destination)
{
    struct { DWORD tag; WORD length; WORD reserved; BYTE data[MAXIMUM_REPARSE_DATA_BUFFER_SIZE]; } reparse;
    struct __backup_privilege_scope privilege;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    DWORD bytes = 0, ignored, error = ERROR_SUCCESS;
    int created = 0, directory = 0;

    input = CreateFileW(source, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (input == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input, &info) ||
        !DeviceIoControl(input, FSCTL_GET_REPARSE_POINT, NULL, 0, &reparse,
            sizeof(reparse), &bytes, NULL)) {
        error = GetLastError();
        goto cleanup;
    }
    if (bytes < 8 || (DWORD)reparse.length + 8 != bytes) {
        error = ERROR_INVALID_REPARSE_DATA;
        goto cleanup;
    }
    
    // IO_REPARSE_TAG_LX_SYMLINK is used by Linux links stored on NTFS.
    if (reparse.tag != IO_REPARSE_TAG_SYMLINK &&
        reparse.tag != IO_REPARSE_TAG_MOUNT_POINT && reparse.tag != 0xA000001DUL) {
        error = ERROR_NOT_SUPPORTED;
        goto cleanup;
    }
    
    directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (directory) {
        if (!CreateDirectoryW(destination, NULL)) {
            error = GetLastError();
            goto cleanup;
        }
        created = 1;
    }
    
    output = CreateFileW(destination, GENERIC_WRITE, 0, NULL,
        directory ? OPEN_EXISTING : CREATE_NEW,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto cleanup;
    }
    created = 1;
    
    __privilege_begin(&privilege, L"SeCreateSymbolicLinkPrivilege");
    if (!DeviceIoControl(output, FSCTL_SET_REPARSE_POINT, &reparse, bytes,
            NULL, 0, &ignored, NULL)) {
        error = GetLastError();
    }
    __backup_privilege_end(&privilege);

cleanup:
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (error != ERROR_SUCCESS) {
        if (created) {
            if (directory) RemoveDirectoryW(destination);
            else DeleteFileW(destination);
        }
        __set_copydir_error("copy link", error);
        return -1;
    }
    return 0;
}

static int __copydir_case_sensitivity(const wchar_t* source, const wchar_t* destination)
{
    struct { ULONG Flags; } source_info, destination_info;
    HANDLE input, output;
    DWORD  error = ERROR_SUCCESS;

    input = CreateFileW(source, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        __set_copydir_error("open directory attributes", GetLastError());
        return -1;
    }

    if (!GetFileInformationByHandleEx(input, FileCaseSensitiveInfo, &source_info, sizeof(source_info))) {
        error = GetLastError();
        CloseHandle(input);
        // Filesystems without this feature use Windows' default name comparison.
        if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED) return 0;
        __set_copydir_error("read directory case sensitivity", error);
        return -1;
    }
    CloseHandle(input);

    output = CreateFileW(destination, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        __set_copydir_error("open destination attributes", GetLastError());
        return -1;
    }

    if (!GetFileInformationByHandleEx(output, FileCaseSensitiveInfo, &destination_info, sizeof(destination_info)) ||
        (source_info.Flags != destination_info.Flags &&
         !SetFileInformationByHandle(output, FileCaseSensitiveInfo, &source_info, sizeof(source_info)))) {
        error = GetLastError();
    }

    CloseHandle(output);
    if (error != ERROR_SUCCESS) {
        __set_copydir_error("preserve directory case sensitivity", error);
        return -1;
    }
    return 0;
}

static int __copydir_windows(const wchar_t* source, const wchar_t* destination)
{
    HANDLE dir_handle;
    int    status = 0;
    BYTE   buffer[64 * 1024];

    if (__mkdir_recursive_wide(destination) != 0) {
        return -1;
    }

    if (__copydir_case_sensitivity(source, destination) != 0) {
        return -1;
    }

    dir_handle = __open_directory_handle(source);
    if (dir_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        __set_copydir_error("open source directory", error);
        return -1;
    }

    while (1) {
        if (!GetFileInformationByHandleEx(dir_handle, FileIdBothDirectoryInfo, buffer, sizeof(buffer))) {
            DWORD error = GetLastError();

            if (error != ERROR_NO_MORE_FILES) {
                __set_copydir_error("enumerate source directory", error);
                status = -1;
            }
            break;
        }

        {
            FILE_ID_BOTH_DIR_INFO* entry = (FILE_ID_BOTH_DIR_INFO*)buffer;

            while (1) {
                DWORD    name_length = entry->FileNameLength / sizeof(WCHAR);
                wchar_t* source_child;
                wchar_t* destination_child;

                if (!__is_dot_entry(entry->FileName, name_length)) {
                    source_child = __join_wide_path(source, entry->FileName, name_length);
                    destination_child = __join_wide_path(destination, entry->FileName, name_length);
                    if (source_child == NULL || destination_child == NULL) {
                        free(source_child);
                        free(destination_child);
                        errno = ENOMEM;
                        status = -1;
                        break;
                    }

                    if (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                        status = __copylink_windows(source_child, destination_child);
                    } else if ((entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        status = __copydir_windows(source_child, destination_child);
                    } else {
                        status = __copyfile_windows(source_child, destination_child);
                    }

                    free(source_child);
                    free(destination_child);
                    if (status != 0) {
                        break;
                    }
                }

                if (entry->NextEntryOffset == 0) {
                    break;
                }
                entry = (FILE_ID_BOTH_DIR_INFO*)((BYTE*)entry + entry->NextEntryOffset);
            }
        }

        if (status != 0) {
            status = -1;
            break;
        }
    }

    CloseHandle(dir_handle);
    return status;
}
#else
static int __copydir_linux(const char* source, const char* destination)
{
    DIR*           directory;
    struct dirent* entry;
    int            status = 0;
    struct platform_stat source_stat;

    if (platform_stat(source, &source_stat) != 0) {
        return -1;
    }
    if (source_stat.type != PLATFORM_FILETYPE_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }

    if (platform_mkdir(destination) != 0) {
        return -1;
    }

    directory = opendir(source);
    if (directory == NULL) {
        return -1;
    }

    for (;;) {
        char* childSource;
        char* childDestination;
        struct platform_stat st;

        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            status = errno == 0 ? 0 : -1;
            break;
        }

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        childSource = strpathcombine(source, entry->d_name);
        childDestination = strpathcombine(destination, entry->d_name);
        if (childSource == NULL || childDestination == NULL) {
            free(childSource);
            free(childDestination);
            errno = ENOMEM;
            status = -1;
            break;
        }

        if (platform_stat(childSource, &st) != 0) {
            free(childSource);
            free(childDestination);
            status = -1;
            break;
        }

        if (st.type == PLATFORM_FILETYPE_DIRECTORY) {
            status = __copydir_linux(childSource, childDestination);
        } else if (st.type == PLATFORM_FILETYPE_FILE) {
            status = platform_copyfile(childSource, childDestination);
            if (status == 0) {
                status = platform_chmod(childDestination, st.permissions);
            }
        } else if (st.type == PLATFORM_FILETYPE_SYMLINK) {
            char* target = NULL;
            status = platform_readlink(childSource, &target);
            if (status == 0) {
                // Do not dereference links or create their targets. In
                // particular, dangling and relative links must stay intact.
                status = symlink(target, childDestination);
            }
            free(target);
        } else {
            errno = ENOTSUP;
            status = -1;
        }

        free(childSource);
        free(childDestination);
        if (status != 0) {
            break;
        }
    }

    {
        int saved_errno = errno;
        closedir(directory);
        errno = saved_errno;
    }
    if (status == 0) {
        status = platform_chmod(destination, source_stat.permissions);
    }
    return status;
}
#endif

int platform_copydir(const char* source, const char* destination)
{
    if (source == NULL || destination == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    struct __backup_privilege_scope privilege_scope;
    wchar_t*                        source_wide;
    wchar_t*                        destination_wide;
    int                             status;

    __clear_copydir_error();
    source_wide = __path_to_normalized_wide(source);
    destination_wide = __path_to_normalized_wide(destination);
    if (source_wide == NULL || destination_wide == NULL) {
        free(source_wide);
        free(destination_wide);
        return -1;
    }

    __privilege_begin(&privilege_scope, L"SeBackupPrivilege");
    status = __copydir_windows(source_wide, destination_wide);
    __backup_privilege_end(&privilege_scope);

    free(source_wide);
    free(destination_wide);
    return status;
#else
    return __copydir_linux(source, destination);
#endif
}

int platform_copydir_case_sensitivity(const char* source, const char* destination)
{
#if defined(_WIN32) || defined(_WIN64)
    wchar_t* from = __path_to_normalized_wide(source);
    wchar_t* to = __path_to_normalized_wide(destination);
    int status = -1;
    if (from && to) status = __copydir_case_sensitivity(from, to);
    free(from);
    free(to);
    return status;
#else
    (void)source;
    (void)destination;
    return 0;
#endif
}

int platform_copylink(const char* source, const char* destination)
{
    int status;

    if (source == NULL || destination == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    wchar_t* source_wide = __path_to_normalized_wide(source);
    wchar_t* destination_wide = __path_to_normalized_wide(destination);
    
    __clear_copydir_error();
    if (source_wide == NULL || destination_wide == NULL) {
        free(source_wide);
        free(destination_wide);
        return -1;
    }
    
    status = __copylink_windows(source_wide, destination_wide);
    free(source_wide);
    free(destination_wide);
#else
    char* target = NULL;
    
    status = platform_readlink(source, &target);
    if (status == 0) {
        status = symlink(target, destination);
    }
    free(target);
#endif
    return status;
}

unsigned long platform_copydir_lasterror(void)
{
#if defined(_WIN32) || defined(_WIN64)
    return (unsigned long)__copydir_last_error;
#else
    return 0;
#endif
}

const char* platform_copydir_lasterror_operation(void)
{
#if defined(_WIN32) || defined(_WIN64)
    return __copydir_last_operation;
#else
    return NULL;
#endif
}
