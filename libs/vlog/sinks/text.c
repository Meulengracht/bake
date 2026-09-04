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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../private.h"
#include "sinks.h"

static const char* g_levelNamesShort[] = {
        "",
        "E",
        "W",
        "T",
        "D"
};
static const char* g_levelNamesLong[] = {
        "",
        "error",
        "warning",
        "trace",
        "debug"
};

static void __text_write(struct vlog_sink_text* sink, const struct vlog_event* event)
{
    char       dateTime[32];
    time_t     now;
    struct tm* timeInfo;

    // ensure level is appropriate for sink
    if (sink->base.handle == NULL || event->data.log.level > sink->base.level) {
        return;
    }

    time(&now);
    timeInfo = localtime(&now);

    strftime(&dateTime[0], sizeof(dateTime) - 1, "%F %T", timeInfo);

    if (sink->options & VLOG_OUTPUT_OPTION_PROGRESS) {
        fprintf(sink->base.handle, __VLOG_CLEAR_LINE __VLOG_RESET_CURSOR);
    }

    if (!(sink->options & VLOG_OUTPUT_OPTION_NODECO)) {
        if (sink->options & VLOG_OUTPUT_OPTION_LONGDECO) {
            fprintf(sink->base.handle, "[%s] %s | %s | ", &dateTime[0], g_levelNamesLong[event->data.log.level], event->data.log.tag);
            if (event->data.log.level == VLOG_LEVEL_ERROR) {
                fprintf(sink->base.handle, "[e%i, %s] | ", event->data.log.err, strerror(event->data.log.err));
            }
        } else {
            if (event->data.log.level == VLOG_LEVEL_ERROR) {
                fprintf(sink->base.handle, "%s[%s%i, %s] ", event->data.log.tag, g_levelNamesShort[event->data.log.level], event->data.log.err, strerror(event->data.log.err));
            } else {
                fprintf(sink->base.handle, "%s[%s] ", event->data.log.tag, g_levelNamesShort[event->data.log.level]);
            }
        }
    }

    fprintf(sink->base.handle, "%s", event->data.log.message);
    fflush(sink->base.handle);
}

static void __text_emit(struct vlog_sink* base, const struct vlog_event* event)
{
    struct vlog_sink_text* sink = (struct vlog_sink_text*)base;

    switch (event->type) {
        case VLOG_EVENT_LOG:
            __text_write(sink, event);
            break;
        default:
            break;
    }
}

static void __text_destroy(struct vlog_sink* base)
{
    struct vlog_sink_text* sink = (struct vlog_sink_text*)base;
    
    if (sink->options & VLOG_OUTPUT_OPTION_CLOSE) {
        fclose(sink->base.handle);
    }
    free(sink);
}

struct vlog_sink* vlog_sink_new_text(FILE* handle, enum vlog_level level, unsigned int options)
{
    struct vlog_sink_text* sink = calloc(1, sizeof(struct vlog_sink_text));
    if (sink == NULL) {
        return NULL;
    }

    sink->base.type = VLOG_SINK_TYPE_TEXT;
    sink->base.handle = handle;
    sink->base.level = level;
    sink->base.emit = __text_emit;
    sink->base.destroy = __text_destroy;
    sink->options = options;

    return &sink->base;
}
