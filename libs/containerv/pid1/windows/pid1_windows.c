/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "pid1_windows.h"
#include "../shared/logging.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple linked list for tracking child processes
typedef struct pid1_process_node {
    HANDLE handle;
    DWORD  pid;
    struct pid1_process_node* next;
} pid1_process_node_t;

static pid1_process_node_t* g_process_list = NULL;
static int                  g_process_count = 0;
static CRITICAL_SECTION     g_process_lock;
static HANDLE               g_job_object = NULL;
static int                  g_job_object_owned = 0;
static volatile BOOL        g_shutdown_requested = FALSE;

// External functions from pid1_common.c
extern int pid1_common_init(void);
extern int pid1_common_cleanup(void);
extern int pid1_validate_spawn(const pid1_process_options_t* options);
extern int pid1_is_initialized(void);

/**
 * @brief Console control handler for CTRL+C, CTRL+BREAK, etc.
 */
static BOOL WINAPI __console_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            PID1_INFO("Shutdown requested (event: %lu)", ctrl_type);
            g_shutdown_requested = TRUE;
            return TRUE;
        default:
            return FALSE;
    }
}

/**
 * @brief Add a process to the tracking list
 */
static int __add_process(HANDLE handle, DWORD pid)
{
    pid1_process_node_t* node;

    EnterCriticalSection(&g_process_lock);

    node = malloc(sizeof(pid1_process_node_t));
    if (node == NULL) {
        LeaveCriticalSection(&g_process_lock);
        return -1;
    }

    node->handle = handle;
    node->pid = pid;
    node->next = g_process_list;
    g_process_list = node;
    g_process_count++;

    PID1_DEBUG("Added process %lu (handle %p) to tracking list (total: %d)",
               pid, handle, g_process_count);

    LeaveCriticalSection(&g_process_lock);
    return 0;
}

/**
 * @brief Remove a process from the tracking list
 */
static void __remove_process(HANDLE handle)
{
    pid1_process_node_t** current;

    EnterCriticalSection(&g_process_lock);

    current = &g_process_list;
    while (*current != NULL) {
        if ((*current)->handle == handle) {
            pid1_process_node_t* to_free = *current;
            DWORD pid = (*current)->pid;
            *current = (*current)->next;
            free(to_free);
            g_process_count--;
            PID1_DEBUG("Removed process %lu (handle %p) from tracking list (total: %d)",
                       pid, handle, g_process_count);
            LeaveCriticalSection(&g_process_lock);
            return;
        }
        current = &(*current)->next;
    }

    LeaveCriticalSection(&g_process_lock);
}

/**
 * @brief Build a command line from arguments
 */
static wchar_t* __build_command_line(const pid1_process_options_t* options)
{
    size_t total_wchars = 0;
    size_t i;
    wchar_t* cmd_line;
    wchar_t* pos;

    // Calculate total length needed in wide characters
    for (i = 0; options->args[i] != NULL; i++) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, options->args[i], -1, NULL, 0);
        if (needed <= 0) {
            return NULL;
        }
        // Add space for quotes (if needed), space separator, and the converted string
        total_wchars += needed + 3; // worst case: " arg" or "arg "
    }
    total_wchars++; // Null terminator

    cmd_line = calloc(total_wchars, sizeof(wchar_t));
    if (cmd_line == NULL) {
        return NULL;
    }

    pos = cmd_line;
    for (i = 0; options->args[i] != NULL; i++) {
        int needed;
        int converted;

        if (i > 0) {
            *pos++ = L' ';
        }

        // Check if argument needs quoting (contains spaces or special chars)
        if (strchr(options->args[i], ' ') != NULL) {
            *pos++ = L'"';
        }

        // Convert to wide string
        needed = MultiByteToWideChar(CP_UTF8, 0, options->args[i], -1, NULL, 0);
        if (needed <= 0) {
            free(cmd_line);
            return NULL;
        }

        converted = MultiByteToWideChar(CP_UTF8, 0, options->args[i], -1, pos, needed);
        if (converted <= 0) {
            free(cmd_line);
            return NULL;
        }

        pos += converted - 1; // -1 to exclude null terminator

        if (strchr(options->args[i], ' ') != NULL) {
            *pos++ = L'"';
        }
    }

    *pos = L'\0';
    return cmd_line;
}

/**
 * @brief Build an environment block from environment variables
 */
static wchar_t* __build_environment_block(const pid1_process_options_t* options)
{
    size_t total_length;
    size_t i;
    wchar_t* env_block;
    wchar_t* pos;

    if (options->environment == NULL) {
        return NULL;
    }

    // Calculate total length needed
    total_length = 1; // Final null terminator
    for (i = 0; options->environment[i] != NULL; i++) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, options->environment[i], -1, NULL, 0);
        if (needed <= 0) {
            return NULL;
        }
        total_length += needed; // Includes null terminator for this string
    }

    env_block = calloc(total_length, sizeof(wchar_t));
    if (env_block == NULL) {
        return NULL;
    }

    pos = env_block;
    for (i = 0; options->environment[i] != NULL; i++) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, options->environment[i], -1, NULL, 0);
        int converted = MultiByteToWideChar(CP_UTF8, 0, options->environment[i], -1, pos, needed);
        if (converted <= 0) {
            free(env_block);
            return NULL;
        }
        pos += converted; // Move past the null terminator
    }

    *pos = L'\0'; // Final terminator
    return env_block;
}

int pid1_windows_init(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info;

    // Initialize common components
    if (pid1_common_init() != 0) {
        return -1;
    }

    // Initialize critical section for process list
    InitializeCriticalSection(&g_process_lock);

    // Set up console control handler
    if (!SetConsoleCtrlHandler(__console_handler, TRUE)) {
        PID1_ERROR("Failed to set console control handler: error %lu", GetLastError());
        DeleteCriticalSection(&g_process_lock);
        return -1;
    }

    // Create Job Object for managing all child processes
    g_job_object = CreateJobObjectW(NULL, NULL);
    if (g_job_object == NULL) {
        PID1_ERROR("Failed to create Job Object: error %lu", GetLastError());
        SetConsoleCtrlHandler(__console_handler, FALSE);
        DeleteCriticalSection(&g_process_lock);
        return -1;
    }
    g_job_object_owned = 1;

    // Configure Job Object to terminate all processes when handle is closed
    memset(&job_info, 0, sizeof(job_info));
    job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!SetInformationJobObject(g_job_object, JobObjectExtendedLimitInformation,
                                  &job_info, sizeof(job_info))) {
        PID1_ERROR("Failed to configure Job Object: error %lu", GetLastError());
        CloseHandle(g_job_object);
        g_job_object = NULL;
        SetConsoleCtrlHandler(__console_handler, FALSE);
        DeleteCriticalSection(&g_process_lock);
        return -1;
    }

    PID1_INFO("Windows PID 1 service initialized (Job Object: %p)", g_job_object);
    return 0;
}

int pid1_windows_spawn(const pid1_process_options_t* options, HANDLE* handle_out)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t* cmd_line = NULL;
    wchar_t* env_block = NULL;
    wchar_t* working_dir = NULL;
    int result = -1;

    // Initialize structures to zero
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));

    // Validate options using common validation
    if (pid1_validate_spawn(options) != 0) {
        return -1;
    }

    // Build command line
    cmd_line = __build_command_line(options);
    if (cmd_line == NULL) {
        PID1_ERROR("Failed to build command line");
        errno = ENOMEM;
        return -1;
    }

    // Build environment block
    env_block = __build_environment_block(options);

    // Convert working directory to wide string
    if (options->working_directory != NULL) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, options->working_directory, -1, NULL, 0);
        if (needed > 0) {
            working_dir = calloc(needed, sizeof(wchar_t));
            if (working_dir != NULL) {
                int converted = MultiByteToWideChar(CP_UTF8, 0, options->working_directory, -1, working_dir, needed);
                if (converted <= 0) {
                    PID1_ERROR("Failed to convert working directory to wide string");
                    free(working_dir);
                    working_dir = NULL;
                }
            }
        }
    }

    // Set up STARTUPINFO
    si.cb = sizeof(si);

    // Create the process
    if (!CreateProcessW(
            NULL,           // Application name (use command line)
            cmd_line,       // Command line
            NULL,           // Process security attributes
            NULL,           // Thread security attributes
            FALSE,          // Inherit handles
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, // Creation flags
            env_block,      // Environment block
            working_dir,    // Working directory
            &si,            // Startup info
            &pi)) {         // Process information
        PID1_ERROR("CreateProcessW() failed: error %lu", GetLastError());
        errno = EINVAL;
        goto cleanup;
    }

    PID1_INFO("Spawned process %lu (handle %p): %s", pi.dwProcessId, pi.hProcess, options->command);

    // Add process to Job Object
    if (g_job_object != NULL) {
        if (!AssignProcessToJobObject(g_job_object, pi.hProcess)) {
            PID1_ERROR("Failed to assign process to Job Object: error %lu", GetLastError());
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            goto cleanup;
        }
    }

    // Resume the process
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // Add to tracking list
    if (__add_process(pi.hProcess, pi.dwProcessId) != 0) {
        PID1_ERROR("Failed to add process to tracking list");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        errno = ENOMEM;
        goto cleanup;
    }

    if (handle_out != NULL) {
        *handle_out = pi.hProcess;
    }

    result = 0;

cleanup:
    if (cmd_line != NULL) {
        free(cmd_line);
    }
    if (env_block != NULL) {
        free(env_block);
    }
    if (working_dir != NULL) {
        free(working_dir);
    }

    return result;
}

int pid1_windows_wait(HANDLE handle, int* exit_code_out)
{
    DWORD exit_code;
    DWORD result;

    if (!pid1_is_initialized() || handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    PID1_DEBUG("Waiting for process (handle %p)", handle);

    result = WaitForSingleObject(handle, INFINITE);
    if (result != WAIT_OBJECT_0) {
        PID1_ERROR("WaitForSingleObject() failed: error %lu", GetLastError());
        errno = EINVAL;
        return -1;
    }

    // Get exit code
    if (!GetExitCodeProcess(handle, &exit_code)) {
        PID1_ERROR("GetExitCodeProcess() failed: error %lu", GetLastError());
        errno = EINVAL;
        return -1;
    }

    // Remove from tracking list
    __remove_process(handle);

    if (exit_code_out != NULL) {
        *exit_code_out = (int)exit_code;
    }

    PID1_INFO("Process (handle %p) exited with code %lu", handle, exit_code);

    return 0;
}

int pid1_windows_kill(HANDLE handle)
{
    if (!pid1_is_initialized() || handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    PID1_INFO("Killing process (handle %p)", handle);

    // Try to terminate gracefully first with exit code 1
    if (!TerminateProcess(handle, 1)) {
        PID1_ERROR("TerminateProcess() failed: error %lu", GetLastError());
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int pid1_windows_cleanup(void)
{
    pid1_process_node_t* current;
    int remaining;

    if (!pid1_is_initialized()) {
        return 0;
    }

    PID1_INFO("Cleaning up Windows PID 1 service");

    EnterCriticalSection(&g_process_lock);

    // Terminate all remaining processes
    current = g_process_list;
    remaining = 0;
    while (current != NULL) {
        PID1_INFO("Terminating process %lu (handle %p)", current->pid, current->handle);
        TerminateProcess(current->handle, 1);
        CloseHandle(current->handle);
        remaining++;
        current = current->next;
    }

    // Free the process list
    while (g_process_list != NULL) {
        pid1_process_node_t* next = g_process_list->next;
        free(g_process_list);
        g_process_list = next;
    }
    g_process_count = 0;

    LeaveCriticalSection(&g_process_lock);

    // Close Job Object (this will terminate any processes that were missed)
    if (g_job_object != NULL && g_job_object_owned) {
        PID1_INFO("Closing Job Object (handle %p)", g_job_object);
        CloseHandle(g_job_object);
        g_job_object = NULL;
        g_job_object_owned = 0;
    }

    // Remove console handler
    SetConsoleCtrlHandler(__console_handler, FALSE);

    DeleteCriticalSection(&g_process_lock);
    pid1_common_cleanup();

    return 0;
}

int pid1_windows_get_process_count(void)
{
    int count;

    EnterCriticalSection(&g_process_lock);
    count = g_process_count;
    LeaveCriticalSection(&g_process_lock);

    return count;
}

int pid1_windows_set_job_object(HANDLE job_handle)
{
    HANDLE dup_handle;

    if (job_handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Duplicate the handle so we have our own copy
    if (!DuplicateHandle(GetCurrentProcess(), job_handle,
                         GetCurrentProcess(), &dup_handle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        PID1_ERROR("Failed to duplicate Job Object handle: error %lu", GetLastError());
        errno = EINVAL;
        return -1;
    }

    // Close existing Job Object if we own it
    if (g_job_object != NULL && g_job_object_owned) {
        CloseHandle(g_job_object);
    }

    g_job_object = dup_handle;
    g_job_object_owned = 1;

    PID1_INFO("Using provided Job Object (handle %p)", g_job_object);
    return 0;
}

HANDLE pid1_windows_get_job_object(void)
{
    return g_job_object;
}

// Implement the common interface functions by delegating to Windows-specific implementations

int pid1_init(void)
{
    return pid1_windows_init();
}

int pid1_spawn_process(const pid1_process_options_t* options, pid1_process_handle_t* handle_out)
{
    return pid1_windows_spawn(options, handle_out);
}

int pid1_wait_process(pid1_process_handle_t handle, int* exit_code_out)
{
    return pid1_windows_wait(handle, exit_code_out);
}

int pid1_kill_process(pid1_process_handle_t handle)
{
    return pid1_windows_kill(handle);
}

int pid1_cleanup(void)
{
    return pid1_windows_cleanup();
}

int pid1_reap_zombies(void)
{
    // No-op on Windows - processes are automatically cleaned up
    return 0;
}

int pid1_get_process_count(void)
{
    return pid1_windows_get_process_count();
}
