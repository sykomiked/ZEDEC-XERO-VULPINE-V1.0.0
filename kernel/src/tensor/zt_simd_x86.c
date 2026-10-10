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

/* zt_term52(s[i] * mul[i], k[i]) for 4 lanes */
static inline AVX2 __m256i term52_avx2(__m128i s, __m128i mul, __m128i k)
{
    const __m256i zero = _mm256_setzero_si256();
    const __m256i lim = _mm256_set1_epi64x(ZT_KERN_LIM52);
    const __m256i nlim = _mm256_set1_epi64x(-ZT_KERN_LIM52);
    __m256i v = _mm256_mul_epi32(_mm256_cvtepi32_epi64(s), _mm256_cvtepi32_epi64(mul));
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
    __m256i ab = _mm256_hadd_epi32(fold512(p[0]), fold512(p[1]));
    __m256i cd = _mm256_hadd_epi32(fold512(p[2]), fold512(p[3]));
    __m256i ef = _mm256_hadd_epi32(fold512(p[4]), fold512(p[5]));
    __m256i gh = _mm256_hadd_epi32(fold512(p[6]), fold512(p[7]));
    __m256i abcd = _mm256_hadd_epi32(ab, cd); /* a b c d | a b c d (halves) */
    __m256i efgh = _mm256_hadd_epi32(ef, gh);
    __m256i lo = _mm256_permute2x128_si256(abcd, efgh, 0x20);
    __m256i hi = _mm256_permute2x128_si256(abcd, efgh, 0x31);
    return _mm256_add_epi32(lo, hi); /* a b c d e f g h */
}

static inline AVX512 __m512i term52_512(__m256i s, __m256i mul, __m256i k)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lim = _mm512_set1_epi64(ZT_KERN_LIM52);
    const __m512i nlim = _mm512_set1_epi64(-ZT_KERN_LIM52);
    __m512i v = _mm512_mul_epi32(_mm512_cvtepi32_epi64(s), _mm512_cvtepi32_epi64(mul));
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

static const zt_kern_t K_AVX2 = {"avx2", dot_raw_avx2, dot_q8_avx2, 0, 0};
static const zt_kern_t K_AVX512 = {"avx512", dot_raw_512, dot_q8_512, 0, 0};

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
