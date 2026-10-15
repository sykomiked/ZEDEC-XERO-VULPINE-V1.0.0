/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_cdc.c — content-defined chunking (alx_cdc.h). */
#include "alx_cdc.h"

/* top k bits set */
#define TOP(k) (~0ull << (64 - (k)))

static const alx_cdc_tier_t TIERS[3] = {
    {1u << 10, 1u << 12, 1u << 14, TOP(14), TOP(10)},
    {1u << 12, 1u << 14, 1u << 16, TOP(16), TOP(12)},
    {1u << 16, 1u << 18, (1u << 20) - 128u, TOP(20), TOP(16)}, /* fits a booklet */
};

const alx_cdc_tier_t *alx_cdc_tier(uint32_t tier)
{
    return tier < 3 ? &TIERS[tier] : 0;
}

uint64_t alx_cdc_gear(uint8_t b)
{
    uint64_t z = (uint64_t) (b + 1u) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint32_t alx_cdc_cut(const uint8_t *b, uint32_t len, uint32_t tier)
{
    const alx_cdc_tier_t *t = alx_cdc_tier(tier);
    if (!t || !b || len == 0) return 0;
    if (len <= t->min) return len;
    uint32_t end = len < t->max ? len : t->max, mid = len < t->avg ? len : t->avg, i = 0;
    uint64_t h = 0;
    /* the minimum is skipped, but the hash sees the last 64 bytes of it */
    for (i = t->min > 64u ? t->min - 64u : 0; i < t->min; i++) h = (h << 1) + alx_cdc_gear(b[i]);
    for (; i < mid; i++) {
        h = (h << 1) + alx_cdc_gear(b[i]);
        if (!(h & t->hard)) return i + 1u;
    }
    for (; i < end; i++) {
        h = (h << 1) + alx_cdc_gear(b[i]);
        if (!(h & t->easy)) return i + 1u;
    }
    return end;
}
