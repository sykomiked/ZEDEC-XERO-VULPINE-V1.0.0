/* karma.h — Karma: an atomic causal event.
 *
 * A karma_event_t is a single trigger (cause) paired with its
 * reaction (effect). It is the element type of the Dharma set --
 * set-theoretically, Dharma is a set of karma_event_t, nothing more,
 * nothing less. Karma itself carries no notion of "many"; that
 * belongs to Dharma.
 *
 * ---- Event space is >= 4-dimensional, not a 1D clock ----
 *
 * event_coord_t is the position of a karma event in event-space:
 * `ordinal` is a causal-ORDER component (from OSEQ) -- a sequence
 * position, not a duration or wall-clock timestamp -- and x/y/z are
 * exact-rational locality coordinates in the OS's resource/task
 * space. Time, as a single deflated scalar dimension, is never the
 * native representation here; event_coord_from_legacy_clock() exists
 * ONLY so legacy, clock-driven systems can be read as input -- Dharma
 * never derives its own sequencing from a clock, only from OSEQ's
 * ordinal advancement, which does not need one.
 *
 * ---- Event locality, grounded in physics ----
 *
 * LOCAL: a single point in event-space -- ordinary relativistic
 * causality, affecting exactly one task_id.
 *
 * NON_LOCAL: genuine quantum non-locality (Bell/EPR-style
 * correlation). Two karma events are coupled WITHOUT any causal
 * ordinal chain between them -- no signal, no ordinal-precedence
 * requirement, only a fixed correlation rule evaluated at observation
 * time (dharma_advance looks up the partner ANYWHERE in the set,
 * regardless of ring position). This respects the no-signaling
 * theorem: correlation fixes RELATIVE phase, it never transmits an
 * independently choosable payload.
 *
 * MULTI_LOCAL: a fixed, small, enumerable set of localities affected
 * simultaneously -- analogous to a delocalized wavefunction with
 * amplitude at several definite positions (e.g. an electron shared
 * across a few bonding sites).
 *
 * POLYLOCAL: like MULTI_LOCAL, but the set of affected localities is
 * NOT fixed -- it grows across successive dharma_advance() calls,
 * analogous to a spreading wavefront / expanding field excitation.
 *
 * OMNILOCAL: affects every locality uniformly and identically --
 * analogous to a global (k=0) field mode / vacuum symmetry, which by
 * definition has the same amplitude everywhere.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef KARMA_H
#define KARMA_H

#include "m5_types.h"
#include "sephirot.h"
#include "rmag_core.h"

typedef enum {
    EVENT_LOCAL = 0,
    EVENT_NON_LOCAL,
    EVENT_MULTI_LOCAL,
    EVENT_POLYLOCAL,
    EVENT_OMNILOCAL,
} event_locality_t;

#define KARMA_MAX_MULTI_LOCAL 4

typedef struct event_coord {
    ordinal_t ordinal;      /* causal-order component, NOT a duration or timestamp */
    rational_t x, y, z;     /* exact-rational locality coordinates */
} event_coord_t;

/* Legacy-clock interop ONLY -- see file header. One-way: this lifts a
 * clock tick INTO event-space; nothing in Dharma derives FROM it. */
ordinal_t event_coord_from_legacy_clock(uint64_t wall_clock_ticks);

typedef struct karma_event {
    ordinal_t cause_ordinal;    /* OSEQ ordinal of the triggering event */
    ordinal_t effect_ordinal;   /* OSEQ ordinal of the reactive event; 0 = not yet reacted */
    uint32_t task_id;           /* primary locality -- meaning depends on `locality` below */
    l13_phase_t cause_phase;    /* OS-layer phase (7-13) at the moment of the cause */
    l13_phase_t effect_phase;   /* OS-layer phase resulting from the reaction; 0 = unresolved */
    rational_t weight;          /* RMAG-exact causal weight (magnitude of this karma) */
    bool resolved;              /* has the reactive event actually occurred? */

    event_locality_t locality;
    event_coord_t coord;                          /* 4D+ position of the cause */
    uint32_t correlated_task_id;                   /* NON_LOCAL: the EPR-like partner */
    uint32_t localities[KARMA_MAX_MULTI_LOCAL];     /* MULTI_LOCAL: fixed set */
    uint32_t num_localities;                        /* MULTI_LOCAL: count in use */
    uint64_t locality_mask;                         /* POLYLOCAL: growable bitmask, up to 64 task ids */
} karma_event_t;

/* LOCAL karma (the common case). */
karma_event_t karma_cause(uint32_t task_id, ordinal_t cause_ordinal, l13_phase_t cause_phase,
                           rational_t weight);

/* NON_LOCAL: task_id and correlated_task_id are Bell/EPR-coupled --
 * neither one's karma needs to precede the other's in the ring. */
karma_event_t karma_cause_nonlocal(uint32_t task_id, uint32_t correlated_task_id,
                                    ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                    rational_t weight);

/* MULTI_LOCAL: a fixed set of up to KARMA_MAX_MULTI_LOCAL task ids,
 * all affected simultaneously by the same cause. */
karma_event_t karma_cause_multilocal(const uint32_t *task_ids, uint32_t count,
                                      ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                      rational_t weight);

/* POLYLOCAL: starts from an initial bitmask of affected task ids;
 * dharma_advance() grows this mask over successive calls. */
karma_event_t karma_cause_polylocal(uint64_t initial_mask, ordinal_t cause_ordinal,
                                     l13_phase_t cause_phase, rational_t weight);

/* OMNILOCAL: affects every task uniformly; no explicit locality list. */
karma_event_t karma_cause_omnilocal(ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                     rational_t weight);

/* Reacts a karma event: records the effect ordinal/phase and marks it
 * resolved. Returns TRIT_TRUE if this is a clean first reaction,
 * TRIT_GLUT_MINUS if the event was already resolved (a genuinely
 * contradictory double-reaction, reported rather than silently
 * ignored or hard-failed). */
trit_t karma_react(karma_event_t *k, ordinal_t effect_ordinal, l13_phase_t effect_phase);

/* ---- Entanglement ----
 * Entanglement is a property of a DECLARED NON_LOCAL relation, never
 * an inferred one: two karma events are entangled only if both are
 * EVENT_NON_LOCAL and mutually reference each other's task_id via
 * correlated_task_id. The degree is a concurrence-like overlap of
 * their causal weights, 2*min(wa,wb)/(wa+wb) -- exactly 1 (maximal)
 * when the weights match, degrading toward 0 as they diverge, the
 * same qualitative shape as two-level entanglement entropy peaking at
 * balanced amplitudes and vanishing as one dominates. A non-entangled
 * pair returns an exact rational zero: a definite, present value,
 * never a null/missing one. */
rational_t karma_entanglement_degree(const karma_event_t *a, const karma_event_t *b);
bool karma_is_entangled(const karma_event_t *a, const karma_event_t *b);

#endif
