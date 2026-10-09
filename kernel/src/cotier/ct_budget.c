/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_budget.c — tier-3 budget controller and storage inequality (see
 * ct_budget.h). */
#include "ct_budget.h"

static zt_fx sat32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (zt_fx) v;
}

bool ct_budget_init(ct_budget_t *b, const ct_budget_cfg_t *cfg)
{
    if (!b || !cfg || cfg->levels < 2 || cfg->levels > CT_BUDGET_MAX_LEVELS || cfg->N < 2 ||
        cfg->delta < 0 || cfg->delta > ZT_ONE || cfg->eta < 0 || cfg->h_low >= cfg->h_high ||
        !cfg->batch_min || cfg->batch_min > cfg->batch_max || !cfg->ctx_min ||
        cfg->ctx_min > cfg->ctx_max || cfg->ctx_max >= (1u << 27))
        return false;
    b->cfg = *cfg;
    b->Q = cfg->Q0 < 0 ? 0 : cfg->Q0;
    b->lnN = zt_ln((uint64_t) cfg->N << 16);
    b->C = 0;
    b->h = b->h_prev = b->h_min = ct_budget_headroom(b, 0);
    b->level = b->above = b->tick = 0;
    b->batch = cfg->batch_max;
    b->ctx = cfg->ctx_max;
    b->accel_on = true;
    b->changes = b->reversals = b->last_dir = 0;
    for (uint32_t l = 0; l < CT_BUDGET_MAX_LEVELS; l++) b->relief[l] = 0;
    b->c_at_change = 0;
    b->since_change = 2;
    b->from_level = 0;
    b->commits = b->refusals = 0;
    return true;
}

/* n / d in Q16, clamped to [0, cap], for any 64-bit n and d > 0. */
static uint64_t ratio_q16(uint64_t n, uint64_t d, uint64_t cap)
{
    if (!d) return cap;
    while (n >= ((uint64_t) 1 << 47)) {
        n >>= 1;
        d >>= 1;
    }
    if (!d) return cap;
    uint64_t r = zt_udiv64(n << 16, d, 0);
    return r > cap ? cap : r;
}

zt_fx ct_cost(const ct_budget_cfg_t *cfg, const ct_metrics_t *m)
{
    if (!cfg || !m) return 0;
    int64_t c = 0;
    if ((m->present & CT_M_WATTS) && cfg->watts_ref_mw) {
        uint64_t r = ratio_q16(m->watts_mw, cfg->watts_ref_mw, 4u * ZT_ONE);
        c += ((int64_t) cfg->w_watts * (int64_t) r) >> 16;
    }
    if ((m->present & CT_M_STALL) && m->total_cycles) {
        uint64_t s = m->stall_cycles > m->total_cycles ? m->total_cycles : m->stall_cycles;
        uint64_t r = ratio_q16(s, m->total_cycles, ZT_ONE);
        c += ((int64_t) cfg->w_stall * (int64_t) r) >> 16;
    }
    if ((m->present & CT_M_TEMP) && cfg->headroom_ref_mc > 0) {
        uint64_t load;
        if (m->headroom_mc <= 0)
            load = ZT_ONE;
        else if (m->headroom_mc >= cfg->headroom_ref_mc)
            load = 0;
        else
            load = ZT_ONE -
                   ratio_q16((uint64_t) m->headroom_mc, (uint64_t) cfg->headroom_ref_mc, ZT_ONE);
        c += ((int64_t) cfg->w_temp * (int64_t) load) >> 16;
    }
    return sat32(c);
}

zt_fx ct_budget_headroom(const ct_budget_t *b, zt_fx C)
{
    int64_t ceil = ((int64_t) b->cfg.eta * b->lnN) >> 16;
    int64_t decay = (b->Q >> 16) * b->cfg.delta + (((b->Q & 0xFFFF) * b->cfg.delta) >> 16);
    return sat32(ceil - decay - C);
}

static void outputs(ct_budget_t *b)
{
    const ct_budget_cfg_t *c = &b->cfg;
    uint32_t l = b->level, top = c->levels - 1u;
    uint32_t bt = l >= 32 ? 0 : c->batch_max >> l;
    b->batch = bt < c->batch_min ? c->batch_min : bt;
    b->ctx = c->ctx_max - ((c->ctx_max - c->ctx_min) * l) / top; /* < 2^27 * 16: 32 bits */
    b->accel_on = l < top;
}

static void move(ct_budget_t *b, uint32_t to)
{
    if (to == b->level) return;
    uint32_t dir = to > b->level ? 1u : 2u;
    if (b->last_dir && dir != b->last_dir) b->reversals++;
    b->last_dir = dir;
    b->changes++;
    b->c_at_change = b->C;
    b->since_change = 0;
    b->from_level = b->level;
    b->level = to;
    b->above = 0;
    outputs(b);
}

zt_fx ct_budget_step(ct_budget_t *b, ct_metrics_fn fn, void *ctx, zt_fx S)
{
    ct_metrics_t m;
    m.present = 0;
    m.watts_mw = 0;
    m.stall_cycles = m.total_cycles = 0;
    m.headroom_mc = 0;
    zt_fx C = 0;
    if (fn && fn(ctx, &m)) C = ct_cost(&b->cfg, &m);
    if (C < 0) C = 0;
    b->C = C;
    zt_fx h = ct_budget_headroom(b, C);
    b->since_change++;
    /* Extrapolate only when h_t and h_{t-1} were both measured at the current
     * level: the step our own last change caused is not a trend. */
    int64_t pred = b->tick && b->since_change >= 2u ? (int64_t) h + ((int64_t) h - b->h_prev) : h;
    uint32_t top = b->cfg.levels - 1u;
    /* learn the relief of the last throttle, split over the levels it crossed */
    if (b->last_dir == 1u && b->since_change <= b->cfg.hold && b->level > b->from_level) {
        int64_t r = (int64_t) b->c_at_change - C;
        if (b->level - b->from_level == 2u) r /= 2; /* by a constant: no helper */
        for (uint32_t l = b->from_level; l < b->level; l++)
            if (r > b->relief[l]) b->relief[l] = (zt_fx) r;
    }
    if (h < b->cfg.h_low || pred < b->cfg.h_low) {
        uint32_t step = h < 0 ? 2u : 1u;
        move(b, b->level + step > top ? top : b->level + step);
        b->above = 0;
    } else if (h > b->cfg.h_high && pred > b->cfg.h_high && b->level > 0 &&
               (int64_t) h - b->relief[b->level - 1u] >=
                   b->cfg.h_low + (((int64_t) b->cfg.h_high - b->cfg.h_low) >> 1)) {
        if (++b->above >= b->cfg.hold) move(b, b->level - 1u);
    } else {
        b->above = 0;
    }
    /* Q_{t+1} = (1 - delta) Q_t + eta S_t - C_t, never below 0. */
    int64_t decay = (b->Q >> 16) * b->cfg.delta + (((b->Q & 0xFFFF) * b->cfg.delta) >> 16);
    int64_t q = b->Q - decay + (((int64_t) b->cfg.eta * S) >> 16) - C;
    b->Q = q < 0 ? 0 : q;
    b->h_prev = h;
    b->h = h;
    if (h < b->h_min) b->h_min = h;
    b->tick++;
    return h;
}

bool ct_storage_ok(const ct_budget_t *b, zt_fx F)
{
    int64_t decay = (b->Q >> 16) * b->cfg.delta + (((b->Q & 0xFFFF) * b->cfg.delta) >> 16);
    return decay + b->C <= (((int64_t) b->cfg.eta * F) >> 16);
}

int32_t ct_budget_commit(ct_budget_t *b, zt_fx F, ct_commit_fn fn, void *ctx, const void *rec,
                         uint32_t len)
{
    if (!b || !fn) return CT_COMMIT_EARG;
    if (!ct_storage_ok(b, F)) {
        b->refusals++;
        return CT_COMMIT_REFUSED;
    }
    int32_t r = fn(ctx, rec, len);
    if (r == 0) b->commits++;
    return r;
}
