/* lpres_bridge.c — the bridge and the tandem, the first paradox-operator
 * pair (phases 4 and 5). See lpres_bridge.h for the design and
 * docs/agent_loop/LP_EXTENSION.md for the Fibonacci ladder.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lpres_bridge.h"

/* The rank of a value, or -1 for one out of range. The operators use the
 * kernel's single rank table (lpres_rank), so the pair extends with the
 * ladder and never re-derives an ordering. */
static int rank_of(lpres_state_t s)
{
    if ((unsigned) s >= LPRES_STATE_COUNT)
        return -1;
    return (int) lpres_rank[s];
}

lpres_state_t lpres_bridge(lpres_state_t foundation, lpres_state_t phase)
{
    /* The bridge's two jobs at once. As the ENTRY: it accepts any phase
     * above the gateway (a phase is reachable through it). As the first
     * paradox operator: it never manufactures authority, so if either
     * input is out of range or the foundation is a non-TRUE value, the
     * result is that non-TRUE value held (the paradox is contained, never
     * resolved into TRUE). */
    int rf = rank_of(foundation);
    int rp = rank_of(phase);
    if (rf < 0 || rp < 0)
        return LPRES_STATE_NEITHER;   /* an unknown phase is never authority */
    /* the entry: a phase at or above the gateway is passed through */
    if (rp >= (int) lpres_rank[LPRES_STATE_GATEWAY] && rf >= (int) lpres_rank[LPRES_STATE_TRUE])
        return phase;                /* the foundation authorises; the phase enters */
    /* the hold: otherwise the pair is held at the lower rank */
    return lpres_rank[foundation] <= lpres_rank[phase] ? foundation : phase;
}

lpres_state_t lpres_tandem(lpres_state_t a, lpres_state_t b)
{
    /* The tandem: the bridge's partner. It pairs two phases and returns
     * the defined result — the join side when the ranks ascend, the meet
     * side when they descend — which is exactly the pair (plus, minus)
     * that every Fibonacci step adds. */
    int ra = rank_of(a);
    int rb = rank_of(b);
    if (ra < 0 || rb < 0)
        return LPRES_STATE_NEITHER;
    /* ascending ranks take the join (the plus side), descending the
     * meet (the minus side) */
    return ra < rb ? lpres_disjoin(a, b) : lpres_conjoin(a, b);
}

int lpres_holds_paradox(lpres_state_t result)
{
    /* the guard as a callable: a held paradox is any non-TRUE result */
    return result != LPRES_STATE_TRUE;
}
