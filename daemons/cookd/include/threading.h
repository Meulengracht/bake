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

#ifndef __COOKD_THREADS_H__
#define __COOKD_THREADS_H__

#include "cookd-config.h"

#if defined(__clang__)
#define __TLS_VAR __thread
#elif defined(__GNUC__) || defined(__GNUG__)
#define __TLS_VAR __thread
#elif defined(_MSC_VER)
#define __TLS_VAR __declspec(thread)
#endif

#if defined(HAVE_C11_THREADS) || defined(__VALI__)
#include <threads.h>
#elif defined(HAVE_PTHREAD)
#include <pthread.h>

typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_t thrd_t;

#define thrd_success 0

#define mtx_plain NULL

#define mtx_init    pthread_mutex_init
#define mtx_destroy pthread_mutex_destroy
#define mtx_trylock pthread_mutex_trylock
#define mtx_lock    pthread_mutex_lock
#define mtx_unlock  pthread_mutex_unlock

#define cnd_init(cnd) pthread_cond_init(cnd, NULL)
#define cnd_destroy   pthread_cond_destroy
#define cnd_wait      pthread_cond_wait
#define cnd_signal    pthread_cond_signal
#define cnd_broadcast pthread_cond_broadcast

#define thrd_join(thr, ret)          pthread_join(thr, (void**)ret)
#define thrd_create(thrp, func, arg) pthread_create(thrp, NULL, (void * (*)(void *))func, arg)

#elif defined(_WIN32)
#include <windows.h>

typedef CRITICAL_SECTION mtx_t;
typedef CONDITION_VARIABLE cnd_t;
typedef HANDLE thrd_t;

#define thrd_success 0
#define thrd_error   -1

#define mtx_plain NULL
#define mtx_recursive NULL

static inline int mtx_init(mtx_t* mtx, void* unused) {
    InitializeCriticalSection(mtx);
    return thrd_success;
}

#define mtx_destroy DeleteCriticalSection

static inline int mtx_trylock(mtx_t* mtx) {
    BOOL status = TryEnterCriticalSection(mtx);
    return status == TRUE ? thrd_success : thrd_error;
}

static inline int mtx_lock(mtx_t* mtx) {
    EnterCriticalSection(mtx);
    return thrd_success;
}

static inline int mtx_unlock(mtx_t* mtx) {
    LeaveCriticalSection(mtx);
    return thrd_success;
}

static inline int cnd_init(cnd_t* cnd)
{
    InitializeConditionVariable(cnd);
    return thrd_success;
}

#define cnd_destroy WakeAllConditionVariable

static inline int cnd_signal(cnd_t* cnd)
{
    WakeConditionVariable(cnd);
    return thrd_success;
}

static inline int cnd_wait(cnd_t* cnd, mtx_t* mtx)
{
    BOOL status = SleepConditionVariableCS(cnd, mtx, INFINITE);
    return status == TRUE ? thrd_success : thrd_error;
}

static inline int thrd_create(thrd_t* thrp, int (*start)(void*), void* arg) {
    thrd_t thr = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start, arg, 0, NULL);
    if (thr == NULL) {
        return thrd_error;
    }
    *thrp = thr;
    return 0;
}

static inline int thrd_join(thrd_t thrp, int* exitCode) {
    BOOL status;
    WaitForSingleObject(thrp, INFINITE);
    status = GetExitCodeThread(thrp, (LPDWORD)exitCode);
    return status == TRUE ? thrd_success : thrd_error;
}
#else
#error "Undefined platform for threads"
#endif

#endif // !__GRACHT_THREADS_H__
