/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_os.h — what one RTOS port implements (zxp_posix.c, zxp_freertos.c,
 * zxp_zephyr.c). zxp_core.c builds the public zxp.h API on top. Not public. */
#ifndef ZXP_OS_H
#define ZXP_OS_H

#include <stddef.h>
#include <stdint.h>

#include "zxp.h"

/* Native mutex in caller storage (sizeof m->os). 0 on success. */
int zxp_os_mutex_create(zxp_mutex_t *m);
void zxp_os_mutex_delete(zxp_mutex_t *m);
int zxp_os_mutex_lock(zxp_mutex_t *m);
int zxp_os_mutex_unlock(zxp_mutex_t *m);

/* A short global critical section (no blocking, no allocation inside). */
void zxp_os_crit_enter(void);
void zxp_os_crit_exit(void);

void zxp_os_sleep_tick(void);
uint64_t zxp_os_monotonic_us(void);
/* An identifier of the calling task/thread (never NULL in a task). */
void *zxp_os_task_self(void);

/* The port's default allocator. */
void *zxp_os_alloc(void *user, size_t n);
void zxp_os_free(void *user, void *p);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void zxp_os_halt(const char *why);

#endif /* ZXP_OS_H */
