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
 *   1b. the Q4_K and Q6_K dot products (GGUF K-quant rows used in place) the
 *      same way, and the C reference against exact math: ggml's own
 *      dequantization, transcribed here, summed in 128-bit integers;
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

/* ---- 1b. K-quant rows (GGUF Q4_K / Q6_K used in place) ---- */

typedef struct {
    uint32_t nb;      /* activation blocks (8 per K-quant block) */
    uint8_t *q4, *q6; /* nb / 8 blocks of ZT_Q4K_BYTES / ZT_Q6K_BYTES */
    int16_t *a;
    uint8_t *ash;
    int32_t *asum;
} krow_t;

static void krow_alloc(krow_t *r, uint32_t nkb)
{
    r->nb = nkb * 8;
    r->q4 = malloc((size_t) nkb * ZT_Q4K_BYTES);
    r->q6 = malloc((size_t) nkb * ZT_Q6K_BYTES);
    r->a = malloc((size_t) r->nb * 32 * 2);
    r->ash = malloc(r->nb);
    r->asum = malloc((size_t) r->nb * 4);
}

static void krow_free(krow_t *r)
{
    free(r->q4);
    free(r->q6);
    free(r->a);
    free(r->ash);
    free(r->asum);
}

static uint8_t qbyte(uint32_t mode)
{
    return mode == 1 ? 0xFF : mode == 2 ? 0x00 : (uint8_t) rnd();
}

/* f16val, or (model) finite normal scales of model-like size, 2^-23..2^-7
 * (real Q4_K/Q6_K scales are about 1e-4 to 1e-2) */
static uint16_t kf16(uint32_t model)
{
    return model ? (uint16_t) ((rnd() & 0x8000) | ((uint32_t) (2 + below(7)) << 10) | below(0x400))
                 : f16val();
}

/* qmode: 0 random, 1 all bits set, 2 all clear; model: model-like scales and
 * activations (no saturation), as the exact check below needs */
static void kfill(krow_t *r, uint32_t qmode, uint32_t xmode, uint32_t shmode, uint32_t model)
{
    for (uint32_t k = 0; k < r->nb / 8; k++) {
        uint8_t *b4 = r->q4 + (size_t) k * ZT_Q4K_BYTES, *b6 = r->q6 + (size_t) k * ZT_Q6K_BYTES;
        uint16_t d = kf16(model), dmin = kf16(model), d6 = kf16(model);
        b4[0] = (uint8_t) d, b4[1] = (uint8_t) (d >> 8);
        b4[2] = (uint8_t) dmin, b4[3] = (uint8_t) (dmin >> 8);
        for (uint32_t i = 4; i < 16; i++) b4[i] = qmode == 1 ? 0xFF : (uint8_t) rnd();
        for (uint32_t i = 16; i < ZT_Q4K_BYTES; i++) b4[i] = qbyte(qmode);
        for (uint32_t i = 0; i < 192; i++) b6[i] = qbyte(qmode);
        for (uint32_t i = 192; i < 208; i++)
            b6[i] = qmode == 1 ? (uint8_t) (rnd() & 1 ? 0x7F : 0x80) : (uint8_t) rnd();
        b6[208] = (uint8_t) d6, b6[209] = (uint8_t) (d6 >> 8);
    }
    for (uint32_t b = 0; b < r->nb; b++) {
        r->ash[b] = (uint8_t) (shmode == 1 ? below(256) : below(model ? 6 : 18));
        int32_t sum = 0;
        for (uint32_t i = 0; i < 32; i++) {
            int16_t x = model ? (int16_t) ((int) below(65535) - 32767) : xval(xmode, 1);
            r->a[b * 32 + i] = x;
            sum += x;
        }
        r->asum[b] = sum;
    }
}

typedef struct {
    uint64_t rows, mism4, mism6;
} kstat_t;

static void kcmp_row(const zt_kern_t *k, const krow_t *r, kstat_t *st)
{
    zt_fx c4 = zt_dot_q4k_c(r->q4, r->a, r->ash, r->asum, r->nb);
    zt_fx s4 = k->dot_q4k(r->q4, r->a, r->ash, r->asum, r->nb);
    zt_fx c6 = zt_dot_q6k_c(r->q6, r->a, r->ash, r->asum, r->nb);
    zt_fx s6 = k->dot_q6k(r->q6, r->a, r->ash, r->asum, r->nb);
    st->rows++;
    if (c4 != s4 && st->mism4++ < 4)
        printf("  q4k mismatch nb=%u: C %d, %s %d\n", r->nb, c4, k->name, s4);
    if (c6 != s6 && st->mism6++ < 4)
        printf("  q6k mismatch nb=%u: C %d, %s %d\n", r->nb, c6, k->name, s6);
}

static void test_kdots(const zt_kern_t *k)
{
    static const uint32_t lens[] = {1, 2, 3, 6, 35}; /* K-quant blocks: 256 .. 8960 values */
    kstat_t st = {0, 0, 0};
    krow_t r;
    for (uint32_t t = 0; t < 3000; t++) {
        krow_alloc(&r, 1 + below(8));
        kfill(&r, below(3), below(5), below(2), 0);
        kcmp_row(k, &r, &st);
        krow_free(&r);
    }
    for (uint32_t li = 0; li < sizeof lens / sizeof lens[0]; li++)
        for (uint32_t qm = 0; qm < 3; qm++)
            for (uint32_t xm = 0; xm < 5; xm++)
                for (uint32_t sm = 0; sm < 2; sm++) {
                    krow_alloc(&r, lens[li]);
                    kfill(&r, qm, xm, sm, 0);
                    kcmp_row(k, &r, &st);
                    krow_free(&r);
                }
    /* every activation shift from 0 to 255 (term52's floor, exact and saturating paths) */
    for (uint32_t kk = 0; kk < 256; kk++) {
        krow_alloc(&r, 2);
        kfill(&r, kk % 3, 1, 0, 0);
        for (uint32_t b = 0; b < r.nb; b++) r.ash[b] = (uint8_t) kk;
        kcmp_row(k, &r, &st);
        krow_free(&r);
    }
    CHECK(st.mism4 == 0 && st.mism6 == 0,
          "%s: Q4_K and Q6_K dot products == C on %llu rows (random, all-ones and all-zero "
          "quants, every f16 class, activation shifts 0..255; %llu / %llu mismatches)",
          k->name, (unsigned long long) st.rows, (unsigned long long) st.mism4,
          (unsigned long long) st.mism6);
}

/* ggml's own dequantization (ggml-quants.c dequantize_row_q4_K / _q6_K),
 * transcribed independently of zt_kern.h, as integers: weight i of block k is
 * wd[i] * d - wm[i] * dmin (Q4_K) or wd[i] * d (Q6_K, wm = 0). */
static void ggml_q4k_ints(const uint8_t *blk, int32_t wd[256], int32_t wm[256])
{
    const uint8_t *sc = blk + 4, *q = blk + 16;
    int is = 0;
    for (int j = 0; j < 256; j += 64, q += 32, is += 2) {
        int s1, m1, s2, m2;
        for (int h = 0; h < 2; h++) {
            int jj = is + h, sv, mv;
            if (jj < 4) {
                sv = sc[jj] & 63;
                mv = sc[jj + 4] & 63;
            } else {
                sv = (sc[jj + 4] & 0xF) | ((sc[jj - 4] >> 6) << 4);
                mv = (sc[jj + 4] >> 4) | ((sc[jj] >> 6) << 4);
            }
            if (h)
                s2 = sv, m2 = mv;
            else
                s1 = sv, m1 = mv;
        }
        for (int l = 0; l < 32; l++) {
            wd[j + l] = s1 * (q[l] & 0xF), wm[j + l] = m1;
            wd[j + 32 + l] = s2 * (q[l] >> 4), wm[j + 32 + l] = m2;
        }
    }
}

static void ggml_q6k_ints(const uint8_t *blk, int32_t wd[256])
{
    const uint8_t *ql = blk, *qh = blk + 128;
    const int8_t *sc = (const int8_t *) (blk + 192);
    for (int n = 0; n < 256; n += 128, ql += 64, qh += 32, sc += 8)
        for (int l = 0; l < 32; l++) {
            int is = l / 16;
            int q1 = ((ql[l] & 0xF) | (((qh[l] >> 0) & 3) << 4)) - 32;
            int q2 = ((ql[l + 32] & 0xF) | (((qh[l] >> 2) & 3) << 4)) - 32;
            int q3 = ((ql[l] >> 4) | (((qh[l] >> 4) & 3) << 4)) - 32;
            int q4 = ((ql[l + 32] >> 4) | (((qh[l] >> 6) & 3) << 4)) - 32;
            wd[n + l] = sc[is] * q1;
            wd[n + l + 32] = sc[is + 2] * q2;
            wd[n + l + 64] = sc[is + 4] * q3;
            wd[n + l + 96] = sc[is + 6] * q4;
        }
}

/* f16 as mm * 2^e, decoded here without zt_kern_f16 (finite normals only) */
static void f16_norm(uint16_t h, int32_t *mm, int32_t *e)
{
    int32_t m = (h & 0x3FF) | 0x400;
    *e = (int32_t) ((h >> 10) & 31) - 25;
    *mm = (h & 0x8000) ? -m : m;
}

/* The exact dot product of one row times 2^24 (Q32 times 2^8), and the number
 * of term52 floors the C reference takes. Model-like rows keep every
 * exponent at or above -8, so the sum is an exact integer, and every term
 * below 2^46 Q32 units, so term52 never saturates. */
typedef __int128 i128;
static i128 kexact(const uint8_t *row, uint32_t is6, const krow_t *r, uint32_t *nterms)
{
    i128 tot = 0;
    *nterms = 0;
    for (uint32_t k = 0; k < r->nb / 8; k++) {
        const uint8_t *blk = row + (size_t) k * (is6 ? ZT_Q6K_BYTES : ZT_Q4K_BYTES);
        int32_t wd[256], wm[256] = {0}, dm, de, mm = 0, me = 0;
        if (is6) {
            ggml_q6k_ints(blk, wd);
            f16_norm((uint16_t) (blk[208] | (blk[209] << 8)), &dm, &de);
        } else {
            ggml_q4k_ints(blk, wd, wm);
            f16_norm((uint16_t) (blk[0] | (blk[1] << 8)), &dm, &de);
            f16_norm((uint16_t) (blk[2] | (blk[3] << 8)), &mm, &me);
        }
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t b = k * 8 + i / 32;
            i128 x = r->a[k * 256 + i];
            /* weight * act in Q32 * 2^8: wd dm 2^(de + 16 + ash + 8), likewise for the min */
            tot += x * wd[i] * dm * ((i128) 1 << (de + 24 + r->ash[b]));
            tot -= x * wm[i] * mm * ((i128) 1 << (me + 24 + r->ash[b]));
        }
        *nterms += is6 ? 8 : 16;
    }
    return tot;
}

static void test_kexact(void)
{
    krow_t r;
    uint64_t rows = 0, bad4 = 0, bad6 = 0;
    i128 worst4 = 0, worst6 = 0;
    for (uint32_t t = 0; t < 400; t++) {
        krow_alloc(&r, 1 + below(6));
        kfill(&r, below(3), 3, 0, 1);
        for (uint32_t is6 = 0; is6 < 2; is6++) {
            uint32_t nt;
            i128 ex = kexact(is6 ? r.q6 : r.q4, is6, &r, &nt);
            zt_fx c = is6 ? zt_dot_q6k_c(r.q6, r.a, r.ash, r.asum, r.nb)
                          : zt_dot_q4k_c(r.q4, r.a, r.ash, r.asum, r.nb);
            i128 err = (i128) c * (1 << 24) - ex;
            if (err < 0) err = -err;
            /* rounding to Q16 (2^15 Q32) plus one Q32 unit per floored term */
            int ok = c != INT32_MAX && c != INT32_MIN && err <= (i128) (32768 + nt) * 256;
            if (is6) {
                bad6 += !ok;
                if (err > worst6) worst6 = err;
            } else {
                bad4 += !ok;
                if (err > worst4) worst4 = err;
            }
        }
        rows++;
        krow_free(&r);
    }
    CHECK(bad4 == 0 && bad6 == 0,
          "C Q4_K / Q6_K dot products == exact math (ggml's own dequantization, 128-bit "
          "integers) to within Q16 rounding plus one Q32 unit per term on %llu model-like rows "
          "(worst error %.4f / %.4f Q16 units)",
          (unsigned long long) rows, (double) worst4 / (1 << 24), (double) worst6 / (1 << 24));
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
        if (ks[i]->dot_q4k && ks[i]->dot_q6k) test_kdots(ks[i]);
    }
    test_kexact();
    test_pool();
    test_models(ks, nk);
    if (argc > 1 && !strcmp(argv[1], "bench")) bench(ks, nk);
    printf("%d/%d checks passed (%u SIMD kernel set%s tested)\n", checks - failures, checks, simd,
           simd == 1 ? "" : "s");
    return failures ? 1 : 0;
}
