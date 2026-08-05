/* wyrmgate.c — the six-fold judgment. See wyrmgate.h.
 *
 * The whole file is composition: each phase is an existing, tested module,
 * and Wyrmgate only sequences their verdicts under a fixed precedence. The
 * precedence is deliberate — a violated LAW (S-) outranks a merely-unsettled
 * condition (S0), and both outrank a commit. */
#include "wyrmgate.h"

static wyrm_result_t verdict(wyrm_verdict_t v, wyrm_reason_t r,
                             surplus_real_t R, uint32_t distinct, bool conserved) {
    wyrm_result_t out;
    out.verdict = v; out.reason = r; out.R = R;
    out.distinct = distinct; out.value_conserved = conserved;
    return out;
}

wyrm_result_t wyrm_judge(const wyrm_event_t *e) {
    if (!e) return verdict(WYRM_DEFER, WYRM_R_PHASE_UNHEALTHY, SR_ZERO, 0, false);

    /* --- Phase 1: OSEQ. An event may not be judged before its cause. --- */
    if (e->parent_ordinal != 0 &&
        (!e->parent_committed || e->ordinal <= e->parent_ordinal))
        return verdict(WYRM_DEFER, WYRM_R_OUT_OF_ORDER, SR_ZERO, 0, false);

    /* --- Phase 2: RMAG. Value must conserve — the rational deltas sum to
     * exactly zero. This is a LAW; violating it is S- (reject), not S0. --- */
    rmag_rational_t sum = rmag_rational_from_uint(0);
    uint32_t nd = (e->n_deltas <= WYRM_MAX_DELTAS) ? e->n_deltas : WYRM_MAX_DELTAS;
    for (uint32_t i = 0; i < nd; i++)
        sum = rmag_rational_add(sum, e->delta[i]);
    bool conserved = rmag_rational_is_zero(rmag_rational_reduce(sum));
    if (!conserved)
        return verdict(WYRM_REJECT, WYRM_R_VALUE_NOT_CONSERVED, SR_ZERO, 0, false);

    /* --- Phase 3: LPRES. Resolve the four-valued evidence. A contradiction
     * is held (S0), never collapsed; disproof is a law-violation (S-);
     * absence is unsettled (S0). --- */
    switch (e->evidence) {
    case LPRES_STATE_BOTH:
        return verdict(WYRM_DEFER, WYRM_R_EVIDENCE_CONTRADICT, SR_ZERO, 0, conserved);
    case LPRES_STATE_FALSE:
        return verdict(WYRM_REJECT, WYRM_R_EVIDENCE_FALSE, SR_ZERO, 0, conserved);
    case LPRES_STATE_NEITHER:
        return verdict(WYRM_DEFER, WYRM_R_EVIDENCE_ABSENT, SR_ZERO, 0, conserved);
    case LPRES_STATE_TRUE:
    default:
        break;                 /* attested present — continue */
    }

    /* --- Phase 4: Chiglet. TRUE evidence must also be INDEPENDENT: a chorus
     * of echoes (low R) cannot manufacture confidence. S0 if it is not. --- */
    uint32_t distinct = 0;
    surplus_real_t R = SR_ZERO;
    if (e->n_ev > 0) {
        R = chg_effective_experts(e->ev, e->n_ev, CHG_DIM, &distinct);
        if (R < e->r_min)
            return verdict(WYRM_DEFER, WYRM_R_NOT_INDEPENDENT, R, distinct, conserved);
    } else if (e->r_min > SR_ZERO) {
        /* independence was required but no evidence vectors were supplied */
        return verdict(WYRM_DEFER, WYRM_R_NOT_INDEPENDENT, SR_ZERO, 0, conserved);
    }

    /* --- Phase 5: routing + phase health (PHASE_COORD). Without a live
     * route or healthy phases the event cannot be applied — hold it. --- */
    if (!e->route_available)
        return verdict(WYRM_DEFER, WYRM_R_NO_ROUTE, R, distinct, conserved);
    if (!e->phases_healthy)
        return verdict(WYRM_DEFER, WYRM_R_PHASE_UNHEALTHY, R, distinct, conserved);

    /* --- Phase 6: the coordinator commits. Every law satisfied, everything
     * settled: S+. --- */
    return verdict(WYRM_COMMIT, WYRM_OK, R, distinct, conserved);
}

const char *wyrm_verdict_name(wyrm_verdict_t v) {
    switch (v) {
    case WYRM_COMMIT: return "S+ COMMIT";
    case WYRM_REJECT: return "S- REJECT";
    case WYRM_DEFER:  return "S0 DEFER";
    default:          return "?";
    }
}

const char *wyrm_reason_name(wyrm_reason_t r) {
    switch (r) {
    case WYRM_OK:                   return "ok";
    case WYRM_R_OUT_OF_ORDER:       return "out-of-order (parent unjudged)";
    case WYRM_R_VALUE_NOT_CONSERVED:return "value not conserved";
    case WYRM_R_EVIDENCE_FALSE:     return "evidence disproves";
    case WYRM_R_EVIDENCE_CONTRADICT:return "evidence contradictory";
    case WYRM_R_EVIDENCE_ABSENT:    return "evidence absent";
    case WYRM_R_NOT_INDEPENDENT:    return "evidence not independent";
    case WYRM_R_NO_ROUTE:           return "no live route";
    case WYRM_R_PHASE_UNHEALTHY:    return "phase unhealthy";
    default:                        return "?";
    }
}
