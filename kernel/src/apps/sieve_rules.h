/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sieve_rules.h — Sieve's rules in C, the reference its Eve, KRON, C++
 * (WebAssembly), Trebago and MYRA blocks are checked against
 * (blocks/sieve). Pure integer functions, no state: the model
 * (sieve_model.c) and the language test (tests/sieve_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   SR1 A HELD SUGGESTION MAY BE APPLIED when, each given as its margin:
 *       accepted, the owner's accept, is exactly 1; secondsleft, its
 *       expiry minus now, is 1..86400 (a hold never lasts longer than a
 *       day, the longest zxvd's web4.consent grants, so one limit holds
 *       for every suggestion); and drift, the target's version now minus
 *       the version the suggestion was made against, is exactly 0 (the
 *       target unchanged since). Anything else is no.
 *   SR2 THE SAME RULE AS A NUMBER to count with: 1 or 0; refused when the
 *       accept is no flag (not 0 or 1) or secondsleft is longer than any
 *       hold (past 86400). An expired hold (secondsleft 0 or less) and any
 *       drift are an ordinary 0.
 *   SR3 HELD FOR AN APP: how many suggestions are still held (pending, one
 *       flag per suggestion, 0 or 1) for the app `want`, one app slot per
 *       suggestion: the sum of pending over the rows whose app is want;
 *       refused when a flag is no flag.
 *   SR4 THE LIMITS OF A HOLD: its source is 0..2 (SIEVE_SRC_*), its time to
 *       live (ttl) 1..86400 seconds and the second it was made 0..
 *       253402300799 (the last second of the year 9999, zxvd's "now").
 *   SR5 A SUGGESTION RECORD of ^ZXSIEVE holds integers of at most the
 *       places its pieces declare, sign aside (blocks/sieve/record.myra):
 *       id 10, app 3, source 1, state 1, expires 12, version 18.
 *   SR6 SECONDS LEFT of a hold made at `made` for `ttl` seconds, seen at
 *       now: made + ttl - now, never below 0 (0 is lapsed); each part
 *       inside SR4's ranges, else refused.
 *   SR7 DRIFT of a target: its version now minus its version then, each
 *       0..999999999999999999 (MYRA's i18), else refused as INT64_MIN, a
 *       drift no rule reads as unchanged. */
#ifndef SIEVE_RULES_H
#define SIEVE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SIEVE_HOLD_MAX      86400                       /* SR1, SR4: a day, web4.consent's most */
#define SIEVE_NOW_MAX       INT64_C(253402300799)       /* SR4: the last second of 9999 */
#define SIEVE_VERSION_MAX   INT64_C(999999999999999999) /* SR7: 18 digits */
#define SIEVE_SRC_ANSWER    0                           /* SR4: a swarm.ask answer */
#define SIEVE_SRC_DRAFT     1                           /* SR4: a play.draft draft */
#define SIEVE_SRC_ENVELOPE  2                           /* SR4: a web4 envelope held for consent */
#define SIEVE_SRC_MAX       2                           /* SR4 */
#define SIEVE_ID_FIELD      INT64_C(9999999999)         /* SR5: i10 */
#define SIEVE_APP_FIELD     999                         /* SR5: i3 */
#define SIEVE_SOURCE_FIELD  9                           /* SR5: i1 */
#define SIEVE_STATE_FIELD   9                           /* SR5: i1 */
#define SIEVE_EXPIRES_FIELD INT64_C(999999999999)       /* SR5: i12 */
#define SIEVE_VERSION_FIELD SIEVE_VERSION_MAX           /* SR5: i18 */
#define SIEVE_REFUSED       (-1)

/* SR1 */
static inline bool sieve_apply_ok(int64_t accepted, int64_t secondsleft, int64_t drift)
{
    return accepted == 1 && secondsleft >= 1 && secondsleft <= SIEVE_HOLD_MAX && drift == 0;
}

/* SR2 */
static inline int64_t sieve_apply_n(int64_t accepted, int64_t secondsleft, int64_t drift)
{
    if ((accepted != 0 && accepted != 1) || secondsleft > SIEVE_HOLD_MAX) return SIEVE_REFUSED;
    return sieve_apply_ok(accepted, secondsleft, drift) ? 1 : 0;
}

/* SR3 */
static inline int64_t sieve_held_count(const int64_t *app, const int64_t *pending, uint32_t n,
                                       int64_t want)
{
    int64_t c = 0;
    if ((!app || !pending) && n) return SIEVE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (pending[i] != 0 && pending[i] != 1) return SIEVE_REFUSED;
        if (app[i] == want) c += pending[i];
    }
    return c;
}

/* SR4 */
static inline bool sieve_hold_ok(int64_t source, int64_t ttl, int64_t made)
{
    return source >= 0 && source <= SIEVE_SRC_MAX && ttl >= 1 && ttl <= SIEVE_HOLD_MAX &&
           made >= 0 && made <= SIEVE_NOW_MAX;
}

static inline bool sieve_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* SR5 */
static inline bool sieve_record_ok(int64_t id, int64_t app, int64_t source, int64_t state,
                                   int64_t expires, int64_t version)
{
    return sieve_fits(id, SIEVE_ID_FIELD) && sieve_fits(app, SIEVE_APP_FIELD) &&
           sieve_fits(source, SIEVE_SOURCE_FIELD) && sieve_fits(state, SIEVE_STATE_FIELD) &&
           sieve_fits(expires, SIEVE_EXPIRES_FIELD) && sieve_fits(version, SIEVE_VERSION_FIELD);
}

/* SR6: SIEVE_REFUSED when a part is outside SR4's ranges */
static inline int64_t sieve_seconds_left(int64_t made, int64_t ttl, int64_t now)
{
    if (!sieve_hold_ok(SIEVE_SRC_ANSWER, ttl, made) || now < 0 || now > SIEVE_NOW_MAX)
        return SIEVE_REFUSED;
    int64_t left = made + ttl - now; /* each part bounded: no overflow */
    return left > 0 ? left : 0;
}

/* SR7 */
static inline int64_t sieve_drift(int64_t now_version, int64_t then_version)
{
    if (now_version < 0 || now_version > SIEVE_VERSION_MAX || then_version < 0 ||
        then_version > SIEVE_VERSION_MAX)
        return INT64_MIN;
    return now_version - then_version;
}

#endif
