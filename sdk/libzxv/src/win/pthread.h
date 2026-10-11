/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* src/win/pthread.h — the few pthread calls libzxv makes, on Windows'
 * own slim locks (SRWLOCK) and condition variables. The Makefile puts this
 * folder on the include path for Windows targets only, so the DLL needs no
 * winpthreads runtime and zig's MinGW (which has none) can build it.
 *
 * libzxv only uses default (non-recursive) mutexes, a static initializer,
 * and one condition variable waited on in a loop; nothing else is mapped.
 * An SRWLOCK needs no destruction and its init cannot fail. */
#ifndef ZXV_WIN_PTHREAD_H
#define ZXV_WIN_PTHREAD_H

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef int pthread_mutexattr_t;
typedef int pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    (void) a;
    InitializeSRWLock(m);
    return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void) m;
    return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    AcquireSRWLockExclusive(m);
    return 0;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    ReleaseSRWLockExclusive(m);
    return 0;
}
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    (void) a;
    InitializeConditionVariable(c);
    return 0;
}
static inline int pthread_cond_destroy(pthread_cond_t *c)
{
    (void) c;
    return 0;
}
static inline int pthread_cond_broadcast(pthread_cond_t *c)
{
    WakeAllConditionVariable(c);
    return 0;
}
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : (int) GetLastError();
}

#endif
