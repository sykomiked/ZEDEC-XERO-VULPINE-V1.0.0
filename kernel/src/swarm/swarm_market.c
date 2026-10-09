/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_market.c — free market for swarm tokens. See swarm_market.h. */
#include "swarm_market.h"

#define SWARM_MAX_MONEY ((uint64_t)1 << 62)

bool swarm_cap_is_crown(swarm_cap_t f) {
    return f == SWARM_CAP_SOCIAL || f == SWARM_CAP_NATURAL ||
           f == SWARM_CAP_CULTURAL || f == SWARM_CAP_SPIRITUAL;
}

static bool is_value_form(swarm_cap_t f) {
    return f == SWARM_CAP_MANUFACTURED || f == SWARM_CAP_INTELLECTUAL ||
           f == SWARM_CAP_HUMAN || f == SWARM_CAP_SYSTEM;
}

static swarm_trader_t *find(swarm_market_t *m, uint32_t model_id) {
    for (uint32_t i = 0; i < m->n; i++)
        if (m->t[i].model_id == model_id) return &m->t[i];
    return 0;
}

const swarm_trader_t *swarm_market_trader(const swarm_market_t *m, uint32_t model_id) {
    if (!m) return 0;
    for (uint32_t i = 0; i < m->n; i++)
        if (m->t[i].model_id == model_id) return &m->t[i];
    return 0;
}

void swarm_market_init(swarm_market_t *m) {
    if (!m) return;
    m->n = 0;
    m->money_supply = 0;
    m->pot = 0;
    m->last_floor = m->last_market = m->last_unsold = 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        swarm_trader_t *t = &m->t[i];
        t->model_id = 0;
        for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++) t->cap[f] = 0;
        t->bid = t->won = t->paid = t->value_cycle = t->social_cycle = 0;
    }
}

swarm_status_t swarm_market_join(swarm_market_t *m, uint32_t model_id, uint64_t endowment) {
    if (!m) return SWARM_ERR_ARG;
    if (find(m, model_id)) return SWARM_ERR_DUPLICATE;
    if (m->n >= SWARM_MAX_MODELS) return SWARM_ERR_FULL;
    if (endowment > SWARM_MAX_MONEY - m->money_supply) return SWARM_ERR_ARG;
    swarm_trader_t *t = &m->t[m->n++];
    t->model_id = model_id;
    t->cap[SWARM_CAP_FINANCIAL] = endowment;
    m->money_supply += endowment;
    return SWARM_OK;
}

swarm_status_t swarm_market_bid(swarm_market_t *m, uint32_t model_id, uint64_t amount) {
    if (!m) return SWARM_ERR_ARG;
    swarm_trader_t *t = find(m, model_id);
    if (!t) return SWARM_ERR_NO_MODEL;
    if (amount > t->cap[SWARM_CAP_FINANCIAL]) return SWARM_ERR_ARG;   /* no debt */
    t->bid = amount;
    return SWARM_OK;
}

uint64_t swarm_capped_split(uint64_t total, const uint64_t *w, uint32_t n,
                            uint64_t cap, uint64_t *out) {
    bool     fixed[SWARM_MAX_MODELS];
    uint64_t ww[SWARM_MAX_MODELS];
    uint64_t share[SWARM_MAX_MODELS];
    uint64_t remaining = total;
    for (uint32_t i = 0; i < n; i++) { fixed[i] = false; out[i] = 0; }

    for (;;) {                                   /* water-filling, <= n rounds */
        uint64_t any = 0;
        for (uint32_t i = 0; i < n; i++) { ww[i] = fixed[i] ? 0 : w[i]; any += ww[i]; }
        if (any == 0) return remaining;          /* nobody left to take it */
        swarm_split_lr(remaining, ww, n, share);
        bool capped = false;
        for (uint32_t i = 0; i < n; i++) {
            if (fixed[i] || ww[i] == 0 || share[i] <= cap) continue;
            out[i]    = cap;
            fixed[i]  = true;
            remaining -= cap;
            capped    = true;
        }
        if (!capped) {
            for (uint32_t i = 0; i < n; i++) if (!fixed[i]) out[i] = share[i];
            return 0;
        }
    }
}

swarm_status_t swarm_market_begin_cycle(swarm_budget_t *b, swarm_market_t *m,
                                        swarm_emotion_state_t *emo) {
    if (!b || !m) return SWARM_ERR_ARG;

    uint64_t imag   = emo ? swarm_emotion_pool(b, emo) : 0;                       /* M1 */
    uint64_t real   = b->tokens_per_cycle - imag;
    uint64_t floor_ = swarm_muldiv(real, SWARM_MKT_FLOOR_NUM, SWARM_MKT_DEN, 0);  /* M2 */
    uint64_t market = real - floor_;                                               /* M3 */

    uint64_t bids[SWARM_MAX_MODELS];
    uint64_t won[SWARM_MAX_MODELS];
    for (uint32_t i = 0; i < b->num_slots; i++) {
        const swarm_trader_t *t = swarm_market_trader(m, b->slots[i].model_id);
        bids[i] = (b->slots[i].active && t) ? t->bid : 0;
    }
    uint64_t cap    = swarm_muldiv(market, SWARM_MKT_FLOOR_NUM, SWARM_MKT_DEN, 0); /* M4 */
    uint64_t unsold = swarm_capped_split(market, bids, b->num_slots, cap, won);

    swarm_status_t st = swarm_budget_begin_cycle_real(b, floor_ + unsold);
    if (st != SWARM_OK) return st;

    for (uint32_t i = 0; i < b->num_slots; i++) {
        b->slots[i].allotted_mk  = won[i];
        b->slots[i].allotted    += won[i];
    }
    for (uint32_t k = 0; k < m->n; k++) { m->t[k].won = 0; m->t[k].paid = 0; }
    for (uint32_t i = 0; i < b->num_slots; i++) {                                  /* M5 */
        swarm_trader_t *t = find(m, b->slots[i].model_id);
        if (!t) continue;
        t->won = won[i];
        if (won[i] > 0) {
            t->cap[SWARM_CAP_FINANCIAL] -= t->bid;
            t->paid  = t->bid;
            m->pot  += t->bid;
        }
    }
    for (uint32_t k = 0; k < m->n; k++) m->t[k].bid = 0;

    if (emo) swarm_emotion_apply(b, emo, imag);

    m->last_floor  = floor_ + unsold;
    m->last_market = market;
    m->last_unsold = unsold;
    return SWARM_OK;
}

swarm_status_t swarm_market_credit(swarm_market_t *m, uint32_t model_id,
                                   swarm_cap_t form, uint64_t amount) {
    if (!m || form >= SWARM_CAP_COUNT || form == SWARM_CAP_FINANCIAL) return SWARM_ERR_ARG;
    swarm_trader_t *t = find(m, model_id);
    if (!t) return SWARM_ERR_NO_MODEL;
    t->cap[form] += amount;
    if (is_value_form(form))      t->value_cycle  += amount;
    if (form == SWARM_CAP_SOCIAL) t->social_cycle += amount;
    return SWARM_OK;
}

void swarm_market_credit_frugality(swarm_market_t *m, const swarm_budget_t *b) {
    if (!m || !b || !b->cycle_open) return;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        swarm_trader_t *t = find(m, b->slots[i].model_id);
        if (t) t->cap[SWARM_CAP_NATURAL] += b->slots[i].allotted - b->slots[i].used;
    }
}

static void pay_by(swarm_market_t *m, uint64_t amount, int which) {
    uint64_t w[SWARM_MAX_MODELS];
    uint64_t share[SWARM_MAX_MODELS];
    for (uint32_t k = 0; k < m->n; k++)
        w[k] = which == 0 ? m->t[k].value_cycle : which == 1 ? m->t[k].social_cycle
                                                            : m->t[k].paid;
    swarm_split_lr(amount, w, m->n, share);
    for (uint32_t k = 0; k < m->n; k++) m->t[k].cap[SWARM_CAP_FINANCIAL] += share[k];
}

static void wealth_cap(swarm_market_t *m) {                                        /* M7 */
    if (m->n == 0) return;
    uint64_t M = m->money_supply;
    uint64_t cap = swarm_muldiv(M, SWARM_MKT_FLOOR_NUM, SWARM_MKT_DEN, 0);
    uint64_t r;
    uint64_t equal = swarm_muldiv(M, 1, m->n, &r) + (r ? 1u : 0u);   /* ceil(M / n) */
    if (equal > cap) cap = equal;

    uint64_t excess = 0;
    for (uint32_t k = 0; k < m->n; k++) {
        uint64_t *f = &m->t[k].cap[SWARM_CAP_FINANCIAL];
        if (*f > cap) { excess += *f - cap; *f = cap; }
    }
    while (excess > 0) {
        uint64_t ones[SWARM_MAX_MODELS];
        uint64_t share[SWARM_MAX_MODELS];
        uint32_t under = 0;
        for (uint32_t k = 0; k < m->n; k++) {
            ones[k] = m->t[k].cap[SWARM_CAP_FINANCIAL] < cap ? 1u : 0u;
            under  += (uint32_t)ones[k];
        }
        if (under == 0) break;               /* unreachable: cap * n >= M */
        swarm_split_lr(excess, ones, m->n, share);
        excess = 0;
        for (uint32_t k = 0; k < m->n; k++) {
            uint64_t *f = &m->t[k].cap[SWARM_CAP_FINANCIAL];
            *f += share[k];
            if (*f > cap) { excess += *f - cap; *f = cap; }
        }
    }
}

swarm_status_t swarm_market_settle(swarm_market_t *m) {
    if (!m) return SWARM_ERR_ARG;
    uint64_t value = 0, social = 0;
    for (uint32_t k = 0; k < m->n; k++) {
        value  += m->t[k].value_cycle;
        social += m->t[k].social_cycle;
    }
    uint64_t income = swarm_muldiv(m->pot, SWARM_MKT_INCOME_NUM, SWARM_MKT_DEN, 0);  /* M6 */
    uint64_t divid  = m->pot - income;
    if (value && social) {
        pay_by(m, income, 0);
        pay_by(m, divid, 1);
    } else if (value) {
        pay_by(m, m->pot, 0);
    } else if (social) {
        pay_by(m, m->pot, 1);
    } else {
        pay_by(m, m->pot, 2);                                                       /* refund */
    }
    m->pot = 0;
    wealth_cap(m);
    for (uint32_t k = 0; k < m->n; k++) {
        m->t[k].value_cycle = m->t[k].social_cycle = 0;
        m->t[k].won = m->t[k].paid = 0;
    }
    return SWARM_OK;
}

bool swarm_market_conserved(const swarm_market_t *m) {
    if (!m) return false;
    uint64_t sum = m->pot;
    for (uint32_t k = 0; k < m->n; k++) sum += m->t[k].cap[SWARM_CAP_FINANCIAL];
    return sum == m->money_supply;
}
