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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

#define __VLOG_RESET_CURSOR "\r"
#define __VLOG_CLEAR_LINE "\x1b[2K"
#define __VLOG_CLEAR_TOCURSOR "\x1b[0J"
#define __VLOG_MOVEUP_CURSOR "\x1b[1A"
#define __VLOG_MOVEUP_CURSOR_FMT "\x1b[%iF"

#define VLOG_MAX_OUTPUTS 4

struct vlog_output {
    FILE*           handle;
    enum vlog_level level;
    unsigned int    options;
    int             columns;
    int             lastRowCount;
};

struct vlog_context {
    struct vlog_output outputs[VLOG_MAX_OUTPUTS];
    int                outputs_count;
    enum vlog_level    default_level;
};

static struct vlog_context g_vlog = { { NULL, 0 } , 0, VLOG_LEVEL_DISABLED };
static const char*         g_levelNames[] = {
        "",
        "error",
        "warning",
        "trace",
        "debug"
};

void vlog_initialize(void)
{
    // nothing to do
}

void vlog_cleanup(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].options & VLOG_OUTPUT_OPTION_CLOSE) {
            fclose(g_vlog.outputs[i].handle);
        }
    }
    memset(&g_vlog, 0, sizeof(struct vlog_context));
}

void vlog_set_level(enum vlog_level level)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        g_vlog.outputs[i].level = level;
    }
    g_vlog.default_level = level;
}

int vlog_add_output(FILE* output)
{
    if (g_vlog.outputs_count == 4) {
        errno = ENOSPC;
        return -1;
    }

    g_vlog.outputs[g_vlog.outputs_count].handle       = output;
    g_vlog.outputs[g_vlog.outputs_count].level        = g_vlog.default_level;
    g_vlog.outputs[g_vlog.outputs_count].options      = 0;
    g_vlog.outputs[g_vlog.outputs_count].columns      = 0;
    g_vlog.outputs[g_vlog.outputs_count].lastRowCount = 0;
    g_vlog.outputs_count++;
    return 0;
}

void vlog_set_output_options(FILE* output, unsigned int flags)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].options |= flags;
            break;
        }
    }
}

void vlog_clear_output_options(FILE* output, unsigned int flags)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].options &= ~(flags);

            // when clearing RETRACE we reset the row count
            if (flags & VLOG_OUTPUT_OPTION_RETRACE) {
                g_vlog.outputs[i].lastRowCount = 0;
            }
            break;
        }
    }
}

void vlog_set_output_level(FILE* output, enum vlog_level level)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].level = level;
            break;
        }
    }
}

void vlog_set_output_width(FILE* output, int columns)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].columns = columns;
            break;
        }
    }
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list    args;
    char       dateTime[32];
    time_t     now;
    struct tm* timeInfo;
    char       cc[32] = { 0 };

    if (!g_vlog.outputs_count) {
        return;
    }

    time(&now);
    timeInfo = localtime(&now);

    strftime(&dateTime[0], sizeof(dateTime) - 1, "%F %T", timeInfo);
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        struct vlog_output* output      = &g_vlog.outputs[i];
        int                 colsWritten = 0;

        // ensure level is appropriate for output
        if (level > output->level) {
            continue;
        }

        // select control-code if any, and provided row count is valid
        if ((output->options & VLOG_OUTPUT_OPTION_RETRACE) && output->lastRowCount > 0) {
            snprintf(
                &cc[0],
                sizeof(cc),
                __VLOG_MOVEUP_CURSOR_FMT __VLOG_CLEAR_TOCURSOR,
                output->lastRowCount
            );
        }

        va_start(args, format);
        colsWritten += fprintf(output->handle, "%s[%s] %s | %s | ", &cc[0], &dateTime[0], g_levelNames[level], tag);
        if (level == VLOG_LEVEL_ERROR) {
            colsWritten += fprintf(output->handle, "[errno = %i, %s] | ", errno, strerror(errno));
        }
        colsWritten += vfprintf(output->handle, format, args);
        va_end(args);

        // calculate the last printed row-count if we have columns configured
        if ((output->options & VLOG_OUTPUT_OPTION_RETRACE) && output->columns > 0) {
            output->lastRowCount = (colsWritten + (output->columns - 1)) / output->columns;
        }
    }
}

void vlog_flush(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        fflush(g_vlog.outputs[i].handle);
    }
}
