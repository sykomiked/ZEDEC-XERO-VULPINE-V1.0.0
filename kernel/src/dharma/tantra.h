/* tantra.h — Tantra: the Dimension of Integration (Runtime Protocol)
 *
 * Tantra: Tan (to weave/expand) + Tra (instrument) -- "the system
 * that weaves reality together." Tantra is the L13 OS Hypervisor
 * itself: the runtime engine that takes a Mantra signal (mantra.h),
 * contains it within a Yantra hardware topology (yantra.h), and
 * weaves it into the Dharma set using Karma's causal reactions and
 * Bodhi's transcendent observation as its governing laws. Without a
 * Mantra, a Yantra is static artwork; without a Yantra, a Mantra has
 * no spatial focus; without Tantra, neither runs.
 *
 * ---- The Paradox Trap (dual-rail 1-1-0) ----
 *
 * Dual-rail logic represents one signal on two wires: (1,0) = true,
 * (0,1) = false, (0,0) = unset, and (1,1) = both rails asserted
 * simultaneously -- a genuine hardware contradiction, the same shape
 * as a GLUT trit. tantra_paradox_trap reads Bodhi's coherence
 * (the "true" rail) and dispersion (the "false" rail): when BOTH are
 * asserted (nonzero) and coherence does not dominate (not
 * transcendent), that is exactly the (1,1) code. The trap forces a
 * single deterministic output (TANTRA_PARADOX_TRAPPED) rather than
 * letting a both-true-and-false condition propagate further --
 * "110": rail A high, rail B high, output rail forced low/clear.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef TANTRA_H
#define TANTRA_H

#include "m5_types.h"
#include "sephirot.h"
#include "karma.h"
#include "dharma.h"
#include "bodhi.h"
#include "mantra.h"
#include "yantra.h"

typedef struct tantra_engine {
    dharma_set_t dharma;
    scheduler_t *sched;
    oseq_state_t *oseq;
} tantra_engine_t;

void tantra_init(tantra_engine_t *t, scheduler_t *sched, oseq_state_t *oseq);

/* Weaves a raw seed (Mantra) into a Yantra container of the given
 * dimension, then emits it as a karma of the given locality into the
 * Dharma set, weighted by the mantra's own amplitude. */
void tantra_weave(tantra_engine_t *t, uint32_t task_id, const uint8_t *seed, uint32_t len,
                   l13_dim_t container_dim, event_locality_t locality);

/* One integration cycle: advances the Dharma set, observes Bodhi over
 * the result, and applies the Paradox Trap. */
typedef enum { TANTRA_CLEAR = 0, TANTRA_PARADOX_TRAPPED = 1 } tantra_result_t;
tantra_result_t tantra_run(tantra_engine_t *t);

tantra_result_t tantra_paradox_trap(const bodhi_state_t *b);

#endif
