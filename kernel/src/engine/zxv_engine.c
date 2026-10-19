/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_engine.c — the libzxv facade. Design and guarantees: zxv_engine.h. */
#include "zxv_engine.h"
#include "../mlkem/keccak.h"

#define ENGINE_OWNER        0u
#define ENGINE_BUCKET_OWNER 0xFFFFFF00u
#define LEG_MAX             ((uint64_t) 1 << 59) /* pay_ledger's per-line limit */

/* ---- posting identifiers: SHAKE256(seed || tag || seq) ---- */
static void derive(const zxv_engine_t *e, uint8_t tag, uint64_t seq, uint8_t *out, size_t n)
{
    uint8_t in[32 + 1 + 8];
    pay_memcpy(in, e->seed, 32);
    in[32] = tag;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (seq >> (8 * i));
    shake256(in, sizeof in, out, n);
}

static void uetr_of(const zxv_engine_t *e, uint64_t seq, char out[PAY_UETR_LEN + 1])
{
    uint8_t rnd[16];
    derive(e, 'U', seq, rnd, sizeof rnd);
    pay_uetr_from_random(rnd, out);
}

/* A fresh request for posting number e->seq (the caller bumps seq when the
 * posting succeeds, so a refused posting's ids are reused). */
static void new_request(const zxv_engine_t *e, pay_posting_req_t *req, uint64_t tick, uint8_t kind)
{
    pay_memset(req, 0, sizeof *req);
    derive(e, 'I', e->seq, req->idem_key, sizeof req->idem_key);
    uetr_of(e, e->seq, req->uetr);
    pay_w w;
    pay_w_init(&w, req->e2e, sizeof req->e2e);
    pay_w_s(&w, "ZXV-");
    pay_w_u64(&w, e->seq);
    pay_w_finish(&w);
    pay_strlcpy(req->memo, "libzxv", sizeof req->memo);
    req->initiator = ENGINE_OWNER;
    req->tick = tick;
    req->attestor = ENGINE_OWNER;
    req->kind = kind;
    req->ext_cap = PAY_CAP_FINANCIAL;
}

/* ---- lifecycle ---- */
zxv_status_t zxv_engine_init(zxv_engine_t *e, const char *alpha, uint16_t numeric, uint8_t minor,
                             const uint8_t seed[32])
{
    if (!e || !alpha || !seed) return ZXV_E_ARG;
    pay_memset(e, 0, sizeof *e);
    pay_memcpy(e->seed, seed, 32);
    pay_ledger_init(&e->L, 0);
    pay_status_t st = numeric
                          ? pay_ledger_add_fiat(&e->L, alpha, numeric, minor, &e->asset)
                          : pay_ledger_add_asset(&e->L, alpha, minor, PAY_ASSET_UNIT, &e->asset);
    if (st != PAY_OK) return ZXV_E_ARG;
    if (pay_ledger_open(&e->L, ENGINE_OWNER, e->asset, PAY_CAP_FINANCIAL,
                        PAY_ACCT_ISSUER | PAY_ACCT_EXTERNAL, &e->issuer) != PAY_OK)
        return ZXV_E_LEDGER;
    for (uint32_t b = 0; b < PAY_ASSURE_BUCKETS; b++)
        if (pay_ledger_open(&e->L, ENGINE_BUCKET_OWNER + b, e->asset, PAY_CAP_FINANCIAL,
                            PAY_ACCT_COMMONS, &e->bucket[b]) != PAY_OK)
            return ZXV_E_LEDGER;
    return ZXV_E_OK;
}

zxv_status_t zxv_engine_add_member(zxv_engine_t *e, uint32_t *member)
{
    if (!e || !member) return ZXV_E_ARG;
    if (e->n_members >= ZXV_ENGINE_MAX_MEMBERS) return ZXV_E_FULL;
    uint32_t acct;
    if (pay_ledger_open(&e->L, e->n_members + 1u, e->asset, PAY_CAP_FINANCIAL, 0, &acct) != PAY_OK)
        return ZXV_E_FULL;
    e->acct[e->n_members] = acct;
    *member = e->n_members++;
    return ZXV_E_OK;
}

static uint64_t debit_of(const zxv_engine_t *e, uint32_t acct)
{
    const pay_account_t *a = pay_ledger_account(&e->L, acct);
    return a ? a->debit : 0;
}

uint64_t zxv_engine_balance(const zxv_engine_t *e, uint32_t member)
{
    if (!e || member >= e->n_members) return 0;
    return debit_of(e, e->acct[member]);
}

uint64_t zxv_engine_bucket(const zxv_engine_t *e, uint32_t b)
{
    if (!e || b >= PAY_ASSURE_BUCKETS) return 0;
    return debit_of(e, e->bucket[b]);
}

zxv_status_t zxv_engine_fund(zxv_engine_t *e, uint32_t member, uint64_t amount, uint64_t tick)
{
    if (!e || member >= e->n_members || amount == 0 || amount > LEG_MAX) return ZXV_E_ARG;
    pay_posting_req_t req;
    pay_receipt_t rc;
    new_request(e, &req, tick, PAY_KIND_ISSUE);
    if (pay_ledger_issue(&e->L, &req, e->issuer, e->acct[member], amount, &rc) != PAY_OK)
        return ZXV_E_OVERFLOW;
    e->seq++;
    return ZXV_E_OK;
}

zxv_status_t zxv_engine_withdraw(zxv_engine_t *e, uint32_t member, uint64_t amount, uint64_t tick)
{
    if (!e || member >= e->n_members || amount == 0 || amount > LEG_MAX) return ZXV_E_ARG;
    if (zxv_engine_balance(e, member) < amount) return ZXV_E_FUNDS;
    pay_posting_req_t req;
    pay_receipt_t rc;
    new_request(e, &req, tick, PAY_KIND_REDEEM);
    if (pay_ledger_redeem(&e->L, &req, e->acct[member], e->issuer, amount, &rc) != PAY_OK)
        return ZXV_E_LEDGER;
    e->seq++;
    return ZXV_E_OK;
}

zxv_status_t zxv_engine_add_corridor(zxv_engine_t *e, const zxv_corridor_t *c)
{
    if (!e || !c || c->from >= e->n_members || c->to >= e->n_members || c->from == c->to ||
        c->fee_ppm > ZXV_FEE_PPM_MAX)
        return ZXV_E_ARG;
    if (e->n_cor >= ZXV_ENGINE_MAX_CORRIDORS) return ZXV_E_FULL;
    e->cor[e->n_cor++] = *c;
    return ZXV_E_OK;
}

bool zxv_engine_check(const zxv_engine_t *e)
{
    if (!e || !pay_ledger_check(&e->L)) return false;
    const pay_account_t *is = pay_ledger_account(&e->L, e->issuer);
    if (!is) return false;
    uint64_t held = 0;
    for (uint32_t m = 0; m < e->n_members; m++)
        if (!pay_add_ok(held, zxv_engine_balance(e, m), &held)) return false;
    for (uint32_t b = 0; b < PAY_ASSURE_BUCKETS; b++)
        if (!pay_add_ok(held, zxv_engine_bucket(e, b), &held)) return false;
    return held == is->credit;
}

/* ---- 1: compress ---- */
static bool nets(uint32_t members, const zxv_obligation_t *o, uint32_t n, int64_t *net)
{
    for (uint32_t m = 0; m < members; m++) net[m] = 0;
    for (uint32_t i = 0; i < n; i++)
        if (__builtin_sub_overflow(net[o[i].from], (int64_t) o[i].amount, &net[o[i].from]) ||
            __builtin_add_overflow(net[o[i].to], (int64_t) o[i].amount, &net[o[i].to]))
            return false;
    return true;
}

zxv_status_t zxv_compress(zxv_engine_t *e, const zxv_obligation_t *in, uint32_t n,
                          zxv_obligation_t *out, uint32_t cap, uint32_t *n_out)
{
    if (!e || (!in && n) || !out || !n_out || n > AB_MAX_OBLIGATIONS) return ZXV_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (in[i].from >= e->n_members || in[i].to >= e->n_members || in[i].from == in[i].to ||
            in[i].amount == 0 || in[i].amount > LEG_MAX)
            return ZXV_E_ARG;
    int64_t before[ZXV_ENGINE_MAX_MEMBERS], after[ZXV_ENGINE_MAX_MEMBERS];
    if (!nets(e->n_members, in, n, before)) return ZXV_E_OVERFLOW;

    abacus_t *a = &e->ab;
    smaug_init(a);
    for (uint32_t m = 0; m < e->n_members; m++) {
        char name[8] = {'m', 0};
        pay_w w;
        pay_w_init(&w, name + 1, sizeof name - 1);
        pay_w_u64(&w, m);
        pay_w_finish(&w);
        if (smaug_add_member(a, name) != (int32_t) m) return ZXV_E_FULL;
    }
    for (uint32_t i = 0; i < n; i++)
        if (!smaug_owe(a, in[i].from, in[i].to, rat_from_int((int64_t) in[i].amount), 0))
            return ZXV_E_FULL;
    (void) smaug_clear(a);
    if (a->clear_failed) return ZXV_E_OVERFLOW;
    if (a->num_obl > cap) return ZXV_E_FULL;

    /* G1, checked rather than trusted: same net position for every member */
    zxv_obligation_t tmp[AB_MAX_MEMBERS];
    uint32_t k = 0;
    for (uint32_t i = 0; i < a->num_obl; i++) {
        const ab_obligation_t *o = &a->obl[i];
        if (!o->active) continue;
        if (!o->amount.valid || o->amount.den != 1 || o->amount.num <= 0 || k >= AB_MAX_MEMBERS)
            return ZXV_E_OVERFLOW;
        tmp[k].from = o->from;
        tmp[k].to = o->to;
        tmp[k].amount = (uint64_t) o->amount.num;
        k++;
    }
    if (k > cap || !nets(e->n_members, tmp, k, after)) return ZXV_E_OVERFLOW;
    for (uint32_t m = 0; m < e->n_members; m++)
        if (before[m] != after[m]) return ZXV_E_OVERFLOW;
    for (uint32_t i = 0; i < k; i++) out[i] = tmp[i];
    *n_out = k;
    return ZXV_E_OK;
}

/* ---- 2: route ---- */
zxv_status_t zxv_route(const zxv_engine_t *e, uint32_t src, uint32_t dst, uint64_t amount,
                       uint32_t *path, uint32_t cap, uint32_t *n_path, uint64_t *cost_ppm)
{
    if (!e || !path || !n_path || src >= e->n_members || dst >= e->n_members || cap == 0)
        return ZXV_E_ARG;
    uint32_t n = e->n_members;
    uint64_t cost[ZXV_ENGINE_MAX_MEMBERS];
    uint32_t hops[ZXV_ENGINE_MAX_MEMBERS], prev[ZXV_ENGINE_MAX_MEMBERS];
    bool done[ZXV_ENGINE_MAX_MEMBERS], seen[ZXV_ENGINE_MAX_MEMBERS];
    for (uint32_t i = 0; i < n; i++) {
        seen[i] = done[i] = false;
        cost[i] = 0;
        hops[i] = 0;
        prev[i] = i;
    }
    seen[src] = true;
    for (;;) {
        /* the closest unfinished member: least cost, then fewest hops, then
         * lowest id (the scan order) */
        uint32_t u = n;
        for (uint32_t i = 0; i < n; i++)
            if (seen[i] && !done[i] &&
                (u == n || cost[i] < cost[u] || (cost[i] == cost[u] && hops[i] < hops[u])))
                u = i;
        if (u == n || u == dst) break;
        done[u] = true;
        for (uint32_t c = 0; c < e->n_cor; c++) {
            const zxv_corridor_t *k = &e->cor[c];
            if (k->from != u || k->capacity < amount || done[k->to]) continue;
            uint64_t nc = cost[u] + k->fee_ppm; /* <= 64 hops * 10^6: no overflow */
            uint32_t nh = hops[u] + 1u;
            uint32_t v = k->to;
            if (!seen[v] || nc < cost[v] || (nc == cost[v] && nh < hops[v]) ||
                (nc == cost[v] && nh == hops[v] && u < prev[v])) {
                seen[v] = true;
                cost[v] = nc;
                hops[v] = nh;
                prev[v] = u;
            }
        }
    }
    if (!seen[dst]) return ZXV_E_NO_ROUTE;
    uint32_t len = hops[dst] + 1u;
    if (len > cap) return ZXV_E_FULL;
    uint32_t v = dst;
    for (uint32_t i = len; i > 0; i--) {
        path[i - 1] = v;
        v = prev[v];
    }
    *n_path = len;
    if (cost_ppm) *cost_ppm = cost[dst];
    return ZXV_E_OK;
}

/* ---- 3: decompress ---- */
zxv_status_t zxv_decompress(zxv_engine_t *e, const zxv_obligation_t *legs, uint32_t n,
                            uint64_t tick, uint64_t *fees_out)
{
    if (fees_out) *fees_out = 0;
    if (!e || (!legs && n) || n > ZXV_ENGINE_MAX_LEGS) return ZXV_E_ARG;

    /* G2 pre-check: every payer covers its legs plus their fees. With a
     * carry the fee on g is at most floor(g * 8889 / 10^7) + 1. */
    uint64_t need[ZXV_ENGINE_MAX_MEMBERS];
    for (uint32_t m = 0; m < e->n_members; m++) need[m] = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zxv_obligation_t *l = &legs[i];
        if (l->from >= e->n_members || l->to >= e->n_members || l->from == l->to ||
            l->amount == 0 || l->amount > LEG_MAX)
            return ZXV_E_ARG;
        uint64_t w;
        if (!pay_add_ok(l->amount, pay_assure_fee(l->amount) + 1u, &w)) return ZXV_E_OVERFLOW;
        /* the payer's posting line carries amount + fee, so that is what
         * must fit the ledger's per-line limit */
        if (w > LEG_MAX) return ZXV_E_ARG;
        if (!pay_add_ok(need[l->from], w, &need[l->from])) return ZXV_E_OVERFLOW;
    }
    for (uint32_t m = 0; m < e->n_members; m++)
        if (need[m] > zxv_engine_balance(e, m)) return ZXV_E_FUNDS;

    uint64_t first = e->seq, fees = 0;
    for (uint32_t i = 0; i < n; i++) {
        pay_posting_req_t req;
        pay_receipt_t rc;
        uint64_t fee = 0;
        new_request(e, &req, tick, PAY_KIND_TRANSFER);
        pay_status_t st = pay_ledger_pay_with_fee(&e->L, &req, e->acct[legs[i].from],
                                                  e->acct[legs[i].to], legs[i].amount, e->bucket, 0,
                                                  e->issuer, e->acct[legs[i].to], 0, &rc, &fee);
        if (st != PAY_OK) {
            /* roll back, newest first: each reversal is an ordinary posting */
            for (uint64_t s = e->seq; s > first; s--) {
                char orig[PAY_UETR_LEN + 1];
                uetr_of(e, s - 1u, orig);
                new_request(e, &req, tick, PAY_KIND_RETURN);
                if (pay_ledger_reverse(&e->L, &req, orig, &rc) != PAY_OK) return ZXV_E_LEDGER;
                e->seq++;
            }
            return ZXV_E_LEDGER;
        }
        e->seq++;
        fees += fee;
    }
    e->fees += fees;
    if (fees_out) *fees_out = fees;
    return ZXV_E_OK;
}
