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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __PID1_WINDOWS_H__
#define __PID1_WINDOWS_H__

#include "../shared/pid1_common.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/**
 * @brief Windows-specific PID 1 initialization
 * 
 * Sets up console control handlers for CTRL+C and CTRL+BREAK.
 * Initializes the process tracking list and synchronization primitives.
 * Creates a Job Object for managing all child processes.
 * 
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_init(void);

/**
 * @brief Windows-specific process spawning
 * 
 * Uses CreateProcess() to spawn a new process within the container.
 * The process is added to a Job Object for resource management and
 * automatic cleanup.
 * 
 * @param options Process configuration
 * @param handle_out Output parameter to receive process handle
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_spawn(const pid1_process_options_t* options, HANDLE* handle_out);

/**
 * @brief Windows-specific process wait
 * 
 * Waits for the specified process to exit using WaitForSingleObject().
 * 
 * @param handle Process handle to wait for
 * @param exit_code_out Output parameter to receive exit code
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_wait(HANDLE handle, int* exit_code_out);

/**
 * @brief Windows-specific process termination
 * 
 * Terminates the specified process using TerminateProcess().
 * First attempts graceful shutdown via console control event.
 * 
 * @param handle Process handle to kill
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_kill(HANDLE handle);

/**
 * @brief Windows-specific cleanup
 * 
 * Terminates all remaining child processes and cleans up resources.
 * Closes the Job Object, which will terminate all processes in it.
 * 
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_cleanup(void);

/**
 * @brief Get the number of active child processes
 * 
 * @return Number of tracked child processes, or -1 on error
 */
extern int pid1_windows_get_process_count(void);

/**
 * @brief Set the Job Object to use for process management
 * 
 * This allows external code to provide a pre-configured Job Object
 * that should be used for all spawned processes.
 * 
 * @param job_handle Job Object handle (will be duplicated internally)
 * @return 0 on success, -1 on failure
 */
extern int pid1_windows_set_job_object(HANDLE job_handle);

/**
 * @brief Get the Job Object used for process management
 * 
 * @return Job Object handle, or NULL if not initialized
 */
extern HANDLE pid1_windows_get_job_object(void);

#endif //!__PID1_WINDOWS_H__
