/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_common.h — small private helpers shared by the call engine.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Header-only static inline helpers: big-endian
 * load/store, byte copy/fill/compare, CRC-8, wrapping time comparison and a
 * deterministic 32-bit mixer (used for ordering, never for security).
 *
 * HONEST LIMITS. These are plain byte loops, not tuned memcpy. The mixer is
 * a non-cryptographic hash; nothing here protects confidentiality or
 * integrity. That is the Carracho session's job.
 */
#ifndef CALL_COMMON_H
#define CALL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline void call_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static inline void call_put24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 16);
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) v;
}

static inline void call_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static inline uint16_t call_get16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static inline uint32_t call_get24(const uint8_t *p)
{
    return ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
}

static inline uint32_t call_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static inline void call_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static inline void call_fill(void *d, uint8_t v, uint32_t n)
{
    uint8_t *p = (uint8_t *) d;
    for (uint32_t i = 0; i < n; i++) p[i] = v;
}

static inline int call_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* Struct copy through call_copy, so no compiler-generated memcpy call. */
#define CALL_SET(dst, src)                                                                         \
    call_copy((uint8_t *) &(dst), (const uint8_t *) &(src), (uint32_t) sizeof(dst))

/* CRC-8, polynomial 0x07, init 0. */
static inline uint8_t call_crc8(const uint8_t *p, uint32_t n)
{
    uint8_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (uint8_t) ((c & 0x80) ? (c << 1) ^ 0x07 : (c << 1));
    }
    return c;
}

/* true when time a is at or after time b (32-bit wrapping milliseconds). */
static inline bool call_time_ge(uint32_t a, uint32_t b)
{
    return (int32_t) (a - b) >= 0;
}

/* signed 16-bit sequence distance b - a, wrap-safe. */
static inline int32_t call_seq_diff(uint16_t a, uint16_t b)
{
    return (int16_t) (uint16_t) (b - a);
}

static inline uint32_t call_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t call_abs32(int32_t v)
{
    return v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
}

#endif /* CALL_COMMON_H */
