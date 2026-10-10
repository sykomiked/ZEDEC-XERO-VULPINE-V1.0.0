/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_simd.c — every SIMD kernel set (zt_simd.h) against the portable C
 * reference (zt_kern.h), bit for bit:
 *   1. the two dot products on random rows and on edge cases: full-range and
 *      extreme weights and activations (|s| up to 2^27), f16 scales of every
 *      class (zero, subnormal, normal, max, inf, NaN), zt_q8_t scales over the
 *      whole int32 range, every shift that reaches term52's saturation and
 *      floor paths, and row lengths that exercise every tail (1..80 blocks,
 *      plus 280 and 1024);
 *   2. the thread pool: every row computed exactly once for 2..8 threads;
 *   3. whole forward passes of the three fixture models (test_model_fixture.h)
 *      through each kernel set, alone and threaded: all logits identical.
 * Kernel sets this CPU cannot run are reported as SKIP. "bench" as the first
 * argument adds a per-kernel speed figure (in-cache rows).
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor src/tensor/test_zt_simd.c \
 *       src/tensor/zt_simd.c src/tensor/zt_simd_x86.c src/tensor/zt_simd_neon.c \
 *       src/tensor/zt_model.c src/tensor/zt_tok.c src/tensor/zt_rope.c src/tensor/zt_gguf.c \
 *       src/tensor/zt.c -lpthread -o /tmp/test_zt_simd && /tmp/test_zt_simd
 * The same file cross-compiles for aarch64 and runs under qemu-aarch64. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_model_fixture.h"
#include "zt_model.h"
#include "zt_simd.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("[FAIL] ");                                                                     \
        } else                                                                                     \
            printf("[PASS] ");                                                                     \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static uint64_t rng_state = 0x5EED2026ull;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t below(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

/* ---- 1. dot products ---- */

typedef struct {
    uint32_t nb;
    uint8_t *raw; /* nb * 34 bytes */
    zt_q8_t *q8;  /* nb blocks */
    int16_t *a;   /* nb * 32 */
    uint8_t *ash; /* nb */
} row_t;

static void row_alloc(row_t *r, uint32_t nb)
{
    r->nb = nb;
    r->raw = malloc((size_t) nb * 34);
    r->q8 = malloc((size_t) nb * sizeof(zt_q8_t));
    r->a = malloc((size_t) nb * 32 * 2);
    r->ash = malloc(nb);
}

static void row_free(row_t *r)
{
    free(r->raw);
    free(r->q8);
    free(r->a);
    free(r->ash);
}

static int8_t wval(uint32_t mode)
{
    switch (mode) {
    case 0:
        return (int8_t) (rnd() & 0xFF); /* full range */
    case 1:
        return (int8_t) -128; /* extreme */
    case 2:
        return (int8_t) (rnd() & 1 ? 127 : -128); /* extreme, mixed sign */
    case 3:
        return (int8_t) ((int) below(255) - 127); /* model-like */
    default:
        return (int8_t) ((int) below(9) - 4); /* small */
    }
}

static int16_t xval(uint32_t mode, uint32_t wmode)
{
    switch (mode) {
    case 0:
        return (int16_t) (rnd() & 0xFFFF); /* full range, -32768 too */
    case 1:
        return wmode == 1 ? (int16_t) -32768 : (int16_t) 32767; /* |s| = 2^27 with w = -128 */
    case 2:
        return (int16_t) (rnd() & 1 ? 32767 : -32767);
    case 3:
        return (int16_t) ((int) below(65535) - 32767); /* act_quant's range */
    default:
        return (int16_t) ((int) below(201) - 100);
    }
}

static uint16_t f16val(void)
{
    switch (below(8)) {
    case 0:
        return (uint16_t) rnd(); /* anything */
    case 1:
        return (uint16_t) ((rnd() & 0x8000) | below(0x400)); /* zero / subnormal */
    case 2:
        return (uint16_t) ((rnd() & 0x8000) | 0x7C00 | below(0x400)); /* inf / NaN */
    case 3:
        return (uint16_t) ((rnd() & 0x8000) | 0x7BFF); /* largest finite */
    default:                                           /* model-like */
        return (uint16_t) ((rnd() & 0x8000) | ((uint32_t) (3 + below(12)) << 10) | below(0x400));
    }
}

static int32_t scaleval(void)
{
    switch (below(6)) {
    case 0:
        return (int32_t) (uint32_t) rnd();
    case 1:
        return below(2) ? INT32_MAX : INT32_MIN;
    case 2:
        return 0;
    default:
        return (int32_t) ((1u << 24) + below(1u << 24)); /* requant's [2^24, 2^25] */
    }
}

static void fill(row_t *r, uint32_t wmode, uint32_t xmode, uint32_t shmode)
{
    for (uint32_t b = 0; b < r->nb; b++) {
        uint16_t d = f16val();
        r->raw[b * 34] = (uint8_t) d;
        r->raw[b * 34 + 1] = (uint8_t) (d >> 8);
        r->q8[b].scale = scaleval();
        r->q8[b].phi_k = 0;
        r->q8[b].shift = (uint8_t) (shmode == 0   ? below(32)
                                    : shmode == 1 ? below(256)
                                                  : 24 + below(8));
        r->ash[b] = (uint8_t) (shmode == 1 ? below(256) : below(18));
        for (uint32_t i = 0; i < 32; i++) {
            int8_t w = wval(wmode);
            r->raw[b * 34 + 2 + i] = (uint8_t) w;
            r->q8[b].q[i] = w;
            r->a[b * 32 + i] = xval(xmode, wmode);
        }
    }
}

typedef struct {
    uint64_t rows, mism_raw, mism_q8, sat;
} dstat_t;

static void cmp_row(const zt_kern_t *k, const row_t *r, dstat_t *st)
{
    zt_fx c1 = zt_dot_raw_c(r->raw, r->a, r->ash, r->nb);
    zt_fx s1 = k->dot_raw(r->raw, r->a, r->ash, r->nb);
    zt_fx c2 = zt_dot_q8_c(r->q8, r->a, r->ash, r->nb);
    zt_fx s2 = k->dot_q8(r->q8, r->a, r->ash, r->nb);
    st->rows++;
    if (c1 != s1) {
        if (st->mism_raw++ < 4)
            printf("  raw mismatch nb=%u: C %d, %s %d\n", r->nb, c1, k->name, s1);
    }
    if (c2 != s2) {
        if (st->mism_q8++ < 4) printf("  q8 mismatch nb=%u: C %d, %s %d\n", r->nb, c2, k->name, s2);
    }
    st->sat += (c1 == INT32_MAX || c1 == INT32_MIN) + (c2 == INT32_MAX || c2 == INT32_MIN);
}

static void test_dots(const zt_kern_t *k)
{
    static const uint32_t lens[] = {1,  2,  3,  4,  5,  7,  8,  9,   12,
                                    15, 16, 17, 31, 32, 33, 48, 280, 1024};
    dstat_t st = {0, 0, 0, 0};
    row_t r;
    /* random lengths 1..80 under every mix of modes */
    for (uint32_t t = 0; t < 6000; t++) {
        row_alloc(&r, 1 + below(80));
        fill(&r, below(5), below(5), below(3));
        cmp_row(k, &r, &st);
        row_free(&r);
    }
    CHECK(st.mism_raw == 0 && st.mism_q8 == 0,
          "%s: Q8_0 and zt_q8_t dot products == C on %llu random rows of 1..80 blocks "
          "(%llu raw / %llu q8 mismatches, %llu saturated results among them)",
          k->name, (unsigned long long) st.rows, (unsigned long long) st.mism_raw,
          (unsigned long long) st.mism_q8, (unsigned long long) st.sat);
    /* every tail length, every mode */
    dstat_t s2 = {0, 0, 0, 0};
    for (uint32_t li = 0; li < sizeof lens / sizeof lens[0]; li++)
        for (uint32_t wm = 0; wm < 5; wm++)
            for (uint32_t xm = 0; xm < 5; xm++)
                for (uint32_t sm = 0; sm < 3; sm++) {
                    row_alloc(&r, lens[li]);
                    fill(&r, wm, xm, sm);
                    cmp_row(k, &r, &s2);
                    row_free(&r);
                }
    CHECK(s2.mism_raw == 0 && s2.mism_q8 == 0,
          "%s: == C for lengths 1..1024 blocks x 5 weight x 5 activation x 3 shift patterns "
          "(%llu rows, %llu saturated)",
          k->name, (unsigned long long) s2.rows, (unsigned long long) s2.sat);
    /* hand-made extremes: |s| = 2^27 in every block, every shift k from the
     * floor-to-sign region to past the left saturation, uniform per row */
    dstat_t s3 = {0, 0, 0, 0};
    for (int32_t kk = 0; kk < 256; kk++) {
        row_alloc(&r, 9);
        fill(&r, 1, 1, 0);
        for (uint32_t b = 0; b < 9; b++) {
            r.raw[b * 34] = 0xFF; /* f16 0x7BFF: largest finite, mm = 2047, e = 5 */
            r.raw[b * 34 + 1] = 0x7B;
            r.ash[b] = (uint8_t) kk;
            r.q8[b].scale = b & 1 ? INT32_MIN : INT32_MAX;
            r.q8[b].shift = (uint8_t) (255 - kk);
        }
        cmp_row(k, &r, &s3);
        for (uint32_t b = 0; b < 9; b++) {
            r.raw[b * 34] = 0x01; /* smallest subnormal, mm = 1, e = -24 */
            r.raw[b * 34 + 1] = (uint8_t) (b & 1 ? 0x80 : 0x00);
            r.q8[b].scale = (int32_t) (b + 1);
            r.q8[b].shift = (uint8_t) kk;
            r.ash[b] = 0;
        }
        cmp_row(k, &r, &s3);
        row_free(&r);
    }
    CHECK(s3.mism_raw == 0 && s3.mism_q8 == 0,
          "%s: == C at the extremes (|s| = 2^27, largest and smallest scales, shifts -255..255; "
          "%llu rows, %llu saturated)",
          k->name, (unsigned long long) s3.rows, (unsigned long long) s3.sat);
}

/* ---- 2. thread pool ---- */

typedef struct {
    uint32_t *hits;
} hit_ctx_t;

static void hit_rows(void *ctx, uint32_t r0, uint32_t r1)
{
    hit_ctx_t *h = ctx;
    for (uint32_t r = r0; r < r1; r++) h->hits[r]++;
}

static void test_pool(void)
{
    uint32_t ok = 0, runs = 0;
    for (uint32_t T = 2; T <= 8; T++) {
        zt_pool_t *p = zt_pool_new(T);
        if (!p) {
            printf("[SKIP] thread pool: no threads on this build\n");
            return;
        }
        zt_kern_t k;
        zt_kern_threaded(&k, zt_simd_best(), p);
        for (uint32_t n = 1; n < 5000; n += 1 + n / 3) {
            uint32_t *hits = calloc(n, 4);
            hit_ctx_t h = {hits};
            k.par_rows(k.pctx, hit_rows, &h, n, (uint64_t) n * 4096);
            uint32_t good = 1;
            for (uint32_t i = 0; i < n; i++) good &= hits[i] == 1;
            ok += good;
            runs++;
            free(hits);
        }
        zt_pool_free(p);
    }
    CHECK(ok == runs, "thread pool: every row computed exactly once, 2..8 threads (%u/%u jobs)", ok,
          runs);
}

/* ---- 3. whole forward passes (fixture models, as test_zt_model.c) ---- */

static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint8_t *rebuild(const mf_variant_t *v)
{
    uint8_t *b = calloc(1, v->file_len);
    memcpy(b, v->hdr, v->hdr_len);
    for (uint32_t t = 0; t < v->n_gen; t++) {
        const mf_gen_t *g = &v->gen[t];
        uint8_t *p = b + v->hdr_len + g->off;
        if (g->kind == 1) {
            for (uint64_t k = 0; k < g->n / 32; k++, p += 34) {
                uint16_t d = (uint16_t) ((g->param << 10) | (mix(g->seed, k * 33) & 0x3FF));
                p[0] = (uint8_t) d;
                p[1] = (uint8_t) (d >> 8);
                for (int i = 0; i < 32; i++)
                    p[2 + i] =
                        (uint8_t) (int8_t) ((int) (mix(g->seed, k * 33 + 1 + i) % 255) - 127);
            }
        } else if (g->kind == 2) {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                uint16_t h = (uint16_t) ((((z >> 20) & 1) << 15) |
                                         ((g->param - ((z >> 16) & 1)) << 10) | (z & 0x3FF));
                p[2 * i] = (uint8_t) h;
                p[2 * i + 1] = (uint8_t) (h >> 8);
            }
        } else {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                float f = g->kind == 3   ? 1.0f + (float) ((int) (z % 1025) - 512) / 2048.0f
                          : g->kind == 4 ? (float) ((int) (z % 2049) - 1024) / 4096.0f
                                         : 1.0f + (float) i / 8.0f;
                memcpy(p + 4 * i, &f, 4);
            }
        }
    }
    return b;
}

/* all logits of the first sequence (batch 4), then 8 greedy decode steps */
static zt_fx *run_model(const zt_model_t *m, const mf_variant_t *v, uint32_t kv, uint64_t *n_out)
{
    uint32_t V = v->n_vocab, n0 = v->seq_len[0], steps = 8;
    uint64_t need = zt_model_state_bytes(m, 64, 4, kv);
    void *mem = malloc(need);
    zt_model_state_t s;
    zt_fx *out = malloc((size_t) V * (n0 + steps) * 4);
    zt_model_state_init(&s, m, 64, 4, kv, mem, need);
    zt_model_eval(m, &s, v->seq, n0, out, ZT_EVAL_ALL_LOGITS);
    zt_fx *lg = out + (uint64_t) (n0 - 1) * V;
    for (uint32_t i = 0; i < steps; i++) {
        int32_t id = zt_argmax(lg, V);
        lg += V;
        zt_model_decode(m, &s, id, lg);
    }
    free(mem);
    *n_out = (uint64_t) V * (n0 + steps);
    return out;
}

static void test_models(const zt_kern_t **ks, uint32_t nk)
{
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *file = rebuild(v);
        zt_gguf_t g;
        zt_model_t m;
        zt_model_err_t err;
        if (zt_gguf_open(&g, file, v->file_len)) {
            CHECK(0, "%s: fixture opens", v->name);
            continue;
        }
        uint64_t ab = zt_model_arena_bytes(&g);
        void *arena = malloc(ab);
        if (zt_model_load(&m, &g, arena, ab, &err)) {
            CHECK(0, "%s: fixture loads", v->name);
            continue;
        }
        for (uint32_t kv = 0; kv < 2; kv++) {
            uint64_t n = 0;
            m.kern = 0;
            zt_fx *ref = run_model(&m, v, kv, &n);
            for (uint32_t ki = 0; ki < nk; ki++)
                for (uint32_t th = 0; th < 2; th++) {
                    zt_kern_t k;
                    zt_kern_threaded(&k, ks[ki], th ? pool : 0);
                    if (th && !pool) continue;
                    m.kern = &k;
                    uint64_t n2 = 0;
                    zt_fx *got = run_model(&m, v, kv, &n2);
                    CHECK(n2 == n && !memcmp(ref, got, n * 4),
                          "%s, KV %s, %s%s: all %llu logits (prefill + 8 decode steps) identical "
                          "to the C reference",
                          v->name, kv ? "Q8" : "Q16", ks[ki]->name, th ? " + 3 threads" : "",
                          (unsigned long long) n);
                    free(got);
                }
            free(ref);
        }
        free(arena);
        free(file);
    }
    zt_pool_free(pool);
}

/* ---- speed (optional) ---- */

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void bench(const zt_kern_t **ks, uint32_t nk)
{
    row_t r;
    row_alloc(&r, 280); /* one 8960-value row */
    fill(&r, 3, 3, 0);
    for (uint32_t ki = 0; ki < nk; ki++) {
        const zt_kern_t *k = ks[ki];
        zt_dot_raw_fn fr = k->dot_raw;
        zt_dot_q8_fn fq = k->dot_q8;
        volatile int32_t sink = 0;
        uint32_t it = 20000;
        double t0 = now();
        for (uint32_t i = 0; i < it; i++)
            sink += fr ? fr(r.raw, r.a, r.ash, r.nb) : zt_dot_raw_c(r.raw, r.a, r.ash, r.nb);
        double t1 = now();
        for (uint32_t i = 0; i < it; i++)
            sink += fq ? fq(r.q8, r.a, r.ash, r.nb) : zt_dot_q8_c(r.q8, r.a, r.ash, r.nb);
        double t2 = now();
        printf("[INFO] %-7s Q8_0 row of 8960: %7.0f ns (%.2f GMAC/s); zt_q8_t row: %7.0f ns "
               "(in cache, one thread)\n",
               k->name, (t1 - t0) / it * 1e9, 8960.0 * it / (t1 - t0) * 1e-9, (t2 - t1) / it * 1e9);
        (void) sink;
    }
    row_free(&r);
}

int main(int argc, char **argv)
{
    printf("=== zt_simd: SIMD kernels and threads vs the C reference, bit for bit ===\n");
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    printf("[INFO] kernel sets on this CPU:");
    for (uint32_t i = 0; i < nk; i++) printf(" %s", ks[i]->name);
    printf(" (best: %s)\n", zt_simd_best()->name);
    static const char *names[] = {"avx2", "avx512", "neon"};
    for (uint32_t i = 0; i < 3; i++)
        if (!zt_simd_find(names[i]))
            printf("[SKIP] %s: not compiled for this target or not on this CPU\n", names[i]);
    CHECK(nk >= 1 && !strcmp(ks[nk - 1]->name, "c") && zt_simd_find("c") == ks[nk - 1],
          "the C reference is always listed, last");
    uint32_t simd = 0;
    for (uint32_t i = 0; i < nk; i++) {
        if (!ks[i]->dot_raw) continue;
        simd++;
        test_dots(ks[i]);
    }
    test_pool();
    test_models(ks, nk);
    if (argc > 1 && !strcmp(argv[1], "bench")) bench(ks, nk);
    printf("%d/%d checks passed (%u SIMD kernel set%s tested)\n", checks - failures, checks, simd,
           simd == 1 ? "" : "s");
    return failures ? 1 : 0;
}
