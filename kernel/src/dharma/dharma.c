/* dharma.c — Dharma: a Set of Karma, Toroidally Fed Back. See dharma.h. */
#include "dharma.h"
#include "rmag_core.h"
#include "lpres_core.h"
#include "phase_coordinator.h"
#include "upaah.h"

/* ---- Kernel <-> OS-layer bridge ----
 * Delegates to upaah.h's UPAAH/VPAAH/PIR interface engine -- the
 * named, charge-polarity-split implementation of this exact bridge.
 * Kept as dharma_trit_to_phase/dharma_phase_to_trit for API
 * compatibility with existing callers throughout this module. */

l13_phase_t dharma_trit_to_phase(trit_t t) {
    return phase7_bridge(t);
}

trit_t dharma_phase_to_trit(l13_phase_t phase) {
    return phase7_unbridge(phase);
}

/* ---- The set ---- */

void dharma_init(dharma_set_t *d, scheduler_t *sched, oseq_state_t *oseq) {
    d->head = 0;
    d->tail = 0;
    d->count = 0;
    d->sched = sched;
    d->oseq = oseq;
    d->advance_count = 0;
}

bool dharma_emit(dharma_set_t *d, karma_event_t k) {
    if (d->count >= DHARMA_RING_SIZE) {
        /* Coil at capacity: evict the oldest to make room, favoring
         * recent causality over stale backlog. */
        d->head = (d->head + 1) % DHARMA_RING_SIZE;
        d->count--;
    }
    d->ring[d->tail] = k;
    d->tail = (d->tail + 1) % DHARMA_RING_SIZE;
    d->count++;
    return true;
}

uint32_t dharma_set_size(const dharma_set_t *d) {
    return d->count;
}

/* Decides the effect phase for a single task_id from its current
 * kernel-layer state: RMAG magnitude (exact) and LPRES presence
 * (trit_t), the same two quantities sched_tick's coverage gate reads. */
static l13_phase_t decide_phase(uint32_t task_id) {
    rational_t r = rmag_get_quota((ordinal_t)task_id);
    trit_t ell = lpres_get_presence((ordinal_t)task_id);
    double coverage = rational_mag(r) * trit_to_ell(ell);
    if (coverage >= 1.8) return SEPH_MALKUTH; /* same r*l>=1.8 coverage hyperbola as sched_tick */
    return dharma_trit_to_phase(ell);
}

static task_t *find_task(scheduler_t *sched, uint32_t task_id) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (sched->tasks[i].id == task_id && sched->tasks[i].state != TASK_UNUSED) return &sched->tasks[i];
    }
    return 0;
}

/* Applies a resolved phase to sched.c's own task_state_t bookkeeping,
 * so existing consumers (sysmon, etc.) stay consistent with Dharma's
 * decisions without needing to know about L13 phases themselves. */
static void apply_phase_to_task(scheduler_t *sched, uint32_t task_id, l13_phase_t phase) {
    task_t *t = find_task(sched, task_id);
    if (!t) return;
    switch (phase) {
        case SEPH_MALKUTH: t->state = TASK_RUNNING; break;
        case SEPH_YESOD:    t->state = TASK_READY; break;
        case SEPH_HOD:      t->state = TASK_BLOCKED; break;
        case SEPH_NETZACH:  t->state = TASK_SLEEPING; break;
        case VEIL_AIN:      t->state = TASK_TERMINATED; break;
        default: break; /* VEIL_AIN_SOPH / VEIL_AIN_SOPH_AUR: pre-manifest, no task_state_t change */
    }
}

/* Finds the newest unresolved NON_LOCAL karma for `task_id` anywhere
 * in the ring -- deliberately NOT restricted to ring-adjacency or
 * ordinal precedence, since non-locality means no causal path is
 * required between correlated events. */
static karma_event_t *find_correlated(dharma_set_t *d, uint32_t task_id) {
    for (uint32_t i = 0; i < d->count; i++) {
        uint32_t idx = (d->head + d->count - 1 - i) % DHARMA_RING_SIZE; /* newest-first */
        karma_event_t *k = &d->ring[idx];
        if (k->locality == EVENT_NON_LOCAL && k->task_id == task_id && !k->resolved) return k;
    }
    return 0;
}

static uint32_t next_growth_task(dharma_set_t *d, uint64_t mask) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *t = &d->sched->tasks[i];
        if (t->state == TASK_UNUSED) continue;
        if (t->id >= 64) continue; /* mask only covers task ids 0..63 */
        if (!((mask >> t->id) & 1ULL)) return t->id;
    }
    return 0xFFFFFFFFu; /* no growth candidate found */
}

void dharma_advance(dharma_set_t *d) {
    d->advance_count++;
    d->oseq->current_cycle++;
    ordinal_t now = d->oseq->current_cycle;

    uint32_t n = d->count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (d->head + i) % DHARMA_RING_SIZE;
        karma_event_t *k = &d->ring[idx];
        if (k->resolved) continue;

        switch (k->locality) {
        case EVENT_LOCAL: {
            l13_phase_t ph = decide_phase(k->task_id);
            karma_react(k, now, ph);
            apply_phase_to_task(d->sched, k->task_id, ph);
            karma_event_t next = karma_cause(k->task_id, now, ph, k->weight);
            dharma_emit(d, next);
            break;
        }
        case EVENT_NON_LOCAL: {
            l13_phase_t ph = decide_phase(k->task_id);
            karma_react(k, now, ph);
            apply_phase_to_task(d->sched, k->task_id, ph);
            /* Bell/EPR-style correlation: the partner's effect phase
             * is fixed by correlation to THIS phase, not by its own
             * independent RMAG/LPRES read -- no ordinal precedence
             * between the two is required or assumed. */
            karma_event_t *partner = find_correlated(d, k->correlated_task_id);
            l13_phase_t partner_ph = dharma_trit_to_phase(dharma_phase_to_trit(ph)); /* correlated (mirrored via the same trit bridge) */
            if (partner) {
                karma_react(partner, now, partner_ph);
                apply_phase_to_task(d->sched, partner->task_id, partner_ph);
            }
            karma_event_t next = karma_cause_nonlocal(k->task_id, k->correlated_task_id, now, ph, k->weight);
            dharma_emit(d, next);
            break;
        }
        case EVENT_MULTI_LOCAL: {
            l13_phase_t ph = decide_phase(k->task_id);
            karma_react(k, now, ph);
            for (uint32_t j = 0; j < k->num_localities; j++)
                apply_phase_to_task(d->sched, k->localities[j], ph);
            karma_event_t next = karma_cause_multilocal(k->localities, k->num_localities, now, ph, k->weight);
            dharma_emit(d, next);
            break;
        }
        case EVENT_POLYLOCAL: {
            l13_phase_t ph = decide_phase(k->task_id);
            karma_react(k, now, ph);
            uint64_t mask = k->locality_mask;
            for (uint32_t bit = 0; bit < 64; bit++) {
                if ((mask >> bit) & 1ULL) apply_phase_to_task(d->sched, bit, ph);
            }
            /* Spreading wavefront: grow the mask by one more locality
             * per advance, analogous to an expanding field excitation. */
            uint32_t grown = next_growth_task(d, mask);
            if (grown != 0xFFFFFFFFu && grown < 64) mask |= (1ULL << grown);
            karma_event_t next = karma_cause_polylocal(mask, now, ph, k->weight);
            dharma_emit(d, next);
            break;
        }
        case EVENT_OMNILOCAL: {
            l13_phase_t ph = decide_phase(k->task_id);
            karma_react(k, now, ph);
            for (uint32_t s = 0; s < MAX_TASKS; s++) {
                if (d->sched->tasks[s].state != TASK_UNUSED)
                    apply_phase_to_task(d->sched, d->sched->tasks[s].id, ph);
            }
            karma_event_t next = karma_cause_omnilocal(now, ph, k->weight);
            dharma_emit(d, next);
            break;
        }
        }
    }
}

void dharma_on_spawn(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal) {
    /* Left UNRESOLVED deliberately: the first dharma_advance() call
     * reacts it via decide_phase's live RMAG/LPRES read. Pre-resolving
     * here (as an earlier version did) would permanently freeze the
     * task at whatever phase was hardcoded, since dharma_advance
     * skips already-resolved karma -- a spawned task must remain
     * live, not settle once and never move again. */
    karma_event_t k = karma_cause(task_id, at_ordinal, VEIL_AIN_SOPH_AUR, (rational_t){1, 1});
    apply_phase_to_task(d->sched, task_id, VEIL_AIN_SOPH_AUR);
    dharma_emit(d, k);
}

void dharma_on_block(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal) {
    karma_event_t k = karma_cause(task_id, at_ordinal, SEPH_HOD, (rational_t){1, 1});
    apply_phase_to_task(d->sched, task_id, SEPH_HOD);
    dharma_emit(d, k);
}

void dharma_on_sleep(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal) {
    karma_event_t k = karma_cause(task_id, at_ordinal, SEPH_NETZACH, (rational_t){1, 1});
    apply_phase_to_task(d->sched, task_id, SEPH_NETZACH);
    dharma_emit(d, k);
}

void dharma_on_terminate(dharma_set_t *d, uint32_t task_id, ordinal_t at_ordinal) {
    karma_event_t k = karma_cause(task_id, at_ordinal, VEIL_AIN, (rational_t){1, 1});
    karma_react(&k, at_ordinal, VEIL_AIN);
    apply_phase_to_task(d->sched, task_id, VEIL_AIN);
    dharma_emit(d, k);
}

l13_phase_t dharma_task_phase(const dharma_set_t *d, uint32_t task_id) {
    for (uint32_t i = 0; i < d->count; i++) {
        uint32_t idx = (d->head + d->count - 1 - i) % DHARMA_RING_SIZE; /* newest-first */
        const karma_event_t *k = &d->ring[idx];
        if (k->task_id == task_id && k->resolved) return k->effect_phase;
        if (k->locality == EVENT_MULTI_LOCAL) {
            for (uint32_t j = 0; j < k->num_localities; j++)
                if (k->localities[j] == task_id && k->resolved) return k->effect_phase;
        }
    }
    return (l13_phase_t)0;
}
