/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_quality.h — thorough, checked work is the minimum viable output.
 *
 *   Q1  THE GATE.  An output may be presented only if r * l >= 1.8, the
 *       Hackronomicon quality gate: r is breadth (how much of the request it
 *       covers, x1000) and l is the share of its claims actually verified
 *       (x1000, from tests, sources and witnesses, not self-estimates).
 *   Q2  REVISE FIRST.  An output that fails the gate is audited and revised
 *       before anyone sees it, up to F(L+2) passes for a swarm of L levels.
 *   Q3  NO DRESSING UP.  If it still fails after the last pass it is shown
 *       FLAGGED, with what is unverified stated plainly, never as finished.
 *   Q4  QUALITY PAYS.  Only outputs that pass the gate earn value income in
 *       the market (swarm_market.h M6).
 * Freestanding: no libc, no floating point.
 */
#ifndef SWARM_QUALITY_H
#define SWARM_QUALITY_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_market.h"

#define SWARM_Q_GATE_MILLI2 1800000u /* 1.8 in (x1000) * (x1000) */

typedef enum {
    SWARM_Q_PRESENT = 0,    /* passed the gate */
    SWARM_Q_REVISE,         /* audit and revise, then gate again */
    SWARM_Q_PRESENT_FLAGGED /* out of passes: show with what is unverified */
} swarm_q_action_t;

/* Q1 */
bool swarm_quality_passes(uint32_t r_milli, uint32_t l_milli);

/* Q2: most audit-and-revise passes for a swarm of L levels, F(L+2). */
uint32_t swarm_quality_max_passes(uint32_t num_levels);

/* Q1-Q3: what to do with an output after `passes_done` revisions. */
swarm_q_action_t swarm_quality_next(uint32_t r_milli, uint32_t l_milli, uint32_t passes_done,
                                    uint32_t num_levels);

/* Q4: credit value capital only if the output passed the gate.
 * SWARM_ERR_ARG if it did not (nothing is credited). */
swarm_status_t swarm_quality_credit(swarm_market_t *m, uint32_t model_id, swarm_cap_t form,
                                    uint64_t amount, uint32_t r_milli, uint32_t l_milli);

#endif /* SWARM_QUALITY_H */
