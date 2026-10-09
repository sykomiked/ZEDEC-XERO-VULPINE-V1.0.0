/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_hqc5_gf2x.c — fast constant-time GF(2)[X]/(X^n - 1) multiply for HQC-5.
 *
 * Replaces hqc5/gf2x.c (kept in the tree unchanged, and still compiled as
 * the reference that pqm_hqc5_mul_selftest() checks this one against).
 * Included only by pq_hqc5.c, inside the HQC unit, so it sees the
 * reference's parameters.h and the zxv_hqc5_* renames.
 *
 * WHY
 * ---
 * The reference multiplies 16-word blocks one bit at a time; that is
 * almost all of HQC's run time (one multiply in KeyGen, two in Encaps,
 * three in Decaps), and it made a MATRIX handshake cost ~80 ms. Here a
 * 64 x 64 -> 128-bit carry-less multiply is the base case, Karatsuba
 * recursion runs above it, and the reduction mod X^n - 1 is the
 * reference's. The base case is chosen at COMPILE time:
 *
 *   "pclmulqdq"  x86-64 built with -mpclmul (or a -march that has it)
 *   "pmull"      AArch64 built with the crypto extension
 *                (-march=armv8-a+crypto, or +aes)
 *   "mulholes"   x86-64 / AArch64 without those: integer multiplies of
 *                operands masked to every 4th bit ("bmul", as in BearSSL's
 *                ghash_ctmul), so carries land in the gaps and are masked
 *                off. Constant time BECAUSE the 64-bit multiplier of every
 *                x86-64 and AArch64 core is; ~4x the shift-and-mask speed.
 *   "shiftmask"  every other target, or forced with -DPQM_HQC_SHIFTMASK:
 *                shifts and masks only, every bit of `a` through an
 *                all-ones / all-zeros mask. Constant time on any CPU,
 *                including cores (e.g. Cortex-M3) whose multiplier is not.
 *
 * -DPQM_HQC_PORTABLE_CLMUL disables the two instruction paths (for
 * testing the portable ones on hardware that has them).
 *
 * CONSTANT TIME: no branch and no memory index depends on the operands;
 * the recursion shape depends only on the public length n. PCLMULQDQ and
 * PMULL are fixed-latency instructions.
 */
#include <stddef.h>
#include <stdint.h>

#include "hqc5/gf2x.h"
#include "hqc5/parameters.h"

#if defined(__x86_64__) && defined(__PCLMUL__) && !defined(PQM_HQC_PORTABLE_CLMUL)
#    include <wmmintrin.h>
#    define PQM_CLMUL_BACKEND "pclmulqdq"
static inline void pqm_clmul64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    __m128i r = _mm_clmulepi64_si128(_mm_cvtsi64_si128((long long) a),
                                     _mm_cvtsi64_si128((long long) b), 0x00);
    *lo = (uint64_t) _mm_cvtsi128_si64(r);
    *hi = (uint64_t) _mm_cvtsi128_si64(_mm_unpackhi_epi64(r, r));
}
#elif defined(__aarch64__) && (defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)) &&     \
    !defined(PQM_HQC_PORTABLE_CLMUL)
#    include <arm_neon.h>
#    define PQM_CLMUL_BACKEND "pmull"
static inline void pqm_clmul64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    poly128_t r = vmull_p64((poly64_t) a, (poly64_t) b);
    uint64x2_t v = vreinterpretq_u64_p128(r);
    *lo = vgetq_lane_u64(v, 0);
    *hi = vgetq_lane_u64(v, 1);
}
#elif (defined(__x86_64__) || defined(__aarch64__)) && !defined(PQM_HQC_SHIFTMASK)
#    define PQM_CLMUL_BACKEND "mulholes"
/* 32 x 32 -> 64 carry-less: split each operand into the bits at positions
 * = k mod 4. A product of two such parts has at most 8 terms per output
 * position, which needs 3 bits, and the gap between used positions is 4,
 * so the integer carries never reach another used position. */
static inline uint64_t pqm_bmul32(uint32_t x, uint32_t y)
{
    const uint64_t x0 = x & 0x11111111u, x1 = x & 0x22222222u, x2 = x & 0x44444444u,
                   x3 = x & 0x88888888u;
    const uint64_t y0 = y & 0x11111111u, y1 = y & 0x22222222u, y2 = y & 0x44444444u,
                   y3 = y & 0x88888888u;
    uint64_t z0 = (x0 * y0) ^ (x1 * y3) ^ (x2 * y2) ^ (x3 * y1);
    uint64_t z1 = (x0 * y1) ^ (x1 * y0) ^ (x2 * y3) ^ (x3 * y2);
    uint64_t z2 = (x0 * y2) ^ (x1 * y1) ^ (x2 * y0) ^ (x3 * y3);
    uint64_t z3 = (x0 * y3) ^ (x1 * y2) ^ (x2 * y1) ^ (x3 * y0);
    return (z0 & 0x1111111111111111ull) | (z1 & 0x2222222222222222ull) |
           (z2 & 0x4444444444444444ull) | (z3 & 0x8888888888888888ull);
}
/* 64 x 64 by one Karatsuba step over 32-bit halves: 48 multiplies. */
static inline void pqm_clmul64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    uint32_t a0 = (uint32_t) a, a1 = (uint32_t) (a >> 32);
    uint32_t b0 = (uint32_t) b, b1 = (uint32_t) (b >> 32);
    uint64_t z0 = pqm_bmul32(a0, b0), z2 = pqm_bmul32(a1, b1);
    uint64_t zm = pqm_bmul32(a0 ^ a1, b0 ^ b1) ^ z0 ^ z2;
    *lo = z0 ^ (zm << 32);
    *hi = z2 ^ (zm >> 32);
}
#else
#    define PQM_CLMUL_BACKEND "shiftmask"
/* 64 x 64 -> 128 by shifts and masks. Four bits of `a` per step against
 * the precomputed b, 2b, 4b, 8b (all in registers, no table lookup). */
static inline void pqm_clmul64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    const uint64_t b1l = b << 1, b1h = b >> 63;
    const uint64_t b2l = b << 2, b2h = b >> 62;
    const uint64_t b3l = b << 3, b3h = b >> 61;
    uint64_t l = 0, h = 0;
    for (unsigned i = 0; i < 64; i += 4) {
        uint64_t m0 = -((a >> i) & 1), m1 = -((a >> (i + 1)) & 1);
        uint64_t m2 = -((a >> (i + 2)) & 1), m3 = -((a >> (i + 3)) & 1);
        uint64_t tl = (b & m0) ^ (b1l & m1) ^ (b2l & m2) ^ (b3l & m3);
        uint64_t th = (b1h & m1) ^ (b2h & m2) ^ (b3h & m3);
        /* shift the 128-bit term left by i and accumulate; the i == 0
         * case depends only on the public loop counter */
        l ^= tl << i;
        h ^= (th << i) ^ (i ? tl >> (64 - i) : 0);
    }
    *lo = l;
    *hi = h;
}
#endif

/* Karatsuba stops at this many words: below it a word-level schoolbook
 * is cheaper. Tuned on HQC-5 vect_mul timings (test_pq_matrix.c). */
#ifndef PQM_KARA_BASE
#    if defined(__PCLMUL__) || defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
#        define PQM_KARA_BASE 8
#    else
#        define PQM_KARA_BASE 1
#    endif
#endif

/* r[0 .. 2n) = a[0 .. n) * b[0 .. n), schoolbook over words. */
static void pqm_school(uint64_t *r, const uint64_t *a, const uint64_t *b, size_t n)
{
    for (size_t i = 0; i < 2 * n; i++) r[i] = 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            uint64_t lo, hi;
            pqm_clmul64(a[i], b[j], &lo, &hi);
            r[i + j] ^= lo;
            r[i + j + 1] ^= hi;
        }
    }
}

/* r[0 .. 2n) = a * b by Karatsuba. Splits n into a low half of m = n/2
 * words and a high half of n1 = n - m >= m words. tmp needs
 * 4 * n1 words here plus whatever the recursion below uses
 * (< 8 n + 4 log2 n words in all). */
static void pqm_kara(uint64_t *r, const uint64_t *a, const uint64_t *b, size_t n, uint64_t *tmp)
{
    if (n <= PQM_KARA_BASE) {
        pqm_school(r, a, b, n);
        return;
    }
    const size_t m = n >> 1, n1 = n - m;
    uint64_t *ta = tmp, *tb = ta + n1, *zm = tb + n1, *child = zm + 2 * n1;

    pqm_kara(r, a, b, m, child);                  /* z0 = a0 b0 -> r[0 .. 2m)  */
    pqm_kara(r + 2 * m, a + m, b + m, n1, child); /* z2 = a1 b1 -> r[2m .. 2n) */
    for (size_t i = 0; i < n1; i++) {
        ta[i] = a[m + i] ^ (i < m ? a[i] : 0);
        tb[i] = b[m + i] ^ (i < m ? b[i] : 0);
    }
    pqm_kara(zm, ta, tb, n1, child); /* (a0 + a1)(b0 + b1) */
    for (size_t i = 0; i < 2 * n1; i++) zm[i] ^= r[2 * m + i] ^ (i < 2 * m ? r[i] : 0);
    for (size_t i = 0; i < 2 * n1; i++) r[m + i] ^= zm[i];
}

#define PQM_KARA_TMP_WORDS (8 * VEC_N_SIZE_64 + 64)

void vect_mul(uint64_t *o, const uint64_t *a1, const uint64_t *a2)
{
    uint64_t unreduced[2 * VEC_N_SIZE_64];
    uint64_t tmp[PQM_KARA_TMP_WORDS];
    pqm_kara(unreduced, a1, a2, VEC_N_SIZE_64, tmp);

    /* mod X^n - 1: fold the high half back, as hqc5/gf2x.c reduce() does */
    for (size_t i = 0; i < VEC_N_SIZE_64; i++) {
        uint64_t r = unreduced[i + VEC_N_SIZE_64 - 1] >> (PARAM_N & 0x3F);
        uint64_t carry = unreduced[i + VEC_N_SIZE_64] << (64 - (PARAM_N & 0x3F));
        o[i] = unreduced[i] ^ r ^ carry;
    }
    o[VEC_N_SIZE_64 - 1] &= BITMASK(PARAM_N, 64);

    volatile uint64_t *w = unreduced;
    for (size_t i = 0; i < 2 * VEC_N_SIZE_64; i++) w[i] = 0;
    w = tmp;
    for (size_t i = 0; i < PQM_KARA_TMP_WORDS; i++) w[i] = 0;
}
