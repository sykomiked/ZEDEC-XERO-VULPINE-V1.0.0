/* bodhi.h — Bodhi: a Transcendent Superposition Beyond Karma or Dharma
 *
 * Neither an element (karma_event_t) nor the set itself (dharma_set_t),
 * bodhi_state_t is a closure computed OVER the whole Dharma set: the
 * cyc13_t sum, across every karma currently in the set, of that
 * karma's resolved effect phase scaled by its RMAG weight.
 *
 * sephirot.c documents that phase 13 (Ain Soph Aur) maps to zeta^0 = 1,
 * the multiplicative identity "from which the other 12 phases... are
 * generated." Bodhi's transcendence is therefore not a metaphor left
 * unformalized: `transcendent` becomes true precisely when the
 * superposition's coefficient at index 0 dominates every other
 * coefficient -- the aggregate state has converged to pure,
 * undifferentiated source, beyond the particular phase of any single
 * karma and beyond the multiplicity of the set as a whole.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef BODHI_H
#define BODHI_H

#include "m5_types.h"
#include "sephirot.h"
#include "dharma.h"

typedef struct bodhi_state {
    cyc13_t superposition;  /* sum over the set of weight * cyc13_from_phase(effect_phase) */
    rational_t coherence;   /* magnitude of the index-0 (source) component */
    rational_t dispersion;  /* summed magnitude of every OTHER component */
    bool transcendent;      /* coherence > dispersion: the set as a whole has converged to source */
} bodhi_state_t;

/* Observes the current Dharma set and computes its Bodhi state.
 * Read-only: does not mutate the set, any karma in it, or advance
 * any ordinal -- observation is deliberately non-interfering. */
bodhi_state_t bodhi_observe(const dharma_set_t *d);

#endif
