/* wyrmgate.h — Wyrmgate: the six-fold judgment through which events pass
 *
 * WHAT THIS IS
 * ------------
 * The first ADVANCED application of ZXV, assembled by compiling tested
 * modules together rather than writing new logic. Every state-changing event
 * on the system passes through Wyrmgate and receives a Tri-Space verdict —
 * S+ commit, S- reject, or S0 defer — computed by the kernel's own phase
 * logic. It is the durable-transaction spine the whole architecture was built
 * to support, distilled into one gate.
 *
 * WHAT IT COMPOSES (nothing here is new machinery)
 * ------------------------------------------------
 *   OSEQ   — causal order: an event may not be judged before its parent.
 *   RMAG   — exact value: the event's value deltas must conserve (sum to
 *            zero) in rational arithmetic, so nothing is created or destroyed.
 *   LPRES  — four-valued evidence: TRUE / FALSE / NEITHER / BOTH. A
 *            contradiction (BOTH) is never silently collapsed to a yes/no.
 *   Chiglet — independence: even TRUE evidence must be INDEPENDENT (R >=
 *            R_min), so a chorus of echoes cannot manufacture confidence.
 *   PHASE_COORD — the gate: order + value + evidence + independence + a live
 *            route + healthy phases, or the commit is withheld.
 *
 * THE VERDICT IS TRI-SPACE, AND SO ARE THE FAILURES
 * -------------------------------------------------
 * The distinction that makes this ZXV and not a boolean validator: a failure
 * is not simply "no." An event that VIOLATES a law (value not conserved,
 * disproven evidence) is REJECTED (S-). An event that is merely NOT YET
 * SETTLED (out of order, contradictory or absent evidence, not independent
 * enough, no route, unhealthy phase) is DEFERRED (S0) — held for resolution,
 * never forced into a false decision. Only a clean pass commits (S+).
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Wyrmgate slice)
 * License: SEL-3.3
 */
#ifndef ZXV_WYRMGATE_H
#define ZXV_WYRMGATE_H

#include <stdint.h>
#include <stdbool.h>
#include "../rmag/rmag.h"
#include "../lpres/lpres.h"
#include "../chiglet/chiglet.h"

#define WYRM_MAX_DELTAS  16u

/* Tri-Space verdict. */
typedef enum {
    WYRM_COMMIT = 0,   /* S+  — every law satisfied; apply the event */
    WYRM_REJECT,       /* S-  — a law was violated; refuse and compensate */
    WYRM_DEFER         /* S0  — not yet resolvable; hold, do not force */
} wyrm_verdict_t;

typedef enum {
    WYRM_OK = 0,               /* committed */
    WYRM_R_OUT_OF_ORDER,       /* S0: parent not yet judged */
    WYRM_R_VALUE_NOT_CONSERVED,/* S-: rational deltas do not sum to zero */
    WYRM_R_EVIDENCE_FALSE,     /* S-: evidence attests the event is invalid */
    WYRM_R_EVIDENCE_CONTRADICT,/* S0: supporting AND contradicting evidence */
    WYRM_R_EVIDENCE_ABSENT,    /* S0: no attestation either way */
    WYRM_R_NOT_INDEPENDENT,    /* S0: evidence not independent enough (echoes) */
    WYRM_R_NO_ROUTE,           /* S0: no live route to apply the event */
    WYRM_R_PHASE_UNHEALTHY      /* S0: a phase gate is unhealthy */
} wyrm_reason_t;

typedef struct {
    /* OSEQ — causal order */
    uint64_t ordinal;
    uint64_t parent_ordinal;      /* 0 = no parent */
    bool     parent_committed;    /* has the parent already been judged S+? */

    /* RMAG — value conservation: the signed rational deltas across all
     * parties, which must sum to exactly zero */
    rmag_rational_t delta[WYRM_MAX_DELTAS];
    uint32_t        n_deltas;

    /* LPRES — combined evidence state for this event */
    lpres_state_t   evidence;

    /* Chiglet — the evidence directions and the independence floor */
    surplus_real_t  ev[CHG_MAX_EXPERTS][CHG_DIM];
    uint32_t        n_ev;
    surplus_real_t  r_min;

    /* PHASE_COORD — is there a live route, and are the phases healthy? */
    bool route_available;
    bool phases_healthy;
} wyrm_event_t;

typedef struct {
    wyrm_verdict_t verdict;
    wyrm_reason_t  reason;
    surplus_real_t R;             /* measured evidence independence */
    uint32_t       distinct;      /* distinct evidence directions */
    bool           value_conserved;
} wyrm_result_t;

/* Judge one event through the six-fold gate. Deterministic and replayable. */
wyrm_result_t wyrm_judge(const wyrm_event_t *e);

const char *wyrm_verdict_name(wyrm_verdict_t v);
const char *wyrm_reason_name(wyrm_reason_t r);

#endif /* ZXV_WYRMGATE_H */
