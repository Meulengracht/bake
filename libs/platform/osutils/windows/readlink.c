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
#include <windows.h>
#include <stdlib.h>
#include <errno.h>

#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE 16384
#endif

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000C
#endif

// Define the reparse data buffer structure if not available
#ifndef REPARSE_DATA_BUFFER_HEADER_SIZE
typedef struct _REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

int platform_readlink(const char* path, char** bufferOut)
{
    HANDLE hFile;
    char* buffer;
    DWORD bytesReturned;
    BOOL success;
    
    if (path == NULL || bufferOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = (char*)calloc(1, MAX_PATH);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // Open the reparse point
    hFile = CreateFileA(
        path,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        free(buffer);
        errno = ENOENT;
        return -1;
    }

    // Allocate buffer for reparse data
    BYTE reparseBuffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    REPARSE_DATA_BUFFER* reparseData = (REPARSE_DATA_BUFFER*)reparseBuffer;

    success = DeviceIoControl(
        hFile,
        FSCTL_GET_REPARSE_POINT,
        NULL,
        0,
        reparseData,
        MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
        &bytesReturned,
        NULL
    );

    CloseHandle(hFile);

    if (!success) {
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    // Check if it's a symbolic link
    if (reparseData->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        WCHAR* targetPath = reparseData->SymbolicLinkReparseBuffer.PathBuffer +
            (reparseData->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(WCHAR));
        int targetLength = reparseData->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);

        // Convert wide string to multibyte
        int result = WideCharToMultiByte(
            CP_UTF8,
            0,
            targetPath,
            targetLength,
            buffer,
            MAX_PATH - 1,
            NULL,
            NULL
        );

        if (result == 0) {
            free(buffer);
            errno = EINVAL;
            return -1;
        }

        buffer[result] = '\0';
        *bufferOut = buffer;
        return 0;
    }

    // Not a symbolic link
    free(buffer);
    errno = EINVAL;
    return -1;
}
