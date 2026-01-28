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

#ifndef __PID1_LOGGING_H__
#define __PID1_LOGGING_H__

#include <stdarg.h>

/**
 * @brief Log levels for PID 1 service
 */
typedef enum pid1_log_level {
    PID1_LOG_DEBUG = 0,
    PID1_LOG_INFO,
    PID1_LOG_WARNING,
    PID1_LOG_ERROR,
    PID1_LOG_FATAL
} pid1_log_level_t;

/**
 * @brief Initialize logging subsystem
 * 
 * @param log_path Path to log file (NULL for stderr)
 * @param level Minimum log level to record
 * @return 0 on success, -1 on failure
 */
extern int pid1_log_init(const char* log_path, pid1_log_level_t level);

/**
 * @brief Log a formatted message
 * 
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Format arguments
 */
extern void pid1_log(pid1_log_level_t level, const char* format, ...);

/**
 * @brief Log a formatted message with va_list
 * 
 * @param level Log level
 * @param format Printf-style format string
 * @param args Variable argument list
 */
extern void pid1_logv(pid1_log_level_t level, const char* format, va_list args);

/**
 * @brief Close logging subsystem
 */
extern void pid1_log_close(void);

// Convenience macros for logging
#define PID1_DEBUG(fmt, ...) pid1_log(PID1_LOG_DEBUG, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define PID1_INFO(fmt, ...)  pid1_log(PID1_LOG_INFO,  "[INFO]  " fmt "\n", ##__VA_ARGS__)
#define PID1_WARN(fmt, ...)  pid1_log(PID1_LOG_WARNING, "[WARN]  " fmt "\n", ##__VA_ARGS__)
#define PID1_ERROR(fmt, ...) pid1_log(PID1_LOG_ERROR, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define PID1_FATAL(fmt, ...) pid1_log(PID1_LOG_FATAL, "[FATAL] " fmt "\n", ##__VA_ARGS__)

#endif //!__PID1_LOGGING_H__
