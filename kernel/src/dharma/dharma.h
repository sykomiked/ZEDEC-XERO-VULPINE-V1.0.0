/* dharma.h — Dharma: a Set of Karma, Toroidally Fed Back (an "ABHA coil")
 *
 * Set theory, taken literally: Dharma is not a scheduler that "owns"
 * tasks -- it is the SET of karma_event_t currently in play. Its
 * cardinality |D| is a real, queryable number. Karma is the element;
 * Dharma is the set of elements.
 *
 * The set is realized as a ring (a toroidal buffer): a karma event
 * that reacts (see karma_react) does not drain out of the set --
 * dharma_advance() re-emits its reaction as a NEW karma event back
 * into the SAME ring. The loop closes on itself, coil-wise, rather
 * than flowing linearly from an input end to an output end. This is
 * the literal ABHA-coil structure: cause feeds effect, effect feeds
 * back as the next cause, wound around a fixed-capacity torus.
 *
 * ---- The bridge between kernel logic and OS-layer logic ----
 *
 * m5_types.h's trit_t is the kernel's six-valued logic (per
 * sephirot.h's own description of L13 as "sitting above the existing
 * L6 kernel"). Dharma's karma events carry L13 phases 7-13 (Netzach
 * through Ain Soph Aur) -- the OS layer above that kernel logic.
 *
 * Phases 9-13 bridge directly to the 5 semantically distinct trit_t
 * states (TRIT_GLUT is documented in m5_types.h as a legacy alias of
 * TRIT_GLUT_NEUTRAL, so only 5 of the 6 enum literals are distinct).
 * Phases 7 (Netzach) and 8 (Hod) are OS-layer-NATIVE concepts --
 * "persisting through time" (sleep) and "structured communication"
 * (blocked-on-IPC) are scheduling concepts that do not exist at the
 * raw logic-gate kernel level, so they have no trit_t equivalent.
 *
 *   Phase 13 (Ain Soph Aur, source of emanation) <-> TRIT_GLUT_PLUS  : task spawn
 *   Phase 12 (Ain Soph, unbounded/unresolved)    <-> TRIT_GLUT_MINUS: unused slot
 *   Phase 11 (Ain, no-thing)                     <-> TRIT_FALSE     : terminated
 *   Phase 10 (Malkuth, grounded/manifest)        <-> TRIT_TRUE      : running
 *   Phase 9  (Yesod, staged/foundation)          <-> TRIT_GLUT_NEUTRAL: ready
 *   Phase 8  (Hod, structured communication)     <-> (no equivalent): blocked/IPC
 *   Phase 7  (Netzach, persistence/endurance)    <-> (no equivalent): sleeping
 *
 * A karma event's effect reaching Malkuth (running) is the literal
 * bridge point: dharma_advance() drives phase_coordinator_tick() (the
 * kernel's phase-6/Tiferet-anchored tick) at that moment, so the OS
 * layer's decision to grant CPU manifests as an actual kernel tick.
 *
 * See bodhi.h for the transcendent observation OVER this set -- a
 * state beyond any individual karma AND beyond Dharma's set structure
 * itself.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef DHARMA_H
#define DHARMA_H

#include "m5_types.h"
#include "sephirot.h"
#include "karma.h"
#include "sched.h"
#include "oseq_core.h"

/* ---- Kernel <-> OS-layer bridge ---- */

l13_phase_t dharma_trit_to_phase(trit_t t);
/* Returns TRIT_FALSE for phases 7/8 (Netzach/Hod), which have no
 * kernel-trit equivalent by design -- not an error sentinel. */
trit_t dharma_phase_to_trit(l13_phase_t phase);

/* ---- The set (ring/torus) of karma ---- */

#define DHARMA_RING_SIZE 128

typedef struct dharma_set {
    karma_event_t ring[DHARMA_RING_SIZE];
    uint32_t head, tail, count;  /* toroidal ring indices; count == |D| */
    scheduler_t *sched;          /* underlying task table (owned by caller) */
    oseq_state_t *oseq;          /* the OSEQ event sequence driving this set */
    uint32_t advance_count;      /* number of dharma_advance() calls so far */
} dharma_set_t;

void dharma_init(dharma_set_t *d, scheduler_t *sched, oseq_state_t *oseq);

/* Inserts a new karma event into the set. If the ring is at capacity,
 * the OLDEST karma is evicted to make room (a live coil favors recent
 * causality over stale backlog, same policy as gui/event.c's queue). */
bool dharma_emit(dharma_set_t *d, karma_event_t k);

/* |D|: the literal cardinality of the set. */
uint32_t dharma_set_size(const dharma_set_t *d);

/* THE nonlinear event-sequence scheduler tick. Call this whenever a
 * new OSEQ ordinal event occurs -- NOT on a fixed timer interval.
 * Reacts every unresolved karma in the set (driven by RMAG magnitude +
 * LPRES presence for the karma's task_id), and re-emits each
 * reaction's effect as a NEW karma event back into the SAME ring --
 * the toroidal feedback. Any karma whose effect_phase collapses to
 * Malkuth (10) additionally drives phase_coordinator_tick(), bridging
 * into the kernel layer, and updates the corresponding task's
 * task_state_t to keep sched.c's own bookkeeping consistent. */
void dharma_advance(dharma_set_t *d);

/* Emits the Ain-Soph-Aur (13) spawn karma for a freshly created task
 * id, immediately followed by its first reaction into Yesod (9). */
void dharma_on_spawn(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal);
void dharma_on_block(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal);     /* -> Hod (8) */
void dharma_on_sleep(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal);     /* -> Netzach (7) */
void dharma_on_terminate(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal); /* -> Ain (11) */

/* Most recent resolved effect_phase for this task_id found in the
 * set, scanning newest-first; 0 if no karma for this task exists. */
l13_phase_t dharma_task_phase(const dharma_set_t *d, uint32_t task_id);

#endif
