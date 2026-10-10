/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_upcheck_util.h — private helpers for zx_upcheck*.c (no libc).
 *
 * HONEST LIMITS: byte loops, not tuned; zxu__eq is constant-time in the
 * length only, which is all its callers need.
 */
#ifndef ZXV_ZX_UPCHECK_UTIL_H
#define ZXV_ZX_UPCHECK_UTIL_H

#include <stdint.h>
#include <stdbool.h>

static inline void zxu__cpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static inline void zxu__set(void *dst, uint8_t v, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}

static inline bool zxu__eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static inline uint32_t zxu__strlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (s && n < max && s[n]) n++;
    return n;
}

#endif /* ZXV_ZX_UPCHECK_UTIL_H */
