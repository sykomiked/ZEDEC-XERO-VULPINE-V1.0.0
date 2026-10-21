/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_common.h: helpers for the differential probes (classify.py). A probe
 * prints results and state; it never asserts. */
#ifndef ZXV_PROBE_COMMON_H
#define ZXV_PROBE_COMMON_H
#include <stdint.h>
#include <stdio.h>

static uint64_t probe_rng = 0x9E3779B97F4A7C15ull;
static inline void probe_seed(uint64_t s) { probe_rng = s * 0x9E3779B97F4A7C15ull + 1; }
static inline uint32_t probe_next(void) {
    probe_rng ^= probe_rng << 13;
    probe_rng ^= probe_rng >> 7;
    probe_rng ^= probe_rng << 17;
    return (uint32_t)(probe_rng >> 16);
}
static inline uint32_t probe_below(uint32_t n) { return n ? probe_next() % n : 0; }

/* FNV-1a over raw bytes: a whole public struct, padding included (probes
 * fill a struct with one byte value before its init, so padding agrees). */
static inline uint64_t probe_fnv(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
    return h;
}
/* Field-wise digest, for structs whose padding a by-value copy can fill
 * with stack bytes: feed each field with probe_mix. */
static inline void probe_mix(uint64_t *h, uint64_t v) {
    for (int i = 0; i < 8; i++) { *h ^= (uint8_t)(v >> (8 * i)); *h *= 0x100000001b3ull; }
}
#define PROBE_STATE(tag, obj) printf("%s state %016llx\n", (tag), \
                                     (unsigned long long)probe_fnv(&(obj), sizeof(obj)))
#endif
