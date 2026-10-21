/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* crown_rules.h — Crown's rules in C, the reference its Eve, Sutra and
 * Trebago blocks are checked against (blocks/crown). Pure integer
 * functions, no state: the model (crown_model.c) and the language test
 * (tests/crown_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   CW1 GOOD STANDING. grants.eval answers the reason bits an appeal has
 *       not cleared (uptime 1, dropped 2, routing 4, audit 8, fraud 16,
 *       volume 32, stake 64, invalid 128; grants.h G2). A member is in
 *       good standing for a grant exactly when no such bit is set: each
 *       bit is a flag, and every flag must be 0. A flag that is not 0 is
 *       never read as clear.
 *   CW2 A GRANT SPLIT BY WEIGHT. units whole units (0..CROWN_NINE) go to n
 *       recipients (1..CROWN_ROWS) by weight (each at least 0, summing to
 *       1..CROWN_NINE): recipient i gets floor(units * w_i / W) and the
 *       units left over go one each to the largest remainders
 *       (units * w_i mod W), a tie to the earlier recipient, exactly as
 *       grants.h G3 splits a grant's barrels. The grants sum to units.
 *   CW3 THE DRAW ORDER. From a published seed (0..CROWN_NINE, Sutra's
 *       9(9)) and a member count (1..CROWN_MEMBERS_MAX), the member drawn
 *       at place step (0..members - 1) is (seed + step * CROWN_STRIDE) mod
 *       members. CROWN_STRIDE is a prime above every member count, so the
 *       places reach every member once: anyone holding the seed can check
 *       the order. No hashing: integer modular steps only.
 *   CW4 THE POOL AFTER EACH EPOCH. opening + the running sum of
 *       (inflow - granted) through each epoch, exact; an amount below 0, a
 *       pool that would go below 0 (grants are paid only from the pool's
 *       balance) or a sum past 64 bits is refused.
 *
 * Anything else is refused (CROWN_REFUSED), never rounded or wrapped. */
#ifndef CROWN_RULES_H
#define CROWN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define CROWN_REFUSED     (-1)
#define CROWN_NINE        INT64_C(999999999) /* Sutra's 9(9) */
#define CROWN_ROWS        64u                /* recipients or epochs in one call */
#define CROWN_MEMBERS_MAX INT64_C(999999)    /* Sutra's 9(6) */
#define CROWN_STRIDE      INT64_C(1000003)   /* a prime above CROWN_MEMBERS_MAX */
#define CROWN_REASONS     8u                 /* reason bits grants.eval answers */

/* CW1: every reason flag 0. */
static inline bool crown_standing(const int64_t flag[CROWN_REASONS])
{
    for (uint32_t i = 0; i < CROWN_REASONS; i++)
        if (flag[i] != 0) return false;
    return true;
}

/* CW1: grants.eval's mask as its eight flags, bit k into flag[k]. */
static inline void crown_reason_flags(uint32_t mask, int64_t flag[CROWN_REASONS])
{
    for (uint32_t i = 0; i < CROWN_REASONS; i++) flag[i] = (int64_t) ((mask >> i) & 1u);
}

/* CW2: share[i] for each recipient, with extra[i] (the largest remainder's
 * 1, or 0) and *total (the sum of the weights) as allot.sutra takes them;
 * 0, or CROWN_REFUSED with nothing written. */
static inline int crown_allot(int64_t units, const int64_t *weight, uint32_t n, int64_t *share,
                              int64_t *extra, int64_t *total)
{
    if (!weight || !share || !extra || !total || n == 0 || n > CROWN_ROWS) return CROWN_REFUSED;
    if (units < 0 || units > CROWN_NINE) return CROWN_REFUSED;
    int64_t w = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (weight[i] < 0 || weight[i] > CROWN_NINE) return CROWN_REFUSED;
        w += weight[i]; /* at most 64 * 9(9): fits */
    }
    if (w == 0 || w > CROWN_NINE) return CROWN_REFUSED;
    int64_t floor_[CROWN_ROWS], rem[CROWN_ROWS], given = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t x = units * weight[i]; /* at most 9(9) * 9(9): fits */
        floor_[i] = x / w;
        rem[i] = x % w;
        given += floor_[i];
    }
    int64_t left = units - given; /* below n */
    for (uint32_t i = 0; i < n; i++) {
        int64_t rank = 0;
        for (uint32_t j = 0; j < n; j++)
            if (rem[j] > rem[i] || (rem[j] == rem[i] && j < i)) rank++;
        extra[i] = rank < left ? 1 : 0;
        share[i] = floor_[i] + extra[i];
    }
    *total = w;
    return 0;
}

/* CW3: the member drawn at place step, or CROWN_REFUSED. */
static inline int64_t crown_draw_at(int64_t seed, int64_t members, int64_t step)
{
    if (seed < 0 || seed > CROWN_NINE || members < 1 || members > CROWN_MEMBERS_MAX || step < 0 ||
        step >= members)
        return CROWN_REFUSED;
    return (seed + step * CROWN_STRIDE) % members; /* below 2^41: fits */
}

/* CW4: pool[i] after epoch i, or CROWN_REFUSED with nothing written. */
static inline int crown_pool_after(int64_t opening, const int64_t *inflow, const int64_t *granted,
                                   uint32_t n, int64_t *pool)
{
    if (!inflow || !granted || !pool || n == 0 || n > CROWN_ROWS || opening < 0)
        return CROWN_REFUSED;
    int64_t p = opening, out[CROWN_ROWS];
    for (uint32_t i = 0; i < n; i++) {
        if (inflow[i] < 0 || granted[i] < 0) return CROWN_REFUSED;
        if (p > INT64_MAX - inflow[i]) return CROWN_REFUSED;
        p += inflow[i];
        if (granted[i] > p) return CROWN_REFUSED;
        p -= granted[i];
        out[i] = p;
    }
    for (uint32_t i = 0; i < n; i++) pool[i] = out[i];
    return 0;
}

#endif
