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

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "private.h"
#include "sinks/sinks.h"
#include "utils/queue.h"

struct vlog_renderer {
    thrd_t             tid;
    int                running;
    int                index;
    long long          time;
    int                update;
    struct queue       events;
    struct vlog_sink** sinks;
    int                sinks_count;

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    volatile sig_atomic_t resize;
#endif
};

static struct vlog_renderer g_renderer = { 0 };

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
static int __get_column_count(FILE* handle)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE                     console;
    int                        columns;

    if (handle == NULL) {
        return 80;
    }

    console = (HANDLE)_get_osfhandle(fileno(handle));
    if (console == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(console, &csbi)) {
        return 80;
    }

    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (columns <= 0) {
        return 80;
    }
    return columns;
}
#else
#include <sys/ioctl.h>
#include <unistd.h>
static int __get_column_count(FILE* handle)
{
    struct winsize w = { 0 };
    int            fd;

    if (handle == NULL) {
        return 80;
    }

    fd = fileno(handle);
    if (fd < 0 || ioctl(fd, TIOCGWINSZ, &w) != 0 || w.ws_col == 0) {
        return 80;
    }
    return (int)w.ws_col;
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

static void __sink_destroy(struct vlog_sink* sink, unsigned int ignoreClose)
{
    if (sink == NULL) {
        return;
    }

    // destroy must be implemented by each sink type
    sink->destroy(sink, ignoreClose);
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
                // Since we are migrating the handle, let's not close it if ownership
                // of the handle was controlled by the sink
                __sink_destroy(renderer->sinks[i], 1);
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
        __sink_destroy(sink, 0);
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
            __sink_destroy(renderer->sinks[i], 0);
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
                        break;
                    case VLOG_EVENT_BARRIER:
                        mtx_lock(&event->data.barrier.state->lock);
                        event->data.barrier.state->completed = 1;
                        cnd_signal(&event->data.barrier.state->done);
                        mtx_unlock(&event->data.barrier.state->lock);
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
        if (renderer->resize) {
            renderer->resize = 0;
            for (int i = 0; i < renderer->sinks_count; i++) {
                if (renderer->sinks[i] != NULL && renderer->sinks[i]->type == VLOG_SINK_TYPE_VIEW) {
                    ((struct vlog_sink_tty*)renderer->sinks[i])->columns = __get_column_count(renderer->sinks[i]->handle);
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
        __sink_destroy(renderer->sinks[i], 0);
    }
    free(renderer->sinks);
    renderer->sinks = NULL;
    renderer->sinks_count = 0;
    return 0;
}

int vlog_renderer_start(void)
{
    // initialize the event queue
    if (queue_init(&g_renderer.events, 128) != 0) {
        fprintf(stderr, "vlog: failed to initialize event queue\n");
        return -1;
    }

    // spawn the renderer thread
    if (thrd_create(&g_renderer.tid, __renderer_main, &g_renderer) != thrd_success) {
        fprintf(stderr, "vlog: failed to start renderer thread\n");
        return -1;
    }
    return 0;
}

void vlog_renderer_stop(void)
{
    if (g_renderer.running) {
        int                res;
        struct vlog_event* event = __vlog_event_new(VLOG_EVENT_SHUTDOWN);
        if (event != NULL) {
            queue_push(&g_renderer.events, event);
        } else {
            g_renderer.running = 0;
        }
        thrd_join(g_renderer.tid, &res);
    }

    // clean resources
    queue_destroy(&g_renderer.events);
}

void vlog_renderer_resize(void)
{
#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    g_renderer.resize = 1;
#endif
}

void vlog_renderer_push_event(struct vlog_event* event)
{
    queue_push(&g_renderer.events, event);
}
