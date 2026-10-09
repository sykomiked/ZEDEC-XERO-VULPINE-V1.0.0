/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_receipt.c — co-signed usage receipts, settlement, disputes and
 * reputation (G5, G6, G8, U4, U5 of prov.h).
 *
 * Receipt body (little-endian, PROV_RECEIPT_BODY = 330 bytes):
 *   u32 version | job_id | provider_id | user_id | desc_hash | fee_schedule |
 *   request_hash | response_hash (7 x 32) | u8 rclass | u8 unit |
 *   u8 sla_outcome | u8 flags | u16 asset | asset_code[12] (NUL padded) |
 *   u64 units, unit_price, gross, sla_credit, fee, net, refund, hold |
 *   u32 latency_ms | u64 start_tick | u64 end_tick
 * Digest = SHA3-256(le32(19) || "zxv-prov-receipt-v1" || body). Both parties
 * sign the digest with ML-DSA-65. */
#include "prov.h"

static uint32_t put_hash(uint8_t *o, uint32_t at, const uint8_t *h)
{
    prov_memcpy(o + at, h, PROV_HASH_LEN);
    return at + PROV_HASH_LEN;
}

int32_t prov_receipt_encode(const prov_receipt_t *r, uint8_t *o, uint32_t cap)
{
    if (!r || !o) return PROV_ERR_ARG;
    if (cap < PROV_RECEIPT_BODY) return PROV_ERR_SPACE;
    uint32_t at = 0;
    prov_le32_put(o, r->version);
    at = 4;
    at = put_hash(o, at, r->job_id);
    at = put_hash(o, at, r->provider_id);
    at = put_hash(o, at, r->user_id);
    at = put_hash(o, at, r->desc_hash);
    at = put_hash(o, at, r->fee_schedule);
    at = put_hash(o, at, r->request_hash);
    at = put_hash(o, at, r->response_hash);
    o[at++] = r->rclass;
    o[at++] = r->unit;
    o[at++] = r->sla_outcome;
    o[at++] = r->flags;
    o[at++] = (uint8_t) r->asset;
    o[at++] = (uint8_t) (r->asset >> 8);
    for (uint32_t i = 0; i < PROV_CODE_MAX; i++) o[at++] = (uint8_t) r->asset_code[i];
    const uint64_t v[8] = {r->units, r->unit_price, r->gross,  r->sla_credit,
                           r->fee,   r->net,        r->refund, r->hold};
    for (uint32_t i = 0; i < 8; i++, at += 8) prov_le64_put(o + at, v[i]);
    prov_le32_put(o + at, r->latency_ms);
    at += 4;
    prov_le64_put(o + at, r->start_tick);
    at += 8;
    prov_le64_put(o + at, r->end_tick);
    at += 8;
    return (int32_t) at;
}

int prov_receipt_decode(const uint8_t *in, uint32_t len, prov_receipt_t *r)
{
    if (!in || !r) return PROV_ERR_ARG;
    if (len != PROV_RECEIPT_BODY) return PROV_ERR_PARSE;
    prov_memset(r, 0, sizeof *r);
    uint32_t at = 4;
    r->version = prov_le32_get(in);
    if (r->version != 1) return PROV_ERR_PARSE;
    uint8_t *hs[7] = {r->job_id,       r->provider_id,  r->user_id,      r->desc_hash,
                      r->fee_schedule, r->request_hash, r->response_hash};
    for (uint32_t i = 0; i < 7; i++, at += PROV_HASH_LEN)
        prov_memcpy(hs[i], in + at, PROV_HASH_LEN);
    r->rclass = in[at++];
    r->unit = in[at++];
    r->sla_outcome = in[at++];
    r->flags = in[at++];
    r->asset = (uint16_t) (in[at] | (in[at + 1] << 8));
    at += 2;
    for (uint32_t i = 0; i < PROV_CODE_MAX; i++) r->asset_code[i] = (char) in[at++];
    if (r->asset_code[PROV_CODE_MAX - 1] != 0) return PROV_ERR_PARSE;
    uint64_t *v[8] = {&r->units, &r->unit_price, &r->gross,  &r->sla_credit,
                      &r->fee,   &r->net,        &r->refund, &r->hold};
    for (uint32_t i = 0; i < 8; i++, at += 8) *v[i] = prov_le64_get(in + at);
    r->latency_ms = prov_le32_get(in + at);
    at += 4;
    r->start_tick = prov_le64_get(in + at);
    at += 8;
    r->end_tick = prov_le64_get(in + at);
    if (r->rclass >= PROV_RC_COUNT || r->unit >= PROV_UNIT_COUNT ||
        r->sla_outcome > PROV_SLA_UNAVAILABLE || (r->flags & ~0x07u))
        return PROV_ERR_PARSE;
    return PROV_OK;
}

void prov_receipt_digest(const prov_receipt_t *r, uint8_t out[PROV_HASH_LEN])
{
    uint8_t body[PROV_RECEIPT_BODY];
    prov_hb_t h;
    prov_hb_init(&h, "zxv-prov-receipt-v1");
    if (prov_receipt_encode(r, body, sizeof body) != (int32_t) PROV_RECEIPT_BODY) h.err = true;
    prov_hb_put(&h, body, sizeof body);
    prov_hb_final(&h, out);
}

/* Arithmetic every receipt must satisfy, with `fee_expected` the fee the
 * schedule gives on (gross - sla_credit). */
static bool money_ok(const prov_receipt_t *r, uint64_t fee_expected)
{
    uint64_t g, t;
    if (!prov_mul_ok(r->units, r->unit_price, &g) || g != r->gross) return false;
    if (r->gross > r->hold || r->sla_credit > r->gross) return false;
    if (r->fee != fee_expected || r->fee > r->gross - r->sla_credit) return false;
    if (r->net != r->gross - r->sla_credit - r->fee) return false;
    t = r->net + r->fee; /* <= gross <= hold: no overflow */
    if (r->refund != r->hold - t) return false;
    if (r->sla_outcome == PROV_SLA_MET && r->sla_credit != 0) return false;
    if (r->end_tick < r->start_tick) return false;
    return true;
}

int prov_receipt_build(const prov_net_t *n, uint32_t fill, const prov_usage_t *u, prov_receipt_t *r)
{
    const prov_fill_t *f = prov_fill(n, fill);
    if (!f || !u || !r) return PROV_ERR_ARG;
    if (f->state != PROV_FILL_RESERVED && f->state != PROV_FILL_DISPUTED) return PROV_ERR_STATE;
    if (u->units > f->qty || u->sla_outcome > PROV_SLA_UNAVAILABLE || u->end_tick < u->start_tick)
        return PROV_ERR_ARG;
    const prov_provider_t *p = prov_provider(n, f->provider);
    const prov_asset_t *a = prov_asset(n, f->asset);
    if (!p || !a || f->offer >= p->desc.n_offers) return PROV_ERR_STATE;
    const prov_offer_t *o = &p->desc.offers[f->offer];
    prov_memset(r, 0, sizeof *r);
    r->version = 1;
    prov_memcpy(r->job_id, f->job_id, PROV_HASH_LEN);
    prov_memcpy(r->provider_id, p->id, PROV_ID_LEN);
    prov_memcpy(r->user_id, n->users[f->user - 1].id, PROV_ID_LEN);
    prov_memcpy(r->desc_hash, f->desc_hash, PROV_HASH_LEN);
    prov_memcpy(r->fee_schedule, n->fee_schedule, PROV_HASH_LEN);
    prov_memcpy(r->request_hash, f->request_hash, PROV_HASH_LEN);
    prov_memcpy(r->response_hash, u->response_hash, PROV_HASH_LEN);
    r->rclass = o->rclass;
    r->unit = o->unit;
    r->sla_outcome = u->sla_outcome;
    r->flags =
        (uint8_t) ((f->no_train ? PROV_RF_NO_TRAIN : 0u) | (f->no_retain ? PROV_RF_NO_RETAIN : 0u) |
                   (prov_is_attested(n, f->provider) ? PROV_RF_ATTESTED : 0u));
    r->asset = f->asset;
    prov_strlcpy(r->asset_code, a->code, sizeof r->asset_code);
    r->units = u->units;
    r->unit_price = f->unit_price;
    r->gross = u->units * f->unit_price; /* <= hold */
    r->hold = f->hold;
    if (u->sla_outcome != PROV_SLA_MET) {
        uint64_t c = 0;
        if (!prov_muldiv(r->gross, p->desc.sla[o->sla].credit_bps, 10000u, &c)) c = 0;
        r->sla_credit = c; /* U5: provider's own declared credit, from the gross */
    }
    r->fee = prov_fee(n, r->gross - r->sla_credit);
    r->net = r->gross - r->sla_credit - r->fee;
    r->refund = r->hold - r->net - r->fee;
    r->latency_ms = u->latency_ms;
    r->start_tick = u->start_tick;
    r->end_tick = u->end_tick;
    return PROV_OK;
}

static const prov_provider_t *find_prov(const prov_net_t *n, const uint8_t id[32], uint32_t *h)
{
    for (uint32_t i = 0; i < PROV_MAX_PROVIDERS; i++)
        if (n->prov[i].used && prov_memeq(n->prov[i].id, id, PROV_ID_LEN)) {
            if (h) *h = i + 1;
            return &n->prov[i];
        }
    return 0;
}

static const prov_user_t *find_user(const prov_net_t *n, const uint8_t id[32], uint32_t *h)
{
    for (uint32_t i = 0; i < PROV_MAX_USERS; i++)
        if (n->users[i].used && prov_memeq(n->users[i].id, id, PROV_ID_LEN)) {
            if (h) *h = i + 1;
            return &n->users[i];
        }
    return 0;
}

static bool sigs_ok(prov_verify_fn vf, void *ctx, const prov_receipt_t *r, const uint8_t *ppk,
                    const uint8_t *upk)
{
    uint8_t dg[PROV_HASH_LEN];
    if (!vf || !r->has_sig_provider || !r->has_sig_user) return false;
    prov_receipt_digest(r, dg);
    return vf(ctx, ppk, dg, PROV_HASH_LEN, r->sig_provider) &&
           vf(ctx, upk, dg, PROV_HASH_LEN, r->sig_user);
}

int prov_receipt_verify(const prov_net_t *n, const prov_receipt_t *r)
{
    if (!n || !r) return PROV_ERR_ARG;
    if (!n->cfg.verify) return PROV_ERR_HOOK;
    if (r->version != 1) return PROV_ERR_PARSE;
    const prov_provider_t *p = find_prov(n, r->provider_id, 0);
    const prov_user_t *u = find_user(n, r->user_id, 0);
    if (!p || !u) return PROV_ERR_NOT_FOUND;
    if (!prov_memeq(r->fee_schedule, n->fee_schedule, PROV_HASH_LEN)) return PROV_ERR_TAMPER;
    if (r->sla_credit > r->gross || !money_ok(r, prov_fee(n, r->gross - r->sla_credit)))
        return PROV_ERR_TAMPER;
    if (!sigs_ok(n->cfg.verify, n->cfg.verify_ctx, r, p->pk, u->pk)) return PROV_ERR_AUTH;
    return PROV_OK;
}

/* Does the receipt describe exactly this fill? */
static bool matches_fill(const prov_net_t *n, const prov_fill_t *f, const prov_receipt_t *r)
{
    const prov_provider_t *p = &n->prov[f->provider - 1];
    const prov_offer_t *o = &p->desc.offers[f->offer];
    uint8_t want_flags =
        (uint8_t) ((f->no_train ? PROV_RF_NO_TRAIN : 0u) | (f->no_retain ? PROV_RF_NO_RETAIN : 0u));
    uint64_t credit = 0;
    if (r->sla_outcome != PROV_SLA_MET &&
        !prov_muldiv(r->gross, p->desc.sla[o->sla].credit_bps, 10000u, &credit))
        return false;
    return prov_memeq(r->job_id, f->job_id, PROV_HASH_LEN) &&
           prov_memeq(r->provider_id, p->id, PROV_ID_LEN) &&
           prov_memeq(r->user_id, n->users[f->user - 1].id, PROV_ID_LEN) &&
           prov_memeq(r->desc_hash, f->desc_hash, PROV_HASH_LEN) &&
           prov_memeq(r->request_hash, f->request_hash, PROV_HASH_LEN) && r->asset == f->asset &&
           r->unit_price == f->unit_price && r->hold == f->hold && r->units <= f->qty &&
           r->rclass == o->rclass && r->unit == o->unit &&
           (r->flags & (PROV_RF_NO_TRAIN | PROV_RF_NO_RETAIN)) == want_flags &&
           r->sla_credit == credit;
}

static bool rep_counts(const prov_net_t *n, const prov_provider_t *p, const prov_user_t *u)
{
    if (prov_memeq(p->id, u->id, PROV_ID_LEN)) return false; /* self-dealing */
    switch (n->cfg.sybil) {
    case PROV_SYBIL_STAKE:
        return u->stake >= n->cfg.min_stake;
    case PROV_SYBIL_ATTESTED:
        return u->attested;
    case PROV_SYBIL_EITHER:
        return u->attested || u->stake >= n->cfg.min_stake;
    default:
        return true;
    }
}

static int settle_core(prov_net_t *n, uint32_t fill, const prov_receipt_t *r, bool resolving,
                       bool at_fault)
{
    if (!n || !r || fill == PROV_NONE || fill > PROV_MAX_FILLS || !n->fills[fill - 1].used)
        return PROV_ERR_ARG;
    prov_fill_t *f = &n->fills[fill - 1];
    if (f->state == PROV_FILL_SETTLED) return PROV_ERR_DUPLICATE;
    if (f->state == PROV_FILL_RELEASED) return PROV_ERR_STATE;
    if (resolving ? f->state != PROV_FILL_DISPUTED : f->state != PROV_FILL_RESERVED)
        return PROV_ERR_STATE;
    if (!n->cfg.verify || !n->cfg.settle) return PROV_ERR_HOOK;
    if (r->version != 1) return PROV_ERR_PARSE;
    /* Units are bounded by the reservation before any multiplication. */
    if (r->units > f->qty || r->sla_credit > r->gross) return PROV_ERR_TAMPER;
    if (!prov_memeq(r->fee_schedule, n->fee_schedule, PROV_HASH_LEN) ||
        !money_ok(r, prov_fee(n, r->gross - r->sla_credit)) || !matches_fill(n, f, r))
        return PROV_ERR_TAMPER;
    prov_provider_t *p = &n->prov[f->provider - 1];
    prov_user_t *u = &n->users[f->user - 1];
    if (!sigs_ok(n->cfg.verify, n->cfg.verify_ctx, r, p->pk, u->pk)) {
        /* G6: a half-signed (or badly signed) receipt moves no money. */
        f->state = PROV_FILL_DISPUTED;
        return PROV_ERR_AUTH;
    }
    /* U4: the only charges are usage (<= what was held) and the fee. */
    if (prov_charge_check(PROV_CHARGE_USAGE, r->hold, r->gross, false) != PROV_OK ||
        prov_charge_check(PROV_CHARGE_NETWORK_FEE, r->gross, r->fee, false) != PROV_OK)
        return PROV_ERR_USURY;

    prov_settlement_t s;
    prov_memset(&s, 0, sizeof s);
    s.kind = PROV_SETTLE_FINAL;
    s.asset = f->asset;
    s.user = f->user;
    s.provider = f->provider;
    s.fill = fill;
    s.hold = r->hold;
    s.gross = r->gross;
    s.sla_credit = r->sla_credit;
    s.fee = r->fee;
    s.net = r->net;
    s.refund = r->refund;
    prov_receipt_digest(r, s.ref);
    if (n->cfg.settle(n->cfg.settle_ctx, &s) != 0) return PROV_ERR_SETTLE;

    f->state = PROV_FILL_SETTLED;
    n->fees_total += r->fee;
    if (resolving && at_fault) p->rep.disputes_lost++;
    prov_memcpy(p->rep_log[p->rep_log_n % PROV_REP_LOG], s.ref, PROV_HASH_LEN);
    p->rep_log_n++;
    uint32_t ui = f->user - 1, pi = f->provider - 1;
    if (rep_counts(n, p, u) && n->rep_pair[pi][ui] < n->cfg.rep_max_per_user) {
        n->rep_pair[pi][ui]++;
        p->rep.receipts++;
        p->rep.units_served += r->units;
        if (r->sla_outcome == PROV_SLA_MET)
            p->rep.sla_met++;
        else
            p->rep.sla_breached++;
    }
    return PROV_OK;
}

int prov_settle(prov_net_t *n, uint32_t fill, const prov_receipt_t *r)
{
    return settle_core(n, fill, r, false, false);
}

int prov_dispute_resolve(prov_net_t *n, uint32_t fill, const prov_receipt_t *r,
                         bool provider_at_fault)
{
    return settle_core(n, fill, r, true, provider_at_fault);
}

uint32_t prov_rep_export(const prov_net_t *n, uint32_t provider, uint8_t (*out)[PROV_HASH_LEN],
                         uint32_t max)
{
    const prov_provider_t *p = prov_provider(n, provider);
    if (!p || !out) return 0;
    uint32_t have = p->rep_log_n < PROV_REP_LOG ? p->rep_log_n : PROV_REP_LOG;
    uint32_t k = have < max ? have : max;
    /* newest first */
    for (uint32_t i = 0; i < k; i++)
        prov_memcpy(out[i], p->rep_log[(p->rep_log_n - 1u - i) % PROV_REP_LOG], PROV_HASH_LEN);
    return k;
}

#define PROV_REBUILD_MAX 256u

uint32_t prov_rep_rebuild(prov_verify_fn verify, void *ctx, const uint8_t provider_id[PROV_ID_LEN],
                          const uint8_t provider_pk[PROV_PK_BYTES], const prov_receipt_t *rs,
                          const uint8_t (*user_pks)[PROV_PK_BYTES], uint32_t count, prov_rep_t *out)
{
    static uint8_t seen[PROV_REBUILD_MAX][PROV_HASH_LEN]; /* not reentrant */
    uint32_t ns = 0;
    uint8_t id[PROV_ID_LEN];
    if (!out) return 0;
    prov_memset(out, 0, sizeof *out);
    if (!verify || !provider_id || !provider_pk || !rs || !user_pks) return 0;
    prov_sha3(provider_pk, PROV_PK_BYTES, id);
    if (!prov_memeq(id, provider_id, PROV_ID_LEN)) return 0;
    if (count > PROV_REBUILD_MAX) count = PROV_REBUILD_MAX;
    for (uint32_t i = 0; i < count; i++) {
        const prov_receipt_t *r = &rs[i];
        uint8_t uid[PROV_ID_LEN], dg[PROV_HASH_LEN];
        if (r->version != 1 || !prov_memeq(r->provider_id, provider_id, PROV_ID_LEN)) continue;
        prov_sha3(user_pks[i], PROV_PK_BYTES, uid);
        if (!prov_memeq(uid, r->user_id, PROV_ID_LEN) || prov_memeq(uid, id, PROV_ID_LEN)) continue;
        /* Fee schedules may differ between networks; check everything else. */
        if (r->sla_credit > r->gross || !money_ok(r, r->fee)) continue;
        if (!sigs_ok(verify, ctx, r, provider_pk, user_pks[i])) continue;
        prov_receipt_digest(r, dg);
        bool dup = false;
        for (uint32_t k = 0; k < ns; k++)
            if (prov_memeq(seen[k], dg, PROV_HASH_LEN)) dup = true;
        if (dup) continue;
        prov_memcpy(seen[ns++], dg, PROV_HASH_LEN);
        out->receipts++;
        out->units_served += r->units;
        if (r->sla_outcome == PROV_SLA_MET)
            out->sla_met++;
        else
            out->sla_breached++;
    }
    return out->receipts;
}
