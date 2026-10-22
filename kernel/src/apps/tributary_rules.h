/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tributary_rules.h — Tributary's rules in C, the reference its KRON,
 * Trebago, Eve, Sutra and MYRA blocks are checked against
 * (blocks/tributary). Pure integer functions, no state: the model
 * (tributary_model.c) and the language test (tests/tributary_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   TB1 AGREE COUNT: each answer's normalised code is 1..TRIB_CODE_MAX
 *       (0 is no answer); row i counts the answers whose code is code[i],
 *       itself included: witness.kind_count (witness_rules.h WR2) taken
 *       with each row's own code as want. A code outside refuses the whole
 *       vector (Trebago divides by zero; KRON counts it: held).
 *   TB2 VERDICT: the answers about one question joined in four values.
 *       Each answer is a source on the claim "the answer is the first
 *       answer's code": TRUE when its code is that code, FALSE otherwise;
 *       the sources are joined by lpres_kjoin (TRUE with FALSE is BOTH,
 *       no source is NEITHER). The lead is the code with the most agreeing
 *       answers (TB1's counts), none when two codes tie. The quorum
 *       is a second fact: TRUE when at least `quorum` answers came, else
 *       NEITHER (too few to decide). The verdict is lpres_conjoin of the
 *       two: all agree TRUE, split BOTH, too few or none NEITHER.
 *   TB3 MAY ASK ANOTHER MODEL: the owner's caps less what this question
 *       has spent and what the next ask may spend, as margins, each held
 *       to its own range: tokens 0..TRIB_TOKEN_ROOM_MAX, energy (uJ) and
 *       price (VFV minor units, before the assurance fee) each
 *       0..TRIB_ROOM_MAX. A margin below 0 is over a cap; one past its
 *       range is no measurement (Eve refuses it; KRON reads any margin at
 *       or above 0 as room: held).
 *   TB4 CONFIDENCE: 100 * agree / asked, an exact reduced fraction of a
 *       percent, never rounded; agree 0..asked and asked
 *       1..TRIB_ASKED_MAX, else refused (Trebago divides by zero).
 *   TB5 CODE MODELS: the answer matrix, models by codes; row i is one
 *       answer (its model, its code) and answers how many distinct models
 *       gave code[i] (its column's models), each model counted once
 *       however often it answered so.
 *   TB6 QUERY LEDGER: a query's token cost entered at one denomination of
 *       swarm_denom.h (D1: tier k has the face F(k+2), 1, 2, 3, 5, 8, ...):
 *       after = before + pieces * face(tier). Tiers 0..TRIB_TIER_MAX
 *       (PLAIN to RACING octane, faces 1..75025); before, pieces and after
 *       each 0..TRIB_LEDGER_MAX (Sutra's 9(18)); anything past is refused,
 *       never wrapped.
 *   TB7 SOURCE RECORD: ^ZXTRIB(question, model) = code ^ stamp holds
 *       integers of at most question 9, model 2, code 9 and stamp (ms since
 *       1970) 15 digits, sign aside (blocks/tributary/source.myra).
 *   TB8 TOKEN SHARE: each source's share of the question's tokens, per
 *       mille, rounded down: swarm_shares.cycle_share's meaning,
 *       floor(1000 * t / max(1, sum t)). */
#ifndef TRIBUTARY_RULES_H
#define TRIBUTARY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "assay_rules.h"
#include "lpres.h"
#include "retort_rules.h"
#include "twelvefold_rules.h" /* and witness_rules.h, which Twelvefold composes */

#define TRIB_CODE_MAX       INT64_C(999999999)          /* TB1: a normalised answer code */
#define TRIB_TOKEN_ROOM_MAX INT64_C(999999999)          /* TB3 */
#define TRIB_ROOM_MAX       INT64_C(999999999999999999) /* TB3: Eve's 18 digits */
#define TRIB_ASKED_MAX      INT64_C(999999999)          /* TB4 */
#define TRIB_TIER_MAX       23u                         /* TB6: SWARM_OCTANE_RACING */
#define TRIB_LEDGER_MAX     INT64_C(999999999999999999) /* TB6: 9(18) */
#define TRIB_REFUSED        INT64_MIN

static inline bool tributary_code_ok(int64_t code)
{
    return code >= 1 && code <= TRIB_CODE_MAX;
}

/* TB1: out[i] for every row, false (out untouched) when a code is no code. */
static inline bool tributary_agree_counts(const int64_t *code, uint32_t n, int64_t *out)
{
    if (!code || !out) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!tributary_code_ok(code[i])) return false;
    for (uint32_t i = 0; i < n; i++) out[i] = witness_kind_count(code, n, code[i]);
    return true;
}

/* TB2: the verdict over n answers' codes. */
static inline lpres_state_t tributary_verdict(const int64_t *code, uint32_t n, int64_t quorum)
{
    lpres_state_t said = LPRES_STATE_NEITHER;
    if (!code || n == 0) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < n; i++)
        said = lpres_kjoin(said, code[i] == code[0] ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    lpres_state_t enough = (int64_t) n >= quorum ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    return lpres_conjoin(said, enough);
}

/* TB2: the code most answers gave, or 0 when two codes tie for the most
 * (none is picked at random) or there is no answer. */
static inline int64_t tributary_lead_code(const int64_t *code, const int64_t *agree, uint32_t n)
{
    int64_t best = 0, lead = 0;
    bool tie = false;
    if (!code || !agree) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (agree[i] > best) {
            best = agree[i], lead = code[i], tie = false;
        } else if (agree[i] == best && code[i] != lead) {
            tie = true;
        }
    }
    return tie ? 0 : lead;
}

/* TB3 */
static inline bool tributary_ask_within_caps(int64_t tokenroom, int64_t energyroom,
                                             int64_t priceroom)
{
    return tokenroom >= 0 && tokenroom <= TRIB_TOKEN_ROOM_MAX && energyroom >= 0 &&
           energyroom <= TRIB_ROOM_MAX && priceroom >= 0 && priceroom <= TRIB_ROOM_MAX;
}

/* TB4: *num / *den reduced, false (nothing written) when refused. */
static inline bool tributary_confidence_x100(int64_t agree, int64_t asked, int64_t *num,
                                             int64_t *den)
{
    if (!num || !den || asked < 1 || asked > TRIB_ASKED_MAX || agree < 0 || agree > asked)
        return false;
    int64_t g = retort_gcd(100 * agree, asked); /* asked >= 1, so g >= 1 */
    *num = 100 * agree / g;
    *den = asked / g;
    return true;
}

/* TB5: the distinct models that gave code[i], or TRIB_REFUSED past n. */
static inline int64_t tributary_code_models(const int64_t *model, const int64_t *code, uint32_t n,
                                            uint32_t i)
{
    int64_t c = 0;
    if (!model || !code || i >= n) return TRIB_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (code[j] != code[i]) continue;
        bool first = true; /* model[j]'s first answer of this code */
        for (uint32_t k = 0; k < j && first; k++)
            if (code[k] == code[i] && model[k] == model[j]) first = false;
        c += first;
    }
    return c;
}

/* TB6: F(tier + 2), or 0 past TRIB_TIER_MAX (swarm_denom.h D1). */
static inline int64_t tributary_face(int64_t tier)
{
    int64_t a = 1, b = 2; /* F(2), F(3) */
    if (tier < 0 || tier > (int64_t) TRIB_TIER_MAX) return 0;
    for (int64_t k = 0; k < tier; k++) {
        int64_t c = a + b;
        a = b;
        b = c;
    }
    return a;
}

/* TB6: the ledger after one entry, or TRIB_REFUSED. */
static inline int64_t tributary_ledger_after(int64_t before, int64_t tier, int64_t pieces)
{
    int64_t face = tributary_face(tier);
    if (face == 0 || before < 0 || before > TRIB_LEDGER_MAX || pieces < 0 ||
        pieces > TRIB_LEDGER_MAX)
        return TRIB_REFUSED;
    if (pieces > (TRIB_LEDGER_MAX - before) / face) return TRIB_REFUSED;
    return before + pieces * face;
}

/* TB7 */
static inline bool tributary_source_fits(int64_t question, int64_t model, int64_t code,
                                         int64_t stamp)
{
    return retort_digits_fit(question, 9) && retort_digits_fit(model, 2) &&
           retort_digits_fit(code, 9) && retort_digits_fit(stamp, 15);
}

/* TB8: t[i]'s share per mille, or TRIB_REFUSED (a negative or a sum past
 * 64 bits / 1000). */
static inline int64_t tributary_token_permille(const int64_t *t, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!t || i >= n) return TRIB_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (t[j] < 0 || t[j] > INT64_MAX / 1000 - s) return TRIB_REFUSED;
        s += t[j];
    }
    return t[i] * 1000 / (s > 0 ? s : 1);
}

#endif
