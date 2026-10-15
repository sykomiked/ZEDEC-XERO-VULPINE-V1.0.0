/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_cdc.h — content-defined chunking for the matrix (docs/spec/MATRIX.md).
 *
 * A file is cut where its bytes say, not at fixed offsets, so an edit
 * moves only the chunks around it and identical runs in different files
 * become identical chunks (one CID, stored once). The cut rule is the
 * normalised gear-hash rule: h = (h << 1) + G[byte] over the bytes since
 * the last cut; below the tier's minimum never cut; from the minimum to
 * the average cut when the top 32 bits of h are below 2^32 / (4 avg)
 * (harder); from the average to the maximum when they are below
 * 2^32 * 4 / avg (easier); at the maximum always. For a power-of-two
 * average this is exactly "the top log2(avg) + 2 bits are zero" (and
 * log2(avg) - 2 after the average), the normalised mask rule; the threshold
 * form lets the average be any size, a Fibonacci number included, with no
 * division at cut time. G is fixed (splitmix64 of i + 1), so
 * tools/alexandria/cdc_cut.c and this file cut identically, which the
 * tests check.
 *
 * Three tiers, by how the content is used:
 *   HOT   code, read often, deduplicated finely   1 KiB / 4 KiB / 16 KiB
 *   WARM  text, tables and documents              4 KiB / 16 KiB / 64 KiB
 *   COLD  bulk and already-dense data            64 KiB / 256 KiB / 1 MiB - 128
 * Every chunk fits one raw block (1 MiB) with a booklet's header and one
 * directory entry, so even a chunk of the maximum size has a booklet.
 * alx_cdc_make builds any other tier (for example Fibonacci sizes) under
 * the same rule and the same bounds.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_CDC_H
#define ZXV_ALX_CDC_H

#include <stdint.h>

enum { ALX_CDC_HOT = 0, ALX_CDC_WARM = 1, ALX_CDC_COLD = 2 };

#define ALX_CDC_MAXLEN ((1u << 20) - 128u) /* a chunk with its booklet fits 1 MiB */

typedef struct {
    uint32_t min, avg, max;
    uint32_t hard, easy; /* cut below these, on the top 32 bits of h */
} alx_cdc_tier_t;

/* the tier's sizes and thresholds; 0 for an unknown tier */
const alx_cdc_tier_t *alx_cdc_tier(uint32_t tier);
/* A tier of any sizes: 64 <= min < avg < max <= ALX_CDC_MAXLEN. Returns 0,
 * or -1 (t untouched) for sizes outside that. */
int alx_cdc_make(alx_cdc_tier_t *t, uint32_t min, uint32_t avg, uint32_t max);
/* gear value for a byte */
uint64_t alx_cdc_gear(uint8_t b);
/* The length of the first chunk of b[0..len): len itself when len is at
 * most the tier's minimum; never more than the maximum. 0 for len 0 or an
 * unknown tier. */
uint32_t alx_cdc_cut(const uint8_t *b, uint32_t len, uint32_t tier);
/* the same, for a tier from alx_cdc_make (0 for a null tier) */
uint32_t alx_cdc_cut_with(const uint8_t *b, uint32_t len, const alx_cdc_tier_t *t);

#endif
