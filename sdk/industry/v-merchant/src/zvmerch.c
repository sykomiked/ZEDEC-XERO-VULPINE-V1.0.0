/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvmerch.c — v-merchant: invoices, offline-verified payment proofs, batch
 * settlement with duplicate refusal through the ledger reference, and exact
 * end-of-day reports. Fees come from zxv_fee_*. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zvmerch.h"

#define DOMAIN "v-merchant"

typedef struct {
    char code[4];
    uint8_t digits;
    uint32_t merchant, fee[ZXV_FEE_BUCKETS];
    uint64_t carry;
    zvm_totals day;
} ccy_t;

typedef struct {
    uint32_t account;
    int32_t alg;
    uint8_t pk[ZIND_PK_BYTES];
    char last4[5];
} payer_t;

typedef struct {
    char id[ZVM_ID_MAX + 1];
    uint32_t ccy;
    int64_t amount;
    uint64_t issued, expires;
    int32_t state;
    uint8_t tag;
} invoice_t;

typedef struct {
    uint32_t invoice, ccy, payer;
    int64_t amount;
    uint8_t identity[32];
    int32_t state, status;
} proof_t;

struct zvm_terminal {
    const zxv_cap *cap;
    char merchant[ZVM_ID_MAX + 1];
    const zind_signer *signer;
    zind_qsink *qs;
    ccy_t ccy[ZVM_MAX_CCY];
    uint32_t n_ccy;
    payer_t *payers;
    uint32_t n_payers;
    invoice_t *inv;
    uint32_t n_inv, inv_alloc;
    proof_t *prf;
    uint32_t n_prf, prf_alloc, next_queued;
    zind_seen *ids, *nonces;
    zind_chain *chain;
    uint32_t last_day;
    int closed_once;
};

const char *zvm_status_name(int32_t status) {
    switch (status) {
    case ZVM_E_EXPIRED: return "ZVM_E_EXPIRED";
    case ZVM_E_MISMATCH: return "ZVM_E_MISMATCH";
    case ZVM_E_PAYER: return "ZVM_E_PAYER";
    case ZVM_E_CARD: return "ZVM_E_CARD";
    case ZVM_E_PENDING: return "ZVM_E_PENDING";
    default: return zind_status_name(status);
    }
}

/* ---- the proof's bytes ---- */

static int ccy_ok(const char *c) {
    if (!c) return 0;
    for (int i = 0; i < 3; i++)
        if (c[i] < 'A' || c[i] > 'Z') return 0;
    return c[3] == '\0';
}

static int proof_fields_ok(const zvm_proof *p) {
    return p && zind_ident_ok(p->merchant, ZVM_ID_MAX) && zind_ident_ok(p->invoice, ZVM_ID_MAX) &&
           ccy_ok(p->ccy) && p->amount > 0 && p->amount <= ZXV_AMOUNT_MAX;
}

static int sig_len_ok(int32_t alg, size_t len) {
    return (alg == ZVM_ALG_ED25519 && len == ZXV_ED25519_SIG_BYTES) ||
           (alg == ZVM_ALG_MLDSA87 && len == ZIND_SIG_BYTES);
}

static void signed_fields(zind_wr *w, const zvm_proof *p) {
    zind_wr_str(w, p->merchant);
    zind_wr_str(w, p->invoice);
    zind_wr_str(w, p->ccy);
    zind_wr_u64(w, (uint64_t) p->amount);
    zind_wr_u32(w, p->payer);
    zind_wr_u64(w, p->nonce);
    zind_wr_u64(w, p->time);
}

int32_t zvm_pay_bytes(const zvm_proof *p, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    if (!out_len || !proof_fields_ok(p)) return ZXV_E_ARG;
    *out_len = 10 + 3 + strlen(p->merchant) + strlen(p->invoice) + 3 + 8 + 4 + 8 + 8;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zvm/pay/v1", 10);
    signed_fields(&w, p);
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t zvm_proof_encode(const zvm_proof *p, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    if (!out_len || !proof_fields_ok(p) || !sig_len_ok(p->alg, p->sig_len)) return ZXV_E_ARG;
    *out_len = 4 + 1 + 3 + strlen(p->merchant) + strlen(p->invoice) + 3 + 8 + 4 + 8 + 8 + 1 + 2 +
               p->sig_len;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ZVMP", 4);
    zind_wr_u8(&w, 1);
    signed_fields(&w, p);
    zind_wr_u8(&w, (uint8_t) p->alg);
    zind_wr_u16(&w, (uint16_t) p->sig_len);
    zind_wr_bytes(&w, p->sig, p->sig_len);
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t zvm_proof_decode(const uint8_t *buf, size_t len, zvm_proof *out) {
    zind_rd r;
    zvm_proof p;
    uint8_t magic[5];
    uint16_t sl;
    if (!buf || !out) return ZXV_E_ARG;
    if (len < 5 || len > ZVM_PROOF_MAX) return ZXV_E_FORMAT;
    memset(&p, 0, sizeof p);
    zind_rd_init(&r, buf, len);
    for (int i = 0; i < 5; i++) magic[i] = zind_rd_u8(&r);
    if (memcmp(magic, "ZVMP\x01", 5) != 0) return ZXV_E_FORMAT;
    {
        char s[256];
        zind_rd_str(&r, s, sizeof s);
        if (r.bad || strlen(s) > ZVM_ID_MAX) return ZXV_E_FORMAT;
        memcpy(p.merchant, s, strlen(s) + 1);
        zind_rd_str(&r, s, sizeof s);
        if (r.bad || strlen(s) > ZVM_ID_MAX) return ZXV_E_FORMAT;
        memcpy(p.invoice, s, strlen(s) + 1);
        zind_rd_str(&r, s, sizeof s);
        if (r.bad || strlen(s) != 3) return ZXV_E_FORMAT;
        memcpy(p.ccy, s, 4);
    }
    p.amount = (int64_t) zind_rd_u64(&r);
    p.payer = zind_rd_u32(&r);
    p.nonce = zind_rd_u64(&r);
    p.time = zind_rd_u64(&r);
    p.alg = zind_rd_u8(&r);
    sl = zind_rd_u16(&r);
    if (r.bad || !sig_len_ok(p.alg, sl) || r.len - r.pos != sl) return ZXV_E_FORMAT;
    memcpy(p.sig, r.buf + r.pos, sl);
    p.sig_len = sl;
    if (!proof_fields_ok(&p)) return ZXV_E_FORMAT;
    *out = p;
    return ZXV_OK;
}

int32_t zvm_card_check(int32_t network, const char *pan) {
    int32_t valid = 0, st;
    if (!pan) return ZXV_E_ARG;
    st = zxv_card_check_digit(network, pan, &valid);
    if (st != ZXV_OK) return st;
    if (!valid) {
        zind_set_error("zvm_card_check: the number fails its network's check digit");
        return ZVM_E_CARD;
    }
    return ZXV_OK;
}

/* ---- terminal ---- */

static int32_t append(zvm_terminal *t, uint16_t kind, const zind_wr *w, uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(t->cap, t->chain, t->signer, kind, w->buf, w->len, NULL, hash);
}

static void quarantine(zvm_terminal *t, uint8_t tag, uint8_t reason, uint32_t who,
                       const uint8_t *data, size_t len) {
    uint8_t subj[32];
    if (!t->qs || zind_subject("v-merchant/event", data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(t->qs, tag, reason, who, subj, NULL);
}

static void quarantine_false(zvm_terminal *t, uint8_t reason, uint32_t who, const uint8_t *data,
                             size_t len) {
    uint8_t ftag = 0;
    if (zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) == ZXV_OK)
        quarantine(t, ftag, reason, who, data, len);
}

int32_t zvm_terminal_create(const zxv_cap *cap, const char *merchant_id,
                            const zind_signer *signer, zxv_hexlog *qlog, zvm_terminal **out) {
    zvm_terminal *t;
    int32_t st;
    if (!cap || !signer || !out) return ZXV_E_ARG;
    *out = NULL;
    if (!zind_ident_ok(merchant_id, ZVM_ID_MAX)) {
        zind_set_error("zvm_terminal_create: a merchant id is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zvm_terminal_create: the cap needs LEDGER_POST, LEDGER_READ and "
                       "CRYPTO_SIGN");
        return st;
    }
    t = (zvm_terminal *) calloc(1, sizeof *t);
    if (!t) return ZXV_E_NOMEM;
    t->cap = cap;
    memcpy(t->merchant, merchant_id, strlen(merchant_id) + 1);
    t->signer = signer;
    st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &t->chain);
    if (st == ZXV_OK) st = zind_seen_create(ZVM_MAX_INVOICES, &t->ids);
    if (st == ZXV_OK) st = zind_seen_create(ZVM_MAX_INVOICES, &t->nonces);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &t->qs);
    if (st != ZXV_OK) {
        zvm_terminal_destroy(t);
        return st;
    }
    *out = t;
    return ZXV_OK;
}

void zvm_terminal_destroy(zvm_terminal *t) {
    if (!t) return;
    zind_chain_destroy(t->chain);
    zind_seen_destroy(t->ids);
    zind_seen_destroy(t->nonces);
    zind_qsink_destroy(t->qs);
    free(t->payers);
    free(t->inv);
    free(t->prf);
    free(t);
}

const zind_chain *zvm_records(const zvm_terminal *t) { return t ? t->chain : NULL; }

static int find_ccy(const zvm_terminal *t, const char *code, uint32_t *out) {
    if (!code) return 0;
    for (uint32_t i = 0; i < t->n_ccy; i++)
        if (strcmp(t->ccy[i].code, code) == 0) {
            *out = i;
            return 1;
        }
    return 0;
}

int32_t zvm_ccy_add(zvm_terminal *t, const char *ccy, uint32_t merchant_account) {
    ccy_t c;
    uint8_t digits = 0, buf[64];
    uint32_t idx = 0;
    zind_wr w;
    int32_t st;
    if (!t || !ccy) return ZXV_E_ARG;
    if (!ccy_ok(ccy) || zxv_iso4217_by_alpha(ccy, NULL, &digits, NULL) != ZXV_OK ||
        digits == ZXV_MINOR_NA) {
        zind_set_error("zvm_ccy_add: \"%.8s\" is not an ISO 4217 currency with minor units", ccy);
        return ZXV_E_CURRENCY;
    }
    if (find_ccy(t, ccy, &idx)) return ZXV_E_DUPLICATE;
    if (t->n_ccy >= ZVM_MAX_CCY) return ZXV_E_CAPACITY;
    {
        char ac[16];
        size_t al = 0;
        st = zxv_ledger_account_ccy_cap(t->cap, merchant_account, ac, sizeof ac, &al);
        if (st != ZXV_OK) return st;
        if (strcmp(ac, ccy) != 0) return ZXV_E_CURRENCY;
    }
    memset(&c, 0, sizeof c);
    memcpy(c.code, ccy, 4);
    c.digits = digits;
    c.merchant = merchant_account;
    for (int i = 0; i < ZXV_FEE_BUCKETS; i++) {
        static const char *const nm[ZXV_FEE_BUCKETS] = {"reserve-floor", "vbill-pool", "infra",
                                                        "regen"};
        char name[64];
        snprintf(name, sizeof name, "fee:%s:%s:%s", nm[i], t->merchant, ccy);
        st = zxv_ledger_open_account_cap(t->cap, name, ccy, ZXV_ACCT_HOLDER, &c.fee[i]);
        if (st != ZXV_OK) return st;
    }
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, ccy);
    zind_wr_u32(&w, merchant_account);
    for (int i = 0; i < ZXV_FEE_BUCKETS; i++) zind_wr_u32(&w, c.fee[i]);
    st = append(t, ZVM_REC_CCY, &w, NULL);
    if (st != ZXV_OK) return st;
    t->ccy[t->n_ccy++] = c;
    return ZXV_OK;
}

int32_t zvm_fee_accounts(const zvm_terminal *t, const char *ccy, uint32_t out[ZXV_FEE_BUCKETS]) {
    uint32_t i = 0;
    if (!t || !out) return ZXV_E_ARG;
    if (!find_ccy(t, ccy, &i)) return ZXV_E_NOT_FOUND;
    memcpy(out, t->ccy[i].fee, sizeof t->ccy[i].fee);
    return ZXV_OK;
}

static payer_t *find_payer(const zvm_terminal *t, uint32_t account) {
    for (uint32_t i = 0; i < t->n_payers; i++)
        if (t->payers[i].account == account) return &t->payers[i];
    return NULL;
}

int32_t zvm_payer_add(zvm_terminal *t, uint32_t payer_account, int32_t alg, const uint8_t *pk,
                      size_t pk_len, int32_t card_network, const char *pan) {
    payer_t p;
    uint8_t buf[64], kh[32];
    zind_wr w;
    int32_t st;
    if (!t || !pk) return ZXV_E_ARG;
    if (!((alg == ZVM_ALG_ED25519 && pk_len == ZXV_ED25519_PK_BYTES) ||
          (alg == ZVM_ALG_MLDSA87 && pk_len == ZIND_PK_BYTES)))
        return ZXV_E_ARG;
    if (find_payer(t, payer_account)) return ZXV_E_DUPLICATE;
    if (t->n_payers >= ZVM_MAX_PAYERS) return ZXV_E_CAPACITY;
    memset(&p, 0, sizeof p);
    if (card_network) {
        size_t n;
        st = zvm_card_check(card_network, pan);
        if (st != ZXV_OK) return st;
        n = strlen(pan);
        if (n < 4) return ZVM_E_CARD;
        memcpy(p.last4, pan + n - 4, 5);
    }
    {
        /* the account must exist; any currency */
        int32_t kind = 0;
        st = zxv_ledger_account_kind_cap(t->cap, payer_account, &kind);
        if (st != ZXV_OK) return st;
    }
    if (!t->payers) {
        t->payers = (payer_t *) calloc(ZVM_MAX_PAYERS, sizeof *t->payers);
        if (!t->payers) return ZXV_E_NOMEM;
    }
    p.account = payer_account;
    p.alg = alg;
    memcpy(p.pk, pk, pk_len);
    (void) zxv_sha3_256(pk, pk_len, kh);
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_u32(&w, payer_account);
    zind_wr_u8(&w, (uint8_t) alg);
    zind_wr_bytes(&w, kh, 32);
    zind_wr_str(&w, p.last4);
    st = append(t, ZVM_REC_PAYER, &w, NULL);
    if (st != ZXV_OK) return st;
    t->payers[t->n_payers++] = p;
    return ZXV_OK;
}

static invoice_t *get_inv(const zvm_terminal *t, uint32_t i) {
    if (!t || i == 0 || i > t->n_inv) {
        zind_set_error("zvm: unknown invoice %u", i);
        return NULL;
    }
    return &t->inv[i - 1];
}

int32_t zvm_invoice_create(zvm_terminal *t, const char *invoice_id, const char *ccy,
                           int64_t amount_minor, uint64_t issued_at, uint64_t expires_at,
                           uint32_t *out_invoice) {
    invoice_t v;
    uint8_t idh[32], buf[96];
    uint32_t c = 0;
    int32_t present = 0, st;
    zind_wr w;
    if (!t || !out_invoice) return ZXV_E_ARG;
    if (!zind_ident_ok(invoice_id, ZVM_ID_MAX)) {
        zind_set_error("zvm_invoice_create: an invoice id is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (!find_ccy(t, ccy, &c)) return ZXV_E_CURRENCY;
    if (amount_minor <= 0 || amount_minor > ZXV_AMOUNT_MAX) return ZXV_E_RANGE;
    if (expires_at <= issued_at) return ZXV_E_ARG;
    st = zind_subject("v-merchant/invoice", (const uint8_t *) invoice_id, strlen(invoice_id), idh);
    if (st != ZXV_OK) return st;
    if (zind_seen_has(t->ids, idh, &present) != ZXV_OK || present) return ZXV_E_DUPLICATE;
    if (t->n_inv >= ZVM_MAX_INVOICES) return ZXV_E_CAPACITY;
    if (t->n_inv == t->inv_alloc) {
        uint32_t a = t->inv_alloc ? t->inv_alloc * 2 : 32;
        invoice_t *ni = (invoice_t *) realloc(t->inv, (size_t) a * sizeof *ni);
        if (!ni) return ZXV_E_NOMEM;
        t->inv = ni;
        t->inv_alloc = a;
    }
    memset(&v, 0, sizeof v);
    memcpy(v.id, invoice_id, strlen(invoice_id) + 1);
    v.ccy = c;
    v.amount = amount_minor;
    v.issued = issued_at;
    v.expires = expires_at;
    v.state = ZVM_INV_OPEN;
    st = zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &v.tag);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, invoice_id);
    zind_wr_str(&w, t->ccy[c].code);
    zind_wr_u64(&w, (uint64_t) amount_minor);
    zind_wr_u64(&w, issued_at);
    zind_wr_u64(&w, expires_at);
    st = append(t, ZVM_REC_INVOICE, &w, NULL);
    if (st != ZXV_OK) return st;
    (void) zind_seen_add(t->ids, idh);
    t->inv[t->n_inv++] = v;
    *out_invoice = t->n_inv;
    return ZXV_OK;
}

int32_t zvm_invoice_void(zvm_terminal *t, uint32_t invoice) {
    invoice_t *v = get_inv(t, invoice);
    uint8_t nt = 0, buf[64];
    zind_wr w;
    int32_t st;
    if (!v) return t ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (v->state != ZVM_INV_OPEN) {
        zind_set_error("zvm_invoice_void: only an open invoice can be voided");
        return ZXV_E_STATE;
    }
    st = zxv_hex_set(v->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, v->id);
    st = append(t, ZVM_REC_VOID, &w, NULL);
    if (st != ZXV_OK) return st;
    v->tag = nt;
    v->state = ZVM_INV_VOID;
    return ZXV_OK;
}

int32_t zvm_invoice_state(const zvm_terminal *t, uint32_t invoice, int32_t *out_state,
                          uint8_t *out_tag) {
    const invoice_t *v = get_inv(t, invoice);
    if (!v) return t ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = v->state;
    if (out_tag) *out_tag = v->tag;
    return ZXV_OK;
}

int32_t zvm_invoice_find(const zvm_terminal *t, const char *invoice_id, uint32_t *out_invoice) {
    if (!t || !invoice_id || !out_invoice) return ZXV_E_ARG;
    for (uint32_t i = 0; i < t->n_inv; i++)
        if (strcmp(t->inv[i].id, invoice_id) == 0) {
            *out_invoice = i + 1;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

static int32_t identity(const zvm_proof *p, uint8_t out[32]) {
    uint8_t b[12];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (p->payer >> (8 * i));
    for (int i = 0; i < 8; i++) b[4 + i] = (uint8_t) (p->nonce >> (8 * i));
    return zind_subject("v-merchant/nonce", b, sizeof b, out);
}

int32_t zvm_accept(zvm_terminal *t, const zvm_proof *p, uint32_t *out_proof) {
    invoice_t *v;
    payer_t *py;
    uint32_t inv = 0, c = 0;
    uint8_t msg[160], id[32], nt = 0, buf[128];
    size_t len = 0;
    int32_t st, present = 0;
    proof_t pr;
    zind_wr w;
    if (!t || !p) return ZXV_E_ARG;
    if (!proof_fields_ok(p)) {
        zind_set_error("zvm_accept: malformed proof fields");
        return ZXV_E_ARG;
    }
    if (strcmp(p->merchant, t->merchant) != 0) {
        zind_set_error("zvm_accept: the proof pays merchant %s, not %s", p->merchant,
                       t->merchant);
        return ZVM_E_MISMATCH;
    }
    st = zvm_invoice_find(t, p->invoice, &inv);
    if (st != ZXV_OK) return st;
    v = &t->inv[inv - 1];
    if (v->state == ZVM_INV_ACCEPTED || v->state == ZVM_INV_SETTLED) {
        zind_set_error("zvm_accept: invoice %s is already paid", v->id);
        return ZIND_E_REPLAY;
    }
    if (v->state != ZVM_INV_OPEN) return ZXV_E_STATE;
    if (!find_ccy(t, p->ccy, &c) || c != v->ccy || p->amount != v->amount) {
        zind_set_error("zvm_accept: the proof pays %lld %s; invoice %s is for %lld %s",
                       (long long) p->amount, p->ccy, v->id, (long long) v->amount,
                       t->ccy[v->ccy].code);
        return ZVM_E_MISMATCH;
    }
    if (p->time < v->issued || p->time > v->expires) {
        zind_set_error("zvm_accept: dated %llu, outside [%llu, %llu]",
                       (unsigned long long) p->time, (unsigned long long) v->issued,
                       (unsigned long long) v->expires);
        return ZVM_E_EXPIRED;
    }
    py = find_payer(t, p->payer);
    if (!py || py->alg != p->alg) {
        zind_set_error("zvm_accept: payer %u with that key type is not on this terminal's list",
                       p->payer);
        return ZVM_E_PAYER;
    }
    if (!sig_len_ok(p->alg, p->sig_len)) return ZXV_E_ARG;
    st = zvm_pay_bytes(p, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    if (p->alg == ZVM_ALG_ED25519)
        st = zxv_ed25519_verify(t->cap, py->pk, msg, len, p->sig);
    else
        st = zind_verify(t->cap, py->pk, msg, len, ZVM_PAY_CTX, p->sig);
    if (st != ZXV_OK) {
        zind_set_error("zvm_accept: the payer's signature does not verify");
        if (st == ZXV_E_VERIFY) quarantine_false(t, ZIND_REASON_TAMPER, p->payer, msg, len);
        return st;
    }
    st = identity(p, id);
    if (st != ZXV_OK) return st;
    if (zind_seen_has(t->nonces, id, &present) != ZXV_OK || present) {
        zind_set_error("zvm_accept: payer %u already used nonce %llu", p->payer,
                       (unsigned long long) p->nonce);
        quarantine_false(t, ZIND_REASON_REPLAY, p->payer, id, 32);
        return ZIND_E_REPLAY;
    }
    if (t->n_prf >= ZVM_MAX_INVOICES) return ZXV_E_CAPACITY;
    if (t->n_prf == t->prf_alloc) {
        uint32_t a = t->prf_alloc ? t->prf_alloc * 2 : 32;
        proof_t *np = (proof_t *) realloc(t->prf, (size_t) a * sizeof *np);
        if (!np) return ZXV_E_NOMEM;
        t->prf = np;
        t->prf_alloc = a;
    }
    st = zxv_hex_step(v->tag, ZXV_HEX_PHASE_EMERGING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, v->id);
    zind_wr_u32(&w, p->payer);
    zind_wr_u64(&w, p->nonce);
    zind_wr_u64(&w, p->time);
    zind_wr_bytes(&w, id, 32);
    st = append(t, ZVM_REC_ACCEPT, &w, NULL);
    if (st != ZXV_OK) return st;
    (void) zind_seen_add(t->nonces, id);
    memset(&pr, 0, sizeof pr);
    pr.invoice = inv;
    pr.ccy = c;
    pr.payer = p->payer;
    pr.amount = p->amount;
    memcpy(pr.identity, id, 32);
    pr.state = ZVM_PRF_QUEUED;
    t->prf[t->n_prf++] = pr;
    v->tag = nt;
    v->state = ZVM_INV_ACCEPTED;
    if (out_proof) *out_proof = t->n_prf;
    return ZXV_OK;
}

int32_t zvm_proof_state(const zvm_terminal *t, uint32_t proof, int32_t *out_state,
                        int32_t *out_status) {
    if (!t || proof == 0 || proof > t->n_prf) return t ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = t->prf[proof - 1].state;
    if (out_status) *out_status = t->prf[proof - 1].status;
    return ZXV_OK;
}

static void hex16(const uint8_t *id, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[2 * i] = hx[id[i] >> 4];
        out[2 * i + 1] = hx[id[i] & 15];
    }
    out[32] = 0;
}

/* Fresh Potential tag: a reopened invoice starts its lifecycle again. */
static uint8_t fresh_tag(void) {
    uint8_t t = 0;
    (void) zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &t);
    return t;
}

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

static int add_ok(int64_t *acc, int64_t v) {
    if (v < 0 || *acc > ZXV_AMOUNT_MAX - v) return 0;
    *acc += v;
    return 1;
}

/* Was `ref` already posted to `account`? The ledger checks funds before it
 * checks a reference, so a duplicate whose payer is now short reads as
 * ZXV_E_FUNDS; the journal tells them apart. ABI 1.5 has no lookup by
 * reference, so this scans the account's journal, newest first (only on a
 * refused posting). */
static int ref_used(const zvm_terminal *t, uint32_t account, const char *ref) {
    uint32_t n = 0;
    if (zxv_ledger_entry_count_cap(t->cap, account, &n) != ZXV_OK) return 0;
    while (n-- > 0) {
        char r[40];
        size_t rl = 0;
        if (zxv_ledger_entry_at_cap(t->cap, account, n, NULL, NULL, NULL, r, sizeof r, &rl) ==
                ZXV_OK &&
            strcmp(r, ref) == 0)
            return 1;
    }
    return 0;
}

static int32_t settle_one(zvm_terminal *t, uint32_t k, zvm_batch *b) {
    proof_t *pr = &t->prf[k];
    invoice_t *v = &t->inv[pr->invoice - 1];
    ccy_t *c = &t->ccy[pr->ccy];
    char ref[36], hx[33];
    uint8_t buf[96], nt = 0;
    uint64_t entry = 0, fee = 0, rem = 0, parts[ZXV_FEE_BUCKETS];
    zind_wr w;
    int32_t st;
    hex16(pr->identity, hx);
    snprintf(ref, sizeof ref, "VM%s", hx);
    st = zxv_ledger_post_cap(t->cap, pr->payer, c->merchant, pr->amount, c->code, ref, &entry);
    if (st != ZXV_OK && st != ZXV_E_DUPLICATE && st != ZXV_E_POISONED && st != ZXV_E_INVARIANT &&
        ref_used(t, c->merchant, ref))
        st = ZXV_E_DUPLICATE;
    pr->status = st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, v->id);
    zind_wr_bytes(&w, pr->identity, 32);
    if (st == ZXV_E_DUPLICATE) {
        /* the same proof was posted before: by another till on this ledger */
        st = zxv_hex_set(v->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st == ZXV_OK) st = append(t, ZVM_REC_DUP, &w, NULL);
        if (st != ZXV_OK) return st;
        quarantine_false(t, ZIND_REASON_REPLAY, pr->payer, pr->identity, 32);
        pr->state = ZVM_PRF_DUPLICATE;
        v->tag = nt;
        v->state = ZVM_INV_VOID;
        c->day.duplicates++;
        b->duplicates++;
        return ZXV_OK;
    }
    if (st == ZXV_E_POISONED || st == ZXV_E_INVARIANT || st == ZXV_E_INTERNAL ||
        st == ZXV_E_NOMEM || st == ZXV_E_CAPACITY)
        return st; /* a ledger fault: stop the batch, the proof stays queued */
    if (st != ZXV_OK) {
        /* the payer could not pay (funds, currency, unknown account): reopen */
        zind_wr_u32(&w, (uint32_t) -st);
        st = append(t, ZVM_REC_BOUNCE, &w, NULL);
        if (st != ZXV_OK) return st;
        pr->state = ZVM_PRF_BOUNCED;
        v->tag = fresh_tag();
        v->state = ZVM_INV_OPEN;
        c->day.bounced++;
        b->bounced++;
        return ZXV_OK;
    }
    st = zxv_fee_assure_carry(c->carry, (uint64_t) pr->amount, &fee, &rem);
    if (st == ZXV_OK) st = zxv_fee_split(fee, parts);
    for (int i = 0; st == ZXV_OK && i < ZXV_FEE_BUCKETS; i++)
        if (parts[i]) {
            snprintf(ref, sizeof ref, "VF%s%d", hx, i);
            st = zxv_ledger_post_cap(t->cap, c->merchant, c->fee[i], (int64_t) parts[i], c->code,
                                     ref, NULL);
        }
    if (st != ZXV_OK) return st;
    c->carry = rem;
    if (!add_ok(&c->day.gross, pr->amount) || !add_ok(&c->day.fee, (int64_t) fee) ||
        !add_ok(&c->day.net, pr->amount - (int64_t) fee))
        return ZXV_E_RANGE;
    for (int i = 0; i < ZXV_FEE_BUCKETS; i++)
        if (!add_ok(&c->day.fee_part[i], (int64_t) parts[i])) return ZXV_E_RANGE;
    c->day.settled++;
    st = zxv_hex_set(v->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK) st = to_manifest(nt, &nt);
    zind_wr_u64(&w, entry);
    zind_wr_u64(&w, fee);
    if (st == ZXV_OK) st = append(t, ZVM_REC_SETTLE, &w, NULL);
    if (st != ZXV_OK) return st;
    pr->state = ZVM_PRF_SETTLED;
    v->tag = nt;
    v->state = ZVM_INV_SETTLED;
    b->settled++;
    return ZXV_OK;
}

int32_t zvm_settle(zvm_terminal *t, zvm_batch *out) {
    zvm_batch b = {0, 0, 0};
    int32_t st = ZXV_OK;
    if (!t) return ZXV_E_ARG;
    while (t->next_queued < t->n_prf) {
        if (t->prf[t->next_queued].state == ZVM_PRF_QUEUED) {
            st = settle_one(t, t->next_queued, &b);
            if (st != ZXV_OK) break;
        }
        t->next_queued++;
    }
    if (out) *out = b;
    return st;
}

int32_t zvm_day_totals(const zvm_terminal *t, const char *ccy, zvm_totals *out) {
    uint32_t c = 0;
    if (!t || !out) return ZXV_E_ARG;
    if (!find_ccy(t, ccy, &c)) return ZXV_E_NOT_FOUND;
    *out = t->ccy[c].day;
    return ZXV_OK;
}

/* Append to the report, tracking the length a full report needs. */
typedef struct {
    char *buf;
    size_t cap, len;
} sb_t;

static void sb_puts(sb_t *s, const char *x) {
    size_t n = strlen(x);
    if (s->buf && s->len + n < s->cap) memcpy(s->buf + s->len, x, n);
    s->len += n;
}

static int32_t sb_amount(sb_t *s, int64_t minor, uint8_t digits) {
    zind_qty q;
    char tmp[64];
    size_t n = 0;
    int32_t st = zind_qty_from_minor(minor, digits, &q);
    if (st == ZXV_OK) st = zind_qty_format(q, tmp, sizeof tmp, &n);
    if (st != ZXV_OK) return st;
    sb_puts(s, tmp);
    return ZXV_OK;
}

int32_t zvm_eod_report(const zvm_terminal *t, uint32_t day, char *buf, size_t cap,
                       size_t *out_len) {
    sb_t s;
    char line[160];
    int32_t st = ZXV_OK;
    if (!t || !out_len) return ZXV_E_ARG;
    s.buf = buf;
    s.cap = cap;
    s.len = 0;
    snprintf(line, sizeof line, "EOD %s day %u\n", t->merchant, day);
    sb_puts(&s, line);
    for (uint32_t i = 0; st == ZXV_OK && i < t->n_ccy; i++) {
        const ccy_t *c = &t->ccy[i];
        const zvm_totals *d = &c->day;
        snprintf(line, sizeof line, "%s settled %u gross ", c->code, d->settled);
        sb_puts(&s, line);
        st = sb_amount(&s, d->gross, c->digits);
        sb_puts(&s, " fee ");
        if (st == ZXV_OK) st = sb_amount(&s, d->fee, c->digits);
        sb_puts(&s, " net ");
        if (st == ZXV_OK) st = sb_amount(&s, d->net, c->digits);
        snprintf(line, sizeof line, " bounced %u duplicates %u\n%s fee reserve-floor ",
                 d->bounced, d->duplicates, c->code);
        sb_puts(&s, line);
        if (st == ZXV_OK) st = sb_amount(&s, d->fee_part[ZXV_FEE_RESERVE_FLOOR], c->digits);
        sb_puts(&s, " vbill-pool ");
        if (st == ZXV_OK) st = sb_amount(&s, d->fee_part[ZXV_FEE_VBILL_DIVIDEND], c->digits);
        sb_puts(&s, " infrastructure ");
        if (st == ZXV_OK) st = sb_amount(&s, d->fee_part[ZXV_FEE_INFRA_BOUNTY], c->digits);
        sb_puts(&s, " regenerative ");
        if (st == ZXV_OK) st = sb_amount(&s, d->fee_part[ZXV_FEE_REGEN_CAPITAL], c->digits);
        sb_puts(&s, "\n");
    }
    if (st != ZXV_OK) return st;
    *out_len = s.len;
    if (!buf || cap <= s.len) return ZXV_E_BUFFER;
    buf[s.len] = '\0';
    return ZXV_OK;
}

int32_t zvm_eod_close(zvm_terminal *t, uint32_t day, uint8_t out_digest[32]) {
    char *rep;
    size_t len = 0;
    uint8_t dig[32], *buf;
    zind_wr w;
    int32_t st;
    size_t bcap;
    if (!t) return ZXV_E_ARG;
    bcap = 64 + (size_t) t->n_ccy * (4 + 4 * 3 + 8 * 7);
    for (uint32_t k = t->next_queued; k < t->n_prf; k++)
        if (t->prf[k].state == ZVM_PRF_QUEUED) {
            zind_set_error("zvm_eod_close: %u proofs are still queued; settle first",
                           t->n_prf - k);
            return ZVM_E_PENDING;
        }
    if (t->closed_once && day <= t->last_day) {
        zind_set_error("zvm_eod_close: day %u is not after the last closed day %u", day,
                       t->last_day);
        return ZXV_E_STATE;
    }
    st = zvm_eod_report(t, day, NULL, 0, &len);
    if (st != ZXV_E_BUFFER) return st == ZXV_OK ? ZXV_E_INTERNAL : st;
    rep = (char *) malloc(len + 1);
    buf = (uint8_t *) malloc(bcap);
    if (!rep || !buf) {
        free(rep);
        free(buf);
        return ZXV_E_NOMEM;
    }
    st = zvm_eod_report(t, day, rep, len + 1, &len);
    if (st == ZXV_OK) st = zxv_sha3_256((const uint8_t *) rep, len, dig);
    if (st == ZXV_OK) {
        zind_wr_init(&w, buf, bcap);
        zind_wr_u32(&w, day);
        zind_wr_bytes(&w, dig, 32);
        zind_wr_u8(&w, (uint8_t) t->n_ccy);
        for (uint32_t i = 0; i < t->n_ccy; i++) {
            const zvm_totals *d = &t->ccy[i].day;
            zind_wr_str(&w, t->ccy[i].code);
            zind_wr_u32(&w, d->settled);
            zind_wr_u32(&w, d->bounced);
            zind_wr_u32(&w, d->duplicates);
            zind_wr_u64(&w, (uint64_t) d->gross);
            zind_wr_u64(&w, (uint64_t) d->fee);
            zind_wr_u64(&w, (uint64_t) d->net);
            for (int k = 0; k < ZXV_FEE_BUCKETS; k++) zind_wr_u64(&w, (uint64_t) d->fee_part[k]);
        }
        st = append(t, ZVM_REC_EOD, &w, NULL);
    }
    free(rep);
    free(buf);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < t->n_ccy; i++) memset(&t->ccy[i].day, 0, sizeof t->ccy[i].day);
    t->last_day = day;
    t->closed_once = 1;
    if (out_digest) memcpy(out_digest, dig, 32);
    return ZXV_OK;
}
