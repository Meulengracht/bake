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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../private.h"
#include "sinks.h"

static const char* g_statusNames[] = {
        "",
        "WAITING",
        "WORKING",
        "DONE",
        "FAILED"
};
static const char* g_statusColor[] = {
        "\x1b[0m",
        "\x1b[90m",
        "\x1b[37m",
        "\x1b[32m",
        "\x1b[31m"
};
static const char* g_animatorCharacter[] = {
        "|",
        "/",
        "-",
        "\\",
        "/",
        "-"
};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
static int __get_column_count(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int                        columns;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    // rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return columns;
}

#else
#include <sys/ioctl.h>
#include <unistd.h>
static int __get_column_count(void)
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return (int)w.ws_col;
}
#endif

static void __render_line_with_text(struct vlog_sink_tty* sink, const char* embed, int lcorner, int middle, int rcorner)
{
    int columns = sink->columns;
    int titleCount = embed != NULL ? ((int)strlen(embed) + 2) : 0;
    int lcount = 3;
    int rcount = columns - (titleCount + 2 + lcount);

    fprintf(sink->base.handle, "%lc", lcorner);
    for (int i = 0; i < lcount; i++) fprintf(sink->base.handle, "%lc", middle);
    if (embed != NULL) {
        fprintf(sink->base.handle, " %s ", embed);
    }
    for (int i = 0; i < rcount; i++) fprintf(sink->base.handle, "%lc", middle);
    fprintf(sink->base.handle, "%lc\n", rcorner);
}

static void __fmt_indicator(struct vlog_sink_tty* sink, char* buffer, enum vlog_content_status_type status)
{
    if (status == VLOG_CONTENT_STATUS_WORKING) {
        long long seconds = sink->spinner_time_ms / 1000;
        long long ms = (sink->spinner_time_ms % 1000) / 100;
        int index = sink->spinner_index % 6;
        sprintf(buffer, "%s %lli.%llis", g_animatorCharacter[index], seconds, ms);
    } else {
        strcpy(buffer, g_statusNames[status]);
    }
}

static void __refresh_view(struct vlog_sink_tty* sink, int clear)
{
    char indicator[20] = { 0 };
    int  messageWidth;

    if (!sink->view_active) {
        return;
    }

    messageWidth = sink->columns - 25;
    if (messageWidth < 1) {
        messageWidth = 1;
    }
    
    if (clear) {
        fprintf(
            sink->base.handle,
            __VLOG_MOVEUP_CURSOR_FMT __VLOG_CLEAR_TOCURSOR,
            sink->rendered_rows
        );
    }

    // print first line
    __render_line_with_text(sink, sink->title, 0x250D, 0x2500, 0x2511);

    // print content lines
    for (size_t i = 0; i < sink->step_count; i++) {
        const char* label = sink->steps[i].label != NULL ? sink->steps[i].label : "";
        const char* message = sink->steps[i].message != NULL ? sink->steps[i].message : "";

        __fmt_indicator(sink, &indicator[0], sink->steps[i].status);
        fprintf(sink->base.handle, "%lc %-10s %-*.*s %s%-10s%s%lc\n",
            0x2502,
            label,
            messageWidth,
            messageWidth,
            message,
            g_statusColor[sink->steps[i].status],
            &indicator[0],
            g_statusColor[0],
            0x2502
        );
    }

    // print final line
    __render_line_with_text(sink, sink->footer, 0x2515, 0x2500, 0x2519);
    fputs("\x1b[0m", sink->base.handle);

    sink->rendered_rows = (int)sink->step_count + 2;
    fflush(sink->base.handle);
}

static void __view_step_destroy(struct vlog_tty_step* step)
{
    free(step->label);
    free(step->message);
    memset(step, 0, sizeof(struct vlog_tty_step));
}

static void __view_steps_clear(struct vlog_sink_tty* sink)
{
    for (size_t i = 0; i < sink->step_count; i++) {
        __view_step_destroy(&sink->steps[i]);
    }
    sink->step_count = 0;
}

static struct vlog_tty_step* __view_step_find(struct vlog_sink_tty* sink, unsigned int stepId)
{
    for (size_t i = 0; i < sink->step_count; i++) {
        if (sink->steps[i].id == stepId) {
            return &sink->steps[i];
        }
    }
    return NULL;
}

static struct vlog_tty_step* __view_step_get(struct vlog_sink_tty* sink, unsigned int stepId)
{
    struct vlog_tty_step* steps;
    size_t                capacity;

    struct vlog_tty_step* step = __view_step_find(sink, stepId);
    if (step != NULL) {
        return step;
    }

    if (sink->step_count == sink->step_capacity) {
        capacity = sink->step_capacity == 0 ? 8 : sink->step_capacity * 2;
        steps = realloc(sink->steps, capacity * sizeof(struct vlog_tty_step));
        if (steps == NULL) {
            return NULL;
        }
        memset(&steps[sink->step_capacity], 0, (capacity - sink->step_capacity) * sizeof(struct vlog_tty_step));
        sink->steps = steps;
        sink->step_capacity = capacity;
    }

    step = &sink->steps[sink->step_count++];
    memset(step, 0, sizeof(struct vlog_tty_step));
    step->id = stepId;
    return step;
}

static void __view_step_set_text(char** target, const char* text)
{
    char* copy = vlog_strdup(text != NULL ? text : "");
    if (copy == NULL) {
        return;
    }

    for (int i = 0; copy[i] != '\0'; i++) {
        if (copy[i] == '\n') {
            copy[i] = ' ';
        }
    }

    free(*target);
    *target = copy;
}

static int __view_has_working_steps(struct vlog_sink_tty* sink)
{
    for (size_t i = 0; i < sink->step_count; i++) {
        if (sink->steps[i].status == VLOG_CONTENT_STATUS_WORKING) {
            return 1;
        }
    }
    return 0;
}

static void __view_step_open(struct vlog_sink_tty* sink, const struct vlog_event* event)
{
    struct vlog_tty_step* step = __view_step_get(sink, event->data.step_open.step_id);
    if (step == NULL) {
        return;
    }

    __view_step_set_text(&step->label, event->data.step_open.label);
    step->status = VLOG_CONTENT_STATUS_WAITING;
}

static void __view_step_update(struct vlog_sink_tty* sink, unsigned int stepId, enum vlog_content_status_type status, const char* message)
{
    struct vlog_tty_step* step = __view_step_get(sink, stepId);
    if (step == NULL) {
        return;
    }

    step->status = status;
    if (message != NULL) {
        __view_step_set_text(&step->message, message);
    }
    if (status == VLOG_CONTENT_STATUS_WORKING) {
        sink->active_step_id = stepId;
        sink->spinner_time_ms = 0;
        sink->spinner_index = 0;
    }
}

static void __view_write(struct vlog_sink_tty* sink, enum vlog_level level, const char* message)
{
    struct vlog_tty_step* step;

    // ensure level is appropriate for sink
    if (sink->base.handle == NULL || level > sink->base.level) {
        return;
    }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // update column count on sink to stdout if on windows, we can
    // only poll
    sink->columns = __get_column_count();
#endif

    if (!sink->view_active || sink->active_step_id == 0) {
        return;
    }

    step = __view_step_find(sink, sink->active_step_id);
    if (step == NULL) {
        return;
    }

    __view_step_set_text(&step->message, message);
}

static void __view_emit(struct vlog_sink* base, const struct vlog_event* event)
{
    struct vlog_sink_tty* sink = (struct vlog_sink_tty*)base;

    switch (event->type) {
        case VLOG_EVENT_LOG:
            __view_write(sink, event->data.log.level, event->data.log.message);
            break;
        case VLOG_EVENT_RESIZE:
            sink->columns = event->data.resize.columns;
            break;
        case VLOG_EVENT_VIEW_OPEN:
            if (event->data.view_open.handle != sink->base.handle) {
                break;
            }
            __view_steps_clear(sink);
            __view_step_set_text(&sink->title, event->data.view_open.header);
            __view_step_set_text(&sink->footer, event->data.view_open.footer);
            sink->view_active = 1;
            sink->rendered_rows = 0;
            break;
        case VLOG_EVENT_VIEW_CLOSE:
            if (event->data.view_close.handle != NULL && event->data.view_close.handle != sink->base.handle) {
                break;
            }
            if (sink->view_active) {
                __refresh_view(sink, sink->rendered_rows > 0);
                sink->view_active = 0;
            }
            break;
        case VLOG_EVENT_STEP_OPEN:
            __view_step_open(sink, event);
            break;
        case VLOG_EVENT_STEP_UPDATE:
            __view_step_update(
                sink,
                event->data.step_update.step_id,
                event->data.step_update.status,
                event->data.step_update.message
            );
            break;
        case VLOG_EVENT_STEP_CLOSE:
            __view_step_update(
                sink,
                event->data.step_close.step_id,
                event->data.step_close.status,
                event->data.step_close.message
            );
            break;
        default:
            break;
    }
    if (sink->view_active) {
        __refresh_view(sink, sink->rendered_rows > 0);
    }
}

static void __view_tick(struct vlog_sink* base, long long time)
{
    struct vlog_sink_tty* sink = (struct vlog_sink_tty*)base;

    if (!sink->view_active || !__view_has_working_steps(sink)) {
        return;
    }

    sink->spinner_time_ms += time;
    if (sink->spinner_time_ms >= 500) {
        sink->spinner_index++;
        __refresh_view(sink, 1);
    }
}

static void __view_destroy(struct vlog_sink* base)
{
    struct vlog_sink_tty* sink = (struct vlog_sink_tty*)base;
    
    if (sink->options & VLOG_OUTPUT_OPTION_CLOSE) {
        fclose(base->handle);
    }
    __view_steps_clear(sink);
    free(sink->steps);
    free(sink->title);
    free(sink->footer);
    free(sink);
}

struct vlog_sink* vlog_sink_new_view(FILE* handle, enum vlog_level level, unsigned int options)
{
    struct vlog_sink_tty* sink = calloc(1, sizeof(struct vlog_sink_tty));
    if (sink == NULL) {
        return NULL;
    }

    sink->base.type = VLOG_SINK_TYPE_VIEW;
    sink->base.handle = handle;
    sink->base.level = level;
    sink->base.emit = __view_emit;
    sink->base.tick = __view_tick;
    sink->base.destroy = __view_destroy;
    sink->columns = __get_column_count();
    sink->options = options;

    return &sink->base;
}
