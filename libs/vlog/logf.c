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

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

#define __VLOG_RESET_CURSOR "\r"
#define __VLOG_CLEAR_LINE "\x1b[2K"
#define __VLOG_CLEAR_TOCURSOR "\x1b[0J"
#define __VLOG_MOVEUP_CURSOR "\x1b[1A"
#define __VLOG_MOVEUP_CURSOR_FMT "\x1b[%iF"
#define __VLOG_MOVEDOWN_CURSOR "\x1b[1B"
#define __VLOG_MOVEDOWN_CURSOR_FMT "\x1b[%iE"

// Forward declarations
struct vlog_event;

struct vlog_sink {
    enum vlog_sink_type type;
    void (*emit)(struct vlog_sink*, const struct vlog_event*);
    void (*flush)(struct vlog_sink*);
    void (*destroy)(struct vlog_sink*);
};

struct vlog_text_sink {
    struct vlog_sink base;
    FILE* handle;
    enum vlog_level level;
    unsigned int options;
    int close;
};

struct vlog_tty_sink {
    struct vlog_sink base;
    FILE* handle;
    enum vlog_level level;
    unsigned int options;
    int close;

    int columns;
    int view_active;
    int rendered_rows;

    char* title;
    char* footer;

    struct vlog_tty_step* steps;
    size_t step_count;
    size_t step_capacity;

    unsigned long long active_step_id;
    long long spinner_time_ms;
    int spinner_index;

    const char* title;
    const char* footer;
    struct vlog_content_line* lines;
};

struct vlog_content_line {
    const char*                   prefix;
    enum vlog_content_status_type status;
    char                          buffer[1024];
};

enum vlog_event_type {
    // Events that are emitted to the renderer
    VLOG_EVENT_SINK_ADD,
    VLOG_EVENT_SINK_REMOVE,

    // Events that are emitted to the sinks
    VLOG_EVENT_LOG,
    VLOG_EVENT_RESIZE,
    VLOG_EVENT_VIEW_CREATE,
    VLOG_EVENT_VIEW_DESTROY,
    VLOG_EVENT_STEP_OPEN,
    VLOG_EVENT_STEP_UPDATE,
    VLOG_EVENT_STEP_CLOSE,
    VLOG_EVENT_FLUSH,
    VLOG_EVENT_SHUTDOWN
};

struct vlog_event {
    enum vlog_event_type type;
    struct timespec      timestamp;
    union {
        struct {
            FILE*               handle;
            enum vlog_sink_type type;
            enum vlog_level     level;
            unsigned int        options;
        } sink;

        struct {
            const char* tag;
            const char* message;
            enum vlog_level level;
        } log;

        struct {
            int columns;
        } resize;

        struct {
            unsigned int step_id;
            const char* prefix;
        } step_open;

        struct {
            unsigned int step_id;
            enum vlog_content_status_type status;
            const char* message;
        } step_update;

        struct {
            unsigned int step_id;
            int success;
        } step_close;
    } data;
    
    struct vlog_event* next;
};

struct bb_queue {
    mtx_t lock;
    cnd_t drained;
    cnd_t full;

    int                 capacity;
    int                 count;
    int                 qindex;
    int                 dqindex;
    struct vlog_event** items;
};

struct vlog_renderer {
    thrd_t             tid;
    volatile int       running;
    volatile int       index;
    volatile long long time;
    volatile int       update;
    struct bb_queue    events;
    struct vlog_sink** sinks;
    int                sinks_count; 
};

struct vlog_context {
    enum vlog_level      default_level;
    struct vlog_renderer renderer;

    // view information
    int         content_line_count;
    int         content_line_index;
};

static struct vlog_context g_vlog = { { NULL, 0 } };
static const char*         g_levelNamesShort[] = {
        "",
        "E",
        "W",
        "T",
        "D"
};
static const char*         g_levelNamesLong[] = {
        "",
        "error",
        "warning",
        "trace",
        "debug"
};

static const char*         g_statusNames[] = {
        "",
        "WAITING",
        "WORKING",
        "DONE",
        "FAILED"
};
static const char*         g_statusColor[] = {
        "\x1b[37m",
        "\x1b[90m",
        "\x1b[37m",
        "\x1b[32m",
        "\x1b[31m"
};
static const char*         g_animatorCharacter[] = {
        "|",
        "/",
        "-",
        "\\",
        "/",
        "-"
};

static struct vlog_sink* __get_output(FILE* handle);
static void __refresh_view(struct vlog_sink* output, int clear);

static void __bb_queue_init(struct bb_queue* queue, int capacity)
{
    memset(queue, 0, sizeof(struct bb_queue));
    queue->items = calloc(capacity, sizeof(struct vlog_event*));
    if (queue->items == NULL) {
        fprintf(stderr, "failed to allocate memory for queue\n");
        exit(1);
    }

    mtx_init(&queue->lock, mtx_plain);
    cnd_init(&queue->drained);
    cnd_init(&queue->full);
    queue->capacity = capacity;
}

static void __bb_queue_destroy(struct bb_queue* queue)
{
    if (queue->items) {
        free(queue->items);
        queue->items = NULL;
    }
    cnd_destroy(&queue->drained);
    cnd_destroy(&queue->full);
    mtx_destroy(&queue->lock);
}

static void __bb_queue_push(struct bb_queue* queue, struct vlog_event* event)
{
    mtx_lock(&queue->lock);
    while (queue->count == queue->capacity) {
        cnd_wait(&queue->full, &queue->lock);
    }

    queue->items[queue->qindex] = event;
    queue->qindex = (queue->qindex + 1) % queue->capacity;
    queue->count++;

    cnd_signal(&queue->drained);
    mtx_unlock(&queue->lock);
}

static struct vlog_event* __bb_queue_pop(struct bb_queue* queue, struct timespec* timeout)
{
    struct vlog_event* event = NULL;

    mtx_lock(&queue->lock);
    while (queue->count == 0) {
        if (cnd_timedwait(&queue->drained, &queue->lock, timeout) == thrd_timedout) {
            mtx_unlock(&queue->lock);
            return NULL;
        }
    }

    event = queue->items[queue->dqindex];
    queue->dqindex = (queue->dqindex + 1) % queue->capacity;
    queue->count--;

    cnd_signal(&queue->full);
    mtx_unlock(&queue->lock);

    return event;
}

static void __vlog_event_delete(struct vlog_event* event)
{
    if (event) {
        free(event);
    }
}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
int __get_column_count(void)
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
int __get_column_count(void)
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return (int)w.ws_col;
}

void __winch_handler(int sig)
{
    struct vlog_event* event;

    signal(SIGWINCH, SIG_IGN);

    // allocate a new event and push it to the renderer thread
    event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        fprintf(stderr, "vlog: failed to allocate memory for resize event\n");
        exit(EXIT_FAILURE);
    }

    event->type = VLOG_EVENT_RESIZE;
    event->data.resize.columns = __get_column_count();
    __bb_queue_push(&g_vlog.renderer.events, event);
    signal(SIGWINCH, __winch_handler);
}
#endif

static void __sink_add(struct vlog_renderer* renderer, struct vlog_event* event)
{
    struct vlog_sink* sink;

    sink = calloc(1, sizeof(struct vlog_sink));
    if (sink == NULL) {
        return;
    }

    sink->handle = output;
    sink->level = g_vlog.default_level;

    if (output == stdout) {
        sink->columns = __get_column_count();
    }

    if (close) {
        sink->options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    // Check for an open spot first
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i] == NULL) {
            renderer->sinks[i] = sink;
            return;
        }
    }

    // Otherwise grow the array
    renderer->sinks = realloc(
        renderer->sinks,
        (renderer->sinks_count + 1) * sizeof(struct vlog_sink*)
    );
    g_vlog.renderer.sinks[g_vlog.renderer.sinks_count++] = sink;
}

static void __sink_remove(struct vlog_renderer* renderer, struct vlog_event* event)
{
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i]->handle == event->data.sink.handle) {
            if (renderer->sinks[i]->options & VLOG_OUTPUT_OPTION_CLOSE) {
                fclose(renderer->sinks[i]->handle);
            }
            free(renderer->sinks[i]);
            renderer->sinks[i] = NULL;
            renderer->sinks_count--;
            break;
        }
    }
}

static int __renderer_main(void* context)
{
    struct vlog_renderer* renderer = context;
    struct vlog_sink*     output = __get_output(stdout);
    struct timespec       ts;
    struct vlog_event*    event;
    int                   updater = 0;

    renderer->running = 1;
    while (renderer->running == 1) {
        timespec_get(&ts, TIME_UTC);
        // wait for 100ms
        ts.tv_nsec += 100 * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        do {
            event = __bb_queue_pop(&renderer->events, &ts);
            if (event != NULL) {
                switch (event->type) {
                    case VLOG_EVENT_SINK_ADD:
                        __sink_add(renderer, event);
                        break;
                    case VLOG_EVENT_SINK_REMOVE:
                        __sink_remove(renderer, event);
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

        renderer->time += 100;
        updater++;
        if ((updater % 5) == 0) {
            renderer->index++;
        }
        if (renderer->update) {
            __refresh_view(output, 1);
        }
    }

    // Close all sinks
    for (int i = 0; i < renderer->sinks_count; i++) {
        if (renderer->sinks[i]->destroy) {
            renderer->sinks[i]->destroy(renderer->sinks[i]);
        }
        free(renderer->sinks[i]);
    }
    free(renderer->sinks);
    memset(renderer, 0, sizeof(struct vlog_renderer));
    return 0;
}

void vlog_initialize(enum vlog_level level)
{
    memset(&g_vlog, 0, sizeof(struct vlog_context));
    __bb_queue_init(&g_vlog.renderer.events, 128);

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
    }
}

static struct vlog_sink* __get_output(FILE* handle)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == handle) {
            return &g_vlog.outputs[i];
        }
    }
    return NULL;
}

void vlog_cleanup(void)
{
    if (g_vlog.renderer.running) {
        int res;
        g_vlog.renderer.running = 0;
        thrd_join(g_vlog.renderer.tid, &res);
    }
    memset(&g_vlog, 0, sizeof(struct vlog_context));
}

int vlog_sink_add_text(FILE* output, int close)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return -1;
    }

    event->type = VLOG_EVENT_SINK_ADD;
    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;
    event->data.sink.type = VLOG_SINK_TYPE_TEXT;
    if (close) {
        event->data.sink.options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    __bb_queue_push(&g_vlog.renderer.events, event);
    return 0;
}

int vlog_sink_add_view(FILE* output, int close)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return -1;
    }

    event->type = VLOG_EVENT_SINK_ADD;
    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;
    event->data.sink.type = VLOG_SINK_TYPE_VIEW;
    if (close) {
        event->data.sink.options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    __bb_queue_push(&g_vlog.renderer.events, event);
    return 0;
}

int vlog_sink_remove(FILE* output)
{
    // Create a new sink and push it to the renderer thread
    struct vlog_event* event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return -1;
    }

    event->type = VLOG_EVENT_SINK_REMOVE;
    event->data.sink.handle = output;
    event->data.sink.level = g_vlog.default_level;
    event->data.sink.options = 0;

    __bb_queue_push(&g_vlog.renderer.events, event);
    return 0;
}

void vlog_set_level(enum vlog_level level)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        g_vlog.outputs[i].level = level;
    }
    g_vlog.default_level = level;
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

void vlog_flush(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        fflush(g_vlog.outputs[i].handle);
    }
}

static void __render_line_with_text(struct vlog_sink* output, const char* embed, int lcorner, int middle, int rcorner)
{
    int columns = output->columns;
    int titleCount = embed != NULL ? ((int)strlen(embed) + 2) : 0;
    int lcount = 3;
    int rcount = columns - (titleCount + 2 + lcount);

    fprintf(output->handle, "%lc", lcorner);
    for (int i = 0; i < lcount; i++) fprintf(output->handle, "%lc", middle);
    if (embed != NULL) {
        fprintf(output->handle, " %s ", embed);
    }
    for (int i = 0; i < rcount; i++) fprintf(output->handle, "%lc", middle);
    fprintf(output->handle, "%lc\n", rcorner);
}

static void __fmt_indicator(char* buffer, enum vlog_content_status_type status)
{
    if (status == VLOG_CONTENT_STATUS_WORKING) {
        long long seconds = g_vlog.renderer.time / 1000;
        long long ms = (g_vlog.renderer.time % 1000) / 100;
        int index = g_vlog.renderer.index % 6;
        sprintf(buffer, "%s %lli.%llis", g_animatorCharacter[index], seconds, ms);
    } else {
        strcpy(buffer, g_statusNames[status]);
    }
}

static void __refresh_view(struct vlog_sink* output, int clear)
{
    char indicator[20] = { 0 };
    
    if (clear) {
        fprintf(output->handle, __VLOG_MOVEUP_CURSOR_FMT __VLOG_CLEAR_TOCURSOR, g_vlog.content_line_count + 2);
    }

    // print first line
    __render_line_with_text(output, g_vlog.title, 0x250D, 0x2500, 0x2511);

    // print content lines
    for (int i = 0; i < g_vlog.content_line_count; i++) {
        __fmt_indicator(&indicator[0], g_vlog.lines[i].status);
        fprintf(output->handle, "%lc %-10s %-*.*s %s%-10s%s%lc\n",
            0x2502,
            g_vlog.lines[i].prefix,
            output->columns - 25,
            output->columns - 25,
            &g_vlog.lines[i].buffer[0],
            g_statusColor[g_vlog.lines[i].status],
            &indicator[0],
            g_statusColor[0],
            0x2502
        );
    }

    // print final line
    __render_line_with_text(output, g_vlog.footer, 0x2515, 0x2500, 0x2519);

    fflush(output->handle);
}

void vlog_view_create(FILE* handle, const char* header, const char* footer)
{
    struct vlog_sink* output = __get_output(handle);

    // must be a terminal
    if (output == NULL || !isatty(fileno(handle))) {
        return;
    }

    // update stats
    g_vlog.title = header;
    g_vlog.footer = footer;
    g_vlog.content_line_count = 0;
    g_vlog.content_line_index = 0;
    g_vlog.lines = NULL;
}

void vlog_view_destroy(void)
{

}

void vlog_content_set_index(int index)
{
    if (index < 0 || index >= g_vlog.content_line_count) {
        return;
    }
    g_vlog.content_line_index = index;
}

void vlog_content_set_prefix(const char* prefix)
{
    g_vlog.lines[g_vlog.content_line_index].prefix = prefix;
}

void vlog_content_set_status(enum vlog_content_status_type status)
{
    g_vlog.lines[g_vlog.content_line_index].status = status;
    
    g_vlog.renderer.time = 0;
    g_vlog.renderer.index = 0;

    if (status == VLOG_CONTENT_STATUS_WORKING) {
        g_vlog.renderer.update = 1;
    } else {
        g_vlog.renderer.update = 0;
    }
}

void vlog_refresh(FILE* handle)
{
    struct vlog_sink* output = __get_output(handle);
    __refresh_view(output, 1);
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list            args;
    struct vlog_event* event;

    // use __bb_queue_push to push the event to the renderer thread
    event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return;
    }

    event->type = VLOG_EVENT_LOG;
    event->data.log.level = level;
    event->data.log.tag = tag;
    
    va_start(args, format);
    vasprintf(&event->data.log.message, format, args);
    va_end(args);

    __bb_queue_push(&g_vlog.renderer.events, event);
}

static void __text_write(struct vlog_sink* sink, enum vlog_level level, const char* tag, const char* message)
{
    char       dateTime[32];
    time_t     now;
    struct tm* timeInfo;

    // ensure level is appropriate for sink
    if (sink->handle == NULL || level > sink->level) {
        return;
    }

    time(&now);
    timeInfo = localtime(&now);

    strftime(&dateTime[0], sizeof(dateTime) - 1, "%F %T", timeInfo);

    if (sink->options & VLOG_OUTPUT_OPTION_PROGRESS) {
        fprintf(sink->handle, __VLOG_CLEAR_LINE __VLOG_RESET_CURSOR);
    }

    if (!(sink->options & VLOG_OUTPUT_OPTION_NODECO)) {
        if (sink->options & VLOG_OUTPUT_OPTION_LONGDECO) {
            fprintf(sink->handle, "[%s] %s | %s | ", &dateTime[0], g_levelNamesLong[level], tag);
            if (level == VLOG_LEVEL_ERROR) {
                fprintf(sink->handle, "[e%i, %s] | ", errno, strerror(errno));
            }
        } else {
            if (level == VLOG_LEVEL_ERROR) {
                fprintf(sink->handle, "%s[%s%i, %s] ", tag, g_levelNamesShort[level], errno, strerror(errno));
            } else {
                fprintf(sink->handle, "%s[%s] ", tag, g_levelNamesShort[level]);
            }
        }
    }

    fprintf(sink->handle, "%s", message);
    fflush(sink->handle);
}

static void __text_emit(struct vlog_sink* sink, struct vlog_event* event)
{
    switch (event->type) {
        case VLOG_EVENT_LOG:
            __text_write(sink, event->data.log.level, event->data.log.tag, event->data.log.message);
            break;
        case VLOG_EVENT_RESIZE:
            sink->columns = event->data.resize.columns;
            break;
        default:
            break;
    }
}

static void __view_write(struct vlog_sink* sink, enum vlog_level level, const char* message)
{
    // ensure level is appropriate for sink
    if (sink->handle == NULL || level > sink->level) {
        return;
    }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // update column count on sink to stdout if on windows, we can
    // only poll
    sink->columns = __get_column_count();
#endif
    strncpy(
        &g_vlog.lines[g_vlog.content_line_index].buffer[0],
        message,
        sizeof(g_vlog.lines[g_vlog.content_line_index].buffer) - 1
    );

    // strip the newlines
    for (int j = 0; g_vlog.lines[g_vlog.content_line_index].buffer[j]; j++) {
        if (g_vlog.lines[g_vlog.content_line_index].buffer[j] == '\n') {
            g_vlog.lines[g_vlog.content_line_index].buffer[j] = ' ';
        }
    }
}

static void __view_emit(struct vlog_sink* sink, struct vlog_event* event)
{
    switch (event->type) {
        case VLOG_EVENT_LOG:
            __view_write(sink, event->data.log.level, event->data.log.message);
            break;
        case VLOG_EVENT_RESIZE:
            sink->columns = event->data.resize.columns;
            break;
        case VLOG_EVENT_STEP_OPEN:
            vlog_content_set_index(event->data.step_open.step_id);
            vlog_content_set_prefix(event->data.step_open.prefix);
            vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);
            break;
        case VLOG_EVENT_STEP_UPDATE:
            vlog_content_set_index(event->data.step_update.step_id);
            vlog_content_set_status(event->data.step_update.status);
            __view_write(sink, VLOG_LEVEL_TRACE, event->data.step_update.message);
            break;
        case VLOG_EVENT_STEP_CLOSE:
            vlog_content_set_index(event->data.step_close.step_id);
            vlog_content_set_status(event->data.step_close.success ? VLOG_CONTENT_STATUS_DONE
                                                                  : VLOG_CONTENT_STATUS_FAILED);
            break;
        default:
            break;
    }
    __refresh_view(sink, 1);
}

void vlog_step_init(struct vlog_step* step, int index, const char* prefix)
{
    if (!step) {
        return;
    }

    step->index = index;
    step->prefix = prefix;

    vlog_content_set_index(index);
    vlog_content_set_prefix(prefix);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);
}

void vlog_step_begin(struct vlog_step* step)
{
    if (!step) {
        return;
    }

    vlog_content_set_index(step->index);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
}

void vlog_step_end(struct vlog_step* step, int success)
{
    if (!step) {
        return;
    }

    vlog_content_set_index(step->index);
    vlog_content_set_status(success ? VLOG_CONTENT_STATUS_DONE
                                    : VLOG_CONTENT_STATUS_FAILED);
}

void vlog_step_fail(struct vlog_step* step)
{
    vlog_step_end(step, 0);
}

