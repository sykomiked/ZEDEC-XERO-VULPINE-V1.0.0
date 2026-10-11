/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp.h — the ZXV embedded port layer: the few operating-system services
 * libzxv needs, behind one small interface that a firmware project
 * implements once per RTOS.
 *
 *   mutex      zxp_mutex_*        (libzxv's pthread mutexes map here)
 *   condition  zxp_cond_*         (only rmag listener removal waits on one)
 *   entropy    zxp_set_entropy    THE INTEGRATOR'S RESPONSIBILITY: no
 *                                 default source on any RTOS port
 *   time       zxp_monotonic_us   (for the phase-clock driver, zxe_clock)
 *   allocator  zxp_set_allocator, zxp_set_heap_limit, zxp_heap_stats
 *   fatal      zxp_fatal          (fail closed: never returns)
 *
 * Ports: src/zxp_posix.c (host, tests), src/zxp_freertos.c,
 * src/zxp_zephyr.c. The OS-independent part is src/zxp_core.c.
 *
 * FAIL CLOSED. Until zxp_init() has succeeded with a configured, healthy
 * entropy source, zxp_mutex_init() refuses, so zxv_ctx_create() (and every
 * libzxv handle that needs a lock) fails with ZXV_E_INTERNAL, and
 * zxv_random() fails with ZXV_E_IO. Pure functions (rationals, four-valued
 * logic, the hexagram tag, hashes, wire frames) need no lock and keep
 * working. A failed entropy health test latches: the port stays closed
 * until reset.
 *
 * No floating point anywhere in this layer. */
#ifndef ZXP_H
#define ZXP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes (0 or negative). */
#define ZXP_OK              0
#define ZXP_E_ARG          (-1)
#define ZXP_E_NO_ENTROPY   (-2) /* zxp_set_entropy was never called        */
#define ZXP_E_ENTROPY      (-3) /* the source failed or its health test did */
#define ZXP_E_STATE        (-4) /* zxp_init not done, or done twice         */
#define ZXP_E_NOMEM        (-5)
#define ZXP_E_OS           (-6) /* the RTOS refused (mutex, ...)            */

/* ---- Initialisation ---------------------------------------------------- */

/* Board entropy source: fill `buf` with `len` bytes from a true random
 * number generator (or a DRBG properly seeded from one). Return 0 on
 * success, non-zero on any failure (the port then fails closed).
 * Called with no ZXV lock held, from any task that needs randomness. */
typedef int (*zxp_entropy_fn)(void *user, uint8_t *buf, size_t len);

/* Set the entropy source. Must be called before zxp_init(); a second call
 * after zxp_init() is ZXP_E_STATE (the source cannot be swapped silently). */
int zxp_set_entropy(zxp_entropy_fn fn, void *user);

/* Bring the port up: check that an entropy source is configured and run a
 * start-up health test on it (two 32-byte reads; refused if either read is
 * one repeated byte or the two reads are equal). This catches a missing or
 * stuck source, not a weak one: it is not an SP 800-90B assessment.
 * Returns ZXP_OK, ZXP_E_NO_ENTROPY, ZXP_E_ENTROPY or ZXP_E_STATE. */
int zxp_init(void);

/* 1 once zxp_init() has succeeded and no entropy failure has latched. */
int zxp_ready(void);

/* Human-readable name of a ZXP_* status (static ASCII). */
const char *zxp_status_name(int status);

/* ---- Entropy ----------------------------------------------------------- */

/* Fill buf from the configured source, with a continuous test (a 16-byte
 * block equal to the previous call's first block fails and latches).
 * Returns ZXP_OK or a negative ZXP_E_*; never returns partial success. */
int zxp_entropy_fill(uint8_t *buf, size_t len);

/* libzxv's zxv_random() calls this through the build's getentropy mapping
 * (port/compat/zxp_prelude.h): 0 on success, -1 on failure. */
int zxp_getentropy(void *buf, size_t len);

/* ---- Allocator --------------------------------------------------------- */

typedef struct {
    void *(*alloc)(void *user, size_t n); /* NULL on failure; 8-byte aligned or better */
    void (*free)(void *user, void *p);
    void *user;
} zxp_allocator;

/* Replace the port's default allocator (before zxp_init). NULL restores the
 * default. Every libzxv allocation in the embedded build comes through here
 * (malloc, calloc, realloc, free and strdup are mapped by the prelude). */
int zxp_set_allocator(const zxp_allocator *a);

/* A hard ceiling on bytes live through zxp_* allocations (0 = none). An
 * allocation that would pass it fails, and libzxv reports ZXV_E_NOMEM. */
void zxp_set_heap_limit(size_t bytes);

typedef struct {
    size_t live_bytes;   /* requested bytes currently allocated            */
    size_t peak_bytes;   /* high-water mark of live_bytes                  */
    uint32_t live_blocks;
    uint32_t failures;   /* allocations refused (limit or backend)         */
} zxp_heap_stats_t;
void zxp_heap_stats(zxp_heap_stats_t *out);

void *zxp_malloc(size_t n);
void *zxp_calloc(size_t count, size_t size);
void *zxp_realloc(void *p, size_t n);
void zxp_free(void *p);
char *zxp_strdup(const char *s);

/* ---- Mutex and condition ----------------------------------------------- */

/* Storage large enough for every port's native mutex; each port checks
 * the size at compile time. Override with -DZXP_MUTEX_WORDS=n. */
#ifndef ZXP_MUTEX_WORDS
#    define ZXP_MUTEX_WORDS 24
#endif
typedef struct {
    volatile uint32_t state; /* 0 not created, 1 ready, 2 destroyed */
    uint32_t pad_;
    union {
        uint64_t align_;
        void *ptr_;
        uintptr_t words[ZXP_MUTEX_WORDS];
    } os;
} zxp_mutex_t;
#define ZXP_MUTEX_INITIALIZER {0, 0, {0}}

/* 0 on success. Refuses (non-zero) until zxp_ready(). */
int zxp_mutex_init(zxp_mutex_t *m);
int zxp_mutex_destroy(zxp_mutex_t *m);
/* A statically initialised mutex is created on its first lock. A lock or
 * unlock the OS refuses is a broken invariant: zxp_fatal, never ignored. */
int zxp_mutex_lock(zxp_mutex_t *m);
int zxp_mutex_unlock(zxp_mutex_t *m);

/* A condition variable as POSIX allows it to behave: wait may wake
 * spuriously. It releases the mutex, sleeps one OS tick and re-locks;
 * callers loop on their predicate (libzxv does). Only zxv_rmag_listen
 * waits, and only while another task is inside a budget listener. */
typedef struct {
    volatile uint32_t seq;
} zxp_cond_t;
int zxp_cond_init(zxp_cond_t *c);
int zxp_cond_destroy(zxp_cond_t *c);
int zxp_cond_wait(zxp_cond_t *c, zxp_mutex_t *m);
int zxp_cond_broadcast(zxp_cond_t *c);

/* ---- Single-owner mode (builds without thread-local storage) ------------
 * When libzxv is built with ZXP_NO_TLS (FreeRTOS by default), its last-error
 * text and its "inside a listener" marker are process-wide instead of
 * per-thread. That is only correct if one task makes every libzxv call.
 * zxp_set_owner_current() names that task; from then on any lock taken by
 * another task is a fatal error (zxp_fatal), not a silent race. Other tasks
 * talk to the owner through a queue (see freertos-zxv/). */
void zxp_set_owner_current(void);
int zxp_no_tls_build(void); /* 1 if this port was built with ZXP_NO_TLS */

/* ---- Time --------------------------------------------------------------- */

/* Microseconds since an arbitrary start, monotonic, never wrapping in
 * practice (64-bit). Resolution is the OS tick on RTOS ports. */
uint64_t zxp_monotonic_us(void);

/* Sleep at least one OS tick (lets lower-priority tasks run). */
void zxp_sleep_tick(void);

/* ---- Fatal -------------------------------------------------------------- */

typedef void (*zxp_fatal_fn)(const char *why);
/* Called before the port halts; it may log or persist `why`. If it
 * returns, the port halts anyway (abort, k_panic, or a disabled-interrupt
 * loop on FreeRTOS). */
void zxp_set_fatal_handler(zxp_fatal_fn fn);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void zxp_fatal(const char *why);

/* ---- Test support ------------------------------------------------------- */
#ifdef ZXP_TESTING
/* Return the port to its pre-init state (host tests only). */
void zxp_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZXP_H */
