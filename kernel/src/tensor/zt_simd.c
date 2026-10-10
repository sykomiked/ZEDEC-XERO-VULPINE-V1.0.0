/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd.c — kernel-set selection and the row-parallel thread pool for the
 * forward pass (see zt_simd.h). HOSTED ONLY (libc, pthreads, malloc): never
 * linked into a kernel image. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "zt_simd.h"

#include <stdlib.h>
#include <string.h>

static const zt_kern_t K_C = {"c", 0, 0, 0, 0};

uint32_t zt_simd_list(const zt_kern_t **out, uint32_t cap)
{
    const zt_kern_t *all[4] = {zt_simd_x86_avx512(), zt_simd_x86_avx2(), zt_simd_neon(), &K_C};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (all[i] && n < cap) out[n++] = all[i];
    return n;
}

const zt_kern_t *zt_simd_best(void)
{
    const zt_kern_t *k = &K_C;
    zt_simd_list(&k, 1);
    return k;
}

const zt_kern_t *zt_simd_find(const char *name)
{
    const zt_kern_t *l[4];
    uint32_t n = zt_simd_list(l, 4);
    for (uint32_t i = 0; name && i < n; i++)
        if (!strcmp(l[i]->name, name)) return l[i];
    return 0;
}

#if defined(_WIN32)

uint32_t zt_pool_ncpu(void)
{
    return 1;
}

zt_pool_t *zt_pool_new(uint32_t n_threads)
{
    (void) n_threads;
    return 0;
}

void zt_pool_free(zt_pool_t *p)
{
    (void) p;
}

void zt_kern_threaded(zt_kern_t *out, const zt_kern_t *base, zt_pool_t *pool)
{
    (void) pool;
    *out = *base;
    out->par_rows = 0;
    out->pctx = 0;
}

const zt_kern_t *zt_simd_default(void)
{
    const zt_kern_t *k = zt_simd_find(getenv("ZT_KERN"));
    return k ? k : zt_simd_best();
}

#else

#    include <pthread.h>
#    include <stdatomic.h>
#    include <unistd.h>

#    define POOL_MAX     64u
#    define SPIN         20000u /* polls before a waiting thread sleeps */
/* below this many multiply-adds a matrix runs on the calling thread */
#    define PAR_MIN_WORK ((uint64_t) 1 << 16)

struct zt_pool {
    uint32_t n; /* threads including the caller */
    pthread_t th[POOL_MAX];
    pthread_mutex_t mu;
    pthread_cond_t go, done;
    atomic_uint_fast64_t gen; /* bumped once per job */
    atomic_uint active;       /* workers still on the current job */
    atomic_uint next;         /* next chunk to take */
    atomic_bool quit;
    zt_rows_fn fn;
    void *ctx;
    uint32_t rows, chunk;
};

uint32_t zt_pool_ncpu(void)
{
#    ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t) n : 1;
#    else
    return 1;
#    endif
}

/* Take chunks until none are left. Which thread computes a row does not
 * matter: rows are independent and each is computed whole by one thread. */
static void run_chunks(zt_pool_t *p)
{
    for (;;) {
        uint32_t c = atomic_fetch_add_explicit(&p->next, 1, memory_order_relaxed);
        uint64_t r0 = (uint64_t) c * p->chunk;
        if (r0 >= p->rows) return;
        uint64_t r1 = r0 + p->chunk > p->rows ? p->rows : r0 + p->chunk;
        p->fn(p->ctx, (uint32_t) r0, (uint32_t) r1);
    }
}

static void *worker(void *arg)
{
    zt_pool_t *p = arg;
    uint64_t seen = 0;
    for (;;) {
        uint32_t i = 0;
        while (atomic_load_explicit(&p->gen, memory_order_acquire) == seen &&
               !atomic_load(&p->quit) && i < SPIN)
            i++;
        if (atomic_load_explicit(&p->gen, memory_order_acquire) == seen && !atomic_load(&p->quit)) {
            pthread_mutex_lock(&p->mu);
            while (atomic_load(&p->gen) == seen && !atomic_load(&p->quit))
                pthread_cond_wait(&p->go, &p->mu);
            pthread_mutex_unlock(&p->mu);
        }
        if (atomic_load(&p->quit)) return 0;
        seen = atomic_load_explicit(&p->gen, memory_order_acquire);
        run_chunks(p);
        if (atomic_fetch_sub_explicit(&p->active, 1, memory_order_acq_rel) == 1) {
            pthread_mutex_lock(&p->mu);
            pthread_cond_signal(&p->done);
            pthread_mutex_unlock(&p->mu);
        }
    }
}

static void pool_run(void *pctx, zt_rows_fn fn, void *ctx, uint32_t n, uint64_t work)
{
    zt_pool_t *p = pctx;
    if (!p || work < PAR_MIN_WORK || n < 2 * p->n) {
        fn(ctx, 0, n);
        return;
    }
    uint32_t chunk = n / (p->n * 4);
    if (chunk < 8) chunk = 8;
    p->fn = fn;
    p->ctx = ctx;
    p->rows = n;
    p->chunk = chunk;
    atomic_store_explicit(&p->next, 0, memory_order_relaxed);
    atomic_store_explicit(&p->active, p->n - 1, memory_order_relaxed);
    pthread_mutex_lock(&p->mu);
    atomic_fetch_add_explicit(&p->gen, 1, memory_order_release);
    pthread_cond_broadcast(&p->go);
    pthread_mutex_unlock(&p->mu);
    run_chunks(p);
    uint32_t i = 0;
    while (atomic_load_explicit(&p->active, memory_order_acquire) && i < SPIN) i++;
    if (atomic_load_explicit(&p->active, memory_order_acquire)) {
        pthread_mutex_lock(&p->mu);
        while (atomic_load(&p->active)) pthread_cond_wait(&p->done, &p->mu);
        pthread_mutex_unlock(&p->mu);
    }
}

zt_pool_t *zt_pool_new(uint32_t n_threads)
{
    if (n_threads < 2) return 0;
    if (n_threads > POOL_MAX) n_threads = POOL_MAX;
    zt_pool_t *p = calloc(1, sizeof *p);
    if (!p) return 0;
    pthread_mutex_init(&p->mu, 0);
    pthread_cond_init(&p->go, 0);
    pthread_cond_init(&p->done, 0);
    atomic_init(&p->gen, 0);
    atomic_init(&p->active, 0);
    atomic_init(&p->next, 0);
    atomic_init(&p->quit, false);
    p->n = 1;
    for (uint32_t i = 1; i < n_threads; i++) {
        if (pthread_create(&p->th[i], 0, worker, p) != 0) break;
        p->n++;
    }
    if (p->n < 2) {
        zt_pool_free(p);
        return 0;
    }
    return p;
}

void zt_pool_free(zt_pool_t *p)
{
    if (!p) return;
    pthread_mutex_lock(&p->mu);
    atomic_store(&p->quit, true);
    pthread_cond_broadcast(&p->go);
    pthread_mutex_unlock(&p->mu);
    for (uint32_t i = 1; i < p->n; i++) pthread_join(p->th[i], 0);
    pthread_cond_destroy(&p->go);
    pthread_cond_destroy(&p->done);
    pthread_mutex_destroy(&p->mu);
    free(p);
}

void zt_kern_threaded(zt_kern_t *out, const zt_kern_t *base, zt_pool_t *pool)
{
    *out = *base;
    out->par_rows = pool ? pool_run : 0;
    out->pctx = pool;
}

static zt_kern_t g_default;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void make_default(void)
{
    const zt_kern_t *k = zt_simd_find(getenv("ZT_KERN"));
    if (!k) k = zt_simd_best();
    uint32_t n = zt_pool_ncpu();
    const char *e = getenv("ZT_THREADS");
    if (e && *e) n = (uint32_t) strtoul(e, 0, 10);
    if (n > 16) n = 16;
    zt_kern_threaded(&g_default, k, zt_pool_new(n));
}

const zt_kern_t *zt_simd_default(void)
{
    pthread_once(&g_once, make_default);
    return &g_default;
}

#endif
