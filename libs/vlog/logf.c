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
#include <signal.h>
#include <time.h>
#include <vlog.h>

#define __VLOG_RESET_CURSOR "\r"
#define __VLOG_CLEAR_LINE "\x1b[2K"
#define __VLOG_CLEAR_TOCURSOR "\x1b[0J"
#define __VLOG_MOVEUP_CURSOR "\x1b[1A"
#define __VLOG_MOVEUP_CURSOR_FMT "\x1b[%iF"

#define VLOG_MAX_OUTPUTS 4

struct vlog_output {
    FILE*           handle;
    enum vlog_level level;
    unsigned int    options;
    int             columns;
    int             lastRowCount;
};

struct vlog_context {
    struct vlog_output outputs[VLOG_MAX_OUTPUTS];
    int                outputs_count;
    enum vlog_level    default_level;
};

static struct vlog_context g_vlog = { { NULL, 0 } , 0, VLOG_LEVEL_DISABLED };
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
    signal(SIGWINCH, __winch_handler);
}
#endif

void vlog_initialize(void)
{
#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
    // register the handler that will update the terminal stats correctly
    // once the user resizes the terminal
    signal(SIGWINCH, __winch_handler);
#endif
}

void vlog_cleanup(void)
{
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

int vlog_add_output(FILE* output)
{
    if (g_vlog.outputs_count == 4) {
        errno = ENOSPC;
        return -1;
    }

    g_vlog.outputs[g_vlog.outputs_count].handle       = output;
    g_vlog.outputs[g_vlog.outputs_count].level        = g_vlog.default_level;
    g_vlog.outputs[g_vlog.outputs_count].options      = 0;
    g_vlog.outputs[g_vlog.outputs_count].lastRowCount = 0;
    if (output == stdout) {
        g_vlog.outputs[g_vlog.outputs_count].columns = __get_column_count();
    } else {
        g_vlog.outputs[g_vlog.outputs_count].columns = 0;
    }

    g_vlog.outputs_count++;
    return 0;
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

            // when clearing RETRACE we reset the row count
            if (flags & VLOG_OUTPUT_OPTION_RETRACE) {
                g_vlog.outputs[i].lastRowCount = 0;
            }
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
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        if (g_vlog.outputs[i].handle == output) {
            g_vlog.outputs[i].columns = columns;
            break;
        }
    }
}

void vlog_set_output_short_fmt(FILE* output)
{

}

void vlog_set_output_long_fmt(FILE* output)
{

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
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        struct vlog_output* output      = &g_vlog.outputs[i];
        int                 colsWritten = 0;

        // ensure level is appropriate for output
        if (level > output->level) {
            continue;
        }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        // update column count on output to stdout if on windows, we can
        // only poll
        if (output->handle == stdout) {
            output->columns = __get_column_count();
        }
#endif

        // Retrace only on non-error/warn
        if (level > VLOG_LEVEL_WARNING) {
            // output control-code if any, and provided row count is valid
            if ((output->options & VLOG_OUTPUT_OPTION_RETRACE) && output->lastRowCount > 0) {
                fprintf(output->handle,
                    __VLOG_MOVEUP_CURSOR_FMT __VLOG_CLEAR_TOCURSOR,
                    output->lastRowCount
                );
            }
        } else {
            output->lastRowCount = 0;
        }

        va_start(args, format);
        if (!(output->options & VLOG_OUTPUT_OPTION_NODECO)) {
            if (output->options & VLOG_OUTPUT_OPTION_LONGDECO) {
                colsWritten += fprintf(output->handle, "[%s] %s | %s | ", &dateTime[0], g_levelNamesLong[level], tag);
                if (level == VLOG_LEVEL_ERROR) {
                    colsWritten += fprintf(output->handle, "[e%i, %s] | ", errno, strerror(errno));
                }
            } else {
                if (level == VLOG_LEVEL_ERROR) {
                    colsWritten += fprintf(output->handle, "%s[%s%i, %s] ", tag, g_levelNamesShort[level], errno, strerror(errno));
                } else {
                    colsWritten += fprintf(output->handle, "%s[%s] ", tag, g_levelNamesShort[level]);
                }
            }
        }
        colsWritten += vfprintf(output->handle, format, args) - 1;
        va_end(args);

        // calculate the last printed row-count if we have columns configured
        if (level > VLOG_LEVEL_WARNING) {
            if ((output->options & VLOG_OUTPUT_OPTION_RETRACE) && output->columns > 0) {
                output->lastRowCount = (colsWritten + (output->columns - 1)) / output->columns;
            }
        }
    }
}

void vlog_flush(void)
{
    for (int i = 0; i < g_vlog.outputs_count; i++) {
        fflush(g_vlog.outputs[i].handle);
    }
}
