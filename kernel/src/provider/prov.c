/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov.c — provider registry, signed asks, user bids, price-time matching
 * under the cooperative share cap, the published fee and the no-usury guard.
 * Receipts, settlement and reputation are in prov_receipt.c. */
#include "prov.h"
#include "../pay/pay_util.h"
#include "../pay/pay_assure.h"
#include "../pay/pay_ledger.h"
#include "../tensor/zt.h"

/* ===== Configuration ===== */

void prov_config_default(prov_config_t *c)
{
    if (!c) return;
    prov_memset(c, 0, sizeof *c);
    c->fee_mode = PROV_FEE_ASSURE;
    c->fee_bps = 0;
    c->cap_q32 = PROV_CAP_INV_PHI2_Q32;
    c->sybil = PROV_SYBIL_NONE;
    c->min_stake = 0;
    c->rep_max_per_user = 4;
}

void prov_fee_schedule_hash(const prov_config_t *c, uint8_t out[PROV_HASH_LEN])
{
    prov_hb_t h;
    prov_hb_init(&h, "zxv-prov-fee-v1");
    prov_hb_u8(&h, c->fee_mode);
    prov_hb_u32(&h, c->fee_mode == PROV_FEE_BPS ? c->fee_bps : 0u);
    prov_hb_final(&h, out);
}

int prov_net_init(prov_net_t *n, const prov_config_t *cfg)
{
    prov_config_t def;
    if (!n) return PROV_ERR_ARG;
    if (!cfg) {
        prov_config_default(&def);
        cfg = &def;
    }
    if (cfg->fee_mode > PROV_FEE_BPS) return PROV_ERR_ARG;
    if (cfg->fee_mode == PROV_FEE_BPS && cfg->fee_bps > PROV_FEE_MAX_BPS) return PROV_ERR_ARG;
    if (cfg->cap_q32 == 0 || cfg->sybil > PROV_SYBIL_EITHER) return PROV_ERR_ARG;
    prov_memset(n, 0, sizeof *n);
    prov_memcpy(&n->cfg, cfg, sizeof n->cfg);
    prov_fee_schedule_hash(&n->cfg, n->fee_schedule);
    uint16_t vfv;
    return prov_asset_add(n, PROV_ASSET_VFV, "VFV", PAY_RAIL_DEBIT_CODE, 2, PROV_CHAIN_NONE, 0,
                          &vfv);
}

void prov_next_cycle(prov_net_t *n)
{
    if (!n) return;
    n->cycle++;
    prov_memset(n->rep_pair, 0, sizeof n->rep_pair);
}

/* ===== Fee and no usury ===== */

uint64_t prov_fee(const prov_net_t *n, uint64_t amount)
{
    uint64_t f = 0;
    if (!n) return 0;
    if (n->cfg.fee_mode == PROV_FEE_ASSURE) return pay_assure_fee(amount);
    if (!prov_muldiv(amount, n->cfg.fee_bps, 10000u, &f)) return 0;
    return f;
}

int prov_charge_check(uint8_t kind, uint64_t principal, uint64_t repaid, bool time_based)
{
    if (kind >= PROV_CHARGE_COUNT) return PROV_ERR_ARG;
    if (kind == PROV_CHARGE_INTEREST || kind == PROV_CHARGE_LATE_FEE || kind == PROV_CHARGE_HOLDING)
        return PROV_ERR_USURY;
    if (pay_usury_check(principal, repaid, time_based) != PAY_OK) return PROV_ERR_USURY;
    return PROV_OK;
}

/* ===== Assets ===== */

static bool upper3(const char *s)
{
    if (prov_strnlen(s, 4) != 3) return false;
    for (int i = 0; i < 3; i++)
        if (s[i] < 'A' || s[i] > 'Z') return false;
    return true;
}

int prov_asset_add(prov_net_t *n, uint8_t kind, const char *code, uint16_t numeric, uint8_t minor,
                   uint8_t chain_family, const char *chain_id, uint16_t *out)
{
    if (!n || !code || minor > 18) return PROV_ERR_ARG;
    size_t cl = prov_strnlen(code, PROV_CODE_MAX);
    if (cl == 0 || cl >= PROV_CODE_MAX) return PROV_ERR_ARG;
    switch (kind) {
    case PROV_ASSET_VFV:
        if (!prov_streq(code, "VFV") || numeric != PAY_RAIL_DEBIT_CODE) return PROV_ERR_ARG;
        break;
    case PROV_ASSET_ISO4217:
        if (!upper3(code) || prov_streq(code, "VFV") || numeric == 0 || numeric > 999 ||
            numeric == PAY_RAIL_DEBIT_CODE || numeric == PAY_RAIL_CREDIT_CODE ||
            numeric == PAY_RAIL_EQUITY_CODE)
            return PROV_ERR_ARG;
        break;
    case PROV_ASSET_CHAIN:
        if (chain_family == PROV_CHAIN_NONE || chain_family >= PROV_CHAIN_COUNT || !chain_id ||
            prov_strnlen(chain_id, PROV_CHAIN_ID_MAX) == 0 ||
            prov_strnlen(chain_id, PROV_CHAIN_ID_MAX) >= PROV_CHAIN_ID_MAX)
            return PROV_ERR_ARG;
        numeric = 0;
        break;
    default:
        return PROV_ERR_ARG;
    }
    uint32_t free = PROV_MAX_ASSETS;
    for (uint32_t i = 0; i < PROV_MAX_ASSETS; i++) {
        const prov_asset_t *a = &n->assets[i];
        if (!a->used) {
            if (free == PROV_MAX_ASSETS) free = i;
            continue;
        }
        if (a->kind == kind && prov_streq(a->code, code) &&
            (kind != PROV_ASSET_CHAIN || prov_streq(a->chain_id, chain_id)))
            return PROV_ERR_DUPLICATE;
    }
    if (free == PROV_MAX_ASSETS) return PROV_ERR_FULL;
    prov_asset_t *a = &n->assets[free];
    prov_memset(a, 0, sizeof *a);
    a->used = true;
    a->kind = kind;
    prov_strlcpy(a->code, code, sizeof a->code);
    a->numeric = numeric;
    a->minor = minor;
    a->chain_family = kind == PROV_ASSET_CHAIN ? chain_family : (uint8_t) PROV_CHAIN_NONE;
    if (kind == PROV_ASSET_CHAIN) prov_strlcpy(a->chain_id, chain_id, sizeof a->chain_id);
    if (out) *out = (uint16_t) free;
    return PROV_OK;
}

const prov_asset_t *prov_asset(const prov_net_t *n, uint16_t asset)
{
    if (!n || asset >= PROV_MAX_ASSETS || !n->assets[asset].used) return 0;
    return &n->assets[asset];
}

/* ===== Providers ===== */

static prov_provider_t *pget(prov_net_t *n, uint32_t h)
{
    if (!n || h == PROV_NONE || h > PROV_MAX_PROVIDERS || !n->prov[h - 1].used) return 0;
    return &n->prov[h - 1];
}

const prov_provider_t *prov_provider(const prov_net_t *n, uint32_t provider)
{
    return pget((prov_net_t *) n, provider);
}

static prov_user_t *uget(prov_net_t *n, uint32_t h)
{
    if (!n || h == PROV_NONE || h > PROV_MAX_USERS || !n->users[h - 1].used) return 0;
    return &n->users[h - 1];
}

static bool str_ok(const char *s, size_t cap, bool nonempty)
{
    size_t l = prov_strnlen(s, cap);
    if (l >= cap) return false;
    return !nonempty || l > 0;
}

static bool desc_valid(const prov_desc_t *d)
{
    if (!str_ok(d->name, sizeof d->name, true) || d->network > PROV_NET_WEB3) return false;
    if (d->chain_family >= PROV_CHAIN_COUNT || !str_ok(d->chain_id, sizeof d->chain_id, false) ||
        !str_ok(d->payout_addr, sizeof d->payout_addr, false) ||
        !str_ok(d->jurisdiction, sizeof d->jurisdiction, true))
        return false;
    if (d->network == PROV_NET_WEB3 &&
        (d->chain_family == PROV_CHAIN_NONE || prov_strnlen(d->payout_addr, 2) == 0))
        return false;
    if (d->n_regions == 0 || d->n_regions > PROV_MAX_REGIONS) return false;
    for (uint32_t i = 0; i < d->n_regions; i++)
        if (!str_ok(d->regions[i].code, sizeof d->regions[i].code, true)) return false;
    if (d->n_sla == 0 || d->n_sla > PROV_MAX_SLA) return false;
    for (uint32_t i = 0; i < d->n_sla; i++)
        if (d->sla[i].availability_ppm > 1000000u || d->sla[i].credit_bps > 10000u) return false;
    if (d->n_offers == 0 || d->n_offers > PROV_MAX_OFFERS) return false;
    for (uint32_t i = 0; i < d->n_offers; i++) {
        const prov_offer_t *o = &d->offers[i];
        if (o->rclass >= PROV_RC_COUNT || o->unit >= PROV_UNIT_COUNT || o->region >= d->n_regions ||
            o->sla >= d->n_sla || o->capacity_per_cycle == 0 || o->api.shape >= PROV_SHAPE_COUNT)
            return false;
        if (o->rclass == PROV_RC_INFERENCE && o->api.shape == PROV_SHAPE_NONE) return false;
        if (!str_ok(o->accel_type, sizeof o->accel_type, false) ||
            !str_ok(o->licence.spdx, sizeof o->licence.spdx, false) ||
            !str_ok(o->api.model, sizeof o->api.model, false) ||
            !str_ok(o->api.usage_in, sizeof o->api.usage_in, false) ||
            !str_ok(o->api.usage_out, sizeof o->api.usage_out, false) ||
            !str_ok(o->api.text_path, sizeof o->api.text_path, false) ||
            !str_ok(o->api.privacy_header, sizeof o->api.privacy_header, false))
            return false;
    }
    return true;
}

void prov_desc_digest(const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                      uint8_t out[PROV_HASH_LEN])
{
    prov_hb_t h;
    uint8_t id[PROV_ID_LEN];
    prov_sha3(pk, PROV_PK_BYTES, id);
    prov_hb_init(&h, "zxv-prov-desc-v1");
    prov_hb_put(&h, id, PROV_ID_LEN);
    prov_hb_str(&h, d->name, PROV_NAME_MAX);
    prov_hb_u8(&h, d->network);
    prov_hb_u8(&h, d->chain_family);
    prov_hb_str(&h, d->chain_id, PROV_CHAIN_ID_MAX);
    prov_hb_str(&h, d->payout_addr, PROV_ADDR_MAX);
    prov_hb_str(&h, d->jurisdiction, 8);
    prov_hb_u8(&h, d->n_regions);
    for (uint32_t i = 0; i < d->n_regions && i < PROV_MAX_REGIONS; i++)
        prov_hb_str(&h, d->regions[i].code, 8);
    prov_hb_u8(&h, d->n_sla);
    for (uint32_t i = 0; i < d->n_sla && i < PROV_MAX_SLA; i++) {
        prov_hb_u32(&h, d->sla[i].availability_ppm);
        prov_hb_u32(&h, d->sla[i].max_latency_ms);
        prov_hb_u32(&h, d->sla[i].credit_bps);
    }
    prov_hb_u8(&h, d->n_offers);
    for (uint32_t i = 0; i < d->n_offers && i < PROV_MAX_OFFERS; i++) {
        const prov_offer_t *o = &d->offers[i];
        prov_hb_u8(&h, o->rclass);
        prov_hb_u8(&h, o->unit);
        prov_hb_u8(&h, o->region);
        prov_hb_u8(&h, o->sla);
        prov_hb_str(&h, o->accel_type, PROV_NAME_MAX);
        prov_hb_u32(&h, o->accel_count);
        prov_hb_u64(&h, o->accel_mem_bytes);
        prov_hb_u64(&h, o->capacity_per_cycle);
        prov_hb_str(&h, o->licence.spdx, PROV_SPDX_MAX);
        prov_hb_u8(&h, o->licence.commercial_use ? 1u : 0u);
        prov_hb_put(&h, o->licence.terms_hash, PROV_HASH_LEN);
        prov_hb_u8(&h, o->api.shape);
        prov_hb_str(&h, o->api.model, PROV_MODEL_MAX);
        prov_hb_str(&h, o->api.usage_in, PROV_KEYPATH_MAX);
        prov_hb_str(&h, o->api.usage_out, PROV_KEYPATH_MAX);
        prov_hb_str(&h, o->api.text_path, PROV_KEYPATH_MAX);
        prov_hb_str(&h, o->api.privacy_header, PROV_KEYPATH_MAX);
    }
    prov_hb_u8(&h, d->honours_no_train ? 1u : 0u);
    prov_hb_u8(&h, d->zero_retention ? 1u : 0u);
    prov_hb_u64(&h, d->version);
    prov_hb_final(&h, out);
}

static void withdraw_asks(prov_net_t *n, uint32_t handle)
{
    for (uint32_t i = 0; i < PROV_MAX_ASKS; i++)
        if (n->asks[i].used && n->asks[i].provider == handle) n->asks[i].live = false;
}

int prov_register(prov_net_t *n, const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                  const uint8_t sig[PROV_SIG_BYTES], uint32_t *handle)
{
    if (!n || !d || !pk || !sig) return PROV_ERR_ARG;
    if (!n->cfg.verify) return PROV_ERR_HOOK;
    if (!desc_valid(d)) return PROV_ERR_ARG;
    uint8_t dg[PROV_HASH_LEN], id[PROV_ID_LEN];
    prov_desc_digest(d, pk, dg);
    if (!n->cfg.verify(n->cfg.verify_ctx, pk, dg, PROV_HASH_LEN, sig)) return PROV_ERR_AUTH;
    prov_sha3(pk, PROV_PK_BYTES, id);
    uint32_t slot = PROV_MAX_PROVIDERS;
    for (uint32_t i = 0; i < PROV_MAX_PROVIDERS; i++) {
        prov_provider_t *p = &n->prov[i];
        if (p->used && prov_memeq(p->id, id, PROV_ID_LEN)) {
            if (d->version <= p->desc.version) return PROV_ERR_STATE;
            /* Terms changed: no live ask may keep the old ones; evidence was
             * about the old descriptor. Reputation and receipts stay. */
            withdraw_asks(n, i + 1);
            prov_memcpy(&p->desc, d, sizeof p->desc);
            prov_memcpy(p->desc_hash, dg, PROV_HASH_LEN);
            prov_memset(p->attest, 0, sizeof p->attest);
            p->active = true;
            if (handle) *handle = i + 1;
            return PROV_OK;
        }
        if (!p->used && slot == PROV_MAX_PROVIDERS) slot = i;
    }
    if (slot == PROV_MAX_PROVIDERS) return PROV_ERR_FULL;
    prov_provider_t *p = &n->prov[slot];
    prov_memset(p, 0, sizeof *p);
    p->used = true;
    p->active = true;
    prov_memcpy(p->id, id, PROV_ID_LEN);
    prov_memcpy(p->pk, pk, PROV_PK_BYTES);
    prov_memcpy(p->desc_hash, dg, PROV_HASH_LEN);
    prov_memcpy(&p->desc, d, sizeof p->desc);
    if (handle) *handle = slot + 1;
    return PROV_OK;
}

int prov_attest_present(prov_net_t *n, uint32_t provider, uint8_t slot, uint8_t kind,
                        const uint8_t *evidence, uint32_t len)
{
    prov_provider_t *p = pget(n, provider);
    if (!p || slot >= PROV_MAX_ATTEST || kind == PROV_ATTK_NONE || kind > PROV_ATTK_OTHER ||
        !evidence || len == 0)
        return PROV_ERR_ARG;
    prov_attest_t *a = &p->attest[slot];
    a->kind = kind;
    a->len = len;
    prov_sha3(evidence, len, a->evidence_hash);
    a->state = PROV_ATT_UNVERIFIED;
    if (!n->cfg.attest) return PROV_ERR_HOOK; /* carried, but never counted as verified */
    a->state = n->cfg.attest(n->cfg.attest_ctx, kind, evidence, len, p->desc_hash)
                   ? (uint8_t) PROV_ATT_VERIFIED
                   : (uint8_t) PROV_ATT_FAILED;
    return a->state == PROV_ATT_VERIFIED ? PROV_OK : PROV_ERR_AUTH;
}

bool prov_is_attested(const prov_net_t *n, uint32_t provider)
{
    const prov_provider_t *p = prov_provider(n, provider);
    if (!p) return false;
    for (uint32_t i = 0; i < PROV_MAX_ATTEST; i++)
        if (p->attest[i].state == PROV_ATT_VERIFIED) return true;
    return false;
}

int prov_provider_leave(prov_net_t *n, uint32_t provider)
{
    prov_provider_t *p = pget(n, provider);
    if (!p) return PROV_ERR_NOT_FOUND;
    p->active = false;
    withdraw_asks(n, provider);
    int rc = PROV_OK;
    for (uint32_t i = 0; i < PROV_MAX_FILLS; i++) {
        prov_fill_t *f = &n->fills[i];
        if (!f->used || f->provider != provider || f->state != PROV_FILL_RESERVED) continue;
        prov_settlement_t s;
        prov_memset(&s, 0, sizeof s);
        s.kind = PROV_SETTLE_RELEASE;
        s.asset = f->asset;
        s.user = f->user;
        s.provider = f->provider;
        s.fill = i + 1;
        s.hold = f->hold;
        s.refund = f->hold;
        prov_memcpy(s.ref, f->job_id, PROV_HASH_LEN);
        if (n->cfg.settle && n->cfg.settle(n->cfg.settle_ctx, &s) != 0) {
            rc = PROV_ERR_SETTLE; /* stays reserved; retry later */
            continue;
        }
        f->state = PROV_FILL_RELEASED;
        prov_bid_t *b = (f->bid && f->bid <= PROV_MAX_BIDS) ? &n->bids[f->bid - 1] : 0;
        if (b && b->used) {
            b->filled -= f->qty;
            b->spent -= f->hold;
            if (!b->cancelled) b->open = true; /* demand unmet again: may re-match */
        }
    }
    return rc;
}

/* ===== Users ===== */

int prov_user_add(prov_net_t *n, const uint8_t pk[PROV_PK_BYTES], uint64_t stake, bool attested,
                  uint32_t *handle)
{
    if (!n || !pk) return PROV_ERR_ARG;
    uint8_t id[PROV_ID_LEN];
    prov_sha3(pk, PROV_PK_BYTES, id);
    uint32_t slot = PROV_MAX_USERS;
    for (uint32_t i = 0; i < PROV_MAX_USERS; i++) {
        if (n->users[i].used && prov_memeq(n->users[i].id, id, PROV_ID_LEN))
            return PROV_ERR_DUPLICATE;
        if (!n->users[i].used && slot == PROV_MAX_USERS) slot = i;
    }
    if (slot == PROV_MAX_USERS) return PROV_ERR_FULL;
    prov_user_t *u = &n->users[slot];
    prov_memset(u, 0, sizeof *u);
    u->used = true;
    prov_memcpy(u->id, id, PROV_ID_LEN);
    prov_memcpy(u->pk, pk, PROV_PK_BYTES);
    u->stake = stake;
    u->attested = attested;
    if (handle) *handle = slot + 1;
    return PROV_OK;
}

int prov_user_set_stake(prov_net_t *n, uint32_t user, uint64_t stake, bool attested)
{
    prov_user_t *u = uget(n, user);
    if (!u) return PROV_ERR_NOT_FOUND;
    u->stake = stake;
    u->attested = attested;
    return PROV_OK;
}

/* ===== Asks ===== */

void prov_ask_digest(const prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                     uint64_t unit_price, uint64_t qty, uint64_t nonce, uint8_t out[PROV_HASH_LEN])
{
    prov_hb_t h;
    const prov_provider_t *p = prov_provider(n, provider);
    const prov_asset_t *a = prov_asset(n, asset);
    prov_hb_init(&h, "zxv-prov-ask-v1");
    if (p) {
        prov_hb_put(&h, p->id, PROV_ID_LEN);
        prov_hb_put(&h, p->desc_hash, PROV_HASH_LEN);
    }
    prov_hb_u8(&h, offer);
    if (a) {
        prov_hb_u8(&h, a->kind);
        prov_hb_str(&h, a->code, PROV_CODE_MAX);
        prov_hb_str(&h, a->chain_id, PROV_CHAIN_ID_MAX);
    }
    prov_hb_u64(&h, unit_price);
    prov_hb_u64(&h, qty);
    prov_hb_u64(&h, nonce);
    prov_hb_final(&h, out);
}

int prov_ask_post(prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                  uint64_t unit_price, uint64_t qty, uint64_t nonce,
                  const uint8_t sig[PROV_SIG_BYTES], uint32_t *ask)
{
    prov_provider_t *p = pget(n, provider);
    if (!p || !sig) return PROV_ERR_ARG;
    if (!p->active) return PROV_ERR_STATE;
    if (offer >= p->desc.n_offers || !prov_asset(n, asset) || unit_price == 0 || qty == 0 ||
        qty > p->desc.offers[offer].capacity_per_cycle)
        return PROV_ERR_ARG;
    if (!n->cfg.verify) return PROV_ERR_HOOK;
    if (nonce <= p->last_ask_nonce) return PROV_ERR_DUPLICATE; /* replay */
    uint8_t dg[PROV_HASH_LEN];
    prov_ask_digest(n, provider, offer, asset, unit_price, qty, nonce, dg);
    /* G2: only the provider's own key lists the provider's service. */
    if (!n->cfg.verify(n->cfg.verify_ctx, p->pk, dg, PROV_HASH_LEN, sig)) return PROV_ERR_AUTH;
    uint32_t slot = PROV_MAX_ASKS;
    for (uint32_t i = 0; i < PROV_MAX_ASKS; i++)
        if (!n->asks[i].used || (!n->asks[i].live && slot == PROV_MAX_ASKS)) {
            slot = i;
            break;
        }
    if (slot == PROV_MAX_ASKS) return PROV_ERR_FULL;
    prov_ask_t *a = &n->asks[slot];
    prov_memset(a, 0, sizeof *a);
    a->used = true;
    a->live = true;
    a->provider = provider;
    a->offer = offer;
    a->asset = asset;
    a->unit_price = unit_price;
    a->qty = qty;
    a->nonce = nonce;
    a->seq = ++n->seq;
    p->last_ask_nonce = nonce;
    if (ask) *ask = slot + 1;
    return PROV_OK;
}

int prov_ask_cancel(prov_net_t *n, uint32_t provider, uint32_t ask)
{
    if (!n || ask == PROV_NONE || ask > PROV_MAX_ASKS || !n->asks[ask - 1].used)
        return PROV_ERR_NOT_FOUND;
    if (n->asks[ask - 1].provider != provider) return PROV_ERR_AUTH;
    n->asks[ask - 1].live = false;
    return PROV_OK;
}

/* ===== Bids ===== */

void prov_job_default(prov_job_t *j)
{
    if (!j) return;
    prov_memset(j, 0, sizeof *j);
    j->allow_split = true;
}

int prov_bid_post(prov_net_t *n, uint32_t user, const prov_job_t *job, uint32_t *bid)
{
    if (!uget(n, user) || !job) return PROV_ERR_ARG;
    if (job->rclass >= PROV_RC_COUNT || job->unit >= PROV_UNIT_COUNT ||
        !prov_asset(n, job->asset) || job->qty == 0 || job->max_unit_price == 0 ||
        job->n_regions > PROV_MAX_REGIONS || job->n_jurisdictions > PROV_MAX_REGIONS ||
        job->n_spdx > PROV_MAX_SPDX_ALLOW || job->net_mask > 3u)
        return PROV_ERR_ARG;
    uint64_t ceiling;
    if (!prov_mul_ok(job->qty, job->max_unit_price, &ceiling)) return PROV_ERR_OVERFLOW;
    uint32_t slot = PROV_MAX_BIDS;
    for (uint32_t i = 0; i < PROV_MAX_BIDS; i++)
        if (!n->bids[i].used || !n->bids[i].open) {
            /* reuse a closed bid only if no fill still references it */
            bool ref = false;
            for (uint32_t k = 0; n->bids[i].used && k < PROV_MAX_FILLS; k++)
                if (n->fills[k].used && n->fills[k].bid == i + 1 &&
                    n->fills[k].state == PROV_FILL_RESERVED)
                    ref = true;
            if (!ref) {
                slot = i;
                break;
            }
        }
    if (slot == PROV_MAX_BIDS) return PROV_ERR_FULL;
    prov_bid_t *b = &n->bids[slot];
    prov_memset(b, 0, sizeof *b);
    b->used = true;
    b->open = true;
    b->user = user;
    prov_memcpy(&b->job, job, sizeof b->job);
    if (b->job.max_total == 0 || b->job.max_total > ceiling) b->job.max_total = ceiling;
    b->seq = ++n->seq;
    if (bid) *bid = slot + 1;
    return PROV_OK;
}

int prov_bid_cancel(prov_net_t *n, uint32_t user, uint32_t bid)
{
    if (!n || bid == PROV_NONE || bid > PROV_MAX_BIDS || !n->bids[bid - 1].used)
        return PROV_ERR_NOT_FOUND;
    if (n->bids[bid - 1].user != user) return PROV_ERR_AUTH;
    n->bids[bid - 1].open = false;
    n->bids[bid - 1].cancelled = true;
    return PROV_OK;
}

/* ===== Eligibility (U2, U3) ===== */

static bool region_in(const char *code, const prov_region_t *list, uint8_t n)
{
    if (n == 0) return true;
    for (uint32_t i = 0; i < n; i++)
        if (prov_streq(code, list[i].code)) return true;
    return false;
}

uint32_t prov_rep_score_q16(const prov_rep_t *r)
{
    if (!r) return 0;
    uint64_t num = ((uint64_t) r->sla_met + 1u) << 16;
    uint64_t den = (uint64_t) r->sla_met + r->sla_breached + 2u * (uint64_t) r->disputes_lost + 2u;
    return (uint32_t) zt_udiv64(num, den, 0);
}

bool prov_eligible(const prov_net_t *n, const prov_job_t *j, uint32_t ask)
{
    if (!n || !j || ask == PROV_NONE || ask > PROV_MAX_ASKS) return false;
    const prov_ask_t *a = &n->asks[ask - 1];
    if (!a->used || !a->live || a->asset != j->asset) return false;
    const prov_provider_t *p = prov_provider(n, a->provider);
    if (!p || !p->active || a->offer >= p->desc.n_offers) return false;
    const prov_desc_t *d = &p->desc;
    const prov_offer_t *o = &d->offers[a->offer];
    if (o->rclass != j->rclass || o->unit != j->unit) return false;
    if (!region_in(d->regions[o->region].code, j->regions, j->n_regions)) return false;
    if (!region_in(d->jurisdiction, j->jurisdictions, j->n_jurisdictions)) return false;
    if (j->require_commercial && !o->licence.commercial_use) return false;
    if (j->n_spdx) {
        bool ok = false;
        for (uint32_t i = 0; i < j->n_spdx; i++)
            if (prov_streq(o->licence.spdx, j->spdx_allow[i])) ok = true;
        if (!ok) return false;
    }
    if (j->require_attested && !prov_is_attested(n, a->provider)) return false;
    if (d->sla[o->sla].availability_ppm < j->min_availability_ppm) return false;
    if (j->min_rep_q16 && prov_rep_score_q16(&p->rep) < j->min_rep_q16) return false;
    if (j->net_mask && !(j->net_mask & (1u << d->network))) return false;
    if (j->no_train && !d->honours_no_train) return false;
    if (j->no_retain && !d->zero_retention) return false;
    return true;
}

/* ===== Matching (G3, G7, U1) ===== */

uint64_t prov_cap_units(uint64_t demand, uint32_t providers, uint32_t cap_q32)
{
    uint64_t share = 0, rem = 0, eq;
    if (providers == 0 || demand == 0) return 0;
    if (!prov_muldiv(demand, cap_q32, PROV_Q32_ONE, &share)) share = demand;
    eq = zt_udiv64(demand, providers, &rem);
    if (rem) eq++;
    return share > eq ? share : eq;
}

uint64_t prov_filled_units(const prov_net_t *n, uint32_t provider, uint8_t rclass, uint8_t unit,
                           uint16_t asset)
{
    uint64_t s = 0;
    const prov_provider_t *p = prov_provider(n, provider);
    if (!p) return 0;
    for (uint32_t i = 0; i < PROV_MAX_FILLS; i++) {
        const prov_fill_t *f = &n->fills[i];
        if (!f->used || f->provider != provider || f->cycle != n->cycle || f->asset != asset ||
            f->state == PROV_FILL_RELEASED || f->offer >= p->desc.n_offers)
            continue;
        const prov_offer_t *o = &p->desc.offers[f->offer];
        if (o->rclass == rclass && o->unit == unit) s += f->qty;
    }
    return s;
}

const prov_fill_t *prov_fill(const prov_net_t *n, uint32_t fill)
{
    if (!n || fill == PROV_NONE || fill > PROV_MAX_FILLS || !n->fills[fill - 1].used) return 0;
    return &n->fills[fill - 1];
}

static bool in_market(const prov_net_t *n, const prov_ask_t *a, uint8_t rc, uint8_t unit,
                      uint16_t asset)
{
    if (!a->used || !a->live || a->qty == 0 || a->asset != asset) return false;
    const prov_provider_t *p = prov_provider(n, a->provider);
    if (!p || !p->active || a->offer >= p->desc.n_offers) return false;
    return p->desc.offers[a->offer].rclass == rc && p->desc.offers[a->offer].unit == unit;
}

static void job_id(const prov_net_t *n, uint32_t slot, const prov_fill_t *f, uint8_t out[32])
{
    prov_hb_t h;
    prov_hb_init(&h, "zxv-prov-job-v1");
    prov_hb_u32(&h, slot);
    prov_hb_u64(&h, f->cycle);
    prov_hb_u64(&h, n->seq);
    prov_hb_put(&h, n->prov[f->provider - 1].id, PROV_ID_LEN);
    prov_hb_put(&h, n->users[f->user - 1].id, PROV_ID_LEN);
    prov_hb_put(&h, f->desc_hash, PROV_HASH_LEN);
    prov_hb_u8(&h, f->offer);
    prov_hb_u32(&h, f->asset);
    prov_hb_u64(&h, f->qty);
    prov_hb_u64(&h, f->unit_price);
    prov_hb_u8(&h, (uint8_t) ((f->no_train ? 1u : 0u) | (f->no_retain ? 2u : 0u)));
    prov_hb_put(&h, f->request_hash, PROV_HASH_LEN);
    prov_hb_final(&h, out);
}

int prov_match(prov_net_t *n, uint8_t rclass, uint8_t unit, uint16_t asset, uint32_t *n_fills)
{
    uint32_t bi[PROV_MAX_BIDS], ai[PROV_MAX_ASKS], nb = 0, na = 0, made = 0;
    uint64_t used[PROV_MAX_PROVIDERS];
    bool seen[PROV_MAX_PROVIDERS];
    if (n_fills) *n_fills = 0;
    if (!n || rclass >= PROV_RC_COUNT || unit >= PROV_UNIT_COUNT || !prov_asset(n, asset))
        return PROV_ERR_ARG;
    if (!n->cfg.settle) return PROV_ERR_HOOK;

    /* Asks: price asc, then seq asc (insertion sort, stable). */
    for (uint32_t i = 0; i < PROV_MAX_ASKS; i++) {
        if (!in_market(n, &n->asks[i], rclass, unit, asset)) continue;
        uint32_t k = na++;
        while (k > 0) {
            const prov_ask_t *x = &n->asks[ai[k - 1]], *y = &n->asks[i];
            if (x->unit_price < y->unit_price ||
                (x->unit_price == y->unit_price && x->seq <= y->seq))
                break;
            ai[k] = ai[k - 1];
            k--;
        }
        ai[k] = i;
    }
    /* Bids: ceiling desc, then seq asc. */
    for (uint32_t i = 0; i < PROV_MAX_BIDS; i++) {
        const prov_bid_t *b = &n->bids[i];
        if (!b->used || !b->open || b->job.rclass != rclass || b->job.unit != unit ||
            b->job.asset != asset || b->filled >= b->job.qty)
            continue;
        uint32_t k = nb++;
        while (k > 0) {
            const prov_bid_t *x = &n->bids[bi[k - 1]];
            if (x->job.max_unit_price > b->job.max_unit_price ||
                (x->job.max_unit_price == b->job.max_unit_price && x->seq <= b->seq))
                break;
            bi[k] = bi[k - 1];
            k--;
        }
        bi[k] = i;
    }

    /* G7: eligible demand D and live providers P for this market cycle. */
    uint64_t demand = 0;
    uint32_t providers = 0;
    for (uint32_t i = 0; i < PROV_MAX_PROVIDERS; i++) seen[i] = false;
    for (uint32_t k = 0; k < na; k++) {
        uint32_t p = n->asks[ai[k]].provider - 1;
        if (!seen[p]) seen[p] = true, providers++;
    }
    for (uint32_t k = 0; k < nb; k++) {
        const prov_bid_t *b = &n->bids[bi[k]];
        for (uint32_t m = 0; m < na; m++)
            if (n->asks[ai[m]].unit_price <= b->job.max_unit_price &&
                prov_eligible(n, &b->job, ai[m] + 1)) {
                demand += b->job.qty - b->filled;
                break;
            }
    }
    /* The cap is a share of the whole cycle: what is already matched in this
     * market this cycle plus the eligible demand still open. */
    uint64_t already = 0;
    for (uint32_t i = 0; i < PROV_MAX_PROVIDERS; i++) {
        used[i] = n->prov[i].used ? prov_filled_units(n, i + 1, rclass, unit, asset) : 0;
        if (used[i] && !seen[i]) seen[i] = true, providers++; /* matched earlier this cycle */
        already += used[i];
    }
    uint64_t cap = prov_cap_units(demand + already, providers, n->cfg.cap_q32);

    for (uint32_t k = 0; k < nb; k++) {
        prov_bid_t *b = &n->bids[bi[k]];
        for (uint32_t m = 0; m < na && b->filled < b->job.qty; m++) {
            prov_ask_t *a = &n->asks[ai[m]];
            if (a->unit_price > b->job.max_unit_price) break; /* sorted: none cheaper left */
            if (a->qty == 0 || !prov_eligible(n, &b->job, ai[m] + 1)) continue;
            uint32_t p = a->provider - 1;
            if (used[p] >= cap) continue; /* excess flows to the next ask */
            uint64_t want = b->job.qty - b->filled;
            uint64_t take = want;
            if (take > a->qty) take = a->qty;
            if (take > cap - used[p]) take = cap - used[p];
            uint64_t afford = zt_udiv64(b->job.max_total - b->spent, a->unit_price, 0);
            if (take > afford) take = afford;
            if (!b->job.allow_split && take != want) continue;
            if (take == 0) continue;
            uint32_t slot = PROV_MAX_FILLS;
            for (uint32_t s = 0; s < PROV_MAX_FILLS; s++)
                if (!n->fills[s].used) {
                    slot = s;
                    break;
                }
            if (slot == PROV_MAX_FILLS) {
                if (n_fills) *n_fills = made;
                return PROV_ERR_FULL;
            }
            prov_fill_t *f = &n->fills[slot];
            prov_memset(f, 0, sizeof *f);
            f->bid = bi[k] + 1;
            f->ask = ai[m] + 1;
            f->provider = a->provider;
            f->user = b->user;
            f->offer = a->offer;
            f->asset = asset;
            f->qty = take;
            f->unit_price = a->unit_price;
            f->hold = take * a->unit_price; /* <= max_total - spent, no overflow */
            f->cycle = n->cycle;
            f->no_train = b->job.no_train;
            f->no_retain = b->job.no_retain;
            prov_memcpy(f->request_hash, b->job.request_hash, PROV_HASH_LEN);
            prov_memcpy(f->desc_hash, n->prov[p].desc_hash, PROV_HASH_LEN);
            n->seq++;
            job_id(n, slot, f, f->job_id);

            prov_settlement_t s;
            prov_memset(&s, 0, sizeof s);
            s.kind = PROV_SETTLE_HOLD;
            s.asset = asset;
            s.user = f->user;
            s.provider = f->provider;
            s.fill = slot + 1;
            s.hold = f->hold;
            prov_memcpy(s.ref, f->job_id, PROV_HASH_LEN);
            if (n->cfg.settle(n->cfg.settle_ctx, &s) != 0) {
                prov_memset(f, 0, sizeof *f); /* user could not fund: no fill */
                break;
            }
            f->used = true;
            f->state = PROV_FILL_RESERVED;
            a->qty -= take;
            used[p] += take;
            b->filled += take;
            b->spent += f->hold;
            made++;
        }
        if (b->filled >= b->job.qty) b->open = false;
    }
    if (n_fills) *n_fills = made;
    return PROV_OK;
}
