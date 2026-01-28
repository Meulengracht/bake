/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

static FILE*            g_log_file = NULL;
static pid1_log_level_t g_log_level = PID1_LOG_INFO;
static int              g_log_initialized = 0;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
static CRITICAL_SECTION g_log_lock;
#else
#include <pthread.h>
static pthread_mutex_t  g_log_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static const char* __level_to_string(pid1_log_level_t level)
{
    switch (level) {
        case PID1_LOG_DEBUG:   return "DEBUG";
        case PID1_LOG_INFO:    return "INFO";
        case PID1_LOG_WARNING: return "WARN";
        case PID1_LOG_ERROR:   return "ERROR";
        case PID1_LOG_FATAL:   return "FATAL";
        default:               return "UNKNOWN";
    }
}

static void __get_timestamp(char* buffer, size_t size)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timeval tv;
    struct tm tm_info;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_info);
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             tv.tv_usec / 1000);
#endif
}

int pid1_log_init(const char* log_path, pid1_log_level_t level)
{
    if (g_log_initialized) {
        return 0; // Already initialized
    }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    InitializeCriticalSection(&g_log_lock);
#endif

    g_log_level = level;

    if (log_path != NULL) {
        g_log_file = fopen(log_path, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "pid1_log_init: failed to open log file: %s\n", log_path);
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            DeleteCriticalSection(&g_log_lock);
#endif
            return -1;
        }
        // Enable line buffering for better real-time visibility
        setvbuf(g_log_file, NULL, _IOLBF, 0);
    } else {
        g_log_file = stderr;
    }

    g_log_initialized = 1;
    return 0;
}

void pid1_logv(pid1_log_level_t level, const char* format, va_list args)
{
    if (!g_log_initialized || g_log_file == NULL) {
        return;
    }

    if (level < g_log_level) {
        return;
    }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    EnterCriticalSection(&g_log_lock);
#else
    pthread_mutex_lock(&g_log_lock);
#endif

    char timestamp[32];
    __get_timestamp(timestamp, sizeof(timestamp));

    // Format: [timestamp] [LEVEL] message
    fprintf(g_log_file, "[%s] [%s] ", timestamp, __level_to_string(level));
    vfprintf(g_log_file, format, args);
    fflush(g_log_file);

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    LeaveCriticalSection(&g_log_lock);
#else
    pthread_mutex_unlock(&g_log_lock);
#endif
}

void pid1_log(pid1_log_level_t level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    pid1_logv(level, format, args);
    va_end(args);
}

void pid1_log_close(void)
{
    if (!g_log_initialized) {
        return;
    }

    if (g_log_file != NULL && g_log_file != stderr) {
        fclose(g_log_file);
    }

    g_log_file = NULL;
    g_log_initialized = 0;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    DeleteCriticalSection(&g_log_lock);
#endif
}
