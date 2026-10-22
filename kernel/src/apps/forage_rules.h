/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* forage_rules.h — Forage's rules in C, the reference its Sutra, Trebago,
 * Eve and MYRA blocks are checked against (blocks/forage). Pure integer
 * functions, no state: the model (forage_model.c) and the language test
 * (tests/forage_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   FR1 NET AFTER THE FEE: amount - fairground_assurance_fee(amount), the
 *       0.08889% assurance fee taken from the gross, floor rounding as
 *       Fairground's (so the fee is never rounded up and the net never
 *       down). amount 0..FAIRGROUND_AMOUNT_MAX (2^53 - 1); else refused.
 *       Witnessed for 0..FAIRGROUND_FEE_WITNESSED (Sutra's 9(15)); past it
 *       Sutra refuses and Trebago answers: held. Since the fee never
 *       falls as the amount grows, net + fee(net) <= amount: a price of
 *       net, paid with its fee on top, never passes the gross.
 *   FR2 A CLAIM FITS: the work's swarm tokens a cycle 1..FORAGE_NINE, its
 *       power draw in milliwatts 0..FORAGE_NINE, and the node's budgets
 *       not passed, each given as its margin 0..FORAGE_NINE: tokroom (the
 *       swarm tokens the owner lets bounties take this cycle, less those
 *       already claimed, less this work's) and mwroom (the owner's power
 *       cap, less the node's draw now, less this work's).
 *   FR3 RANK BY VALUE PER MINUTE: each bounty's place, best first, by
 *       amount / minutes compared exactly (a * m' against a' * m, never
 *       divided), ties sharing the higher place: 1 + the number of
 *       bounties strictly better (competition ranking, 1224). amount
 *       0..FORAGE_NINE, minutes 1..FORAGE_NINE, at least one bounty; else
 *       refused.
 *   FR4 A BOUNTY'S RECORD of ^ZXFORAGE: bounty at most 4 digits, kind 1,
 *       tokens 9, mw 9, amount 15, minutes 9, status 1, sign aside
 *       (blocks/forage/bounty.myra). */
#ifndef FORAGE_RULES_H
#define FORAGE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "fairground_rules.h"

#define FORAGE_NINE       INT64_C(999999999) /* FR2, FR3: 9 digits */
#define FORAGE_RECORD_MAX INT64_C(9999)      /* FR4: a bounty's number, 4 digits */
#define FORAGE_REFUSED    INT64_MIN

/* FR1: the net, or FORAGE_REFUSED outside 0..FAIRGROUND_AMOUNT_MAX. */
static inline int64_t forage_net_after_fee(int64_t amount)
{
    int64_t fee = fairground_assurance_fee(amount);
    if (fee == FAIRGROUND_REFUSED) return FORAGE_REFUSED;
    return amount - fee;
}

/* FR2 */
static inline bool forage_claim_fits(int64_t tokens, int64_t mw, int64_t tokroom, int64_t mwroom)
{
    return tokens >= 1 && tokens <= FORAGE_NINE && mw >= 0 && mw <= FORAGE_NINE && tokroom >= 0 &&
           tokroom <= FORAGE_NINE && mwroom >= 0 && mwroom <= FORAGE_NINE;
}

/* FR3: rank[i] for each of n bounties; false (nothing written) when refused. */
static inline bool forage_rate_rank(const int64_t *amount, const int64_t *minutes, uint32_t n,
                                    int64_t *rank)
{
    if (!amount || !minutes || !rank || n == 0) return false;
    for (uint32_t i = 0; i < n; i++)
        if (amount[i] < 0 || amount[i] > FORAGE_NINE || minutes[i] < 1 || minutes[i] > FORAGE_NINE)
            return false;
    for (uint32_t i = 0; i < n; i++) {
        int64_t r = 1;
        for (uint32_t j = 0; j < n; j++)
            if (amount[j] * minutes[i] > amount[i] * minutes[j]) r++;
        rank[i] = r;
    }
    return true;
}

/* FR4: does x have at most `digits` digits, sign aside? */
static inline bool forage_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool forage_record_fits(int64_t bounty, int64_t kind, int64_t tokens, int64_t mw,
                                      int64_t amount, int64_t minutes, int64_t status)
{
    return forage_digits_fit(bounty, 4) && forage_digits_fit(kind, 1) &&
           forage_digits_fit(tokens, 9) && forage_digits_fit(mw, 9) &&
           forage_digits_fit(amount, 15) && forage_digits_fit(minutes, 9) &&
           forage_digits_fit(status, 1);
}

#endif
