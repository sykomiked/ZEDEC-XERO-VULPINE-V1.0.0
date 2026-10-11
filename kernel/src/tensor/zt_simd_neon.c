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

static const zt_kern_t K_NEON = {"neon", dot_raw_neon, dot_q8_neon, 0,
                                 0,      dot_q4k_neon, dot_q6k_neon};

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
