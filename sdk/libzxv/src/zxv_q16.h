/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_q16.h — exact rational -> Q16.16 rounding for zxv_rat_to_q16. Not
 * part of the ABI (static inline, included by zxv_core.c and by the test
 * test/test_q16.c, which checks the portable path against the 128-bit one).
 *
 * Two implementations give identical results:
 *   - 128-bit: (num * 65536) / den with __int128, when the compiler has it
 *     (__SIZEOF_INT128__) and ZXV_NO_INT128 is not defined;
 *   - portable: integer part by 64-bit division, then the 16 fraction bits
 *     by restoring long division of the remainder. Every intermediate stays
 *     below 2^64 because the remainder is < den < 2^63. Used on compilers
 *     without __int128 (MSVC, 32-bit targets) and forced by -DZXV_NO_INT128.
 *
 * Rounding is half away from zero. Inputs must be reduced with den > 0 and
 * num != INT64_MIN (zxv_rat_to_q16 normalizes first). */
#ifndef ZXV_Q16_H
#define ZXV_Q16_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__SIZEOF_INT128__) && !defined(ZXV_NO_INT128)
#    define ZXV_Q16_HAVE_INT128 1
#endif

/* Portable path. true and *out on success, false if outside int32. */
static inline bool zxv_q16_portable(int64_t num, int64_t den, int32_t *out)
{
    bool neg = num < 0;
    uint64_t a = neg ? (uint64_t) 0 - (uint64_t) num : (uint64_t) num;
    uint64_t d = (uint64_t) den;
    uint64_t q = a / d, r = a % d;
    /* |result| >= q * 65536; anything above 2^31 cannot fit. */
    if (q > (UINT64_C(1) << 15)) return false;
    uint64_t frac = 0;
    for (int i = 0; i < 16; i++) { /* r < d < 2^63, so 2r < 2^64 */
        r <<= 1;
        frac <<= 1;
        if (r >= d) {
            r -= d;
            frac |= 1;
        }
    }
    uint64_t mag = (q << 16) | frac;
    if (r >= d - r) mag++; /* 2r >= d without forming 2r */
    if (neg ? mag > (UINT64_C(1) << 31) : mag > (uint64_t) INT32_MAX) return false;
    *out = neg ? (int32_t) (0 - (int64_t) mag) : (int32_t) mag;
    return true;
}

#ifdef ZXV_Q16_HAVE_INT128
static inline bool zxv_q16_int128(int64_t num, int64_t den, int32_t *out)
{
    __int128 scaled = (__int128) num * 65536;
    __int128 q = scaled / den, rem = scaled % den;
    if (rem < 0) rem = -rem;
    if (2 * rem >= (__int128) den) q += scaled < 0 ? -1 : 1;
    if (q > INT32_MAX || q < INT32_MIN) return false;
    *out = (int32_t) q;
    return true;
}
#endif

static inline bool zxv_q16_round(int64_t num, int64_t den, int32_t *out)
{
#ifdef ZXV_Q16_HAVE_INT128
    return zxv_q16_int128(num, den, out);
#else
    return zxv_q16_portable(num, den, out);
#endif
}

#endif /* ZXV_Q16_H */
