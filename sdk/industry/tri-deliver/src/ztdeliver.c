/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztdeliver.c — tri-deliver: escrow on the ledger, released by a
 * recipient-signed offline handoff proof; disputes held as BOTH for a
 * registered arbiter. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztdeliver.h"

#define DOMAIN "tri-deliver"
#define MAX_ARBITERS 16u
#define MAX_WINDOW (30ULL * 24 * 3600)

typedef struct {
    char id[ZTD_ID_MAX + 1];
    char courier[ZTD_ID_MAX + 1];
    uint32_t sender, payee;
    int64_t amount;
    int32_t alg;
    uint8_t pk[ZIND_PK_BYTES];
    uint64_t window, dispatch_time;
    int32_t state;
    uint8_t tag;
    uint8_t dispute_hash[32];
} parcel_t;

struct ztd_desk {
    const zxv_cap *cap;
    char ccy[4];
    const zind_signer *signer;
    zind_qsink *qs;
    uint32_t escrow;
    char refp[9]; /* ledger reference prefix: the desk key id, 8 hex digits */
    uint32_t ref_ctr; /* every posting gets a fresh reference */
    uint8_t arbiters[MAX_ARBITERS][32];
    uint32_t n_arbiters;
    parcel_t *parcels;
    uint32_t n, alloc;
    zind_seen *ids, *proofs;
    zind_chain *chain;
};

const char *ztd_status_name(int32_t status) {
    switch (status) {
    case ZTD_E_COURIER: return "ZTD_E_COURIER";
    case ZTD_E_WINDOW: return "ZTD_E_WINDOW";
    case ZTD_E_ARBITER: return "ZTD_E_ARBITER";
    default: return zind_status_name(status);
    }
}

static parcel_t *get(const ztd_desk *d, uint32_t p) {
    if (!d || p == 0 || p > d->n) {
        zind_set_error("ztd: unknown parcel %u", p);
        return NULL;
    }
    return &d->parcels[p - 1];
}

static int32_t append(ztd_desk *d, uint16_t kind, const zind_wr *w, uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(d->cap, d->chain, d->signer, kind, w->buf, w->len, NULL, hash);
}

static int32_t post(ztd_desk *d, uint32_t from, uint32_t to, int64_t amount, uint32_t p,
                    char suffix, uint64_t *entry) {
    char ref[36];
    snprintf(ref, sizeof ref, "TD%s-%u-%u%c", d->refp, ++d->ref_ctr, p, suffix);
    return zxv_ledger_post_cap(d->cap, from, to, amount, d->ccy, ref, entry);
}

static void quarantine(ztd_desk *d, uint8_t tag, uint8_t reason, uint32_t p, const uint8_t *data,
                       size_t len) {
    uint8_t subj[32];
    if (!d->qs || zind_subject("tri-deliver/event", data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(d->qs, tag, reason, p, subj, NULL);
}

int32_t ztd_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, ztd_desk **out) {
    ztd_desk *d;
    int32_t st;
    uint8_t minor = 0;
    if (!cap || !ccy || !desk_signer || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("ztd_desk_create: the cap needs LEDGER_POST, LEDGER_READ and CRYPTO_SIGN");
        return st;
    }
    if (zxv_iso4217_by_alpha(ccy, NULL, &minor, NULL) != ZXV_OK || minor == ZXV_MINOR_NA) {
        zind_set_error("ztd_desk_create: \"%.8s\" is not an ISO 4217 currency with minor units",
                       ccy);
        return ZXV_E_CURRENCY;
    }
    st = zxv_ccy_enable_cap(cap, ccy);
    if (st != ZXV_OK && st != ZXV_E_DUPLICATE) return st;
    d = (ztd_desk *) calloc(1, sizeof *d);
    if (!d) return ZXV_E_NOMEM;
    d->cap = cap;
    memcpy(d->ccy, ccy, 3);
    d->signer = desk_signer;
    {
        const uint8_t *k = zind_signer_key_id(desk_signer);
        snprintf(d->refp, sizeof d->refp, "%02x%02x%02x%02x", k[0], k[1], k[2], k[3]);
    }
    st = zxv_ledger_open_account_cap(cap, "escrow:tri-deliver", ccy, ZXV_ACCT_HOLDER, &d->escrow);
    if (st == ZXV_OK) st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &d->chain);
    if (st == ZXV_OK) st = zind_seen_create(ZTD_MAX_PARCELS, &d->ids);
    if (st == ZXV_OK) st = zind_seen_create(ZTD_MAX_PARCELS, &d->proofs);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &d->qs);
    if (st != ZXV_OK) {
        ztd_desk_destroy(d);
        return st;
    }
    *out = d;
    return ZXV_OK;
}

void ztd_desk_destroy(ztd_desk *d) {
    if (!d) return;
    zind_chain_destroy(d->chain);
    zind_seen_destroy(d->ids);
    zind_seen_destroy(d->proofs);
    zind_qsink_destroy(d->qs);
    free(d->parcels);
    free(d);
}

uint32_t ztd_escrow_account(const ztd_desk *d) { return d ? d->escrow : UINT32_MAX; }
const zind_chain *ztd_records(const ztd_desk *d) { return d ? d->chain : NULL; }

int32_t ztd_arbiter_add(ztd_desk *d, const uint8_t pk[ZIND_PK_BYTES]) {
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

int32_t ztd_parcel_create(ztd_desk *d, const char *parcel_id, uint32_t sender_account,
                          uint32_t payee_account, int64_t amount_minor, int32_t recipient_alg,
                          const uint8_t *recipient_pk, size_t pk_len, uint64_t window_seconds,
                          uint32_t *out_parcel) {
    parcel_t p;
    uint8_t idh[32], buf[256];
    int32_t present = 0, st;
    zind_wr w;
    if (!d || !out_parcel || !recipient_pk) return ZXV_E_ARG;
    if (!zind_ident_ok(parcel_id, ZTD_ID_MAX)) {
        zind_set_error("ztd_parcel_create: a parcel id is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (!((recipient_alg == ZTD_ALG_ED25519 && pk_len == ZXV_ED25519_PK_BYTES) ||
          (recipient_alg == ZTD_ALG_MLDSA87 && pk_len == ZIND_PK_BYTES))) {
        zind_set_error("ztd_parcel_create: the key length does not match the algorithm");
        return ZXV_E_ARG;
    }
    if (amount_minor <= 0 || amount_minor > ZXV_AMOUNT_MAX) {
        zind_set_error("ztd_parcel_create: the amount must be 1..ZXV_AMOUNT_MAX minor units");
        return ZXV_E_RANGE;
    }
    if (window_seconds == 0 || window_seconds > MAX_WINDOW) {
        zind_set_error("ztd_parcel_create: the delivery window is 1 second .. 30 days");
        return ZXV_E_ARG;
    }
    if (sender_account == payee_account || sender_account == d->escrow ||
        payee_account == d->escrow) {
        zind_set_error("ztd_parcel_create: sender, payee and escrow must be distinct accounts");
        return ZXV_E_ARG;
    }
    st = zind_subject("tri-deliver/id", (const uint8_t *) parcel_id, strlen(parcel_id), idh);
    if (st != ZXV_OK) return st;
    if (zind_seen_has(d->ids, idh, &present) != ZXV_OK || present) {
        zind_set_error("ztd_parcel_create: parcel id %s was already used on this desk", parcel_id);
        return ZXV_E_DUPLICATE;
    }
    if (d->n >= ZTD_MAX_PARCELS) return ZXV_E_CAPACITY;
    {
        /* the payee account must exist in the desk currency */
        char c[16];
        size_t cl = 0;
        st = zxv_ledger_account_ccy_cap(d->cap, payee_account, c, sizeof c, &cl);
        if (st != ZXV_OK) return st;
        if (strcmp(c, d->ccy) != 0) return ZXV_E_CURRENCY;
    }
    if (d->n == d->alloc) {
        uint32_t a = d->alloc ? d->alloc * 2 : 16;
        parcel_t *np = (parcel_t *) realloc(d->parcels, (size_t) a * sizeof *np);
        if (!np) return ZXV_E_NOMEM;
        d->parcels = np;
        d->alloc = a;
    }
    memset(&p, 0, sizeof p);
    memcpy(p.id, parcel_id, strlen(parcel_id) + 1);
    p.sender = sender_account;
    p.payee = payee_account;
    p.amount = amount_minor;
    p.alg = recipient_alg;
    memcpy(p.pk, recipient_pk, pk_len);
    p.window = window_seconds;
    p.state = ZTD_ST_ESCROW;
    st = zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &p.tag);
    if (st != ZXV_OK) return st;
    /* the money moves first: a sender without funds creates nothing */
    st = post(d, sender_account, d->escrow, amount_minor, d->n + 1, 'E', NULL);
    if (st != ZXV_OK) {
        zind_set_error("ztd_parcel_create: moving %lld into escrow failed (%s)",
                       (long long) amount_minor, zxv_status_name(st));
        return st;
    }
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p.id);
    zind_wr_u32(&w, sender_account);
    zind_wr_u32(&w, payee_account);
    zind_wr_u64(&w, (uint64_t) amount_minor);
    zind_wr_str(&w, d->ccy);
    zind_wr_u8(&w, (uint8_t) recipient_alg);
    {
        uint8_t kh[32];
        zxv_sha3_256(recipient_pk, pk_len, kh);
        zind_wr_bytes(&w, kh, 32);
    }
    zind_wr_u64(&w, window_seconds);
    st = append(d, ZTD_REC_CREATE, &w, NULL);
    if (st != ZXV_OK) {
        /* undo the escrow move so the books match the records */
        (void) post(d, d->escrow, sender_account, amount_minor, d->n + 1, 'U', NULL);
        return st;
    }
    (void) zind_seen_add(d->ids, idh);
    d->parcels[d->n++] = p;
    *out_parcel = d->n;
    return ZXV_OK;
}

int32_t ztd_parcel_find(const ztd_desk *d, const char *parcel_id, uint32_t *out_parcel) {
    if (!d || !parcel_id || !out_parcel) return ZXV_E_ARG;
    for (uint32_t i = 0; i < d->n; i++)
        if (strcmp(d->parcels[i].id, parcel_id) == 0) {
            *out_parcel = i + 1;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

int32_t ztd_dispatch(ztd_desk *d, uint32_t parcel, const char *courier, uint64_t time) {
    parcel_t *p = get(d, parcel);
    uint8_t nt = 0, buf[128];
    zind_wr w;
    int32_t st;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!zind_ident_ok(courier, ZTD_ID_MAX)) return ZXV_E_ARG;
    if (p->state == ZTD_ST_DISPUTED) return ZIND_E_HELD;
    if (p->state != ZTD_ST_ESCROW) return ZXV_E_STATE;
    if (time > UINT64_MAX - p->window) return ZXV_E_RANGE;
    st = zxv_hex_step(p->tag, ZXV_HEX_PHASE_EMERGING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    zind_wr_str(&w, courier);
    zind_wr_u64(&w, time);
    st = append(d, ZTD_REC_DISPATCH, &w, NULL);
    if (st != ZXV_OK) return st;
    memcpy(p->courier, courier, strlen(courier) + 1);
    p->dispatch_time = time;
    p->tag = nt;
    p->state = ZTD_ST_OUT;
    return ZXV_OK;
}

int32_t ztd_handoff_message(const char *parcel_id, const char *courier, uint64_t time,
                            uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    if (!out_len || !zind_ident_ok(parcel_id, ZTD_ID_MAX) || !zind_ident_ok(courier, ZTD_ID_MAX))
        return ZXV_E_ARG;
    *out_len = 14 + 1 + strlen(parcel_id) + 1 + strlen(courier) + 8;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztd/handoff/v1", 14);
    zind_wr_str(&w, parcel_id);
    zind_wr_str(&w, courier);
    zind_wr_u64(&w, time);
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
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

static int32_t release_to_payee(ztd_desk *d, uint32_t parcel, parcel_t *p, uint64_t *entry) {
    return post(d, d->escrow, p->payee, p->amount, parcel, 'R', entry);
}

int32_t ztd_handoff_submit(ztd_desk *d, uint32_t parcel, const char *courier, uint64_t time,
                           const uint8_t *sig, size_t sig_len) {
    parcel_t *p = get(d, parcel);
    uint8_t msg[128], proof[32], nt = 0, buf[256];
    size_t len = 0;
    uint64_t entry = 0;
    zind_wr w;
    int32_t st, present = 0;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!sig || !zind_ident_ok(courier, ZTD_ID_MAX)) return ZXV_E_ARG;
    switch (p->state) {
    case ZTD_ST_OUT: break;
    case ZTD_ST_DELIVERED:
    case ZTD_ST_CLOSED:
        zind_set_error("ztd_handoff_submit: parcel %s was already handed off", p->id);
        return ZIND_E_REPLAY;
    case ZTD_ST_DISPUTED:
        zind_set_error("ztd_handoff_submit: parcel %s is disputed (BOTH) and frozen", p->id);
        return ZIND_E_HELD;
    default:
        zind_set_error("ztd_handoff_submit: parcel %s is not out for delivery", p->id);
        return ZXV_E_STATE;
    }
    if ((p->alg == ZTD_ALG_ED25519 && sig_len != ZXV_ED25519_SIG_BYTES) ||
        (p->alg == ZTD_ALG_MLDSA87 && sig_len != ZIND_SIG_BYTES))
        return ZXV_E_ARG;
    if (strcmp(courier, p->courier) != 0) {
        zind_set_error("ztd_handoff_submit: parcel %s went out with courier %s, not %s", p->id,
                       p->courier, courier);
        return ZTD_E_COURIER;
    }
    if (time < p->dispatch_time || time - p->dispatch_time > p->window) {
        zind_set_error("ztd_handoff_submit: time %llu is outside [%llu, %llu]",
                       (unsigned long long) time, (unsigned long long) p->dispatch_time,
                       (unsigned long long) (p->dispatch_time + p->window));
        return ZTD_E_WINDOW;
    }
    st = ztd_handoff_message(p->id, courier, time, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    if (p->alg == ZTD_ALG_ED25519)
        st = zxv_ed25519_verify(d->cap, p->pk, msg, len, sig);
    else
        st = zind_verify(d->cap, p->pk, msg, len, ZTD_HANDOFF_CTX, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0;
        zind_set_error("ztd_handoff_submit: the recipient's signature does not verify");
        if (st == ZXV_E_VERIFY &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) == ZXV_OK)
            quarantine(d, ftag, ZIND_REASON_TAMPER, parcel, sig, sig_len);
        return st;
    }
    {
        /* the proof's identity: message and signature together */
        uint8_t tmp[128 + ZIND_SIG_BYTES];
        memcpy(tmp, msg, len);
        memcpy(tmp + len, sig, sig_len);
        st = zind_subject("tri-deliver/proof", tmp, len + sig_len, proof);
        if (st != ZXV_OK) return st;
    }
    if (zind_seen_has(d->proofs, proof, &present) != ZXV_OK || present) return ZIND_E_REPLAY;
    st = zxv_hex_set(p->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK) st = to_manifest(nt, &nt);
    if (st != ZXV_OK) return st;
    st = release_to_payee(d, parcel, p, &entry);
    if (st != ZXV_OK) {
        zind_set_error("ztd_handoff_submit: releasing escrow failed (%s)", zxv_status_name(st));
        return st;
    }
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    zind_wr_str(&w, courier);
    zind_wr_u64(&w, time);
    zind_wr_bytes(&w, proof, 32);
    zind_wr_u64(&w, entry);
    st = append(d, ZTD_REC_DELIVER, &w, NULL);
    if (st != ZXV_OK) return st;
    (void) zind_seen_add(d->proofs, proof);
    p->tag = nt;
    p->state = ZTD_ST_DELIVERED;
    return ZXV_OK;
}

int32_t ztd_dispute(ztd_desk *d, uint32_t parcel, uint32_t reason) {
    parcel_t *p = get(d, parcel);
    uint8_t nt = 0, buf[64], h[32];
    zind_wr w;
    int32_t st;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (p->state == ZTD_ST_DISPUTED) return ZIND_E_HELD;
    if (p->state != ZTD_ST_ESCROW && p->state != ZTD_ST_OUT) {
        zind_set_error("ztd_dispute: parcel %s holds no escrow any more; a dispute about a "
                       "settled parcel is handled outside the kit",
                       p->id);
        return ZXV_E_STATE;
    }
    st = zxv_hex_set(p->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    zind_wr_u32(&w, reason);
    st = append(d, ZTD_REC_DISPUTE, &w, h);
    if (st != ZXV_OK) return st;
    memcpy(p->dispute_hash, h, 32);
    p->tag = nt;
    p->state = ZTD_ST_DISPUTED;
    quarantine(d, nt, ZIND_REASON_HELD_BOTH, parcel, (const uint8_t *) p->id, strlen(p->id));
    return ZXV_OK;
}

static int32_t arb_msg(const parcel_t *p, int32_t outcome, uint8_t *buf, size_t cap,
                       size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztd/arbiter/v1", 14);
    zind_wr_str(&w, p->id);
    zind_wr_bytes(&w, p->dispute_hash, 32);
    zind_wr_u8(&w, (uint8_t) outcome);
    *out_len = 14 + 1 + strlen(p->id) + 32 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t ztd_arbiter_message(const ztd_desk *d, uint32_t parcel, int32_t outcome, uint8_t *buf,
                            size_t cap, size_t *out_len) {
    const parcel_t *p = get(d, parcel);
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out_len || (outcome != ZTD_PAY_PAYEE && outcome != ZTD_REFUND_SENDER)) return ZXV_E_ARG;
    if (p->state != ZTD_ST_DISPUTED) return ZXV_E_STATE;
    return arb_msg(p, outcome, buf, cap, out_len);
}

int32_t ztd_arbiter_resolve(ztd_desk *d, uint32_t parcel, const uint8_t arbiter_pk[ZIND_PK_BYTES],
                            int32_t outcome, const uint8_t sig[ZIND_SIG_BYTES]) {
    parcel_t *p = get(d, parcel);
    uint8_t id[32], msg[128], nt = 0, buf[160], sh[32];
    size_t len = 0;
    uint64_t entry = 0;
    uint32_t i;
    zind_wr w;
    int32_t st;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!arbiter_pk || !sig || (outcome != ZTD_PAY_PAYEE && outcome != ZTD_REFUND_SENDER))
        return ZXV_E_ARG;
    st = zxv_cap_check(d->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("ztd_arbiter_resolve: the desk cap needs ATTEST to apply a decision");
        return st;
    }
    if (p->state != ZTD_ST_DISPUTED) {
        zind_set_error("ztd_arbiter_resolve: parcel %s is not disputed", p->id);
        return (p->state == ZTD_ST_DELIVERED || p->state == ZTD_ST_REFUNDED ||
                p->state == ZTD_ST_CLOSED)
                   ? ZIND_E_REPLAY
                   : ZXV_E_STATE;
    }
    st = zind_key_id(arbiter_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < d->n_arbiters; i++)
        if (memcmp(d->arbiters[i], id, 32) == 0) break;
    if (i == d->n_arbiters) {
        zind_set_error("ztd_arbiter_resolve: the key is not a registered arbiter");
        return ZTD_E_ARBITER;
    }
    st = arb_msg(p, outcome, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(d->cap, arbiter_pk, msg, len, ZTD_ARBITER_CTX, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0;
        zind_set_error("ztd_arbiter_resolve: the arbiter's signature does not verify");
        if (st == ZXV_E_VERIFY &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) == ZXV_OK)
            quarantine(d, ftag, ZIND_REASON_TAMPER, parcel, sig, ZIND_SIG_BYTES);
        return st;
    }
    if (outcome == ZTD_PAY_PAYEE) {
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st == ZXV_OK) st = to_manifest(nt, &nt);
        if (st == ZXV_OK) st = release_to_payee(d, parcel, p, &entry);
    } else {
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st == ZXV_OK) st = post(d, d->escrow, p->sender, p->amount, parcel, 'F', &entry);
    }
    if (st != ZXV_OK) return st;
    st = zxv_sha3_256(sig, ZIND_SIG_BYTES, sh);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    zind_wr_bytes(&w, id, 32);
    zind_wr_u8(&w, (uint8_t) outcome);
    zind_wr_bytes(&w, sh, 32);
    zind_wr_u64(&w, entry);
    st = append(d, ZTD_REC_ARBITER, &w, NULL);
    if (st != ZXV_OK) return st;
    p->tag = nt;
    p->state = outcome == ZTD_PAY_PAYEE ? ZTD_ST_DELIVERED : ZTD_ST_REFUNDED;
    return ZXV_OK;
}

int32_t ztd_cancel(ztd_desk *d, uint32_t parcel) {
    parcel_t *p = get(d, parcel);
    uint8_t nt = 0, buf[64];
    uint64_t entry = 0;
    zind_wr w;
    int32_t st;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (p->state == ZTD_ST_DISPUTED) return ZIND_E_HELD;
    if (p->state != ZTD_ST_ESCROW) {
        zind_set_error("ztd_cancel: only a parcel not yet dispatched can be cancelled");
        return ZXV_E_STATE;
    }
    st = zxv_hex_set(p->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK) st = post(d, d->escrow, p->sender, p->amount, parcel, 'C', &entry);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    zind_wr_u64(&w, entry);
    st = append(d, ZTD_REC_CANCEL, &w, NULL);
    if (st != ZXV_OK) return st;
    p->tag = nt;
    p->state = ZTD_ST_CANCELLED;
    return ZXV_OK;
}

int32_t ztd_close(ztd_desk *d, uint32_t parcel) {
    parcel_t *p = get(d, parcel);
    uint8_t nt = 0, buf[64];
    zind_wr w;
    int32_t st;
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (p->state == ZTD_ST_DISPUTED) return ZIND_E_HELD;
    if (p->state != ZTD_ST_DELIVERED) return ZXV_E_STATE;
    st = zxv_hex_step(p->tag, ZXV_HEX_PHASE_DISSOLVING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, p->id);
    st = append(d, ZTD_REC_CLOSE, &w, NULL);
    if (st != ZXV_OK) return st;
    p->tag = nt;
    p->state = ZTD_ST_CLOSED;
    return ZXV_OK;
}

int32_t ztd_parcel_state(const ztd_desk *d, uint32_t parcel, int32_t *out_state,
                         uint8_t *out_tag) {
    const parcel_t *p = get(d, parcel);
    if (!p) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = p->state;
    if (out_tag) *out_tag = p->tag;
    return ZXV_OK;
}

int32_t ztd_escrow_owed(const ztd_desk *d, int64_t *out_minor) {
    int64_t sum = 0;
    if (!d || !out_minor) return ZXV_E_ARG;
    for (uint32_t i = 0; i < d->n; i++) {
        int32_t s = d->parcels[i].state;
        if (s == ZTD_ST_ESCROW || s == ZTD_ST_OUT || s == ZTD_ST_DISPUTED)
            sum += d->parcels[i].amount; /* each <= ZXV_AMOUNT_MAX and the ledger held the total */
    }
    *out_minor = sum;
    return ZXV_OK;
}
