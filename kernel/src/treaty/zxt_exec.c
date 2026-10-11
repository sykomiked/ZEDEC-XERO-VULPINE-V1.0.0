/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxt_exec.c — run a treaty on the engine (see zxt_exec.h). Integer only. */
#include "zxt_exec.h"

#define TICK_FUND   1u
#define TICK_SETTLE 2u
#define TICK_CLAIM  3u

static void zero_result(zxt_result_t *r)
{
    volatile uint8_t *b = (volatile uint8_t *) r;
    for (uint32_t i = 0; i < (uint32_t) sizeof(*r); i++) b[i] = 0;
}

static int32_t fail(zxt_result_t *r, int32_t st)
{
    zero_result(r);
    r->status = st;
    return st;
}

/* a * b as a 128-bit (hi, lo) pair. */
static void mul_64x64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    *lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* (hi:lo) / d for hi < d, by shift and subtract; *rem gets the remainder. */
static uint64_t div_128x64(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = hi;
    for (int i = 63; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | ((lo >> i) & 1u);
        q <<= 1;
        if (top || r >= d) {
            r -= d;
            q |= 1u;
        }
    }
    *rem = r;
    return q;
}

/* Share `total` by the treaty's weights (they sum to 1, zxt.h R1): floor
 * shares, then the leftover units one each to the largest fractional
 * remainders rem/den, ties to the lowest index. */
static void share_by_weight(const zxt_treaty_t *t, uint64_t total, int64_t *out)
{
    uint64_t rem[ZXT_MAX_PARTIES], given = 0;
    bool bumped[ZXT_MAX_PARTIES];
    for (uint32_t i = 0; i < t->n_party; i++) {
        uint64_t hi, lo;
        mul_64x64(total, t->party[i].w_num, &hi, &lo);
        uint64_t s = div_128x64(hi, lo, t->party[i].w_den, &rem[i]);
        out[i] = (int64_t) s;
        given += s;
        bumped[i] = false;
    }
    for (uint64_t left = total - given; left > 0; left--) {
        uint32_t best = ZXT_MAX_PARTIES;
        for (uint32_t i = 0; i < t->n_party; i++) {
            if (bumped[i]) continue;
            if (best == ZXT_MAX_PARTIES ||
                rem[i] * t->party[best].w_den > rem[best] * t->party[i].w_den)
                best = i;
        }
        bumped[best] = true;
        out[best]++;
    }
}

int32_t zxt_exec(const zxt_treaty_t *t, zxv_engine_t *ws, const uint8_t seed[32], zxt_result_t *r)
{
    zxv_obligation_t legs[AB_MAX_OBLIGATIONS];
    zxv_obligation_t gross[ZXT_MAX_OWES];
    uint32_t member[ZXT_MAX_PARTIES], n_leg = 0;
    zxv_status_t es;
    if (!r) return ZXT_E_ARG;
    if (!t || !ws || !seed) return fail(r, ZXT_E_ARG);
    zxt_status_t vs = zxt_validate(t);
    if (vs != ZXT_OK) return fail(r, vs);
    zero_result(r);
    r->n_party = t->n_party;

    /* net positions from the gross obligations, in PIC S9(18) */
    for (uint32_t j = 0; j < t->n_owe; j++) {
        const zxt_owe_t *o = &t->owe[j];
        if (!zxt_pic_sub(r->net[o->from], o->amount, &r->net[o->from]) ||
            !zxt_pic_add(r->net[o->to], o->amount, &r->net[o->to]))
            return fail(r, ZXT_E_SIZE);
    }

    if ((es = zxv_engine_init(ws, t->asset, 0, 0, seed)) != ZXV_E_OK)
        return fail(r, ZXT_E_EXEC + es);
    for (uint32_t i = 0; i < t->n_party; i++) {
        if ((es = zxv_engine_add_member(ws, &member[i])) != ZXV_E_OK)
            return fail(r, ZXT_E_EXEC + es);
        if (t->party[i].deposit > 0 &&
            (es = zxv_engine_fund(ws, member[i], (uint64_t) t->party[i].deposit, TICK_FUND)) !=
                ZXV_E_OK)
            return fail(r, ZXT_E_EXEC + es);
    }
    for (uint32_t j = 0; j < t->n_owe; j++) {
        gross[j].from = member[t->owe[j].from];
        gross[j].to = member[t->owe[j].to];
        gross[j].amount = (uint64_t) t->owe[j].amount;
    }
    if ((es = zxv_compress(ws, gross, t->n_owe, legs, AB_MAX_OBLIGATIONS, &n_leg)) != ZXV_E_OK)
        return fail(r, ZXT_E_EXEC + es);
    if (n_leg > ZXT_MAX_OWES) return fail(r, ZXT_E_EXEC + ZXV_E_FULL);

    if (t->op == ZXT_OP_SETL && n_leg > 0) {
        uint64_t before = zxv_engine_bucket(ws, PAY_ASSURE_VBILL_DIVIDEND), fees = 0;
        if ((es = zxv_decompress(ws, legs, n_leg, TICK_SETTLE, &fees)) != ZXV_E_OK)
            return fail(r, ZXT_E_EXEC + es);
        r->fees = (int64_t) fees;
        share_by_weight(t, zxv_engine_bucket(ws, PAY_ASSURE_VBILL_DIVIDEND) - before, r->dividend);
    }
    if (t->op == ZXT_OP_SETL)
        for (uint32_t i = 0; i < t->n_party; i++)
            if (t->party[i].claim > 0 &&
                (es = zxv_engine_withdraw(ws, member[i], (uint64_t) t->party[i].claim,
                                          TICK_CLAIM)) != ZXV_E_OK)
                return fail(r, ZXT_E_EXEC + es);
    if (!zxv_engine_check(ws)) return fail(r, ZXT_E_EXEC + ZXV_E_LEDGER);

    for (uint32_t i = 0; i < t->n_party; i++)
        r->balance[i] = (int64_t) zxv_engine_balance(ws, member[i]);
    for (uint32_t k = 0; k < n_leg; k++) {
        uint32_t f = ZXT_MAX_PARTIES, to = ZXT_MAX_PARTIES;
        for (uint32_t i = 0; i < t->n_party; i++) {
            if (member[i] == legs[k].from) f = i;
            if (member[i] == legs[k].to) to = i;
        }
        if (f == ZXT_MAX_PARTIES || to == ZXT_MAX_PARTIES)
            return fail(r, ZXT_E_EXEC + ZXV_E_LEDGER);
        r->leg[k].from = (uint8_t) f;
        r->leg[k].to = (uint8_t) to;
        r->leg[k].amount = (int64_t) legs[k].amount;
    }
    r->n_leg = (uint8_t) n_leg;
    return ZXT_OK;
}

/* ------------------------------------------------------------ result record */

static void put_udigits(uint8_t *d, uint64_t v, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;) {
        d[i] = (uint8_t) ('0' + v % 10u);
        v /= 10u;
    }
}

static void put_signed(uint8_t *d, int64_t v, uint32_t digits)
{
    d[0] = v < 0 ? '-' : '+';
    put_udigits(d + 1, (uint64_t) (v < 0 ? -v : v), digits);
}

zxt_status_t zxt_emit_result(const zxt_result_t *r, uint8_t *rec, uint32_t cap)
{
    if (!r || !rec) return ZXT_E_ARG;
    if (cap < ZXT_RES_LEN) return ZXT_E_CAP;
    if (r->status < -9999 || r->status > 0 || r->where > 99999u || r->n_party > ZXT_MAX_PARTIES ||
        r->n_leg > ZXT_MAX_OWES || !zxt_pic_ok(r->fees))
        return ZXT_E_SIZE;
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++)
        if (!zxt_pic_ok(r->net[i]) || !zxt_pic_ok(r->balance[i]) || !zxt_pic_ok(r->dividend[i]))
            return ZXT_E_SIZE;
    for (uint32_t k = 0; k < ZXT_MAX_OWES; k++)
        if (!zxt_pic_ok(r->leg[k].amount)) return ZXT_E_SIZE;
    for (uint32_t i = 0; i < 4u; i++) rec[i] = (uint8_t) ZXT_RES_MAGIC[i];
    put_signed(rec + 4, r->status, 4u);
    put_udigits(rec + 9, r->where, 5u);
    put_udigits(rec + 14, r->n_party, 2u);
    put_udigits(rec + 16, r->n_leg, 3u);
    put_signed(rec + 19, r->fees, 18u);
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        uint8_t *d = rec + ZXT_RES_HEAD + i * ZXT_RES_PARTY;
        put_signed(d, r->net[i], 18u);
        put_signed(d + 19, r->balance[i], 18u);
        put_signed(d + 38, r->dividend[i], 18u);
    }
    for (uint32_t k = 0; k < ZXT_MAX_OWES; k++) {
        uint8_t *d = rec + ZXT_RES_HEAD + ZXT_MAX_PARTIES * ZXT_RES_PARTY + k * ZXT_RES_LEG;
        bool used = k < r->n_leg;
        put_udigits(d, used ? r->leg[k].from + 1u : 0u, 2u);
        put_udigits(d + 2, used ? r->leg[k].to + 1u : 0u, 2u);
        put_signed(d + 4, r->leg[k].amount, 18u);
    }
    return ZXT_OK;
}
