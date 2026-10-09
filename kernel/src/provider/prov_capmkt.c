/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_capmkt.c — prov <-> capmkt bridge (see prov_capmkt.h). */
#include "prov_capmkt.h"
#include "../tensor/zt.h"

void prov_cm_id(const uint8_t id[PROV_ID_LEN], uint8_t out[CM_ID_BYTES])
{
    prov_memcpy(out, id, CM_ID_BYTES);
}

uint64_t prov_cm_tithe(void *ctx, uint64_t amount)
{
    const prov_cm_t *b = (const prov_cm_t *) ctx;
    return b ? prov_fee(b->n, amount) : 0;
}

void prov_cm_bind(prov_cm_t *b, prov_net_t *n, cm_market_t *m, cm_params_t *params)
{
    if (!b) return;
    prov_memset(b, 0, sizeof *b);
    b->n = n;
    b->m = m;
    if (params) {
        cm_params_default(params);
        params->tithe = prov_cm_tithe;
        params->verify = prov_cm_verify;
        params->ctx = b;
    }
}

static bool is_vfv(const prov_net_t *n, uint16_t asset)
{
    const prov_asset_t *a = prov_asset(n, asset);
    return a && a->kind == PROV_ASSET_VFV;
}

int prov_cm_ask(prov_cm_t *b, uint32_t ask, cm_key_t key, uint64_t factor, uint64_t expires_ms,
                uint32_t *order_id)
{
    if (!b || !b->n || !b->m || factor == 0 || ask == PROV_NONE || ask > PROV_MAX_ASKS)
        return PROV_ERR_ARG;
    prov_ask_t *a = &b->n->asks[ask - 1];
    const prov_provider_t *p = prov_provider(b->n, a->provider);
    if (!a->used || !a->live || !p || !p->active) return PROV_ERR_STATE;
    if (!is_vfv(b->n, a->asset)) return PROV_ERR_UNSUPPORTED; /* capmkt is VFV only */
    uint64_t price, qty = zt_udiv64(a->qty, factor, 0);
    if (qty == 0 || !prov_mul_ok(a->unit_price, factor, &price)) return PROV_ERR_ARG;
    uint8_t id[CM_ID_BYTES];
    prov_cm_id(p->id, id);
    if (cm_ask(b->m, id, key, qty, price, expires_ms, order_id) != CM_OK) return PROV_ERR_POLICY;
    a->live = false; /* never on two books at once */
    return PROV_OK;
}

int prov_cm_bid(prov_cm_t *b, uint32_t user, const prov_job_t *job, cm_key_t key, uint64_t factor,
                uint64_t expires_ms, uint32_t *order_id)
{
    if (!b || !b->n || !b->m || !job || factor == 0 || user == PROV_NONE || user > PROV_MAX_USERS ||
        !b->n->users[user - 1].used)
        return PROV_ERR_ARG;
    if (!is_vfv(b->n, job->asset)) return PROV_ERR_UNSUPPORTED;
    uint64_t limit, qty = zt_udiv64(job->qty, factor, 0);
    if (qty == 0 || !prov_mul_ok(job->max_unit_price, factor, &limit)) return PROV_ERR_ARG;
    uint8_t id[CM_ID_BYTES];
    prov_cm_id(b->n->users[user - 1].id, id);
    cm_status_t st = cm_bid(b->m, id, key, qty, limit, expires_ms, order_id);
    if (st == CM_ERR_FUNDS) return PROV_ERR_BUDGET;
    return st == CM_OK ? PROV_OK : PROV_ERR_POLICY;
}

static const prov_provider_t *by16(const prov_net_t *n, const uint8_t id16[CM_ID_BYTES])
{
    for (uint32_t i = 0; i < PROV_MAX_PROVIDERS; i++)
        if (n->prov[i].used && prov_memeq(n->prov[i].id, id16, CM_ID_BYTES)) return &n->prov[i];
    return 0;
}

static const prov_user_t *user16(const prov_net_t *n, const uint8_t id16[CM_ID_BYTES])
{
    for (uint32_t i = 0; i < PROV_MAX_USERS; i++)
        if (n->users[i].used && prov_memeq(n->users[i].id, id16, CM_ID_BYTES)) return &n->users[i];
    return 0;
}

int prov_cm_receipt(const prov_cm_t *b, uint32_t cid, uint8_t rclass, uint8_t unit,
                    const prov_usage_t *u, prov_receipt_t *r)
{
    if (!b || !u || !r || rclass >= PROV_RC_COUNT || unit >= PROV_UNIT_COUNT) return PROV_ERR_ARG;
    const cm_contract_t *c = cm_contract(b->m, cid);
    if (!c || c->state != CM_C_OPEN) return PROV_ERR_STATE;
    if (u->units == 0 || u->units > c->qty - c->delivered || u->end_tick < u->start_tick)
        return PROV_ERR_ARG;
    const prov_provider_t *p = by16(b->n, c->provider);
    const prov_user_t *us = user16(b->n, c->buyer);
    if (!p || !us) return PROV_ERR_NOT_FOUND;
    prov_memset(r, 0, sizeof *r);
    r->version = 1;
    prov_hb_t h;
    prov_hb_init(&h, "zxv-prov-cm-job-v1");
    prov_hb_u32(&h, c->id);
    prov_hb_u64(&h, c->round);
    prov_hb_put(&h, c->round_digest, 32);
    prov_hb_u32(&h, c->proof_seq + 1u);
    prov_hb_final(&h, r->job_id);
    prov_memcpy(r->provider_id, p->id, PROV_ID_LEN);
    prov_memcpy(r->user_id, us->id, PROV_ID_LEN);
    prov_memcpy(r->desc_hash, p->desc_hash, PROV_HASH_LEN);
    prov_memcpy(r->fee_schedule, b->n->fee_schedule, PROV_HASH_LEN);
    prov_memcpy(r->response_hash, u->response_hash, PROV_HASH_LEN);
    r->rclass = rclass;
    r->unit = unit;
    r->sla_outcome = u->sla_outcome > PROV_SLA_UNAVAILABLE ? PROV_SLA_UNAVAILABLE : u->sla_outcome;
    r->asset = 0; /* VFV */
    prov_strlcpy(r->asset_code, "VFV", sizeof r->asset_code);
    r->units = u->units;
    r->unit_price = c->price;
    if (!prov_mul_ok(u->units, c->price, &r->gross)) return PROV_ERR_OVERFLOW;
    r->hold = r->gross;
    r->fee = prov_fee(b->n, r->gross);
    r->net = r->gross - r->fee;
    r->latency_ms = u->latency_ms;
    r->start_tick = u->start_tick;
    r->end_tick = u->end_tick;
    return PROV_OK;
}

int prov_cm_accept(prov_cm_t *b, uint32_t cid, const prov_receipt_t *r)
{
    if (!b || !r) return PROV_ERR_ARG;
    const cm_contract_t *c = cm_contract(b->m, cid);
    if (!c) return PROV_ERR_NOT_FOUND;
    if (!prov_memeq(r->provider_id, c->provider, CM_ID_BYTES) ||
        !prov_memeq(r->user_id, c->buyer, CM_ID_BYTES) || r->unit_price != c->price ||
        r->asset != 0)
        return PROV_ERR_TAMPER;
    int rc = prov_receipt_verify(b->n, r);
    if (rc != PROV_OK) return rc;
    uint8_t d[PROV_HASH_LEN];
    prov_receipt_digest(r, d);
    uint32_t slot = PROV_CM_MAX_RECEIPTS;
    for (uint32_t i = 0; i < PROV_CM_MAX_RECEIPTS; i++) {
        if (b->rc[i].used && prov_memeq(b->rc[i].digest, d, PROV_HASH_LEN))
            return PROV_ERR_DUPLICATE;
        if (!b->rc[i].used && slot == PROV_CM_MAX_RECEIPTS) slot = i;
    }
    if (slot == PROV_CM_MAX_RECEIPTS) return PROV_ERR_FULL;
    prov_cm_rcpt_t *e = &b->rc[slot];
    e->used = true;
    e->spent = false;
    e->contract_id = cid;
    e->units = r->units;
    prov_memcpy(e->digest, d, PROV_HASH_LEN);
    prov_memcpy(e->provider16, c->provider, CM_ID_BYTES);
    prov_memcpy(e->user16, c->buyer, CM_ID_BYTES);
    return PROV_OK;
}

bool prov_cm_verify(void *ctx, const cm_contract_t *c, const cm_proof_t *p)
{
    prov_cm_t *b = (prov_cm_t *) ctx;
    if (!b || !c || !p) return false;
    for (uint32_t i = 0; i < PROV_CM_MAX_RECEIPTS; i++) {
        prov_cm_rcpt_t *e = &b->rc[i];
        if (!e->used || e->spent || !prov_memeq(e->digest, p->evidence, PROV_HASH_LEN)) continue;
        if (e->contract_id != c->id || e->units != p->units ||
            !prov_memeq(e->provider16, c->provider, CM_ID_BYTES) ||
            !prov_memeq(e->user16, c->buyer, CM_ID_BYTES))
            return false;
        e->spent = true; /* one receipt pays one proof */
        return true;
    }
    return false;
}
