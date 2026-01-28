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

#include "pid1_common.h"
#include "logging.h"
#include <errno.h>
#include <string.h>

// Platform-specific initialization is handled by platform-specific modules
// This file provides validation and common utilities

static int g_pid1_initialized = 0;

/**
 * @brief Validate process options
 * 
 * Ensures that the process options structure contains valid data
 * before attempting to spawn a process.
 * 
 * @param options Process options to validate
 * @return 0 if valid, -1 if invalid (errno will be set)
 */
static int __validate_process_options(const pid1_process_options_t* options)
{
    if (options == NULL) {
        errno = EINVAL;
        PID1_ERROR("Process options cannot be NULL");
        return -1;
    }

    if (options->command == NULL || options->command[0] == '\0') {
        errno = EINVAL;
        PID1_ERROR("Process command cannot be NULL or empty");
        return -1;
    }

    // Args array must at least contain the command itself
    if (options->args == NULL || options->args[0] == NULL) {
        errno = EINVAL;
        PID1_ERROR("Process args must include at least argv[0]");
        return -1;
    }

    // CPU percent must be 0-100
    if (options->cpu_percent > 100) {
        errno = EINVAL;
        PID1_ERROR("CPU percent must be between 0 and 100, got %u", options->cpu_percent);
        return -1;
    }

    return 0;
}

/**
 * @brief Common initialization that is platform-independent
 * 
 * This is called by the platform-specific pid1_init() implementations
 * after they have completed their platform-specific setup.
 * 
 * @return 0 on success, -1 on failure
 */
int pid1_common_init(void)
{
    if (g_pid1_initialized) {
        PID1_WARN("PID 1 service already initialized");
        return 0;
    }

    PID1_INFO("Initializing PID 1 service");
    g_pid1_initialized = 1;
    return 0;
}

/**
 * @brief Common cleanup that is platform-independent
 * 
 * This is called by the platform-specific pid1_cleanup() implementations
 * before they complete their platform-specific cleanup.
 * 
 * @return 0 on success, -1 on failure
 */
int pid1_common_cleanup(void)
{
    if (!g_pid1_initialized) {
        PID1_WARN("PID 1 service not initialized");
        return 0;
    }

    PID1_INFO("Cleaning up PID 1 service");
    g_pid1_initialized = 0;
    return 0;
}

/**
 * @brief Check if PID 1 service is initialized
 * 
 * @return 1 if initialized, 0 if not
 */
int pid1_is_initialized(void)
{
    return g_pid1_initialized;
}

/**
 * @brief Validate and log process spawn attempt
 * 
 * This is called by platform-specific implementations before spawning
 * to validate options and log the spawn attempt.
 * 
 * @param options Process options to validate
 * @return 0 if valid, -1 if invalid
 */
int pid1_validate_spawn(const pid1_process_options_t* options)
{
    if (!g_pid1_initialized) {
        errno = EINVAL;
        PID1_ERROR("PID 1 service not initialized");
        return -1;
    }

    if (__validate_process_options(options) != 0) {
        return -1;
    }

    PID1_DEBUG("Spawning process: %s", options->command);
    
    // Log arguments
    if (options->args != NULL) {
        for (int i = 0; options->args[i] != NULL; i++) {
            PID1_DEBUG("  arg[%d]: %s", i, options->args[i]);
        }
    }

    // Log resource limits if specified
    if (options->memory_limit_bytes > 0) {
        PID1_DEBUG("  Memory limit: %llu bytes", (unsigned long long)options->memory_limit_bytes);
    }
    if (options->cpu_percent > 0) {
        PID1_DEBUG("  CPU limit: %u%%", options->cpu_percent);
    }
    if (options->process_limit > 0) {
        PID1_DEBUG("  Process limit: %u", options->process_limit);
    }

    return 0;
}
