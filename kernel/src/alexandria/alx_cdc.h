/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_cdc.h — content-defined chunking for the matrix (docs/spec/MATRIX.md).
 *
 * A file is cut where its bytes say, not at fixed offsets, so an edit
 * moves only the chunks around it and identical runs in different files
 * become identical chunks (one CID, stored once). The cut rule is the
 * normalised gear-hash rule: h = (h << 1) + G[byte] over the bytes since
 * the last cut; below the tier's minimum never cut; from the minimum to
 * the average cut when the top s bits of h are all zero (s = log2(avg) + 2,
 * harder); from the average to the maximum when the top s - 4 bits are
 * (easier); at the maximum always. G is fixed (splitmix64 of i + 1), so
 * tools/alexandria/pack_matrix.py and this file cut identically, which the
 * tests check.
 *
 * Three tiers, by how the content is used:
 *   HOT   code, read often, deduplicated finely   1 KiB / 4 KiB / 16 KiB
 *   WARM  text, tables and documents              4 KiB / 16 KiB / 64 KiB
 *   COLD  bulk and already-dense data            64 KiB / 256 KiB / 1 MiB - 128
 * Every chunk fits one raw block (1 MiB) with a booklet's header and one
 * directory entry, so even a chunk of the maximum size has a booklet.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_CDC_H
#define ZXV_ALX_CDC_H

#include <stdint.h>

enum { ALX_CDC_HOT = 0, ALX_CDC_WARM = 1, ALX_CDC_COLD = 2 };

typedef struct {
    uint32_t min, avg, max;
    uint64_t hard, easy; /* the masks before and after the average */
} alx_cdc_tier_t;

/* the tier's sizes and masks; 0 for an unknown tier */
const alx_cdc_tier_t *alx_cdc_tier(uint32_t tier);
/* gear value for a byte */
uint64_t alx_cdc_gear(uint8_t b);
/* The length of the first chunk of b[0..len): len itself when len is at
 * most the tier's minimum; never more than the maximum. 0 for len 0 or an
 * unknown tier. */
uint32_t alx_cdc_cut(const uint8_t *b, uint32_t len, uint32_t tier);

#endif
