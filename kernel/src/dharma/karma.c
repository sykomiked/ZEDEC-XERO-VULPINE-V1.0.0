/* karma.c — Karma: atomic causal event implementation. */
#include "karma.h"

ordinal_t event_coord_from_legacy_clock(uint64_t wall_clock_ticks) {
    /* One-way lift only: a legacy tick count becomes an ordinal
     * position. This does NOT make Dharma depend on the clock -- it
     * simply lets clock-driven input be read as a point in event
     * space, same as any other ordinal-tagged cause. */
    return (ordinal_t)wall_clock_ticks;
}

static karma_event_t karma_base(ordinal_t cause_ordinal, l13_phase_t cause_phase, rational_t weight) {
    karma_event_t k;
    k.cause_ordinal = cause_ordinal;
    k.effect_ordinal = 0;
    k.task_id = 0;
    k.cause_phase = cause_phase;
    k.effect_phase = (l13_phase_t)0;
    k.weight = weight;
    k.resolved = false;
    k.locality = EVENT_LOCAL;
    k.coord.ordinal = cause_ordinal;
    k.coord.x = (rational_t){0, 1};
    k.coord.y = (rational_t){0, 1};
    k.coord.z = (rational_t){0, 1};
    k.correlated_task_id = 0;
    for (uint32_t i = 0; i < KARMA_MAX_MULTI_LOCAL; i++) k.localities[i] = 0;
    k.num_localities = 0;
    k.locality_mask = 0;
    return k;
}

karma_event_t karma_cause(uint32_t task_id, ordinal_t cause_ordinal, l13_phase_t cause_phase,
                           rational_t weight) {
    karma_event_t k = karma_base(cause_ordinal, cause_phase, weight);
    k.task_id = task_id;
    k.locality = EVENT_LOCAL;
    k.coord.x = (rational_t){(int64_t)task_id, 1};
    return k;
}

karma_event_t karma_cause_nonlocal(uint32_t task_id, uint32_t correlated_task_id,
                                    ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                    rational_t weight) {
    karma_event_t k = karma_base(cause_ordinal, cause_phase, weight);
    k.task_id = task_id;
    k.locality = EVENT_NON_LOCAL;
    k.correlated_task_id = correlated_task_id;
    k.coord.x = (rational_t){(int64_t)task_id, 1};
    return k;
}

karma_event_t karma_cause_multilocal(const uint32_t *task_ids, uint32_t count,
                                      ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                      rational_t weight) {
    karma_event_t k = karma_base(cause_ordinal, cause_phase, weight);
    k.locality = EVENT_MULTI_LOCAL;
    uint32_t n = count > KARMA_MAX_MULTI_LOCAL ? KARMA_MAX_MULTI_LOCAL : count;
    for (uint32_t i = 0; i < n; i++) k.localities[i] = task_ids[i];
    k.num_localities = n;
    k.task_id = n > 0 ? task_ids[0] : 0;
    return k;
}

karma_event_t karma_cause_polylocal(uint64_t initial_mask, ordinal_t cause_ordinal,
                                     l13_phase_t cause_phase, rational_t weight) {
    karma_event_t k = karma_base(cause_ordinal, cause_phase, weight);
    k.locality = EVENT_POLYLOCAL;
    k.locality_mask = initial_mask;
    return k;
}

karma_event_t karma_cause_omnilocal(ordinal_t cause_ordinal, l13_phase_t cause_phase,
                                     rational_t weight) {
    karma_event_t k = karma_base(cause_ordinal, cause_phase, weight);
    k.locality = EVENT_OMNILOCAL;
    return k;
}

rational_t karma_entanglement_degree(const karma_event_t *a, const karma_event_t *b) {
    rational_t zero = {0, 1};
    bool linked = (a->locality == EVENT_NON_LOCAL && b->locality == EVENT_NON_LOCAL &&
                   a->correlated_task_id == b->task_id && b->correlated_task_id == a->task_id);
    if (!linked) return zero; /* not entangled: a definite, present zero -- never null/missing */

    rational_t wa = a->weight, wb = b->weight;
    rational_t sum = rmag_add_quotas(wa, wb);
    if (sum.num == 0) return zero;
    rational_t lo = (rational_mag(wa) <= rational_mag(wb)) ? wa : wb;
    rational_t two_lo = rmag_mul_quotas(lo, (rational_t){2, 1});
    return rmag_div_quotas(two_lo, sum);
}

bool karma_is_entangled(const karma_event_t *a, const karma_event_t *b) {
    rational_t d = karma_entanglement_degree(a, b);
    return d.num != 0;
}

trit_t karma_react(karma_event_t *k, ordinal_t effect_ordinal, l13_phase_t effect_phase) {
    if (k->resolved) {
        /* A karma event reacting twice is not a crash-worthy error --
         * it is a genuinely contradictory state (two effects claiming
         * the same cause). Report it as such rather than overwriting
         * silently or failing hard. */
        return TRIT_GLUT_MINUS;
    }
    k->effect_ordinal = effect_ordinal;
    k->effect_phase = effect_phase;
    k->resolved = true;
    return TRIT_TRUE;
}
