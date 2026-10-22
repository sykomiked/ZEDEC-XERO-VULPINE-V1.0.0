/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assize_rules.h — Assize's rules in C, the reference its Eve, KRON,
 * Trebago, Sutra, ALGOL 60 and MYRA blocks are checked against
 * (blocks/assize), with the rules it composes from nearer the core. Pure
 * integer functions, no state: the model (assize_model.c) and the
 * language test (tests/assize_zxapp.inc) both include this.
 *
 *   AR1 FILING ACCEPTED (filing_accepted): a filing is taken onto a case's
 *       docket when it is made by a party to the case (party 1), inside
 *       the deadline (left, response_days_left of the case's deadline and
 *       today, 0..ASSIZE_LEFT_MAX: 0 is the last day) and within the
 *       procedural limits (pages 1..ASSIZE_PAGES_MAX, exhibits
 *       0..ASSIZE_EXHIBITS_MAX). Each field on its own range; a left past
 *       ASSIZE_LEFT_MAX is no deadline a tribunal sets (Eve refuses it,
 *       KRON reads only left >= 0: held).
 *   AR2 DAYS LEFT TO RESPOND (response_days_left): deadline - today, both
 *       days since 1970, 0..ASSIZE_DAY_MAX; below 0 the deadline has passed
 *       (never clamped); anything outside refused (Trebago refuses, KRON
 *       answers: held).
 *   AR3 DOCKET NUMBER (docket_number): caseno * 10000 + seq, caseno
 *       1..999999 and seq (the filing's place on its case's docket, from
 *       tallystick.next_doc_number, never reused) 1..9999; past 9999 the
 *       docket is full: refused, never wrapped. Sutra's pictures (9(6),
 *       9(4), 9(10)) hold a case or seq of 0, the ALGOL 60 proof refuses
 *       them (no docket number names nothing): held.
 *   AR4 EVIDENCE MATRIX (claim_evidence_state): one evidence item a row,
 *       the claim it bears on (1..9999) and what it says of it (says: 0
 *       silent, 1 supports, 2 contradicts). Each row's answer is its
 *       claim's four-valued state in the kernel's codes (lpres.h): 1 when
 *       some item supports the claim, plus 2 when some item contradicts
 *       it, so 0 NEITHER, 1 TRUE, 2 FALSE, 3 BOTH: exactly lpres_kjoin of
 *       the claim's items. A claim or says outside is refused.
 *   AR5 DISPUTE NODE (dispute_node_ok) fits ^ZXASSIZE(caseno, seq) = kind
 *       ^ party ^ filed ^ claim: caseno 6 digits, seq 4, kind 1, party 2,
 *       filed (days since 1970) 6, claim 4, sign aside.
 *   AR6 WHERE A WITNESS PLACES THE EVENT: a claim asserts a site; an
 *       evidence item that places the event at the asserted site supports
 *       it (says 1), one that places it elsewhere contradicts it (says 2),
 *       one that places it nowhere (site 0) is silent. Two witnesses who
 *       place one event at two sites make the claim BOTH: both items are
 *       kept, never merged, and the claim is held in escrow until the
 *       arbiter (a person) rules.
 *   Composed, as the parts' own rules: witness.chain_ok, witness.audit_ok
 *   and witness.kind_count (witness_rules.h WR1-WR3),
 *   broadsheet.well_formed and broadsheet.note_ok (broadsheet_rules.h
 *   BR1, BR4), sealwax.sign_ready and sealwax.field_record_ok
 *   (sealwax_rules.h SR1, SR3, SR6), twelvefold.lp_entail_kept
 *   (twelvefold_rules.h TR2) and tallystick.next_doc_number
 *   (tallystick_rules.h TR3). */
#ifndef ASSIZE_RULES_H
#define ASSIZE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "broadsheet_rules.h"
#include "lpres.h"
#include "sealwax_rules.h"
#include "tallystick_rules.h"
#include "twelvefold_rules.h"
#include "witness_rules.h"

#define ASSIZE_LEFT_MAX     INT64_C(9999)       /* AR1: days to a deadline */
#define ASSIZE_PAGES_MAX    INT64_C(250)        /* AR1: pages of one filing */
#define ASSIZE_EXHIBITS_MAX INT64_C(99)         /* AR1: exhibits of one filing */
#define ASSIZE_DAY_MAX      INT64_C(999999)     /* AR2: days since 1970, 6 digits */
#define ASSIZE_CASE_MAX     INT64_C(999999)     /* AR3: 9(6) */
#define ASSIZE_SEQ_MAX      INT64_C(9999)       /* AR3: 9(4) a case */
#define ASSIZE_DOCKET_MAX   INT64_C(9999999999) /* AR3: 9(10) */
#define ASSIZE_CLAIM_MAX    INT64_C(9999)       /* AR4 */
#define ASSIZE_PARTY_MAX    INT64_C(99)         /* AR5: party 2 digits */
#define ASSIZE_REFUSED      (-1)

/* what an evidence item says of its claim (AR4) */
enum { ASSIZE_SILENT = 0, ASSIZE_SUPPORTS = 1, ASSIZE_CONTRADICTS = 2 };

/* AR1 */
static inline bool assize_filing_accepted(int64_t party, int64_t left, int64_t pages,
                                          int64_t exhibits)
{
    return party == 1 && left >= 0 && left <= ASSIZE_LEFT_MAX && pages >= 1 &&
           pages <= ASSIZE_PAGES_MAX && exhibits >= 0 && exhibits <= ASSIZE_EXHIBITS_MAX;
}

/* AR2: false when refused */
static inline bool assize_days_left(int64_t deadline, int64_t today, int64_t *left)
{
    if (!left || deadline < 0 || deadline > ASSIZE_DAY_MAX || today < 0 || today > ASSIZE_DAY_MAX)
        return false;
    *left = deadline - today;
    return true;
}

/* AR3: the docket number, or ASSIZE_REFUSED */
static inline int64_t assize_docket_number(int64_t caseno, int64_t seq)
{
    if (caseno < 1 || caseno > ASSIZE_CASE_MAX || seq < 1 || seq > ASSIZE_SEQ_MAX)
        return ASSIZE_REFUSED;
    return caseno * 10000 + seq;
}

/* AR3: a case's next filing seq after `last` (tallystick.next_doc_number), or ASSIZE_REFUSED
 * when the docket is full */
static inline int64_t assize_next_seq(int64_t last)
{
    int64_t next = tstk_next_number(last);
    return next == TSTK_REFUSED || next > ASSIZE_SEQ_MAX ? ASSIZE_REFUSED : next;
}

/* AR4: row i's claim state over n evidence rows, or ASSIZE_REFUSED */
static inline int64_t assize_claim_state(const int64_t *claim, const int64_t *says, uint32_t n,
                                         uint32_t i)
{
    int64_t sup = 0, con = 0;
    if (!claim || !says || i >= n) return ASSIZE_REFUSED;
    for (uint32_t k = 0; k < n; k++)
        if (claim[k] < 1 || claim[k] > ASSIZE_CLAIM_MAX || says[k] < ASSIZE_SILENT ||
            says[k] > ASSIZE_CONTRADICTS)
            return ASSIZE_REFUSED;
    for (uint32_t k = 0; k < n; k++) {
        if (claim[k] != claim[i]) continue;
        sup |= says[k] == ASSIZE_SUPPORTS;
        con |= says[k] == ASSIZE_CONTRADICTS;
    }
    return sup + 2 * con;
}

/* AR5 */
static inline bool assize_node_ok(int64_t caseno, int64_t seq, int64_t kind, int64_t party,
                                  int64_t filed, int64_t claim)
{
    return witness_digits_fit(caseno, 6) && witness_digits_fit(seq, 4) &&
           witness_digits_fit(kind, 1) && witness_digits_fit(party, 2) &&
           witness_digits_fit(filed, 6) && witness_digits_fit(claim, 4);
}

/* AR6: what an item placing the event at `site` says of a claim asserting `asserted` */
static inline int64_t assize_says(int64_t asserted, int64_t site)
{
    if (!site) return ASSIZE_SILENT;
    return site == asserted ? ASSIZE_SUPPORTS : ASSIZE_CONTRADICTS;
}

/* AR6: the claim's state from n items' sites, joined with lpres_kjoin */
static inline lpres_state_t assize_sites_state(int64_t asserted, const int64_t *site, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    if (!site) return s;
    for (uint32_t i = 0; i < n; i++) {
        int64_t says = assize_says(asserted, site[i]);
        if (says == ASSIZE_SUPPORTS) s = lpres_kjoin(s, LPRES_STATE_TRUE);
        if (says == ASSIZE_CONTRADICTS) s = lpres_kjoin(s, LPRES_STATE_FALSE);
    }
    return s;
}

#endif
