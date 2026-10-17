/* tribute.c — the exact 11% tributes. See tribute.h.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tribute.h"

/* The public addresses of the three bearer identities (the current
 * generation; public identifiers, never secrets). */
const char *const TRIBUTE_ADDRESS[TRIBUTE_N] = {
    "e3404dd347a15bc3968c634825f21f6c62ebc87a7fa47fefbb51dcfd9a003500", /* Crown */
    "6070ed935310dc866a1dbaa6263774b290710f86cbf99ba1af28af2746fc2527", /* Ministry */
    "4d460611f91c297e6bfcbacd52fd017c6710ee6e5ba8e6bfd6e4d2697ff11006", /* fountain */
};

uint32_t tribute_bps(tribute_rail_t rail)
{
    (void) rail;
    return TRIBUTE_BPS;
}

/* exact floor(yield * bps / denom) without overflowing: split the yield
 * into its denom-multiples and the residue, each scaled separately */
static uint64_t exact_part(uint64_t yield, uint32_t bps)
{
    return (yield / TRIBUTE_BPS_DENOM) * bps +
           (yield % TRIBUTE_BPS_DENOM) * bps / TRIBUTE_BPS_DENOM;
}

int tribute_split(uint64_t yield, uint64_t out[TRIBUTE_N])
{
    if (!out) return 0;
    uint64_t each = exact_part(yield, TRIBUTE_BPS);
    uint64_t total = 0;
    for (uint32_t i = 0; i < TRIBUTE_N; i++) {
        out[i] = each;
        total += each;
    }
    /* the truncation dust rides the Crown: the odd unit to card 0 is the
     * kernel's standing precedent. The unallocated 67% stays with the
     * protocol — never with a tribute rail. */
    out[TRIBUTE_CROWN] += exact_part(yield, TRIBUTE_BPS * TRIBUTE_N) - total;
    return 1;
}
