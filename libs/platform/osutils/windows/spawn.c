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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void __report(char* line, enum platform_spawn_output_type type, struct platform_spawn_options* options)
{
    const char* s = line;
    char*       p = line;
    char        tmp[2048];

    while (*p) {
        if (*p == '\n') {
            // include the \n
            size_t count = (size_t)(p - s) + 1;
            strncpy(&tmp[0], s, count);

            // zero terminate the string and report
            tmp[count] = '\0';
            options->output_handler(&tmp[0], type);

            // update new start
            s = ++p;
        } else {
            p++;
        }
    }
    
    // only do a final report if the line didn't end with a newline
    if (s != p) {
        options->output_handler(s, type);
    }
}

static DWORD __read_from_pipe(HANDLE pipe, char* buffer, DWORD bufferSize)
{
    DWORD bytesRead = 0;
    DWORD bytesAvail = 0;

    if (!PeekNamedPipe(pipe, NULL, 0, NULL, &bytesAvail, NULL) || bytesAvail == 0) {
        return 0;
    }

    if (!ReadFile(pipe, buffer, bufferSize - 1, &bytesRead, NULL)) {
        return 0;
    }

    buffer[bytesRead] = '\0';
    return bytesRead;
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp, struct platform_spawn_options* options)
{
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES)};
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION pi = {0};
    char* cmdLine = NULL;
    char* envBlock = NULL;
    int status = -1;
    DWORD exitCode = 0;

    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Create pipes for stdout and stderr if output handler is provided
    if (options && options->output_handler) {
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
            !CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
            fprintf(stderr, "platform_spawn: failed to create pipes\n");
            goto cleanup;
        }

        // Ensure the read handles are not inherited
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStderrWrite;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.wShowWindow = SW_HIDE;
    } else {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }

    // Build command line: "path" arguments
    size_t cmdLineLen = strlen(path) + 3; // for quotes and space
    if (arguments) {
        cmdLineLen += strlen(arguments) + 1;
    }
    
    cmdLine = (char*)calloc(1, cmdLineLen);
    if (!cmdLine) {
        fprintf(stderr, "platform_spawn: failed to allocate command line\n");
        goto cleanup;
    }

    // Quote the path to handle spaces
    snprintf(cmdLine, cmdLineLen, "\"%s\"", path);
    if (arguments && arguments[0] != '\0') {
        strcat(cmdLine, " ");
        strcat(cmdLine, arguments);
    }

    // Build environment block if provided
    if (envp) {
        size_t totalSize = 0;
        const char* const* env = envp;
        
        // Calculate total size needed
        while (*env) {
            totalSize += strlen(*env) + 1;
            env++;
        }
        totalSize++; // Double null terminator
        
        envBlock = (char*)calloc(1, totalSize);
        if (envBlock) {
            char* p = envBlock;
            env = envp;
            while (*env) {
                size_t len = strlen(*env);
                memcpy(p, *env, len);
                p += len;
                *p++ = '\0';
                env++;
            }
            *p = '\0'; // Double null terminator
        }
    }

    // Create the process
    if (!CreateProcessA(
        NULL,
        cmdLine,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        envBlock,
        options && options->cwd ? options->cwd : NULL,
        &si,
        &pi)) {
        fprintf(stderr, "platform_spawn: failed to create process: %lu\n", GetLastError());
        goto cleanup;
    }

    // Close write ends of pipes in parent
    if (hStdoutWrite) {
        CloseHandle(hStdoutWrite);
        hStdoutWrite = NULL;
    }
    if (hStderrWrite) {
        CloseHandle(hStderrWrite);
        hStderrWrite = NULL;
    }

    // Read output if handler is provided
    if (options && options->output_handler) {
        char buffer[2048];
        BOOL processRunning = TRUE;

        while (processRunning) {
            DWORD bytesRead;

            // Check if process is still running
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                processRunning = FALSE;
            }

            // Read stdout
            bytesRead = __read_from_pipe(hStdoutRead, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT, options);
            }

            // Read stderr
            bytesRead = __read_from_pipe(hStderrRead, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDERR, options);
            }
        }

        // Final read to get any remaining output
        for (int i = 0; i < 2; i++) {
            DWORD bytesRead;
            
            bytesRead = __read_from_pipe(hStdoutRead, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT, options);
            }

            bytesRead = __read_from_pipe(hStderrRead, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDERR, options);
            }
        }
    } else {
        // Just wait for process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    // Get exit code
    if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
        status = (int)exitCode;
    }

cleanup:
    if (hStdoutRead) CloseHandle(hStdoutRead);
    if (hStdoutWrite) CloseHandle(hStdoutWrite);
    if (hStderrRead) CloseHandle(hStderrRead);
    if (hStderrWrite) CloseHandle(hStderrWrite);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    free(cmdLine);
    free(envBlock);
    
    return status;
}
