/* lpres_bridge.h — the paradox-operator bridge (the 4/5 pair).
 *
 * The four foundation values are frozen; the gateway (4) is the expansion
 * port. Phase 4 is the BRIDGE: it is two things at once — the entry to the
 * higher phases AND the first paradox operator. Phase 5 is its TANDEM
 * partner. Together they are the first pair of paradox operators, and a
 * paradox operator is usable code: a callable function over the phases
 * that returns a defined result. The next Fibonacci step adds three more
 * (phase 8), then five (phase 13), and so on (LP_EXTENSION.md).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_LPRES_BRIDGE_H
#define ZXV_LPRES_BRIDGE_H

#include <stdint.h>

#include "lpres.h"

/* The bridge: phase 4, two things at once. It takes a foundation value and
 * a phase and returns the phase through the gateway — the entry — and, as
 * the first paradox operator, it HOLDS the contradiction rather than
 * resolving it: a contradiction passed through the bridge stays a
 * non-TRUE value (the never-authorise guard), so the bridge is how a
 * higher phase is reached without any value ever granting authority on
 * its own. */
lpres_state_t lpres_bridge(lpres_state_t foundation, lpres_state_t phase);

/* The tandem: phase 5, the partner of the bridge. Where the bridge holds,
 * the tandem PAIRS: given two phases it returns the pair's defined result
 * — the plus side (the join) when the ranks ascend, the minus side (the
 * meet) when they descend. It is the second paradox operator. */
lpres_state_t lpres_tandem(lpres_state_t a, lpres_state_t b);

/* A paradox operator over the whole ladder: the pair's joint evaluation.
 * Returns 1 when the result is a non-TRUE phase (a held paradox), 0 when
 * it is TRUE. This is the guard as a callable: any function that composes
 * the bridge and the tandem can be checked with it. */
int lpres_holds_paradox(lpres_state_t result);

#endif /* ZXV_LPRES_BRIDGE_H */
