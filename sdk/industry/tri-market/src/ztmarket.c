/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztmarket.c — tri-market: e-commerce escrow on the ledger, released by the
 * buyer's signed delivery confirmation minus the libzxv assurance fee;
 * disputes held as BOTH for an arbiter whose signed ruling splits the
 * escrow exactly; timeouts refund. Fees come from zxv_fee_*, cross-currency
 * amounts from zxv_vhub_quote. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztmarket.h"

#define DOMAIN "tri-market"
#define MAX_ARBITERS 16u

typedef struct {
    char id[ZTM_ID_MAX + 1];
    uint32_t buyer, seller;
    int64_t price;
    int32_t alg;
    uint8_t pk[ZIND_PK_BYTES];
    uint64_t deadline;
    int32_t state;
    uint8_t tag;
    uint8_t create_hash[32], dispute_hash[32];
    int64_t fee, to_seller, to_buyer;
    int quoted;
    uint8_t list_slot;
    uint64_t list_price, vbills, list_epoch, pay_epoch;
} order_t;

typedef struct {
    uint32_t account;
    uint64_t rem;
} carry_t;

struct ztm_desk {
    const zxv_cap *cap;
    char ccy[4];
    uint8_t minor;
    const zind_signer *signer;
    zind_qsink *qs;
    uint32_t escrow, fee_acct[ZXV_FEE_BUCKETS];
    char refp[9];
    uint32_t ref_ctr;
    uint8_t arbiters[MAX_ARBITERS][32];
    uint32_t n_arbiters;
    zxv_vhub *hub;
    uint8_t pay_slot;
    order_t *orders;
    uint32_t n, alloc;
    carry_t *carry;
    uint32_t n_carry, carry_alloc;
    zind_seen *ids, *proofs;
    zind_chain *chain;
};

const char *ztm_status_name(int32_t status) {
    switch (status) {
    case ZTM_E_SPLIT: return "ZTM_E_SPLIT";
    case ZTM_E_ARBITER: return "ZTM_E_ARBITER";
    case ZTM_E_EXPIRED: return "ZTM_E_EXPIRED";
    case ZTM_E_EARLY: return "ZTM_E_EARLY";
    case ZTM_E_NOHUB: return "ZTM_E_NOHUB";
    default: return zind_status_name(status);
    }
}

static order_t *get(const ztm_desk *d, uint32_t o) {
    if (!d || o == 0 || o > d->n) {
        zind_set_error("ztm: unknown order %u", o);
        return NULL;
    }
    return &d->orders[o - 1];
}

static int32_t append(ztm_desk *d, uint16_t kind, const zind_wr *w, uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(d->cap, d->chain, d->signer, kind, w->buf, w->len, NULL, hash);
}

static int32_t post(ztm_desk *d, uint32_t from, uint32_t to, int64_t amount, uint32_t o,
                    char suffix, uint64_t *entry) {
    char ref[36];
    snprintf(ref, sizeof ref, "TM%s-%u-%u%c", d->refp, ++d->ref_ctr, o, suffix);
    return zxv_ledger_post_cap(d->cap, from, to, amount, d->ccy, ref, entry);
}

static void quarantine(ztm_desk *d, uint8_t tag, uint8_t reason, uint32_t o, const uint8_t *data,
                       size_t len) {
    uint8_t subj[32];
    if (!d->qs || zind_subject("tri-market/event", data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(d->qs, tag, reason, o, subj, NULL);
}

static void forged(ztm_desk *d, uint32_t o, const uint8_t *data, size_t len) {
    uint8_t ftag = 0;
    if (zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) == ZXV_OK)
        quarantine(d, ftag, ZIND_REASON_TAMPER, o, data, len);
}

int32_t ztm_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, ztm_desk **out) {
    static const char *const fee_names[ZXV_FEE_BUCKETS] = {
        "fee:reserve-floor:tri-market", "fee:vbill-pool:tri-market", "fee:infra:tri-market",
        "fee:regen:tri-market"};
    ztm_desk *d;
    int32_t st;
    uint8_t minor = 0;
    if (!cap || !ccy || !desk_signer || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("ztm_desk_create: the cap needs LEDGER_POST, LEDGER_READ and CRYPTO_SIGN");
        return st;
    }
    if (zxv_iso4217_by_alpha(ccy, NULL, &minor, NULL) != ZXV_OK || minor == ZXV_MINOR_NA) {
        zind_set_error("ztm_desk_create: \"%.8s\" is not an ISO 4217 currency with minor units",
                       ccy);
        return ZXV_E_CURRENCY;
    }
    st = zxv_ccy_enable_cap(cap, ccy);
    if (st != ZXV_OK && st != ZXV_E_DUPLICATE) return st;
    d = (ztm_desk *) calloc(1, sizeof *d);
    if (!d) return ZXV_E_NOMEM;
    d->cap = cap;
    memcpy(d->ccy, ccy, 3);
    d->minor = minor;
    d->signer = desk_signer;
    {
        const uint8_t *k = zind_signer_key_id(desk_signer);
        snprintf(d->refp, sizeof d->refp, "%02x%02x%02x%02x", k[0], k[1], k[2], k[3]);
    }
    st = zxv_ledger_open_account_cap(cap, "escrow:tri-market", ccy, ZXV_ACCT_HOLDER, &d->escrow);
    for (int i = 0; st == ZXV_OK && i < ZXV_FEE_BUCKETS; i++)
        st = zxv_ledger_open_account_cap(cap, fee_names[i], ccy, ZXV_ACCT_HOLDER, &d->fee_acct[i]);
    if (st == ZXV_OK) st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &d->chain);
    if (st == ZXV_OK) st = zind_seen_create(ZTM_MAX_ORDERS, &d->ids);
    if (st == ZXV_OK) st = zind_seen_create(ZTM_MAX_ORDERS, &d->proofs);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &d->qs);
    if (st != ZXV_OK) {
        ztm_desk_destroy(d);
        return st;
    }
    *out = d;
    return ZXV_OK;
}

void ztm_desk_destroy(ztm_desk *d) {
    if (!d) return;
    zind_chain_destroy(d->chain);
    zind_seen_destroy(d->ids);
    zind_seen_destroy(d->proofs);
    zind_qsink_destroy(d->qs);
    free(d->orders);
    free(d->carry);
    free(d);
}

uint32_t ztm_escrow_account(const ztm_desk *d) { return d ? d->escrow : UINT32_MAX; }
const zind_chain *ztm_records(const ztm_desk *d) { return d ? d->chain : NULL; }

int32_t ztm_fee_accounts(const ztm_desk *d, uint32_t out[ZXV_FEE_BUCKETS]) {
    if (!d || !out) return ZXV_E_ARG;
    memcpy(out, d->fee_acct, sizeof d->fee_acct);
    return ZXV_OK;
}

int32_t ztm_arbiter_add(ztm_desk *d, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!d || !pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < d->n_arbiters; i++)
        if (memcmp(d->arbiters[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (d->n_arbiters >= MAX_ARBITERS) return ZXV_E_CAPACITY;
    memcpy(d->arbiters[d->n_arbiters++], id, 32);
    return ZXV_OK;
}

int32_t ztm_hub_attach(ztm_desk *d, zxv_vhub *hub, uint8_t pay_slot, uint8_t pay_minor_units) {
    uint8_t buf[16];
    zind_wr w;
    int32_t st;
    if (!d || !hub) return ZXV_E_ARG;
    if (d->hub) return ZXV_E_DUPLICATE;
    if (pay_minor_units != d->minor) {
        zind_set_error("ztm_hub_attach: the desk currency %s has %u minor units, not %u", d->ccy,
                       d->minor, pay_minor_units);
        return ZXV_E_CURRENCY;
    }
    /* the slot must exist on a hub of the desk cap's context */
    st = zxv_vhub_ccy_info(d->cap, hub, pay_slot, NULL, NULL, NULL, NULL, NULL, NULL);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_u8(&w, pay_slot);
    zind_wr_u8(&w, pay_minor_units);
    st = append(d, ZTM_REC_HUB, &w, NULL);
    if (st != ZXV_OK) return st;
    d->hub = hub;
    d->pay_slot = pay_slot;
    return ZXV_OK;
}

static int32_t create(ztm_desk *d, const char *order_id, uint32_t buyer, uint32_t seller,
                      int64_t price, int32_t alg, const uint8_t *pk, size_t pk_len,
                      uint64_t deadline, uint64_t now, const order_t *quote, uint32_t *out) {
    order_t o;
    uint8_t idh[32], buf[256], kh[32];
    int32_t present = 0, st;
    zind_wr w;
    if (!out || !pk) return ZXV_E_ARG;
    if (!zind_ident_ok(order_id, ZTM_ID_MAX)) {
        zind_set_error("ztm_order_create: an order id is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (!((alg == ZTM_ALG_ED25519 && pk_len == ZXV_ED25519_PK_BYTES) ||
          (alg == ZTM_ALG_MLDSA87 && pk_len == ZIND_PK_BYTES))) {
        zind_set_error("ztm_order_create: the key length does not match the algorithm");
        return ZXV_E_ARG;
    }
    if (price <= 0 || price > ZXV_AMOUNT_MAX) {
        zind_set_error("ztm_order_create: the price must be 1..ZXV_AMOUNT_MAX minor units");
        return ZXV_E_RANGE;
    }
    if (deadline <= now || deadline - now > ZTM_MAX_WINDOW) {
        zind_set_error("ztm_order_create: the deadline is 1 second .. 90 days after now");
        return ZXV_E_ARG;
    }
    if (buyer == seller || buyer == d->escrow || seller == d->escrow) {
        zind_set_error("ztm_order_create: buyer, seller and escrow must be distinct accounts");
        return ZXV_E_ARG;
    }
    for (int i = 0; i < ZXV_FEE_BUCKETS; i++)
        if (buyer == d->fee_acct[i] || seller == d->fee_acct[i]) return ZXV_E_ARG;
    st = zind_subject("tri-market/id", (const uint8_t *) order_id, strlen(order_id), idh);
    if (st != ZXV_OK) return st;
    if (zind_seen_has(d->ids, idh, &present) != ZXV_OK || present) {
        zind_set_error("ztm_order_create: order id %s was already used on this desk", order_id);
        return ZXV_E_DUPLICATE;
    }
    if (d->n >= ZTM_MAX_ORDERS) return ZXV_E_CAPACITY;
    {
        char c[16];
        size_t cl = 0;
        st = zxv_ledger_account_ccy_cap(d->cap, seller, c, sizeof c, &cl);
        if (st != ZXV_OK) return st;
        if (strcmp(c, d->ccy) != 0) return ZXV_E_CURRENCY;
    }
    if (d->n == d->alloc) {
        uint32_t a = d->alloc ? d->alloc * 2 : 16;
        order_t *np = (order_t *) realloc(d->orders, (size_t) a * sizeof *np);
        if (!np) return ZXV_E_NOMEM;
        d->orders = np;
        d->alloc = a;
    }
    if (quote)
        o = *quote;
    else
        memset(&o, 0, sizeof o);
    memcpy(o.id, order_id, strlen(order_id) + 1);
    o.buyer = buyer;
    o.seller = seller;
    o.price = price;
    o.alg = alg;
    memcpy(o.pk, pk, pk_len);
    o.deadline = deadline;
    o.state = ZTM_ST_ESCROW;
    st = zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &o.tag);
    if (st != ZXV_OK) return st;
    st = post(d, buyer, d->escrow, price, d->n + 1, 'E', NULL);
    if (st != ZXV_OK) {
        zind_set_error("ztm_order_create: moving %lld into escrow failed (%s)", (long long) price,
                       zxv_status_name(st));
        return st;
    }
    (void) zxv_sha3_256(pk, pk_len, kh);
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o.id);
    zind_wr_u32(&w, buyer);
    zind_wr_u32(&w, seller);
    zind_wr_u64(&w, (uint64_t) price);
    zind_wr_str(&w, d->ccy);
    zind_wr_u8(&w, (uint8_t) alg);
    zind_wr_bytes(&w, kh, 32);
    zind_wr_u64(&w, deadline);
    zind_wr_u64(&w, now);
    zind_wr_u8(&w, (uint8_t) o.quoted);
    if (o.quoted) {
        zind_wr_u8(&w, o.list_slot);
        zind_wr_u64(&w, o.list_price);
        zind_wr_u64(&w, o.vbills);
        zind_wr_u64(&w, o.list_epoch);
        zind_wr_u8(&w, d->pay_slot);
        zind_wr_u64(&w, o.pay_epoch);
    }
    st = append(d, ZTM_REC_CREATE, &w, o.create_hash);
    if (st != ZXV_OK) {
        (void) post(d, d->escrow, buyer, price, d->n + 1, 'U', NULL);
        return st;
    }
    (void) zind_seen_add(d->ids, idh);
    d->orders[d->n++] = o;
    *out = d->n;
    return ZXV_OK;
}

int32_t ztm_order_create(ztm_desk *d, const char *order_id, uint32_t buyer_account,
                         uint32_t seller_account, int64_t price_minor, int32_t buyer_alg,
                         const uint8_t *buyer_pk, size_t pk_len, uint64_t deadline, uint64_t now,
                         uint32_t *out_order) {
    if (!d) return ZXV_E_ARG;
    return create(d, order_id, buyer_account, seller_account, price_minor, buyer_alg, buyer_pk,
                  pk_len, deadline, now, NULL, out_order);
}

int32_t ztm_order_create_quoted(ztm_desk *d, const char *order_id, uint32_t buyer_account,
                                uint32_t seller_account, uint8_t list_slot, uint64_t list_price,
                                int32_t buyer_alg, const uint8_t *buyer_pk, size_t pk_len,
                                uint64_t deadline, uint64_t now, uint32_t *out_order) {
    order_t q;
    uint64_t amount = 0, vb = 0, recv = 0;
    int32_t st;
    if (!d) return ZXV_E_ARG;
    if (!d->hub) {
        zind_set_error("ztm_order_create_quoted: no hub is attached to this desk");
        return ZTM_E_NOHUB;
    }
    if (list_price == 0) return ZXV_E_RANGE;
    memset(&q, 0, sizeof q);
    st = zxv_vhub_quote(d->cap, d->hub, list_slot, d->pay_slot, list_price, &amount, &vb, &recv);
    if (st != ZXV_OK) return st;
    if (amount != list_price || recv == 0) {
        zind_set_error("ztm_order_create_quoted: %llu in slot %u has no exact price under the "
                       "pegs (largest exact amount %llu)",
                       (unsigned long long) list_price, list_slot, (unsigned long long) amount);
        return ZIND_E_INEXACT;
    }
    if (recv > (uint64_t) ZXV_AMOUNT_MAX) return ZXV_E_RANGE;
    st = zxv_vhub_ccy_info(d->cap, d->hub, list_slot, NULL, NULL, NULL, NULL, NULL,
                           &q.list_epoch);
    if (st == ZXV_OK)
        st = zxv_vhub_ccy_info(d->cap, d->hub, d->pay_slot, NULL, NULL, NULL, NULL, NULL,
                               &q.pay_epoch);
    if (st != ZXV_OK) return st;
    q.quoted = 1;
    q.list_slot = list_slot;
    q.list_price = list_price;
    q.vbills = vb;
    return create(d, order_id, buyer_account, seller_account, (int64_t) recv, buyer_alg, buyer_pk,
                  pk_len, deadline, now, &q, out_order);
}

int32_t ztm_order_quote(const ztm_desk *d, uint32_t order, uint8_t *out_list_slot,
                        uint64_t *out_list_price, uint64_t *out_vbills, uint64_t *out_list_epoch,
                        uint64_t *out_pay_epoch) {
    const order_t *o = get(d, order);
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!o->quoted) return ZXV_E_STATE;
    if (out_list_slot) *out_list_slot = o->list_slot;
    if (out_list_price) *out_list_price = o->list_price;
    if (out_vbills) *out_vbills = o->vbills;
    if (out_list_epoch) *out_list_epoch = o->list_epoch;
    if (out_pay_epoch) *out_pay_epoch = o->pay_epoch;
    return ZXV_OK;
}

int32_t ztm_order_find(const ztm_desk *d, const char *order_id, uint32_t *out_order) {
    if (!d || !order_id || !out_order) return ZXV_E_ARG;
    for (uint32_t i = 0; i < d->n; i++)
        if (strcmp(d->orders[i].id, order_id) == 0) {
            *out_order = i + 1;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

int32_t ztm_ship(ztm_desk *d, uint32_t order, const char *shipment_ref, uint64_t time) {
    order_t *o = get(d, order);
    uint8_t nt = 0, buf[96];
    zind_wr w;
    int32_t st;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!zind_ident_ok(shipment_ref, ZTM_ID_MAX)) return ZXV_E_ARG;
    if (o->state == ZTM_ST_DISPUTED) return ZIND_E_HELD;
    if (o->state != ZTM_ST_ESCROW) return ZXV_E_STATE;
    st = zxv_hex_step(o->tag, ZXV_HEX_PHASE_EMERGING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o->id);
    zind_wr_str(&w, shipment_ref);
    zind_wr_u64(&w, time);
    st = append(d, ZTM_REC_SHIP, &w, NULL);
    if (st != ZXV_OK) return st;
    o->tag = nt;
    o->state = ZTM_ST_SHIPPED;
    return ZXV_OK;
}

int32_t ztm_order_hash(const ztm_desk *d, uint32_t order, uint8_t out[32]) {
    const order_t *o = get(d, order);
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out) return ZXV_E_ARG;
    memcpy(out, o->create_hash, 32);
    return ZXV_OK;
}

int32_t ztm_confirm_bytes(const char *order_id, const uint8_t order_hash[32], int64_t price,
                          uint64_t time, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    if (!out_len || !order_hash || price <= 0 || !zind_ident_ok(order_id, ZTM_ID_MAX))
        return ZXV_E_ARG;
    *out_len = 14 + 1 + strlen(order_id) + 32 + 8 + 8;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztm/confirm/v1", 14);
    zind_wr_str(&w, order_id);
    zind_wr_bytes(&w, order_hash, 32);
    zind_wr_u64(&w, (uint64_t) price);
    zind_wr_u64(&w, time);
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t ztm_confirm_message(const ztm_desk *d, uint32_t order, uint64_t time, uint8_t *buf,
                            size_t cap, size_t *out_len) {
    const order_t *o = get(d, order);
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    return ztm_confirm_bytes(o->id, o->create_hash, o->price, time, buf, cap, out_len);
}

/* Move the tag to Manifest from wherever it is (one Gray step at a time). */
static int32_t to_manifest(uint8_t tag, uint8_t *out) {
    uint8_t ph = 0, t = tag;
    int32_t st = zxv_hex_unpack(tag, NULL, NULL, &ph);
    while (st == ZXV_OK && ph != ZXV_HEX_PHASE_MANIFEST) {
        uint8_t next = 0;
        st = zxv_hex_phase_next(ph, &next);
        if (st == ZXV_OK) st = zxv_hex_step(t, next, &t);
        ph = next;
    }
    *out = t;
    return st;
}

static carry_t *carry_slot(ztm_desk *d, uint32_t account, int create_it) {
    for (uint32_t i = 0; i < d->n_carry; i++)
        if (d->carry[i].account == account) return &d->carry[i];
    if (!create_it) return NULL;
    if (d->n_carry == d->carry_alloc) {
        uint32_t a = d->carry_alloc ? d->carry_alloc * 2 : 16;
        carry_t *nc = (carry_t *) realloc(d->carry, (size_t) a * sizeof *nc);
        if (!nc) return NULL;
        d->carry = nc;
        d->carry_alloc = a;
    }
    d->carry[d->n_carry].account = account;
    d->carry[d->n_carry].rem = 0;
    return &d->carry[d->n_carry++];
}

/* Pay `gross` out of escrow to the seller: the library's fee (with the
 * seller's carry) and its four-way split, the rest to the seller. */
static int32_t pay_seller(ztm_desk *d, uint32_t ord, order_t *o, int64_t gross, int64_t *out_fee) {
    carry_t *c = carry_slot(d, o->seller, 1);
    uint64_t fee = 0, rem = 0, parts[ZXV_FEE_BUCKETS];
    int32_t st;
    if (!c) return ZXV_E_NOMEM;
    st = zxv_fee_assure_carry(c->rem, (uint64_t) gross, &fee, &rem);
    if (st == ZXV_OK) st = zxv_fee_split(fee, parts);
    if (st != ZXV_OK) return st;
    if (fee >= (uint64_t) gross) return ZXV_E_INTERNAL; /* 0.08889% never reaches the gross */
    st = post(d, d->escrow, o->seller, gross - (int64_t) fee, ord, 'S', NULL);
    for (int i = 0; st == ZXV_OK && i < ZXV_FEE_BUCKETS; i++)
        if (parts[i]) st = post(d, d->escrow, d->fee_acct[i], (int64_t) parts[i], ord, 'f', NULL);
    if (st != ZXV_OK) return st;
    c->rem = rem;
    *out_fee = (int64_t) fee;
    return ZXV_OK;
}

int32_t ztm_confirm(ztm_desk *d, uint32_t order, uint64_t time, const uint8_t *sig,
                    size_t sig_len) {
    order_t *o = get(d, order);
    uint8_t msg[160], proof[32], nt = 0, buf[160];
    size_t len = 0;
    int64_t fee = 0;
    zind_wr w;
    int32_t st, present = 0;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!sig) return ZXV_E_ARG;
    switch (o->state) {
    case ZTM_ST_ESCROW:
    case ZTM_ST_SHIPPED: break;
    case ZTM_ST_DISPUTED:
        zind_set_error("ztm_confirm: order %s is disputed (BOTH) and frozen", o->id);
        return ZIND_E_HELD;
    default:
        zind_set_error("ztm_confirm: order %s no longer holds escrow; nothing is released twice",
                       o->id);
        return ZIND_E_REPLAY;
    }
    if ((o->alg == ZTM_ALG_ED25519 && sig_len != ZXV_ED25519_SIG_BYTES) ||
        (o->alg == ZTM_ALG_MLDSA87 && sig_len != ZIND_SIG_BYTES))
        return ZXV_E_ARG;
    st = ztm_confirm_bytes(o->id, o->create_hash, o->price, time, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    if (o->alg == ZTM_ALG_ED25519)
        st = zxv_ed25519_verify(d->cap, o->pk, msg, len, sig);
    else
        st = zind_verify(d->cap, o->pk, msg, len, ZTM_CONFIRM_CTX, sig);
    if (st != ZXV_OK) {
        zind_set_error("ztm_confirm: the buyer's signature does not verify");
        if (st == ZXV_E_VERIFY) forged(d, order, sig, sig_len);
        return st;
    }
    if (time > o->deadline) {
        zind_set_error("ztm_confirm: dated %llu, after the deadline %llu",
                       (unsigned long long) time, (unsigned long long) o->deadline);
        return ZTM_E_EXPIRED;
    }
    st = zind_subject("tri-market/confirm", msg, len, proof);
    if (st != ZXV_OK) return st;
    if (zind_seen_has(d->proofs, proof, &present) != ZXV_OK || present) return ZIND_E_REPLAY;
    st = zxv_hex_set(o->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK) st = to_manifest(nt, &nt);
    if (st == ZXV_OK) st = pay_seller(d, order, o, o->price, &fee);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o->id);
    zind_wr_u64(&w, time);
    zind_wr_bytes(&w, proof, 32);
    zind_wr_u64(&w, (uint64_t) o->price);
    zind_wr_u64(&w, (uint64_t) fee);
    st = append(d, ZTM_REC_RELEASE, &w, NULL);
    if (st != ZXV_OK) return st;
    (void) zind_seen_add(d->proofs, proof);
    o->tag = nt;
    o->state = ZTM_ST_RELEASED;
    o->fee = fee;
    o->to_seller = o->price - fee;
    o->to_buyer = 0;
    return ZXV_OK;
}

int32_t ztm_dispute(ztm_desk *d, uint32_t order, int32_t party, uint32_t reason) {
    order_t *o = get(d, order);
    uint8_t nt = 0, buf[64], h[32];
    zind_wr w;
    int32_t st;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (party != ZTM_PARTY_BUYER && party != ZTM_PARTY_SELLER) return ZXV_E_ARG;
    if (o->state == ZTM_ST_DISPUTED) return ZIND_E_HELD;
    if (o->state != ZTM_ST_ESCROW && o->state != ZTM_ST_SHIPPED) {
        zind_set_error("ztm_dispute: order %s holds no escrow any more", o->id);
        return ZXV_E_STATE;
    }
    st = zxv_hex_set(o->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o->id);
    zind_wr_u8(&w, (uint8_t) party);
    zind_wr_u32(&w, reason);
    st = append(d, ZTM_REC_DISPUTE, &w, h);
    if (st != ZXV_OK) return st;
    memcpy(o->dispute_hash, h, 32);
    o->tag = nt;
    o->state = ZTM_ST_DISPUTED;
    quarantine(d, nt, ZIND_REASON_HELD_BOTH, order, h, 32);
    return ZXV_OK;
}

static int32_t ruling_msg(const order_t *o, int64_t to_seller, int64_t to_buyer, uint8_t *buf,
                          size_t cap, size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztm/ruling/v1", 13);
    zind_wr_str(&w, o->id);
    zind_wr_bytes(&w, o->dispute_hash, 32);
    zind_wr_u64(&w, (uint64_t) to_seller);
    zind_wr_u64(&w, (uint64_t) to_buyer);
    *out_len = 13 + 1 + strlen(o->id) + 32 + 16;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

static int32_t split_ok(const order_t *o, int64_t to_seller, int64_t to_buyer) {
    if (to_seller < 0 || to_buyer < 0 || to_seller > o->price || to_buyer != o->price - to_seller) {
        zind_set_error("ztm: a ruling must split %lld exactly (got %lld + %lld)",
                       (long long) o->price, (long long) to_seller, (long long) to_buyer);
        return ZTM_E_SPLIT;
    }
    return ZXV_OK;
}

int32_t ztm_ruling_message(const ztm_desk *d, uint32_t order, int64_t to_seller, int64_t to_buyer,
                           uint8_t *buf, size_t cap, size_t *out_len) {
    const order_t *o = get(d, order);
    int32_t st;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out_len) return ZXV_E_ARG;
    if (o->state != ZTM_ST_DISPUTED) return ZXV_E_STATE;
    st = split_ok(o, to_seller, to_buyer);
    if (st != ZXV_OK) return st;
    return ruling_msg(o, to_seller, to_buyer, buf, cap, out_len);
}

int32_t ztm_rule(ztm_desk *d, uint32_t order, const uint8_t arbiter_pk[ZIND_PK_BYTES],
                 int64_t to_seller, int64_t to_buyer, const uint8_t sig[ZIND_SIG_BYTES]) {
    order_t *o = get(d, order);
    uint8_t id[32], msg[128], mh[32], nt = 0, buf[160];
    size_t len = 0;
    int64_t fee = 0;
    uint32_t i;
    zind_wr w;
    int32_t st;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!arbiter_pk || !sig) return ZXV_E_ARG;
    st = zxv_cap_check(d->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("ztm_rule: the desk cap needs ATTEST to apply a ruling");
        return st;
    }
    if (o->state != ZTM_ST_DISPUTED) {
        zind_set_error("ztm_rule: order %s is not disputed", o->id);
        return (o->state == ZTM_ST_RULED || o->state == ZTM_ST_RELEASED ||
                o->state == ZTM_ST_REFUNDED)
                   ? ZIND_E_REPLAY
                   : ZXV_E_STATE;
    }
    st = split_ok(o, to_seller, to_buyer);
    if (st != ZXV_OK) return st;
    st = zind_key_id(arbiter_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < d->n_arbiters; i++)
        if (memcmp(d->arbiters[i], id, 32) == 0) break;
    if (i == d->n_arbiters) {
        zind_set_error("ztm_rule: the key is not a registered arbiter");
        return ZTM_E_ARBITER;
    }
    st = ruling_msg(o, to_seller, to_buyer, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(d->cap, arbiter_pk, msg, len, ZTM_RULING_CTX, sig);
    if (st != ZXV_OK) {
        zind_set_error("ztm_rule: the arbiter's signature does not verify");
        if (st == ZXV_E_VERIFY) forged(d, order, msg, len);
        return st;
    }
    st = zxv_hex_set(o->tag, to_seller > 0 ? ZXV_HEX_BASE_TRUE : ZXV_HEX_BASE_FALSE,
                     ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK && to_seller > 0) st = to_manifest(nt, &nt);
    if (st == ZXV_OK && to_seller > 0) st = pay_seller(d, order, o, to_seller, &fee);
    if (st == ZXV_OK && to_buyer > 0) st = post(d, d->escrow, o->buyer, to_buyer, order, 'B', NULL);
    if (st != ZXV_OK) return st;
    (void) zxv_sha3_256(msg, len, mh);
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o->id);
    zind_wr_bytes(&w, id, 32);
    zind_wr_bytes(&w, mh, 32);
    zind_wr_u64(&w, (uint64_t) to_seller);
    zind_wr_u64(&w, (uint64_t) to_buyer);
    zind_wr_u64(&w, (uint64_t) fee);
    st = append(d, ZTM_REC_RULING, &w, NULL);
    if (st != ZXV_OK) return st;
    o->tag = nt;
    o->state = ZTM_ST_RULED;
    o->fee = fee;
    o->to_seller = to_seller - fee;
    o->to_buyer = to_buyer;
    return ZXV_OK;
}

int32_t ztm_expire(ztm_desk *d, uint32_t order, uint64_t now) {
    order_t *o = get(d, order);
    uint8_t nt = 0, buf[64];
    zind_wr w;
    int32_t st;
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (o->state == ZTM_ST_DISPUTED) {
        zind_set_error("ztm_expire: order %s is disputed; only an arbiter moves it", o->id);
        return ZIND_E_HELD;
    }
    if (o->state != ZTM_ST_ESCROW && o->state != ZTM_ST_SHIPPED) return ZIND_E_REPLAY;
    if (now <= o->deadline) {
        zind_set_error("ztm_expire: order %s is open until %llu", o->id,
                       (unsigned long long) o->deadline);
        return ZTM_E_EARLY;
    }
    st = zxv_hex_set(o->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_ENTROPIC, &nt);
    if (st == ZXV_OK) st = post(d, d->escrow, o->buyer, o->price, order, 'T', NULL);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, o->id);
    zind_wr_u64(&w, now);
    st = append(d, ZTM_REC_REFUND, &w, NULL);
    if (st != ZXV_OK) return st;
    o->tag = nt;
    o->state = ZTM_ST_REFUNDED;
    o->to_buyer = o->price;
    return ZXV_OK;
}

int32_t ztm_order_state(const ztm_desk *d, uint32_t order, int32_t *out_state, uint8_t *out_tag) {
    const order_t *o = get(d, order);
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = o->state;
    if (out_tag) *out_tag = o->tag;
    return ZXV_OK;
}

int32_t ztm_order_amounts(const ztm_desk *d, uint32_t order, int64_t *out_price,
                          int64_t *out_fee, int64_t *out_to_seller, int64_t *out_to_buyer) {
    const order_t *o = get(d, order);
    if (!o) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_price) *out_price = o->price;
    if (out_fee) *out_fee = o->fee;
    if (out_to_seller) *out_to_seller = o->to_seller;
    if (out_to_buyer) *out_to_buyer = o->to_buyer;
    return ZXV_OK;
}

int32_t ztm_fee_carry(const ztm_desk *d, uint32_t seller_account, uint64_t *out_rem) {
    if (!d || !out_rem) return ZXV_E_ARG;
    *out_rem = 0;
    for (uint32_t i = 0; i < d->n_carry; i++)
        if (d->carry[i].account == seller_account) {
            *out_rem = d->carry[i].rem;
            return ZXV_OK;
        }
    return ZXV_OK;
}

int32_t ztm_escrow_owed(const ztm_desk *d, int64_t *out_minor) {
    int64_t sum = 0;
    if (!d || !out_minor) return ZXV_E_ARG;
    for (uint32_t i = 0; i < d->n; i++) {
        int32_t s = d->orders[i].state;
        if (s == ZTM_ST_ESCROW || s == ZTM_ST_SHIPPED || s == ZTM_ST_DISPUTED)
            sum += d->orders[i].price; /* the ledger held the total, so it fits */
    }
    *out_minor = sum;
    return ZXV_OK;
}
