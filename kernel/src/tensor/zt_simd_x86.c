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
#include "zt_gguf.h"

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#    include <immintrin.h>

#    define AVX2   __attribute__((target("avx2")))
#    define AVX512 __attribute__((target("avx2,avx512f,avx512bw,avx512vl,avx512dq")))
#    define AVX512VNNI __attribute__((target("avx2,avx512f,avx512bw,avx512vl,avx512dq,avx512vnni")))

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
/* zt_term52(v[i], kk[i]) for 4 lanes, kk as 64-bit lanes */
static inline AVX2 __m256i term52q_avx2(__m256i v, __m256i kk)
{
    const __m256i zero = _mm256_setzero_si256();
    const __m256i lim = _mm256_set1_epi64x(ZT_KERN_LIM52);
    const __m256i nlim = _mm256_set1_epi64x(-ZT_KERN_LIM52);
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

static inline AVX2 __m256i term52v_avx2(__m256i v, __m128i k)
{
    return term52q_avx2(v, _mm256_cvtepi32_epi64(k));
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

/* zt_term52(v[i], kk[i]) for 8 lanes, kk as 64-bit lanes */
static inline AVX512 __m512i term52q_512(__m512i v, __m512i kk)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lim = _mm512_set1_epi64(ZT_KERN_LIM52);
    const __m512i nlim = _mm512_set1_epi64(-ZT_KERN_LIM52);
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

static inline AVX512 __m512i term52v_512(__m512i v, __m256i k)
{
    return term52q_512(v, _mm512_cvtepi32_epi64(k));
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

/* ---- batched rows (zt_kern.h zt_mmb_t): a lane per token ----
 * For each weight block the row's weights are unpacked once to int16 (w), a
 * weight pair is broadcast to every lane, and VPMADDWD against the token-major
 * activations gives 16 tokens' exact block sums with no horizontal adds. The
 * scale products are exact in 64-bit lanes as above. term52 is the general
 * form unless the (group, block)'s largest exponent proves it cannot saturate
 * (zt_kern.h ZT_MMB_SAFE_*): then it is (v << (ash + max(e, 0))) >> max(-e, 0),
 * an exact left shift and one floor shift, which is the same value. Groups of
 * tokens are taken MMB_GC at a time so the accumulators stay small. */
#    define MMB_GC 8u

static inline int32_t ld32(const int16_t *p)
{
    int32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

static inline uint32_t mmb_groups(const zt_mmb_t *j)
{
    return (j->B + ZT_MMB_G - 1) / ZT_MMB_G;
}

/* the rounded results of a chunk of groups for row r */
static inline void mmb_store(const zt_mmb_t *j, uint32_t r, uint32_t g0, uint32_t gn,
                             const int64_t (*acc)[ZT_MMB_G])
{
    for (uint32_t gi = 0; gi < gn; gi++)
        for (uint32_t t = 0; t < ZT_MMB_G; t++) {
            uint32_t tok = (g0 + gi) * ZT_MMB_G + t;
            if (tok < j->B)
                j->y[(uint64_t) tok * j->ystride + r] = zt_kern_round(acc[gi][ZT_MMB_SLOT(t)]);
        }
}

/* AVX-512: 16 tokens per vector. A tile is TR weight rows by two groups
 * (32 tokens): each activation vector loaded serves TR rows, each broadcast
 * weight pair two groups. The forward pass feeds n_batch tokens at a time, so
 * the activations of one batch stay in L2 while the rows stream past. The
 * tile is compiled twice from zt_simd_x86_mmb.h: with VPDPWSSD (AVX512-VNNI,
 * the same exact sums with the add fused) and with VPMADDWD + VPADDD. */
#    define TR 4u

/* Per weight type: unpack 256 weights of row i into w + 256 i and the
 * per-sub-block terms. Q4_K and Q8_0 have one multiplier per sub-block (m0),
 * Q6_K two (m0 for pairs 0..7, m1 for 8..15); Q4_K adds a min term (mt, em);
 * Q8_0 has an exponent per sub-block (ek). */
typedef struct {
    int64_t m0[8], m1[8], mt[8];
    int32_t ek[8], e, em;
} tparam_t;

static inline AVX512 void unpack_q4k_512(const uint8_t *blk, int16_t *w, tparam_t *t)
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
    const __m512i m4 = _mm512_set1_epi16(0x0F);
    for (uint32_t p = 0; p < 4; p++) {
        __m512i q = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *) (blk + 16 + 32 * p)));
        _mm512_store_si512((void *) (w + 64 * p), _mm512_and_si512(q, m4));
        _mm512_store_si512((void *) (w + 64 * p + 32), _mm512_srli_epi16(q, 4));
    }
}

static inline AVX512 void unpack_q6k_512(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    int32_t dm;
    zt_kern_f16(ld16(blk + 208), &dm, &t->e);
    t->e += 16;
    const int8_t *sc = (const int8_t *) (blk + 192);
    for (uint32_t i = 0; i < 8; i++) {
        t->m0[i] = sc[2 * i] * dm;
        t->m1[i] = sc[2 * i + 1] * dm;
    }
    __m256i qb[8];
    q6k_bytes_avx2(blk, 0, qb);
    q6k_bytes_avx2(blk, 1, qb + 4);
    for (uint32_t i = 0; i < 8; i++)
        _mm512_store_si512((void *) (w + 32 * i), _mm512_cvtepi8_epi16(qb[i]));
}

/* 8 Q8_0 blocks (8 * 34 bytes), each with its own f16 scale */
static inline AVX512 void unpack_q8_0_512(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    for (uint32_t i = 0; i < 8; i++, blk += 34) {
        int32_t mm;
        zt_kern_f16(ld16(blk), &mm, &t->ek[i]);
        t->m0[i] = mm;
        t->ek[i] += 16;
        _mm512_store_si512((void *) (w + 32 * i),
                           _mm512_cvtepi8_epi16(_mm256_loadu_si256((const __m256i *) (blk + 2))));
    }
}

/* term52(v, e + ash) for 8 slots; fast as in the comment above */
static inline AVX512 __m512i shift_512(__m512i v, __m512i ash, int32_t e, int fast)
{
    if (fast) {
        if (e > 0) return _mm512_sllv_epi64(v, _mm512_add_epi64(ash, _mm512_set1_epi64(e)));
        v = _mm512_sllv_epi64(v, ash);
        return e < 0 ? _mm512_srav_epi64(v, _mm512_set1_epi64(-e)) : v;
    }
    return term52q_512(v, _mm512_add_epi64(ash, _mm512_set1_epi64(e)));
}

/* term52(v, e + ash) of a group's 16 slots (ve: even, vo: odd) for weight
 * exponent e and the group's block exponents (h0, h1; hx = their largest):
 * one arithmetic right shift by -e - ash when e + ash <= 0 for every token
 * (the usual case), else shift_512 */
static inline AVX512 void term16_512(__m512i *ve, __m512i *vo, __m512i h0, __m512i h1, int32_t e,
                                     int32_t hx, int32_t safe)
{
    if (e <= 0 && hx <= -e) {
        __m512i R = _mm512_set1_epi64(-(int64_t) e);
        *ve = _mm512_srav_epi64(*ve, _mm512_sub_epi64(R, h0));
        *vo = _mm512_srav_epi64(*vo, _mm512_sub_epi64(R, h1));
    } else {
        int fast = hx + e <= safe;
        *ve = shift_512(*ve, h0, e, fast);
        *vo = shift_512(*vo, h1, e, fast);
    }
}

#    define MMB_NAME mmb_tile_512
#    define MMB_TGT  AVX512
#    define MMB_VNNI 0
#    include "zt_simd_x86_mmb.h"
#    define MMB_NAME mmb_tile_vnni
#    define MMB_TGT  AVX512VNNI
#    define MMB_VNNI 1
#    include "zt_simd_x86_mmb.h"

/* AVX2: 8 tokens per vector, a group of 16 in two halves. A tile is TR rows
 * by one group (TR x 2 accumulators of 8 lanes), with the same unpacking, slot
 * order and term52 shortcuts as the AVX-512 tile; AVX2 has no 64-bit
 * arithmetic shift, so a floor shift is a logical shift of v ^ sign. */

static inline AVX2 __m256i srav64_avx2(__m256i v, __m256i n)
{
    __m256i sign = _mm256_cmpgt_epi64(_mm256_setzero_si256(), v);
    return _mm256_xor_si256(_mm256_srlv_epi64(_mm256_xor_si256(v, sign), n), sign);
}

/* term52(v, e + ash) for 4 slots, as term16_512 */
static inline AVX2 __m256i term4_avx2(__m256i v, __m256i h, int32_t e, int32_t hx, int32_t safe)
{
    if (e <= 0 && hx <= -e) return srav64_avx2(v, _mm256_sub_epi64(_mm256_set1_epi64x(-e), h));
    if (hx + e <= safe) {
        v = _mm256_sllv_epi64(v, _mm256_add_epi64(h, _mm256_set1_epi64x(e > 0 ? e : 0)));
        return e < 0 ? srav64_avx2(v, _mm256_set1_epi64x(-e)) : v;
    }
    return term52q_avx2(v, _mm256_add_epi64(h, _mm256_set1_epi64x(e)));
}

static inline AVX2 void unpack_q4k_avx2(const uint8_t *blk, int16_t *w, tparam_t *t)
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
    const __m256i m4 = _mm256_set1_epi16(0x0F);
    for (uint32_t p = 0; p < 8; p++) {
        __m256i q = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *) (blk + 16 + 16 * p)));
        /* bytes 32(p/2) + 16(p%2) ..: low nibbles to sub-block 2(p/2), high to 2(p/2) + 1 */
        int16_t *o = w + 64 * (p / 2) + 16 * (p % 2);
        _mm256_store_si256((__m256i *) o, _mm256_and_si256(q, m4));
        _mm256_store_si256((__m256i *) (o + 32), _mm256_srli_epi16(q, 4));
    }
}

static inline AVX2 void unpack_q6k_avx2(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    int32_t dm;
    zt_kern_f16(ld16(blk + 208), &dm, &t->e);
    t->e += 16;
    const int8_t *sc = (const int8_t *) (blk + 192);
    for (uint32_t i = 0; i < 8; i++) {
        t->m0[i] = sc[2 * i] * dm;
        t->m1[i] = sc[2 * i + 1] * dm;
    }
    __m256i qb[8];
    q6k_bytes_avx2(blk, 0, qb);
    q6k_bytes_avx2(blk, 1, qb + 4);
    for (uint32_t i = 0; i < 8; i++) {
        _mm256_store_si256((__m256i *) (w + 32 * i),
                           _mm256_cvtepi8_epi16(_mm256_castsi256_si128(qb[i])));
        _mm256_store_si256((__m256i *) (w + 32 * i + 16),
                           _mm256_cvtepi8_epi16(_mm256_extracti128_si256(qb[i], 1)));
    }
}

static inline AVX2 void unpack_q8_0_avx2(const uint8_t *blk, int16_t *w, tparam_t *t)
{
    for (uint32_t i = 0; i < 8; i++, blk += 34) {
        int32_t mm;
        zt_kern_f16(ld16(blk), &mm, &t->ek[i]);
        t->m0[i] = mm;
        t->ek[i] += 16;
        _mm256_store_si256((__m256i *) (w + 32 * i),
                           _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i *) (blk + 2))));
        _mm256_store_si256((__m256i *) (w + 32 * i + 16),
                           _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i *) (blk + 18))));
    }
}

/* d[2i + hh] = row i's pairs [p0, p1) against half hh (8 tokens) of the group */
#    define DOT_AVX2(d, p0, p1)                                                                    \
        do {                                                                                       \
            __m256i d0 = _mm256_setzero_si256(), d1 = d0, d2 = d0, d3 = d0, d4 = d0, d5 = d0,      \
                    d6 = d0, d7 = d0;                                                              \
            _Pragma("GCC unroll 16") for (uint32_t p = (p0); p < (p1); p++)                        \
            {                                                                                      \
                __m256i x0 = _mm256_load_si256((const __m256i *) (xa + 32 * p));                   \
                __m256i x1 = _mm256_load_si256((const __m256i *) (xa + 32 * p + 16));              \
                __m256i w0 = _mm256_set1_epi32(ld32(ws + 2 * p));                                  \
                __m256i w1 = _mm256_set1_epi32(ld32(ws + 256 + 2 * p));                            \
                d0 = _mm256_add_epi32(d0, _mm256_madd_epi16(w0, x0));                              \
                d1 = _mm256_add_epi32(d1, _mm256_madd_epi16(w0, x1));                              \
                d2 = _mm256_add_epi32(d2, _mm256_madd_epi16(w1, x0));                              \
                d3 = _mm256_add_epi32(d3, _mm256_madd_epi16(w1, x1));                              \
                w0 = _mm256_set1_epi32(ld32(ws + 512 + 2 * p));                                    \
                w1 = _mm256_set1_epi32(ld32(ws + 768 + 2 * p));                                    \
                d4 = _mm256_add_epi32(d4, _mm256_madd_epi16(w0, x0));                              \
                d5 = _mm256_add_epi32(d5, _mm256_madd_epi16(w0, x1));                              \
                d6 = _mm256_add_epi32(d6, _mm256_madd_epi16(w1, x0));                              \
                d7 = _mm256_add_epi32(d7, _mm256_madd_epi16(w1, x1));                              \
            }                                                                                      \
            d[0] = d0, d[1] = d1, d[2] = d2, d[3] = d3, d[4] = d4, d[5] = d5, d[6] = d6,           \
            d[7] = d7;                                                                             \
        } while (0)

static inline __attribute__((always_inline)) AVX2 void mmb_body_avx2(const zt_mmb_t *j, uint32_t r0,
                                                                     uint32_t r1, const uint32_t qt)
{
    uint32_t nb = j->cols / 32, nk = nb / 8, ng = mmb_groups(j);
    uint64_t kbytes = qt == ZT_GGML_Q4_K   ? ZT_Q4K_BYTES
                      : qt == ZT_GGML_Q6_K ? ZT_Q6K_BYTES
                                           : 8 * 34;
    int32_t safe = qt == ZT_GGML_Q4_K   ? ZT_MMB_SAFE_Q4K
                   : qt == ZT_GGML_Q6_K ? ZT_MMB_SAFE_Q6K
                                        : ZT_MMB_SAFE_Q8;
    int16_t w[TR * 256] __attribute__((aligned(64)));
    int64_t acc[TR][MMB_GC][ZT_MMB_G] __attribute__((aligned(64)));
    tparam_t tp[TR];
    for (uint32_t r = r0; r < r1; r += TR) {
        uint32_t nr = r1 - r < TR ? r1 - r : TR;
        if (nr < TR) __builtin_memset(w, 0, sizeof w);
        for (uint32_t g0 = 0; g0 < ng; g0 += MMB_GC) {
            uint32_t gn = ng - g0 < MMB_GC ? ng - g0 : MMB_GC;
            __builtin_memset(acc, 0, sizeof acc);
            for (uint32_t k = 0; k < nk; k++) {
                for (uint32_t i = 0; i < nr; i++) {
                    const uint8_t *blk = j->raw + ((uint64_t) (r + i) * nk + k) * kbytes;
                    if (qt == ZT_GGML_Q4_K)
                        unpack_q4k_avx2(blk, w + 256 * i, &tp[i]);
                    else if (qt == ZT_GGML_Q6_K)
                        unpack_q6k_avx2(blk, w + 256 * i, &tp[i]);
                    else
                        unpack_q8_0_avx2(blk, w + 256 * i, &tp[i]);
                }
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t sb = 0; sb < 8; sb++) {
                        uint64_t gb = (uint64_t) (g0 + gi) * nb + k * 8 + sb;
                        const int16_t *xa = j->aT + gb * 32 * ZT_MMB_G;
                        const int16_t *ws = w + 32 * sb;
                        __m256i s[2 * TR], s2[2 * TR];
                        if (qt == ZT_GGML_Q6_K) {
                            DOT_AVX2(s, 0, 8);
                            DOT_AVX2(s2, 8, 16);
                        } else {
                            DOT_AVX2(s, 0, 16);
                        }
                        const int64_t *hp = j->ashT + gb * ZT_MMB_G;
                        const int64_t *up = j->asumT + gb * ZT_MMB_G;
                        int32_t hx = j->ashx[gb];
                        for (uint32_t i = 0; i < nr; i++) {
                            const tparam_t *t = &tp[i];
                            int64_t *a = acc[i][gi];
                            for (uint32_t hh = 0; hh < 2; hh++) {
                                /* even slots 4hh.., odd slots 8 + 4hh.. */
                                __m256i he = _mm256_load_si256((const __m256i *) (hp + 4 * hh));
                                __m256i ho = _mm256_load_si256((const __m256i *) (hp + 8 + 4 * hh));
                                __m256i sv = s[2 * i + hh], so = _mm256_srli_epi64(sv, 32);
                                __m256i M0 = _mm256_set1_epi64x(t->m0[sb]);
                                __m256i ve = _mm256_mul_epi32(sv, M0),
                                        vo = _mm256_mul_epi32(so, M0);
                                if (qt == ZT_GGML_Q6_K) {
                                    __m256i M1 = _mm256_set1_epi64x(t->m1[sb]);
                                    __m256i s2v = s2[2 * i + hh];
                                    ve = _mm256_add_epi64(ve, _mm256_mul_epi32(s2v, M1));
                                    vo = _mm256_add_epi64(
                                        vo, _mm256_mul_epi32(_mm256_srli_epi64(s2v, 32), M1));
                                    ve = term4_avx2(ve, he, t->e, hx, safe);
                                    vo = term4_avx2(vo, ho, t->e, hx, safe);
                                } else if (qt == ZT_GGML_Q4_K) {
                                    __m256i MT = _mm256_set1_epi64x(t->mt[sb]);
                                    __m256i ue = _mm256_mul_epi32(
                                        _mm256_load_si256((const __m256i *) (up + 4 * hh)), MT);
                                    __m256i uo = _mm256_mul_epi32(
                                        _mm256_load_si256((const __m256i *) (up + 8 + 4 * hh)), MT);
                                    ve = _mm256_sub_epi64(term4_avx2(ve, he, t->e, hx, safe),
                                                          term4_avx2(ue, he, t->em, hx, safe));
                                    vo = _mm256_sub_epi64(term4_avx2(vo, ho, t->e, hx, safe),
                                                          term4_avx2(uo, ho, t->em, hx, safe));
                                } else {
                                    ve = term4_avx2(ve, he, t->ek[sb], hx, safe);
                                    vo = term4_avx2(vo, ho, t->ek[sb], hx, safe);
                                }
                                __m256i *ae = (__m256i *) (a + 4 * hh),
                                        *ao = (__m256i *) (a + 8 + 4 * hh);
                                _mm256_store_si256(ae, _mm256_add_epi64(_mm256_load_si256(ae), ve));
                                _mm256_store_si256(ao, _mm256_add_epi64(_mm256_load_si256(ao), vo));
                            }
                        }
                    }
            }
            for (uint32_t i = 0; i < nr; i++)
                mmb_store(j, r + i, g0, gn, (const int64_t(*)[ZT_MMB_G]) acc[i]);
        }
    }
}

#    undef DOT_AVX2

static AVX2 void mmb_tile_avx2(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (j->qtype == ZT_GGML_Q4_K)
        mmb_body_avx2(j, r0, r1, ZT_GGML_Q4_K);
    else if (j->qtype == ZT_GGML_Q6_K)
        mmb_body_avx2(j, r0, r1, ZT_GGML_Q6_K);
    else
        mmb_body_avx2(j, r0, r1, ZT_GGML_Q8_0);
}

/* the weight types the tiles handle: Q4_K and Q6_K (rows of whole blocks),
 * Q8_0 when a row is a whole number of 8 blocks */
static int mmb_handles(const zt_mmb_t *j)
{
    return j->raw && (j->qtype == ZT_GGML_Q4_K || j->qtype == ZT_GGML_Q6_K ||
                      (j->qtype == ZT_GGML_Q8_0 && j->cols % 256 == 0));
}

static int mmb_512(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!mmb_handles(j)) return 1;
    mmb_tile_512(j, r0, r1);
    return 0;
}

static int mmb_avx2(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!mmb_handles(j)) return 1;
    mmb_tile_avx2(j, r0, r1);
    return 0;
}

static int mmb_vnni(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!mmb_handles(j)) return 1;
    mmb_tile_vnni(j, r0, r1);
    return 0;
}

/* ---- attention over a Q16 KV cache (zt_kern.h zt_att_qk_c, zt_att_wv_c):
 * exact 32 x 32 -> 64-bit products (VPMULDQ on the even lanes, and on the odd
 * lanes shifted down), summed in 64-bit lanes ---- */

static AVX2 void att_qk_avx2(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                             uint32_t hd, int64_t *S)
{
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        __m256i a = _mm256_setzero_si256();
        uint32_t j = 0;
        for (; j + 8 <= hd; j += 8) {
            __m256i x = _mm256_loadu_si256((const __m256i *) (q + j));
            __m256i y = _mm256_loadu_si256((const __m256i *) (k + j));
            a = _mm256_add_epi64(a, _mm256_mul_epi32(x, y));
            a = _mm256_add_epi64(
                a, _mm256_mul_epi32(_mm256_srli_epi64(x, 32), _mm256_srli_epi64(y, 32)));
        }
        int64_t t = hsum64_avx2(a);
        for (; j < hd; j++) t += (int64_t) q[j] * k[j];
        S[i] = t;
    }
}

static AVX2 void att_wv_avx2(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                             uint32_t hd, int64_t *acc)
{
    uint32_t j0 = 0;
    for (; j0 + 32 <= hd; j0 += 32) {
        __m256i a[8];
        for (uint32_t c = 0; c < 8; c++)
            a[c] = _mm256_loadu_si256((const __m256i *) (acc + j0 + 4 * c));
        const int32_t *vi = v + j0;
        for (uint32_t i = 0; i < n; i++, vi += vstride) {
            if (!w[i]) continue;
            __m256i wv = _mm256_set1_epi64x(w[i]);
            for (uint32_t c = 0; c < 8; c++)
                a[c] =
                    _mm256_add_epi64(a[c], _mm256_mul_epi32(_mm256_cvtepi32_epi64(_mm_loadu_si128(
                                                                (const __m128i *) (vi + 4 * c))),
                                                            wv));
        }
        for (uint32_t c = 0; c < 8; c++) _mm256_storeu_si256((__m256i *) (acc + j0 + 4 * c), a[c]);
    }
    if (j0 < hd) zt_att_wv_c(w, v + j0, vstride, n, hd - j0, acc + j0);
}

static AVX512 void att_qk_512(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                              uint32_t hd, int64_t *S)
{
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        __m512i a = _mm512_setzero_si512();
        uint32_t j = 0;
        for (; j + 16 <= hd; j += 16) {
            __m512i x = _mm512_loadu_si512((const void *) (q + j));
            __m512i y = _mm512_loadu_si512((const void *) (k + j));
            a = _mm512_add_epi64(a, _mm512_mul_epi32(x, y));
            a = _mm512_add_epi64(
                a, _mm512_mul_epi32(_mm512_srli_epi64(x, 32), _mm512_srli_epi64(y, 32)));
        }
        int64_t t = _mm512_reduce_add_epi64(a);
        for (; j < hd; j++) t += (int64_t) q[j] * k[j];
        S[i] = t;
    }
}

static AVX512 void att_wv_512(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                              uint32_t hd, int64_t *acc)
{
    uint32_t j0 = 0;
    for (; j0 + 64 <= hd; j0 += 64) {
        __m512i a[8];
        for (uint32_t c = 0; c < 8; c++)
            a[c] = _mm512_loadu_si512((const void *) (acc + j0 + 8 * c));
        const int32_t *vi = v + j0;
        for (uint32_t i = 0; i < n; i++, vi += vstride) {
            if (!w[i]) continue;
            __m512i wv = _mm512_set1_epi64(w[i]);
            for (uint32_t c = 0; c < 8; c++)
                a[c] = _mm512_add_epi64(
                    a[c], _mm512_mul_epi32(_mm512_cvtepi32_epi64(
                                               _mm256_loadu_si256((const __m256i *) (vi + 8 * c))),
                                           wv));
        }
        for (uint32_t c = 0; c < 8; c++) _mm512_storeu_si512((void *) (acc + j0 + 8 * c), a[c]);
    }
    if (j0 < hd) att_wv_avx2(w, v + j0, vstride, n, hd - j0, acc + j0);
}

static const zt_kern_t K_AVX2 = {"avx2",       dot_raw_avx2, dot_q8_avx2, 0,           0,
                                 dot_q4k_avx2, dot_q6k_avx2, mmb_avx2,    att_qk_avx2, att_wv_avx2};
static const zt_kern_t K_AVX512 = {"avx512",    dot_raw_512, dot_q8_512, 0,          0,
                                   dot_q4k_512, dot_q6k_512, mmb_512,    att_qk_512, att_wv_512};
/* the same with the AVX512-VNNI tile */
static const zt_kern_t K_AVX512V = {"avx512vnni", dot_raw_512, dot_q8_512, 0,          0,
                                    dot_q4k_512,  dot_q6k_512, mmb_vnni,   att_qk_512, att_wv_512};

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

const zt_kern_t *zt_simd_x86_avx512vnni(void)
{
    return zt_simd_x86_avx512() && __builtin_cpu_supports("avx512vnni") ? &K_AVX512V : 0;
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

const zt_kern_t *zt_simd_x86_avx512vnni(void)
{
    return 0;
}

#endif
