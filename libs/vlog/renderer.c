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
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "private.h"
#include "sinks/sinks.h"
#include "utils/pipe.h"

#define VLOG_WIRE_MAGIC       0x564c4f47u
#define VLOG_WIRE_VERSION     1u
#define VLOG_WIRE_NULL_LENGTH UINT32_MAX

struct vlog_wire_header {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t size;
    int64_t  timestamp_sec;
    int64_t  timestamp_nsec;
    uint64_t handle;
    int32_t  level;
    int32_t  status;
    int32_t  columns;
    int32_t  err;
    uint32_t options;
    uint32_t step_id;
    uint64_t barrier_state;
    uint32_t tag_length;
    uint32_t message_length;
    uint32_t header_length;
    uint32_t footer_length;
    uint32_t label_length;
};

struct vlog_renderer {
    thrd_t             tid;
    int                running;
    int                index;
    long long          time;
    int                update;
    struct vlog_pipe   pipe;
    mtx_t              write_lock;
    unsigned long      owner_process_id;
    struct vlog_sink** sinks;
    int                sinks_count;

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    volatile sig_atomic_t resize;
#endif
};

static struct vlog_renderer g_renderer = { 0 };

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
static unsigned long __current_process_id(void)
{
    return (unsigned long)GetCurrentProcessId();
}

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
static unsigned long __current_process_id(void)
{
    return (unsigned long)getpid();
}

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

static uint32_t __wire_string_length(const char* text)
{
    size_t length;

    if (text == NULL) {
        return VLOG_WIRE_NULL_LENGTH;
    }

    length = strlen(text);
    if (length >= VLOG_WIRE_NULL_LENGTH) {
        return VLOG_WIRE_NULL_LENGTH;
    }
    return (uint32_t)length;
}

static int __wire_add_string_size(size_t* size, uint32_t length)
{
    if (length == VLOG_WIRE_NULL_LENGTH) {
        return 0;
    }
    if (*size > SIZE_MAX - length) {
        errno = EOVERFLOW;
        return -1;
    }
    *size += length;
    return 0;
}

static void __wire_write_string(unsigned char** cursor, const char* text, uint32_t length)
{
    if (length == VLOG_WIRE_NULL_LENGTH || length == 0) {
        return;
    }
    memcpy(*cursor, text, length);
    *cursor += length;
}

static char* __wire_read_string(const unsigned char** cursor, size_t* remaining, uint32_t length)
{
    char* text;

    if (length == VLOG_WIRE_NULL_LENGTH) {
        return NULL;
    }
    if (length > *remaining) {
        errno = EINVAL;
        return NULL;
    }

    text = malloc((size_t)length + 1);
    if (text == NULL) {
        return NULL;
    }
    if (length > 0) {
        memcpy(text, *cursor, length);
    }
    text[length] = '\0';
    *cursor += length;
    *remaining -= length;
    return text;
}

static int __wire_header_init(struct vlog_wire_header* header, const struct vlog_event* event)
{
    memset(header, 0, sizeof(struct vlog_wire_header));
    header->magic = VLOG_WIRE_MAGIC;
    header->version = VLOG_WIRE_VERSION;
    header->type = (uint32_t)event->type;
    header->timestamp_sec = (int64_t)event->timestamp.tv_sec;
    header->timestamp_nsec = (int64_t)event->timestamp.tv_nsec;

    switch (event->type) {
        case VLOG_EVENT_SINK_ADD:
        case VLOG_EVENT_SINK_REMOVE:
        case VLOG_EVENT_SINK_SET_LEVEL:
        case VLOG_EVENT_SINK_SET_OPTIONS:
        case VLOG_EVENT_SINK_CLEAR_OPTIONS:
            header->handle = (uint64_t)(uintptr_t)event->data.sink.handle;
            header->level = (int32_t)event->data.sink.level;
            header->status = (int32_t)event->data.sink.type;
            header->options = event->data.sink.options;
            break;
        case VLOG_EVENT_LOG:
            header->level = (int32_t)event->data.log.level;
            header->err = event->data.log.err;
            header->tag_length = __wire_string_length(event->data.log.tag);
            header->message_length = __wire_string_length(event->data.log.message);
            break;
        case VLOG_EVENT_RESIZE:
            header->columns = event->data.resize.columns;
            break;
        case VLOG_EVENT_VIEW_OPEN:
            header->handle = (uint64_t)(uintptr_t)event->data.view_open.handle;
            header->header_length = __wire_string_length(event->data.view_open.header);
            header->footer_length = __wire_string_length(event->data.view_open.footer);
            break;
        case VLOG_EVENT_VIEW_CLOSE:
            header->handle = (uint64_t)(uintptr_t)event->data.view_close.handle;
            break;
        case VLOG_EVENT_STEP_OPEN:
            header->step_id = event->data.step_open.step_id;
            header->label_length = __wire_string_length(event->data.step_open.label);
            break;
        case VLOG_EVENT_STEP_UPDATE:
            header->step_id = event->data.step_update.step_id;
            header->status = (int32_t)event->data.step_update.status;
            header->message_length = __wire_string_length(event->data.step_update.message);
            break;
        case VLOG_EVENT_STEP_CLOSE:
            header->step_id = event->data.step_close.step_id;
            header->status = (int32_t)event->data.step_close.status;
            header->message_length = __wire_string_length(event->data.step_close.message);
            break;
        case VLOG_EVENT_BARRIER:
            header->barrier_state = (uint64_t)(uintptr_t)event->data.barrier.state;
            break;
        default:
            break;
    }
    return 0;
}

static int __wire_event_size(struct vlog_wire_header* header, size_t* size)
{
    *size = sizeof(struct vlog_wire_header);
    if (__wire_add_string_size(size, header->tag_length) != 0 ||
        __wire_add_string_size(size, header->message_length) != 0 ||
        __wire_add_string_size(size, header->header_length) != 0 ||
        __wire_add_string_size(size, header->footer_length) != 0 ||
        __wire_add_string_size(size, header->label_length) != 0) {
        return -1;
    }
    if (*size > VLOG_PIPE_MESSAGE_MAX) {
        errno = EMSGSIZE;
        return -1;
    }
    header->size = (uint32_t)*size;
    return 0;
}

static int __renderer_send_event(struct vlog_renderer* renderer, const struct vlog_event* event)
{
    struct vlog_wire_header header;
    unsigned char*          buffer;
    unsigned char*          cursor;
    size_t                  size;
    int                     status;

    if (__wire_header_init(&header, event) != 0 || __wire_event_size(&header, &size) != 0) {
        return -1;
    }

    buffer = malloc(size);
    if (buffer == NULL) {
        return -1;
    }

    memcpy(buffer, &header, sizeof(header));
    cursor = buffer + sizeof(header);
    switch (event->type) {
        case VLOG_EVENT_LOG:
            __wire_write_string(&cursor, event->data.log.tag, header.tag_length);
            __wire_write_string(&cursor, event->data.log.message, header.message_length);
            break;
        case VLOG_EVENT_VIEW_OPEN:
            __wire_write_string(&cursor, event->data.view_open.header, header.header_length);
            __wire_write_string(&cursor, event->data.view_open.footer, header.footer_length);
            break;
        case VLOG_EVENT_STEP_OPEN:
            __wire_write_string(&cursor, event->data.step_open.label, header.label_length);
            break;
        case VLOG_EVENT_STEP_UPDATE:
            __wire_write_string(&cursor, event->data.step_update.message, header.message_length);
            break;
        case VLOG_EVENT_STEP_CLOSE:
            __wire_write_string(&cursor, event->data.step_close.message, header.message_length);
            break;
        default:
            break;
    }

    if (renderer->owner_process_id == __current_process_id()) {
        mtx_lock(&renderer->write_lock);
        status = vlog_pipe_send(&renderer->pipe, buffer, size);
        mtx_unlock(&renderer->write_lock);
    } else {
        status = vlog_pipe_send(&renderer->pipe, buffer, size);
    }

    free(buffer);
    return status;
}

static struct vlog_event* __renderer_decode_event(const void* buffer, size_t length)
{
    const struct vlog_wire_header* header = buffer;
    const unsigned char*           cursor;
    struct vlog_event*             event;
    size_t                         remaining;

    if (length < sizeof(struct vlog_wire_header) ||
        header->magic != VLOG_WIRE_MAGIC ||
        header->version != VLOG_WIRE_VERSION ||
        header->size != length) {
        errno = EINVAL;
        return NULL;
    }

    event = __vlog_event_new((enum vlog_event_type)header->type);
    if (event == NULL) {
        return NULL;
    }
    event->timestamp.tv_sec = (time_t)header->timestamp_sec;
    event->timestamp.tv_nsec = (long)header->timestamp_nsec;

    cursor = (const unsigned char*)buffer + sizeof(struct vlog_wire_header);
    remaining = length - sizeof(struct vlog_wire_header);
    switch (event->type) {
        case VLOG_EVENT_SINK_ADD:
        case VLOG_EVENT_SINK_REMOVE:
        case VLOG_EVENT_SINK_SET_LEVEL:
        case VLOG_EVENT_SINK_SET_OPTIONS:
        case VLOG_EVENT_SINK_CLEAR_OPTIONS:
            event->data.sink.handle = (FILE*)(uintptr_t)header->handle;
            event->data.sink.level = (enum vlog_level)header->level;
            event->data.sink.type = (enum vlog_sink_type)header->status;
            event->data.sink.options = header->options;
            break;
        case VLOG_EVENT_LOG:
            event->data.log.level = (enum vlog_level)header->level;
            event->data.log.err = header->err;
            event->data.log.tag = __wire_read_string(&cursor, &remaining, header->tag_length);
            event->data.log.message = __wire_read_string(&cursor, &remaining, header->message_length);
            if ((header->tag_length != VLOG_WIRE_NULL_LENGTH && event->data.log.tag == NULL) ||
                (header->message_length != VLOG_WIRE_NULL_LENGTH && event->data.log.message == NULL)) {
                __vlog_event_delete(event);
                return NULL;
            }
            break;
        case VLOG_EVENT_RESIZE:
            event->data.resize.columns = header->columns;
            break;
        case VLOG_EVENT_VIEW_OPEN:
            event->data.view_open.handle = (FILE*)(uintptr_t)header->handle;
            event->data.view_open.header = __wire_read_string(&cursor, &remaining, header->header_length);
            event->data.view_open.footer = __wire_read_string(&cursor, &remaining, header->footer_length);
            if ((header->header_length != VLOG_WIRE_NULL_LENGTH && event->data.view_open.header == NULL) ||
                (header->footer_length != VLOG_WIRE_NULL_LENGTH && event->data.view_open.footer == NULL)) {
                __vlog_event_delete(event);
                return NULL;
            }
            break;
        case VLOG_EVENT_VIEW_CLOSE:
            event->data.view_close.handle = (FILE*)(uintptr_t)header->handle;
            break;
        case VLOG_EVENT_STEP_OPEN:
            event->data.step_open.step_id = header->step_id;
            event->data.step_open.label = __wire_read_string(&cursor, &remaining, header->label_length);
            if (header->label_length != VLOG_WIRE_NULL_LENGTH && event->data.step_open.label == NULL) {
                __vlog_event_delete(event);
                return NULL;
            }
            break;
        case VLOG_EVENT_STEP_UPDATE:
            event->data.step_update.step_id = header->step_id;
            event->data.step_update.status = (enum vlog_content_status_type)header->status;
            event->data.step_update.message = __wire_read_string(&cursor, &remaining, header->message_length);
            if (header->message_length != VLOG_WIRE_NULL_LENGTH && event->data.step_update.message == NULL) {
                __vlog_event_delete(event);
                return NULL;
            }
            break;
        case VLOG_EVENT_STEP_CLOSE:
            event->data.step_close.step_id = header->step_id;
            event->data.step_close.status = (enum vlog_content_status_type)header->status;
            event->data.step_close.message = __wire_read_string(&cursor, &remaining, header->message_length);
            if (header->message_length != VLOG_WIRE_NULL_LENGTH && event->data.step_close.message == NULL) {
                __vlog_event_delete(event);
                return NULL;
            }
            break;
        case VLOG_EVENT_BARRIER:
            event->data.barrier.state = (struct vlog_barrier_state*)(uintptr_t)header->barrier_state;
            break;
        default:
            break;
    }

    if (remaining != 0) {
        __vlog_event_delete(event);
        errno = EINVAL;
        return NULL;
    }
    return event;
}

static struct vlog_event* __renderer_recv_event(struct vlog_renderer* renderer, const struct timespec* deadline)
{
    unsigned char* buffer;
    struct vlog_event* event;
    size_t             length;
    int                status;

    buffer = malloc(VLOG_PIPE_MESSAGE_MAX);
    if (buffer == NULL) {
        return NULL;
    }

    status = vlog_pipe_recv(&renderer->pipe, buffer, VLOG_PIPE_MESSAGE_MAX, &length, deadline);
    if (status <= 0) {
        free(buffer);
        return NULL;
    }

    event = __renderer_decode_event(buffer, length);
    free(buffer);
    return event;
}

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
            
            event = __renderer_recv_event(renderer, &ts);
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
        } while (event != NULL && renderer->running == 1);

        if (renderer->running != 1) {
            break;
        }

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
    if (g_renderer.running) {
        return 0;
    }

    memset(&g_renderer, 0, sizeof(struct vlog_renderer));
    g_renderer.owner_process_id = __current_process_id();
    g_renderer.running = 1;

    if (mtx_init(&g_renderer.write_lock, mtx_plain) != thrd_success) {
        g_renderer.running = 0;
        fprintf(stderr, "vlog: failed to initialize renderer write lock\n");
        return -1;
    }

    // initialize the event pipe before spawning the renderer thread
    if (vlog_pipe_open(&g_renderer.pipe) != 0) {
        g_renderer.running = 0;
        mtx_destroy(&g_renderer.write_lock);
        fprintf(stderr, "vlog: failed to initialize event pipe\n");
        return -1;
    }

    // spawn the renderer thread
    if (thrd_create(&g_renderer.tid, __renderer_main, &g_renderer) != thrd_success) {
        g_renderer.running = 0;
        vlog_pipe_close(&g_renderer.pipe);
        mtx_destroy(&g_renderer.write_lock);
        fprintf(stderr, "vlog: failed to start renderer thread\n");
        return -1;
    }
    return 0;
}

void vlog_renderer_stop(void)
{
    if (!vlog_renderer_is_owner()) {
        return;
    }

    if (g_renderer.running) {
        int                res;
        struct vlog_event* event = __vlog_event_new(VLOG_EVENT_SHUTDOWN);
        if (event != NULL) {
            vlog_renderer_push_event(event);
        } else {
            g_renderer.running = 0;
        }
        thrd_join(g_renderer.tid, &res);
    }

    // clean resources
    vlog_pipe_close(&g_renderer.pipe);
    mtx_destroy(&g_renderer.write_lock);
    memset(&g_renderer, 0, sizeof(struct vlog_renderer));
}

int vlog_renderer_is_owner(void)
{
    return g_renderer.owner_process_id == __current_process_id();
}

void vlog_renderer_resize(void)
{
#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    g_renderer.resize = 1;
#endif
}

void vlog_renderer_push_event(struct vlog_event* event)
{
    if (event == NULL) {
        return;
    }

    __renderer_send_event(&g_renderer, event);
    __vlog_event_delete(event);
}
