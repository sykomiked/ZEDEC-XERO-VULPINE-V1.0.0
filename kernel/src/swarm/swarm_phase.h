/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_phase.h — counting phase ticks by numerological reduction, and
 * placing thought constructs on a golden-ratio geometry.
 *
 *   P1  TWO COUNTS.  Every clock keeps the long count (the exact tick, a
 *       64-bit ledger value) and its reduction, the digital root 1..9. The
 *       reduction steps 1 2 ... 9 1 2 ... with no division, so the hot path
 *       only touches a number below 10, however long the swarm has run.
 *   P2  ZERO IS NOW.  In the reduction, 0 is not a count. It is the point of
 *       action: the tick at which something is being done reads 0. Every
 *       other tick, past or future, reads 1..9.
 *   P3  VORTEX PHASE.  The reduction tells the phase: 3 6 9 are the axis,
 *       1 2 4 8 7 5 the loop, at a loop step (swarm_enochian.h E5).
 *   P4  THE LEDGER CHECK.  The long count is what is posted to the ledger;
 *       the reduction must always equal root(long count). Anyone can check
 *       that in one step, the same way the witness checks an allotment.
 *   P5  GEOMETRIC FORM.  Every logic construct gets a place on the Fibonacci
 *       lattice of size F(n): point k is (k mod F(n), k * F(n-1) mod F(n)).
 *       Its points spread by the golden ratio (F(n-1)/F(n) tends to 1/phi),
 *       like seeds in a sunflower. A claim's level picks n (F(level + 5)
 *       points, at least 5), its topic picks k, and its space picks the layer:
 *       +1 for S+, -1 for S-, 0 for S0. Opposite claims on a topic land on
 *       mirror points, so a contradiction is a visible reflection.
 * Freestanding: no libc, no floating point, no 64-bit division.
 */
#ifndef SWARM_PHASE_H
#define SWARM_PHASE_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_enochian.h"
#include "swarm_logic.h"

typedef struct {
    uint64_t long_count; /* P1, the ledger value */
    uint8_t reduced;     /* P1, 1..9 */
} swarm_phase_t;

typedef struct {
    uint8_t reading;       /* P2: 0 now, else 1..9 */
    swarm_vortex_t vortex; /* P3 */
    int8_t loop_step;
} swarm_phase_reading_t;

void swarm_phase_init(swarm_phase_t *p, uint64_t long_count);
void swarm_phase_step(swarm_phase_t *p, uint32_t ticks);
swarm_phase_reading_t swarm_phase_read(const swarm_phase_t *p, bool acting_now);
bool swarm_phase_check(const swarm_phase_t *p); /* P4 */

typedef struct {
    uint64_t n; /* lattice size F(...) */
    uint64_t x, y;
    int8_t layer; /* +1, -1, 0 */
} swarm_geo_t;

/* P5 */
swarm_geo_t swarm_geo_point(uint64_t k, uint32_t fib_index);
swarm_geo_t swarm_geo_claim(const swarm_claim_t *c);

#endif /* SWARM_PHASE_H */
