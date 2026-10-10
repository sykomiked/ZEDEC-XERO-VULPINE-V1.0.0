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

static const zt_kern_t K_NEON = {"neon", dot_raw_neon, dot_q8_neon, 0, 0};

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
