/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_assure.c — the 0.08889% assurance fee, its exact four-bucket partition,
 * contributions and the commons. See pay_assure.h. */
#include "pay_assure.h"
#include "swarm_market.h"

/* ===== F1-F3: the fee ===== */

_Static_assert(PAY_ASSURE_NUM < PAY_ASSURE_DEN, "the fee is a fraction of the gross");

bool pay_assure_fee_carry(uint64_t rem_in, uint64_t gross, uint64_t *fee, uint64_t *rem_out)
{
    uint64_t r;
    if (rem_in >= PAY_ASSURE_DEN) return false;
    /* g * 8889 < 2^78 and r < 2^24: the 128-bit sum never wraps. */
    pay_u128 x = pay_u128_add64(pay_mul64(gross, PAY_ASSURE_NUM), rem_in);
    pay_u128 q = pay_udiv128_64(x, PAY_ASSURE_DEN, &r);
    if (q.hi) return false; /* unreachable: q <= g */
    if (fee) *fee = q.lo;
    if (rem_out) *rem_out = r;
    return true;
}

uint64_t pay_assure_fee(uint64_t gross)
{
    uint64_t f = 0;
    (void) pay_assure_fee_carry(0, gross, &f, 0);
    return f;
}

bool pay_assure_charge(pay_assure_carry_t *c, uint64_t gross, uint64_t *fee)
{
    uint64_t f, r;
    if (!c || !pay_assure_fee_carry(c->rem, gross, &f, &r)) return false;
    c->rem = r;
    if (fee) *fee = f;
    return true;
}

/* ===== B1: the four buckets ===== */

const uint8_t pay_assure_bucket_pct[PAY_ASSURE_BUCKETS] = {50, 25, 15, 10};
const char *const pay_assure_bucket_name[PAY_ASSURE_BUCKETS] = {
    "reserve floor", "V-Bill dividend pool", "infrastructure/node bounties",
    "regenerative capital"};
_Static_assert(PAY_ASSURE_BUCKETS == 4 && 50 + 25 + 15 + 10 == 100, "four buckets, 100%");

void pay_assure_split(uint64_t fee, uint64_t out[PAY_ASSURE_BUCKETS])
{
    uint64_t rest = fee;
    if (!out) return;
    for (int i = 1; i < PAY_ASSURE_BUCKETS; i++) {
        uint64_t part = 0;
        /* floor(fee * pct / 100) <= fee: fits, so pay_muldiv cannot fail */
        (void) pay_muldiv(fee, pay_assure_bucket_pct[i], 100u, &part, 0);
        out[i] = part;
        rest -= part; /* sum of the three floors <= 50% of fee: never wraps */
    }
    out[PAY_ASSURE_RESERVE_FLOOR] = rest; /* 50% plus every remainder unit */
}

/* ===== Policy ===== */

void pay_assure_policy_default(pay_assure_policy_t *p)
{
    if (!p) return;
    p->allow_below_fee = false;
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

pay_assure_status_t pay_assure_compute(const pay_assure_policy_t *p, pay_unit_kind_t unit,
                                       uint64_t amount, const pay_contrib_t *contrib,
                                       const pay_rat_t *posted_rate, pay_assure_result_t *out)
{
    if (!p || !out || (unsigned) unit >= PAY_UNIT_KIND_COUNT) return PAY_ASSURE_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    out->amount = amount;
    out->fee = pay_assure_fee(amount);

    uint64_t want = out->fee;
    if (contrib) {
        switch (contrib->mode) {
        case PAY_CONTRIB_FEE:
            break;
        case PAY_CONTRIB_RATE:
            if (contrib->rate.den == 0) return out->status = PAY_ASSURE_ERR_RATE;
            if (!pay_muldiv(amount, contrib->rate.num, contrib->rate.den, &want, 0))
                return out->status = PAY_ASSURE_ERR_OVERFLOW;
            break;
        case PAY_CONTRIB_ABSOLUTE:
            want = contrib->absolute;
            break;
        default:
            return out->status = PAY_ASSURE_ERR_ARG;
        }
    }

    if (want < out->fee) {
        if (p->allow_below_fee) {
            out->contribution = want;
            out->shortfall = out->fee - want;
        } else {
            out->contribution = out->fee;
        }
        return out->status = PAY_ASSURE_OK;
    }
    out->contribution = want;
    out->excess = want - out->fee;
    if (out->excess == 0) return out->status = PAY_ASSURE_OK;

    pay_rat_t rate = posted_rate ? *posted_rate : p->vfv_rate[unit];
    if (rate.den == 0 || p->credit_mult.den == 0) return out->status = PAY_ASSURE_ERR_RATE;
    if (rate.num == 0) return out->status = PAY_ASSURE_RATE_UNSET;
    uint64_t v;
    if (!pay_muldiv(out->excess, rate.num, rate.den, &v, 0))
        return out->status = PAY_ASSURE_ERR_OVERFLOW;
    if (!pay_muldiv(v, p->credit_mult.num, p->credit_mult.den, &out->vfv_credit, 0))
        return out->status = PAY_ASSURE_ERR_OVERFLOW;
    return out->status = PAY_ASSURE_OK;
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
