/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vderiv.c — V-Calls and V-Perps (vderiv.h V0-V6) on a real pay_ledger:
 * the signed jurisdiction switch, index order, call payoffs below, between
 * and above the strike and cap, escrow at zero after settlement, both
 * signatures, perp moves inside and beyond the band, auto-close, voluntary
 * close, conservation, fee exactness, and the check catching tampering. The
 * signature hook is a stand-in (SHA-256 keyed by the signer id); production
 * passes ML-DSA verification. Integers only: no floating point. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "vderiv.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

#define GATE  77u
#define JURIS 0x0050534Eu /* an opaque test code */
#define NU    6
#define FUND  1000000000ull

static pay_ledger_t L;
static vderiv_t S;
static vderiv_cfg_t C;
static uint16_t vno, vbl;
static uint32_t viss, esc, escL, escS, rsv, other;
static uint32_t u[NU];
static uint64_t nfund;

static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[160];
    memset(b, 0, sizeof b);
    b[0] = (uint8_t) who;
    b[1] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static int verifies;
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    verifies++;
    if (sl != 32 || n > 150) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, ca};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&L, &r, &rc);
}
static pay_status_t fund(uint32_t to, uint64_t amt)
{
    return raw(to, (int64_t) amt, 0, viss, 0, (int64_t) amt);
}
static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}
static uint64_t bal(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}
/* every Vino unit outside the issuer: users, escrows and the reserve */
static uint64_t total(void)
{
    uint64_t t = bal(esc) + bal(escL) + bal(escS) + bal(rsv);
    for (int i = 0; i < NU; i++) t += bal(u[i]);
    return t;
}

static void ledger(void)
{
    nfund = 0;
    pay_ledger_init(&L, NULL);
    pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    viss = open_acct(vno, 1, PAY_ACCT_ISSUER);
    esc = open_acct(vno, 2, 0);
    escL = open_acct(vno, 3, 0);
    escS = open_acct(vno, 4, 0);
    rsv = open_acct(vno, 5, 0);
    other = open_acct(vbl, 6, 0);
    for (int i = 0; i < NU; i++) {
        u[i] = open_acct(vno, 40 + (uint32_t) i, 0); /* owner id == signing key */
        fund(u[i], FUND);
    }
}

static void cfg(void)
{
    vderiv_cfg_default(&C);
    C.vino = vno;
    C.escrow = esc;
    C.esc_long = escL;
    C.esc_short = escS;
    C.reserve = rsv;
    C.gateway = GATE;
    C.juris = JURIS;
    C.first_epoch = 10;
    C.verify = verify;
    for (int i = 0; i < 32; i++) C.seed[i] = (uint8_t) (i * 5 + 3);
}

static vderiv_status_t sw(uint32_t juris, bool en, uint64_t epoch, uint32_t signer)
{
    uint8_t m[VDERIV_SWITCH_MSG], sig[32];
    vderiv_switch_msg(juris, en, epoch, m);
    sign(signer, m, sizeof m, sig);
    return vderiv_enable(&S, juris, en, epoch, sig, 32);
}

static void setup(void)
{
    ledger();
    cfg();
    vderiv_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
}

static bool inv(void)
{
    return pay_ledger_check(&L) && vderiv_check(&S) == VDERIV_OK;
}

static const pay_journal_t *last(void)
{
    return &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
}

/* keys are 40 + the user index; accounts u[i] */
static vderiv_call_terms_t cterms(int w, int b, uint64_t K, uint64_t Cp, uint64_t n, uint64_t exp,
                                  uint64_t prem)
{
    vderiv_call_terms_t t = {
        S.nonce, 40u + (uint32_t) w, u[w], 40u + (uint32_t) b, u[b], K, Cp, n, exp, prem};
    return t;
}
static vderiv_status_t copen(const vderiv_call_terms_t *t, uint32_t *id)
{
    uint8_t m[VDERIV_CALL_MSG], ws[32], bs[32];
    vderiv_call_msg(t, m);
    sign(t->writer_key, m, sizeof m, ws);
    sign(t->buyer_key, m, sizeof m, bs);
    return vderiv_call_open(&S, t, ws, 32, bs, 32, 5, id);
}
static vderiv_perp_terms_t pterms(int l, int s, uint64_t n, uint64_t band, uint64_t ll, uint64_t ls)
{
    vderiv_perp_terms_t t = {
        S.nonce, 40u + (uint32_t) l, u[l], 40u + (uint32_t) s, u[s], n, band, ll, ls};
    return t;
}
static vderiv_status_t perp_open(const vderiv_perp_terms_t *t, uint32_t *id)
{
    uint8_t m[VDERIV_PERP_MSG], ls[32], ss[32];
    vderiv_perp_msg(t, m);
    sign(t->long_key, m, sizeof m, ls);
    sign(t->short_key, m, sizeof m, ss);
    return vderiv_perp_open(&S, t, ls, 32, ss, 32, 6, id);
}
static vderiv_status_t perp_close(uint32_t id, uint8_t side, uint32_t signer, uint64_t *out)
{
    uint8_t m[VDERIV_CLOSE_MSG], sig[32];
    vderiv_close_msg(S.perp[id].t.nonce, side, m);
    sign(signer, m, sizeof m, sig);
    return vderiv_perp_close(&S, id, side, sig, 32, 9, out);
}
static vderiv_status_t push(uint64_t qty)
{
    return vderiv_index_push(&S, S.n_pushed ? S.last_epoch + 1 : C.first_epoch, 7, qty, 8, NULL);
}

static void test_init(void)
{
    vderiv_cfg_t d;
    memset(&d, 0x55, sizeof d);
    vderiv_cfg_default(&d);
    CHECK(d.escrow == VDERIV_NONE && d.esc_long == VDERIV_NONE && d.esc_short == VDERIV_NONE &&
              d.reserve == VDERIV_NONE && d.first_epoch == 1 && d.verify == NULL && d.juris == 0 &&
              d.gateway == 0 && d.vino == 0 && d.vctx == NULL && d.seed[31] == 0,
          "defaults: no accounts, first epoch 1, no hook");
    vderiv_cfg_default(NULL);

    ledger();
    cfg();
    CHECK(vderiv_init(&S, &L, &C) == VDERIV_OK && !S.enabled && S.L == &L && S.nonce == 0 &&
              S.n_pushed == 0 && S.cfg.escrow == esc && S.seq == 0,
          "init: switch OFF");
    CHECK(vderiv_check(&S) == VDERIV_OK, "an empty instance checks");
    CHECK(vderiv_init(NULL, &L, &C) == VDERIV_ERR_ARG &&
              vderiv_init(&S, NULL, &C) == VDERIV_ERR_ARG &&
              vderiv_init(&S, &L, NULL) == VDERIV_ERR_ARG,
          "missing arguments");
    vderiv_cfg_t b = C;
    b.verify = NULL;
    CHECK(vderiv_init(&S, &L, &b) == VDERIV_ERR_ARG, "no verify hook");
    b = C;
    b.first_epoch = 0;
    CHECK(vderiv_init(&S, &L, &b) == VDERIV_ERR_ARG, "first epoch 0");
    uint32_t *f[4] = {&b.escrow, &b.esc_long, &b.esc_short, &b.reserve};
    int bad = 0;
    for (int k = 0; k < 4; k++) {
        b = C;
        *f[k] = VDERIV_NONE;
        if (vderiv_init(&S, &L, &b) != VDERIV_ERR_ARG) bad++;
        b = C;
        *f[k] = other; /* another asset */
        if (vderiv_init(&S, &L, &b) != VDERIV_ERR_ARG) bad++;
        b = C;
        *f[k] = viss; /* an issuer */
        if (vderiv_init(&S, &L, &b) != VDERIV_ERR_ARG) bad++;
        for (int j = 0; j < 4; j++) {
            if (j == k) continue;
            b = C;
            *f[k] = j == 0 ? C.escrow : j == 1 ? C.esc_long : j == 2 ? C.esc_short : C.reserve;
            if (vderiv_init(&S, &L, &b) != VDERIV_ERR_ARG) bad++;
        }
    }
    CHECK(bad == 0, "every account: missing, wrong asset, issuer, or a duplicate is refused");
    fund(rsv, 5);
    CHECK(vderiv_init(&S, &L, &C) == VDERIV_OK, "a reserve that already holds fees is fine");
    fund(escS, 1);
    CHECK(vderiv_init(&S, &L, &C) == VDERIV_ERR_STATE, "a short escrow that is not empty");
    ledger();
    fund(escL, 1);
    CHECK(vderiv_init(&S, &L, &C) == VDERIV_ERR_STATE, "a long escrow that is not empty");
    ledger();
    fund(esc, 1);
    CHECK(vderiv_init(&S, &L, &C) == VDERIV_ERR_STATE, "a call escrow that is not empty");
}

static void test_switch(void)
{
    ledger();
    cfg();
    vderiv_init(&S, &L, &C);
    push(100);
    vderiv_call_terms_t ct = cterms(0, 1, 100, 150, 10, 20, 1000);
    uint64_t seq = L.seq;
    CHECK(copen(&ct, NULL) == VDERIV_ERR_DISABLED && L.seq == seq && S.nonce == 0,
          "OFF by default: a call does not open");
    vderiv_perp_terms_t pt = pterms(0, 1, 10, 5, 50, 50);
    CHECK(perp_open(&pt, NULL) == VDERIV_ERR_DISABLED && L.seq == seq, "nor does a perp");

    uint8_t m[VDERIV_SWITCH_MSG], sig[32];
    vderiv_switch_msg(JURIS, true, 1, m);
    CHECK(m[0] == 'z' && m[15] == '1' && m[16] == 0x4E && m[17] == 0x53 && m[18] == 0x50 &&
              m[19] == 0 && m[20] == 1 && m[21] == 1 && m[28] == 0,
          "switch message layout");
    vderiv_switch_msg(JURIS, false, 0x0102030405060708ull, m);
    CHECK(m[20] == 0 && m[21] == 8 && m[28] == 1, "switch message: flag and epoch LE");
    CHECK(sw(JURIS + 1, true, 1, GATE) == VDERIV_ERR_JURIS, "another jurisdiction");
    CHECK(sw(JURIS, true, 1, GATE + 1) == VDERIV_ERR_SIG && !S.enabled, "not the gateway key");
    CHECK(vderiv_enable(&S, JURIS, true, 1, NULL, 32) == VDERIV_ERR_ARG &&
              vderiv_enable(NULL, JURIS, true, 1, sig, 32) == VDERIV_ERR_ARG,
          "missing arguments");
    vderiv_switch_msg(JURIS, true, 1, m);
    sign(GATE, m, sizeof m, sig);
    CHECK(vderiv_enable(&S, JURIS, true, 2, sig, 32) == VDERIV_ERR_SIG,
          "a signature for epoch 1 does not enable epoch 2");
    CHECK(vderiv_enable(&S, JURIS, true, 1, sig, 31) == VDERIV_ERR_SIG, "the length is passed");
    CHECK(sw(JURIS, true, 0, GATE) == VDERIV_ERR_EPOCH, "epoch 0 is not above the start");
    CHECK(sw(JURIS, true, 3, GATE) == VDERIV_OK && S.enabled && S.switch_epoch == 3, "ON");
    CHECK(sw(JURIS, false, 3, GATE) == VDERIV_ERR_EPOCH && S.enabled, "an old epoch is replayed");
    CHECK(copen(&ct, NULL) == VDERIV_OK && S.nonce == 1 && inv(), "ON: the call opens");
    CHECK(sw(JURIS, false, 4, GATE) == VDERIV_OK && !S.enabled && S.switch_epoch == 4, "OFF again");
    ct = cterms(0, 1, 100, 150, 10, 20, 1000);
    CHECK(copen(&ct, NULL) == VDERIV_ERR_DISABLED, "OFF refuses opening");
    for (int e = 0; e < 10; e++) push(100 + (uint64_t) e * 10);
    vderiv_settle_t r;
    CHECK(S.call[0].state == VDERIV_FIXED && vderiv_call_settle(&S, 0, 1, &r) == VDERIV_OK &&
              r.payoff == 500 && inv(),
          "OFF still pushes the index and settles: nothing is stuck");
}

static void test_index(void)
{
    setup();
    uint64_t I = 99;
    CHECK(!vderiv_index_at(&S, 10, &I) && I == 99, "nothing pushed");
    vderiv_push_t o;
    memset(&o, 0x33, sizeof o);
    CHECK(vderiv_index_push(&S, 9, 1, 5, 1, &o) == VDERIV_ERR_EPOCH && o.moved == 0 && o.fees == 0,
          "the first push must be first_epoch (10), not 9");
    CHECK(vderiv_index_push(&S, 11, 1, 5, 1, NULL) == VDERIV_ERR_EPOCH, "nor 11");
    CHECK(vderiv_index_push(NULL, 10, 1, 5, 1, NULL) == VDERIV_ERR_ARG, "no instance");
    CHECK(vderiv_index_push(&S, 10, 300, 5, 1, &o) == VDERIV_OK && S.n_pushed == 1 &&
              S.last_epoch == 10 && S.ring[0].price == 300 && S.ring[0].qty == 5 &&
              S.ring[0].epoch == 10 && o.moved == 0 && o.fixed == 0 && o.stopped == 0,
          "epoch 10 pushed");
    CHECK(vderiv_index_push(&S, 10, 1, 6, 1, NULL) == VDERIV_ERR_EPOCH && S.ring[0].qty == 5,
          "a duplicate is refused");
    CHECK(vderiv_index_push(&S, 12, 1, 6, 1, NULL) == VDERIV_ERR_EPOCH, "a gap is refused");
    CHECK(vderiv_index_push(&S, 11, 1, 6, 1, NULL) == VDERIV_OK, "11 follows 10");
    CHECK(vderiv_index_push(&S, 10, 1, 6, 1, NULL) == VDERIV_ERR_EPOCH, "going back is refused");
    CHECK(vderiv_index_at(&S, 10, &I) && I == 5 && vderiv_index_at(&S, 11, &I) && I == 6 &&
              !vderiv_index_at(&S, 12, &I) && !vderiv_index_at(&S, 9, &I) &&
              !vderiv_index_at(&S, 10, NULL) && !vderiv_index_at(NULL, 10, &I),
          "index_at inside the window only");
    for (uint64_t e = 12; e < 12 + VDERIV_RING; e++) vderiv_index_push(&S, e, e, e * 3, 1, NULL);
    CHECK(S.n_pushed == 2 + VDERIV_RING && S.last_epoch == 11 + VDERIV_RING, "the ring wrapped");
    CHECK(!vderiv_index_at(&S, 11, &I) && vderiv_index_at(&S, 12, &I) && I == 36 &&
              vderiv_index_at(&S, 11 + VDERIV_RING, &I) && I == (11 + VDERIV_RING) * 3,
          "the oldest epochs left the ring; the window is VDERIV_RING");
    S.last_epoch = UINT64_MAX;
    CHECK(vderiv_index_push(&S, 0, 1, 1, 1, NULL) == VDERIV_ERR_EPOCH, "the epoch counter ends");
}

static void test_payoff(void)
{
    uint64_t p = 7;
    CHECK(!vderiv_call_payoff(100, 100, 1, 200, &p) && p == 7 &&
              !vderiv_call_payoff(100, 99, 1, 200, &p) && !vderiv_call_payoff(1, 2, 1, 2, NULL),
          "cap <= strike is refused");
    CHECK(vderiv_call_payoff(100, 150, 1000, 90, &p) && p == 0, "below K: 0");
    CHECK(vderiv_call_payoff(100, 150, 1000, 100, &p) && p == 0, "at K: 0");
    CHECK(vderiv_call_payoff(100, 150, 1000, 101, &p) && p == 1000, "K + 1: n");
    CHECK(vderiv_call_payoff(100, 150, 1000, 149, &p) && p == 49000, "C - 1");
    CHECK(vderiv_call_payoff(100, 150, 1000, 150, &p) && p == 50000, "at C");
    CHECK(vderiv_call_payoff(100, 150, 1000, 151, &p) && p == 50000 &&
              vderiv_call_payoff(100, 150, 1000, UINT64_MAX, &p) && p == 50000,
          "above C: capped");
    CHECK(!vderiv_call_payoff(0, UINT64_MAX, 2, UINT64_MAX, &p) &&
              vderiv_call_payoff(0, UINT64_MAX, 1, UINT64_MAX, &p) && p == UINT64_MAX,
          "overflow refused");

    uint64_t v = 7;
    int d = 5;
    CHECK(vderiv_perp_variation(10, 50, 100, 130, &v, &d) && v == 300 && d == 1, "up 30: long");
    CHECK(vderiv_perp_variation(10, 50, 130, 100, &v, &d) && v == 300 && d == -1, "down: short");
    CHECK(vderiv_perp_variation(10, 50, 100, 151, &v, &d) && v == 500 && d == 1 &&
              vderiv_perp_variation(10, 50, 100, 150, &v, &d) && v == 500 &&
              vderiv_perp_variation(10, 50, 100, 149, &v, &d) && v == 490,
          "clamped to the band");
    CHECK(vderiv_perp_variation(10, 50, 200, 0, &v, &d) && v == 500 && d == -1, "down, clamped");
    CHECK(vderiv_perp_variation(10, 50, 100, 100, &v, &d) && v == 0 && d == 0, "no move");
    CHECK(vderiv_perp_variation(0, 50, 100, 120, &v, &d) && v == 0 && d == 0, "size 0: no move");
    CHECK(!vderiv_perp_variation(UINT64_MAX, 2, 0, 2, &v, &d) &&
              !vderiv_perp_variation(1, 1, 0, 1, NULL, &d) &&
              !vderiv_perp_variation(1, 1, 0, 1, &v, NULL),
          "overflow and missing outputs");
    CHECK(pay_assure_fee(2000000) == 1777 && pay_assure_fee(20000) == 17 &&
              pay_assure_fee(50000) == 44 && pay_assure_fee(1124) == 0,
          "the fee figures this test relies on");
}

static void test_call(void)
{
    setup();
    uint64_t T0 = total();
    vderiv_call_terms_t t = cterms(0, 1, 100, 150, 1000, 12, 2000000);
    uint32_t id = 99;
    uint8_t m[VDERIV_CALL_MSG], ws[32], bs[32];
    vderiv_call_msg(&t, m);
    CHECK(m[0] == 'z' && m[11] == 'c' && m[12] == 'l' && m[16] == 0 && m[24] == 40 &&
              m[28] == u[0] && m[32] == 41 && m[36] == u[1] && m[40] == 100 && m[48] == 150 &&
              m[56] == 0xE8 && m[57] == 3 && m[64] == 12 && m[72] == 0x80 && m[73] == 0x84 &&
              m[74] == 0x1E,
          "call message layout");
    {
        vderiv_call_terms_t z = t;
        z.nonce = 0x0102030405060708ull;
        vderiv_call_msg(&z, m);
        CHECK(m[15] == '1' && m[16] == 8 && m[23] == 1 && m[24] == 40,
              "call message: the nonce follows the tag");
        vderiv_call_msg(&t, m);
    }
    sign(40, m, sizeof m, ws);
    sign(41, m, sizeof m, bs);
    uint64_t seq = L.seq;
    CHECK(vderiv_call_open(&S, &t, ws, 32, ws, 32, 1, &id) == VDERIV_ERR_SIG && id == 99,
          "the writer's signature twice: the buyer did not sign");
    CHECK(vderiv_call_open(&S, &t, bs, 32, bs, 32, 1, &id) == VDERIV_ERR_SIG,
          "the buyer's twice: the writer did not sign");
    CHECK(vderiv_call_open(&S, &t, ws, 32, bs, 31, 1, &id) == VDERIV_ERR_SIG &&
              vderiv_call_open(&S, &t, ws, 31, bs, 32, 1, &id) == VDERIV_ERR_SIG,
          "each length is passed");
    CHECK(vderiv_call_open(&S, &t, NULL, 32, bs, 32, 1, &id) == VDERIV_ERR_ARG &&
              vderiv_call_open(&S, &t, ws, 32, NULL, 32, 1, &id) == VDERIV_ERR_ARG &&
              vderiv_call_open(&S, NULL, ws, 32, bs, 32, 1, &id) == VDERIV_ERR_ARG &&
              vderiv_call_open(NULL, &t, ws, 32, bs, 32, 1, &id) == VDERIV_ERR_ARG,
          "missing arguments");
    vderiv_call_terms_t x = t;
    x.premium = 2000001;
    CHECK(vderiv_call_open(&S, &x, ws, 32, bs, 32, 1, &id) == VDERIV_ERR_SIG,
          "a changed premium is not what was signed");
    CHECK(L.seq == seq && S.nonce == 0 && inv(), "nothing posted");

    /* term checks */
    x = t, x.size = 0;
    CHECK(copen(&x, NULL) == VDERIV_ERR_ARG, "size 0");
    x = t, x.cap = 100;
    CHECK(copen(&x, NULL) == VDERIV_ERR_ARG, "cap == strike");
    x = t, x.premium = VDERIV_LINE_MAX + 1;
    CHECK(copen(&x, NULL) == VDERIV_ERR_ARG, "premium past the line cap");
    x = t, x.premium = VDERIV_LINE_MAX;
    CHECK(copen(&x, NULL) == VDERIV_ERR_FUNDS, "a premium of exactly 2^59 is allowed (unfunded)");
    x = t, x.strike = 0, x.cap = VDERIV_LINE_MAX, x.size = 1;
    x.premium = 0;
    CHECK(copen(&x, NULL) == VDERIV_ERR_FUNDS && S.nonce == 0,
          "a lock of exactly 2^59 is within the cap; the writer cannot fund it");
    x.cap = VDERIV_LINE_MAX + 1;
    CHECK(copen(&x, NULL) == VDERIV_ERR_OVERFLOW, "a lock past 2^59");
    x = t, x.size = UINT64_MAX;
    CHECK(copen(&x, NULL) == VDERIV_ERR_OVERFLOW, "n * (C - K) overflows");
    x = t, x.expiry = 9;
    CHECK(copen(&x, NULL) == VDERIV_ERR_EPOCH, "expiry before the first epoch");
    x = t, x.expiry = 10;
    CHECK(copen(&x, &id) == VDERIV_OK && id == 0 && S.nonce == 1 && inv(),
          "expiry at the first epoch is fine before any push");
    x = t, x.nonce = 0;
    CHECK(copen(&x, NULL) == VDERIV_ERR_NONCE, "a used nonce");
    x = t, x.nonce = 2;
    CHECK(copen(&x, NULL) == VDERIV_ERR_NONCE, "a future nonce");
    t = cterms(0, 1, 100, 150, 1000, 12, 2000000);
    x = t, x.buyer = u[0];
    CHECK(copen(&x, NULL) == VDERIV_ERR_ARG, "writer == buyer");
    x = t, x.buyer_key = x.writer_key;
    CHECK(copen(&x, NULL) == VDERIV_ERR_ARG, "one key for both sides");
    uint32_t bads[6] = {esc, escL, escS, rsv, other, 500};
    int bad = 0;
    for (int k = 0; k < 6; k++) {
        x = t, x.buyer = bads[k];
        if (copen(&x, NULL) != VDERIV_ERR_ARG) bad++;
        x = t, x.writer = bads[k];
        if (copen(&x, NULL) != VDERIV_ERR_ARG) bad++;
    }
    CHECK(bad == 0, "the module's accounts, another asset or no account are no party");

    push(100); /* epoch 10: the first call is fixed at 100 */
    CHECK(S.call[0].state == VDERIV_FIXED && S.call[0].fixing == 100, "fixed at expiry");
    x = t, x.expiry = 10;
    CHECK(copen(&x, NULL) == VDERIV_ERR_EPOCH, "an expiry already pushed");
    vderiv_settle_t r;
    memset(&r, 0x44, sizeof r);
    uint64_t w0 = bal(u[0]);
    CHECK(vderiv_call_settle(&S, 0, 2, &r) == VDERIV_OK && r.payoff == 0 && r.fee == 0 &&
              r.to_buyer == 0 && r.to_writer == 50000 && bal(u[0]) == w0 + 50000 && bal(esc) == 0 &&
              inv(),
          "at K: the writer gets the whole lock back");
    CHECK(vderiv_call_settle(&S, 0, 2, &r) == VDERIV_ERR_STATE && r.payoff == 0,
          "settled once only");

    /* below K, between K and C, above C: three calls expiring at 12 */
    uint32_t id1, id2, id3;
    uint64_t W = bal(u[0]), B = bal(u[1]), R = bal(rsv);
    seq = L.seq;
    t = cterms(0, 1, 100, 150, 1000, 12, 2000000);
    CHECK(copen(&t, &id1) == VDERIV_OK && id1 == 0 && L.seq == seq + 1, "opened in one posting");
    CHECK(bal(u[1]) == B - 2000000 && bal(u[0]) == W + 2000000 - 1777 - 50000 &&
              bal(rsv) == R + 1777 && bal(esc) == 50000 && last()->n_lines == 5 &&
              last()->kind == PAY_KIND_TRADE && !strncmp(last()->e2e, "VDERIV-", 7),
          "premium less its fee to the writer, fee to the reserve, lock to escrow");
    t = cterms(2, 3, 100, 150, 1000, 12, 0);
    CHECK(copen(&t, &id2) == VDERIV_OK && id2 == 1, "a zero premium is allowed");
    t = cterms(4, 5, 100, 150, 1000, 13, 1124);
    CHECK(copen(&t, &id3) == VDERIV_OK && id3 == 2 && bal(esc) == 150000 && inv(),
          "a premium under one fee unit pays no fee");
    CHECK(vderiv_call_settle(&S, id1, 3, &r) == VDERIV_ERR_STATE, "not before expiry");
    push(90); /* epoch 11 */
    CHECK(S.call[id1].state == VDERIV_OPEN &&
              vderiv_call_settle(&S, id1, 3, &r) == VDERIV_ERR_STATE,
          "epoch 11 is not the expiry");
    vderiv_push_t o;
    CHECK(vderiv_index_push(&S, 12, 7, 120, 4, &o) == VDERIV_OK && o.fixed == 2 &&
              S.call[id1].state == VDERIV_FIXED && S.call[id3].state == VDERIV_OPEN,
          "epoch 12 fixes the two calls expiring there");
    W = bal(u[0]), B = bal(u[1]), R = bal(rsv);
    CHECK(vderiv_call_settle(&S, id1, 5, &r) == VDERIV_OK && r.payoff == 20000 && r.fee == 17 &&
              r.to_buyer == 19983 && r.to_writer == 30000,
          "I = 120: payoff 1000 * 20, fee 17");
    CHECK(bal(u[1]) == B + 19983 && bal(u[0]) == W + 30000 && bal(rsv) == R + 17 &&
              bal(esc) == 100000 && last()->n_lines == 4 && inv(),
          "one posting; the sum is exact");
    CHECK(vderiv_call_settle(&S, id2, 5, NULL) == VDERIV_OK && bal(u[3]) == FUND + 20000 - 17 &&
              bal(u[2]) == FUND - 20000,
          "the second call: same payoff");
    push(500); /* 13: above the cap */
    CHECK(vderiv_call_settle(&S, id3, 5, &r) == VDERIV_OK && r.payoff == 50000 && r.fee == 44 &&
              r.to_writer == 0 && bal(esc) == 0 && bal(u[5]) == FUND - 1124 + 50000 - 44 &&
              bal(u[4]) == FUND + 1124 - 50000 && inv(),
          "I = 500: capped at n * (C - K); escrow ends at zero");
    CHECK(total() == T0, "nothing created or lost");
    CHECK(S.fees_total == 1777 + 1777 + 17 + 17 + 44 && bal(rsv) == S.fees_total,
          "every call fee is counted and sits in the reserve");
    CHECK(vderiv_call_settle(&S, VDERIV_MAX_CALLS, 5, &r) == VDERIV_ERR_ARG &&
              vderiv_call_settle(NULL, 0, 5, &r) == VDERIV_ERR_ARG,
          "bad settle arguments");

    /* the table fills; a refused posting leaves the nonce */
    for (uint32_t i = 0; i < VDERIV_MAX_CALLS; i++) {
        t = cterms(0, 1, 1, 2, 1, 100, 0);
        copen(&t, NULL);
    }
    t = cterms(0, 1, 1, 2, 1, 100, 0);
    CHECK(S.nonce == 4 + VDERIV_MAX_CALLS && copen(&t, NULL) == VDERIV_ERR_FULL && inv(), "full");
    setup();
    t = cterms(0, 1, 100, 150, FUND, 12, 0);
    CHECK(copen(&t, NULL) == VDERIV_ERR_FUNDS && S.nonce == 0 && S.call[0].state == VDERIV_FREE &&
              S.last_ledger == PAY_ERR_FUNDS,
          "a writer who cannot lock: FUNDS, nothing kept");
    t = cterms(0, 1, 100, 150, 1, 12, FUND + 1);
    CHECK(copen(&t, NULL) == VDERIV_ERR_FUNDS, "a buyer who cannot pay");
    pay_ledger_set_flags(&L, esc, PAY_ACCT_FROZEN);
    t = cterms(0, 1, 100, 150, 1, 10, 0);
    CHECK(copen(&t, NULL) == VDERIV_OK, "a frozen escrow still receives");
    push(140);
    CHECK(vderiv_call_settle(&S, 0, 1, &r) == VDERIV_ERR_LEDGER &&
              S.last_ledger == PAY_ERR_POLICY && S.call[0].state == VDERIV_FIXED && r.payoff == 0,
          "a refused settlement changes nothing");
    pay_ledger_set_flags(&L, esc, 0);
    CHECK(vderiv_call_settle(&S, 0, 1, &r) == VDERIV_OK && r.payoff == 40 && inv(), "then settles");
}

static void test_perp(void)
{
    setup();
    uint64_t T0 = total();
    vderiv_perp_terms_t t = pterms(0, 1, 100000, 50, 20000000, 20000000);
    uint8_t m[VDERIV_PERP_MSG];
    vderiv_perp_msg(&t, m);
    CHECK(m[0] == 'z' && m[11] == 'p' && m[12] == 'p' && m[24] == 40 && m[28] == u[0] &&
              m[32] == 41 && m[36] == u[1] && m[40] == 0xA0 && m[41] == 0x86 && m[42] == 1 &&
              m[48] == 50 && m[56] == 0 && m[57] == 0x2D && m[58] == 0x31 && m[59] == 1 &&
              m[64] == 0 && m[67] == 1,
          "perp message layout");
    {
        vderiv_perp_terms_t z = t;
        z.nonce = 0x0102030405060708ull;
        vderiv_perp_msg(&z, m);
        CHECK(m[15] == '1' && m[16] == 8 && m[23] == 1 && m[24] == 40,
              "perp message: the nonce follows the tag");
        vderiv_perp_msg(&t, m);
    }
    CHECK(perp_open(&t, NULL) == VDERIV_ERR_EPOCH, "no index yet: no reference");
    push(100);
    vderiv_perp_terms_t x = t;
    x.band = 0;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_ARG, "band 0");
    x = t, x.size = 0;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_ARG, "size 0");
    x = t, x.lock_long = 4999999;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_ARG, "long lock below n * band");
    x = t, x.lock_short = 4999999;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_ARG, "short lock below n * band");
    x = t, x.size = UINT64_MAX;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_OVERFLOW, "n * band overflows");
    x = t, x.lock_long = UINT64_MAX;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_OVERFLOW, "locks overflow");
    x = t, x.lock_long = VDERIV_LINE_MAX - 20000000, x.size = 1;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_FUNDS, "two locks of exactly 2^59 fit the cap");
    x.lock_long++;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_OVERFLOW, "one more is past it");
    x = t, x.short_acct = u[0];
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_ARG, "long == short");
    x = t, x.nonce = 1;
    CHECK(perp_open(&x, NULL) == VDERIV_ERR_NONCE, "wrong nonce");
    uint8_t ls[32], ss[32];
    sign(40, m, sizeof m, ls);
    sign(41, m, sizeof m, ss);
    CHECK(vderiv_perp_open(&S, &t, ls, 32, ls, 32, 1, NULL) == VDERIV_ERR_SIG &&
              vderiv_perp_open(&S, &t, ss, 32, ss, 32, 1, NULL) == VDERIV_ERR_SIG &&
              vderiv_perp_open(&S, &t, NULL, 32, ss, 32, 1, NULL) == VDERIV_ERR_ARG &&
              vderiv_perp_open(&S, &t, ls, 32, NULL, 32, 1, NULL) == VDERIV_ERR_ARG &&
              vderiv_perp_open(&S, NULL, ls, 32, ss, 32, 1, NULL) == VDERIV_ERR_ARG &&
              vderiv_perp_open(NULL, &t, ls, 32, ss, 32, 1, NULL) == VDERIV_ERR_ARG,
          "both signatures required");
    uint32_t id = 99;
    uint64_t seq = L.seq;
    CHECK(vderiv_perp_open(&S, &t, ls, 32, ss, 32, 1, &id) == VDERIV_OK && id == 0 &&
              L.seq == seq + 1 && bal(escL) == 20000000 && bal(escS) == 20000000 &&
              bal(u[0]) == FUND - 20000000 && bal(u[1]) == FUND - 20000000 &&
              S.perp[0].ref == 100 && S.perp[0].opened == 10 && S.nonce == 1 && inv(),
          "both locks in one posting; reference 100");

    /* a second, opposite perp between u2 (long) and u3 (short), smaller */
    uint32_t id2;
    vderiv_perp_terms_t t2 = pterms(2, 3, 10, 50, 600, 2000);
    CHECK(perp_open(&t2, &id2) == VDERIV_OK && id2 == 1 && inv(), "second perp");

    vderiv_push_t o;
    uint64_t R = bal(rsv);
    seq = L.seq;
    CHECK(vderiv_index_push(&S, 11, 7, 130, 1, &o) == VDERIV_OK && o.moved == 2 && o.stopped == 0 &&
              o.fees == 2666 && L.seq == seq + 1 && last()->n_lines == 3,
          "up 30 inside the band: both perps settle in ONE posting");
    CHECK(S.perp[0].bal[0] == 22997334 && S.perp[0].bal[1] == 17000000 && S.perp[0].fees == 2666 &&
              S.perp[0].ref == 130 && S.perp[1].bal[0] == 900 && S.perp[1].bal[1] == 1700 &&
              S.perp[1].fees == 0 && bal(rsv) == R + 2666 && inv(),
          "long gains 3,000,000 less fee 2666; the small perp pays no fee on 300");
    CHECK(vderiv_index_push(&S, 12, 7, 50, 1, &o) == VDERIV_OK && o.moved == 2 && o.fees == 4444 &&
              o.stopped == 1,
          "down 80, clamped to 50: short wins 5,000,000 less 4444");
    CHECK(S.perp[0].bal[0] == 17997334 && S.perp[0].bal[1] == 21995556 &&
              S.perp[0].state == VDERIV_OPEN && S.perp[1].bal[0] == 400 &&
              S.perp[1].bal[1] == 2200 && S.perp[1].state == VDERIV_STOPPED &&
              S.perp[1].stop_epoch == 12 && inv(),
          "the small perp's long fell below n * band: it stops at epoch 12");
    CHECK(vderiv_index_push(&S, 13, 7, 50, 1, &o) == VDERIV_OK && o.moved == 0 && o.fees == 0 &&
              L.seq == seq + 2,
          "no move: no posting");
    push(0);
    CHECK(S.perp[0].bal[0] == 12997334 && S.perp[1].bal[0] == 400 && S.perp[1].ref == 50,
          "a stopped perp no longer moves");
    push(0);
    CHECK(S.perp[0].bal[0] == 12997334 && S.perp[0].ref == 0, "index at 0, no move");
    push(10);
    push(0);
    CHECK(S.perp[0].bal[0] == 12997334 - 1000000 + 999112 && S.perp[0].state == VDERIV_OPEN,
          "up 10 then down 10: the fee on each side");
    uint64_t lb = S.perp[0].bal[0];
    push(200); /* +50 for long */
    lb += 5000000 - 4444;
    CHECK(S.perp[0].bal[0] == lb, "clamped up");
    for (int k = 0; k < 2; k++) push(150 - 50 * (uint64_t) k);
    CHECK(S.perp[0].state == VDERIV_OPEN && S.perp[0].bal[0] == lb - 10000000 &&
              S.perp[0].bal[0] >= 5000000,
          "two downs: still at least n * band");
    push(50);
    CHECK(S.perp[0].state == VDERIV_STOPPED && S.perp[0].bal[0] == lb - 15000000 &&
              S.perp[0].bal[0] < 5000000 && S.perp[0].stop_epoch == S.last_epoch && inv(),
          "the long's balance fell below n * band: auto-close");

    /* payouts */
    uint64_t got[2] = {9, 9}, a0 = bal(u[0]), a1 = bal(u[1]);
    uint64_t pl = S.perp[0].bal[0], ps = S.perp[0].bal[1];
    CHECK(vderiv_perp_close(&S, 0, VDERIV_LONG, NULL, 0, 1, got) == VDERIV_OK && got[0] == pl &&
              got[1] == ps && bal(u[0]) == a0 + pl && bal(u[1]) == a1 + ps &&
              S.perp[0].state == VDERIV_FREE && inv(),
          "a stopped perp pays both sides without a signature");
    CHECK(vderiv_perp_close(&S, 0, VDERIV_LONG, NULL, 0, 1, got) == VDERIV_ERR_STATE,
          "paid once only");
    CHECK(vderiv_perp_close(&S, 1, 2, NULL, 0, 1, NULL) == VDERIV_ERR_ARG &&
              vderiv_perp_close(&S, VDERIV_MAX_PERPS, 0, NULL, 0, 1, NULL) == VDERIV_ERR_ARG &&
              vderiv_perp_close(NULL, 1, 0, NULL, 0, 1, NULL) == VDERIV_ERR_ARG,
          "bad close arguments");
    CHECK(vderiv_perp_close(&S, 1, VDERIV_SHORT, NULL, 0, 1, NULL) == VDERIV_OK &&
              bal(u[2]) == FUND - 600 + 400 && bal(u[3]) == FUND - 2000 + 2200 && bal(escL) == 0 &&
              bal(escS) == 0 && inv(),
          "the second perp paid out; both escrows at zero");
    CHECK(total() == T0, "conservation: every unit is with a party, an escrow or the reserve");
    CHECK(S.fees_total == bal(rsv), "every fee is in the reserve");

    /* voluntary close */
    t = pterms(4, 5, 10, 5, 100, 100);
    CHECK(perp_open(&t, &id) == VDERIV_OK && id == 0, "a perp to close");
    CHECK(S.ring[(S.n_pushed - 1) % VDERIV_RING].qty == 50 && S.perp[0].ref == 50, "ref 50");
    push(53);
    CHECK(S.perp[0].bal[0] == 130 && S.perp[0].bal[1] == 70, "short lost 30");
    CHECK(perp_close(0, VDERIV_LONG, 45, got) == VDERIV_ERR_SIG && S.perp[0].state == VDERIV_OPEN,
          "the short's key cannot close as the long");
    CHECK(perp_close(0, VDERIV_SHORT, 44, got) == VDERIV_ERR_SIG, "nor the long's key as the short");
    CHECK(vderiv_perp_close(&S, 0, VDERIV_LONG, NULL, 32, 1, got) == VDERIV_ERR_SIG,
          "an open perp needs a signature");
    uint8_t cm[VDERIV_CLOSE_MSG], cs[32];
    vderiv_close_msg(S.perp[0].t.nonce, VDERIV_LONG, cm);
    CHECK(cm[0] == 'z' && cm[11] == 'c' && cm[12] == 'x' && cm[16] == 2 && cm[17] == 0 &&
              cm[24] == 0,
          "close message layout");
    sign(44, cm, sizeof cm, cs);
    CHECK(vderiv_perp_close(&S, 0, VDERIV_LONG, cs, 31, 1, got) == VDERIV_ERR_SIG,
          "the length is passed");
    pay_ledger_set_flags(&L, escS, PAY_ACCT_FROZEN);
    CHECK(perp_close(0, VDERIV_SHORT, 45, got) == VDERIV_ERR_LEDGER && S.perp[0].state == VDERIV_OPEN,
          "a refused payout changes nothing");
    uint64_t le = S.last_epoch;
    vderiv_push_t po;
    memset(&po, 0x33, sizeof po);
    CHECK(vderiv_index_push(&S, le + 1, 7, 55, 8, &po) == VDERIV_ERR_LEDGER &&
              S.last_ledger == PAY_ERR_POLICY && po.moved == 0 && po.fees == 0 && po.stopped == 0 &&
              po.fixed == 0,
          "the ledger refuses the push; the report is cleared");
    CHECK(S.last_epoch == le && S.perp[0].bal[0] == 130 && S.perp[0].ref == 53,
          "a refused push changes nothing");
    pay_ledger_set_flags(&L, escS, 0);
    CHECK(perp_close(0, VDERIV_SHORT, 45, got) == VDERIV_OK && got[0] == 130 && got[1] == 70 &&
              bal(u[4]) == FUND + 30 && bal(u[5]) == FUND - 30 && inv(),
          "the short closes: both get their balances");
    t = pterms(4, 5, 10, 5, 100, 100);
    CHECK(perp_open(&t, &id) == VDERIV_OK && perp_close(id, VDERIV_LONG, 44, NULL) == VDERIV_OK &&
              total() == T0 && inv(),
          "the long may close too, at once");

    /* the table fills; a reused slot does not take an old close signature */
    for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
        t = pterms(4, 5, 1, 1, 1, 1);
        perp_open(&t, NULL);
    }
    t = pterms(4, 5, 1, 1, 1, 1);
    CHECK(perp_open(&t, NULL) == VDERIV_ERR_FULL, "full");
    uint64_t old = S.perp[3].t.nonce;
    vderiv_close_msg(old, VDERIV_LONG, cm);
    sign(44, cm, sizeof cm, cs);
    CHECK(vderiv_perp_close(&S, 3, VDERIV_LONG, cs, 32, 1, NULL) == VDERIV_OK, "slot 3 closed");
    t = pterms(4, 5, 1, 1, 1, 1);
    CHECK(perp_open(&t, &id) == VDERIV_OK && id == 3 &&
              vderiv_perp_close(&S, 3, VDERIV_LONG, cs, 32, 1, NULL) == VDERIV_ERR_SIG,
          "the old signature does not close the new position in the slot");
    CHECK(inv(), "ledger and positions agree");
    setup();
    push(1);
    t = pterms(0, 1, 1, 1, FUND + 1, 1);
    CHECK(perp_open(&t, NULL) == VDERIV_ERR_FUNDS && S.nonce == 0, "the long cannot fund its lock");
}

static void test_tamper(void)
{
    setup();
    push(100);
    vderiv_call_terms_t c = cterms(0, 1, 100, 150, 10, 20, 1000);
    copen(&c, NULL);
    vderiv_perp_terms_t p = pterms(2, 3, 10, 5, 100, 100);
    perp_open(&p, NULL);
    push(103);
    CHECK(inv(), "clean");
    CHECK(vderiv_check(NULL) == VDERIV_ERR_ARG, "no instance");
    S.call[0].lock++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a call's lock changed");
    S.call[0].lock--;
    S.call[0].state = VDERIV_FREE;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a call dropped");
    S.call[0].state = VDERIV_FIXED;
    CHECK(vderiv_check(&S) == VDERIV_OK, "a fixed call still counts");
    S.call[0].state = VDERIV_OPEN;
    S.perp[0].bal[0]++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a long balance changed");
    S.perp[0].bal[0]--;
    S.perp[0].bal[1]++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a short balance changed");
    S.perp[0].bal[1]--;
    S.perp[0].fees++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the fee record changed");
    S.perp[0].fees--;
    S.perp[0].t.lock_long++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a lock term changed");
    S.perp[0].t.lock_long--;
    S.perp[0].bal[0]++, S.perp[0].bal[1]--;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a unit moved between sides off the ledger");
    S.perp[0].bal[0]--, S.perp[0].bal[1]++;
    S.perp[0].state = VDERIV_STOPPED;
    CHECK(vderiv_check(&S) == VDERIV_OK, "a stopped perp still counts");
    S.perp[0].state = VDERIV_FREE;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a perp dropped");
    S.perp[0].state = VDERIV_OPEN;
    uint64_t sv = S.perp[0].bal[0];
    S.perp[0].bal[0] = UINT64_MAX;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "a huge balance");
    S.perp[0].bal[0] = sv;
    S.perp[5] = S.perp[0];
    S.perp[5].bal[1] = UINT64_MAX;
    S.perp[5].bal[0] = 0;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the short sum overflows");
    S.perp[5].bal[1] = 0;
    S.perp[5].bal[0] = UINT64_MAX;
    S.perp[5].fees = 0;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the long sum overflows");
    S.perp[5].bal[0] = 0;
    S.perp[5].fees = UINT64_MAX;
    S.perp[5].t.lock_long = UINT64_MAX;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the fee and lock sums overflow");
    memset(&S.perp[5], 0, sizeof S.perp[5]);
    S.call[5] = S.call[0];
    S.call[5].lock = UINT64_MAX;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the call sum overflows");
    memset(&S.call[5], 0, sizeof S.call[5]);
    CHECK(vderiv_check(&S) == VDERIV_OK, "restored");
    L.acct[esc].credit = 1;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "escrow CREDIT");
    L.acct[esc].credit = 0;
    L.acct[escS].credit = 1;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "short escrow CREDIT");
    L.acct[escS].credit = 0;
    L.acct[escL].debit++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the long escrow's balance changed");
    L.acct[escL].debit--;
    L.acct[escS].debit--;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the short escrow's balance changed");
    L.acct[escS].debit++;
    /* a balanced change the ledger itself accepts: only V4 sees it */
    L.acct[escS].debit--, L.acct[escS].equity--;
    L.acct[u[0]].debit++, L.acct[u[0]].equity++;
    CHECK(pay_ledger_check(&L) && vderiv_check(&S) == VDERIV_ERR_STATE,
          "a unit taken out of the short escrow on the ledger");
    L.acct[escS].debit++, L.acct[escS].equity++;
    L.acct[u[0]].debit--, L.acct[u[0]].equity--;
    L.acct[escL].debit--, L.acct[escL].equity--;
    L.acct[u[0]].debit++, L.acct[u[0]].equity++;
    CHECK(pay_ledger_check(&L) && vderiv_check(&S) == VDERIV_ERR_STATE,
          "a unit taken out of the long escrow on the ledger");
    L.acct[escL].debit++, L.acct[escL].equity++;
    L.acct[u[0]].debit--, L.acct[u[0]].equity--;
    L.acct[esc].debit--;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "the call escrow's balance changed");
    L.acct[esc].debit++;
    L.acct[u[0]].equity++;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "pay_ledger_check runs too");
    L.acct[u[0]].equity--;
    S.cfg.esc_long = other;
    CHECK(vderiv_check(&S) == VDERIV_ERR_STATE, "an escrow of another asset");
    S.cfg.esc_long = escL;
    CHECK(vderiv_check(&S) == VDERIV_OK, "restored");
}

/* many moving perps in one push: one posting, exact per-perp fees */
/* identifiers, counters, bounds and the stop boundary */
static void test_edges(void)
{
    setup();
    push(100);
    vderiv_call_terms_t c = cterms(0, 1, 100, 150, 10, 20, 1000);
    CHECK(copen(&c, NULL) == VDERIV_OK && S.seq == 1 && S.posted == 1, "one posting counted");
    const pay_journal_t *j = last();
    uint8_t in[41], want[32], d[32];
    char uetr[PAY_UETR_LEN + 1];
    memcpy(in, C.seed, 32);
    in[32] = 'I';
    memset(in + 33, 0, 8);
    sha256(in, sizeof in, want);
    in[32] = 'U';
    sha256(in, sizeof in, d);
    pay_uetr_from_random(d, uetr);
    CHECK(!memcmp(j->idem_key, want, 32) && !strcmp(j->uetr, uetr) && !strcmp(j->e2e, "VDERIV-0"),
          "identifiers are SHA-256(seed || tag || seq)");
    c = cterms(0, 1, 100, 150, 10, 20, 1000);
    CHECK(copen(&c, NULL) == VDERIV_OK && S.seq == 2 && S.posted == 2 &&
              !strcmp(last()->e2e, "VDERIV-1"),
          "the sequence counts up");
    c = cterms(0, 1, 100, 150, FUND, 20, 0);
    CHECK(copen(&c, NULL) == VDERIV_ERR_FUNDS && S.seq == 3 && S.posted == 2,
          "a refused posting uses a sequence number, not a posted count");

    /* the perp escrow cap counts both escrow accounts */
    uint64_t big = (uint64_t) 1 << 58;
    fund(u[0], big);
    fund(u[2], big);
    vderiv_perp_terms_t p = pterms(0, 1, 1, 1, big, 1);
    CHECK(perp_open(&p, NULL) == VDERIV_OK && bal(escL) == big && bal(escS) == 1, "a 2^58 long lock");
    p = pterms(2, 3, 1, 1, big - 1, 1);
    CHECK(perp_open(&p, NULL) == VDERIV_ERR_OVERFLOW, "2^58 + 1 + 2^58 is past 2^59");
    p = pterms(2, 3, 1, 1, big - 2, 1);
    CHECK(perp_open(&p, NULL) == VDERIV_OK && inv(), "exactly 2^59 in the two escrows fits");

    /* exactly n * band left after a move: the perp stays open */
    setup();
    push(100);
    p = pterms(0, 1, 10, 5, 80, 80);
    CHECK(perp_open(&p, NULL) == VDERIV_OK, "perp A");
    push(103);
    CHECK(S.perp[0].bal[1] == 50 && S.perp[0].state == VDERIV_OPEN,
          "the short at exactly n * band stays open");
    p = pterms(2, 3, 10, 5, 80, 80);
    CHECK(perp_open(&p, NULL) == VDERIV_OK, "perp B");
    push(100);
    CHECK(S.perp[1].bal[0] == 50 && S.perp[1].state == VDERIV_OPEN &&
              S.perp[0].state == VDERIV_OPEN && S.perp[0].bal[0] == 80 && inv(),
          "the long at exactly n * band stays open");
}

/* The loops in vderiv_index_push must stop at the table size. What sits
 * straight after each table is made to look like a live slot, so a read one
 * past the end shows up: call[] is followed by perp[0] (its expiry overlays
 * perp[0].t.lock_short) and perp[] by fees_total (its state byte is the low
 * byte of fees_total). The layout this relies on is checked first. */
static void test_bounds(void)
{
    CHECK(offsetof(vderiv_t, perp) == offsetof(vderiv_t, call) + sizeof S.call &&
              offsetof(vderiv_t, fees_total) == offsetof(vderiv_t, perp) + sizeof S.perp &&
              offsetof(vderiv_call_t, state) == 0 && offsetof(vderiv_perp_t, state) == 0 &&
              offsetof(vderiv_call_t, t) + offsetof(vderiv_call_terms_t, expiry) ==
                  offsetof(vderiv_perp_t, t) + offsetof(vderiv_perp_terms_t, lock_short),
          "layout: a slot past each table overlays the next field");
    setup();
    push(100); /* epoch 10 */
    /* perp[0]'s short lock is 12: read as call[32], it expires at 12 */
    vderiv_perp_terms_t p = pterms(0, 1, 1, 1, 1, 12);
    CHECK(perp_open(&p, NULL) == VDERIV_OK && S.perp[0].t.lock_short == 12, "perp in slot 0");
    /* one fee unit: fees_total == 1, so a perp[32] would read as OPEN */
    vderiv_call_terms_t c = cterms(2, 3, 1, 2, 1, 100, 1125);
    CHECK(copen(&c, NULL) == VDERIV_OK && S.fees_total == 1 &&
              (uint8_t) S.fees_total == VDERIV_OPEN,
          "fees_total is 1");
    for (uint32_t i = 1; i < VDERIV_MAX_PERPS; i++) {
        p = pterms(2, 3, 1, 1, 1, 1);
        perp_open(&p, NULL);
    }
    for (uint32_t i = 1; i < VDERIV_MAX_CALLS; i++) {
        c = cterms(2, 3, 1, 2, 1, 100, 0);
        copen(&c, NULL);
    }
    CHECK(S.perp[VDERIV_MAX_PERPS - 1].state == VDERIV_OPEN &&
              S.call[VDERIV_MAX_CALLS - 1].state == VDERIV_OPEN && inv(),
          "both tables full");
    vderiv_push_t o;
    CHECK(vderiv_index_push(&S, 11, 1, 100, 1, &o) == VDERIV_OK && o.fixed == 0 &&
              vderiv_index_push(&S, 12, 1, 100, 1, &o) == VDERIV_OK && o.fixed == 0 && o.moved == 0,
          "no call expires at 11 or 12; nothing moves");
    CHECK(S.perp[0].state == VDERIV_OPEN && S.perp[0].ref == 100 && S.fees_total == 1 &&
              bal(rsv) == 1 && S.seq == VDERIV_MAX_PERPS + VDERIV_MAX_CALLS && S.posted == S.seq &&
              inv(),
          "nothing past either table was read or written");
}

static void test_fees(void)
{
    setup();
    push(1000);
    uint64_t T0 = total();
    uint32_t n = 0;
    for (int k = 0; k < 6; k++) {
        vderiv_perp_terms_t t =
            pterms(k % 3, 3 + k % 3, 1125 + (uint64_t) k * 1000, 9, 300000, 300000);
        if (perp_open(&t, NULL) == VDERIV_OK) n++;
    }
    CHECK(n == 6, "six perps");
    uint64_t want = 0;
    for (int k = 0; k < 6; k++) want += pay_assure_fee((1125 + (uint64_t) k * 1000) * 7);
    vderiv_push_t o;
    uint64_t R = bal(rsv), seq = L.seq;
    CHECK(vderiv_index_push(&S, 11, 1, 1007, 1, &o) == VDERIV_OK && o.fees == want &&
              o.moved == 6 && bal(rsv) == R + want && L.seq == seq + 1 && inv(),
          "the fee is floored per perp, summed into one posting");
    CHECK(vderiv_index_push(&S, 12, 1, 990, 1, &o) == VDERIV_OK && o.moved == 6 && inv(),
          "and back down, clamped at 9");
    CHECK(total() == T0, "conserved");
    for (uint32_t i = 0; i < 6; i++) {
        vderiv_perp_t *p = &S.perp[i];
        uint64_t q = p->t.size;
        CHECK(p->bal[0] == 300000 + 7 * q - pay_assure_fee(7 * q) - 9 * q &&
                  p->bal[1] == 300000 - 7 * q + 9 * q - pay_assure_fee(9 * q) &&
                  p->fees == pay_assure_fee(7 * q) + pay_assure_fee(9 * q),
              "each perp's balances are exact");
    }
}

int main(void)
{
    test_init();
    test_switch();
    test_index();
    test_payoff();
    test_call();
    test_perp();
    test_tamper();
    test_edges();
    test_bounds();
    test_fees();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
