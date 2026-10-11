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

/* ---- K-quant rows used in place (GGUF Q4_K and Q6_K, 256 weights a block) ----
 * Each 32-weight sub-block meets one activation block. Its exact 32-bit sums
 * are formed first, then widened and scaled once per term, as above, so any
 * SIMD path that forms the same sums gives the same bits. */

#define ZT_Q4K_BYTES 144u /* d, dmin (f16), 12 bytes of 6-bit scales and mins, 128 of nibbles */
#define ZT_Q6K_BYTES 210u /* 128 low nibbles, 64 bytes of high bit pairs, 16 int8 scales, d */

/* Q4_K scales and mins of one block as bytes: sc[0..7] then mn[0..7] (ggml's
 * 6-bit packing, unpacked as llama.cpp does with three masks). */
static inline void zt_kern_q4k_scales(const uint8_t *p12, uint8_t out[16])
{
    uint32_t u[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < 12; i++) u[i / 4] |= (uint32_t) p12[i] << (8 * (i % 4));
    const uint32_t k1 = 0x3f3f3f3fu, k2 = 0x0f0f0f0fu, k3 = 0x03030303u;
    uint32_t r0 = u[0] & k1, r1 = (u[2] & k2) | (((u[0] >> 6) & k3) << 4);
    uint32_t r2 = u[1] & k1, r3 = ((u[2] >> 4) & k2) | (((u[1] >> 6) & k3) << 4);
    uint32_t r[4] = {r0, r1, r2, r3};
    for (uint32_t i = 0; i < 16; i++) out[i] = (uint8_t) (r[i / 4] >> (8 * (i % 4)));
}

/* One Q4_K row (nb / 8 blocks) against nb activation blocks; asum[b] is the
 * exact sum of activation block b's 32 values (act_quant writes it, so it is
 * formed once per input instead of once per row). Sub-block j of a block has
 * weights d*sc*q - dmin*mn (q 0..15); with s = sum q x (|s| < 2^24) and
 * t = asum (|t| <= 2^20) it adds
 * term52(d.mm * sc * s, d.e + 16 + sh) - term52(dmin.mm * mn * t, dmin.e + 16 + sh). */
static inline zt_fx zt_dot_q4k_c(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                 const int32_t *asum, uint32_t nb)
{
    int64_t acc = 0; /* Q32 */
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q4K_BYTES) {
        int32_t dm, de, mm, me;
        zt_kern_f16((uint32_t) row[0] | ((uint32_t) row[1] << 8), &dm, &de);
        zt_kern_f16((uint32_t) row[2] | ((uint32_t) row[3] << 8), &mm, &me);
        uint8_t sm[16];
        zt_kern_q4k_scales(row + 4, sm);
        const uint8_t *q = row + 16;
        for (uint32_t j = 0; j < 8; j++) {
            uint32_t b = k * 8 + j;
            const int16_t *x = a + b * 32;
            const uint8_t *qj = q + (j / 2) * 32;
            uint32_t hi = (j & 1) * 4;
            int32_t s = 0;
            for (uint32_t i = 0; i < 32; i++) s += (int32_t) ((qj[i] >> hi) & 0xF) * x[i];
            acc += zt_term52((int64_t) s * sm[j] * dm, de + 16 + ash[b]);
            acc -= zt_term52((int64_t) asum[b] * sm[8 + j] * mm, me + 16 + ash[b]);
        }
    }
    return zt_kern_round(acc);
}

/* Q6_K weight i (0..255) of a block, as a signed value in -32..31. */
static inline int32_t zt_kern_q6k_q(const uint8_t *blk, uint32_t i)
{
    uint32_t h = i / 128, r = i % 128, quarter = r / 32, l = r % 32;
    const uint8_t *ql = blk + h * 64, *qh = blk + 128 + h * 32;
    uint32_t lo = quarter & 1 ? ql[l + 32] : ql[l];
    lo = quarter & 2 ? lo >> 4 : lo & 0xF;
    return (int32_t) (lo | (((uint32_t) qh[l] >> (2 * quarter) & 3) << 4)) - 32;
}

/* One Q6_K row against nb activation blocks. A 32-weight sub-block has two
 * int8 scales (one per 16 weights): its term is
 * term52(d.mm * (sc0 * s0 + sc1 * s1), d.e + 16 + sh), |sc s| < 2^31. */
static inline zt_fx zt_dot_q6k_c(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                 const int32_t *asum, uint32_t nb)
{
    (void) asum;
    int64_t acc = 0;
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q6K_BYTES) {
        int32_t dm, de;
        zt_kern_f16((uint32_t) row[208] | ((uint32_t) row[209] << 8), &dm, &de);
        const int8_t *sc = (const int8_t *) (row + 192);
        for (uint32_t j = 0; j < 8; j++) {
            uint32_t b = k * 8 + j;
            const int16_t *x = a + b * 32;
            int32_t s0 = 0, s1 = 0;
            for (uint32_t i = 0; i < 16; i++) {
                s0 += zt_kern_q6k_q(row, j * 32 + i) * x[i];
                s1 += zt_kern_q6k_q(row, j * 32 + 16 + i) * x[16 + i];
            }
            /* sub-block j covers scales 2j, 2j + 1 (ggml: is = l / 16 per quarter) */
            int64_t v = (int64_t) sc[2 * j] * s0 + (int64_t) sc[2 * j + 1] * s1;
            acc += zt_term52(v * dm, de + 16 + ash[b]);
        }
    }
    return zt_kern_round(acc);
}

/* ---- batched rows (prefill): one weight row against many tokens ----
 * A batch of B activation vectors (act_quant's a16, ash and asum for each
 * token) is repacked token-major so that a SIMD lane holds one token: groups
 * of ZT_MMB_G tokens; for each group g, block b (32 values) and pair p
 * (values 2p and 2p + 1 of the block), the 16 tokens' two int16 values side
 * by side (64 bytes). A kernel then unpacks each weight block once, broadcasts
 * a weight pair to every lane, and forms 16 tokens' exact block sums with no
 * horizontal adds. The block exponents and sums sit beside them as int64 per
 * token, even tokens first (slot ZT_MMB_SLOT(t)), which is the order in
 * which a 64-bit multiply of the even and then the odd 32-bit lanes yields the
 * products, and ashx holds each (group, block)'s largest exponent so a kernel can
 * tell when term52 cannot saturate. Missing tokens of the last group are
 * zero (their sums are 0, their outputs are not written).
 * The block sums are the same exact integers as the per-row kernels', and
 * term52 is applied to the same values, so y is bit for bit what the dot
 * products above give for each token. */
#define ZT_MMB_G 16u
#define ZT_MMB_SLOT(t) (((t) & 1u) * (ZT_MMB_G / 2) + (t) / 2)

typedef struct zt_mmb {
    const uint8_t *raw; /* GGUF rows (qtype), or null with q8 */
    const zt_q8_t *q8;
    uint32_t qtype, cols;
    const int16_t *aT;    /* [ng][cols / 32][16 pairs][ZT_MMB_G][2] */
    const int64_t *ashT;  /* [ng][cols / 32][ZT_MMB_G slots] */
    const int64_t *asumT; /* [ng][cols / 32][ZT_MMB_G slots] */
    const uint8_t *ashx;  /* [ng][cols / 32] */
    uint32_t B;           /* tokens; ng = (B + ZT_MMB_G - 1) / ZT_MMB_G */
    zt_fx *y;             /* y[t * ystride + r] = the row r, token t dot product (Q16) */
    uint32_t ystride;
} zt_mmb_t;

/* Repack B tokens' activations (a16 rows of astride values, cols used) into
 * the layout above: aT holds ng * cols * ZT_MMB_G int16, ashT and asumT
 * ng * (cols / 32) * ZT_MMB_G values each, ashx ng * (cols / 32). */
static inline void zt_mmb_pack(const int16_t *a16, const uint8_t *ash, const int32_t *asum,
                               uint32_t astride, uint32_t B, uint32_t cols, int16_t *aT,
                               int64_t *ashT, int64_t *asumT, uint8_t *ashx)
{
    uint32_t nb = cols / 32, ng = (B + ZT_MMB_G - 1) / ZT_MMB_G;
    for (uint32_t g = 0; g < ng; g++)
        for (uint32_t b = 0; b < nb; b++) {
            uint64_t gb = (uint64_t) g * nb + b;
            int16_t *o = aT + gb * 32 * ZT_MMB_G;
            uint8_t mx = 0;
            for (uint32_t t = 0; t < ZT_MMB_G; t++) {
                uint32_t tok = g * ZT_MMB_G + t;
                if (tok < B) {
                    const int16_t *x = a16 + (uint64_t) tok * astride + b * 32;
                    for (uint32_t p = 0; p < 16; p++) {
                        o[(p * ZT_MMB_G + t) * 2] = x[2 * p];
                        o[(p * ZT_MMB_G + t) * 2 + 1] = x[2 * p + 1];
                    }
                    uint8_t sh = ash[(uint64_t) tok * (astride / 32) + b];
                    ashT[gb * ZT_MMB_G + ZT_MMB_SLOT(t)] = sh;
                    asumT[gb * ZT_MMB_G + ZT_MMB_SLOT(t)] = asum[(uint64_t) tok * (astride / 32) + b];
                    if (sh > mx) mx = sh;
                } else {
                    for (uint32_t p = 0; p < 16; p++) {
                        o[(p * ZT_MMB_G + t) * 2] = 0;
                        o[(p * ZT_MMB_G + t) * 2 + 1] = 0;
                    }
                    ashT[gb * ZT_MMB_G + ZT_MMB_SLOT(t)] = 0;
                    asumT[gb * ZT_MMB_G + ZT_MMB_SLOT(t)] = 0;
                }
            }
            ashx[gb] = mx;
        }
}

/* Largest k = e + ash for which term52(v, k) cannot saturate, for each
 * weight type's bound on |v| (|x| <= 32767): Q4_K |s * sc * d| < 2^41 (and
 * its min term < 2^37), Q6_K |(sc0 s0 + sc1 s1) d| < 2^43, Q8_0 |s d| < 2^38.
 * At or below it, term52 is floor(v * 2^k), which a kernel may form as
 * (v << L) >> R with L, R >= 0 and L - R = k (no 64-bit overflow while
 * L <= 17 + max(e, 0)). */
#define ZT_MMB_SAFE_Q4K 11
#define ZT_MMB_SAFE_Q6K 9
#define ZT_MMB_SAFE_Q8  14

/* ---- attention over a Q16 KV cache ----
 * S[i] = q . k_i (n positions, k_i = k + i * kstride, hd values) and
 * acc[j] += sum_i w[i] v_i[j]: exact 64-bit sums of 32 x 32-bit products
 * (|q| < 2^22 and |w| <= 2^16, so no sum can overflow), the same in any
 * order. */
static inline void zt_att_qk_c(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                               uint32_t hd, int64_t *S)
{
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        int64_t t = 0;
        for (uint32_t j = 0; j < hd; j++) t += (int64_t) q[j] * k[j];
        S[i] = t;
    }
}

static inline void zt_att_wv_c(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                               uint32_t hd, int64_t *acc)
{
    for (uint32_t i = 0; i < n; i++, v += vstride) {
        int64_t wi = w[i];
        if (!wi) continue;
        for (uint32_t j = 0; j < hd; j++) acc[j] += wi * v[j];
    }
}

typedef void (*zt_att_qk_fn)(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                             uint32_t hd, int64_t *S);
typedef void (*zt_att_wv_fn)(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                             uint32_t hd, int64_t *acc);

/* A set of kernels. Each function must return exactly what the C reference
 * returns for every input (test_zt_simd checks this). */
typedef zt_fx (*zt_dot_raw_fn)(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                               uint32_t nb);
typedef zt_fx (*zt_dot_q8_fn)(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb);
/* K-quant rows: as zt_dot_raw_fn, plus the activation block sums. */
typedef zt_fx (*zt_dot_k_fn)(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                             const int32_t *asum, uint32_t nb);
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
    zt_dot_k_fn dot_q4k; /* null: the C reference (rows of ZT_Q4K_BYTES blocks) */
    zt_dot_k_fn dot_q6k; /* null: the C reference (rows of ZT_Q6K_BYTES blocks) */
    /* Optional: rows [r0, r1) of a zt_mmb_t job for all its tokens. Returns 0
     * when done, nonzero (having written nothing) when it does not handle the
     * job's weight type; the caller then uses the dot products above. */
    int (*mm_batch)(const zt_mmb_t *j, uint32_t r0, uint32_t r1);
    zt_att_qk_fn att_qk; /* null: the C reference (Q16 KV cache only) */
    zt_att_wv_fn att_wv; /* null: the C reference (Q16 KV cache only) */
} zt_kern_t;

#endif /* ZT_KERN_H */
