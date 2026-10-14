/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zspeg.c — sovereign-peg: operator calls over zxv_vhub, 64-state capital
 * controls, backed-only issuance, officer decisions and the signed operator
 * log. Every peg, quote, swap, limit and fee figure comes from libzxv; this
 * file only decides whether to ask for it. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zspeg.h"

#define DOMAIN "sovereign-peg"
#define MAX_ACCTS 512u

typedef struct {
    int used;
    uint8_t issuer_pk[ZIND_PK_BYTES];
    uint64_t outflow_cap; /* 0 = none */
} ccy_t;

typedef struct {
    zsp_flow f;
    int32_t state, status;
    uint8_t tag;
    uint8_t rec_hash[32]; /* the flow record a decision binds to */
} flow_t;

struct zsp_desk {
    zxv_vhub *hub;
    const zind_signer *signer;
    zind_qsink *qs;
    zind_chain *chain;
    ccy_t ccy[ZXV_VHUB_MAX_CCY];
    uint32_t acct[MAX_ACCTS];
    uint8_t acct_slot[MAX_ACCTS];
    uint32_t n_acct;
    uint32_t blocked[ZSP_MAX_BLOCKED];
    uint32_t n_blocked;
    uint8_t officers[ZSP_MAX_OFFICERS][32];
    uint32_t n_officers;
    uint8_t policy[ZXV_HEX_STATES];
    flow_t *flows;
    uint32_t n_flows, alloc;
};

const char *zsp_status_name(int32_t status) {
    switch (status) {
    case ZSP_E_UNBACKED: return "ZSP_E_UNBACKED";
    case ZSP_E_POLICY: return "ZSP_E_POLICY";
    case ZSP_E_OFFICER: return "ZSP_E_OFFICER";
    case ZSP_E_REFUSED: return "ZSP_E_REFUSED";
    case ZSP_E_QUARANTINED: return "ZSP_E_QUARANTINED";
    case ZSP_E_OPERATOR: return "ZSP_E_OPERATOR";
    default: return zind_status_name(status);
    }
}

/* ---- helpers ---- */

/* An operator cap: the bits, and a cap of the hub's own context (the hub
 * answers ZXV_E_CAP for a cap of another context). */
static int32_t op_ok(zsp_desk *d, const zxv_cap *op, const char *fn) {
    int32_t st;
    if (!d || !op) return ZXV_E_ARG;
    if (zxv_cap_check(op, ZSP_CAP_OPERATOR) != ZXV_OK) {
        zind_set_error("%s: needs an operator cap (LEDGER_POST, LEDGER_READ, CRYPTO_SIGN, "
                       "ATTEST, attested TRUE)",
                       fn);
        return ZSP_E_OPERATOR;
    }
    st = zxv_vhub_balance(op, d->hub, ZXV_VHUB_VBILL_ISSUER, NULL, NULL, NULL);
    if (st == ZXV_E_CAP) {
        zind_set_error("%s: the cap belongs to another context than this desk", fn);
        return ZSP_E_OPERATOR;
    }
    return st;
}

static uint32_t op_pid(const zxv_cap *op) {
    uint32_t pid = 0;
    (void) zxv_cap_info(op, NULL, &pid, NULL, NULL);
    return pid;
}

/* Every record starts with the operator's process id. */
static void rec_begin(zind_wr *w, uint8_t *buf, size_t cap, const zxv_cap *op) {
    zind_wr_init(w, buf, cap);
    zind_wr_u32(w, op_pid(op));
}

static int32_t append(zsp_desk *d, const zxv_cap *op, uint16_t kind, const zind_wr *w,
                      uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(op, d->chain, d->signer, kind, w->buf, w->len, NULL, hash);
}

static void quarantine(zsp_desk *d, uint8_t tag, uint8_t reason, uint32_t who, const uint8_t *data,
                       size_t len) {
    uint8_t subj[32];
    if (!d->qs || zind_subject("sovereign-peg/event", data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(d->qs, tag, reason, who, subj, NULL);
}

static int slot_ok(const zsp_desk *d, uint8_t slot) {
    return slot < ZXV_VHUB_MAX_CCY && d->ccy[slot].used;
}

static int holder_slot(const zsp_desk *d, uint32_t account, uint8_t *out_slot) {
    for (uint32_t i = 0; i < d->n_acct; i++)
        if (d->acct[i] == account) {
            if (out_slot) *out_slot = d->acct_slot[i];
            return 1;
        }
    return 0;
}

static int is_blocked(const zsp_desk *d, uint32_t account) {
    for (uint32_t i = 0; i < d->n_blocked; i++)
        if (d->blocked[i] == account) return 1;
    return 0;
}

/* circulation <= float at num/den, exactly (zxv_rat_cmp). num == 0 means
 * no peg: only an empty circulation is covered. */
static int32_t covered(uint64_t circ, uint64_t flt, uint64_t num, uint64_t den, int32_t *out) {
    int32_t c = 0, st;
    if (circ == 0) {
        *out = 1;
        return ZXV_OK;
    }
    if (num == 0 || den == 0) {
        *out = 0;
        return ZXV_OK;
    }
    if (circ > (uint64_t) INT64_MAX || flt > (uint64_t) INT64_MAX || num > (uint64_t) INT64_MAX ||
        den > (uint64_t) INT64_MAX)
        return ZXV_E_RANGE;
    /* circ / num <= flt / den  <=>  circ * den <= flt * num */
    st = zxv_rat_cmp((int64_t) circ, (int64_t) num, (int64_t) flt, (int64_t) den, &c);
    if (st != ZXV_OK) return st;
    *out = c <= 0;
    return ZXV_OK;
}

/* Circulation (issuer CREDIT - pool) and float of a slot, read from the hub. */
static int32_t circ_float(zsp_desk *d, const zxv_cap *cap, uint8_t slot, uint64_t *circ,
                          uint64_t *flt, uint64_t *num, uint64_t *den) {
    uint32_t iss = 0, pool = 0, vf = 0;
    uint64_t credit = 0;
    int64_t pool_eq = 0, vf_eq = 0;
    int32_t st = zxv_vhub_ccy_info(cap, d->hub, slot, &iss, &pool, &vf, num, den, NULL);
    if (st == ZXV_OK) st = zxv_vhub_balance(cap, d->hub, iss, NULL, &credit, NULL);
    if (st == ZXV_OK) st = zxv_vhub_balance(cap, d->hub, pool, NULL, NULL, &pool_eq);
    if (st == ZXV_OK) st = zxv_vhub_balance(cap, d->hub, vf, NULL, NULL, &vf_eq);
    if (st != ZXV_OK) return st;
    if (pool_eq < 0 || vf_eq < 0 || (uint64_t) pool_eq > credit) {
        zind_set_error("zsp: slot %u: the hub's balances do not add up", slot);
        return ZXV_E_INVARIANT;
    }
    *circ = credit - (uint64_t) pool_eq;
    *flt = (uint64_t) vf_eq;
    return ZXV_OK;
}

static uint8_t default_decision(uint8_t tag) {
    int32_t act = ZXV_HEX_ACT_REFUSE;
    (void) zxv_hex_action(tag, &act);
    switch (act) {
    case ZXV_HEX_ACT_PROCEED: return ZSP_ALLOW;
    case ZXV_HEX_ACT_WAIT:
    case ZXV_HEX_ACT_ESCALATE: return ZSP_HOLD;
    default: return ZSP_REFUSE;
    }
}

/* ---- desk ---- */

int32_t zsp_desk_create(const zxv_cap *op, const uint8_t *hub_seed, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, zsp_desk **out) {
    zsp_desk *d;
    int32_t st;
    uint8_t buf[64];
    zind_wr w;
    if (!op || !desk_signer || !out) return ZXV_E_ARG;
    *out = NULL;
    if (zxv_cap_check(op, ZSP_CAP_OPERATOR) != ZXV_OK) {
        zind_set_error("zsp_desk_create: needs an operator cap (LEDGER_POST, LEDGER_READ, "
                       "CRYPTO_SIGN, ATTEST)");
        return ZSP_E_OPERATOR;
    }
    d = (zsp_desk *) calloc(1, sizeof *d);
    if (!d) return ZXV_E_NOMEM;
    d->signer = desk_signer;
    for (uint32_t t = 0; t < ZXV_HEX_STATES; t++) d->policy[t] = default_decision((uint8_t) t);
    st = zxv_vhub_create(op, hub_seed, &d->hub);
    if (st == ZXV_OK) st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &d->chain);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(op, qlog, NULL, &d->qs);
    if (st == ZXV_OK) {
        rec_begin(&w, buf, sizeof buf, op);
        zind_wr_bytes(&w, zind_signer_key_id(desk_signer), 32);
        st = append(d, op, ZSP_REC_CREATE, &w, NULL);
    }
    if (st != ZXV_OK) {
        zsp_desk_destroy(d);
        return st;
    }
    *out = d;
    return ZXV_OK;
}

void zsp_desk_destroy(zsp_desk *d) {
    if (!d) return;
    zxv_vhub_destroy(d->hub);
    zind_chain_destroy(d->chain);
    zind_qsink_destroy(d->qs);
    free(d->flows);
    free(d);
}

const zind_chain *zsp_log(const zsp_desk *d) { return d ? d->chain : NULL; }

int32_t zsp_officer_add(zsp_desk *d, const zxv_cap *op, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32], buf[64];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_officer_add");
    if (st != ZXV_OK) return st;
    if (!pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < d->n_officers; i++)
        if (memcmp(d->officers[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (d->n_officers >= ZSP_MAX_OFFICERS) return ZXV_E_CAPACITY;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_bytes(&w, id, 32);
    st = append(d, op, ZSP_REC_OFFICER, &w, NULL);
    if (st != ZXV_OK) return st;
    memcpy(d->officers[d->n_officers++], id, 32);
    return ZXV_OK;
}

int32_t zsp_ccy_add(zsp_desk *d, const zxv_cap *op, uint8_t slot, const char *code,
                    uint8_t minor_units, const uint8_t issuer_pk[ZIND_PK_BYTES],
                    uint64_t pool_funding, uint64_t vbill_backing) {
    uint8_t buf[96], kh[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_ccy_add");
    if (st != ZXV_OK) return st;
    if (!code || !issuer_pk) return ZXV_E_ARG;
    st = zxv_vhub_add_ccy(op, d->hub, slot, code, minor_units, issuer_pk, pool_funding,
                          vbill_backing);
    if (st != ZXV_OK) return st; /* the library said why */
    d->ccy[slot].used = 1;
    memcpy(d->ccy[slot].issuer_pk, issuer_pk, ZIND_PK_BYTES);
    d->ccy[slot].outflow_cap = 0;
    (void) zxv_sha3_256(issuer_pk, ZIND_PK_BYTES, kh);
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, slot);
    zind_wr_str(&w, code);
    zind_wr_u8(&w, minor_units);
    zind_wr_bytes(&w, kh, 32);
    zind_wr_u64(&w, pool_funding);
    zind_wr_u64(&w, vbill_backing);
    return append(d, op, ZSP_REC_CCY, &w, NULL);
}

int32_t zsp_peg_parse(const char *rate, uint8_t minor_units, uint64_t *out_num,
                      uint64_t *out_den) {
    zind_qty q, scale, r;
    int64_t p10 = 1;
    int32_t st;
    if (!rate || !out_num || !out_den || minor_units > 4) return ZXV_E_ARG;
    st = zind_qty_parse(rate, &q);
    if (st != ZXV_OK) return st;
    if (zind_qty_sign(q) <= 0) {
        zind_set_error("zsp_peg_parse: a peg is a positive number of units per V-Bill");
        return ZXV_E_RANGE;
    }
    for (uint8_t i = 0; i < minor_units; i++) p10 *= 10;
    st = zind_qty_make(p10, 1, &scale);
    if (st == ZXV_OK) st = zind_qty_mul(q, scale, &r);
    if (st != ZXV_OK) return st;
    if ((uint64_t) r.num > ZXV_VHUB_AMOUNT_MAX || (uint64_t) r.den > ZXV_VHUB_AMOUNT_MAX) {
        zind_set_error("zsp_peg_parse: num/den beyond ZXV_VHUB_AMOUNT_MAX");
        return ZXV_E_RANGE;
    }
    *out_num = (uint64_t) r.num;
    *out_den = (uint64_t) r.den;
    return ZXV_OK;
}

int32_t zsp_peg_message(uint8_t slot, uint64_t num, uint64_t den, uint64_t epoch,
                        uint8_t out[ZXV_VHUB_PEG_MSG_BYTES]) {
    return zxv_vhub_peg_msg(slot, num, den, epoch, out);
}

int32_t zsp_peg_set(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t num, uint64_t den,
                    uint64_t epoch, const uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[ZXV_VHUB_PEG_MSG_BYTES], buf[96], mh[32];
    uint64_t circ = 0, flt = 0, cn = 0, cd = 0;
    int32_t ok = 0;
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_peg_set");
    if (st != ZXV_OK) return st;
    if (!sig) return ZXV_E_ARG;
    if (!slot_ok(d, slot)) return ZXV_E_NOT_FOUND;
    if (num == 0 || den == 0 || num > ZXV_VHUB_AMOUNT_MAX || den > ZXV_VHUB_AMOUNT_MAX) {
        zind_set_error("zsp_peg_set: num and den are 1..ZXV_VHUB_AMOUNT_MAX");
        return ZXV_E_RANGE;
    }
    st = zxv_vhub_peg_msg(slot, num, den, epoch, msg);
    if (st != ZXV_OK) return st;
    /* the issuer's signature first: a forgery is quarantined, whatever else */
    st = zxv_mldsa87_verify(op, d->ccy[slot].issuer_pk, msg, sizeof msg, NULL, 0, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0;
        zind_set_error("zsp_peg_set: not the issuer's signature over these numbers");
        if (st == ZXV_E_VERIFY &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL, &ftag) == ZXV_OK)
            quarantine(d, ftag, ZIND_REASON_TAMPER, slot, msg, sizeof msg);
        return st;
    }
    st = circ_float(d, op, slot, &circ, &flt, &cn, &cd);
    if (st == ZXV_OK) st = covered(circ, flt, num, den, &ok);
    if (st != ZXV_OK) return st;
    if (!ok) {
        zind_set_error("zsp_peg_set: at %llu/%llu the float (%llu V-Bills) no longer covers "
                       "the circulation (%llu)",
                       (unsigned long long) num, (unsigned long long) den,
                       (unsigned long long) flt, (unsigned long long) circ);
        return ZSP_E_UNBACKED;
    }
    st = zxv_vhub_set_peg(op, d->hub, slot, num, den, epoch, sig);
    if (st != ZXV_OK) return st; /* ZXV_E_PEG: stale or replayed epoch */
    (void) zxv_sha3_256(msg, sizeof msg, mh);
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, slot);
    zind_wr_u64(&w, num);
    zind_wr_u64(&w, den);
    zind_wr_u64(&w, epoch);
    zind_wr_bytes(&w, mh, 32);
    return append(d, op, ZSP_REC_PEG, &w, NULL);
}

int32_t zsp_peg_get(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint64_t *out_num,
                    uint64_t *out_den, uint64_t *out_epoch) {
    if (!d || !rd) return ZXV_E_ARG;
    return zxv_vhub_ccy_info(rd, d->hub, slot, NULL, NULL, NULL, out_num, out_den, out_epoch);
}

int32_t zsp_limit_add(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t limit) {
    uint8_t buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_limit_add");
    if (st != ZXV_OK) return st;
    st = zxv_vhub_add_limit(op, d->hub, slot, limit);
    if (st != ZXV_OK) return st;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, slot);
    zind_wr_u64(&w, limit);
    return append(d, op, ZSP_REC_LIMIT, &w, NULL);
}

int32_t zsp_outflow_cap_set(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t cap_minor) {
    uint8_t buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_outflow_cap_set");
    if (st != ZXV_OK) return st;
    if (!slot_ok(d, slot)) return ZXV_E_NOT_FOUND;
    if (cap_minor > ZXV_VHUB_AMOUNT_MAX) return ZXV_E_RANGE;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, slot);
    zind_wr_u64(&w, cap_minor);
    st = append(d, op, ZSP_REC_CONTROL, &w, NULL);
    if (st != ZXV_OK) return st;
    d->ccy[slot].outflow_cap = cap_minor;
    return ZXV_OK;
}

int32_t zsp_block_sender(zsp_desk *d, const zxv_cap *op, uint32_t account) {
    uint8_t buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_block_sender");
    if (st != ZXV_OK) return st;
    if (!holder_slot(d, account, NULL)) return ZXV_E_NOT_FOUND;
    if (is_blocked(d, account)) return ZXV_E_DUPLICATE;
    if (d->n_blocked >= ZSP_MAX_BLOCKED) return ZXV_E_CAPACITY;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u32(&w, account);
    st = append(d, op, ZSP_REC_BLOCK, &w, NULL);
    if (st != ZXV_OK) return st;
    d->blocked[d->n_blocked++] = account;
    return ZXV_OK;
}

int32_t zsp_account_open(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint32_t owner,
                         uint32_t *out_account) {
    uint8_t buf[32];
    uint32_t a = 0;
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_account_open");
    if (st != ZXV_OK) return st;
    if (!out_account) return ZXV_E_ARG;
    if (d->n_acct >= MAX_ACCTS) return ZXV_E_CAPACITY;
    st = zxv_vhub_open_account(op, d->hub, slot, owner, &a);
    if (st != ZXV_OK) return st;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, slot);
    zind_wr_u32(&w, owner);
    zind_wr_u32(&w, a);
    st = append(d, op, ZSP_REC_ACCOUNT, &w, NULL);
    if (st != ZXV_OK) return st;
    d->acct[d->n_acct] = a;
    d->acct_slot[d->n_acct++] = slot;
    *out_account = a;
    return ZXV_OK;
}

int32_t zsp_issue(zsp_desk *d, const zxv_cap *op, uint32_t account, uint64_t amount) {
    uint8_t slot = 0, buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_issue");
    if (st != ZXV_OK) return st;
    if (amount == 0 || amount > ZXV_VHUB_AMOUNT_MAX) return ZXV_E_RANGE;
    if (holder_slot(d, account, &slot)) {
        uint64_t circ = 0, flt = 0, num = 0, den = 0;
        int32_t ok = 0;
        st = circ_float(d, op, slot, &circ, &flt, &num, &den);
        if (st != ZXV_OK) return st;
        if (num == 0) {
            zind_set_error("zsp_issue: slot %u has no peg yet, so nothing can be backed", slot);
            return ZXV_E_PEG;
        }
        st = covered(circ + amount, flt, num, den, &ok);
        if (st != ZXV_OK) return st;
        if (!ok) {
            zind_set_error("zsp_issue: %llu more would leave circulation %llu above what the "
                           "float of %llu V-Bills covers at %llu/%llu",
                           (unsigned long long) amount, (unsigned long long) circ,
                           (unsigned long long) flt, (unsigned long long) num,
                           (unsigned long long) den);
            return ZSP_E_UNBACKED;
        }
    }
    /* pools, floats and anything else: the library decides */
    st = zxv_vhub_issue(op, d->hub, account, amount);
    if (st != ZXV_OK) return st;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u32(&w, account);
    zind_wr_u64(&w, amount);
    return append(d, op, ZSP_REC_ISSUE, &w, NULL);
}

int32_t zsp_coverage(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint64_t *out_circulation,
                     uint64_t *out_float, int32_t *out_covered) {
    uint64_t circ = 0, flt = 0, num = 0, den = 0;
    int32_t ok = 0, st;
    if (!d || !rd) return ZXV_E_ARG;
    if (!slot_ok(d, slot)) return ZXV_E_NOT_FOUND;
    st = circ_float(d, rd, slot, &circ, &flt, &num, &den);
    if (st == ZXV_OK) st = covered(circ, flt, num, den, &ok);
    if (st != ZXV_OK) return st;
    if (out_circulation) *out_circulation = circ;
    if (out_float) *out_float = flt;
    if (out_covered) *out_covered = ok;
    return ZXV_OK;
}

int32_t zsp_balance(zsp_desk *d, const zxv_cap *rd, uint32_t account, int64_t *out_equity) {
    if (!d || !rd) return ZXV_E_ARG;
    return zxv_vhub_balance(rd, d->hub, account, NULL, NULL, out_equity);
}

int32_t zsp_ccy_accounts(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint32_t *out_issuer,
                         uint32_t *out_pool, uint32_t *out_float) {
    if (!d || !rd) return ZXV_E_ARG;
    return zxv_vhub_ccy_info(rd, d->hub, slot, out_issuer, out_pool, out_float, NULL, NULL, NULL);
}

int32_t zsp_quote(zsp_desk *d, const zxv_cap *rd, uint8_t from_slot, uint8_t to_slot,
                  uint64_t want, uint64_t *out_amount, uint64_t *out_vbills,
                  uint64_t *out_received) {
    if (!d || !rd) return ZXV_E_ARG;
    return zxv_vhub_quote(rd, d->hub, from_slot, to_slot, want, out_amount, out_vbills,
                          out_received);
}

/* ---- policy ---- */

int32_t zsp_policy_get(const zsp_desk *d, uint8_t tag, int32_t *out_decision) {
    if (!d || !out_decision || tag >= ZXV_HEX_STATES) return ZXV_E_ARG;
    *out_decision = d->policy[tag];
    return ZXV_OK;
}

int32_t zsp_policy_set(zsp_desk *d, const zxv_cap *op, uint8_t tag, int32_t decision) {
    uint8_t base = 0, pol = 0, buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_policy_set");
    if (st != ZXV_OK) return st;
    if (tag >= ZXV_HEX_STATES || decision < ZSP_ALLOW || decision > ZSP_REFUSE)
        return ZXV_E_ARG;
    (void) zxv_hex_unpack(tag, &base, &pol, NULL);
    if (pol == ZXV_HEX_POL_FORBIDDEN && decision != ZSP_REFUSE) {
        zind_set_error("zsp_policy_set: a Forbidden tag (%s) always refuses", zxv_hex_name(tag));
        return ZSP_E_POLICY;
    }
    if ((base == ZXV_HEX_BASE_BOTH || base == ZXV_HEX_BASE_FALSE) && decision == ZSP_ALLOW) {
        zind_set_error("zsp_policy_set: %s cannot be allowed (conflicting or refused "
                       "authorisation)",
                       zxv_hex_name(tag));
        return ZSP_E_POLICY;
    }
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u8(&w, tag);
    zind_wr_u8(&w, (uint8_t) decision);
    st = append(d, op, ZSP_REC_POLICY, &w, NULL);
    if (st != ZXV_OK) return st;
    d->policy[tag] = (uint8_t) decision;
    return ZXV_OK;
}

static int32_t flow_args(const zsp_desk *d, const zsp_flow *f) {
    uint8_t s1 = 0, s2 = 0;
    if (!f) return ZXV_E_ARG;
    if (!slot_ok(d, f->from_slot) || !slot_ok(d, f->to_slot)) return ZXV_E_NOT_FOUND;
    if (f->from_slot == f->to_slot) {
        zind_set_error("zsp: a flow goes between two currencies");
        return ZXV_E_ARG;
    }
    if (f->amount == 0 || f->amount > ZXV_VHUB_AMOUNT_MAX) return ZXV_E_RANGE;
    if (!holder_slot(d, f->sender, &s1) || !holder_slot(d, f->receiver, &s2)) {
        zind_set_error("zsp: sender and receiver must be holder accounts opened on this desk");
        return ZXV_E_NOT_FOUND;
    }
    if (s1 != f->from_slot || s2 != f->to_slot) return ZXV_E_CURRENCY;
    return ZXV_OK;
}

static int32_t classify(const zsp_desk *d, const zsp_flow *f, uint8_t *out_tag) {
    int32_t lp = ZXV_LP_NEITHER, st;
    uint8_t base = 0, pol = ZXV_HEX_POL_NEUTRAL;
    uint64_t cap = d->ccy[f->from_slot].outflow_cap;
    st = zxv_lp_from_evidence(f->approvals, f->objections, &lp);
    if (st == ZXV_OK) st = zxv_hex_from_lp(lp, &base);
    if (st != ZXV_OK) return st;
    if (is_blocked(d, f->sender) || (cap != 0 && f->amount > cap))
        pol = ZXV_HEX_POL_FORBIDDEN;
    else if (f->approvals >= 2 && f->objections == 0)
        pol = ZXV_HEX_POL_NEGENTROPIC;
    return zxv_hex_pack(base, pol, ZXV_HEX_PHASE_POTENTIAL, out_tag);
}

int32_t zsp_flow_tag(const zsp_desk *d, const zsp_flow *f, uint8_t *out_tag) {
    int32_t st;
    if (!d || !out_tag) return ZXV_E_ARG;
    st = flow_args(d, f);
    if (st != ZXV_OK) return st;
    return classify(d, f, out_tag);
}

/* Potential -> Emerging -> Manifest, one Gray step at a time. */
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

static void flow_bytes(zind_wr *w, uint32_t id, const zsp_flow *f) {
    zind_wr_u32(w, id);
    zind_wr_u8(w, f->from_slot);
    zind_wr_u8(w, f->to_slot);
    zind_wr_u32(w, f->sender);
    zind_wr_u32(w, f->receiver);
    zind_wr_u64(w, f->amount);
    zind_wr_u64(w, f->tick);
    zind_wr_u32(w, f->approvals);
    zind_wr_u32(w, f->objections);
}

int32_t zsp_flow_submit(zsp_desk *d, const zxv_cap *op, const zsp_flow *f, uint32_t *out_id) {
    flow_t fl;
    uint8_t tag = 0, pol = 0, base = 0, buf[96];
    uint64_t vb = 0, recv = 0;
    int32_t dec, st = op_ok(d, op, "zsp_flow_submit"), ret;
    zind_wr w;
    if (st != ZXV_OK) return st;
    if (!out_id) return ZXV_E_ARG;
    st = flow_args(d, f);
    if (st != ZXV_OK) return st;
    if (d->n_flows >= ZSP_MAX_FLOWS) return ZXV_E_CAPACITY;
    if (d->n_flows == d->alloc) {
        uint32_t a = d->alloc ? d->alloc * 2 : 64;
        flow_t *nf = (flow_t *) realloc(d->flows, (size_t) a * sizeof *nf);
        if (!nf) return ZXV_E_NOMEM;
        d->flows = nf;
        d->alloc = a;
    }
    st = classify(d, f, &tag);
    if (st != ZXV_OK) return st;
    (void) zxv_hex_unpack(tag, &base, &pol, NULL);
    dec = pol == ZXV_HEX_POL_FORBIDDEN ? ZSP_REFUSE : d->policy[tag];
    memset(&fl, 0, sizeof fl);
    fl.f = *f;
    fl.tag = tag;
    if (pol == ZXV_HEX_POL_FORBIDDEN) {
        fl.state = ZSP_FLOW_QUARANTINED;
        ret = ZSP_E_QUARANTINED;
        zind_set_error("zsp_flow_submit: %s: refused and quarantined", zxv_hex_name(tag));
    } else if (dec == ZSP_HOLD) {
        fl.state = ZSP_FLOW_HELD;
        ret = ZIND_E_HELD;
        zind_set_error("zsp_flow_submit: %s: held for an officer", zxv_hex_name(tag));
    } else if (dec == ZSP_REFUSE) {
        fl.state = ZSP_FLOW_REFUSED;
        ret = ZSP_E_REFUSED;
        zind_set_error("zsp_flow_submit: %s: refused by policy", zxv_hex_name(tag));
    } else {
        st = zxv_vhub_swap(op, d->hub, f->from_slot, f->to_slot, f->sender, f->receiver,
                           f->amount, f->tick, &vb, &recv);
        if (st == ZXV_OK) {
            fl.state = ZSP_FLOW_DONE;
            st = to_manifest(tag, &fl.tag);
            if (st != ZXV_OK) return st;
        } else {
            fl.state = ZSP_FLOW_FAILED;
        }
        ret = st;
    }
    fl.status = ret;
    rec_begin(&w, buf, sizeof buf, op);
    flow_bytes(&w, d->n_flows + 1, f);
    zind_wr_u8(&w, fl.tag);
    zind_wr_u8(&w, (uint8_t) fl.state);
    zind_wr_u32(&w, (uint32_t) -ret);
    zind_wr_u64(&w, vb);
    zind_wr_u64(&w, recv);
    st = append(d, op, ZSP_REC_FLOW, &w, fl.rec_hash);
    if (st != ZXV_OK) return st;
    d->flows[d->n_flows++] = fl;
    *out_id = d->n_flows;
    if (fl.state == ZSP_FLOW_QUARANTINED)
        quarantine(d, tag, ZIND_REASON_FORBIDDEN, f->sender, fl.rec_hash, 32);
    else if (fl.state == ZSP_FLOW_HELD && base == ZXV_HEX_BASE_BOTH)
        quarantine(d, tag, ZIND_REASON_HELD_BOTH, f->sender, fl.rec_hash, 32);
    return ret;
}

static flow_t *get_flow(const zsp_desk *d, uint32_t id) {
    if (!d || id == 0 || id > d->n_flows) {
        zind_set_error("zsp: unknown flow %u", id);
        return NULL;
    }
    return &d->flows[id - 1];
}

int32_t zsp_flow_state(const zsp_desk *d, uint32_t id, int32_t *out_state, uint8_t *out_tag,
                       int32_t *out_status) {
    const flow_t *f = get_flow(d, id);
    if (!f) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = f->state;
    if (out_tag) *out_tag = f->tag;
    if (out_status) *out_status = f->status;
    return ZXV_OK;
}

static int32_t dec_msg(uint32_t id, const flow_t *f, int32_t decision, uint8_t *buf, size_t cap,
                       size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zsp/decision/v1", 15);
    zind_wr_u32(&w, id);
    zind_wr_bytes(&w, f->rec_hash, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    *out_len = 15 + 4 + 32 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t zsp_decision_message(const zsp_desk *d, uint32_t id, int32_t decision, uint8_t *buf,
                             size_t cap, size_t *out_len) {
    const flow_t *f = get_flow(d, id);
    if (!f) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out_len || (decision != ZSP_RELEASE && decision != ZSP_DENY)) return ZXV_E_ARG;
    if (f->state != ZSP_FLOW_HELD) return ZXV_E_STATE;
    return dec_msg(id, f, decision, buf, cap, out_len);
}

int32_t zsp_decide(zsp_desk *d, const zxv_cap *op, uint32_t id,
                   const uint8_t officer_pk[ZIND_PK_BYTES], int32_t decision, uint64_t tick,
                   const uint8_t sig[ZIND_SIG_BYTES]) {
    flow_t *f;
    uint8_t kid[32], msg[64], mh[32], nt = 0, buf[128];
    size_t len = 0;
    uint64_t vb = 0, recv = 0;
    uint32_t i;
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_decide");
    if (st != ZXV_OK) return st;
    f = get_flow(d, id);
    if (!f) return ZXV_E_NOT_FOUND;
    if (!officer_pk || !sig || (decision != ZSP_RELEASE && decision != ZSP_DENY)) return ZXV_E_ARG;
    if (f->state != ZSP_FLOW_HELD) {
        zind_set_error("zsp_decide: flow %u is not held", id);
        return (f->state == ZSP_FLOW_RELEASED || f->state == ZSP_FLOW_DENIED) ? ZIND_E_REPLAY
                                                                              : ZXV_E_STATE;
    }
    st = zind_key_id(officer_pk, kid);
    if (st != ZXV_OK) return st;
    for (i = 0; i < d->n_officers; i++)
        if (memcmp(d->officers[i], kid, 32) == 0) break;
    if (i == d->n_officers) {
        zind_set_error("zsp_decide: the key is not a registered officer");
        return ZSP_E_OFFICER;
    }
    st = dec_msg(id, f, decision, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(op, officer_pk, msg, len, ZSP_DECISION_CTX, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0;
        zind_set_error("zsp_decide: the officer's signature does not verify");
        if (st == ZXV_E_VERIFY &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL, &ftag) == ZXV_OK)
            quarantine(d, ftag, ZIND_REASON_TAMPER, id, msg, len);
        return st;
    }
    if (decision == ZSP_RELEASE) {
        st = zxv_vhub_swap(op, d->hub, f->f.from_slot, f->f.to_slot, f->f.sender, f->f.receiver,
                           f->f.amount, tick, &vb, &recv);
        if (st != ZXV_OK) {
            f->status = st;
            return st; /* still held: the officer may deny it, or retry later */
        }
        st = zxv_hex_set(f->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st == ZXV_OK) st = to_manifest(nt, &nt);
    } else {
        st = zxv_hex_set(f->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
    }
    if (st != ZXV_OK) return st;
    (void) zxv_sha3_256(msg, len, mh);
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u32(&w, id);
    zind_wr_bytes(&w, kid, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    zind_wr_bytes(&w, mh, 32);
    zind_wr_u64(&w, tick);
    zind_wr_u64(&w, vb);
    zind_wr_u64(&w, recv);
    st = append(d, op, ZSP_REC_DECISION, &w, NULL);
    if (st != ZXV_OK) return st;
    f->tag = nt;
    f->state = decision == ZSP_RELEASE ? ZSP_FLOW_RELEASED : ZSP_FLOW_DENIED;
    f->status = ZXV_OK;
    return ZXV_OK;
}

int32_t zsp_epoch_next(zsp_desk *d, const zxv_cap *op, uint64_t *out_epoch) {
    uint64_t e = 0;
    uint8_t buf[32];
    zind_wr w;
    int32_t st = op_ok(d, op, "zsp_epoch_next");
    if (st != ZXV_OK) return st;
    st = zxv_vhub_next_epoch(op, d->hub, &e);
    if (st != ZXV_OK) return st;
    rec_begin(&w, buf, sizeof buf, op);
    zind_wr_u64(&w, e);
    st = append(d, op, ZSP_REC_EPOCH, &w, NULL);
    if (st != ZXV_OK) return st;
    if (out_epoch) *out_epoch = e;
    return ZXV_OK;
}

int32_t zsp_check(zsp_desk *d, const zxv_cap *rd, uint64_t *out_postings) {
    int32_t st;
    if (!d || !rd) return ZXV_E_ARG;
    st = zxv_vhub_check(rd, d->hub, out_postings);
    if (st != ZXV_OK) return st;
    for (uint8_t s = 0; s < ZXV_VHUB_MAX_CCY; s++) {
        int32_t ok = 0;
        if (!d->ccy[s].used) continue;
        st = zsp_coverage(d, rd, s, NULL, NULL, &ok);
        if (st != ZXV_OK) return st;
        if (!ok) {
            zind_set_error("zsp_check: slot %u: circulation is not covered by its float", s);
            return ZSP_E_UNBACKED;
        }
    }
    return ZXV_OK;
}
