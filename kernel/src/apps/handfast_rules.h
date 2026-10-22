/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* handfast_rules.h — Handfast's rules in C, the reference its Eve, KRON,
 * Sutra, Trebago, ALGOL 60 and MYRA blocks are checked against
 * (blocks/handfast), with the rules it composes from nearer the core.
 * Pure integer functions, no state: the model (handfast_model.c) and the
 * language test (tests/handfast_zxapp.inc) both include this.
 *
 *   HR1 LIMITS (clause_limits_ok): an agreement has 2..16 parties and a
 *       clause's text is 1..8192 bytes, each on its own range.
 *   HR2 PARTY MATRIX (clause_party_state): the matrix is party by clause,
 *       one cell a row (its clause and the party's mark: 0 silent, 1
 *       agreed, 2 disputed). Each row answers its clause's four-valued
 *       state in the kernel's codes (lpres.h): BOTH 3 when one party
 *       agreed and another disputed (held in escrow, never resolved by
 *       picking one), FALSE 2 when it is disputed and nobody agreed, TRUE
 *       1 when every party agreed (silence is not consent: one silent
 *       cell keeps it from TRUE), NEITHER 0 otherwise. A mark outside
 *       0..2 is no mark the model keeps (it refuses one).
 *   HR3 READY TO SIGN (agreement_ready): 2..16 parties, 1..64 clauses and
 *       no cell of the matrix unagreed (silent, disputed or held), each
 *       count on its own range: every clause agreed by every party.
 *   HR4 OBLIGATION SHARE (obligation_share): one party's obligation on one
 *       due date, in minor units, when `obligors` parties owe a clause's
 *       amount in equal parts: floor(amount / obligors), and one minor
 *       unit more for the first amount mod obligors of them (pos, the
 *       party's place among the obligors, 1..obligors), so the parts sum
 *       to the amount exactly. amount 0..999999999999999 (Sutra's 9(15),
 *       Sealwax's amount bounds), obligors 1..99 (9(2)), else refused.
 *   HR5 CLAUSE TREE (clause_node_ok) fits ^ZXHANDFAST(deal, clause, rev)
 *       = parent ^ bytes ^ state: deal 9 digits, clause 3, rev 6, parent
 *       3, bytes 4 and state 1, sign aside.
 *   Composed, as the parts' own rules: witness.chain_ok and audit_ok
 *   (witness_rules.h) for each clause's revisions on the journal,
 *   commons.room_post_ok and message_text_ok (commons_rules.h) for the
 *   negotiation room, kindred.contact_fields_ok and contact_record_ok
 *   (kindred_rules.h) for each party, sealwax.sign_ready and
 *   field_record_ok (sealwax_rules.h) for the signature page,
 *   furrow.money_cell (through sealwax_amount_minor) for each amount, and
 *   trickle.stream_due (trickle_rules.h TK1) for the payment schedule:
 *   instalment n of N owes floor(total * n / N) - paid. */
#ifndef HANDFAST_RULES_H
#define HANDFAST_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "commons_rules.h"
#include "furrow_rules.h"
#include "kindred_rules.h"
#include "lpres.h"
#include "sealwax_rules.h"
#include "trickle_rules.h"
#include "witness_rules.h"

#define HANDFAST_REFUSED    (-1)
#define HANDFAST_PARTY_MIN  2                        /* HR1 */
#define HANDFAST_PARTY_MAX  16                       /* HR1 */
#define HANDFAST_TEXT_MAX   8192                     /* HR1: bytes of a clause */
#define HANDFAST_CLAUSE_MAX 64                       /* HR3: clauses in one agreement */
#define HANDFAST_AMOUNT_MAX INT64_C(999999999999999) /* HR4: 9(15) */
#define HANDFAST_OBLIGORS   99                       /* HR4: 9(2) */
#define HANDFAST_DEAL_MAX   INT64_C(999999999)       /* HR5: 9 digits */
#define HANDFAST_NUMBER_MAX 999                      /* HR5: a clause's number, 3 digits */
#define HANDFAST_REV_MAX    INT64_C(999999)          /* HR5: 6 digits */
#define HANDFAST_DAY_MAX    INT64_C(999999)          /* a due date, days since 1970 */
#define HANDFAST_KIND       1                        /* witness.audit_ok's kind for a revision */

enum { HANDFAST_SILENT = 0, HANDFAST_AGREED = 1, HANDFAST_DISPUTED = 2 };

/* HR1 */
static inline bool handfast_limits_ok(int64_t parties, int64_t bytes)
{
    return parties >= HANDFAST_PARTY_MIN && parties <= HANDFAST_PARTY_MAX && bytes >= 1 &&
           bytes <= HANDFAST_TEXT_MAX;
}

/* HR2: row i's clause state (its code, 0..3) over n cells. */
static inline int64_t handfast_cell_state(const int64_t *clause, const int64_t *mark, uint32_t n,
                                          uint32_t i)
{
    bool agreed = false, disputed = false, silent = false;
    if (!clause || !mark || i >= n) return LPRES_STATE_NEITHER;
    for (uint32_t j = 0; j < n; j++) {
        if (clause[j] != clause[i]) continue;
        agreed |= mark[j] == HANDFAST_AGREED;
        disputed |= mark[j] == HANDFAST_DISPUTED;
        silent |= mark[j] == HANDFAST_SILENT;
    }
    if (agreed && disputed) return LPRES_STATE_BOTH;
    if (disputed) return LPRES_STATE_FALSE;
    return agreed && !silent ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
}

/* HR3 */
static inline bool handfast_ready_rule(int64_t parties, int64_t clauses, int64_t unagreed)
{
    return parties >= HANDFAST_PARTY_MIN && parties <= HANDFAST_PARTY_MAX && clauses >= 1 &&
           clauses <= HANDFAST_CLAUSE_MAX && unagreed == 0;
}

/* HR4: the share in minor units, or HANDFAST_REFUSED. */
static inline int64_t handfast_share(int64_t amount, int64_t obligors, int64_t pos)
{
    if (amount < 0 || amount > HANDFAST_AMOUNT_MAX || obligors < 1 ||
        obligors > HANDFAST_OBLIGORS || pos < 1 || pos > obligors)
        return HANDFAST_REFUSED;
    int64_t base = amount / obligors;
    return base + (pos <= amount - base * obligors ? 1 : 0);
}

/* HR5 */
static inline bool handfast_node_ok(int64_t deal, int64_t clause, int64_t rev, int64_t parent,
                                    int64_t bytes, int64_t state)
{
    return witness_digits_fit(deal, 9) && witness_digits_fit(clause, 3) &&
           witness_digits_fit(rev, 6) && witness_digits_fit(parent, 3) &&
           witness_digits_fit(bytes, 4) && witness_digits_fit(state, 1);
}

#endif
