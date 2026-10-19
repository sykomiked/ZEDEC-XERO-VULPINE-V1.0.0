/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_in.h — helpers shared by the harnesses: a byte reader that turns fuzz
 * input into typed values (running out of input yields zeros, never reads
 * past the end), an edge-value picker for the economic op streams, and a
 * deterministic PRNG (splitmix64) for the seeded property mode. */
#ifndef ZXV_FUZZ_IN_H
#define ZXV_FUZZ_IN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every harness signals a broken property with abort(). Within harness code
 * that call is routed through here first so the log names the line that
 * failed (the kernel sources never see this macro: they do not include this
 * header). */
/* Defined by the replay driver (fuzz_main.c); a weak reference, so under
 * libFuzzer (which saves crashing inputs itself) it is simply absent. */
extern void (*fz_on_failure)(void) __attribute__((weak));
static inline void fz_abort_at(const char *file, int line)
{
    fprintf(stderr, "FUZZ PROPERTY FAILED at %s:%d\n", file, line);
    fflush(stderr);
    if (&fz_on_failure != NULL && fz_on_failure) fz_on_failure();
    (abort)();
}
#define abort() fz_abort_at(__FILE__, __LINE__)

typedef struct {
    const uint8_t *p;
    size_t n;
} fz_in;

static inline void fz_init(fz_in *in, const uint8_t *p, size_t n)
{
    in->p = p;
    in->n = n;
}

static inline uint8_t fz_u8(fz_in *in)
{
    if (in->n == 0) return 0;
    uint8_t v = in->p[0];
    in->p++;
    in->n--;
    return v;
}

static inline uint16_t fz_u16(fz_in *in)
{
    uint16_t v = fz_u8(in);
    return (uint16_t) (v | ((uint16_t) fz_u8(in) << 8));
}

static inline uint32_t fz_u32(fz_in *in)
{
    uint32_t v = fz_u16(in);
    return v | ((uint32_t) fz_u16(in) << 16);
}

static inline uint64_t fz_u64(fz_in *in)
{
    uint64_t v = fz_u32(in);
    return v | ((uint64_t) fz_u32(in) << 32);
}

/* Copy up to n bytes; the rest of out is zero-filled. Returns bytes taken. */
static inline size_t fz_bytes(fz_in *in, void *out, size_t n)
{
    size_t k = n < in->n ? n : in->n;
    if (k) memcpy(out, in->p, k);
    if (k < n) memset((uint8_t *) out + k, 0, n - k);
    in->p += k;
    in->n -= k;
    return k;
}

/* A value biased toward the boundaries money code gets wrong: zero, one,
 * all-ones, the signed and unsigned maxima and minima, powers of two near
 * the 2^31 / 2^32 / 2^53 / 2^62 / 2^63 limits, and their neighbours. */
static inline uint64_t fz_edge64(fz_in *in)
{
    static const uint64_t edges[] = {
        0u,
        1u,
        2u,
        UINT64_MAX,
        UINT64_MAX - 1u,
        (uint64_t) INT64_MAX,
        (uint64_t) INT64_MAX - 1u,
        (uint64_t) INT64_MAX + 1u, /* INT64_MIN as bits */
        (uint64_t) INT32_MAX,
        (uint64_t) INT32_MAX + 1u,
        (uint64_t) UINT32_MAX,
        (uint64_t) UINT32_MAX + 1u,
        (uint64_t) 1 << 53,
        ((uint64_t) 1 << 53) + 1u,
        (uint64_t) 1 << 62,
        ((uint64_t) 1 << 62) - 1u,
        (uint64_t) -1 * 1000u, /* a small negative as bits */
        100u,
        1000000u,
    };
    uint8_t sel = fz_u8(in);
    if (sel < 0x80u) return edges[sel % (sizeof edges / sizeof edges[0])];
    if (sel < 0xC0u) return fz_u8(in);
    return fz_u64(in);
}

/* splitmix64: a full-period 64-bit generator with good mixing, used only to
 * produce reproducible op streams from a seed (not for anything secret). */
static inline uint64_t fz_splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Copy data[0..n) into an exact-size heap buffer so ASan sees any read past
 * the end of the caller's buffer (malloc(0) included: ASan returns a
 * zero-size chunk, so even a one-byte read of an empty input is caught).
 * Caller frees. */
static inline uint8_t *fz_dup(const uint8_t *data, size_t n)
{
    uint8_t *b = (uint8_t *) malloc(n);
    if (!b) abort();
    if (n) memcpy(b, data, n);
    return b;
}

/* The same, NUL-terminated (for parsers that take C strings): n + 1 bytes. */
static inline char *fz_dupz(const uint8_t *data, size_t n)
{
    char *b = (char *) malloc(n + 1u);
    if (!b) abort();
    if (n) memcpy(b, data, n);
    b[n] = 0;
    return b;
}

#endif /* ZXV_FUZZ_IN_H */
