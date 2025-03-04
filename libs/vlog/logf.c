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

#define __VLOG_RESET_CURSOR "\r"
#define __VLOG_CLEAR_LINE "\x1b[2K"
#define __VLOG_CLEAR_TOCURSOR "\x1b[0J"
#define __VLOG_MOVEUP_CURSOR "\x1b[1A"
#define __VLOG_MOVEUP_CURSOR_FMT "\x1b[%iF"
#define __VLOG_MOVEDOWN_CURSOR "\x1b[1B"
#define __VLOG_MOVEDOWN_CURSOR_FMT "\x1b[%iE"

#define VLOG_MAX_OUTPUTS 4

struct vlog_output {
    FILE*           handle;
    enum vlog_level level;
    unsigned int    options;
    int             columns;
};

struct vlog_content_line {
    const char*                   prefix;
    enum vlog_content_status_type status;
    char                          buffer[1024];
};

struct vlog_context {
    struct vlog_output outputs[VLOG_MAX_OUTPUTS];
    int                outputs_count;
    enum vlog_level    default_level;
    thrd_t             animator_tid;
    volatile int       animator_running;
    volatile int       animator_index;
    volatile long long animator_time;
    volatile int       animator_update;

    // view information
    mtx_t       lock;
    const char* title;
    const char* footer;
    int         view_enabled;
    int         content_line_count;
    int         content_line_index;
    struct vlog_content_line* lines;
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

static struct vlog_output* __get_output(FILE* handle);
static void __refresh_view(struct vlog_output* output, int clear);

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
    signal(SIGWINCH, SIG_IGN);
    vlog_set_output_width(stdout, __get_column_count());
    __refresh_view(__get_output(stdout), 1);
    signal(SIGWINCH, __winch_handler);
}
#endif

static int __animator_loop(void* context)
{
    struct vlog_output* output = __get_output(stdout);
    int updater = 0;
    (void)context;

    g_vlog.animator_running = 1;
    while (g_vlog.animator_running == 1) {
        thrd_sleep(&(struct timespec){.tv_nsec=100 * 1000000}, NULL);
        g_vlog.animator_time += 100;
        
        updater++;
        if ((updater % 5) == 0) {
            g_vlog.animator_index++;
        }
        if (g_vlog.animator_update) {
            __refresh_view(output, 1);
        }
    }
    g_vlog.animator_running = 0;
    return 0;
}

void vlog_initialize(enum vlog_level level)
{
    memset(&g_vlog, 0, sizeof(struct vlog_context));
    mtx_init(&g_vlog.lock, mtx_plain);

    // start by initializing locale
    setlocale(LC_ALL, "");

    // set default output level
    vlog_set_level(level);

    // add stdout by default
    vlog_add_output(stdout, 0);

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    // register the handler that will update the terminal stats correctly
    // once the user resizes the terminal
    signal(SIGWINCH, __winch_handler);
#endif
}

static struct vlog_output* __get_output(FILE* handle)
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
    if (g_vlog.animator_running) {
        // do not wait more than 2s, otherwise just shutdown
        size_t maxWaiting = 2000;
        g_vlog.animator_running = 2;
        while (g_vlog.animator_running && maxWaiting > 0) {
            thrd_sleep(&(struct timespec){.tv_nsec=100 * 1000000}, NULL);
            maxWaiting -= 100;
        }
    }

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

int vlog_add_output(FILE* output, int close)
{
    if (g_vlog.outputs_count == 4) {
        errno = ENOSPC;
        return -1;
    }

    g_vlog.outputs[g_vlog.outputs_count].handle       = output;
    g_vlog.outputs[g_vlog.outputs_count].level        = g_vlog.default_level;
    g_vlog.outputs[g_vlog.outputs_count].options      = 0;
    if (output == stdout) {
        g_vlog.outputs[g_vlog.outputs_count].columns = __get_column_count();
    } else {
        g_vlog.outputs[g_vlog.outputs_count].columns = 0;
    }

    if (close) {
        g_vlog.outputs[g_vlog.outputs_count].options |= VLOG_OUTPUT_OPTION_CLOSE;
    }

    g_vlog.outputs_count++;
    return 0;
}

int vlog_remove_output(FILE* output)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].handle = NULL;
            g_vlog.outputs_count--;
            break;
        }
    }
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

void vlog_set_output_width(FILE* output, int columns)
{
    // must be a terminal
    if (!isatty(fileno(output))) {
        return;
    }

    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].columns = columns;
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

static void __render_line_with_text(struct vlog_output* output, const char* embed, int lcorner, int middle, int rcorner)
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
        long long seconds = g_vlog.animator_time / 1000;
        long long ms = (g_vlog.animator_time % 1000) / 100;
        int index = g_vlog.animator_index % 6;
        sprintf(buffer, "%s %lli.%llis", g_animatorCharacter[index], seconds, ms);
    } else {
        strcpy(buffer, g_statusNames[status]);
    }
}

static void __refresh_view(struct vlog_output* output, int clear)
{
    char indicator[20] = { 0 };

    if (!g_vlog.view_enabled) {
        return;
    }
    
    if (mtx_trylock(&g_vlog.lock) != thrd_success) {
        return;
    }

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
    mtx_unlock(&g_vlog.lock);
}

void vlog_start(FILE* handle, const char* header, const char* footer, int contentLineCount)
{
    struct vlog_output* output = __get_output(handle);

    // must be a terminal
    if (output == NULL || !isatty(fileno(handle))) {
        return;
    }

    // update stats
    g_vlog.title = header;
    g_vlog.footer = footer;
    g_vlog.content_line_count = contentLineCount;
    g_vlog.content_line_index = 0;
    g_vlog.lines = calloc(contentLineCount, sizeof(struct vlog_content_line));
    g_vlog.view_enabled = 1;

    // must not already be started
    if (!g_vlog.animator_running) {
        // spawn the animator thread
        if (thrd_create(&g_vlog.animator_tid, __animator_loop, NULL) != thrd_success) {
            VLOG_ERROR("logv", "failed to spawn thread for animation\n");
        }
    }

    // refresh view
    __refresh_view(output, 0);
}

void vlog_end(void)
{
    // disable
    g_vlog.view_enabled = 0;
}

void vlog_content_set_index(int index)
{
    if (!g_vlog.view_enabled) {
        return;
    }

    if (index < 0 || index >= g_vlog.content_line_count) {
        return;
    }
    g_vlog.content_line_index = index;
}

void vlog_content_set_prefix(const char* prefix)
{
    if (!g_vlog.view_enabled) {
        return;
    }

    g_vlog.lines[g_vlog.content_line_index].prefix = prefix;
}

void vlog_content_set_status(enum vlog_content_status_type status)
{
    if (!g_vlog.view_enabled) {
        return;
    }
    
    g_vlog.lines[g_vlog.content_line_index].status = status;
    
    g_vlog.animator_time = 0;
    g_vlog.animator_index = 0;

    if (status == VLOG_CONTENT_STATUS_WORKING) {
        g_vlog.animator_update = 1;
    } else {
        g_vlog.animator_update = 0;
    }
}

void vlog_refresh(FILE* handle)
{
    struct vlog_output* output = __get_output(handle);
    __refresh_view(output, 1);
}

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    va_list    args;
    char       dateTime[32];
    time_t     now;
    struct tm* timeInfo;

    if (!g_vlog.outputs_count) {
        return;
    }

    time(&now);
    timeInfo = localtime(&now);

    strftime(&dateTime[0], sizeof(dateTime) - 1, "%F %T", timeInfo);
    for (int i = 0; i < VLOG_MAX_OUTPUTS; i++) {
        struct vlog_output* output      = &g_vlog.outputs[i];
        int                 colsWritten = 0;

        // ensure level is appropriate for output
        if (output->handle == NULL || level > output->level) {
            continue;
        }

        // if the output is a tty we handle it differently, unless vlog_start
        // was not configured
        if (g_vlog.view_enabled && isatty(fileno(output->handle))) {
            char* nl;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            // update column count on output to stdout if on windows, we can
            // only poll
            output->columns = __get_column_count();
#endif
            va_start(args, format);
            vsprintf(
                &g_vlog.lines[g_vlog.content_line_index].buffer[0],
                format, args
            );
            va_end(args);
            
            // strip the newlines
            for (int j = 0; g_vlog.lines[g_vlog.content_line_index].buffer[j]; j++) {
                if (g_vlog.lines[g_vlog.content_line_index].buffer[j] == '\n') {
                    g_vlog.lines[g_vlog.content_line_index].buffer[j] = ' ';
                }
            }
            __refresh_view(output, 1);
            continue;
        } else {
            if (output->options & VLOG_OUTPUT_OPTION_PROGRESS) {
                fprintf(output->handle, __VLOG_CLEAR_LINE __VLOG_RESET_CURSOR);
            }
        }
        
        if (!(output->options & VLOG_OUTPUT_OPTION_NODECO)) {
            if (output->options & VLOG_OUTPUT_OPTION_LONGDECO) {
                fprintf(output->handle, "[%s] %s | %s | ", &dateTime[0], g_levelNamesLong[level], tag);
                if (level == VLOG_LEVEL_ERROR) {
                    fprintf(output->handle, "[e%i, %s] | ", errno, strerror(errno));
                }
            } else {
                if (level == VLOG_LEVEL_ERROR) {
                    fprintf(output->handle, "%s[%s%i, %s] ", tag, g_levelNamesShort[level], errno, strerror(errno));
                } else {
                    fprintf(output->handle, "%s[%s] ", tag, g_levelNamesShort[level]);
                }
            }
        }

        va_start(args, format);
        vfprintf(output->handle, format, args) - 1;
        va_end(args);
        fflush(output->handle);
    }
}
