/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_ladder.c — concurrent allotment pots. See allot_ladder.h. */
#include "allot_ladder.h"
#include "pay_util.h"

uint64_t allot_ladder_threshold(uint32_t j, uint32_t n, uint32_t idx_lo, uint32_t idx_hi)
{
    if (n == 0 || n > ALLOT_LADDER_MAX || j >= n) return 0;
    if (idx_lo < ALLOT_FIB_MIN || idx_hi > ALLOT_FIB_MAX || idx_lo > idx_hi) return 0;
    if (n == 1) return allot_fib(idx_lo);
    uint64_t rem, span = idx_hi - idx_lo, steps = n - 1;
    uint64_t q = pay_udiv64((uint64_t) j * span, steps, &rem);
    uint32_t i = idx_lo + (uint32_t) q;
    uint64_t lo = allot_fib(i);
    if (rem == 0) return lo;
    uint64_t gap = allot_fib(i + 1) - lo, add = 0;
    (void) pay_muldiv(gap, rem, steps, &add, 0); /* gap * rem / steps < gap */
    return lo + add;
}

allot_status_t allot_ladder_init(allot_ladder_t *L, const allot_config_t *base, uint32_t n,
                                   uint32_t idx_lo, uint32_t idx_hi, allot_t *levels,
                                   allot_range_t *ranges, uint32_t per_level,
                                   allot_tally_t *tallies, uint32_t tally_per_level,
                                   const uint64_t *weights)
{
    if (!L || !base || !levels || !ranges || per_level == 0) return ALLOT_ERR_ARG;
    if (allot_ladder_threshold(0, n, idx_lo, idx_hi) == 0) return ALLOT_ERR_CONFIG;
    uint64_t wsum = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint64_t w = weights ? weights[k] : 1;
        if (!pay_add_ok(wsum, w, &wsum)) return ALLOT_ERR_CONFIG;
    }
    if (wsum == 0) return ALLOT_ERR_CONFIG;
    pay_memset(L, 0, sizeof *L);
    L->n = n;
    L->lv = levels;
    L->luck_levels = ALLOT_LADDER_LUCK_LEVELS; /* W2 */
    for (uint32_t k = 0; k < n; k++) {
        allot_config_t c = *base;
        c.threshold = allot_ladder_threshold(k, n, idx_lo, idx_hi);
        allot_status_t st =
            allot_init(&levels[k], &c, 1, 0, ranges + (size_t) k * per_level, per_level);
        if (st == ALLOT_OK && tallies)
            st = allot_set_tally(&levels[k], tallies + (size_t) k * tally_per_level,
                                  tally_per_level);
        if (st != ALLOT_OK) return st;
        L->weight[k] = weights ? weights[k] : 1;
    }
    return ALLOT_OK;
}

bool allot_ladder_ready(const allot_ladder_t *L, uint32_t k)
{
    if (!L || k >= L->n) return false;
    const allot_t *r = &L->lv[k];
    return r->phase == ALLOT_OPEN && r->n_parties > 0;
}

allot_status_t allot_ladder_plan_span(const allot_ladder_t *L,
                                        const uint8_t owner[ALLOT_ID_LEN], uint64_t fee,
                                        const uint64_t *weights, const allot_rate_t *luck,
                                        uint32_t lo, uint32_t hi, allot_ladder_receipt_t *plan)
{
    uint64_t wsum = 0, given = 0, carry = 0;
    allot_status_t st;
    if (!L || !owner || !plan || L->n == 0) return ALLOT_ERR_ARG;
    if (lo > hi || hi >= L->n) return ALLOT_ERR_CONFIG; /* P5 */
    for (uint32_t k = 0; k < L->n; k++)                  /* L1: every level must be switched on */
        if (!L->lv[k].cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (fee == 0) return ALLOT_ERR_DUST;
    const uint64_t *w = weights ? weights : L->weight;
    for (uint32_t k = lo; k <= hi; k++)
        if (!pay_add_ok(wsum, w[k], &wsum)) return ALLOT_ERR_CONFIG;
    if (wsum == 0) return ALLOT_ERR_CONFIG;

    pay_memset(plan, 0, sizeof *plan);
    plan->fee = fee;
    int32_t top = -1;
    for (uint32_t k = lo; k <= hi; k++) { /* P5: nothing outside [lo, hi] */
        uint64_t s = 0;
        if (w[k]) (void) pay_muldiv(fee, w[k], wsum, &s, 0); /* P1: s <= fee */
        given += s;
        uint64_t want = s + carry, take = 0;
        if (allot_ladder_ready(L, k)) { /* P2 */
            uint64_t room = allot_room(&L->lv[k], owner);
            if (luck && k < L->luck_levels && room != UINT64_MAX) /* W1: room in fee units */
                (void) pay_muldiv(room, luck->den, luck->num, &room, 0);
            take = want < room ? want : room; /* P2: the excess over the cap moves up */
            top = (int32_t) k;
        }
        plan->share[k] = take;
        plan->cascaded += s > take ? s - take : 0;
        carry = want - take;
    }
    if (top < 0) return ALLOT_ERR_PHASE;
    /* P2: floor remainders and what cascaded past the top go to the highest
     * ready level (of the span). Cascade already counted: only the remainder is new. */
    plan->share[top] += carry + (fee - given);
    for (uint32_t k = 0; k < L->n; k++) { /* P3: check everything first */
        if (!plan->share[k]) continue;
        plan->ballots[k] = luck && k < L->luck_levels
                               ? allot_luck_ballots(&L->lv[k].cfg, plan->share[k], *luck)
                               : plan->share[k];
        if (plan->ballots[k] == 0) return ALLOT_ERR_RATE; /* a weight outside [1, cap] */
        st = allot_mint_check(&L->lv[k], owner, plan->share[k], plan->ballots[k]);
        if (st != ALLOT_OK) return st;
        plan->first[k] = L->lv[k].ballots;
    }
    return ALLOT_OK;
}

allot_status_t allot_ladder_plan_fee(const allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                       uint64_t fee, const uint64_t *weights,
                                       allot_ladder_receipt_t *plan)
{
    if (!L || L->n == 0) return ALLOT_ERR_ARG;
    return allot_ladder_plan_span(L, owner, fee, weights, 0, 0, L->n - 1, plan);
}

allot_status_t allot_ladder_plan(const allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                   uint64_t amount, const allot_rate_t *rate,
                                   const uint64_t *weights, allot_ladder_receipt_t *plan)
{
    uint64_t fee, n;
    if (!L || !owner || !plan || L->n == 0) return ALLOT_ERR_ARG;
    for (uint32_t k = 0; k < L->n; k++) /* L1 before anything else */
        if (!L->lv[k].cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (L->lv[0].cfg.mode != ALLOT_MODE_PAID) return ALLOT_ERR_MODE; /* M1 */
    allot_status_t st = allot_quote(&L->lv[0].cfg, amount, rate, &fee, &n);
    if (st != ALLOT_OK) return st;
    return allot_ladder_plan_fee(L, owner, fee, weights, plan);
}

static allot_status_t mint_plan(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                 uint64_t amount, const allot_rate_t *rate,
                                 allot_ladder_receipt_t *plan, allot_ladder_receipt_t *out);

allot_status_t allot_ladder_enter(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                    uint64_t amount, const allot_rate_t *rate,
                                    const uint64_t *weights, allot_ladder_receipt_t *out)
{
    allot_ladder_receipt_t plan;
    allot_status_t st = allot_ladder_plan(L, owner, amount, rate, weights, &plan);
    if (st != ALLOT_OK) return st;
    return mint_plan(L, owner, amount, rate, &plan, out);
}

allot_status_t allot_ladder_enter_fee(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                        uint64_t amount, const allot_rate_t *rate, uint64_t fee,
                                        const uint64_t *weights, allot_ladder_receipt_t *out)
{
    allot_ladder_receipt_t plan;
    if (!rate || rate->den == 0) return ALLOT_ERR_ARG;
    allot_status_t st = allot_ladder_plan_fee(L, owner, fee, weights, &plan);
    if (st != ALLOT_OK) return st;
    return mint_plan(L, owner, amount, rate, &plan, out);
}

allot_status_t allot_ladder_enter_span(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                         uint64_t amount, const allot_rate_t *rate, uint64_t fee,
                                         const allot_rate_t *luck, const uint64_t *weights,
                                         uint32_t lo, uint32_t hi, allot_ladder_receipt_t *out)
{
    allot_ladder_receipt_t plan;
    uint64_t n;
    if (!L || !rate || rate->den == 0 || L->n == 0) return ALLOT_ERR_ARG;
    if (fee == 0) { /* quote it, as allot_ladder_enter does */
        for (uint32_t k = 0; k < L->n; k++)
            if (!L->lv[k].cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
        if (L->lv[0].cfg.mode != ALLOT_MODE_PAID) return ALLOT_ERR_MODE; /* M1 */
        allot_status_t q = allot_quote(&L->lv[0].cfg, amount, rate, &fee, &n);
        if (q != ALLOT_OK) return q;
    }
    allot_status_t st = allot_ladder_plan_span(L, owner, fee, weights, luck, lo, hi, &plan);
    if (st != ALLOT_OK) return st;
    return mint_plan(L, owner, amount, rate, &plan, out);
}

static allot_status_t mint_plan(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                 uint64_t amount, const allot_rate_t *rate,
                                 allot_ladder_receipt_t *plan, allot_ladder_receipt_t *out)
{
    allot_status_t st;
    for (uint32_t k = 0; k < L->n; k++) {
        if (!plan->share[k]) continue;
        allot_receipt_t rc;
        st = plan->ballots[k] == plan->share[k]
                 ? allot_enter_share(&L->lv[k], owner, amount, rate, plan->share[k], &rc)
                 : allot_enter_weighted(&L->lv[k], owner, amount, rate, plan->share[k],
                                         plan->ballots[k], &rc);
        if (st != ALLOT_OK) return st; /* unreachable: checked in the plan */
        plan->closed += rc.closed_epoch ? 1u : 0u;
    }
    L->payments++;
    L->fees += plan->fee;
    L->cascaded += plan->cascaded;
    if (out) pay_memcpy(out, plan, sizeof *plan);
    return ALLOT_OK;
}
