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

#ifndef __PID1_COMMON_H__
#define __PID1_COMMON_H__

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Process options structure for PID 1 service
 * 
 * Platform-independent structure that contains all information needed
 * to spawn and manage a process within a container.
 */
typedef struct pid1_process_options {
    const char*        command;           // Path to executable
    const char* const* args;              // Null-terminated argument array (including argv[0])
    const char* const* environment;       // Null-terminated environment variable array
    const char*        working_directory; // Working directory (NULL for default)
    const char*        log_path;          // Path for logging output (NULL for no logging)
    
    // Resource limits (platform-specific interpretation)
    uint64_t           memory_limit_bytes; // Memory limit (0 for no limit)
    uint32_t           cpu_percent;        // CPU percentage (0-100, 0 for no limit)
    uint32_t           process_limit;      // Max child processes (0 for no limit)
    
    // User/Group (platform-specific)
    uint32_t           uid;                // User ID (Unix) or SID (Windows)
    uint32_t           gid;                // Group ID (Unix) or unused (Windows)
    
    // Flags
    int                wait_for_exit;      // Block until process exits
    int                forward_signals;    // Forward signals to child process
} pid1_process_options_t;

/**
 * @brief Process handle type (platform-specific)
 * 
 * On Linux: pid_t
 * On Windows: HANDLE
 */
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE pid1_process_handle_t;
#else
#include <sys/types.h>
typedef pid_t pid1_process_handle_t;
#endif

/**
 * @brief Initialize the PID 1 service
 * 
 * This should be called once at container startup before any processes
 * are spawned. It sets up signal handlers, logging, and other required
 * infrastructure.
 * 
 * @return 0 on success, -1 on failure (errno will be set)
 */
extern int pid1_init(void);

/**
 * @brief Spawn a new process with the given options
 * 
 * Creates and starts a new process within the container. The process
 * will be monitored and managed by the PID 1 service.
 * 
 * @param options Process configuration options
 * @param handle_out Output parameter to receive process handle
 * @return 0 on success, -1 on failure (errno will be set)
 */
extern int pid1_spawn_process(const pid1_process_options_t* options, pid1_process_handle_t* handle_out);

/**
 * @brief Wait for a process to exit
 * 
 * Blocks until the specified process terminates and retrieves its exit code.
 * 
 * @param handle Process handle returned from pid1_spawn_process
 * @param exit_code_out Output parameter to receive exit code (may be NULL)
 * @return 0 on success, -1 on failure (errno will be set)
 */
extern int pid1_wait_process(pid1_process_handle_t handle, int* exit_code_out);

/**
 * @brief Send a termination signal to a process
 * 
 * Attempts to gracefully terminate the process. On Linux, sends SIGTERM.
 * On Windows, sends a console control event or terminates the process.
 * 
 * @param handle Process handle returned from pid1_spawn_process
 * @return 0 on success, -1 on failure (errno will be set)
 */
extern int pid1_kill_process(pid1_process_handle_t handle);

/**
 * @brief Cleanup and shutdown the PID 1 service
 * 
 * This should be called when the container is shutting down. It terminates
 * all remaining child processes, cleans up resources, and prepares for exit.
 * 
 * @return 0 on success, -1 on failure (errno will be set)
 */
extern int pid1_cleanup(void);

/**
 * @brief Reap zombie processes (Unix-specific, no-op on Windows)
 * 
 * Collects status information from terminated child processes to prevent
 * zombie processes from accumulating.
 * 
 * @return Number of processes reaped, or -1 on error
 */
extern int pid1_reap_zombies(void);

/**
 * @brief Get the number of active child processes
 * 
 * @return Number of processes currently being managed, or -1 on error
 */
extern int pid1_get_process_count(void);

#endif //!__PID1_COMMON_H__
