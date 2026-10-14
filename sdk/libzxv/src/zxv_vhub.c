/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_vhub.c — ABI 1.5: the V-Bill hub over kernel/src/vhub.
 *
 * The hub owns one pay_ledger (its own, not the context's triple ledger),
 * one vhub_t on it, the issuers' ML-DSA-87 keys and the limit rules'
 * state. The kernel's peg verify hook is bound to ML-DSA-87 here; only the
 * built-in limit rule is registered (no host callbacks; see the header).
 * Every argument the kernel would refuse with a bare VHUB_ERR_ARG is
 * checked first, so each refusal has its own ZXV_E_* code and reason. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "vhub.h"
#ifndef ZXV_NO_PQC
#include "pq_matrix_algs.h"
#endif

_Static_assert(ZXV_VHUB_MAX_CCY == VHUB_MAX_CCY, "slots");
_Static_assert(ZXV_VHUB_MAX_RULES == VHUB_MAX_RULES, "rules");
_Static_assert(ZXV_VHUB_LIMIT_SENDERS == VHUB_LIMIT_SENDERS, "limit senders");
_Static_assert(ZXV_VHUB_PEG_MSG_BYTES == VHUB_PEG_MSG, "peg message");
/* pay_ledger refuses any line above 2^59 (8 lines never sum past 2^62). */
_Static_assert(ZXV_VHUB_AMOUNT_MAX == ((uint64_t) 1 << 59) && ZXV_VHUB_AMOUNT_MAX < PAY_BAL_MAX,
               "amount max");
_Static_assert(ZXV_VHUB_CODE_MAX == PAY_CODE_MAX, "code length");
_Static_assert(ZXV_MLDSA87_PK_BYTES == 2592u && ZXV_MLDSA87_SIG_BYTES == 4627u, "ML-DSA-87");

/* What a ledger account is to the hub. */
enum { ROLE_NONE = 0, ROLE_HOLDER, ROLE_ISSUER, ROLE_POOL, ROLE_VFLOAT, ROLE_VISSUER };

struct zxv_vhub {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    pay_ledger_t L;
    vhub_t h;
    uint16_t vbill_asset;
    uint32_t vissuer;
    uint8_t pk[VHUB_MAX_CCY][ZXV_MLDSA87_PK_BYTES];
    vhub_limit_t lim[VHUB_MAX_CCY][VHUB_MAX_RULES];
    uint8_t role[PAY_MAX_ACCOUNTS];
    uint8_t slot_of[PAY_MAX_ACCOUNTS]; /* currency slot of a non-V-Bill account */
    uint64_t nfund;                    /* faucet postings (their identifiers)   */
};

uint32_t zxvi_feat2_vhub(void)
{
#ifndef ZXV_NO_PQC
    return ZXV_FEAT2_VHUB;
#else
    return 0; /* pegs cannot be verified: the hub cannot be used */
#endif
}

int32_t zxv_vhub_peg_msg(uint8_t slot, uint64_t num, uint64_t den, uint64_t epoch, uint8_t out[40])
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "vhub_peg_msg: out is NULL");
    vhub_peg_msg(slot, num, den, epoch, out);
    return zxvi_ok();
}

/* The kernel's H1 hook: the issuer's ledger account names the slot whose
 * key must have signed. */
static bool verify_peg(void *vctx, uint32_t issuer, const uint8_t *msg, uint32_t len,
                       const uint8_t *sig, uint32_t sig_len)
{
#ifndef ZXV_NO_PQC
    zxv_vhub *v = vctx;
    if (issuer >= PAY_MAX_ACCOUNTS || v->role[issuer] != ROLE_ISSUER ||
        sig_len != ZXV_MLDSA87_SIG_BYTES)
        return false;
    /* The reference verify keeps no mutable state: no lock is needed. */
    return pqm_mldsa87_verify(v->pk[v->slot_of[issuer]], msg, len, NULL, 0, sig);
#else
    (void) vctx, (void) issuer, (void) msg, (void) len, (void) sig, (void) sig_len;
    return false;
#endif
}

void zxv_vhub_destroy(zxv_vhub *hub)
{
    if (!hub) return;
    pthread_mutex_destroy(&hub->mu);
    free(hub);
}

/* SHA-256(seed || tag || n), as the kernel derives its own identifiers. */
static void derive(const zxv_vhub *v, uint8_t tag, uint64_t n, uint8_t out[32])
{
    uint8_t in[41];
    memcpy(in, v->h.seed, 32);
    in[32] = tag;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (n >> (8 * i));
    sha256(in, sizeof in, out);
}

/* Poison the context (the hub ledger broke an invariant). Called with the
 * hub lock held; the context lock is always taken after it. */
static int32_t poison(zxv_vhub *v, const char *why)
{
    int32_t rc = zxvi_ctx_enter(v->ctx);
    if (rc == ZXV_E_POISONED) return rc;
    if (rc) return zxvi_fail(ZXV_E_INVARIANT, "%s", why);
    rc = zxvi_ctx_poison(v->ctx, why);
    zxvi_ctx_leave(v->ctx);
    return rc;
}

static int32_t pay_to_zxv(zxv_vhub *v, pay_status_t st, const char *what)
{
    switch (st) {
    case PAY_OK: return zxvi_ok();
    case PAY_ERR_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: the hub ledger is full", what);
    case PAY_ERR_FUNDS: return zxvi_fail(ZXV_E_FUNDS, "%s: an account would go below zero", what);
    case PAY_ERR_OVERFLOW:
        return zxvi_fail(ZXV_E_RANGE, "%s: an amount above 2^59 or a balance at 2^62", what);
    case PAY_ERR_STATE: return zxvi_fail(ZXV_E_DUPLICATE, "%s: already registered", what);
    case PAY_ERR_INVARIANT:
        return poison(v, "vhub: the hub ledger broke an accounting invariant");
    default:
        return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected pay_ledger status %d", what, (int) st);
    }
}

/* One faucet posting: `issuer` issues `amount` to `to` (same asset). */
static pay_status_t faucet(zxv_vhub *v, uint32_t issuer, uint32_t to, uint64_t amount)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    uint8_t d[32];
    memset(&r, 0, sizeof r);
    derive(v, 'i', v->nfund, r.idem_key);
    derive(v, 'u', v->nfund, d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "VHUB-FUND-");
    pay_w_u64(&w, v->nfund);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "v-bill hub test issue", sizeof r.memo);
    r.ext_cap = PAY_CAP_FINANCIAL;
    v->nfund++;
    pay_status_t st = pay_ledger_issue(&v->L, &r, issuer, to, amount, &rc);
    if (st == PAY_OK && !pay_ledger_check(&v->L)) st = PAY_ERR_INVARIANT;
    return st;
}

static pay_status_t open_acct(zxv_vhub *v, uint32_t owner, uint16_t asset, uint8_t flags,
                              uint8_t role, uint8_t slot, uint32_t *out)
{
    pay_status_t st = pay_ledger_open(&v->L, owner, asset, PAY_CAP_FINANCIAL, flags, out);
    if (st == PAY_OK) {
        v->role[*out] = role;
        v->slot_of[*out] = slot;
    }
    return st;
}

int32_t zxv_vhub_create(const zxv_cap *cap, const uint8_t *seed, zxv_vhub **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "vhub_create: out is NULL");
    *out = NULL;
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_LEDGER_POST, "vhub_create", &ctx);
    if (rc) return rc;
    uint8_t s[32];
    if (seed)
        memcpy(s, seed, 32);
    else if ((rc = zxv_random(s, sizeof s)) != ZXV_OK)
        return rc;
    zxv_vhub *v = calloc(1, sizeof *v);
    if (!v) {
        zxvi_wipe(s, sizeof s);
        return zxvi_fail(ZXV_E_NOMEM, "vhub_create: hub");
    }
    v->ctx = ctx;
    pay_ledger_init(&v->L, NULL);
    uint32_t vi = 0;
    if (pay_ledger_add_asset(&v->L, "VBL", 0, PAY_ASSET_UNIT, &v->vbill_asset) != PAY_OK ||
        open_acct(v, 0, v->vbill_asset, PAY_ACCT_ISSUER, ROLE_VISSUER, 0, &vi) != PAY_OK ||
        vi != ZXV_VHUB_VBILL_ISSUER) {
        free(v);
        zxvi_wipe(s, sizeof s);
        return zxvi_fail(ZXV_E_INTERNAL, "vhub_create: could not set up the V-Bill asset");
    }
    v->vissuer = vi;
    /* No quarantine storage: the built-in limit rule never forbids. */
    vhub_init(&v->h, &v->L, v->vbill_asset, verify_peg, v, s, NULL, 0);
    zxvi_wipe(s, sizeof s);
    pthread_mutex_init(&v->mu, NULL);
    *out = v;
    return zxvi_ok();
}

#define ENTER(v, bit, what)                                                      \
    if (!(v)) return zxvi_fail(ZXV_E_ARG, "%s: hub is NULL", what);             \
    int32_t rc_ = zxvi_cap_gate_ctx(cap, (bit), (what), (v)->ctx);              \
    if (rc_) return rc_;                                                         \
    pthread_mutex_lock(&(v)->mu)
#define LEAVE(v, value)                                                          \
    do {                                                                         \
        int32_t r_ = (value);                                                    \
        pthread_mutex_unlock(&(v)->mu);                                          \
        return r_;                                                               \
    } while (0)

static const vhub_ccy_t *ccy_of(const zxv_vhub *v, uint8_t slot)
{
    return (slot < VHUB_MAX_CCY && v->h.ccy[slot].used) ? &v->h.ccy[slot] : NULL;
}

/* A test currency code: 3..12 of [A-Z0-9], not ISO 4217. ("VBL" and
 * "VFV" are already assets of the hub ledger: the caller's duplicate scan
 * refuses them.) */
static int32_t check_code(const char *code)
{
    int32_t rc = zxvi_check_str(code, ZXV_VHUB_CODE_MAX, "currency code", false);
    if (rc) return rc;
    size_t n = strlen(code);
    if (n < 3) return zxvi_fail(ZXV_E_RANGE, "currency code \"%s\" is shorter than 3 bytes", code);
    for (size_t i = 0; i < n; i++) {
        char c = code[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return zxvi_fail(ZXV_E_FORMAT, "currency code may only use A-Z and 0-9");
    }
    if (zxv_iso4217_by_alpha(code, NULL, NULL, NULL) == ZXV_OK)
        return zxvi_fail(ZXV_E_CURRENCY,
                         "%s is an ISO 4217 currency: the demo hub takes test currencies only",
                         code);
    return ZXV_OK;
}

int32_t zxv_vhub_add_ccy(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, const char *code,
                         uint8_t minor_units, const uint8_t issuer_pk[2592],
                         uint64_t pool_funding, uint64_t vbill_backing)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_add_ccy");
    if (!issuer_pk) LEAVE(hub, zxvi_fail(ZXV_E_ARG, "vhub_add_ccy: issuer key is NULL"));
    if (slot >= VHUB_MAX_CCY)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_add_ccy: slot %u is not 0..7", (unsigned) slot));
    if (hub->h.ccy[slot].used)
        LEAVE(hub, zxvi_fail(ZXV_E_DUPLICATE, "vhub_add_ccy: slot %u is taken", (unsigned) slot));
    int32_t rc = check_code(code);
    if (rc) LEAVE(hub, rc);
    if (minor_units > 4)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_add_ccy: minor units %u > 4", (unsigned) minor_units));
    if (pool_funding > ZXV_VHUB_AMOUNT_MAX || vbill_backing > ZXV_VHUB_AMOUNT_MAX)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_add_ccy: funding above 2^59"));
    for (uint32_t i = 0; i < hub->L.n_assets; i++)
        if (!strcmp(hub->L.assets[i].code, code))
            LEAVE(hub, zxvi_fail(ZXV_E_DUPLICATE, "vhub_add_ccy: %s is already registered", code));
    /* Everything below can fail only on a full ledger: check room first so
     * a failure registers nothing. */
    if (hub->L.n_assets >= PAY_MAX_ASSETS || hub->L.n_accounts + 3 > PAY_MAX_ACCOUNTS)
        LEAVE(hub, zxvi_fail(ZXV_E_CAPACITY, "vhub_add_ccy: the hub ledger is full"));
    /* The issue postings can only overflow the V-Bill issuer's CREDIT. */
    const pay_account_t *vi = pay_ledger_account(&hub->L, hub->vissuer);
    if (vbill_backing > PAY_BAL_MAX - 1 - vi->credit)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_add_ccy: V-Bills issued would reach 2^62"));
    uint16_t asset;
    uint32_t iss, pool, vfl;
    pay_status_t st = pay_ledger_add_asset(&hub->L, code, minor_units, PAY_ASSET_UNIT, &asset);
    if (st == PAY_OK) st = open_acct(hub, 100u + slot, asset, PAY_ACCT_ISSUER, ROLE_ISSUER, slot, &iss);
    if (st == PAY_OK) st = open_acct(hub, 200u + slot, asset, 0, ROLE_POOL, slot, &pool);
    if (st == PAY_OK) st = open_acct(hub, 300u + slot, hub->vbill_asset, 0, ROLE_VFLOAT, slot, &vfl);
    if (st == PAY_OK && vhub_add_ccy(&hub->h, slot, asset, iss, pool, vfl) != VHUB_OK)
        st = PAY_ERR_INVARIANT;
    if (st != PAY_OK) LEAVE(hub, pay_to_zxv(hub, st, "vhub_add_ccy"));
    memcpy(hub->pk[slot], issuer_pk, ZXV_MLDSA87_PK_BYTES);
    if (pool_funding) st = faucet(hub, iss, pool, pool_funding);
    if (st == PAY_OK && vbill_backing) st = faucet(hub, hub->vissuer, vfl, vbill_backing);
    LEAVE(hub, pay_to_zxv(hub, st, "vhub_add_ccy"));
}

int32_t zxv_vhub_ccy_info(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot,
                          uint32_t *out_issuer_account, uint32_t *out_pool_account,
                          uint32_t *out_vfloat_account, uint64_t *out_peg_num,
                          uint64_t *out_peg_den, uint64_t *out_peg_epoch)
{
    ENTER(hub, ZXV_CAP_LEDGER_READ, "vhub_ccy_info");
    const vhub_ccy_t *x = ccy_of(hub, slot);
    if (!x)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_ccy_info: slot %u not registered", (unsigned) slot));
    if (out_issuer_account) *out_issuer_account = x->issuer;
    if (out_pool_account) *out_pool_account = x->pool;
    if (out_vfloat_account) *out_vfloat_account = x->vfloat;
    if (out_peg_num) *out_peg_num = x->num;
    if (out_peg_den) *out_peg_den = x->den;
    if (out_peg_epoch) *out_peg_epoch = x->peg_epoch;
    LEAVE(hub, zxvi_ok());
}

int32_t zxv_vhub_set_peg(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, uint64_t num,
                         uint64_t den, uint64_t epoch, const uint8_t sig[4627])
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_set_peg");
#ifdef ZXV_NO_PQC
    (void) slot, (void) num, (void) den, (void) epoch, (void) sig;
    LEAVE(hub, zxvi_fail(ZXV_E_NOTIMPL, "built without post-quantum crypto (ZXV_NO_PQC)"));
#else
    if (!sig) LEAVE(hub, zxvi_fail(ZXV_E_ARG, "vhub_set_peg: sig is NULL"));
    const vhub_ccy_t *x = ccy_of(hub, slot);
    if (!x)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_set_peg: slot %u not registered", (unsigned) slot));
    if (num == 0 || den == 0)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_set_peg: num and den must both be nonzero"));
    switch (vhub_set_peg(&hub->h, slot, num, den, epoch, sig, ZXV_MLDSA87_SIG_BYTES)) {
    case VHUB_OK: LEAVE(hub, zxvi_ok());
    case VHUB_ERR_EPOCH:
        LEAVE(hub, zxvi_fail(ZXV_E_PEG,
                             "vhub_set_peg: epoch %llu is not above the current peg's %llu",
                             (unsigned long long) epoch, (unsigned long long) x->peg_epoch));
    case VHUB_ERR_SIG:
        LEAVE(hub, zxvi_fail(ZXV_E_VERIFY,
                             "vhub_set_peg: not the issuer's ML-DSA-87 signature over this peg"));
    default: LEAVE(hub, zxvi_fail(ZXV_E_INTERNAL, "vhub_set_peg: unexpected kernel status"));
    }
#endif
}

int32_t zxv_vhub_open_account(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, uint32_t owner,
                              uint32_t *out_account)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_open_account");
    if (!out_account) LEAVE(hub, zxvi_fail(ZXV_E_ARG, "vhub_open_account: out is NULL"));
    const vhub_ccy_t *x = ccy_of(hub, slot);
    if (!x)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_open_account: slot %u not registered",
                             (unsigned) slot));
    uint32_t a;
    pay_status_t st = open_acct(hub, owner, x->asset, 0, ROLE_HOLDER, slot, &a);
    if (st != PAY_OK) LEAVE(hub, pay_to_zxv(hub, st, "vhub_open_account"));
    *out_account = a;
    LEAVE(hub, zxvi_ok());
}

int32_t zxv_vhub_issue(const zxv_cap *cap, zxv_vhub *hub, uint32_t account, uint64_t amount)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_issue");
    if (account >= hub->L.n_accounts)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_issue: no account %u", account));
    uint8_t role = hub->role[account];
    if (role != ROLE_HOLDER && role != ROLE_POOL && role != ROLE_VFLOAT)
        LEAVE(hub, zxvi_fail(ZXV_E_ARG, "vhub_issue: account %u is an issuer", account));
    if (amount == 0 || amount > ZXV_VHUB_AMOUNT_MAX)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_issue: amount must be 1..2^59"));
    uint32_t issuer = role == ROLE_VFLOAT ? hub->vissuer : hub->h.ccy[hub->slot_of[account]].issuer;
    LEAVE(hub, pay_to_zxv(hub, faucet(hub, issuer, account, amount), "vhub_issue"));
}

int32_t zxv_vhub_balance(const zxv_cap *cap, zxv_vhub *hub, uint32_t account, uint64_t *out_debit,
                         uint64_t *out_credit, int64_t *out_equity)
{
    ENTER(hub, ZXV_CAP_LEDGER_READ, "vhub_balance");
    const pay_account_t *a = pay_ledger_account(&hub->L, account);
    if (!a || account >= hub->L.n_accounts)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_balance: no account %u", account));
    if (out_debit) *out_debit = a->debit;
    if (out_credit) *out_credit = a->credit;
    if (out_equity) *out_equity = a->equity;
    LEAVE(hub, zxvi_ok());
}

int32_t zxv_vhub_add_limit(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, uint64_t limit)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_add_limit");
    const vhub_ccy_t *x = ccy_of(hub, slot);
    if (!x)
        LEAVE(hub, zxvi_fail(ZXV_E_NOT_FOUND, "vhub_add_limit: slot %u not registered", (unsigned) slot));
    if (x->n_rules >= VHUB_MAX_RULES)
        LEAVE(hub, zxvi_fail(ZXV_E_CAPACITY, "vhub_add_limit: currency already has %u rules",
                             (unsigned) VHUB_MAX_RULES));
    vhub_limit_t *l = &hub->lim[slot][x->n_rules];
    memset(l, 0, sizeof *l);
    l->limit = limit;
    l->epoch = hub->h.epoch;
    if (vhub_add_rule(&hub->h, slot, vhub_rule_limit, vhub_limit_commit, l) != VHUB_OK)
        LEAVE(hub, zxvi_fail(ZXV_E_INTERNAL, "vhub_add_limit: unexpected kernel status"));
    LEAVE(hub, zxvi_ok());
}

int32_t zxv_vhub_next_epoch(const zxv_cap *cap, zxv_vhub *hub, uint64_t *out_epoch)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_next_epoch");
    if (hub->h.epoch == UINT64_MAX)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_next_epoch: epoch exhausted"));
    hub->h.epoch++;
    if (out_epoch) *out_epoch = hub->h.epoch;
    LEAVE(hub, zxvi_ok());
}

/* The checks both quote and swap make before the kernel sees the pair. */
static int32_t pair_check(const zxv_vhub *v, uint8_t a, uint8_t b, const char *what)
{
    const vhub_ccy_t *A = ccy_of(v, a), *B = ccy_of(v, b);
    if (!A || !B)
        return zxvi_fail(ZXV_E_NOT_FOUND, "%s: slot %u not registered", what,
                         (unsigned) (A ? b : a));
    if (a == b) return zxvi_fail(ZXV_E_ARG, "%s: from and to are the same currency", what);
    if (!A->num || !B->num)
        return zxvi_fail(ZXV_E_PEG, "%s: slot %u has no signed peg yet", what,
                         (unsigned) (A->num ? b : a));
    return ZXV_OK;
}

/* H2: v = a*denA/numA, b = v*numB/denB, both exact. */
static bool convert(const vhub_ccy_t *A, const vhub_ccy_t *B, uint64_t a, uint64_t *v, uint64_t *b)
{
    uint64_t r1, r2;
    return pay_muldiv(a, A->den, A->num, v, &r1) && !r1 && pay_muldiv(*v, B->num, B->den, b, &r2) &&
           !r2;
}

int32_t zxv_vhub_quote(const zxv_cap *cap, zxv_vhub *hub, uint8_t from_slot, uint8_t to_slot,
                       uint64_t want, uint64_t *out_amount, uint64_t *out_vbills,
                       uint64_t *out_received)
{
    ENTER(hub, ZXV_CAP_LEDGER_READ, "vhub_quote");
    int32_t rc = pair_check(hub, from_slot, to_slot, "vhub_quote");
    if (rc) LEAVE(hub, rc);
    if (want > ZXV_VHUB_AMOUNT_MAX)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_quote: want above 2^59"));
    uint64_t q = vhub_quote(&hub->h, from_slot, to_slot, want), v = 0, b = 0;
    if (q && !convert(&hub->h.ccy[from_slot], &hub->h.ccy[to_slot], q, &v, &b))
        LEAVE(hub, zxvi_fail(ZXV_E_INTERNAL, "vhub_quote: quoted amount is not exact"));
    if (out_amount) *out_amount = q;
    if (out_vbills) *out_vbills = v;
    if (out_received) *out_received = b;
    LEAVE(hub, zxvi_ok());
}

static int32_t holder_check(const zxv_vhub *v, uint32_t acct, uint8_t slot, const char *who)
{
    if (acct >= v->L.n_accounts)
        return zxvi_fail(ZXV_E_NOT_FOUND, "vhub_swap: no %s account %u", who, acct);
    if (v->role[acct] != ROLE_HOLDER)
        return zxvi_fail(ZXV_E_ARG, "vhub_swap: %s account %u belongs to the hub, not a holder",
                         who, acct);
    if (v->slot_of[acct] != slot)
        return zxvi_fail(ZXV_E_CURRENCY, "vhub_swap: %s account %u is in slot %u, not %u", who,
                         acct, (unsigned) v->slot_of[acct], (unsigned) slot);
    return ZXV_OK;
}

static int64_t eq(const zxv_vhub *v, uint32_t a)
{
    return pay_ledger_account(&v->L, a)->equity;
}

int32_t zxv_vhub_swap(const zxv_cap *cap, zxv_vhub *hub, uint8_t from_slot, uint8_t to_slot,
                      uint32_t sender, uint32_t receiver, uint64_t amount, uint64_t tick,
                      uint64_t *out_vbills, uint64_t *out_received)
{
    ENTER(hub, ZXV_CAP_LEDGER_POST, "vhub_swap");
    int32_t rc = pair_check(hub, from_slot, to_slot, "vhub_swap");
    if (!rc) rc = holder_check(hub, sender, from_slot, "sender");
    if (!rc) rc = holder_check(hub, receiver, to_slot, "receiver");
    if (rc) LEAVE(hub, rc);
    if (amount == 0 || amount > ZXV_VHUB_AMOUNT_MAX)
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE, "vhub_swap: amount must be 1..2^59"));
    const vhub_ccy_t *A = &hub->h.ccy[from_slot], *B = &hub->h.ccy[to_slot];
    vhub_swap_t sw;
    memset(&sw, 0, sizeof sw);
    sw.from_ccy = from_slot;
    sw.to_ccy = to_slot;
    sw.sender = sender;
    sw.receiver = receiver;
    sw.amount = amount;
    switch (vhub_swap(&hub->h, &sw, tick)) {
    case VHUB_OK: break;
    case VHUB_ERR_INEXACT:
        LEAVE(hub, zxvi_fail(ZXV_E_RANGE,
                             "vhub_swap: %llu is not exact under the pegs (%llu/%llu, %llu/%llu); "
                             "zxv_vhub_quote gives the largest exact amount",
                             (unsigned long long) amount, (unsigned long long) A->num,
                             (unsigned long long) A->den, (unsigned long long) B->num,
                             (unsigned long long) B->den));
    case VHUB_REFUSED:
        LEAVE(hub, zxvi_fail(ZXV_E_LIMIT, "vhub_swap: a limit rule refused this swap"));
    case VHUB_WAIT:
        LEAVE(hub, zxvi_fail(ZXV_E_STATE,
                             "vhub_swap: slot %u has no rule; missing rules fail closed",
                             (unsigned) (A->n_rules ? to_slot : from_slot)));
    case VHUB_ERR_LEDGER:
        if (hub->h.last_ledger == PAY_ERR_FUNDS) {
            const char *who = eq(hub, sender) < (int64_t) amount        ? "the sender"
                              : eq(hub, A->vfloat) < (int64_t) sw.vbills ? "the sending V-Bill float"
                                                                          : "the receiving pool";
            LEAVE(hub, zxvi_fail(ZXV_E_FUNDS, "vhub_swap: %s would go below zero", who));
        }
        LEAVE(hub, pay_to_zxv(hub, hub->h.last_ledger, "vhub_swap"));
    default:
        /* ESCALATE / FORBIDDEN / HALTED come only from rules this ABI does
         * not register; ARG was ruled out above. */
        LEAVE(hub, zxvi_fail(ZXV_E_INTERNAL, "vhub_swap: unexpected kernel status"));
    }
    if (!pay_ledger_check(&hub->L))
        LEAVE(hub, poison(hub, "vhub: the hub ledger broke an accounting invariant"));
    if (out_vbills) *out_vbills = sw.vbills;
    if (out_received) *out_received = sw.out;
    LEAVE(hub, zxvi_ok());
}

int32_t zxv_vhub_check(const zxv_cap *cap, zxv_vhub *hub, uint64_t *out_postings)
{
    ENTER(hub, ZXV_CAP_LEDGER_READ, "vhub_check");
    if (!pay_ledger_check(&hub->L))
        LEAVE(hub, poison(hub, "vhub: the hub ledger fails A = L + E or a per-asset balance"));
    if (!pay_ledger_verify_chain(&hub->L))
        LEAVE(hub, poison(hub, "vhub: the hub ledger's posting chain does not verify"));
    if (out_postings) *out_postings = hub->L.seq;
    LEAVE(hub, zxvi_ok());
}
