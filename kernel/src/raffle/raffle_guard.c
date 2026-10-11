/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle_guard.c — player choice and protection. See raffle_guard.h. */
#include "raffle_guard.h"
#include "pay_util.h"

void raffle_guard_policy_default(raffle_guard_policy_t *p)
{
    if (!p) return;
    pay_memset(p, 0, sizeof *p);
    p->confirm_above.num = 1;
    p->confirm_above.den = 10;
    p->cooling_off = 86400;
    p->day_len = 86400;
    p->require_limits = true;
    p->age_gate = 0; /* G6: fail closed until the deployment sets one */
}

void raffle_player_init(raffle_player_t *pl, const uint8_t owner[RAFFLE_ID_LEN])
{
    if (!pl) return;
    pay_memset(pl, 0, sizeof *pl);
    if (owner) pay_memcpy(pl->owner, owner, RAFFLE_ID_LEN);
    pl->rate.num = 0; /* G1: out */
    pl->rate.den = 1;
}

raffle_status_t raffle_player_set_rate(const raffle_config_t *cfg, raffle_player_t *pl,
                                       raffle_rate_t rate)
{
    uint64_t fee;
    if (!cfg || !pl || rate.den == 0) return RAFFLE_ERR_ARG;
    if (rate.num != 0) {
        raffle_status_t st = raffle_quote(cfg, 0, &rate, &fee, 0); /* range check */
        if (st != RAFFLE_OK) return st;
    }
    pl->rate = rate;
    return RAFFLE_OK;
}

uint64_t raffle_player_day_limit(const raffle_player_t *pl, uint64_t now)
{
    if (!pl) return 0;
    return pl->pend_day && now >= pl->pend_day_at ? pl->pend_day : pl->day_limit;
}

uint64_t raffle_player_epoch_limit(const raffle_player_t *pl, uint64_t now)
{
    if (!pl) return 0;
    return pl->pend_epoch && now >= pl->pend_epoch_at ? pl->pend_epoch : pl->epoch_limit;
}

/* G4 for one limit: first setting or lowering is immediate, raising waits. */
static void set_one(uint64_t *cur, uint64_t *pend, uint64_t *pend_at, uint64_t want, uint64_t now,
                    uint64_t cooling)
{
    if (*pend && now >= *pend_at) { /* a matured raise becomes the limit */
        *cur = *pend;
        *pend = 0;
    }
    if (*cur == 0 || want <= *cur) {
        *cur = want;
        *pend = 0; /* lowering also cancels a queued raise */
    } else {
        *pend = want;
        *pend_at = now > UINT64_MAX - cooling ? UINT64_MAX : now + cooling;
    }
}

raffle_status_t raffle_player_set_limits(const raffle_guard_policy_t *pol, raffle_player_t *pl,
                                         uint64_t day_limit, uint64_t epoch_limit, uint64_t now)
{
    if (!pol || !pl || day_limit == 0 || epoch_limit == 0) return RAFFLE_ERR_ARG;
    set_one(&pl->day_limit, &pl->pend_day, &pl->pend_day_at, day_limit, now, pol->cooling_off);
    set_one(&pl->epoch_limit, &pl->pend_epoch, &pl->pend_epoch_at, epoch_limit, now,
            pol->cooling_off);
    return RAFFLE_OK;
}

void raffle_player_exclude(raffle_player_t *pl, uint64_t now, uint64_t duration)
{
    if (!pl) return;
    uint64_t until = now > UINT64_MAX - duration ? UINT64_MAX : now + duration;
    if (until > pl->excluded_until) pl->excluded_until = until; /* G5: only ever extends */
}

static uint64_t day_of(const raffle_guard_policy_t *pol, uint64_t now)
{
    return pol->day_len ? pay_udiv64(now, pol->day_len, 0) : 0;
}

raffle_status_t raffle_guard_check(const raffle_guard_policy_t *pol, const raffle_config_t *cfg,
                                   const raffle_player_t *pl, uint64_t amount, uint64_t now,
                                   uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee)
{
    uint64_t f, n, total;
    if (!pol || !cfg || !pl || !fee) return RAFFLE_ERR_ARG;
    if (!cfg->jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (pl->rate.num == 0) return RAFFLE_ERR_NOT_IN;               /* G1 */
    if (now < pl->excluded_until) return RAFFLE_ERR_EXCLUDED;      /* G5 */
    if (!pol->age_gate || !pol->age_gate(pol->age_ctx, pl->owner)) /* G6 */
        return RAFFLE_ERR_AGE;
    uint64_t dl = raffle_player_day_limit(pl, now), el = raffle_player_epoch_limit(pl, now);
    if (pol->require_limits && (dl == 0 || el == 0)) return RAFFLE_ERR_NO_LIMITS; /* G3 */
    raffle_status_t st = raffle_quote(cfg, amount, &pl->rate, &f, &n);
    if (st != RAFFLE_OK) return st;
    if (f == 0) return RAFFLE_ERR_DUST;
    uint64_t spent_day = pl->day == day_of(pol, now) ? pl->spent_day : 0;
    uint64_t spent_epoch = pl->epoch == epoch ? pl->spent_epoch : 0;
    if (dl && (!pay_add_ok(spent_day, f, &total) || total > dl)) return RAFFLE_ERR_DAY_LIMIT;
    if (el && (!pay_add_ok(spent_epoch, f, &total) || total > el)) return RAFFLE_ERR_EPOCH_LIMIT;
    *fee = f;
    if (raffle_rate_cmp(pl->rate, pol->confirm_above) > 0 && confirmed_fee != f) /* G2 */
        return RAFFLE_ERR_CONFIRM;
    return RAFFLE_OK;
}

void raffle_guard_commit(const raffle_guard_policy_t *pol, raffle_player_t *pl, uint64_t now,
                         uint64_t epoch, uint64_t fee)
{
    if (!pol || !pl) return;
    uint64_t d = day_of(pol, now);
    if (pl->day != d) {
        pl->day = d;
        pl->spent_day = 0;
    }
    if (pl->epoch != epoch) {
        pl->epoch = epoch;
        pl->spent_epoch = 0;
    }
    if (!pay_add_ok(pl->spent_day, fee, &pl->spent_day)) pl->spent_day = UINT64_MAX;
    if (!pay_add_ok(pl->spent_epoch, fee, &pl->spent_epoch)) pl->spent_epoch = UINT64_MAX;
}
