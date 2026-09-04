/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,½
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VLOG_PRIVATE_H__
#define __VLOG_PRIVATE_H__

#include <stdarg.h>
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

enum vlog_event_type {
    // Events that are emitted to the renderer
    VLOG_EVENT_SINK_ADD,
    VLOG_EVENT_SINK_REMOVE,
    VLOG_EVENT_SINK_SET_LEVEL,
    VLOG_EVENT_SINK_SET_OPTIONS,
    VLOG_EVENT_SINK_CLEAR_OPTIONS,

    // Events that are emitted to the sinks
    VLOG_EVENT_LOG,
    VLOG_EVENT_RESIZE,
    VLOG_EVENT_VIEW_OPEN,
    VLOG_EVENT_VIEW_CLOSE,
    VLOG_EVENT_STEP_OPEN,
    VLOG_EVENT_STEP_UPDATE,
    VLOG_EVENT_STEP_CLOSE,
    VLOG_EVENT_FLUSH,
    VLOG_EVENT_BARRIER,
    VLOG_EVENT_SHUTDOWN
};

struct vlog_barrier_state {
    mtx_t lock;
    cnd_t done;
    int   completed;
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
            char*           tag;
            char*           message;
            enum vlog_level level;
            int             err;
        } log;

        struct {
            int columns;
        } resize;

        struct {
            FILE* handle;
            char* header;
            char* footer;
        } view_open;

        struct {
            FILE* handle;
        } view_close;

        struct {
            unsigned int step_id;
            char* label;
        } step_open;

        struct {
            unsigned int step_id;
            enum vlog_content_status_type status;
            char* message;
        } step_update;

        struct {
            unsigned int step_id;
            enum vlog_content_status_type status;
            char* message;
        } step_close;

        struct {
            struct vlog_barrier_state* state;
        } barrier;
    } data;
    
    struct vlog_event* next;
};

extern char* vlog_strdup(const char* text);
extern char* vlog_vformat(const char* format, va_list args);

extern int vlog_renderer_start(void);
extern void vlog_renderer_stop(void);
extern void vlog_renderer_resize(void);
extern void vlog_renderer_push_event(struct vlog_event* event);

#endif // __VLOG_PRIVATE_H__
