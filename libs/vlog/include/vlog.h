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

#define VLOG_ERROR(tag, ...)   vlog_output(VLOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define VLOG_WARNING(tag, ...) vlog_output(VLOG_LEVEL_WARNING, tag, __VA_ARGS__)
#define VLOG_TRACE(tag, ...)   vlog_output(VLOG_LEVEL_TRACE, tag, __VA_ARGS__)
#define VLOG_DEBUG(tag, ...)   vlog_output(VLOG_LEVEL_DEBUG, tag, __VA_ARGS__)

/**
 * @brief Initializes vlog system. This should be invoked before any calls done to
 * to the vlog_* namespace.
 *
 */
extern void vlog_initialize(void);

/**
 * @brief Frees up any resources allocated by vlog, and closes all FILE* handles that were
 * passed to vlog through _add_output which were marked as 'free'.
 *
 */
extern void vlog_cleanup(void);

/**
 * @brief
 *
 * @param level
 */
extern void vlog_set_level(enum vlog_level level);

/**
 * @brief
 *
 * @param output
 * @param shouldClose
 * @return
 */
extern int vlog_add_output(FILE* output, int shouldClose);

/**
 * @brief
 *
 * @param tag
 * @param format
 * @param ...
 */
extern void vlog_output(enum vlog_level level, const char* tag, const char* format, ...);

/**
 * @brief
 *
 */
extern void vlog_flush(void);

#endif //!__VLOG_H__
