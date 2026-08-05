/* choice_core.h — Choice-Collapse Scheduler (BIOS Stage 4)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef CHOICE_CORE_H
#define CHOICE_CORE_H

#include "m5_types.h"

/*
 * The Choice-Collapse Scheduler (CHOICE) handles inter-dimensional
 * state transitions and non-deterministic resolution. It is
 * responsible for maintaining the Coverage Hyperbola invariant
 * (r · ℓ ≥ 1.8) and enforcing it at every state transition.
 *
 * This implementation uses the collapse_t structure to represent
 * the superposed state of a system. The collapse_t structure
 * is deterministic, meaning that the same inputs will always
 * produce the same output.
 */

/*
 * choice_handoff() — collapses the current state vector and
 * advances the system to the next deterministic state.
 */
void choice_handoff(void);
collapse_t choice_get_state(void);
void choice_set_state(const collapse_t *state);

#endif