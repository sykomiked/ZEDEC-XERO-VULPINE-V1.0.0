/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle_ladder.c — concurrent raffle pots. See raffle_ladder.h. */
#include "raffle_ladder.h"
#include "pay_util.h"

uint64_t raffle_ladder_threshold(uint32_t j, uint32_t n, uint32_t idx_lo, uint32_t idx_hi)
{
    if (n == 0 || n > RAFFLE_LADDER_MAX || j >= n) return 0;
    if (idx_lo < RAFFLE_FIB_MIN || idx_hi > RAFFLE_FIB_MAX || idx_lo > idx_hi) return 0;
    if (n == 1) return raffle_fib(idx_lo);
    uint64_t rem, span = idx_hi - idx_lo, steps = n - 1;
    uint64_t q = pay_udiv64((uint64_t) j * span, steps, &rem);
    uint32_t i = idx_lo + (uint32_t) q;
    uint64_t lo = raffle_fib(i);
    if (rem == 0) return lo;
    uint64_t gap = raffle_fib(i + 1) - lo, add = 0;
    (void) pay_muldiv(gap, rem, steps, &add, 0); /* gap * rem / steps < gap */
    return lo + add;
}

raffle_status_t raffle_ladder_init(raffle_ladder_t *L, const raffle_config_t *base, uint32_t n,
                                   uint32_t idx_lo, uint32_t idx_hi, raffle_t *levels,
                                   raffle_range_t *ranges, uint32_t per_level,
                                   raffle_tally_t *tallies, uint32_t tally_per_level,
                                   const uint64_t *weights)
{
    if (!L || !base || !levels || !ranges || per_level == 0) return RAFFLE_ERR_ARG;
    if (raffle_ladder_threshold(0, n, idx_lo, idx_hi) == 0) return RAFFLE_ERR_CONFIG;
    uint64_t wsum = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint64_t w = weights ? weights[k] : 1;
        if (!pay_add_ok(wsum, w, &wsum)) return RAFFLE_ERR_CONFIG;
    }
    if (wsum == 0) return RAFFLE_ERR_CONFIG;
    pay_memset(L, 0, sizeof *L);
    L->n = n;
    L->lv = levels;
    for (uint32_t k = 0; k < n; k++) {
        raffle_config_t c = *base;
        c.threshold = raffle_ladder_threshold(k, n, idx_lo, idx_hi);
        raffle_status_t st =
            raffle_init(&levels[k], &c, 1, 0, ranges + (size_t) k * per_level, per_level);
        if (st == RAFFLE_OK && tallies)
            st = raffle_set_tally(&levels[k], tallies + (size_t) k * tally_per_level,
                                  tally_per_level);
        if (st != RAFFLE_OK) return st;
        L->weight[k] = weights ? weights[k] : 1;
    }
    return RAFFLE_OK;
}

bool raffle_ladder_ready(const raffle_ladder_t *L, uint32_t k)
{
    if (!L || k >= L->n) return false;
    const raffle_t *r = &L->lv[k];
    return r->phase == RAFFLE_OPEN && r->n_parties > 0;
}

raffle_status_t raffle_ladder_plan(const raffle_ladder_t *L, const uint8_t owner[RAFFLE_ID_LEN],
                                   uint64_t amount, const raffle_rate_t *rate,
                                   const uint64_t *weights, raffle_ladder_receipt_t *plan)
{
    uint64_t fee, n, wsum = 0, given = 0, carry = 0;
    if (!L || !owner || !plan || L->n == 0) return RAFFLE_ERR_ARG;
    for (uint32_t k = 0; k < L->n; k++) /* L1: every level must be switched on */
        if (!L->lv[k].cfg.jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    raffle_status_t st = raffle_quote(&L->lv[0].cfg, amount, rate, &fee, &n);
    if (st != RAFFLE_OK) return st;
    if (fee == 0) return RAFFLE_ERR_DUST;
    const uint64_t *w = weights ? weights : L->weight;
    for (uint32_t k = 0; k < L->n; k++)
        if (!pay_add_ok(wsum, w[k], &wsum)) return RAFFLE_ERR_CONFIG;
    if (wsum == 0) return RAFFLE_ERR_CONFIG;

    pay_memset(plan, 0, sizeof *plan);
    plan->fee = fee;
    int32_t top = -1;
    for (uint32_t k = 0; k < L->n; k++) {
        uint64_t s = 0;
        if (w[k]) (void) pay_muldiv(fee, w[k], wsum, &s, 0); /* P1: s <= fee */
        given += s;
        uint64_t want = s + carry, take = 0;
        if (raffle_ladder_ready(L, k)) { /* P2 */
            uint64_t room = raffle_room(&L->lv[k], owner);
            take = want < room ? want : room; /* P2: the excess over the cap moves up */
            top = (int32_t) k;
        }
        plan->share[k] = take;
        plan->cascaded += s > take ? s - take : 0;
        carry = want - take;
    }
    if (top < 0) return RAFFLE_ERR_PHASE;
    /* P2: floor remainders and what cascaded past the top go to the highest
     * ready level. Cascade already counted: only the remainder is new. */
    plan->share[top] += carry + (fee - given);
    for (uint32_t k = 0; k < L->n; k++) { /* P3: check everything first */
        if (!plan->share[k]) continue;
        st = raffle_mint_check(&L->lv[k], owner, plan->share[k], plan->share[k]);
        if (st != RAFFLE_OK) return st;
        plan->first[k] = L->lv[k].tickets;
    }
    return RAFFLE_OK;
}

raffle_status_t raffle_ladder_enter(raffle_ladder_t *L, const uint8_t owner[RAFFLE_ID_LEN],
                                    uint64_t amount, const raffle_rate_t *rate,
                                    const uint64_t *weights, raffle_ladder_receipt_t *out)
{
    raffle_ladder_receipt_t plan;
    raffle_status_t st = raffle_ladder_plan(L, owner, amount, rate, weights, &plan);
    if (st != RAFFLE_OK) return st;
    for (uint32_t k = 0; k < L->n; k++) {
        if (!plan.share[k]) continue;
        raffle_receipt_t rc;
        st = raffle_enter_share(&L->lv[k], owner, amount, rate, plan.share[k], &rc);
        if (st != RAFFLE_OK) return st; /* unreachable: checked in the plan */
        plan.closed += rc.closed_epoch ? 1u : 0u;
    }
    L->payments++;
    L->fees += plan.fee;
    L->cascaded += plan.cascaded;
    if (out) pay_memcpy(out, &plan, sizeof plan);
    return RAFFLE_OK;
}
