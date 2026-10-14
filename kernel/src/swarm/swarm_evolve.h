/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_evolve.h — the evolution protocol: automated, sustainable
 * self-improvement that never gives back what it has gained.
 *
 * A "model state" is everything that makes one agent the agent it is: its
 * DNA, the base model and adapter it runs, and its measured score and cost.
 * New states are proposed by DNA crossover (swarm_dna.h), by reflection
 * (swarm_self.h) and by outliers. The protocol decides which ones live.
 *
 *   V1  THE RATCHET.  A candidate replaces the champion only if its score is
 *       at least the champion's AND it is strictly better on score or on
 *       cost. Score can therefore never go down: nothing gained is lost.
 *   V2  GROW OR REFINE.  A candidate that costs more than the budget allows
 *       is refused, however good. With no room to grow, the only way forward
 *       is a candidate that keeps the score and costs less. The budget is the
 *       only bound, and growth is always sustainable.
 *   V3  OUTLIERS.  1/21 of every cycle's tokens (F(1)/F(8)) is set aside for
 *       the agent whose DNA is furthest from the swarm's mean, so unusual
 *       approaches keep getting tried. Their work still passes the quality
 *       gate; an outlier whose candidate wins the ratchet is promoted (V4).
 *   V4  SOCIAL MOBILITY.  Each fundamental cycle, at every boundary between
 *       two levels, the fittest agent of the lower level swaps places with
 *       the least fit of the level above when it is fitter. Level sizes
 *       never change. Level 0, the companion, is not contested: its seat
 *       stays with the person's Chiglet, which evolves by V1 like everyone.
 *   V5  SAVE AND RESTORE.  A state saves to a 64-byte record with a CRC-32.
 *       It names its parent, which is its undo path (the S- of trispace.h),
 *       so the person can keep, share, or roll back any model the swarm made.
 *   V6  DETERMINISM.  The same inputs always give the same decisions.
 *   V7  THE COMPANION'S STEP.  On the GROWTH harmonic the companion takes
 *       one V1 step. The candidate is its DNA combined (D3) with the DNA of
 *       the fittest other agent (the donor), nonce = the fundamental. The
 *       champion is scored with the companion's own fitness (D4), the
 *       candidate with the donor's; costs are equal, so the candidate wins
 *       only when the donor is strictly fitter. On a win the companion takes
 *       the child's DNA: a new seed, both parents recorded and the
 *       generation max(parents') + 1 (D5). Fitness above UINT32_MAX counts
 *       as UINT32_MAX. The donor's score stands in for the child's, which
 *       has not run yet: a proxy, not a measurement of the child.
 * Freestanding: no libc, no floating point.
 */
#ifndef SWARM_EVOLVE_H
#define SWARM_EVOLVE_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_dna.h"

#define SWARM_EVO_RECORD 64u

typedef struct {
    uint64_t id, parent; /* parent 0: a founder */
    swarm_dna_t dna;
    uint32_t base_model;  /* index into the installed model packs */
    uint32_t adapter;     /* index of its adapter, 0 for none */
    uint32_t score_milli; /* gated quality, x1000 */
    uint32_t cost_tokens; /* tokens per task at that quality */
} swarm_model_state_t;

typedef enum {
    SWARM_EVO_ACCEPT = 0,
    SWARM_EVO_REJECT_WORSE,   /* V1: lower score */
    SWARM_EVO_REJECT_NO_GAIN, /* V1: no better on score or cost */
    SWARM_EVO_REJECT_BUDGET   /* V2: over budget */
} swarm_evo_verdict_t;

/* V1, V2 */
swarm_evo_verdict_t swarm_evo_judge(const swarm_model_state_t *champion,
                                    const swarm_model_state_t *candidate, uint32_t budget_tokens);

/* V3 */
uint64_t swarm_evo_outlier_share(uint64_t total_tokens);
/* Index of the outlier among n agents (index 0, the companion, excluded);
 * returns 0 if n < 2. */
uint32_t swarm_evo_outlier(const swarm_dna_t *dna, uint32_t n);

/* V4: level[i] and fitness[i] for n agents; swaps levels in place. Returns
 * the number of swaps made. */
uint32_t swarm_evo_mobility(uint8_t *level, const uint64_t *fitness, uint32_t n,
                            uint32_t num_levels);

/* V5 */
uint32_t swarm_evo_crc32(const uint8_t *p, uint32_t len);
void swarm_evo_save(const swarm_model_state_t *s, uint8_t out[SWARM_EVO_RECORD]);
bool swarm_evo_restore(const uint8_t in[SWARM_EVO_RECORD], swarm_model_state_t *s);

/* V7: true if the companion stepped (*companion is then the child). No step
 * when companion or donor is NULL. */
bool swarm_evo_companion(swarm_dna_t *companion, uint64_t companion_fit, const swarm_dna_t *donor,
                         uint64_t donor_fit, uint64_t nonce);

#endif /* SWARM_EVOLVE_H */
