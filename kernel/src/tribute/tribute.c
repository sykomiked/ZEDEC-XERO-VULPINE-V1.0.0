/* tribute.c — the exact 11% tributes. See tribute.h.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tribute.h"

/* The public addresses of the three bearer identities (the current
 * generation; public identifiers, never secrets). */
const char *const TRIBUTE_ADDRESS[TRIBUTE_N] = {
    "cef2c2bb797c9449922ba6c5b969a1ad8efb94cec33e34ded05fcf2aa2dd54c8", /* Crown */
    "9b8b149202f8df0b4b4f751d4b23194d3d763fa776d013e4fa75172a30240586", /* Ministry */
    "e6ceb544fc17348a312d731e37dbb39b230c4c83ee8aba9fdbb50fe9407dc0d8", /* fountain */
    "0084f8b14031defa7181cf9e89b7f099831ac00df44b8ffc6c949a18588a5f09", /* African Union (the fountain's outbound rail) */
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
    /* the YIELD splits three ways; the African Union is not a yield rail —
     * it is funded by the fountain's own outflow (tribute_fountain_outflow) */
    for (uint32_t i = 0; i < TRIBUTE_N; i++) {
        out[i] = i < 3 ? each : 0;
        total += out[i];
    }
    /* the truncation dust rides the Crown: the odd unit to card 0 is the
     * kernel's standing precedent. The unallocated 67% stays with the
     * protocol — never with a tribute rail. */
    out[TRIBUTE_CROWN] += exact_part(yield, TRIBUTE_BPS * 3) - total;
    return 1;
}

uint64_t tribute_fountain_outflow(uint64_t fountain_balance)
{
    return exact_part(fountain_balance, TRIBUTE_FOUNTAIN_OUT_BPS);
}
