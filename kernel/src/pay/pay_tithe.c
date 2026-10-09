/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_tithe.c — exact phi-percent tithe and the commons. See pay_tithe.h. */
#include "pay_tithe.h"
#include "swarm_market.h"

/* ===== 192-bit helpers ===== */

static int u192_cmp(const pay_u192 *a, const pay_u192 *b)
{
    for (int i = 2; i >= 0; i--)
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

static void u192_add(pay_u192 *r, const pay_u192 *a, const pay_u192 *b)
{
    uint64_t c = 0;
    for (int i = 0; i < 3; i++) {
        uint64_t s = a->w[i] + c;
        uint64_t c1 = s < c ? 1u : 0u;
        uint64_t t = s + b->w[i];
        uint64_t c2 = t < s ? 1u : 0u;
        r->w[i] = t;
        c = c1 + c2;
    }
}

static void u192_sub(pay_u192 *r, const pay_u192 *a, const pay_u192 *b)
{
    uint64_t br = 0;
    for (int i = 0; i < 3; i++) {
        uint64_t x = a->w[i], y = b->w[i];
        uint64_t d = x - y - br;
        br = (x < y || (x == y && br)) ? 1u : 0u;
        r->w[i] = d;
    }
}

static void u192_shr(pay_u192 *v, unsigned s) /* s in {1, 2} */
{
    v->w[0] = (v->w[0] >> s) | (v->w[1] << (64 - s));
    v->w[1] = (v->w[1] >> s) | (v->w[2] << (64 - s));
    v->w[2] >>= s;
}

static bool u192_zero(const pay_u192 *v)
{
    return (v->w[0] | v->w[1] | v->w[2]) == 0;
}

pay_u128 pay_isqrt192(pay_u192 v)
{
    pay_u192 res = {{0, 0, 0}};
    pay_u192 bit = {{0, 0, (uint64_t) 1 << 62}}; /* 2^190 */
    while (u192_cmp(&bit, &v) > 0) u192_shr(&bit, 2);
    while (!u192_zero(&bit)) {
        pay_u192 t;
        u192_add(&t, &res, &bit);
        if (u192_cmp(&v, &t) >= 0) {
            u192_sub(&v, &v, &t);
            u192_shr(&res, 1);
            u192_add(&res, &res, &bit);
        } else {
            u192_shr(&res, 1);
        }
        u192_shr(&bit, 2);
    }
    pay_u128 r = {res.w[1], res.w[0]};
    return r;
}

pay_u128 pay_floor_a_sqrt5(uint64_t a)
{
    pay_u128 sq = pay_mul64(a, a);
    pay_u192 x = {{sq.lo, sq.hi, 0}};
    pay_u192 x4 = {{sq.lo << 2, (sq.hi << 2) | (sq.lo >> 62), sq.hi >> 62}};
    pay_u192 five;
    u192_add(&five, &x4, &x); /* 5 a^2 < 2^131 */
    return pay_isqrt192(five);
}

uint64_t pay_tithe_phi(uint64_t a)
{
    pay_u128 s = pay_u128_add64(pay_floor_a_sqrt5(a), a);
    pay_u128 q = pay_udiv128_64(s, 200u, 0);
    return q.lo; /* q < 2^58: always fits */
}

/* ===== Policy ===== */

void pay_tithe_policy_default(pay_tithe_policy_t *p)
{
    if (!p) return;
    p->allow_below_phi = false;
    for (int i = 0; i < PAY_UNIT_KIND_COUNT; i++) {
        p->vfv_rate[i].num = 0;
        p->vfv_rate[i].den = 1;
    }
    p->vfv_rate[PAY_UNIT_MONEY].num = 1;
    p->credit_mult.num = 1;
    p->credit_mult.den = 1;
    p->commons_cap_share.num = SWARM_MKT_FLOOR_NUM; /* 8  */
    p->commons_cap_share.den = SWARM_MKT_DEN;       /* 21 */
}

pay_tithe_status_t pay_tithe_compute(const pay_tithe_policy_t *p, pay_unit_kind_t unit,
                                     uint64_t amount, const pay_contrib_t *contrib,
                                     const pay_rat_t *posted_rate, pay_tithe_result_t *out)
{
    if (!p || !out || (unsigned) unit >= PAY_UNIT_KIND_COUNT) return PAY_TITHE_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    out->amount = amount;
    out->tithe = pay_tithe_phi(amount);

    uint64_t want = out->tithe;
    if (contrib) {
        switch (contrib->mode) {
        case PAY_CONTRIB_PHI:
            break;
        case PAY_CONTRIB_RATE:
            if (contrib->rate.den == 0) return out->status = PAY_TITHE_ERR_RATE;
            if (!pay_muldiv(amount, contrib->rate.num, contrib->rate.den, &want, 0))
                return out->status = PAY_TITHE_ERR_OVERFLOW;
            break;
        case PAY_CONTRIB_ABSOLUTE:
            want = contrib->absolute;
            break;
        default:
            return out->status = PAY_TITHE_ERR_ARG;
        }
    }

    if (want < out->tithe) {
        if (p->allow_below_phi) {
            out->contribution = want;
            out->shortfall = out->tithe - want;
        } else {
            out->contribution = out->tithe;
        }
        return out->status = PAY_TITHE_OK;
    }
    out->contribution = want;
    out->excess = want - out->tithe;
    if (out->excess == 0) return out->status = PAY_TITHE_OK;

    pay_rat_t rate = posted_rate ? *posted_rate : p->vfv_rate[unit];
    if (rate.den == 0 || p->credit_mult.den == 0) return out->status = PAY_TITHE_ERR_RATE;
    if (rate.num == 0) return out->status = PAY_TITHE_RATE_UNSET;
    uint64_t v;
    if (!pay_muldiv(out->excess, rate.num, rate.den, &v, 0))
        return out->status = PAY_TITHE_ERR_OVERFLOW;
    if (!pay_muldiv(v, p->credit_mult.num, p->credit_mult.den, &out->vfv_credit, 0))
        return out->status = PAY_TITHE_ERR_OVERFLOW;
    return out->status = PAY_TITHE_OK;
}

/* ===== Commons ===== */

void pay_commons_init(pay_commons_t *c)
{
    if (c) pay_memset(c, 0, sizeof *c);
}

bool pay_commons_deposit(pay_commons_t *c, pay_unit_kind_t unit, uint64_t amount)
{
    uint64_t a, b;
    if (!c || (unsigned) unit >= PAY_UNIT_KIND_COUNT) return false;
    if (!pay_add_ok(c->pool[unit], amount, &a) || !pay_add_ok(c->received[unit], amount, &b))
        return false;
    c->pool[unit] = a;
    c->received[unit] = b;
    return true;
}

bool pay_commons_conserved(const pay_commons_t *c)
{
    for (int i = 0; i < PAY_UNIT_KIND_COUNT; i++)
        if (c->allocated[i] > c->received[i] || c->pool[i] != c->received[i] - c->allocated[i])
            return false;
    return true;
}

uint64_t pay_commons_split(uint64_t total, const uint64_t *w, uint32_t n, pay_rat_t cap,
                           uint64_t *out)
{
    uint32_t nz = 0;
    uint64_t c = 0, eq, r;
    if (!w || !out || n == 0 || n > PAY_COMMONS_MAX_RECIPIENTS) return total;
    for (uint32_t i = 0; i < n; i++)
        if (w[i]) nz++;
    if (nz == 0) {
        for (uint32_t i = 0; i < n; i++) out[i] = 0;
        return total;
    }
    if (cap.den == 0 || !pay_muldiv(total, cap.num, cap.den, &c, 0)) c = total;
    eq = pay_udiv64(total, nz, &r);
    if (r) eq++;
    if (eq > c) c = eq; /* docs/SWARM_ECONOMY.md 6: or an equal share */
    return swarm_capped_split(total, w, n, c, out);
}

bool pay_commons_allocate(pay_commons_t *c, pay_unit_kind_t unit, const uint64_t *w, uint32_t n,
                          pay_rat_t cap, uint64_t *out)
{
    if (!c || (unsigned) unit >= PAY_UNIT_KIND_COUNT) return false;
    uint64_t total = c->pool[unit];
    uint64_t left = pay_commons_split(total, w, n, cap, out);
    uint64_t given = total - left;
    c->pool[unit] = left;
    c->allocated[unit] += given;
    return true;
}
