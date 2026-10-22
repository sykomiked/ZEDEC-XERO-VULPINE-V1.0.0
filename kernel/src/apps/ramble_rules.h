/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ramble_rules.h — Ramble's rules in C, the reference its Eve, Sutra and
 * MYRA blocks are checked against (blocks/ramble). Pure integer functions,
 * no state: the model (ramble_model.c) and the language test
 * (tests/ramble_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. The CID text check is Seedbank's
 * (seedbank.cid_byte, seedbank_rules.h SR1, SR2), used as it is, and a
 * balance after money leaves is Reckoner's statement_close.
 *
 *   RR1 CONSENT COVERS AN ACTION when, each given as its margin: scopegap
 *       (the scopes the action needs that the grant does not hold,
 *       ramble_scope_gap) is 0; ceilingleft (the grant's ceiling minus the
 *       action's amount, in minor units) is 0..999999999999999999; and
 *       secondsleft (the grant's expiry minus now, in seconds) is 1..86400,
 *       since zxvd's web4.consent never grants for longer than a day
 *       (expires_in 1..86400). A grant that would last longer is no grant
 *       zxvd made, so it covers nothing.
 *   RR2 AN ENVELOPE RECORD of ^ZXRAMBLE holds integers of at most the
 *       places its pieces declare, sign aside (blocks/ramble/envelope.myra):
 *       id 10, kind 1, status 1, amount 18, stamp 12.
 *   RR3 THE SCOPES an action needs: personal data (1) when it carries
 *       personal data, money (2) when it moves an amount above 0, as zxvd's
 *       web4.post holds them for consent. */
#ifndef RAMBLE_RULES_H
#define RAMBLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define RAMBLE_SCOPE_PII    1                           /* RR3 */
#define RAMBLE_SCOPE_MONEY  2                           /* RR3 */
#define RAMBLE_SCOPE_ALL    3                           /* RR3 */
#define RAMBLE_MINOR_MAX    INT64_C(999999999999999999) /* RR1: 18 digits */
#define RAMBLE_GRANT_MAX    86400                       /* RR1: zxvd's expires_in */
#define RAMBLE_ID_FIELD     INT64_C(9999999999)         /* RR2: i10 */
#define RAMBLE_KIND_FIELD   9                           /* RR2: i1 */
#define RAMBLE_STATUS_FIELD 9                           /* RR2: i1 */
#define RAMBLE_AMOUNT_FIELD RAMBLE_MINOR_MAX            /* RR2: i18 */
#define RAMBLE_STAMP_FIELD  INT64_C(999999999999)       /* RR2: i12 */
#define RAMBLE_REFUSED      (-1)

/* RR3 */
static inline int64_t ramble_need(bool pii, int64_t amount)
{
    return (pii ? RAMBLE_SCOPE_PII : 0) | (amount > 0 ? RAMBLE_SCOPE_MONEY : 0);
}

/* RR1: how many scopes need asks for that granted does not hold; both
 * 0..3, else refused. */
static inline int64_t ramble_scope_gap(int64_t need, int64_t granted)
{
    if (need < 0 || need > RAMBLE_SCOPE_ALL || granted < 0 || granted > RAMBLE_SCOPE_ALL)
        return RAMBLE_REFUSED;
    int64_t missing = need & ~granted;
    return (missing & RAMBLE_SCOPE_PII ? 1 : 0) + (missing & RAMBLE_SCOPE_MONEY ? 1 : 0);
}

/* RR1 */
static inline bool ramble_consent_covers(int64_t scopegap, int64_t ceilingleft, int64_t secondsleft)
{
    return scopegap == 0 && ceilingleft >= 0 && ceilingleft <= RAMBLE_MINOR_MAX &&
           secondsleft >= 1 && secondsleft <= RAMBLE_GRANT_MAX;
}

/* RR1's ceilingleft and secondsleft, worked out without overflow: a value
 * that does not fit int64 is past every range, so it is given as INT64_MIN. */
static inline int64_t ramble_margin(int64_t most, int64_t used)
{
    if ((used > 0 && most < INT64_MIN + used) || (used < 0 && most > INT64_MAX + used))
        return INT64_MIN;
    return most - used;
}

static inline bool ramble_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* RR2 */
static inline bool ramble_envelope_record_ok(int64_t id, int64_t kind, int64_t status,
                                             int64_t amount, int64_t stamp)
{
    return ramble_fits(id, RAMBLE_ID_FIELD) && ramble_fits(kind, RAMBLE_KIND_FIELD) &&
           ramble_fits(status, RAMBLE_STATUS_FIELD) && ramble_fits(amount, RAMBLE_AMOUNT_FIELD) &&
           ramble_fits(stamp, RAMBLE_STAMP_FIELD);
}

#endif
