/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __VLOG_H__
#define __VLOG_H__

#include <stdio.h>

enum vlog_level {
    VLOG_LEVEL_DISABLED,
    VLOG_LEVEL_ERROR,
    VLOG_LEVEL_WARNING,
    VLOG_LEVEL_TRACE,
    VLOG_LEVEL_DEBUG
};

#define VLOG_FATAL(tag, ...)   vlog_output(VLOG_LEVEL_ERROR, tag, __VA_ARGS__); exit(EXIT_FAILURE)
#define VLOG_ERROR(tag, ...)   vlog_output(VLOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define VLOG_WARNING(tag, ...) vlog_output(VLOG_LEVEL_WARNING, tag, __VA_ARGS__)
#define VLOG_TRACE(tag, ...)   vlog_output(VLOG_LEVEL_TRACE, tag, __VA_ARGS__)
#define VLOG_DEBUG(tag, ...)   vlog_output(VLOG_LEVEL_DEBUG, tag, __VA_ARGS__)

#define VLOG_OUTPUT_OPTION_CLOSE   0x1
#define VLOG_OUTPUT_OPTION_NODECO  0x4

/**
 * @brief Sets the current logging format. Short (default) is better suited for
 * terminal output where the long form can be too verbose (i.e line length).
 * Long is better for more permanent logging, or where something is not directly
 * invoked by a user.
 */
#define VLOG_OUTPUT_OPTION_LONGDECO 0x8

/**
 * @brief Initializes vlog system. This should be invoked before any calls done to
 * to the vlog_* namespace.
 *
 */
extern void vlog_initialize(enum vlog_level level);

/**
 * @brief Frees up any resources allocated by vlog, and closes all FILE* handles that were
 * passed to vlog through _add_output which were marked as 'free'.
 *
 */
extern void vlog_cleanup(void);

/**
 * @brief Sets the current logging level for all active outputs, and the default
 * level for any added outputs after this call.
 *
 * @param level The log level that should be applied
 */
extern void vlog_set_level(enum vlog_level level);

/**
 * @brief
 *
 * @param output
 * @return
 */
extern int vlog_add_output(FILE* output, int close);

/**
 * @brief 
 */
extern void vlog_set_output_options(FILE* output, unsigned int flags);
extern void vlog_clear_output_options(FILE* output, unsigned int flags);

/**
 * @brief Sets the current logging level for the specified output.
 *
 * @param level The log level that should be applied
 */
extern void vlog_set_output_level(FILE* output, enum vlog_level level);

/**
 * @brief Sets the current width of the output. This is useful for terminal
 * outputs where we want to keep proper retracing support.
 *
 * @param columns The number of columns for the output
 */
extern void vlog_set_output_width(FILE* output, int columns);

/**
 * @brief
 *
 * @param tag
 * @param format
 * @param ...
 */
extern void vlog_output(enum vlog_level level, const char* tag, const char* format, ...);

/**
 * @brief Flushes any remaining log data in active outputs.
 */
extern void vlog_flush(void);


enum vlog_content_status_type {
    VLOG_CONTENT_STATUS_NONE,
    VLOG_CONTENT_STATUS_WAITING,
    VLOG_CONTENT_STATUS_WORKING,
    VLOG_CONTENT_STATUS_DONE,
    VLOG_CONTENT_STATUS_FAILED
};

extern void vlog_start(FILE* handle, const char* header, const char* footer, int contentLineCount);
extern void vlog_content_set_index(int index);
extern void vlog_content_set_prefix(const char* header);
extern void vlog_content_set_status(enum vlog_content_status_type status);
extern void vlog_refresh(FILE* handle);

#endif //!__VLOG_H__
