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

#include <errno.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

int platform_script(const char *script) 
{
    HANDLE hTempFile;
    char tmpPath[MAX_PATH];
    char tmpFileName[MAX_PATH];
    FILE *sfile;
    int status;

    if (!GetTempPathA(MAX_PATH, tmpPath)) {
        fprintf(stderr, "platform_script: GetTempPathA failed: %lu\n", GetLastError());
        return -1;
    }

    if (!GetTempFileNameA(tmpPath, "script_", 0, tmpFileName)) {
        fprintf(stderr, "platform_script: GetTempFileNameA failed: %lu\n", GetLastError());
        return -1;
    }

    hTempFile = CreateFileA(
        tmpFileName,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hTempFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "platform_script: CreateFileA failed: %lu\n", GetLastError());
        return -1;
    }

    int fd = _open_osfhandle((intptr_t)hTempFile, _O_APPEND);
    if (fd == -1) {
        fprintf(stderr, "platform_script: _open_osfhandle failed: %s\n", strerror(errno));
        CloseHandle(hTempFile);
        return -1;
    }

    sfile = _fdopen(fd, "w");
    if (sfile == NULL) {
        fprintf(stderr, "platform_script: _fdopen failed for path %s: %s\n", tmpFileName, strerror(errno));
        CloseHandle(hTempFile);
        return -1;
    }

    fprintf(sfile, "@echo off\r\n");
    fputs(script, sfile);
    fclose(sfile);

    char cmdBuffer[MAX_PATH + 50];
    // execute and unlink
    snprintf(&cmdBuffer[0], sizeof(cmdBuffer), "powershell -ExecutionPolicy Bypass -File %s", tmpPath);
    status = system(&cmdBuffer[0]);
    _unlink(&tmpPath[0]);
    if (status) {
        fprintf(stderr, "platform_script: exec %s failed: %s\n", &tmpPath[0], strerror(errno));
    }
    return status;
}
