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
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <threads.h>
#include <vlog.h>

#include "private.h"
#include "sinks/sinks.h"
#include "utils/queue.h"

struct vlog_context {
    int             initialized;
    enum vlog_level default_level;
    mtx_t           lock;
    unsigned int    next_step_id;
};

static struct vlog_context g_vlog = { 0 };

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
#include <unistd.h>
void __winch_handler(int sig)
{
    (void)sig;
    vlog_renderer_resize();
}
#endif

void vlog_initialize(enum vlog_level level)
{
    if (g_vlog.initialized) {
        return;
    }

    memset(&g_vlog, 0, sizeof(struct vlog_context));
    mtx_init(&g_vlog.lock, mtx_plain);

    // start by initializing locale
    setlocale(LC_ALL, "");

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    // register the handler that will update the terminal stats correctly
    // once the user resizes the terminal
    signal(SIGWINCH, __winch_handler);
#endif

    // spawn the renderer thread
    if (vlog_renderer_start()) {
        fprintf(stderr, "vlog: failed to start renderer\n");
        exit(EXIT_FAILURE);
    }

    // enable the vlog_* function to be used
    g_vlog.initialized = 1;

    // set default output level
    vlog_set_level(level);

    // add stdout by default, and do this after spawning the thread to avoid
    // potential race conditions with the renderer thread
    vlog_sink_add_text(stdout, 0);
}

void vlog_cleanup(void)
{
    // mark as uninitialized so that vlog_output() will not be used anymore
    g_vlog.initialized = 0;

    // shutdown the renderer
    vlog_renderer_stop();

    // cleanup resources
    mtx_destroy(&g_vlog.lock);
    memset(&g_vlog, 0, sizeof(struct vlog_context));
}

int vlog_sink_add_text(FILE* output, int close)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = __vlog_event_new(VLOG_EVENT_SINK_ADD);
    if (event == NULL) {
        return -1;
    }

    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;
    event->data.sink.type = VLOG_SINK_TYPE_TEXT;
    if (close) {
        event->data.sink.options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    vlog_renderer_push_event(event);
    vlog_barrier();
    return 0;
}

int vlog_sink_add_view(FILE* output, int close)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = __vlog_event_new(VLOG_EVENT_SINK_ADD);
    if (event == NULL) {
        return -1;
    }

    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;
    event->data.sink.type = VLOG_SINK_TYPE_VIEW;
    if (close) {
        event->data.sink.options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    vlog_renderer_push_event(event);
    vlog_barrier();
    return 0;
}

int vlog_sink_remove(FILE* output)
{
    struct vlog_event* event;

    // ensure that vlog is initialized
    if (!g_vlog.initialized) {
        return -1;
    }

    // Create a new sink and push it to the renderer thread
    event = __vlog_event_new(VLOG_EVENT_SINK_REMOVE);
    if (event == NULL) {
        return -1;
    }

    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;

    vlog_renderer_push_event(event);
    vlog_barrier();
    return 0;
}

void vlog_set_level(enum vlog_level level)
{
    struct vlog_event* event;

    // ensure that vlog is initialized
    if (!g_vlog.initialized) {
        return;
    }

    g_vlog.default_level = level;
    
    event = __vlog_event_new(VLOG_EVENT_SINK_SET_LEVEL);
    if (event == NULL) {
        return;
    }

    event->data.sink.handle = NULL;
    event->data.sink.level = level;
    vlog_renderer_push_event(event);
}

void vlog_set_output_options(FILE* output, unsigned int flags)
{
    struct vlog_event* event;

    event = __vlog_event_new(VLOG_EVENT_SINK_SET_OPTIONS);
    if (event == NULL) {
        return;
    }

    event->data.sink.handle = output;
    event->data.sink.options = flags;
    vlog_renderer_push_event(event);
}

void vlog_clear_output_options(FILE* output, unsigned int flags)
{
    struct vlog_event* event;

    event = __vlog_event_new(VLOG_EVENT_SINK_CLEAR_OPTIONS);
    if (event == NULL) {
        return;
    }

    event->data.sink.handle = output;
    event->data.sink.options = flags;
    vlog_renderer_push_event(event);
}

void vlog_set_output_level(FILE* output, enum vlog_level level)
{
    struct vlog_event* event;

    event = __vlog_event_new(VLOG_EVENT_SINK_SET_LEVEL);
    if (event == NULL) {
        return;
    }

    event->data.sink.handle = output;
    event->data.sink.level = level;
    vlog_renderer_push_event(event);
}

void vlog_flush(void)
{
    struct vlog_event* event;

    // ensure that vlog is initialized
    if (!g_vlog.initialized) {
        return;
    }

    event = __vlog_event_new(VLOG_EVENT_FLUSH);
    if (event == NULL) {
        return;
    }

    vlog_renderer_push_event(event);
    vlog_barrier();
}

void vlog_barrier(void)
{
    struct vlog_barrier_state state;
    struct vlog_event*      event;

    // ensure that vlog is initialized
    if (!g_vlog.initialized) {
        return;
    }

    event = __vlog_event_new(VLOG_EVENT_BARRIER);
    if (event == NULL) {
        return;
    }

    mtx_init(&state.lock, mtx_plain);
    cnd_init(&state.done);
    state.completed = 0;
    event->data.barrier.state = &state;

    mtx_lock(&state.lock);
    vlog_renderer_push_event(event);
    while (!state.completed) {
        cnd_wait(&state.done, &state.lock);
    }
    mtx_unlock(&state.lock);

    cnd_destroy(&state.done);
    mtx_destroy(&state.lock);
}

void vlog_view_open(FILE* handle, const char* header, const char* footer)
{
    // Create a new event and push it to the renderer thread
    struct vlog_event* event;

    if (!isatty(fileno(handle))) {
        return;
    }

    if (vlog_sink_add_view(handle, 0) != 0) {
        return;
    }

    event = __vlog_event_new(VLOG_EVENT_VIEW_OPEN);
    if (event == NULL) {
        return;
    }

    event->data.view_open.handle = handle;
    event->data.view_open.header = vlog_strdup(header);
    event->data.view_open.footer = vlog_strdup(footer);
    if ((header != NULL && event->data.view_open.header == NULL) ||
        (footer != NULL && event->data.view_open.footer == NULL)) {
        __vlog_event_delete(event);
        return;
    }
    vlog_renderer_push_event(event);
    vlog_barrier();
}

void vlog_view_close(void)
{
    // Create a new event and push it to the renderer thread
    struct vlog_event* event = __vlog_event_new(VLOG_EVENT_VIEW_CLOSE);
    if (event == NULL) {
        return;
    }

    event->data.view_close.handle = NULL;
    vlog_renderer_push_event(event);
    vlog_barrier();
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list            args;
    struct vlog_event* event;

    // ensure that vlog is initialized before we try to log anything
    if (!g_vlog.initialized) {
        return;
    }

    // use vlog_renderer_push_event to push the event to the renderer thread
    event = __vlog_event_new(VLOG_EVENT_LOG);
    if (event == NULL) {
        return;
    }

    event->data.log.level = level;
    event->data.log.tag = vlog_strdup(tag != NULL ? tag : "");
    if (event->data.log.tag == NULL) {
        __vlog_event_delete(event);
        return;
    }
    
    if (level == VLOG_LEVEL_ERROR) {
        event->data.log.err = errno;
    }

    va_start(args, format);
    event->data.log.message = vlog_vformat(format, args);
    va_end(args);
    if (format != NULL && event->data.log.message == NULL) {
        __vlog_event_delete(event);
        return;
    }

    vlog_renderer_push_event(event);
}

int vlog_step_open(struct vlog_step* step, const char* label)
{
    struct vlog_event* event;

    if (step == NULL) {
        errno = EINVAL;
        return -1;
    }

    mtx_lock(&g_vlog.lock);
    step->id = ++g_vlog.next_step_id;
    if (step->id == 0) {
        step->id = ++g_vlog.next_step_id;
    }
    mtx_unlock(&g_vlog.lock);

    event = __vlog_event_new(VLOG_EVENT_STEP_OPEN);
    if (event == NULL) {
        return -1;
    }

    event->data.step_open.step_id = step->id;
    event->data.step_open.label = vlog_strdup(label != NULL ? label : "");
    if (event->data.step_open.label == NULL) {
        __vlog_event_delete(event);
        return -1;
    }

    vlog_renderer_push_event(event);
    return 0;
}

int vlog_step_update(struct vlog_step* step, enum vlog_content_status_type status, const char* format, ...)
{
    va_list            args;
    struct vlog_event* event;

    if (step == NULL || step->id == 0) {
        errno = EINVAL;
        return -1;
    }

    event = __vlog_event_new(VLOG_EVENT_STEP_UPDATE);
    if (event == NULL) {
        return -1;
    }

    event->data.step_update.step_id = step->id;
    event->data.step_update.status = status;
    va_start(args, format);
    event->data.step_update.message = vlog_vformat(format, args);
    va_end(args);
    if (format != NULL && event->data.step_update.message == NULL) {
        __vlog_event_delete(event);
        return -1;
    }

    vlog_renderer_push_event(event);
    return 0;
}

int vlog_step_close(struct vlog_step* step, enum vlog_content_status_type status, const char* format, ...)
{
    va_list            args;
    struct vlog_event* event;

    if (step == NULL || step->id == 0) {
        errno = EINVAL;
        return -1;
    }

    event = __vlog_event_new(VLOG_EVENT_STEP_CLOSE);
    if (event == NULL) {
        return -1;
    }

    event->data.step_close.step_id = step->id;
    event->data.step_close.status = status;
    va_start(args, format);
    event->data.step_close.message = vlog_vformat(format, args);
    va_end(args);
    if (format != NULL && event->data.step_close.message == NULL) {
        __vlog_event_delete(event);
        return -1;
    }

    vlog_renderer_push_event(event);
    return 0;
}

