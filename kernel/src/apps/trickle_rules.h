/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trickle_rules.h — Trickle's rules in C, the reference its Sutra, Trebago
 * and Eve blocks are checked against (blocks/trickle). Pure integer
 * functions, no state: the model (trickle_model.c) and the language test
 * (tests/trickle_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   TK1 A STREAM'S DUE. A stream pays total minor units over ticks periods
 *       (1..TRK_TICKS_MAX, Sutra's 9(5)); at tick t (0..ticks) it owes
 *       floor(total * t / ticks) - paid, paid being what it has paid so
 *       far. The cumulative rule estate.stream keeps (floor(total x
 *       share), the held-back units carried): no period rounds a unit
 *       away, and at t = ticks the payments sum to total exactly. total
 *       0..TRK_TOTAL_MAX (Sutra's 9(12)); a paid past what is owed (the
 *       stream would claw back) is refused.
 *   TK2 A TITHE. A tithe gives tnum / tden of each income (0 <= tnum <=
 *       tden, tden 1..TRK_TICKS_MAX): floor((income * tnum + carry) /
 *       tden), carry being the remainder carried from the incomes before
 *       (0..tden - 1, in tden-ths of a minor unit); the remainder this
 *       income leaves, (income * tnum + carry) mod tden, carries to the
 *       next one, so over any run of incomes the tithes sum to
 *       floor(sum of incomes * tnum / tden) exactly. income
 *       0..TRK_TOTAL_MAX.
 *   TK3 A STREAM PAYMENT ALLOWED. One period's payment may be posted when
 *       due is 1..TRK_AMOUNT_MAX (libzxv ZXV_AMOUNT_MAX, 2^53 - 1), the
 *       paying account not below zero after it (after = balance - due),
 *       the owner's per-day cap for streams not passed (room = cap -
 *       streamed today - due), the stream not paying past its total
 *       (left = total - paid - due), each of the three 0..TRK_MINOR_MAX
 *       (18 digits), and the owner's approval standing (approved exactly
 *       1).
 *
 * Anything else is refused (TRK_REFUSED), never rounded or wrapped. */
#ifndef TRICKLE_RULES_H
#define TRICKLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TRK_REFUSED    INT64_MIN
#define TRK_TOTAL_MAX  INT64_C(999999999999)       /* 9(12) minor units */
#define TRK_TICKS_MAX  INT64_C(99999)              /* 9(5) periods, or a tithe's tden */
#define TRK_AMOUNT_MAX INT64_C(9007199254740991)   /* 2^53 - 1, libzxv ZXV_AMOUNT_MAX */
#define TRK_MINOR_MAX  INT64_C(999999999999999999) /* 18 digits, Sutra's s9(18) */

/* TK1: floor(total * tick / ticks) - paid, or TRK_REFUSED. */
static inline int64_t trk_stream_due(int64_t total, int64_t tick, int64_t ticks, int64_t paid)
{
    if (total < 0 || total > TRK_TOTAL_MAX || ticks < 1 || ticks > TRK_TICKS_MAX || tick < 0 ||
        tick > ticks || paid < 0)
        return TRK_REFUSED;
    int64_t owed = total * tick / ticks; /* total * tick below 10^17 */
    if (paid > owed) return TRK_REFUSED;
    return owed - paid;
}

/* TK2: the checks both tithe rules share. */
static inline bool trk_tithe_in(int64_t income, int64_t tnum, int64_t tden, int64_t carry)
{
    return income >= 0 && income <= TRK_TOTAL_MAX && tden >= 1 && tden <= TRK_TICKS_MAX &&
           tnum >= 0 && tnum <= tden && carry >= 0 && carry < tden;
}

/* TK2: floor((income * tnum + carry) / tden), or TRK_REFUSED. */
static inline int64_t trk_tithe_due(int64_t income, int64_t tnum, int64_t tden, int64_t carry)
{
    if (!trk_tithe_in(income, tnum, tden, carry)) return TRK_REFUSED;
    return (income * tnum + carry) / tden; /* below 10^17 before the division */
}

/* TK2: (income * tnum + carry) mod tden, the remainder carried on, or
 * TRK_REFUSED. */
static inline int64_t trk_tithe_carry(int64_t income, int64_t tnum, int64_t tden, int64_t carry)
{
    if (!trk_tithe_in(income, tnum, tden, carry)) return TRK_REFUSED;
    return (income * tnum + carry) % tden;
}

/* TK3: each field inside its own range. */
static inline bool trk_stream_pay_ok(int64_t due, int64_t after, int64_t room, int64_t left,
                                     int64_t approved)
{
    return due >= 1 && due <= TRK_AMOUNT_MAX && after >= 0 && after <= TRK_MINOR_MAX && room >= 0 &&
           room <= TRK_MINOR_MAX && left >= 0 && left <= TRK_MINOR_MAX && approved == 1;
}

#endif
