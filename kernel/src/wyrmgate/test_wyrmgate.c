/* test_wyrmgate.c — the six-fold judgment, a composed application.
 *
 * This test IS the demonstration that the modules compose: it links rmag +
 * lpres + chiglet + surplus together and drives them through one gate. Each
 * case pins a Tri-Space outcome and the precedence between laws (S-) and
 * unsettled conditions (S0).
 */
#include <stdio.h>
#include <string.h>
#include "wyrmgate.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* A clean, committable event: ordered, value-conserved (+5 and -5), attested
 * TRUE, with independent evidence, a live route, healthy phases. */
static void good_event(wyrm_event_t *e) {
    memset(e, 0, sizeof(*e));
    e->ordinal = 10; e->parent_ordinal = 0;
    e->delta[0] = rmag_rational_from_frac(5, 1, false);   /* +5 to A */
    e->delta[1] = rmag_rational_from_frac(5, 1, true);    /* -5 from B */
    e->n_deltas = 2;
    e->evidence = LPRES_STATE_TRUE;
    /* two independent (orthogonal) evidence directions */
    e->ev[0][0] = SR_ONE;
    e->ev[1][1] = SR_ONE;
    e->n_ev = 2;
    e->r_min = SR_FROM_FLOAT(1.5);
    e->route_available = true;
    e->phases_healthy = true;
}

int main(void) {
    printf("=== Wyrmgate: the six-fold Tri-Space judgment (composed app) ===\n");
    wyrm_event_t e;

    /* ---- the clean pass ---- */
    good_event(&e);
    wyrm_result_t r = wyrm_judge(&e);
    printf("       clean event -> %s (%s) R=%.2f\n",
           wyrm_verdict_name(r.verdict), wyrm_reason_name(r.reason),
           (double)r.R / (double)SR_ONE);
    CHECK(r.verdict == WYRM_COMMIT && r.reason == WYRM_OK,
          "a lawful, settled, independently-evidenced event COMMITS (S+)");
    CHECK(r.value_conserved, "and RMAG confirms value conservation");

    /* ---- RMAG: value not conserved is a LAW violation -> S- REJECT ---- */
    good_event(&e);
    e.delta[1] = rmag_rational_from_frac(4, 1, true);     /* -4, not -5 */
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_REJECT && r.reason == WYRM_R_VALUE_NOT_CONSERVED,
          "value that does not sum to zero is REJECTED (S-), not deferred");

    /* exact rational conservation: 1/3 + 1/3 + 1/3 - 1 == 0 */
    good_event(&e);
    e.delta[0] = rmag_rational_from_frac(1, 3, false);
    e.delta[1] = rmag_rational_from_frac(1, 3, false);
    e.delta[2] = rmag_rational_from_frac(1, 3, false);
    e.delta[3] = rmag_rational_from_frac(1, 1, true);
    e.n_deltas = 4;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_COMMIT,
          "exact rationals conserve: 1/3+1/3+1/3-1 == 0 commits (no float drift)");

    /* ---- LPRES: the four values map to the three spaces ---- */
    good_event(&e); e.evidence = LPRES_STATE_FALSE;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_REJECT && r.reason == WYRM_R_EVIDENCE_FALSE,
          "disproving evidence REJECTS (S-)");

    good_event(&e); e.evidence = LPRES_STATE_BOTH;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_EVIDENCE_CONTRADICT,
          "CONTRADICTORY evidence DEFERS (S0) — never silently collapsed to yes/no");

    good_event(&e); e.evidence = LPRES_STATE_NEITHER;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_EVIDENCE_ABSENT,
          "ABSENT evidence DEFERS (S0) — insufficient, not a decision");

    /* ---- Chiglet: TRUE-but-not-independent evidence DEFERS ---- */
    good_event(&e);
    e.ev[1][0] = SR_ONE; e.ev[1][1] = SR_ZERO;   /* second vector == first */
    r = wyrm_judge(&e);
    printf("       echoed evidence -> %s (%s) R=%.2f distinct=%u\n",
           wyrm_verdict_name(r.verdict), wyrm_reason_name(r.reason),
           (double)r.R / (double)SR_ONE, r.distinct);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_NOT_INDEPENDENT,
          "TRUE but ECHOED evidence DEFERS (S0) — confidence cannot be manufactured");
    CHECK(r.distinct == 1, "the echo collapses to one distinct direction");

    /* ---- OSEQ: out of causal order DEFERS ---- */
    good_event(&e);
    e.parent_ordinal = 20; e.ordinal = 10; e.parent_committed = true;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_OUT_OF_ORDER,
          "an event before its parent DEFERS (S0)");
    good_event(&e);
    e.parent_ordinal = 5; e.ordinal = 10; e.parent_committed = false;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_OUT_OF_ORDER,
          "an event whose parent has not committed DEFERS (S0)");

    /* ---- routing + phase health DEFER ---- */
    good_event(&e); e.route_available = false;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_NO_ROUTE, "no route DEFERS (S0)");
    good_event(&e); e.phases_healthy = false;
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_DEFER && r.reason == WYRM_R_PHASE_UNHEALTHY,
          "an unhealthy phase DEFERS (S0)");

    /* ---- precedence: a violated LAW (S-) outranks an unsettled condition ---- */
    good_event(&e);
    e.delta[1] = rmag_rational_from_frac(99, 1, true);   /* value violation */
    e.route_available = false;                            /* also unsettled */
    r = wyrm_judge(&e);
    CHECK(r.verdict == WYRM_REJECT && r.reason == WYRM_R_VALUE_NOT_CONSERVED,
          "a law-violation (S-) is reported over a merely-unsettled condition (S0)");

    /* ---- determinism / replayability ---- */
    good_event(&e);
    wyrm_result_t a = wyrm_judge(&e), b = wyrm_judge(&e);
    CHECK(a.verdict == b.verdict && a.reason == b.reason && a.R == b.R,
          "the judgment is deterministic and replayable");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
