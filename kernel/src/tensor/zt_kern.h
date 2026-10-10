/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_kern.h — the matrix-vector kernels of the forward pass (T21), as one
 * portable C reference that every faster path must match bit for bit.
 *
 * A row of weights (GGUF Q8_0 blocks in place, or zt_q8_t blocks) times the
 * 16-bit activation blocks of act_quant: per block of 32, an exact 32-bit sum
 * s = sum w[i] x[i] (|s| <= 32 * 128 * 32768 = 2^27), then
 * term52(s * scale, exponent) summed in 64 bits, then rounded to Q16. Every
 * step is an integer operation with a single defined result, so a SIMD path
 * that forms the same per-block sums (in any order: integer addition without
 * overflow is associative) and the same term52 values gives the same bits.
 *
 * zt_model.c uses the C reference below unless the model carries a zt_kern_t
 * (zt_model_t.kern). Kernel images never set one: the SIMD and thread code
 * (zt_simd.c, zt_simd_x86.c, zt_simd_neon.c) is for hosted builds only and
 * is not linked into them. Freestanding: integer C11, no libc. */
#ifndef ZT_KERN_H
#define ZT_KERN_H

#include "zt.h"

#define ZT_KERN_LIM52 ((int64_t) 1 << 52)

/* v * 2^e, floor for e < 0, clamped to +-2^52. */
static inline int64_t zt_term52(int64_t v, int32_t e)
{
    if (v == 0) return 0;
    if (e <= 0) {
        if (e < -62) return v < 0 ? -1 : 0;
        v >>= -e;
    } else {
        if (e > 52 || v > (ZT_KERN_LIM52 >> e) || v < -(ZT_KERN_LIM52 >> e))
            return v < 0 ? -ZT_KERN_LIM52 : ZT_KERN_LIM52;
        v *= (int64_t) 1 << e;
    }
    return v > ZT_KERN_LIM52 ? ZT_KERN_LIM52 : v < -ZT_KERN_LIM52 ? -ZT_KERN_LIM52 : v;
}

static inline zt_fx zt_kern_sat32(int64_t v)
{
    return (zt_fx) (v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

/* A GGUF Q8_0 block's f16 scale d as mm * 2^e (mm signed, |mm| < 2^11).
 * inf/NaN (exponent 31) gives mm = 0: the block reads as zero. */
static inline void zt_kern_f16(uint32_t d, int32_t *mm, int32_t *e)
{
    uint32_t ex = (d >> 10) & 31;
    int32_t m = (int32_t) (d & 0x3FF);
    if (ex == 31) {
        *mm = 0;
        *e = 0;
        return;
    }
    if (ex) {
        m |= 0x400;
        *e = (int32_t) ex - 25;
    } else {
        *e = -24;
    }
    *mm = (d & 0x8000) ? -m : m;
}

/* The 64-bit Q32 sum of a row to a Q16 result. */
static inline zt_fx zt_kern_round(int64_t acc)
{
    return zt_kern_sat32((acc + 32768) >> 16);
}

/* One GGUF Q8_0 row (34-byte blocks, used in place) against nb 16-bit
 * activation blocks a (block b scaled by 2^ash[b]); Q16 result. */
static inline zt_fx zt_dot_raw_c(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                 uint32_t nb)
{
    int64_t acc = 0; /* Q32 */
    for (uint32_t b = 0; b < nb; b++, row += 34) {
        const int8_t *w = (const int8_t *) (row + 2);
        const int16_t *x = a + b * 32;
        int32_t s = 0; /* |s| <= 2^27: exact in 32 bits */
        for (uint32_t i = 0; i < 32; i++) s += (int32_t) w[i] * x[i];
        int32_t mm, e;
        zt_kern_f16((uint32_t) row[0] | ((uint32_t) row[1] << 8), &mm, &e);
        /* widen: s * mm < 2^38; weight = q mm 2^e, act = a 2^(sh-16) */
        acc += zt_term52((int64_t) s * mm, e + 16 + ash[b]);
    }
    return zt_kern_round(acc);
}

/* One row of zt_q8_t blocks against the same activations. */
static inline zt_fx zt_dot_q8_c(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    int64_t acc = 0;
    for (uint32_t b = 0; b < nb; b++) {
        const int16_t *x = a + b * 32;
        int32_t s = 0;
        for (uint32_t i = 0; i < 32; i++) s += (int32_t) w[b].q[i] * x[i];
        acc += zt_term52((int64_t) s * w[b].scale, (int32_t) ash[b] - (int32_t) w[b].shift);
    }
    return zt_kern_round(acc);
}

/* A set of kernels. Each function must return exactly what the C reference
 * returns for every input (test_zt_simd checks this). */
typedef zt_fx (*zt_dot_raw_fn)(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                               uint32_t nb);
typedef zt_fx (*zt_dot_q8_fn)(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb);
/* Row work for par_rows: compute rows [r0, r1). */
typedef void (*zt_rows_fn)(void *ctx, uint32_t r0, uint32_t r1);

typedef struct zt_kern {
    const char *name;
    zt_dot_raw_fn dot_raw; /* null: the C reference */
    zt_dot_q8_fn dot_q8;   /* null: the C reference */
    /* Optional: call fn over [0, n) in disjoint ranges that cover every row
     * once (possibly from several threads), and return when all are done.
     * Rows are independent, so the result does not depend on the split. */
    void (*par_rows)(void *pctx, zt_rows_fn fn, void *ctx, uint32_t n, uint64_t work);
    void *pctx;
} zt_kern_t;

#endif /* ZT_KERN_H */
