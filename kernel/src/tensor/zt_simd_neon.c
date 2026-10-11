/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd_neon.c — AArch64 NEON versions of zt_kern.h's dot products.
 * HOSTED ONLY: never linked into a kernel image (those build with
 * -mgeneral-regs-only and keep the C reference). NEON (Advanced SIMD) is part
 * of every AArch64 CPU, Apple M-series included, so no run-time check.
 *
 * Per block of 32: the 8-bit weights are widened to 16 bits and multiplied
 * by the 16-bit activations with SMULL/SMLAL into 32-bit lanes (exact: a lane
 * collects 8 products below 2^22), the lanes are pair-added to the block's
 * exact 32-bit s, and term52(s * scale, k) is one saturating shift: SQSHL by a
 * signed count is an arithmetic (floor) right shift for k < 0, gives the sign
 * for k <= -64 and saturates on overflow for k > 0; clamping k to [-64, 63]
 * first and the result to +-2^52 after gives exactly the C reference's value.
 *
 * Why not SDOT / I8MM: those multiply 8-bit by 8-bit, and the activations are
 * 16-bit (act_quant keeps 15 bits of each block). An exact 16-bit product via
 * SDOT needs the activations split into two bytes and three dot products per
 * 16 weights (high byte, offset low byte, weight sum), which costs about as
 * many instructions as SMLAL does; it would only pay off with 8-bit
 * activations, which would change the engine's results. */
#include "zt_simd.h"
#include "zt_gguf.h"

#if defined(__aarch64__) && defined(__ARM_NEON)
#    include <arm_neon.h>

static inline uint32_t ld16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

/* 4 lanes whose sum is the block's s */
static inline int32x4_t mac_neon(const int8_t *w, const int16_t *x)
{
    int8x16_t w0 = vld1q_s8(w), w1 = vld1q_s8(w + 16);
    int16x8_t a0 = vld1q_s16(x), a1 = vld1q_s16(x + 8), a2 = vld1q_s16(x + 16),
              a3 = vld1q_s16(x + 24);
    int16x8_t l0 = vmovl_s8(vget_low_s8(w0)), h0 = vmovl_high_s8(w0);
    int16x8_t l1 = vmovl_s8(vget_low_s8(w1)), h1 = vmovl_high_s8(w1);
    int32x4_t p = vmull_s16(vget_low_s16(l0), vget_low_s16(a0));
    int32x4_t q = vmull_high_s16(l0, a0);
    p = vmlal_s16(p, vget_low_s16(h0), vget_low_s16(a1));
    q = vmlal_high_s16(q, h0, a1);
    p = vmlal_s16(p, vget_low_s16(l1), vget_low_s16(a2));
    q = vmlal_high_s16(q, l1, a2);
    p = vmlal_s16(p, vget_low_s16(h1), vget_low_s16(a3));
    q = vmlal_high_s16(q, h1, a3);
    return vaddq_s32(p, q);
}

static inline int32x4_t hsum4_neon(int32x4_t a, int32x4_t b, int32x4_t c, int32x4_t d)
{
    return vpaddq_s32(vpaddq_s32(a, b), vpaddq_s32(c, d));
}

/* zt_term52(s[i] * mul[i], k[i]) for 4 lanes, summed into two 64-bit lanes */
static inline int64x2_t term52_neon(int32x4_t s, int32x4_t mul, int32x4_t k)
{
    const int64x2_t lim = vdupq_n_s64(ZT_KERN_LIM52), nlim = vdupq_n_s64(-ZT_KERN_LIM52);
    k = vmaxq_s32(vminq_s32(k, vdupq_n_s32(63)), vdupq_n_s32(-64));
    int64x2_t lo =
        vqshlq_s64(vmull_s32(vget_low_s32(s), vget_low_s32(mul)), vmovl_s32(vget_low_s32(k)));
    int64x2_t hi = vqshlq_s64(vmull_high_s32(s, mul), vmovl_high_s32(k));
    lo = vbslq_s64(vcgtq_s64(lo, lim), lim, lo);
    lo = vbslq_s64(vcltq_s64(lo, nlim), nlim, lo);
    hi = vbslq_s64(vcgtq_s64(hi, lim), lim, hi);
    hi = vbslq_s64(vcltq_s64(hi, nlim), nlim, hi);
    return vaddq_s64(lo, hi);
}

/* f16 scales of 4 Q8_0 blocks: mm and k = e + 16 + ash, as zt_kern_f16 */
static inline void f16x4(const uint8_t *row, const uint8_t *ash, int32x4_t *mm, int32x4_t *k)
{
    uint32_t dv[4] = {ld16(row), ld16(row + 34), ld16(row + 68), ld16(row + 102)};
    uint32_t sv[4] = {ash[0], ash[1], ash[2], ash[3]};
    int32x4_t d = vreinterpretq_s32_u32(vld1q_u32(dv));
    int32x4_t ex = vandq_s32(vshrq_n_s32(d, 10), vdupq_n_s32(31));
    int32x4_t m = vandq_s32(d, vdupq_n_s32(0x3FF));
    uint32x4_t den = vceqq_s32(ex, vdupq_n_s32(0));
    m = vorrq_s32(m, vbicq_s32(vdupq_n_s32(0x400), vreinterpretq_s32_u32(den)));
    int32x4_t e = vbslq_s32(den, vdupq_n_s32(-24), vsubq_s32(ex, vdupq_n_s32(25)));
    uint32x4_t neg = vtstq_s32(d, vdupq_n_s32(0x8000));
    m = vbslq_s32(neg, vnegq_s32(m), m);
    m = vbicq_s32(m, vreinterpretq_s32_u32(vceqq_s32(ex, vdupq_n_s32(31)))); /* inf/NaN: zero */
    *mm = m;
    *k = vaddq_s32(vaddq_s32(e, vdupq_n_s32(16)), vreinterpretq_s32_u32(vld1q_u32(sv)));
}

static zt_fx dot_raw_neon(const uint8_t *row, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    int64x2_t acc = vdupq_n_s64(0);
    uint32_t b = 0;
    for (; b + 4 <= nb; b += 4, row += 4 * 34) {
        const int16_t *x = a + b * 32;
        int32x4_t s = hsum4_neon(mac_neon((const int8_t *) row + 2, x),
                                 mac_neon((const int8_t *) row + 36, x + 32),
                                 mac_neon((const int8_t *) row + 70, x + 64),
                                 mac_neon((const int8_t *) row + 104, x + 96));
        int32x4_t mm, k;
        f16x4(row, ash + b, &mm, &k);
        acc = vaddq_s64(acc, term52_neon(s, mm, k));
    }
    int64_t t = vaddvq_s64(acc);
    for (; b < nb; b++, row += 34) {
        int32_t s = vaddvq_s32(mac_neon((const int8_t *) row + 2, a + b * 32)), mm, e;
        zt_kern_f16(ld16(row), &mm, &e);
        t += zt_term52((int64_t) s * mm, e + 16 + ash[b]);
    }
    return zt_kern_round(t);
}

static zt_fx dot_q8_neon(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    int64x2_t acc = vdupq_n_s64(0);
    uint32_t b = 0;
    for (; b + 4 <= nb; b += 4) {
        const int16_t *x = a + b * 32;
        const zt_q8_t *v = w + b;
        int32x4_t s = hsum4_neon(mac_neon(v[0].q, x), mac_neon(v[1].q, x + 32),
                                 mac_neon(v[2].q, x + 64), mac_neon(v[3].q, x + 96));
        int32_t sc[4] = {v[0].scale, v[1].scale, v[2].scale, v[3].scale};
        int32_t k[4] = {(int32_t) ash[b] - v[0].shift, (int32_t) ash[b + 1] - v[1].shift,
                        (int32_t) ash[b + 2] - v[2].shift, (int32_t) ash[b + 3] - v[3].shift};
        acc = vaddq_s64(acc, term52_neon(s, vld1q_s32(sc), vld1q_s32(k)));
    }
    int64_t t = vaddvq_s64(acc);
    for (; b < nb; b++) {
        int32_t s = vaddvq_s32(mac_neon(w[b].q, a + b * 32));
        t += zt_term52((int64_t) s * w[b].scale, (int32_t) ash[b] - (int32_t) w[b].shift);
    }
    return zt_kern_round(t);
}

/* ---- K-quant rows in place (zt_kern.h zt_dot_q4k_c / zt_dot_q6k_c) ---- */

/* 4 lanes whose sum is sum w[i] x[i] over 32 weights given as two vectors
 * (the 4- and 6-bit weights fit int8, so the Q8_0 product applies) */
static inline int32x4_t mac_neon_v(int8x16_t w0, int8x16_t w1, const int16_t *x)
{
    int16x8_t a0 = vld1q_s16(x), a1 = vld1q_s16(x + 8), a2 = vld1q_s16(x + 16),
              a3 = vld1q_s16(x + 24);
    int16x8_t l0 = vmovl_s8(vget_low_s8(w0)), h0 = vmovl_high_s8(w0);
    int16x8_t l1 = vmovl_s8(vget_low_s8(w1)), h1 = vmovl_high_s8(w1);
    int32x4_t p = vmull_s16(vget_low_s16(l0), vget_low_s16(a0));
    int32x4_t q = vmull_high_s16(l0, a0);
    p = vmlal_s16(p, vget_low_s16(h0), vget_low_s16(a1));
    q = vmlal_high_s16(q, h0, a1);
    p = vmlal_s16(p, vget_low_s16(l1), vget_low_s16(a2));
    q = vmlal_high_s16(q, l1, a2);
    p = vmlal_s16(p, vget_low_s16(h1), vget_low_s16(a3));
    q = vmlal_high_s16(q, h1, a3);
    return vaddq_s32(p, q);
}

/* 4 lanes whose sum is sum w[i] x[i] over 16 weights */
static inline int32x4_t mac16_neon(int8x16_t w, const int16_t *x)
{
    int16x8_t a0 = vld1q_s16(x), a1 = vld1q_s16(x + 8);
    int16x8_t l = vmovl_s8(vget_low_s8(w)), h = vmovl_high_s8(w);
    int32x4_t p = vmull_s16(vget_low_s16(l), vget_low_s16(a0));
    int32x4_t q = vmull_high_s16(l, a0);
    p = vmlal_s16(p, vget_low_s16(h), vget_low_s16(a1));
    q = vmlal_high_s16(q, h, a1);
    return vaddq_s32(p, q);
}

/* zt_term52(v[i], k[i]) for 2 lanes of exact 64-bit values */
static inline int64x2_t term52v_neon(int64x2_t v, int32x2_t k)
{
    const int64x2_t lim = vdupq_n_s64(ZT_KERN_LIM52), nlim = vdupq_n_s64(-ZT_KERN_LIM52);
    k = vmax_s32(vmin_s32(k, vdup_n_s32(63)), vdup_n_s32(-64));
    v = vqshlq_s64(v, vmovl_s32(k));
    v = vbslq_s64(vcgtq_s64(v, lim), lim, v);
    return vbslq_s64(vcltq_s64(v, nlim), nlim, v);
}

static zt_fx dot_q4k_neon(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                          const int32_t *asum, uint32_t nb)
{
    const uint8x16_t m4 = vdupq_n_u8(0x0F);
    int64x2_t acc = vdupq_n_s64(0);
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q4K_BYTES) {
        const int16_t *x = a + k * 256;
        int32x4_t ps[8];
        for (uint32_t p = 0; p < 4; p++) {
            uint8x16_t b0 = vld1q_u8(row + 16 + 32 * p), b1 = vld1q_u8(row + 32 + 32 * p);
            ps[2 * p] = mac_neon_v(vreinterpretq_s8_u8(vandq_u8(b0, m4)),
                                   vreinterpretq_s8_u8(vandq_u8(b1, m4)), x + 64 * p);
            ps[2 * p + 1] = mac_neon_v(vreinterpretq_s8_u8(vshrq_n_u8(b0, 4)),
                                       vreinterpretq_s8_u8(vshrq_n_u8(b1, 4)), x + 64 * p + 32);
        }
        int32_t dm, de, mm, me;
        zt_kern_f16(ld16(row), &dm, &de);
        zt_kern_f16(ld16(row + 2), &mm, &me);
        uint8_t sm[16];
        zt_kern_q4k_scales(row + 4, sm);
        uint8x16_t smv = vld1q_u8(sm);
        uint16x8_t sc16 = vmovl_u8(vget_low_u8(smv)), mn16 = vmovl_high_u8(smv);
        int32x4_t sh0 =
            vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(vmovl_u8(vld1_u8(ash + k * 8)))));
        int32x4_t sh1 = vreinterpretq_s32_u32(vmovl_high_u16(vmovl_u8(vld1_u8(ash + k * 8))));
        int32x4_t dmv = vdupq_n_s32(dm), mmv = vdupq_n_s32(mm);
        int32x4_t de16 = vdupq_n_s32(de + 16), me16 = vdupq_n_s32(me + 16);
        int32x4_t s0 = hsum4_neon(ps[0], ps[1], ps[2], ps[3]);
        int32x4_t s1 = hsum4_neon(ps[4], ps[5], ps[6], ps[7]);
        acc = vaddq_s64(
            acc,
            term52_neon(s0, vmulq_s32(vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(sc16))), dmv),
                        vaddq_s32(sh0, de16)));
        acc = vaddq_s64(acc,
                        term52_neon(s1, vmulq_s32(vreinterpretq_s32_u32(vmovl_high_u16(sc16)), dmv),
                                    vaddq_s32(sh1, de16)));
        acc = vsubq_s64(
            acc, term52_neon(vld1q_s32(asum + k * 8),
                             vmulq_s32(vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(mn16))), mmv),
                             vaddq_s32(sh0, me16)));
        acc =
            vsubq_s64(acc, term52_neon(vld1q_s32(asum + k * 8 + 4),
                                       vmulq_s32(vreinterpretq_s32_u32(vmovl_high_u16(mn16)), mmv),
                                       vaddq_s32(sh1, me16)));
    }
    return zt_kern_round(vaddvq_s64(acc));
}

static zt_fx dot_q6k_neon(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                          const int32_t *asum, uint32_t nb)
{
    (void) asum;
    const uint8x16_t m4 = vdupq_n_u8(0x0F), m30 = vdupq_n_u8(0x30), o = vdupq_n_u8(32);
    int64x2_t acc = vdupq_n_s64(0);
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q6K_BYTES) {
        const int16_t *x = a + k * 256;
        int32x4_t p0[8], p1[8]; /* weights 0..15, 16..31 of each sub-block */
        for (uint32_t h = 0; h < 2; h++) {
            for (uint32_t i = 0; i < 2; i++) { /* 16 of the 32 weights of each sub-block */
                uint8x16_t l0 = vld1q_u8(row + h * 64 + 16 * i);
                uint8x16_t l1 = vld1q_u8(row + h * 64 + 32 + 16 * i);
                uint8x16_t hb = vld1q_u8(row + 128 + h * 32 + 16 * i);
                uint8x16_t q[4] = {vorrq_u8(vandq_u8(l0, m4), vandq_u8(vshlq_n_u8(hb, 4), m30)),
                                   vorrq_u8(vandq_u8(l1, m4), vandq_u8(vshlq_n_u8(hb, 2), m30)),
                                   vorrq_u8(vshrq_n_u8(l0, 4), vandq_u8(hb, m30)),
                                   vorrq_u8(vshrq_n_u8(l1, 4), vandq_u8(vshrq_n_u8(hb, 2), m30))};
                for (uint32_t u = 0; u < 4; u++) {
                    uint32_t j = 4 * h + u;
                    int32x4_t r =
                        mac16_neon(vreinterpretq_s8_u8(vsubq_u8(q[u], o)), x + 32 * j + 16 * i);
                    if (i)
                        p1[j] = r;
                    else
                        p0[j] = r;
                }
            }
        }
        int32_t dm, de;
        zt_kern_f16(ld16(row + 208), &dm, &de);
        /* sc[2j] (first 16 weights) and sc[2j + 1], times d.mm */
        int8x8x2_t sc = vld2_s8((const int8_t *) row + 192);
        int16x8_t sc0 = vmovl_s8(sc.val[0]), sc1 = vmovl_s8(sc.val[1]);
        int32x4_t dmv = vdupq_n_s32(dm);
        int16x8_t shv = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(ash + k * 8)));
        int32x4_t kv[2] = {vaddq_s32(vmovl_s16(vget_low_s16(shv)), vdupq_n_s32(de + 16)),
                           vaddq_s32(vmovl_high_s16(shv), vdupq_n_s32(de + 16))};
        int32x4_t m0[2] = {vmulq_s32(vmovl_s16(vget_low_s16(sc0)), dmv),
                           vmulq_s32(vmovl_high_s16(sc0), dmv)};
        int32x4_t m1[2] = {vmulq_s32(vmovl_s16(vget_low_s16(sc1)), dmv),
                           vmulq_s32(vmovl_high_s16(sc1), dmv)};
        for (uint32_t g = 0; g < 2; g++) {
            int32x4_t s0 = hsum4_neon(p0[4 * g], p0[4 * g + 1], p0[4 * g + 2], p0[4 * g + 3]);
            int32x4_t s1 = hsum4_neon(p1[4 * g], p1[4 * g + 1], p1[4 * g + 2], p1[4 * g + 3]);
            int64x2_t vlo = vmlal_s32(vmull_s32(vget_low_s32(s0), vget_low_s32(m0[g])),
                                      vget_low_s32(s1), vget_low_s32(m1[g]));
            int64x2_t vhi = vmlal_high_s32(vmull_high_s32(s0, m0[g]), s1, m1[g]);
            acc = vaddq_s64(acc, term52v_neon(vlo, vget_low_s32(kv[g])));
            acc = vaddq_s64(acc, term52v_neon(vhi, vget_high_s32(kv[g])));
        }
    }
    return zt_kern_round(vaddvq_s64(acc));
}

/* ---- batched rows (zt_kern.h zt_mmb_t): a lane per token ----
 * As the x86 tiles: each row's 256 weights are unpacked once to int16, a
 * weight pair is duplicated across the vector, and SMLAL/SMLAL2 against the
 * token-major activations collect each token's two products in two lanes,
 * which one pairwise add turns into the 4 tokens' exact block sums. A tile is
 * TRN rows by 8 tokens (two quads). The scale terms are added in slot order
 * (zt_kern.h ZT_MMB_SLOT): UZP1/UZP2 split a quad into its even and odd
 * tokens. term52 is one SSHL (a signed count: left, or a floor right shift)
 * when the block's largest exponent proves it cannot saturate, else
 * term52v_neon. */
#    define TRN    4u
#    define MMB_GC 8u

typedef struct {
    int32_t m0[8], m1[8], mt[8], ek[8], e, em;
} tparam_t;

static inline int32_t ld32(const int16_t *p)
{
    int32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

static inline void unpack_q4k_neon(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    int32_t dm, mm;
    zt_kern_f16(ld16(blk), &dm, &t->e);
    zt_kern_f16(ld16(blk + 2), &mm, &t->em);
    t->e += 16;
    t->em += 16;
    uint8_t sm[16];
    zt_kern_q4k_scales(blk + 4, sm);
    for (uint32_t i = 0; i < 8; i++) {
        t->m0[i] = sm[i] * dm;
        t->mt[i] = sm[8 + i] * mm;
    }
    const uint8x16_t m4 = vdupq_n_u8(0x0F);
    for (uint32_t p = 0; p < 8; p++) {
        uint8x16_t b = vld1q_u8(blk + 16 + 16 * p);
        uint8x16_t lo = vandq_u8(b, m4), hi = vshrq_n_u8(b, 4);
        int16_t *o = w + 64 * (p / 2) + 16 * (p % 2);
        vst1q_s16(o, vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(lo))));
        vst1q_s16(o + 8, vreinterpretq_s16_u16(vmovl_high_u8(lo)));
        vst1q_s16(o + 32, vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(hi))));
        vst1q_s16(o + 40, vreinterpretq_s16_u16(vmovl_high_u8(hi)));
    }
}

static inline void unpack_q6k_neon(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    int32_t dm;
    zt_kern_f16(ld16(blk + 208), &dm, &t->e);
    t->e += 16;
    const int8_t *sc = (const int8_t *) (blk + 192);
    for (uint32_t i = 0; i < 8; i++) {
        t->m0[i] = sc[2 * i] * dm;
        t->m1[i] = sc[2 * i + 1] * dm;
    }
    const uint8x16_t m4 = vdupq_n_u8(0x0F), m30 = vdupq_n_u8(0x30), o = vdupq_n_u8(32);
    for (uint32_t h = 0; h < 2; h++)
        for (uint32_t i = 0; i < 2; i++) {
            uint8x16_t l0 = vld1q_u8(blk + h * 64 + 16 * i);
            uint8x16_t l1 = vld1q_u8(blk + h * 64 + 32 + 16 * i);
            uint8x16_t hb = vld1q_u8(blk + 128 + h * 32 + 16 * i);
            uint8x16_t q[4] = {vorrq_u8(vandq_u8(l0, m4), vandq_u8(vshlq_n_u8(hb, 4), m30)),
                               vorrq_u8(vandq_u8(l1, m4), vandq_u8(vshlq_n_u8(hb, 2), m30)),
                               vorrq_u8(vshrq_n_u8(l0, 4), vandq_u8(hb, m30)),
                               vorrq_u8(vshrq_n_u8(l1, 4), vandq_u8(vshrq_n_u8(hb, 2), m30))};
            for (uint32_t u = 0; u < 4; u++) {
                int8x16_t v = vreinterpretq_s8_u8(vsubq_u8(q[u], o));
                int16_t *d = w + 32 * (4 * h + u) + 16 * i;
                vst1q_s16(d, vmovl_s8(vget_low_s8(v)));
                vst1q_s16(d + 8, vmovl_high_s8(v));
            }
        }
}

static inline void unpack_q8_0_neon(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    for (uint32_t i = 0; i < 8; i++, blk += 34) {
        zt_kern_f16(ld16(blk), &t->m0[i], &t->ek[i]);
        t->ek[i] += 16;
        int8x16_t a = vld1q_s8((const int8_t *) blk + 2), b = vld1q_s8((const int8_t *) blk + 18);
        vst1q_s16(w + 32 * i, vmovl_s8(vget_low_s8(a)));
        vst1q_s16(w + 32 * i + 8, vmovl_high_s8(a));
        vst1q_s16(w + 32 * i + 16, vmovl_s8(vget_low_s8(b)));
        vst1q_s16(w + 32 * i + 24, vmovl_high_s8(b));
    }
}

/* term52(v, e + h) for 2 slots */
static inline int64x2_t term2_neon(int64x2_t v, int64x2_t h, int32_t e, int32_t hx, int32_t safe)
{
    int64x2_t k = vaddq_s64(h, vdupq_n_s64(e));
    if (hx + e <= safe) return vshlq_s64(v, k);
    return term52v_neon(v, vmovn_s64(k));
}

/* s[i][q] = row i's pairs [p0, p1) against quad q of the half (x: its 2 quads) */
static inline void dot_neon(const int16_t *ws, const int16_t *x, uint32_t p0, uint32_t p1,
                            int32x4_t s[TRN][2])
{
    int32x4_t l[TRN][2], h[TRN][2];
    for (uint32_t i = 0; i < TRN; i++)
        for (uint32_t q = 0; q < 2; q++) l[i][q] = h[i][q] = vdupq_n_s32(0);
    for (uint32_t p = p0; p < p1; p++) {
        int16x8_t x0 = vld1q_s16(x + 32 * p), x1 = vld1q_s16(x + 32 * p + 8);
        for (uint32_t i = 0; i < TRN; i++) {
            int16x8_t wv = vreinterpretq_s16_s32(vdupq_n_s32(ld32(ws + 256 * i + 2 * p)));
            l[i][0] = vmlal_s16(l[i][0], vget_low_s16(x0), vget_low_s16(wv));
            h[i][0] = vmlal_high_s16(h[i][0], x0, wv);
            l[i][1] = vmlal_s16(l[i][1], vget_low_s16(x1), vget_low_s16(wv));
            h[i][1] = vmlal_high_s16(h[i][1], x1, wv);
        }
    }
    for (uint32_t i = 0; i < TRN; i++)
        for (uint32_t q = 0; q < 2; q++) s[i][q] = vpaddq_s32(l[i][q], h[i][q]);
}

static inline __attribute__((always_inline)) void mmb_body_neon(const zt_mmb_t *j, uint32_t r0,
                                                                uint32_t r1, const uint32_t qt)
{
    uint32_t nb = j->cols / 32, nk = nb / 8, ng = (j->B + ZT_MMB_G - 1) / ZT_MMB_G;
    uint64_t kbytes = qt == ZT_GGML_Q4_K ? ZT_Q4K_BYTES : qt == ZT_GGML_Q6_K ? ZT_Q6K_BYTES : 8 * 34;
    int32_t safe = qt == ZT_GGML_Q4_K   ? ZT_MMB_SAFE_Q4K
                   : qt == ZT_GGML_Q6_K ? ZT_MMB_SAFE_Q6K
                                        : ZT_MMB_SAFE_Q8;
    int16_t w[TRN * 256] __attribute__((aligned(64)));
    int64_t acc[TRN][MMB_GC][ZT_MMB_G] __attribute__((aligned(64)));
    tparam_t tp[TRN];
    for (uint32_t r = r0; r < r1; r += TRN) {
        uint32_t nr = r1 - r < TRN ? r1 - r : TRN;
        if (nr < TRN) __builtin_memset(w, 0, sizeof w);
        for (uint32_t g0 = 0; g0 < ng; g0 += MMB_GC) {
            uint32_t gn = ng - g0 < MMB_GC ? ng - g0 : MMB_GC;
            __builtin_memset(acc, 0, sizeof acc);
            for (uint32_t k = 0; k < nk; k++) {
                for (uint32_t i = 0; i < nr; i++) {
                    const uint8_t *blk = j->raw + ((uint64_t) (r + i) * nk + k) * kbytes;
                    if (qt == ZT_GGML_Q4_K)
                        unpack_q4k_neon(blk, w + 256 * i, &tp[i]);
                    else if (qt == ZT_GGML_Q6_K)
                        unpack_q6k_neon(blk, w + 256 * i, &tp[i]);
                    else
                        unpack_q8_0_neon(blk, w + 256 * i, &tp[i]);
                }
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t sb = 0; sb < 8; sb++) {
                        uint64_t gb = (uint64_t) (g0 + gi) * nb + k * 8 + sb;
                        const int64_t *hp = j->ashT + gb * ZT_MMB_G;
                        const int64_t *up = j->asumT + gb * ZT_MMB_G;
                        int32_t hx = j->ashx[gb];
                        for (uint32_t hf = 0; hf < 2; hf++) {
                            const int16_t *x = j->aT + gb * 32 * ZT_MMB_G + 16 * hf;
                            int32x4_t s[TRN][2], s2[TRN][2];
                            if (qt == ZT_GGML_Q6_K) {
                                dot_neon(w + 32 * sb, x, 0, 8, s);
                                dot_neon(w + 32 * sb, x, 8, 16, s2);
                            } else {
                                dot_neon(w + 32 * sb, x, 0, 16, s);
                            }
                            for (uint32_t i = 0; i < nr; i++) {
                                const tparam_t *t = &tp[i];
                                int64_t *a = acc[i][gi];
                                for (uint32_t q = 0; q < 2; q++) {
                                    uint32_t Q = 2 * hf + q; /* quad of the group: slots 2Q, 8 + 2Q */
                                    int64x2_t he = vld1q_s64(hp + 2 * Q), ho = vld1q_s64(hp + 8 + 2 * Q);
                                    int32x4_t sv = s[i][q];
                                    int32x2_t se = vget_low_s32(vuzp1q_s32(sv, sv));
                                    int32x2_t so = vget_low_s32(vuzp2q_s32(sv, sv));
                                    int32x2_t M0 = vdup_n_s32(t->m0[sb]);
                                    int64x2_t ve = vmull_s32(se, M0), vo = vmull_s32(so, M0);
                                    if (qt == ZT_GGML_Q6_K) {
                                        int32x4_t s2v = s2[i][q];
                                        int32x2_t M1 = vdup_n_s32(t->m1[sb]);
                                        ve = vmlal_s32(ve, vget_low_s32(vuzp1q_s32(s2v, s2v)), M1);
                                        vo = vmlal_s32(vo, vget_low_s32(vuzp2q_s32(s2v, s2v)), M1);
                                        ve = term2_neon(ve, he, t->e, hx, safe);
                                        vo = term2_neon(vo, ho, t->e, hx, safe);
                                    } else if (qt == ZT_GGML_Q4_K) {
                                        int32x2_t MT = vdup_n_s32(t->mt[sb]);
                                        int64x2_t ue = vmull_s32(vmovn_s64(vld1q_s64(up + 2 * Q)), MT);
                                        int64x2_t uo =
                                            vmull_s32(vmovn_s64(vld1q_s64(up + 8 + 2 * Q)), MT);
                                        ve = vsubq_s64(term2_neon(ve, he, t->e, hx, safe),
                                                       term2_neon(ue, he, t->em, hx, safe));
                                        vo = vsubq_s64(term2_neon(vo, ho, t->e, hx, safe),
                                                       term2_neon(uo, ho, t->em, hx, safe));
                                    } else {
                                        ve = term2_neon(ve, he, t->ek[sb], hx, safe);
                                        vo = term2_neon(vo, ho, t->ek[sb], hx, safe);
                                    }
                                    vst1q_s64(a + 2 * Q, vaddq_s64(vld1q_s64(a + 2 * Q), ve));
                                    vst1q_s64(a + 8 + 2 * Q, vaddq_s64(vld1q_s64(a + 8 + 2 * Q), vo));
                                }
                            }
                        }
                    }
            }
            for (uint32_t i = 0; i < nr; i++)
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t t = 0; t < ZT_MMB_G; t++) {
                        uint32_t tok = (g0 + gi) * ZT_MMB_G + t;
                        if (tok < j->B)
                            j->y[(uint64_t) tok * j->ystride + r + i] =
                                zt_kern_round(acc[i][gi][ZT_MMB_SLOT(t)]);
                    }
        }
    }
}

static int mmb_neon(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!j->raw) return 1;
    if (j->qtype == ZT_GGML_Q4_K)
        mmb_body_neon(j, r0, r1, ZT_GGML_Q4_K);
    else if (j->qtype == ZT_GGML_Q6_K)
        mmb_body_neon(j, r0, r1, ZT_GGML_Q6_K);
    else if (j->qtype == ZT_GGML_Q8_0 && j->cols % 256 == 0)
        mmb_body_neon(j, r0, r1, ZT_GGML_Q8_0);
    else
        return 1;
    return 0;
}

/* ---- attention over a Q16 KV cache (zt_kern.h): SMULL/SMLAL 32 x 32 -> 64 ---- */

static void att_qk_neon(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                        uint32_t hd, int64_t *S)
{
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        int64x2_t a = vdupq_n_s64(0), b = vdupq_n_s64(0);
        uint32_t j = 0;
        for (; j + 4 <= hd; j += 4) {
            int32x4_t x = vld1q_s32(q + j), y = vld1q_s32(k + j);
            a = vmlal_s32(a, vget_low_s32(x), vget_low_s32(y));
            b = vmlal_high_s32(b, x, y);
        }
        int64_t t = vaddvq_s64(vaddq_s64(a, b));
        for (; j < hd; j++) t += (int64_t) q[j] * k[j];
        S[i] = t;
    }
}

static void att_wv_neon(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                        uint32_t hd, int64_t *acc)
{
    uint32_t j0 = 0;
    for (; j0 + 32 <= hd; j0 += 32) {
        int64x2_t a[16];
        for (uint32_t c = 0; c < 16; c++) a[c] = vld1q_s64(acc + j0 + 2 * c);
        const int32_t *vi = v + j0;
        for (uint32_t i = 0; i < n; i++, vi += vstride) {
            if (!w[i]) continue;
            int32x2_t wv = vdup_n_s32(w[i]);
            for (uint32_t c = 0; c < 8; c++) {
                int32x4_t x = vld1q_s32(vi + 4 * c);
                a[2 * c] = vmlal_s32(a[2 * c], vget_low_s32(x), wv);
                a[2 * c + 1] = vmlal_s32(a[2 * c + 1], vget_high_s32(x), wv);
            }
        }
        for (uint32_t c = 0; c < 16; c++) vst1q_s64(acc + j0 + 2 * c, a[c]);
    }
    if (j0 < hd) zt_att_wv_c(w, v + j0, vstride, n, hd - j0, acc + j0);
}

static const zt_kern_t K_NEON = {"neon", dot_raw_neon, dot_q8_neon, 0, 0, dot_q4k_neon,
                                 dot_q6k_neon, mmb_neon,     att_qk_neon, att_wv_neon};

const zt_kern_t *zt_simd_neon(void)
{
    return &K_NEON;
}

#else

const zt_kern_t *zt_simd_neon(void)
{
    return 0;
}

#endif
