/* tribute.h — the three 11% protocol tributes: the Crown, the Ministry and
 * the keyless R&D fountain. Only the PUBLIC addresses live here (they are
 * identifiers, like account numbers); the private keys never enter the
 * source, the image or the network — they live solely in the bearer's
 * local sealed storage.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_TRIBUTE_H
#define ZXV_TRIBUTE_H

#include <stdint.h>

/* 11% in basis points */
#define TRIBUTE_BPS 1100u
#define TRIBUTE_BPS_DENOM 10000u

typedef enum {
    TRIBUTE_CROWN = 0,    /* the sovereign heritage reserve               */
    TRIBUTE_MINISTRY = 1, /* public goods and civic infrastructure        */
    TRIBUTE_FOUNTAIN = 2, /* the keyless R&D rail: olive-oil-barrel grants */
    TRIBUTE_AFRICAN_UNION = 3, /* funded by the fountain's own 11% flow    */
    TRIBUTE_N = 4
} tribute_rail_t;

/* The fountain's outflow: 11% of the fountain's balance flows to the
 * African Union each cycle — a fountain paying a fountain, so the grant
 * program's rail funds the Union's development rail automatically. */
#define TRIBUTE_FOUNTAIN_OUT_BPS 1100u
uint64_t tribute_fountain_outflow(uint64_t fountain_balance);

/* The public destinations: the sha256 addresses of the bearer identities.
 * Baked in because a destination must be a protocol constant; public by
 * nature (the private halves are held offline by the bearers). */
extern const char *const TRIBUTE_ADDRESS[TRIBUTE_N];

/* The exact split of a yield in the smallest unit: each rail takes 11%,
 * integer division; the truncation remainder rides the Crown (the odd
 * unit to card 0, the kernel's standing precedent). Returns 1 when the
 * sum is exact, 0 on a null out. */
int tribute_split(uint64_t yield, uint64_t out[TRIBUTE_N]);

/* The per-rail basis points (all 1100 today; named so a future schedule
 * can move them in one place). */
uint32_t tribute_bps(tribute_rail_t rail);

#endif /* ZXV_TRIBUTE_H */
