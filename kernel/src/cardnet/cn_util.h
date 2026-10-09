/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_util.h — private freestanding helpers shared by kernel/src/cardnet.
 *
 * Byte copy/zero/compare, bounded string length, and decimal conversion of
 * 64-bit integers by repeated subtraction of powers of ten, so no 64-bit
 * division (and no libgcc __udivdi3 on 32-bit targets) is ever emitted.
 * Not part of the public API.
 */
#ifndef ZXV_CN_UTIL_H
#define ZXV_CN_UTIL_H

#include <stdint.h>
#include <stdbool.h>

static inline void cn_zero(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static inline void cn_copy(void *dst, const void *src, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static inline bool cn_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

/* Length of a NUL-terminated string, at most max. */
static inline uint32_t cn_strnlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

static inline bool cn_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static inline bool cn_all_digits(const char *s, uint32_t n)
{
    if (!s) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!cn_is_digit(s[i])) return false;
    return true;
}

static const uint64_t cn_pow10_tab[20] = {1ull,
                                          10ull,
                                          100ull,
                                          1000ull,
                                          10000ull,
                                          100000ull,
                                          1000000ull,
                                          10000000ull,
                                          100000000ull,
                                          1000000000ull,
                                          10000000000ull,
                                          100000000000ull,
                                          1000000000000ull,
                                          10000000000000ull,
                                          100000000000000ull,
                                          1000000000000000ull,
                                          10000000000000000ull,
                                          100000000000000000ull,
                                          1000000000000000000ull,
                                          10000000000000000000ull};

/* Write v as exactly `width` zero-padded decimal digits (width 1..20).
 * Returns false (and writes nothing) if v does not fit. No division. */
static inline bool cn_u64_to_dec_fixed(uint64_t v, uint32_t width, char *out)
{
    if (width == 0 || width > 20 || !out) return false;
    if (width < 20 && v >= cn_pow10_tab[width]) return false;
    for (uint32_t k = width; k-- > 0;) {
        uint32_t d = 0;
        uint64_t p = cn_pow10_tab[k];
        while (v >= p) {
            v -= p;
            d++;
        }
        out[width - 1 - k] = (char) ('0' + d);
    }
    return true;
}

/* Parse exactly n decimal digits (n <= 19, so it cannot overflow). */
static inline bool cn_dec_to_u64(const char *s, uint32_t n, uint64_t *out)
{
    if (!s || !out || n == 0 || n > 19) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!cn_is_digit(s[i])) return false;
        v = v * 10u + (uint64_t) (s[i] - '0');
    }
    *out = v;
    return true;
}

#endif /* ZXV_CN_UTIL_H */
