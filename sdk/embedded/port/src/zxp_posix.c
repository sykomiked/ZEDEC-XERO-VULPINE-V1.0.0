/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_posix.c — the port on a POSIX host (Linux, macOS): used by the host
 * tests and by Linux-class gateways that want the embedded build.
 * pthreads, CLOCK_MONOTONIC, libc malloc. Entropy is still opt-in: call
 * zxp_set_entropy(zxp_posix_entropy, NULL) before zxp_init(). */
#if defined(__linux__)
#    define _GNU_SOURCE 1 /* getrandom */
#elif defined(__APPLE__)
#    define _DARWIN_C_SOURCE 1
#endif
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#    include <sys/random.h>
#endif

#include "zxp.h"
#include "zxp_os.h"
#include "zxp_posix.h"

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(((zxp_mutex_t *) 0)->os),
               "raise ZXP_MUTEX_WORDS for this pthread_mutex_t");

static pthread_mutex_t *native(zxp_mutex_t *m)
{
    return (pthread_mutex_t *) (void *) &m->os;
}

int zxp_os_mutex_create(zxp_mutex_t *m)
{
    return pthread_mutex_init(native(m), NULL);
}

void zxp_os_mutex_delete(zxp_mutex_t *m)
{
    pthread_mutex_destroy(native(m));
}

int zxp_os_mutex_lock(zxp_mutex_t *m)
{
    return pthread_mutex_lock(native(m));
}

int zxp_os_mutex_unlock(zxp_mutex_t *m)
{
    return pthread_mutex_unlock(native(m));
}

static pthread_mutex_t g_crit = PTHREAD_MUTEX_INITIALIZER;

void zxp_os_crit_enter(void)
{
    pthread_mutex_lock(&g_crit);
}

void zxp_os_crit_exit(void)
{
    pthread_mutex_unlock(&g_crit);
}

void zxp_os_sleep_tick(void)
{
    struct timespec ts = {0, 1000000L}; /* 1 ms */
    nanosleep(&ts, NULL);
}

uint64_t zxp_os_monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000u + (uint64_t) ts.tv_nsec / 1000u;
}

void *zxp_os_task_self(void)
{
    return (void *) pthread_self();
}

void *zxp_os_alloc(void *user, size_t n)
{
    (void) user;
    return malloc(n);
}

void zxp_os_free(void *user, void *p)
{
    (void) user;
    free(p);
}

void zxp_os_halt(const char *why)
{
    fprintf(stderr, "zxp: fatal: %s\n", why);
    fflush(stderr);
    abort();
}

int zxp_posix_entropy(void *user, uint8_t *buf, size_t len)
{
    (void) user;
    size_t got = 0;
    while (got < len) {
#if defined(__linux__)
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t) r;
#else
        size_t n = len - got > 256 ? 256 : len - got;
        if (getentropy(buf + got, n) != 0) return -1;
        got += n;
#endif
    }
    return 0;
}
