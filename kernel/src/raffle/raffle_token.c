/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle_token.c — the raffle block on token manifests. See raffle_token.h. */
#include "raffle_token.h"
#include "pay_util.h"
#include "keccak.h"

static const uint8_t MAGIC[4] = {'Z', 'X', 'R', 'T'};

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t) v;
        v >>= 8;
    }
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static bool rate_zero(raffle_rate_t r)
{
    return r.num == 0 && r.den == 0;
}

static bool rate_eq(raffle_rate_t a, raffle_rate_t b)
{
    return a.num == b.num && a.den == b.den;
}

/* (0, 1/2] */
static bool slice_ok(raffle_rate_t r)
{
    const raffle_rate_t ceil = {RAFFLE_RATE_CEIL_NUM, RAFFLE_RATE_CEIL_DEN};
    return r.den != 0 && r.num != 0 && raffle_rate_cmp(r, ceil) <= 0;
}

void raffle_token_block_default(raffle_token_block_t *b)
{
    if (b) pay_memset(b, 0, sizeof *b);
}

raffle_status_t raffle_token_block_check(const raffle_token_block_t *b)
{
    if (!b) return RAFFLE_ERR_ARG;
    if (b->events & (uint8_t) ~RAFFLE_TOKEN_EV_KNOWN) return RAFFLE_ERR_EVENT; /* T2 */
    if (b->target > RAFFLE_TOKEN_TARGET_LOCAL) return RAFFLE_ERR_MANIFEST;
    if (b->events & RAFFLE_TOKEN_EV_SLICES) { /* T3 */
        if (!slice_ok(b->slice_min) || !slice_ok(b->slice_max) ||
            raffle_rate_cmp(b->slice_min, b->slice_max) > 0)
            return RAFFLE_ERR_MANIFEST;
    } else if (!rate_zero(b->slice_min) || !rate_zero(b->slice_max)) {
        return RAFFLE_ERR_MANIFEST;
    }
    if (b->events & RAFFLE_TOKEN_EV_SAVINGS) { /* T6: (0, 1] */
        if (b->savings_max.den == 0 || b->savings_max.num == 0 ||
            b->savings_max.num > b->savings_max.den)
            return RAFFLE_ERR_MANIFEST;
    } else if (!rate_zero(b->savings_max)) {
        return RAFFLE_ERR_MANIFEST;
    }
    if (b->local_fib_index != 0 &&
        (b->target != RAFFLE_TOKEN_TARGET_LOCAL || b->local_fib_index < RAFFLE_FIB_MIN ||
         b->local_fib_index > RAFFLE_FIB_MAX))
        return RAFFLE_ERR_MANIFEST;
    if (b->tier_lo || b->tier_hi) { /* T8 */
        if (b->target != RAFFLE_TOKEN_TARGET_NETWORK || b->tier_lo == 0 ||
            b->tier_lo > b->tier_hi || b->tier_hi > RAFFLE_LADDER_MAX)
            return RAFFLE_ERR_MANIFEST;
    }
    if (!rate_zero(b->nav)) { /* T7 */
        if (b->target != RAFFLE_TOKEN_TARGET_LOCAL || b->nav.den == 0 || b->nav.num == 0 ||
            b->nav.num >= b->nav.den)
            return RAFFLE_ERR_MANIFEST;
    }
    if (b->events == 0 && (b->target != 0 || b->local_fib_index != 0 || b->tier_lo || b->tier_hi ||
                           !rate_zero(b->nav)))
        return RAFFLE_ERR_MANIFEST;
    return RAFFLE_OK;
}

raffle_status_t raffle_token_encode(const raffle_token_block_t *b,
                                    uint8_t out[RAFFLE_TOKEN_BLOCK_LEN])
{
    if (!out) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_token_block_check(b);
    if (st != RAFFLE_OK) return st;
    pay_memset(out, 0, RAFFLE_TOKEN_BLOCK_LEN);
    pay_memcpy(out, MAGIC, 4);
    out[4] = RAFFLE_TOKEN_VERSION;
    out[5] = b->events;
    out[6] = b->target;
    put64(out + 8, b->slice_min.num);
    put64(out + 16, b->slice_min.den);
    put64(out + 24, b->slice_max.num);
    put64(out + 32, b->slice_max.den);
    put64(out + 40, b->savings_max.num);
    put64(out + 48, b->savings_max.den);
    for (int i = 0; i < 4; i++) out[56 + i] = (uint8_t) (b->local_fib_index >> (24 - 8 * i));
    out[60] = b->tier_lo;
    out[61] = b->tier_hi;
    put64(out + 64, b->nav.num);
    put64(out + 72, b->nav.den);
    return RAFFLE_OK;
}

raffle_status_t raffle_token_decode(const uint8_t *in, size_t len, raffle_token_block_t *b)
{
    raffle_token_block_t t;
    if (!in || !b) return RAFFLE_ERR_ARG;
    if (len != RAFFLE_TOKEN_BLOCK_LEN || !pay_memeq(in, MAGIC, 4) ||
        in[4] != RAFFLE_TOKEN_VERSION || in[7] != 0 || in[62] || in[63])
        return RAFFLE_ERR_MANIFEST;
    t.events = in[5];
    t.target = in[6];
    t.slice_min.num = get64(in + 8);
    t.slice_min.den = get64(in + 16);
    t.slice_max.num = get64(in + 24);
    t.slice_max.den = get64(in + 32);
    t.savings_max.num = get64(in + 40);
    t.savings_max.den = get64(in + 48);
    t.local_fib_index = 0;
    for (int i = 0; i < 4; i++) t.local_fib_index = (t.local_fib_index << 8) | in[56 + i];
    t.tier_lo = in[60];
    t.tier_hi = in[61];
    t.nav.num = get64(in + 64);
    t.nav.den = get64(in + 72);
    raffle_status_t st = raffle_token_block_check(&t);
    if (st != RAFFLE_OK) return st;
    pay_memcpy(b, &t, sizeof t);
    return RAFFLE_OK;
}

raffle_status_t raffle_token_digest(const raffle_token_block_t *b, uint8_t out[RAFFLE_HASH_LEN])
{
    static const char TAG[] = "ZXV-RAFFLE-TOKEN1";
    uint8_t buf[sizeof TAG - 1 + RAFFLE_TOKEN_BLOCK_LEN];
    if (!out) return RAFFLE_ERR_ARG;
    pay_memcpy(buf, TAG, sizeof TAG - 1);
    raffle_status_t st = raffle_token_encode(b, buf + sizeof TAG - 1);
    if (st != RAFFLE_OK) return st;
    sha3_256(buf, sizeof buf, out);
    return RAFFLE_OK;
}

raffle_status_t raffle_token_config(const raffle_config_t *dep, const raffle_token_block_t *b,
                                    raffle_config_t *out)
{
    if (!dep || !b || !out) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_config_check(dep);
    if (st == RAFFLE_OK) st = raffle_token_block_check(b);
    if (st != RAFFLE_OK) return st;
    if (!dep->jurisdiction_permits) return RAFFLE_ERR_DISABLED; /* T5 */
    if (b->events == 0) return RAFFLE_ERR_EVENT;
    raffle_config_t c;
    pay_memcpy(&c, dep, sizeof c);
    if (b->events & RAFFLE_TOKEN_EV_SLICES) { /* T3 */
        if (raffle_rate_cmp(b->slice_min, c.min_rate) > 0) c.min_rate = b->slice_min;
        if (raffle_rate_cmp(b->slice_max, c.max_rate) < 0) c.max_rate = b->slice_max;
        if (raffle_rate_cmp(c.min_rate, c.max_rate) > 0) return RAFFLE_ERR_RATE;
    }
    if (b->target == RAFFLE_TOKEN_TARGET_LOCAL && b->local_fib_index) { /* T4 */
        c.fib_index = b->local_fib_index;
        c.threshold = 0;
    }
    c.nav_num = b->target == RAFFLE_TOKEN_TARGET_LOCAL ? b->nav.num : 0; /* T7 */
    c.nav_den = b->target == RAFFLE_TOKEN_TARGET_LOCAL ? b->nav.den : 0;
    if (raffle_config_check(&c) != RAFFLE_OK) return RAFFLE_ERR_CONFIG;
    pay_memcpy(out, &c, sizeof c);
    return RAFFLE_OK;
}

raffle_status_t raffle_token_span(const raffle_token_block_t *b, uint32_t n, uint32_t *lo,
                                  uint32_t *hi)
{
    if (!b || !lo || !hi || n == 0) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_token_block_check(b);
    if (st != RAFFLE_OK) return st;
    if (b->target != RAFFLE_TOKEN_TARGET_NETWORK) return RAFFLE_ERR_MANIFEST;
    if (b->tier_lo == 0) { /* every level */
        *lo = 0;
        *hi = n - 1;
        return RAFFLE_OK;
    }
    if (b->tier_hi > n) return RAFFLE_ERR_CONFIG;
    *lo = b->tier_lo - 1u;
    *hi = b->tier_hi - 1u;
    return RAFFLE_OK;
}

raffle_status_t raffle_token_pot_check(const raffle_config_t *dep, const raffle_token_block_t *b,
                                       const raffle_t *r)
{
    raffle_config_t c;
    if (!r) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_token_config(dep, b, &c);
    if (st != RAFFLE_OK) return st;
    if (b->target != RAFFLE_TOKEN_TARGET_LOCAL) return RAFFLE_ERR_MANIFEST;
    const raffle_config_t *p = &r->cfg;
    if (p->jurisdiction_permits != c.jurisdiction_permits || p->free_entry != c.free_entry ||
        !rate_eq(p->min_rate, c.min_rate) || !rate_eq(p->max_rate, c.max_rate) ||
        p->skim_num != c.skim_num || p->skim_den != c.skim_den || p->grant_num != c.grant_num ||
        p->grant_den != c.grant_den || p->fib_index != c.fib_index || p->threshold != c.threshold ||
        p->min_reveals != c.min_reveals || p->share_num != c.share_num ||
        p->share_den != c.share_den || p->nav_num != c.nav_num || p->nav_den != c.nav_den)
        return RAFFLE_ERR_CONFIG;
    return RAFFLE_OK;
}

raffle_status_t raffle_token_set_rate(const raffle_config_t *dep, const raffle_token_block_t *b,
                                      raffle_player_t *pl, raffle_rate_t rate)
{
    raffle_config_t c;
    if (!pl) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_token_config(dep, b, &c);
    if (st != RAFFLE_OK) return st;
    return raffle_player_set_rate(&c, pl, rate);
}

raffle_status_t raffle_token_check(const raffle_guard_policy_t *pol, const raffle_config_t *dep,
                                   const raffle_token_block_t *b, uint8_t event,
                                   const raffle_player_t *pl, uint64_t amount, uint64_t now,
                                   uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee)
{
    raffle_config_t c;
    if (!pol || !pl || !fee) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_token_config(dep, b, &c);
    if (st != RAFFLE_OK) return st;
    /* T2: exactly one slice event, and the block has it on */
    if ((event != RAFFLE_TOKEN_EV_MINT && event != RAFFLE_TOKEN_EV_BURN &&
         event != RAFFLE_TOKEN_EV_TRANSFER) ||
        !(b->events & event))
        return RAFFLE_ERR_EVENT;
    return raffle_guard_check(pol, &c, pl, amount, now, epoch, confirmed_fee, fee);
}

/* T5/T6: deployment on, block valid with SAVINGS. */
static raffle_status_t savings_on(const raffle_config_t *dep, const raffle_token_block_t *b)
{
    if (!dep || !b) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_config_check(dep);
    if (st == RAFFLE_OK) st = raffle_token_block_check(b);
    if (st != RAFFLE_OK) return st;
    if (!dep->jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (!(b->events & RAFFLE_TOKEN_EV_SAVINGS)) return RAFFLE_ERR_EVENT;
    return RAFFLE_OK;
}

/* T6: s in (0, savings_max]. */
static bool share_ok(const raffle_token_block_t *b, raffle_rate_t s)
{
    return s.den != 0 && s.num != 0 && raffle_rate_cmp(s, b->savings_max) <= 0;
}

raffle_status_t raffle_token_set_share(const raffle_guard_policy_t *pol, const raffle_config_t *dep,
                                       const raffle_token_block_t *b, raffle_player_t *pl,
                                       raffle_rate_t share, raffle_rate_t confirmed_share)
{
    if (!pol || !pl || share.den == 0) return RAFFLE_ERR_ARG;
    if (share.num == 0) { /* G1: leaving is always allowed, even when switched off */
        pl->rate = share;
        return RAFFLE_OK;
    }
    raffle_status_t st = savings_on(dep, b);
    if (st != RAFFLE_OK) return st;
    if (!share_ok(b, share)) return RAFFLE_ERR_RATE;
    if (raffle_rate_cmp(share, pol->confirm_above) > 0 && !rate_eq(share, confirmed_share))
        return RAFFLE_ERR_CONFIRM; /* G2, once at enrolment */
    pl->rate = share;
    return RAFFLE_OK;
}

raffle_status_t raffle_token_savings_check(const raffle_guard_policy_t *pol,
                                           const raffle_config_t *dep,
                                           const raffle_token_block_t *b, const raffle_player_t *pl,
                                           uint64_t realised_profit, uint64_t now, uint64_t epoch,
                                           uint64_t *fee)
{
    uint64_t f = 0;
    if (!pol || !pl || !fee) return RAFFLE_ERR_ARG;
    raffle_status_t st = savings_on(dep, b);
    if (st != RAFFLE_OK) return st;
    if (pl->rate.num != 0) {
        /* a later manifest may have lowered savings_max: the old share stops */
        if (!share_ok(b, pl->rate)) return RAFFLE_ERR_RATE;
        (void) pay_muldiv(realised_profit, pl->rate.num, pl->rate.den, &f, 0); /* f <= profit */
    }
    st = raffle_guard_check_fee(pol, dep, pl, f, now, epoch);
    if (st != RAFFLE_OK) return st;
    *fee = f;
    return RAFFLE_OK;
}
