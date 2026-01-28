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

#ifndef __PID1_LINUX_H__
#define __PID1_LINUX_H__

#include "../shared/pid1_common.h"
#include <sys/types.h>

/**
 * @brief Linux-specific PID 1 initialization
 * 
 * Sets up signal handlers for SIGCHLD, SIGTERM, etc.
 * Initializes the process tracking list.
 * 
 * @return 0 on success, -1 on failure
 */
extern int pid1_linux_init(void);

/**
 * @brief Linux-specific process spawning
 * 
 * Uses fork() + execve() to create a new process within the container's
 * namespaces. The process is tracked and will be reaped automatically
 * when it exits.
 * 
 * @param options Process configuration
 * @param pid_out Output parameter to receive child PID
 * @return 0 on success, -1 on failure
 */
extern int pid1_linux_spawn(const pid1_process_options_t* options, pid_t* pid_out);

/**
 * @brief Linux-specific process wait
 * 
 * Waits for the specified process to exit using waitpid().
 * 
 * @param pid Process ID to wait for
 * @param exit_code_out Output parameter to receive exit code
 * @return 0 on success, -1 on failure
 */
extern int pid1_linux_wait(pid_t pid, int* exit_code_out);

/**
 * @brief Linux-specific process termination
 * 
 * Sends SIGTERM to the specified process.
 * 
 * @param pid Process ID to kill
 * @return 0 on success, -1 on failure
 */
extern int pid1_linux_kill(pid_t pid);

/**
 * @brief Linux-specific cleanup
 * 
 * Terminates all remaining child processes and cleans up resources.
 * 
 * @return 0 on success, -1 on failure
 */
extern int pid1_linux_cleanup(void);

/**
 * @brief Reap all zombie processes
 * 
 * Called periodically or in response to SIGCHLD to collect status
 * from terminated child processes.
 * 
 * @return Number of processes reaped, or -1 on error
 */
extern int pid1_linux_reap_zombies(void);

/**
 * @brief Get the number of active child processes
 * 
 * @return Number of tracked child processes, or -1 on error
 */
extern int pid1_linux_get_process_count(void);

#endif //!__PID1_LINUX_H__
