/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_port.c — the port layer on the POSIX host: the fail-closed entropy
 * gate, health tests, the counting allocator, mutexes under contention,
 * the condition shim, time, and the fatal paths. Built twice: as is, and
 * with -DZXP_NO_TLS (single-owner mode). */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include "zxe_test.h"
#include "zxp.h"
#include "zxp_posix.h"

/* ---- entropy sources ---- */
static int src_fail(void *u, uint8_t *b, size_t n)
{
    (void) u;
    (void) b;
    (void) n;
    return -1;
}
static int src_zero(void *u, uint8_t *b, size_t n)
{
    (void) u;
    memset(b, 0, n);
    return 0;
}
static int src_same(void *u, uint8_t *b, size_t n) /* the same non-constant bytes each time */
{
    (void) u;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t) (i * 7 + 1);
    return 0;
}
static int g_stick_after = -1, g_calls;
static int src_sticky(void *u, uint8_t *b, size_t n) /* good, then stuck */
{
    if (g_stick_after >= 0 && g_calls++ >= g_stick_after) return src_same(u, b, n);
    return zxp_posix_entropy(u, b, n);
}

static void reset(void)
{
    zxp_test_reset();
    g_stick_after = -1;
    g_calls = 0;
}

static void test_gate(void)
{
    zxp_mutex_t m;
    uint8_t buf[32];
    reset();
    IS(zxp_ready(), 0);
    IS(zxp_mutex_init(&m), ZXP_E_STATE); /* fail closed before init */
    IS(zxp_entropy_fill(buf, sizeof buf), ZXP_E_NO_ENTROPY);
    IS(zxp_getentropy(buf, sizeof buf), -1);
    IS(zxp_init(), ZXP_E_NO_ENTROPY);
    IS(zxp_set_entropy(NULL, NULL), ZXP_E_ARG);

    reset();
    IS(zxp_set_entropy(src_fail, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_E_ENTROPY);
    IS(zxp_ready(), 0);
    /* latched: a good source afterwards does not reopen the port */
    IS(zxp_set_entropy(zxp_posix_entropy, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_E_ENTROPY);
    IS(zxp_mutex_init(&m), ZXP_E_STATE);

    reset();
    IS(zxp_set_entropy(src_zero, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_E_ENTROPY); /* constant bytes */

    reset();
    IS(zxp_set_entropy(src_same, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_E_ENTROPY); /* two equal reads */

    reset();
    IS(zxp_set_entropy(zxp_posix_entropy, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_OK);
    IS(zxp_ready(), 1);
    IS(zxp_init(), ZXP_E_STATE);
    IS(zxp_set_entropy(src_zero, NULL), ZXP_E_STATE); /* no silent swap */
    IS(zxp_set_allocator(NULL), ZXP_E_STATE);
    IS(zxp_entropy_fill(buf, sizeof buf), ZXP_OK);
    IS(zxp_getentropy(buf, 0), 0);
    IS(zxp_entropy_fill(NULL, 1), ZXP_E_ARG);
    IS(zxp_mutex_init(&m), ZXP_OK);
    IS(zxp_mutex_destroy(&m), ZXP_OK);

    /* continuous test: the source gets stuck after init */
    reset();
    g_stick_after = 2; /* the two health-test reads are good */
    IS(zxp_set_entropy(src_sticky, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_OK);
    IS(zxp_entropy_fill(buf, sizeof buf), ZXP_OK);  /* first stuck block: new */
    IS(zxp_entropy_fill(buf, sizeof buf), ZXP_E_ENTROPY); /* repeated: latched */
    IS(zxp_ready(), 0);
    IS(zxp_mutex_init(&m), ZXP_E_STATE);
    IS(zxp_getentropy(buf, sizeof buf), -1);
    uint8_t zero[32] = {0};
    CHECK(memcmp(buf, zero, sizeof buf) == 0, "a failed fill leaves no partial output");

    IS(strcmp(zxp_status_name(ZXP_E_NO_ENTROPY), "ZXP_E_NO_ENTROPY"), 0);
    IS(strcmp(zxp_status_name(42), "ZXP_E_UNKNOWN"), 0);
}

/* ---- allocator ---- */
static int g_allocs, g_frees;
static void *cnt_alloc(void *u, size_t n)
{
    (void) u;
    g_allocs++;
    return malloc(n);
}
static void cnt_free(void *u, void *p)
{
    (void) u;
    g_frees++;
    free(p);
}

static void test_alloc(void)
{
    reset();
    zxp_allocator a = {cnt_alloc, cnt_free, NULL};
    zxp_allocator bad = {NULL, cnt_free, NULL};
    IS(zxp_set_allocator(&bad), ZXP_E_ARG);
    IS(zxp_set_allocator(&a), ZXP_OK);
    zxp_heap_stats_t s0, s;
    zxp_heap_stats(&s0);
    char *p = zxp_malloc(100);
    CHECK(p != NULL, "malloc");
    CHECK(((uintptr_t) p & 7u) == 0, "8-byte aligned");
    memset(p, 'x', 100);
    zxp_heap_stats(&s);
    IS(s.live_bytes - s0.live_bytes, 100);
    IS(s.live_blocks - s0.live_blocks, 1);
    p = zxp_realloc(p, 300);
    CHECK(p && p[0] == 'x' && p[99] == 'x', "realloc keeps the contents");
    zxp_heap_stats(&s);
    IS(s.live_bytes - s0.live_bytes, 300);
    p = zxp_realloc(p, 10);
    CHECK(p && p[9] == 'x', "shrink");
    zxp_free(p);
    zxp_heap_stats(&s);
    IS(s.live_bytes, s0.live_bytes);
    IS(s.live_blocks, s0.live_blocks);
    CHECK(s.peak_bytes >= s0.live_bytes + 300, "peak");
    IS(g_allocs, 3);
    IS(g_frees, 3);

    uint8_t *z = zxp_calloc(16, 4);
    int allzero = 1;
    for (int i = 0; i < 64; i++) allzero &= z[i] == 0;
    CHECK(allzero, "calloc zeroes");
    zxp_free(z);
    CHECK(zxp_calloc(SIZE_MAX / 2, 4) == NULL, "calloc overflow refused");
    char *d = zxp_strdup("ledger");
    CHECK(d && strcmp(d, "ledger") == 0, "strdup");
    zxp_free(d);
    CHECK(zxp_strdup(NULL) == NULL, "strdup NULL");
    zxp_free(NULL);

    /* the hard limit */
    zxp_heap_stats(&s0);
    zxp_set_heap_limit(s0.live_bytes + 1000);
    void *a1 = zxp_malloc(600);
    void *a2 = zxp_malloc(600);
    CHECK(a1 && !a2, "second block passes the limit");
    zxp_heap_stats(&s);
    IS(s.failures - s0.failures, 1);
    void *a3 = zxp_realloc(a1, 2000);
    CHECK(a3 == NULL, "realloc past the limit fails and keeps the block");
    zxp_free(a1);
    zxp_set_heap_limit(0);
    void *big = zxp_malloc(1 << 20);
    CHECK(big != NULL, "no limit");
    zxp_free(big);
    IS(zxp_set_allocator(NULL), ZXP_OK);
}

static void bad_free(void)
{
    int on_stack[8] = {0};
    zxp_free(&on_stack[4]);
}
__attribute__((unused)) static void double_free(void)
{
    void *p = zxp_malloc(8);
    zxp_free(p);
    zxp_free(p);
}
static void lock_before_init(void)
{
    zxp_test_reset();
    static zxp_mutex_t m = ZXP_MUTEX_INITIALIZER;
    zxp_mutex_lock(&m);
}
static void lock_destroyed(void)
{
    zxp_mutex_t m;
    if (zxp_mutex_init(&m) != ZXP_OK) _exit(0);
    zxp_mutex_destroy(&m);
    zxp_mutex_lock(&m);
}
static void exit70(const char *why)
{
    (void) why;
    _exit(70);
}
static void handler_runs(void)
{
    zxp_set_fatal_handler(exit70);
    zxp_fatal("test");
}

/* ---- mutex ---- */
static zxp_mutex_t g_mu;
static zxp_mutex_t g_static_mu = ZXP_MUTEX_INITIALIZER;
static long g_counter, g_counter2;
static void *worker(void *arg)
{
    (void) arg;
    for (int i = 0; i < 50000; i++) {
        zxp_mutex_lock(&g_mu);
        g_counter++;
        zxp_mutex_unlock(&g_mu);
        zxp_mutex_lock(&g_static_mu);
        g_counter2++;
        zxp_mutex_unlock(&g_static_mu);
    }
    return NULL;
}

static zxp_cond_t g_cv;
static volatile int g_flag;
static void *setter(void *arg)
{
    (void) arg;
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
    zxp_mutex_lock(&g_mu);
    g_flag = 1;
    zxp_cond_broadcast(&g_cv);
    zxp_mutex_unlock(&g_mu);
    return NULL;
}

static void owner_violation(void)
{
    zxp_set_owner_current();
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);
    pthread_join(t, NULL);
}

static void test_mutex(void)
{
    reset();
    IS(zxp_set_entropy(zxp_posix_entropy, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_OK);
    IS(zxp_mutex_init(&g_mu), ZXP_OK);
    IS(zxp_mutex_init(NULL), ZXP_E_ARG);
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    IS(g_counter, 200000);
    IS(g_counter2, 200000); /* the static initializer was created lazily, once */

    IS(zxp_cond_init(&g_cv), ZXP_OK);
    pthread_t s;
    pthread_create(&s, NULL, setter, NULL);
    zxp_mutex_lock(&g_mu);
    int waits = 0;
    while (!g_flag) {
        zxp_cond_wait(&g_cv, &g_mu);
        waits++;
    }
    zxp_mutex_unlock(&g_mu);
    pthread_join(s, NULL);
    CHECK(g_flag && waits >= 1, "condition wait loop saw the flag");
    IS(zxp_cond_destroy(&g_cv), ZXP_OK);

    CHECK(dies(bad_free), "free of a foreign pointer is fatal");
#if !defined(__SANITIZE_ADDRESS__)
    /* Best effort: it reads the freed header (ASan reports that read itself). */
    CHECK(dies(double_free), "double free is fatal");
#endif
    CHECK(dies(lock_before_init), "locking a static mutex before init is fatal");
    CHECK(dies(lock_destroyed), "locking a destroyed mutex is fatal");
    CHECK(dies(handler_runs), "the fatal handler runs");
#ifdef ZXP_NO_TLS
    IS(zxp_no_tls_build(), 1);
    CHECK(dies(owner_violation), "a lock from a non-owner task is fatal in NO_TLS builds");
#else
    IS(zxp_no_tls_build(), 0);
    CHECK(!dies(owner_violation), "owner is not enforced when TLS is available");
#endif
    IS(zxp_mutex_destroy(&g_mu), ZXP_OK);
}

static void test_time(void)
{
    uint64_t a = zxp_monotonic_us();
    zxp_sleep_tick();
    uint64_t b = zxp_monotonic_us();
    CHECK(b > a, "monotonic time advances across a tick sleep (%llu -> %llu)",
          (unsigned long long) a, (unsigned long long) b);
    uint64_t prev = b;
    int mono = 1;
    for (int i = 0; i < 10000; i++) {
        uint64_t n = zxp_monotonic_us();
        mono &= n >= prev;
        prev = n;
    }
    CHECK(mono, "never goes backwards");
}

int main(void)
{
    test_gate();
    test_alloc();
    test_mutex();
    test_time();
#ifdef ZXP_NO_TLS
    return finish("test_port (ZXP_NO_TLS)");
#else
    return finish("test_port");
#endif
}
