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

#define VLOG_MAX_OUTPUTS 4

struct vlog_output {
    FILE* handle;
    int   shouldClose;
};

struct vlog_context {
    struct vlog_output outputs[VLOG_MAX_OUTPUTS];
    int                outputs_count;
    enum vlog_level    level;
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
    // nothing to do yet
}

void vlog_cleanup(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].shouldClose) {
            fclose(g_vlog.outputs[i].handle);
        }
    }
    memset(&g_vlog, 0, sizeof(struct vlog_context));
}

void vlog_set_level(enum vlog_level level)
{
    g_vlog.level = level;
}

int vlog_add_output(FILE* output, int shouldClose)
{
    if (g_vlog.outputs_count == 4) {
        errno = ENOSPC;
        return -1;
    }

    g_vlog.outputs[g_vlog.outputs_count].shouldClose = shouldClose;
    g_vlog.outputs[g_vlog.outputs_count].handle      = output;
    g_vlog.outputs_count++;
    return 0;
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list    args;
    char       dateTime[32];
    time_t     now;
    struct tm* timeInfo;

    if (!g_vlog.outputs_count || level > g_vlog.level) {
        return;
    }

    time(&now);
    timeInfo = localtime(&now);

    strftime(&dateTime[0], sizeof(dateTime) - 1, "%F %T", timeInfo);
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        va_start(args, format);
        fprintf(g_vlog.outputs[i].handle, "[%s] %s | %s | ", &dateTime[0], g_levelNames[level], tag);
        if (level == VLOG_LEVEL_ERROR) {
            fprintf(g_vlog.outputs[i].handle, "[errno = %i, desc = %s] | ", errno, strerror(errno));
        }
        vfprintf(g_vlog.outputs[i].handle, format, args);
        va_end(args);
    }
}

void vlog_flush(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        fflush(g_vlog.outputs[i].handle);
    }
}
