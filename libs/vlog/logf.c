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

// Forward declarations
struct vlog_flush_state {
    mtx_t lock;
    cnd_t done;
    int   completed;
};

struct vlog_renderer {
    thrd_t             tid;
    volatile int       running;
    volatile int       index;
    volatile long long time;
    volatile int       update;
    struct queue       events;
    struct vlog_sink** sinks;
    int                sinks_count; 
};

struct vlog_context {
    int                  initialized;
    enum vlog_level      default_level;
    struct vlog_renderer renderer;
    mtx_t                lock;
    unsigned int         next_step_id;
};

static struct vlog_context g_vlog = { 0 };
#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
static volatile sig_atomic_t g_terminal_resized = 0;
#endif

static struct vlog_event* __vlog_event_new(enum vlog_event_type type)
{
    struct vlog_event* event;

    event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return NULL;
    }

    event->type = type;
    timespec_get(&event->timestamp, TIME_UTC);
    return event;
}

static void __vlog_event_delete(struct vlog_event* event)
{
    if (event) {
        switch (event->type) {
            case VLOG_EVENT_LOG:
                free(event->data.log.tag);
                free(event->data.log.message);
                break;
            case VLOG_EVENT_VIEW_OPEN:
                free(event->data.view_open.header);
                free(event->data.view_open.footer);
                break;
            case VLOG_EVENT_STEP_OPEN:
                free(event->data.step_open.label);
                break;
            case VLOG_EVENT_STEP_UPDATE:
                free(event->data.step_update.message);
                break;
            case VLOG_EVENT_STEP_CLOSE:
                free(event->data.step_close.message);
                break;
            default:
                break;
        }
        free(event);
    }
}

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

void __winch_handler(int sig)
{
    (void)sig;
    g_terminal_resized = 1;
}
#endif

static unsigned int* __sink_options(struct vlog_sink* sink)
{
    switch (sink->type) {
        case VLOG_SINK_TYPE_TEXT:
            return &((struct vlog_sink_text*)sink)->options;
        case VLOG_SINK_TYPE_VIEW:
            return &((struct vlog_sink_tty*)sink)->options;
        default:
            return NULL;
    }
}

static void __sink_destroy(struct vlog_sink* sink)
{
    if (sink == NULL) {
        return;
    }

    if (sink->destroy) {
        sink->destroy(sink);
    }
}

static struct vlog_sink* __sink_find(struct vlog_renderer* renderer, FILE* handle, enum vlog_sink_type type)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i] != NULL &&
            renderer->sinks[i]->handle == handle &&
            renderer->sinks[i]->type == type) {
            return renderer->sinks[i];
        }
    }
    return NULL;
}

static void __sink_add(struct vlog_renderer* renderer, struct vlog_event* event)
{
    struct vlog_sink* sink;
    struct vlog_sink** sinks;

    sink = __sink_find(renderer, event->data.sink.handle, event->data.sink.type);
    if (sink != NULL) {
        sink->level = event->data.sink.level;
        return;
    }

    if (event->data.sink.type == VLOG_SINK_TYPE_VIEW) {
        for (int i = 0; i < renderer->sinks_count; i++) {
            if (renderer->sinks[i] != NULL &&
                renderer->sinks[i]->handle == event->data.sink.handle &&
                renderer->sinks[i]->type == VLOG_SINK_TYPE_TEXT) {
                __sink_destroy(renderer->sinks[i]);
                renderer->sinks_count--;
                if (i < renderer->sinks_count) {
                    memmove(
                        &renderer->sinks[i],
                        &renderer->sinks[i + 1],
                        (renderer->sinks_count - i) * sizeof(struct vlog_sink*)
                    );
                }
                break;
            }
        }
    }

    switch (event->data.sink.type) {
        case VLOG_SINK_TYPE_TEXT:
            sink = vlog_sink_new_text(
                event->data.sink.handle,
                event->data.sink.level,
                event->data.sink.options
            );
            break;
        case VLOG_SINK_TYPE_VIEW:
            sink = vlog_sink_new_view(
                event->data.sink.handle,
                event->data.sink.level,
                event->data.sink.options
            );
            break;
        default:
            return;
    }

    if (sink == NULL) {
        fprintf(stderr, "vlog: failed to allocate memory for sink\n");
        return;
    }

    // Otherwise grow the array
    sinks = realloc(
        renderer->sinks,
        (renderer->sinks_count + 1) * sizeof(struct vlog_sink*)
    );
    if (sinks == NULL) {
        __sink_destroy(sink);
        fprintf(stderr, "vlog: failed to allocate memory for sink list\n");
        return;
    }
    renderer->sinks = sinks;
    renderer->sinks[renderer->sinks_count++] = sink;
}

static void __sink_remove(struct vlog_renderer* renderer, struct vlog_event* event)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i] != NULL && renderer->sinks[i]->handle == event->data.sink.handle) {
            __sink_destroy(renderer->sinks[i]);
            renderer->sinks_count--;
            if (i < renderer->sinks_count) {
                memmove(
                    &renderer->sinks[i],
                    &renderer->sinks[i + 1],
                    (renderer->sinks_count - i) * sizeof(struct vlog_sink*)
                );
            }
            i--;
        }
    }
}

static void __sink_set_level(struct vlog_renderer* renderer, struct vlog_event* event)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (event->data.sink.handle == NULL || renderer->sinks[i]->handle == event->data.sink.handle) {
            renderer->sinks[i]->level = event->data.sink.level;
        }
    }
}

static void __sink_set_options(struct vlog_renderer* renderer, struct vlog_event* event, int clear)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        unsigned int* options;

        if (renderer->sinks[i] == NULL || renderer->sinks[i]->handle != event->data.sink.handle) {
            continue;
        }

        options = __sink_options(renderer->sinks[i]);
        if (options == NULL) {
            continue;
        }

        if (clear) {
            *options &= ~event->data.sink.options;
        } else {
            *options |= event->data.sink.options;
        }
    }
}

static void __sink_flush(struct vlog_renderer* renderer)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i] == NULL) {
            continue;
        }

        if (renderer->sinks[i]->flush) {
            renderer->sinks[i]->flush(renderer->sinks[i]);
        } else if (renderer->sinks[i]->handle != NULL) {
            fflush(renderer->sinks[i]->handle);
        }
    }
}

static int __renderer_main(void* context)
{
    struct vlog_renderer* renderer = context;
    struct timespec       ts;
    struct vlog_event*    event;

    renderer->running = 1;
    while (renderer->running == 1) {
        do {
            timespec_get(&ts, TIME_UTC);
            // wait for 100ms
            ts.tv_nsec += 100 * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            
            event = queue_pop(&renderer->events, &ts);
            if (event != NULL) {
                switch (event->type) {
                    case VLOG_EVENT_SINK_ADD:
                        __sink_add(renderer, event);
                        break;
                    case VLOG_EVENT_SINK_REMOVE:
                        __sink_remove(renderer, event);
                        break;
                    case VLOG_EVENT_SINK_SET_LEVEL:
                        __sink_set_level(renderer, event);
                        break;
                    case VLOG_EVENT_SINK_SET_OPTIONS:
                        __sink_set_options(renderer, event, 0);
                        break;
                    case VLOG_EVENT_SINK_CLEAR_OPTIONS:
                        __sink_set_options(renderer, event, 1);
                        break;
                    case VLOG_EVENT_FLUSH:
                        __sink_flush(renderer);
                        mtx_lock(&event->data.flush.state->lock);
                        event->data.flush.state->completed = 1;
                        cnd_signal(&event->data.flush.state->done);
                        mtx_unlock(&event->data.flush.state->lock);
                        break;
                    case VLOG_EVENT_SHUTDOWN:
                        __sink_flush(renderer);
                        renderer->running = 0;
                        break;
                    default: {
                        for (int i = 0; i < renderer->sinks_count; i++) {
                            if (renderer->sinks[i] && renderer->sinks[i]->emit) {
                                renderer->sinks[i]->emit(renderer->sinks[i], event);
                            }
                        }
                    } break;
                }
                __vlog_event_delete(event);
            }
        } while (event != NULL);

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
        if (g_terminal_resized) {
            g_terminal_resized = 0;
            for (int i = 0; i < renderer->sinks_count; i++) {
                if (renderer->sinks[i] != NULL && renderer->sinks[i]->type == VLOG_SINK_TYPE_VIEW) {
                    ((struct vlog_sink_tty*)renderer->sinks[i])->columns = __get_column_count();
                }
            }
        }
#endif
        
        // Tick all sinks that have a tick function
        for (int i = 0; i < renderer->sinks_count; i++) {
            if (renderer->sinks[i] && renderer->sinks[i]->tick) {
                renderer->sinks[i]->tick(renderer->sinks[i], 100);
            }
        }
    }

    // Close all sinks
    for (int i = 0; i < renderer->sinks_count; i++) {
        __sink_destroy(renderer->sinks[i]);
    }
    free(renderer->sinks);
    renderer->sinks = NULL;
    renderer->sinks_count = 0;
    return 0;
}

void vlog_initialize(enum vlog_level level)
{
    memset(&g_vlog, 0, sizeof(struct vlog_context));
    mtx_init(&g_vlog.lock, mtx_plain);

    // initialize the event queue
    if (queue_init(&g_vlog.renderer.events, 128) != 0) {
        fprintf(stderr, "vlog: failed to initialize event queue\n");
        exit(EXIT_FAILURE);
    }

    // start by initializing locale
    setlocale(LC_ALL, "");

    // set default output level
    vlog_set_level(level);

    // add stdout by default
    vlog_sink_add_text(stdout, 0);

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    // register the handler that will update the terminal stats correctly
    // once the user resizes the terminal
    signal(SIGWINCH, __winch_handler);
#endif

    // spawn the renderer thread
    if (thrd_create(&g_vlog.renderer.tid, __renderer_main, &g_vlog.renderer) != thrd_success) {
        fprintf(stderr, "vlog: failed to spawn thread for renderer\n");
        exit(EXIT_FAILURE);
    }

    // enable the vlog_output() function to be used
    g_vlog.initialized = 1;
}

void vlog_cleanup(void)
{
    // mark as uninitialized so that vlog_output() will not be used anymore
    g_vlog.initialized = 0;

    // shutdown the renderer
    if (g_vlog.renderer.running) {
        struct vlog_event* event;
        int res;

        event = __vlog_event_new(VLOG_EVENT_SHUTDOWN);
        if (event != NULL) {
            queue_push(&g_vlog.renderer.events, event);
        } else {
            g_vlog.renderer.running = 0;
        }
        thrd_join(g_vlog.renderer.tid, &res);
    }

    // clean resources
    queue_destroy(&g_vlog.renderer.events);
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

    queue_push(&g_vlog.renderer.events, event);
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

    queue_push(&g_vlog.renderer.events, event);
    return 0;
}

int vlog_sink_remove(FILE* output)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = __vlog_event_new(VLOG_EVENT_SINK_REMOVE);
    if (event == NULL) {
        return -1;
    }

    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;

    queue_push(&g_vlog.renderer.events, event);
    return 0;
}

void vlog_set_level(enum vlog_level level)
{
    struct vlog_event* event;

    g_vlog.default_level = level;
    if (!g_vlog.renderer.running) {
        return;
    }

    event = __vlog_event_new(VLOG_EVENT_SINK_SET_LEVEL);
    if (event == NULL) {
        return;
    }

    event->data.sink.handle = NULL;
    event->data.sink.level = level;
    queue_push(&g_vlog.renderer.events, event);
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
    queue_push(&g_vlog.renderer.events, event);
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
    queue_push(&g_vlog.renderer.events, event);
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
    queue_push(&g_vlog.renderer.events, event);
}

void vlog_flush(void)
{
    struct vlog_flush_state state;
    struct vlog_event*      event;

    if (!g_vlog.renderer.running) {
        return;
    }

    event = __vlog_event_new(VLOG_EVENT_FLUSH);
    if (event == NULL) {
        return;
    }

    mtx_init(&state.lock, mtx_plain);
    cnd_init(&state.done);
    state.completed = 0;
    event->data.flush.state = &state;

    mtx_lock(&state.lock);
    queue_push(&g_vlog.renderer.events, event);
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
    queue_push(&g_vlog.renderer.events, event);
}

void vlog_view_close(void)
{
    // Create a new event and push it to the renderer thread
    struct vlog_event* event = __vlog_event_new(VLOG_EVENT_VIEW_CLOSE);
    if (event == NULL) {
        return;
    }

    event->data.view_close.handle = NULL;
    queue_push(&g_vlog.renderer.events, event);
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list            args;
    struct vlog_event* event;

    // ensure that vlog is initialized before we try to log anything
    if (!g_vlog.initialized) {
        return;
    }

    // use queue_push to push the event to the renderer thread
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
    
    va_start(args, format);
    event->data.log.message = vlog_vformat(format, args);
    va_end(args);
    if (format != NULL && event->data.log.message == NULL) {
        __vlog_event_delete(event);
        return;
    }

    queue_push(&g_vlog.renderer.events, event);
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

    queue_push(&g_vlog.renderer.events, event);
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

    queue_push(&g_vlog.renderer.events, event);
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

    queue_push(&g_vlog.renderer.events, event);
    return 0;
}

