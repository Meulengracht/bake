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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define OUTPUT_BUFFER_SIZE 2048

static void __report(char* line, enum platform_spawn_output_type type, struct platform_spawn_options* options)
{
    const char* s = line;
    char*       p = line;
    char        tmp[OUTPUT_BUFFER_SIZE];

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

    // Avoid blocking when the child has not produced another output chunk.
    if (PeekNamedPipe(pipe, NULL, 0, NULL, &bytesAvail, NULL) == FALSE ||
        bytesAvail == 0) {
        return 0;
    }

    // Read one bounded chunk so __report always receives a terminated string.
    if (ReadFile(pipe, buffer, bufferSize - 1, &bytesRead, NULL) == FALSE) {
        return 0;
    }

    buffer[bytesRead] = '\0';
    return bytesRead;
}

static void __safe_close(HANDLE* handle)
{
    if (handle != NULL && *handle != NULL) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp,
    struct platform_spawn_options* options)
{
    HANDLE hStdoutRead = NULL;
    HANDLE hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL;
    HANDLE hStderrWrite = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi = { 0 };
    char* cmdLine = NULL;
    char* envBlock = NULL;
    int status = -1;
    DWORD exitCode = 0;
    size_t cmdLineLen;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Create pipes for stdout and stderr if output handler is provided.
    if (options != NULL && options->output_handler != NULL) {
        /* The child cannot be monitored unless both output streams have pipes. */
        if (CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) == FALSE ||
            CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0) == FALSE) {
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
    cmdLineLen = strlen(path) + 3; // for quotes and space
    if (arguments != NULL) {
        cmdLineLen += strlen(arguments) + 1;
    }

    cmdLine = (char*)calloc(1, cmdLineLen);
    if (cmdLine == NULL) {
        fprintf(stderr, "platform_spawn: failed to allocate command line\n");
        goto cleanup;
    }

    // Quote the path to handle spaces
    snprintf(cmdLine, cmdLineLen, "\"%s\"", path);
    if (arguments != NULL && arguments[0] != '\0') {
        strcat(cmdLine, " ");
        strcat(cmdLine, arguments);
    }

    // Build environment block if provided
    if (envp != NULL) {
        const char* const* env = envp;
        size_t             totalSize = 0;
        char*              p;

        // Calculate total size needed
        while (*env != NULL) {
            totalSize += strlen(*env) + 1;
            env++;
        }
        totalSize++; // Double null terminator

        envBlock = (char*)calloc(1, totalSize + 1);
        if (envBlock == NULL) {
            goto cleanup;
        }

        p = envBlock;
        env = envp;
        while (*env != NULL) {
            size_t len = strlen(*env);
            memcpy(p, *env, len);
            p += len;
            *p++ = '\0';
            env++;
        }
        *p = '\0'; // Double null terminator
    }

    // Create the process
    if (CreateProcessA(
        NULL,
        cmdLine,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        envBlock,
        options != NULL && options->cwd != NULL ? options->cwd : NULL,
        &si,
        &pi) == FALSE) {
        fprintf(stderr, "platform_spawn: failed to create process: %lu\n", GetLastError());
        goto cleanup;
    }

    // Close write ends of pipes in parent
    if (hStdoutWrite != NULL) {
        CloseHandle(hStdoutWrite);
        hStdoutWrite = NULL;
    }
    if (hStderrWrite != NULL) {
        CloseHandle(hStderrWrite);
        hStderrWrite = NULL;
    }

    // Read output if handler is provided
    if (options != NULL && options->output_handler != NULL) {
        char buffer[OUTPUT_BUFFER_SIZE];
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

        // Drain both streams completely after exit; output may exceed two chunks.
        for (;;) {
            DWORD stdoutBytes;
            DWORD stderrBytes;

            stdoutBytes = __read_from_pipe(hStdoutRead, buffer, sizeof(buffer));
            if (stdoutBytes > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT, options);
            }

            stderrBytes = __read_from_pipe(hStderrRead, buffer, sizeof(buffer));
            if (stderrBytes > 0) {
                __report(buffer, PLATFORM_SPAWN_OUTPUT_TYPE_STDERR, options);
            }

            // Both empty reads mean the child has no more buffered output
            if (stdoutBytes == 0 && stderrBytes == 0) {
                break;
            }
        }
    } else {
        // Just wait for process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    // Get exit code and return it at the actual result
    if (GetExitCodeProcess(pi.hProcess, &exitCode) != FALSE) {
        status = (int)exitCode;
    }

cleanup:
    __safe_close(&hStdoutRead);
    __safe_close(&hStdoutWrite);
    __safe_close(&hStderrRead);
    __safe_close(&hStderrWrite);
    __safe_close(&pi.hProcess);
    __safe_close(&pi.hThread);
    free(cmdLine);
    free(envBlock);
    return status;
}

int platform_spawn_argv(const char* path, const char* const* arguments,
    const char* const* envp, struct platform_spawn_options* options)
{
    char* command;
    int   status;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    command = strargv_windows(arguments);
    if (command == NULL) {
        return -1;
    }

    status = platform_spawn(path, command, envp, options);
    free(command);
    return status;
}
