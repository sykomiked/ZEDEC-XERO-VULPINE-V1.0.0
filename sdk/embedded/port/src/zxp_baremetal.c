/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_baremetal.c — the port for a single-threaded super-loop (no RTOS).
 *
 * Rules: every libzxv / zxe call comes from the main loop, never from an
 * interrupt handler. Build with -DZXP_NO_TLS. A mutex is a busy flag: a
 * lock that finds it taken means re-entry (an interrupt calling in, or a
 * callback recursing) and is fatal rather than a silent corruption.
 * The integrator supplies the clock (zxp_baremetal_set_clock) and, as on
 * every port, the entropy source. Allocation defaults to the C library's
 * malloc (newlib: _sbrk); install a fixed arena with zxp_set_allocator. */
#include <stdlib.h>

#include "zxp.h"
#include "zxp_baremetal.h"
#include "zxp_os.h"

#ifndef ZXP_NO_TLS
#    error "the bare-metal port is single-threaded: build with -DZXP_NO_TLS"
#endif

static uint64_t (*g_clock)(void);

void zxp_baremetal_set_clock(uint64_t (*monotonic_us)(void))
{
    g_clock = monotonic_us;
}

int zxp_os_mutex_create(zxp_mutex_t *m)
{
    m->os.words[0] = 0;
    return 0;
}

void zxp_os_mutex_delete(zxp_mutex_t *m)
{
    (void) m;
}

int zxp_os_mutex_lock(zxp_mutex_t *m)
{
    if (m->os.words[0]) zxp_fatal("re-entrant libzxv call (interrupt handler or recursion)");
    m->os.words[0] = 1;
    return 0;
}

int zxp_os_mutex_unlock(zxp_mutex_t *m)
{
    if (!m->os.words[0]) return -1;
    m->os.words[0] = 0;
    return 0;
}

void zxp_os_crit_enter(void)
{
}

void zxp_os_crit_exit(void)
{
}

void zxp_os_sleep_tick(void)
{
}

uint64_t zxp_os_monotonic_us(void)
{
    return g_clock ? g_clock() : 0;
}

void *zxp_os_task_self(void)
{
    return (void *) &g_clock; /* one thread of control */
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
    (void) why;
    for (;;) {
    }
}
