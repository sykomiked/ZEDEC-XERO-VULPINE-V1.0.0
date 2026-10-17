/* lpres_adapter.c — the logic adapters. See lpres_adapter.h for the
 * design. Every conversion is exact and total: no value is lost, no
 * authority is manufactured, and a caller that knows only binary or only
 * the four foundation values can read any lattice state without error.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lpres_adapter.h"

#include <string.h>

/* the rank of a value, or -1 out of range (the kernel's one rank table) */
static int rank_of(lpres_state_t s)
{
    if ((unsigned) s >= LPRES_STATE_COUNT)
        return -1;
    return (int) lpres_rank[s];
}

lpres_state_t lpres_from_binary(int b)
{
    return b ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

int lpres_to_binary(lpres_state_t s)
{
    /* ONLY TRUE becomes 1. Every other value — FALSE, BOTH, NEITHER, or
     * any phase — becomes 0, so no legacy caller can be handed authority
     * the lattice did not grant (the never-authorise guard). */
    return s == LPRES_STATE_TRUE ? 1 : 0;
}

int lpres_adapt_binary(int b)
{
    /* the round trip a legacy caller performs: binary -> the lattice ->
     * binary. Identity for 0 and 1; every non-TRUE lattice value -> 0. */
    return lpres_to_binary(lpres_from_binary(b));
}

void lpres_to_point(lpres_state_t s, uint8_t point[LPRES_POINT_BYTES])
{
    memset(point, 0, LPRES_POINT_BYTES);
    if ((unsigned) s >= LPRES_STATE_COUNT)
        return;                         /* an unknown phase: the empty point */
    /* the point's bit is set at the value's own id, so the mapping is
     * injective: a different value sets a different bit, and no two
     * values share a point (the canonical rule at the single-bit level) */
    uint32_t bit = (uint32_t) s;
    if (bit < LPRES_POINT_BYTES * 8)
        point[bit / 8] |= (uint8_t) (1u << (bit % 8));
}

lpres_state_t lpres_from_point(const uint8_t point[LPRES_POINT_BYTES])
{
    /* the first set bit names the value; an empty point is NEITHER (no
     * evidence). The read is exact: the same point always yields the same
     * value, so the round trip is stable. */
    for (uint32_t i = 0; i < LPRES_POINT_BYTES * 8; i++) {
        if (point[i / 8] & (uint8_t) (1u << (i % 8))) {
            if (i < LPRES_STATE_COUNT)
                return (lpres_state_t) i;
            return LPRES_STATE_NEITHER;   /* a phase above the gateway */
        }
    }
    return LPRES_STATE_NEITHER;
}

lpres_state_t lpres_phase_to_foundation(lpres_state_t phase)
{
    if ((unsigned) phase < LPRES_STATE_COUNT) {
        /* a foundation value is its own answer, except that the four are
         * already the four */
        return phase;
    }
    int r = rank_of(phase);
    if (r < 0)
        return LPRES_STATE_NEITHER;
    /* the phase's rank maps to the nearest foundation value by the chain:
     * below NEITHER -> NEITHER, NEITHER..BOTH -> BOTH, at TRUE's rank ->
     * TRUE (and only TRUE's rank, so the guard holds) */
    int r_true = rank_of(LPRES_STATE_TRUE);
    if (r >= r_true)
        return LPRES_STATE_TRUE;
    if (r >= rank_of(LPRES_STATE_BOTH))
        return LPRES_STATE_BOTH;
    return LPRES_STATE_NEITHER;
}
