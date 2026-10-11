/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd_x86.c — x86-64 AVX2 and AVX-512 versions of zt_kern.h's dot
 * products. HOSTED ONLY: never linked into a kernel image (those build with
 * -mgeneral-regs-only and keep the C reference).
 *
 * Each function is compiled for its ISA with a target attribute, so this file
 * builds with plain flags (no -mavx2) and zt_simd.c only calls a kernel after
 * the CPU check says it may (__builtin_cpu_supports, which also checks that
 * the OS saves the vector registers).
 *
 * Per block of 32: the 8-bit weights are sign-extended to 16 bits and
 * multiplied by the 16-bit activations with VPMADDWD (exact: each pair sum
 * is below 2^24), the lanes are summed to the block's exact 32-bit s, and
 * term52(s * scale, k) is evaluated in 64-bit lanes with the same integer
 * steps as the C reference: floor right shift (as an unsigned shift of
 * v ^ sign), saturation at +-2^52 before a left shift can overflow, and the
 * final clamp. The 64-bit lane sums are added in a different order than the
 * C loop, which cannot change the result: every term is at most 2^52 and a
 * row has at most 1024 blocks, so no partial sum overflows. */
#include "zt_simd.h"

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#    include <immintrin.h>

#    define AVX2   __attribute__((target("avx2")))
#    define AVX512 __attribute__((target("avx2,avx512f,avx512bw,avx512vl,avx512dq")))

/* Bytes ahead to prefetch: a decode step streams the weights once, and the
 * hardware prefetcher alone leaves half the memory bandwidth unused here. */
#    define PF_AHEAD 1024

static inline uint32_t ld16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

/* ---- AVX2 ---- */

/* 8 lanes whose sum is the block's s */
static inline AVX2 __m256i mac_avx2(const int8_t *w, const int16_t *x)
{
    __m128i wl = _mm_loadu_si128((const __m128i *) w);
    __m128i wh = _mm_loadu_si128((const __m128i *) (w + 16));
    __m256i p =
        _mm256_madd_epi16(_mm256_cvtepi8_epi16(wl), _mm256_loadu_si256((const __m256i *) x));
    __m256i q =
        _mm256_madd_epi16(_mm256_cvtepi8_epi16(wh), _mm256_loadu_si256((const __m256i *) (x + 16)));
    return _mm256_add_epi32(p, q);
}

/* [sum a, sum b, sum c, sum d] */
static inline AVX2 __m128i hsum4_avx2(__m256i a, __m256i b, __m256i c, __m256i d)
{
    __m256i ab = _mm256_hadd_epi32(a, b);
    __m256i cd = _mm256_hadd_epi32(c, d);
    __m256i t = _mm256_hadd_epi32(ab, cd);
    return _mm_add_epi32(_mm256_castsi256_si128(t), _mm256_extracti128_si256(t, 1));
}

static inline AVX2 int32_t hsum1_avx2(__m256i a)
{
    __m128i t = _mm_add_epi32(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
    return _mm_cvtsi128_si32(t);
}

static inline AVX2 __m256i blend64(__m256i a, __m256i b, __m256i mask)
{
    return _mm256_blendv_epi8(a, b, mask);
}

/* zt_term52(v[i], k[i]) for 4 lanes of exact 64-bit products */
static inline AVX2 __m256i term52v_avx2(__m256i v, __m128i k)
{
    const __m256i zero = _mm256_setzero_si256();
    const __m256i lim = _mm256_set1_epi64x(ZT_KERN_LIM52);
    const __m256i nlim = _mm256_set1_epi64x(-ZT_KERN_LIM52);
    __m256i kk = _mm256_cvtepi32_epi64(k);
    __m256i sign = _mm256_cmpgt_epi64(zero, v);
    __m256i vx = _mm256_xor_si256(v, sign);
    /* k <= 0: floor(v / 2^-k); a count of 64 or more gives the sign (0 or -1) */
    __m256i right = _mm256_xor_si256(_mm256_srlv_epi64(vx, _mm256_sub_epi64(zero, kk)), sign);
    /* k > 0: +-2^52 when |v| > 2^52 >> k, else v << k */
    __m256i av = _mm256_sub_epi64(vx, sign);
    __m256i sat = _mm256_cmpgt_epi64(av, _mm256_srlv_epi64(lim, kk));
    __m256i slim = _mm256_sub_epi64(_mm256_xor_si256(lim, sign), sign);
    __m256i left = blend64(_mm256_sllv_epi64(v, kk), slim, sat);
    __m256i r = blend64(right, left, _mm256_cmpgt_epi64(kk, zero));
    r = blend64(r, lim, _mm256_cmpgt_epi64(r, lim));
    return blend64(r, nlim, _mm256_cmpgt_epi64(nlim, r));
}

/* exact 64-bit s[i] * mul[i] for 4 int32 lanes */
static inline AVX2 __m256i mul64_avx2(__m128i s, __m128i mul)
{
    return _mm256_mul_epi32(_mm256_cvtepi32_epi64(s), _mm256_cvtepi32_epi64(mul));
}

/* zt_term52(s[i] * mul[i], k[i]) for 4 lanes */
static inline AVX2 __m256i term52_avx2(__m128i s, __m128i mul, __m128i k)
{
    return term52v_avx2(mul64_avx2(s, mul), k);
}

static inline AVX2 int64_t hsum64_avx2(__m256i a)
{
    __m128i t = _mm_add_epi64(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
    return _mm_cvtsi128_si64(t) + _mm_extract_epi64(t, 1);
}

/* f16 scales of 4 Q8_0 blocks: mm and k = e + 16 + ash, as zt_kern_f16 */
static inline AVX2 void f16x4(const uint8_t *row, const uint8_t *ash, __m128i *mm, __m128i *k)
{
    __m128i d = _mm_setr_epi32((int32_t) ld16(row), (int32_t) ld16(row + 34),
                               (int32_t) ld16(row + 68), (int32_t) ld16(row + 102));
    __m128i sh = _mm_setr_epi32(ash[0], ash[1], ash[2], ash[3]);
    __m128i ex = _mm_and_si128(_mm_srli_epi32(d, 10), _mm_set1_epi32(31));
    __m128i m = _mm_and_si128(d, _mm_set1_epi32(0x3FF));
    __m128i den = _mm_cmpeq_epi32(ex, _mm_setzero_si128());
    m = _mm_or_si128(m, _mm_andnot_si128(den, _mm_set1_epi32(0x400)));
    __m128i e = _mm_blendv_epi8(_mm_sub_epi32(ex, _mm_set1_epi32(25)), _mm_set1_epi32(-24), den);
    __m128i neg = _mm_cmpgt_epi32(_mm_and_si128(d, _mm_set1_epi32(0x8000)), _mm_setzero_si128());
    m = _mm_sub_epi32(_mm_xor_si128(m, neg), neg);
    m = _mm_andnot_si128(_mm_cmpeq_epi32(ex, _mm_set1_epi32(31)), m); /* inf/NaN: zero */
    *mm = m;
    *k = _mm_add_epi32(_mm_add_epi32(e, _mm_set1_epi32(16)), sh);
}

static AVX2 zt_fx dot_raw_avx2(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                               uint32_t nb)
{
    __m256i acc = _mm256_setzero_si256();
    uint32_t b = 0;
    for (; b + 4 <= nb; b += 4, row += 4 * 34) {
        const int16_t *x = a + b * 32;
        _mm_prefetch((const char *) row + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 128, _MM_HINT_T0);
        __m128i s = hsum4_avx2(mac_avx2((const int8_t *) row + 2, x),
                               mac_avx2((const int8_t *) row + 36, x + 32),
                               mac_avx2((const int8_t *) row + 70, x + 64),
                               mac_avx2((const int8_t *) row + 104, x + 96));
        __m128i mm, k;
        f16x4(row, ash + b, &mm, &k);
        acc = _mm256_add_epi64(acc, term52_avx2(s, mm, k));
    }
    int64_t t = hsum64_avx2(acc);
    for (; b < nb; b++, row += 34) {
        int32_t s = hsum1_avx2(mac_avx2((const int8_t *) row + 2, a + b * 32)), mm, e;
        zt_kern_f16(ld16(row), &mm, &e);
        t += zt_term52((int64_t) s * mm, e + 16 + ash[b]);
    }
    return zt_kern_round(t);
}

static AVX2 zt_fx dot_q8_avx2(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    __m256i acc = _mm256_setzero_si256();
    uint32_t b = 0;
    for (; b + 4 <= nb; b += 4) {
        const int16_t *x = a + b * 32;
        const zt_q8_t *v = w + b;
        _mm_prefetch((const char *) v + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) v + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) v + PF_AHEAD + 128, _MM_HINT_T0);
        __m128i s = hsum4_avx2(mac_avx2(v[0].q, x), mac_avx2(v[1].q, x + 32),
                               mac_avx2(v[2].q, x + 64), mac_avx2(v[3].q, x + 96));
        __m128i sc = _mm_setr_epi32(v[0].scale, v[1].scale, v[2].scale, v[3].scale);
        __m128i k = _mm_sub_epi32(_mm_setr_epi32(ash[b], ash[b + 1], ash[b + 2], ash[b + 3]),
                                  _mm_setr_epi32(v[0].shift, v[1].shift, v[2].shift, v[3].shift));
        acc = _mm256_add_epi64(acc, term52_avx2(s, sc, k));
    }
    int64_t t = hsum64_avx2(acc);
    for (; b < nb; b++) {
        int32_t s = hsum1_avx2(mac_avx2(w[b].q, a + b * 32));
        t += zt_term52((int64_t) s * w[b].scale, (int32_t) ash[b] - (int32_t) w[b].shift);
    }
    return zt_kern_round(t);
}

/* ---- AVX-512 (F, BW, VL, DQ): one block per 512-bit VPMADDWD, 8 blocks of
 * term52 per 512-bit step with native 64-bit shifts, compares and min/max ---- */

static inline AVX512 __m512i mac_512(const int8_t *w, const int16_t *x)
{
    __m512i w16 = _mm512_cvtepi8_epi16(_mm256_loadu_si256((const __m256i *) w));
    return _mm512_madd_epi16(w16, _mm512_loadu_si512((const void *) x));
}

static inline AVX512 __m256i fold512(__m512i a)
{
    return _mm256_add_epi32(_mm512_castsi512_si256(a), _mm512_extracti64x4_epi64(a, 1));
}

/* the 8 block sums of 8 blocks */
static inline AVX512 __m256i hsum8_512(const __m512i p[8])
{
    /* interleave and add: each step halves the partial sums per vector */
    __m512i a =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[0], p[1]), _mm512_unpackhi_epi32(p[0], p[1]));
    __m512i b =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[2], p[3]), _mm512_unpackhi_epi32(p[2], p[3]));
    __m512i c =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[4], p[5]), _mm512_unpackhi_epi32(p[4], p[5]));
    __m512i d =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[6], p[7]), _mm512_unpackhi_epi32(p[6], p[7]));
    __m512i ab = _mm512_add_epi32(_mm512_unpacklo_epi64(a, b), _mm512_unpackhi_epi64(a, b));
    __m512i cd = _mm512_add_epi32(_mm512_unpacklo_epi64(c, d), _mm512_unpackhi_epi64(c, d));
    /* each 128-bit chunk of ab holds partial sums of 0 1 2 3, of cd 4 5 6 7 */
    __m512i x =
        _mm512_add_epi32(_mm512_shuffle_i32x4(ab, cd, 0x88), _mm512_shuffle_i32x4(ab, cd, 0xDD));
    /* x chunks: 0123 0123 4567 4567 */
    __m512i y =
        _mm512_add_epi32(_mm512_shuffle_i32x4(x, x, 0x08), _mm512_shuffle_i32x4(x, x, 0x0D));
    return _mm512_castsi512_si256(y); /* 0 1 2 3 4 5 6 7 */
}

static inline AVX512 __m512i mul64_512(__m256i s, __m256i mul)
{
    return _mm512_mul_epi32(_mm512_cvtepi32_epi64(s), _mm512_cvtepi32_epi64(mul));
}

static inline AVX512 __m512i term52v_512(__m512i v, __m256i k)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lim = _mm512_set1_epi64(ZT_KERN_LIM52);
    const __m512i nlim = _mm512_set1_epi64(-ZT_KERN_LIM52);
    __m512i kk = _mm512_cvtepi32_epi64(k);
    /* k <= 0: arithmetic shift right by min(-k, 63) is floor(v / 2^-k) */
    __m512i n = _mm512_min_epi64(_mm512_sub_epi64(zero, kk), _mm512_set1_epi64(63));
    __m512i right = _mm512_srav_epi64(v, n);
    __mmask8 sat =
        _mm512_cmpgt_epu64_mask(_mm512_abs_epi64(v), _mm512_srlv_epi64(lim, kk)); /* k > 0 only */
    __mmask8 neg = _mm512_cmplt_epi64_mask(v, zero);
    __m512i left = _mm512_sllv_epi64(v, kk);
    left = _mm512_mask_mov_epi64(left, sat, _mm512_mask_mov_epi64(lim, neg, nlim));
    __m512i r = _mm512_mask_mov_epi64(right, _mm512_cmpgt_epi64_mask(kk, zero), left);
    return _mm512_max_epi64(_mm512_min_epi64(r, lim), nlim);
}

static inline AVX512 __m512i term52_512(__m256i s, __m256i mul, __m256i k)
{
    return term52v_512(mul64_512(s, mul), k);
}

static AVX512 zt_fx dot_raw_512(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                uint32_t nb)
{
    __m512i acc = _mm512_setzero_si512();
    uint32_t b = 0;
    for (; b + 8 <= nb; b += 8, row += 8 * 34) {
        const int16_t *x = a + b * 32;
        for (uint32_t i = 0; i < 8 * 34; i += 64)
            _mm_prefetch((const char *) row + PF_AHEAD + i, _MM_HINT_T0);
        __m512i p[8];
        for (uint32_t i = 0; i < 8; i++)
            p[i] = mac_512((const int8_t *) row + 34 * i + 2, x + 32 * i);
        __m128i m0, k0, m1, k1;
        f16x4(row, ash + b, &m0, &k0);
        f16x4(row + 4 * 34, ash + b + 4, &m1, &k1);
        acc = _mm512_add_epi64(
            acc, term52_512(hsum8_512(p), _mm256_set_m128i(m1, m0), _mm256_set_m128i(k1, k0)));
    }
    int64_t t = _mm512_reduce_add_epi64(acc);
    for (; b < nb; b++, row += 34) {
        int32_t s = hsum1_avx2(mac_avx2((const int8_t *) row + 2, a + b * 32)), mm, e;
        zt_kern_f16(ld16(row), &mm, &e);
        t += zt_term52((int64_t) s * mm, e + 16 + ash[b]);
    }
    return zt_kern_round(t);
}

static AVX512 zt_fx dot_q8_512(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    __m512i acc = _mm512_setzero_si512();
    uint32_t b = 0;
    for (; b + 8 <= nb; b += 8) {
        const int16_t *x = a + b * 32;
        const zt_q8_t *v = w + b;
        for (uint32_t i = 0; i < 8 * sizeof(zt_q8_t); i += 64)
            _mm_prefetch((const char *) v + PF_AHEAD + i, _MM_HINT_T0);
        __m512i p[8];
        for (uint32_t i = 0; i < 8; i++) p[i] = mac_512(v[i].q, x + 32 * i);
        __m256i sc = _mm256_setr_epi32(v[0].scale, v[1].scale, v[2].scale, v[3].scale, v[4].scale,
                                       v[5].scale, v[6].scale, v[7].scale);
        __m256i sh = _mm256_setr_epi32(v[0].shift, v[1].shift, v[2].shift, v[3].shift, v[4].shift,
                                       v[5].shift, v[6].shift, v[7].shift);
        __m256i k = _mm256_sub_epi32(
            _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *) (ash + b))), sh);
        acc = _mm512_add_epi64(acc, term52_512(hsum8_512(p), sc, k));
    }
    int64_t t = _mm512_reduce_add_epi64(acc);
    for (; b < nb; b++) {
        int32_t s = hsum1_avx2(mac_avx2(w[b].q, a + b * 32));
        t += zt_term52((int64_t) s * w[b].scale, (int32_t) ash[b] - (int32_t) w[b].shift);
    }
    return zt_kern_round(t);
}

/* ---- K-quant rows in place (zt_kern.h zt_dot_q4k_c / zt_dot_q6k_c) ---- */

/* One Q4_K block's scale terms for its 8 sub-blocks: sc * d.mm with
 * k = d.e + 16 + ash, and mn * dmin.mm with dmin.e + 16 + ash. */
static inline AVX2 void q4k_params_avx2(const uint8_t *blk, const uint8_t *ash, __m256i *ms,
                                        __m256i *ks, __m256i *mt, __m256i *kt)
{
    int32_t dm, de, mm, me;
    zt_kern_f16(ld16(blk), &dm, &de);
    zt_kern_f16(ld16(blk + 2), &mm, &me);
    /* zt_kern_q4k_scales on lanes: v = d|dmin, u0, u1, u2 (the 12 scale bytes) */
    __m128i v = _mm_loadu_si128((const __m128i *) blk);
    __m128i A = _mm_shuffle_epi32(v, 0xED); /* u0 u2 u1 u2 */
    __m128i B = _mm_shuffle_epi32(v, 0xA5); /* u0 u0 u1 u1 */
    __m128i even = _mm_and_si128(A, _mm_set1_epi32(0x3f3f3f3f));
    __m128i odd = _mm_or_si128(
        _mm_and_si128(_mm_srlv_epi32(A, _mm_setr_epi32(0, 0, 0, 4)), _mm_set1_epi32(0x0f0f0f0f)),
        _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(B, 6), _mm_set1_epi32(0x03030303)), 4));
    __m128i sm = _mm_blend_epi32(even, odd, 0xA); /* sc[0..7] mn[0..7] */
    __m256i sh = _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *) ash));
    *ms = _mm256_mullo_epi32(_mm256_cvtepu8_epi32(sm), _mm256_set1_epi32(dm));
    *mt = _mm256_mullo_epi32(_mm256_cvtepu8_epi32(_mm_srli_si128(sm, 8)), _mm256_set1_epi32(mm));
    *ks = _mm256_add_epi32(sh, _mm256_set1_epi32(de + 16));
    *kt = _mm256_add_epi32(sh, _mm256_set1_epi32(me + 16));
}

/* Q6_K: per sub-block j, the two scales times d.mm and k = d.e + 16 + ash. */
static inline void q6k_params(const uint8_t *blk, const uint8_t *ash, int32_t m0[8], int32_t m1[8],
                              int32_t ks[8])
{
    int32_t dm, de;
    zt_kern_f16(ld16(blk + 208), &dm, &de);
    const int8_t *sc = (const int8_t *) (blk + 192);
    for (uint32_t j = 0; j < 8; j++) {
        m0[j] = sc[2 * j] * dm;
        m1[j] = sc[2 * j + 1] * dm;
        ks[j] = de + 16 + ash[j];
    }
}

/* the 4 x 32 signed 6-bit weights of Q6_K half h (sub-blocks 4h..4h+3), as
 * int8. 16-bit shifts may carry bits across bytes; the masks drop them. */
static inline AVX2 void q6k_bytes_avx2(const uint8_t *blk, uint32_t h, __m256i q[4])
{
    __m256i l0 = _mm256_loadu_si256((const __m256i *) (blk + h * 64));
    __m256i l1 = _mm256_loadu_si256((const __m256i *) (blk + h * 64 + 32));
    __m256i hb = _mm256_loadu_si256((const __m256i *) (blk + 128 + h * 32));
    const __m256i m4 = _mm256_set1_epi8(0x0F), m30 = _mm256_set1_epi8(0x30),
                  o = _mm256_set1_epi8(32);
    q[0] =
        _mm256_or_si256(_mm256_and_si256(l0, m4), _mm256_and_si256(_mm256_slli_epi16(hb, 4), m30));
    q[1] =
        _mm256_or_si256(_mm256_and_si256(l1, m4), _mm256_and_si256(_mm256_slli_epi16(hb, 2), m30));
    q[2] =
        _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(l0, 4), m4), _mm256_and_si256(hb, m30));
    q[3] = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(l1, 4), m4),
                           _mm256_and_si256(_mm256_srli_epi16(hb, 2), m30));
    for (uint32_t i = 0; i < 4; i++) q[i] = _mm256_sub_epi8(q[i], o);
}

static AVX2 zt_fx dot_q4k_avx2(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                               const int32_t *asum, uint32_t nb)
{
    const __m256i m4 = _mm256_set1_epi16(0x0F);
    __m256i acc = _mm256_setzero_si256();
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q4K_BYTES) {
        _mm_prefetch((const char *) row + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 128, _MM_HINT_T0);
        const int16_t *x = a + k * 256;
        __m256i ps[8];
        for (uint32_t p = 0; p < 4; p++) {
            const uint8_t *qp = row + 16 + 32 * p;
            __m256i w0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *) qp));
            __m256i w1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *) (qp + 16)));
            const int16_t *xp = x + 64 * p;
            /* low nibbles: sub-block 2p; high nibbles: 2p + 1 */
            ps[2 * p] = _mm256_add_epi32(
                _mm256_madd_epi16(_mm256_and_si256(w0, m4),
                                  _mm256_loadu_si256((const __m256i *) xp)),
                _mm256_madd_epi16(_mm256_and_si256(w1, m4),
                                  _mm256_loadu_si256((const __m256i *) (xp + 16))));
            ps[2 * p + 1] = _mm256_add_epi32(
                _mm256_madd_epi16(_mm256_srli_epi16(w0, 4),
                                  _mm256_loadu_si256((const __m256i *) (xp + 32))),
                _mm256_madd_epi16(_mm256_srli_epi16(w1, 4),
                                  _mm256_loadu_si256((const __m256i *) (xp + 48))));
        }
        __m256i ms, ks, mt, kt;
        q4k_params_avx2(row, ash + k * 8, &ms, &ks, &mt, &kt);
        __m256i t = _mm256_loadu_si256((const __m256i *) (asum + k * 8));
        __m128i s0 = hsum4_avx2(ps[0], ps[1], ps[2], ps[3]);
        __m128i s1 = hsum4_avx2(ps[4], ps[5], ps[6], ps[7]);
        acc = _mm256_add_epi64(
            acc, term52_avx2(s0, _mm256_castsi256_si128(ms), _mm256_castsi256_si128(ks)));
        acc = _mm256_add_epi64(
            acc, term52_avx2(s1, _mm256_extracti128_si256(ms, 1), _mm256_extracti128_si256(ks, 1)));
        acc =
            _mm256_sub_epi64(acc, term52_avx2(_mm256_castsi256_si128(t), _mm256_castsi256_si128(mt),
                                              _mm256_castsi256_si128(kt)));
        acc = _mm256_sub_epi64(acc, term52_avx2(_mm256_extracti128_si256(t, 1),
                                                _mm256_extracti128_si256(mt, 1),
                                                _mm256_extracti128_si256(kt, 1)));
    }
    return zt_kern_round(hsum64_avx2(acc));
}

static AVX2 zt_fx dot_q6k_avx2(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                               const int32_t *asum, uint32_t nb)
{
    (void) asum;
    __m256i acc = _mm256_setzero_si256();
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q6K_BYTES) {
        _mm_prefetch((const char *) row + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 128, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 192, _MM_HINT_T0);
        const int16_t *x = a + k * 256;
        __m256i p0[8], p1[8];
        __m256i qb[8];
        q6k_bytes_avx2(row, 0, qb);
        q6k_bytes_avx2(row, 1, qb + 4);
        for (uint32_t j = 0; j < 8; j++) {
            __m256i q = qb[j];
            __m256i w0 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(q));
            __m256i w1 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(q, 1));
            p0[j] = _mm256_madd_epi16(w0, _mm256_loadu_si256((const __m256i *) (x + 32 * j)));
            p1[j] = _mm256_madd_epi16(w1, _mm256_loadu_si256((const __m256i *) (x + 32 * j + 16)));
        }
        int32_t m0[8], m1[8], ks[8];
        q6k_params(row, ash + k * 8, m0, m1, ks);
        for (uint32_t h = 0; h < 2; h++) {
            __m128i s0 = hsum4_avx2(p0[4 * h], p0[4 * h + 1], p0[4 * h + 2], p0[4 * h + 3]);
            __m128i s1 = hsum4_avx2(p1[4 * h], p1[4 * h + 1], p1[4 * h + 2], p1[4 * h + 3]);
            __m256i v =
                _mm256_add_epi64(mul64_avx2(s0, _mm_loadu_si128((const __m128i *) (m0 + 4 * h))),
                                 mul64_avx2(s1, _mm_loadu_si128((const __m128i *) (m1 + 4 * h))));
            acc = _mm256_add_epi64(
                acc, term52v_avx2(v, _mm_loadu_si128((const __m128i *) (ks + 4 * h))));
        }
    }
    return zt_kern_round(hsum64_avx2(acc));
}

static AVX512 zt_fx dot_q4k_512(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                const int32_t *asum, uint32_t nb)
{
    const __m512i m4 = _mm512_set1_epi16(0x0F);
    __m512i acc = _mm512_setzero_si512();
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q4K_BYTES) {
        _mm_prefetch((const char *) row + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 128, _MM_HINT_T0);
        const int16_t *x = a + k * 256;
        __m512i ps[8];
        for (uint32_t p = 0; p < 4; p++) {
            __m512i w =
                _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *) (row + 16 + 32 * p)));
            ps[2 * p] = _mm512_madd_epi16(_mm512_and_si512(w, m4),
                                          _mm512_loadu_si512((const void *) (x + 64 * p)));
            ps[2 * p + 1] = _mm512_madd_epi16(_mm512_srli_epi16(w, 4),
                                              _mm512_loadu_si512((const void *) (x + 64 * p + 32)));
        }
        __m256i ms, ks, mt, kt;
        q4k_params_avx2(row, ash + k * 8, &ms, &ks, &mt, &kt);
        __m256i t = _mm256_loadu_si256((const __m256i *) (asum + k * 8));
        acc = _mm512_add_epi64(acc, term52_512(hsum8_512(ps), ms, ks));
        acc = _mm512_sub_epi64(acc, term52_512(t, mt, kt));
    }
    return zt_kern_round(_mm512_reduce_add_epi64(acc));
}

/* as hsum8_512, stopping one step early: the sums of lanes 0..7 of each
 * vector, then of lanes 8..15 */
static inline AVX512 __m512i hsum8x2_512(const __m512i p[8])
{
    __m512i a =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[0], p[1]), _mm512_unpackhi_epi32(p[0], p[1]));
    __m512i b =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[2], p[3]), _mm512_unpackhi_epi32(p[2], p[3]));
    __m512i c =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[4], p[5]), _mm512_unpackhi_epi32(p[4], p[5]));
    __m512i d =
        _mm512_add_epi32(_mm512_unpacklo_epi32(p[6], p[7]), _mm512_unpackhi_epi32(p[6], p[7]));
    __m512i ab = _mm512_add_epi32(_mm512_unpacklo_epi64(a, b), _mm512_unpackhi_epi64(a, b));
    __m512i cd = _mm512_add_epi32(_mm512_unpacklo_epi64(c, d), _mm512_unpackhi_epi64(c, d));
    __m512i x =
        _mm512_add_epi32(_mm512_shuffle_i32x4(ab, cd, 0x88), _mm512_shuffle_i32x4(ab, cd, 0xDD));
    return _mm512_shuffle_i32x4(x, x, 0xD8); /* lo 0..7 | hi 0..7 */
}

static AVX512 zt_fx dot_q6k_512(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                                const int32_t *asum, uint32_t nb)
{
    (void) asum;
    const __m512i m4 = _mm512_set1_epi16(0x0F), m30 = _mm512_set1_epi16(0x30);
    const __m512i o = _mm512_set1_epi16(32);
    const __m512i eo = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    __m512i acc = _mm512_setzero_si512();
    for (uint32_t k = 0; k < nb / 8; k++, row += ZT_Q6K_BYTES) {
        _mm_prefetch((const char *) row + PF_AHEAD, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 64, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 128, _MM_HINT_T0);
        _mm_prefetch((const char *) row + PF_AHEAD + 192, _MM_HINT_T0);
        const int16_t *x = a + k * 256;
        __m512i p[8];
        for (uint32_t h = 0; h < 2; h++) {
            __m512i l0 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *) (row + h * 64)));
            __m512i l1 =
                _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *) (row + h * 64 + 32)));
            __m512i hb =
                _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *) (row + 128 + h * 32)));
            __m512i q[4] = {_mm512_or_si512(_mm512_and_si512(l0, m4),
                                            _mm512_and_si512(_mm512_slli_epi16(hb, 4), m30)),
                            _mm512_or_si512(_mm512_and_si512(l1, m4),
                                            _mm512_and_si512(_mm512_slli_epi16(hb, 2), m30)),
                            _mm512_or_si512(_mm512_srli_epi16(l0, 4), _mm512_and_si512(hb, m30)),
                            _mm512_or_si512(_mm512_srli_epi16(l1, 4),
                                            _mm512_and_si512(_mm512_srli_epi16(hb, 2), m30))};
            for (uint32_t u = 0; u < 4; u++)
                p[4 * h + u] =
                    _mm512_madd_epi16(_mm512_sub_epi16(q[u], o),
                                      _mm512_loadu_si512((const void *) (x + 32 * (4 * h + u))));
        }
        int32_t dm, de;
        zt_kern_f16(ld16(row + 208), &dm, &de);
        __m512i sc = _mm512_cvtepi8_epi32(_mm_loadu_si128((const __m128i *) (row + 192)));
        sc = _mm512_permutexvar_epi32(eo, _mm512_mullo_epi32(sc, _mm512_set1_epi32(dm)));
        __m512i hs = hsum8x2_512(p); /* weights 0..15 of each sub-block, then 16..31 */
        __m512i v = _mm512_add_epi64(
            mul64_512(_mm512_castsi512_si256(hs), _mm512_castsi512_si256(sc)),
            mul64_512(_mm512_extracti64x4_epi64(hs, 1), _mm512_extracti64x4_epi64(sc, 1)));
        __m256i ks =
            _mm256_add_epi32(_mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *) (ash + k * 8))),
                             _mm256_set1_epi32(de + 16));
        acc = _mm512_add_epi64(acc, term52v_512(v, ks));
    }
    return zt_kern_round(_mm512_reduce_add_epi64(acc));
}

static const zt_kern_t K_AVX2 = {"avx2", dot_raw_avx2, dot_q8_avx2, 0,
                                 0,      dot_q4k_avx2, dot_q6k_avx2};
static const zt_kern_t K_AVX512 = {"avx512", dot_raw_512, dot_q8_512, 0,
                                   0,        dot_q4k_512, dot_q6k_512};

const zt_kern_t *zt_simd_x86_avx2(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") ? &K_AVX2 : 0;
}

const zt_kern_t *zt_simd_x86_avx512(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("avx512f") &&
                   __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl") &&
                   __builtin_cpu_supports("avx512dq")
               ? &K_AVX512
               : 0;
}

#else

const zt_kern_t *zt_simd_x86_avx2(void)
{
    return 0;
}

const zt_kern_t *zt_simd_x86_avx512(void)
{
    return 0;
}

#endif
