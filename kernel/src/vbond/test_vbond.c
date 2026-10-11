/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vbond.c — V-Bonds (vbond.h B0-B5) on a real pay_ledger: the signed
 * jurisdiction switch, issue for each allowed term and a refused one, signed
 * transfers with replay protection, redemption at maturity only, the exact
 * auction premium, the term-weighted distribution with leftovers, batching,
 * a partial distribution, and the check catching tampering. The signature
 * hook is a stand-in (SHA-256 keyed by the signer id); production passes
 * ML-DSA verification. Integers only: no floating point. */
#include <stdio.h>
#include <string.h>
#include "vbond.h"
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

#define GATE   77u
#define JURIS  0x0050534Eu /* an opaque test code */
#define NU     10
#define OWN(i) (100u + (uint32_t) (i))
#define LM     VBOND_LINE_MAX

static pay_ledger_t L;
static vbond_t S;
static vbond_cfg_t C;
static uint16_t vno, vbl;
static uint32_t viss, viss2, biss, lock, pool, pool2, src, other, vbx;
static uint32_t uv[NU], uvb[NU], ub[NU], ub2[NU]; /* users' Vino and V-Bill accounts */
static uint64_t nfund;

static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    memcpy(b, &who, sizeof who);
    b[4] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    if (sl != 32 || n > 56) return false;
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
static pay_status_t fund(uint32_t to, uint64_t amt) /* Vino issued to `to` */
{
    return raw(to, (int64_t) amt, 0, viss, 0, (int64_t) amt);
}
static pay_status_t vbi(uint32_t to, uint64_t amt) /* V-Bills issued */
{
    return raw(to, (int64_t) amt, 0, biss, 0, (int64_t) amt);
}
static pay_status_t move(uint32_t from, uint32_t to, uint64_t amt)
{
    return raw(from, -(int64_t) amt, 0, to, (int64_t) amt, 0);
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

static vbond_status_t sw(uint32_t juris, bool en, uint64_t epoch, uint32_t signer)
{
    uint8_t m[VBOND_SWITCH_MSG], sig[32];
    vbond_switch_msg(juris, en, epoch, m);
    sign(signer, m, sizeof m, sig);
    return vbond_enable(&S, juris, en, epoch, sig, 32);
}

static void txsig(uint32_t signer, uint64_t serial, uint32_t to, uint32_t payee, uint64_t nonce,
                  uint8_t sig[32])
{
    uint8_t m[VBOND_TX_MSG];
    vbond_transfer_msg(JURIS, serial, to, payee, nonce, m);
    sign(signer, m, sizeof m, sig);
}

/* a signed transfer by the current owner at the current nonce */
static vbond_status_t tx(uint64_t serial, uint32_t from, uint32_t to, uint32_t payee)
{
    uint8_t sig[32];
    const vbond_bond_t *b = vbond_get(&S, serial);
    txsig(from, serial, to, payee, b ? b->nonce : 0, sig);
    return vbond_transfer(&S, serial, from, to, payee, sig, 32);
}

static void ledger(void)
{
    pay_ledger_init(&L, NULL);
    pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    viss = open_acct(vno, 1, PAY_ACCT_ISSUER);
    viss2 = open_acct(vno, 1, PAY_ACCT_ISSUER);
    biss = open_acct(vbl, 5, PAY_ACCT_ISSUER);
    lock = open_acct(vbl, 7, 0);
    pool = open_acct(vno, 3, 0);
    pool2 = open_acct(vno, 3, 0);
    src = open_acct(vno, 4, 0);
    other = open_acct(vno, 7, 0); /* the lock owner's Vino account */
    vbx = open_acct(vbl, 9, 0);   /* a V-Bill account of a non-user */
    for (int i = 0; i < NU; i++) {
        uv[i] = open_acct(vno, OWN(i), 0);
        uvb[i] = open_acct(vno, OWN(i), 0);
        ub[i] = open_acct(vbl, OWN(i), 0);
        ub2[i] = open_acct(vbl, OWN(i), 0);
        vbi(ub[i], 1000000);
    }
    fund(src, 1000000000);
}

static void cfg(void)
{
    vbond_cfg_default(&C);
    C.vino = vno;
    C.vbill = vbl;
    C.lock = lock;
    C.gateway = GATE;
    C.juris = JURIS;
    C.verify = verify;
    for (int i = 0; i < 32; i++) C.seed[i] = (uint8_t) (i * 7 + 1);
}

/* a fresh ledger and an enabled module */
static void setup(void)
{
    ledger();
    cfg();
    vbond_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
}

static bool inv(void)
{
    return pay_ledger_check(&L) && vbond_check(&S) == VBOND_OK;
}

static const pay_journal_t *last(void)
{
    return &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
}

static void test_init(void)
{
    vbond_cfg_t d;
    memset(&d, 0x55, sizeof d);
    vbond_cfg_default(&d);
    CHECK(d.lock == VBOND_NONE && d.n_terms == 3 && d.k[0] == 13 && d.k[1] == 17 && d.k[2] == 21 &&
              d.k[3] == 0 && d.weight[0] == 233 && d.weight[1] == 1597 && d.weight[2] == 10946 &&
              d.weight[3] == 0 && d.share_num == 1 && d.share_den == 2 && d.verify == NULL &&
              d.vctx == NULL && d.juris == 0 && d.gateway == 0 && d.seed[31] == 0 && d.vino == 0,
          "defaults: k 13/17/21, weights F(k), share 1/2");
    vbond_cfg_default(NULL);
    CHECK(vbond_fib(0) == 0 && vbond_fib(1) == 1 && vbond_fib(2) == 1 && vbond_fib(3) == 2 &&
              vbond_fib(13) == 233 && vbond_fib(17) == 1597 && vbond_fib(21) == 10946 &&
              vbond_fib(90) == 2880067194370816120ull && vbond_fib(91) == 0,
          "Fibonacci table");

    ledger();
    cfg();
    memset(&S, 0x33, sizeof S);
    CHECK(vbond_init(&S, &L, &C) == VBOND_OK && !S.enabled && S.L == &L && S.next_serial == 1 &&
              S.seq == 0 && S.posted == 0 && S.switch_epoch == 0 && !S.bond[5].used &&
              S.cfg.lock == lock && S.cfg.weight[2] == 10946,
          "init: switch off, empty table, serials from 1");
    CHECK(vbond_check(&S) == VBOND_OK, "check on a fresh module");
    CHECK(vbond_init(NULL, &L, &C) == VBOND_ERR_ARG && vbond_init(&S, NULL, &C) == VBOND_ERR_ARG &&
              vbond_init(&S, &L, NULL) == VBOND_ERR_ARG,
          "missing arguments");

    struct {
        int f;
        uint64_t v;
        vbond_status_t want;
        const char *what;
    } bad[] = {
        {0, 0, VBOND_ERR_ARG, "no verify hook"},
        {1, 0, VBOND_ERR_ARG, "vino == vbill"},
        {2, 0, VBOND_ERR_ARG, "share_num 0"},
        {3, 3, VBOND_ERR_ARG, "share_num > share_den"},
        {4, 0, VBOND_ERR_ARG, "lock is a Vino account"},
        {5, 9999, VBOND_ERR_ARG, "lock does not exist"},
        {6, 0, VBOND_ERR_ARG, "no terms"},
        {7, VBOND_MAX_TERMS + 1, VBOND_ERR_ARG, "too many terms"},
        {8, 0, VBOND_ERR_ARG, "k 0"},
        {8, 91, VBOND_ERR_ARG, "k above KMAX"},
        {9, 0, VBOND_ERR_ARG, "weight 0"},
        {10, 13, VBOND_ERR_ARG, "k not increasing"},
        {11, 232, VBOND_ERR_ARG, "weight falls with the term"},
        {12, 0, VBOND_ERR_ARG, "first k 0"},
        {13, 0, VBOND_ERR_ARG, "first weight 0"},
        {14, 0, VBOND_ERR_ARG, "last k 0"},
        {15, 1, VBOND_OK, "equal weights are allowed"},
        {16, 2, VBOND_OK, "share_num == share_den"},
        {17, 0, VBOND_OK, "unused term slots are ignored"},
        {18, 0, VBOND_OK, "eight terms"},
        {19, 0, VBOND_ERR_ARG, "k not increasing at the last of eight"},
        {20, 0, VBOND_ERR_ARG, "k 0 in slot 7"},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        cfg();
        switch (bad[i].f) {
        case 0:
            C.verify = NULL;
            break;
        case 1:
            C.vbill = vno;
            break;
        case 2:
            C.share_num = 0;
            break;
        case 3:
            C.share_num = bad[i].v;
            break;
        case 4:
            C.lock = pool;
            break;
        case 5:
            C.lock = (uint32_t) bad[i].v;
            break;
        case 6:
            C.n_terms = 0;
            break;
        case 7:
            C.n_terms = (uint32_t) bad[i].v;
            break;
        case 8:
            C.k[1] = (uint32_t) bad[i].v;
            break;
        case 9:
            C.weight[1] = 0;
            break;
        case 10:
            C.k[1] = 13;
            break;
        case 11:
            C.weight[1] = bad[i].v;
            break;
        case 12:
            C.k[0] = 0;
            break;
        case 13:
            C.weight[0] = 0;
            break;
        case 14:
            C.k[2] = 0;
            break;
        case 15:
            C.weight[0] = C.weight[1] = C.weight[2] = 1;
            break;
        case 16:
            C.share_num = C.share_den = 2;
            break;
        case 17:
            C.k[3] = 0;
            C.weight[3] = 0;
            C.k[7] = 5;
            break;
        case 18:
        case 19:
        case 20:
            C.n_terms = 8;
            for (uint32_t t = 0; t < 8; t++) {
                C.k[t] = 10 + t;
                C.weight[t] = 5;
            }
            if (bad[i].f == 19) C.k[7] = C.k[6];
            if (bad[i].f == 20) C.k[7] = 0;
            break;
        }
        CHECK(vbond_init(&S, &L, &C) == bad[i].want, bad[i].what);
    }
    cfg();
    C.n_terms = 1;
    C.k[0] = 90;
    C.weight[0] = 1;
    CHECK(vbond_init(&S, &L, &C) == VBOND_OK, "one term at KMAX");
    C.k[0] = 1;
    CHECK(vbond_init(&S, &L, &C) == VBOND_OK, "one term at KMIN");
    cfg();
    vbi(lock, 5);
    CHECK(vbond_init(&S, &L, &C) == VBOND_ERR_STATE, "a non-empty lock account is refused");
}

static void test_switch(void)
{
    ledger();
    cfg();
    vbond_init(&S, &L, &C);
    uint8_t m[VBOND_SWITCH_MSG];
    vbond_switch_msg(0x04030201u, true, 0x0807060504030201ull, m);
    CHECK(!memcmp(m, "zxv/vbond-sw/v1", 15) && m[15] == 1 && m[16] == 2 && m[17] == 3 &&
              m[18] == 4 && m[19] == 1 && m[20] == 1 && m[27] == 8,
          "switch message layout");
    vbond_switch_msg(0, false, 0, m);
    CHECK(m[19] == 0 && m[15] == 0, "switch message: disable flag 0");

    uint64_t sn = 0;
    CHECK(vbond_issue(&S, OWN(0), ub[0], uv[0], 10, 13, 0, &sn) == VBOND_ERR_DISABLED && sn == 0 &&
              L.seq == NU + 1u,
          "issue refused while OFF (the default)");
    CHECK(vbond_pool_credit(&S, src, pool, 5, 0) == VBOND_ERR_DISABLED,
          "pool credit refused while OFF");
    uint64_t a = 77;
    CHECK(vbond_premium_credit(&S, src, pool, 110, 100, 1, 0, &a) == VBOND_ERR_DISABLED && a == 0,
          "premium credit refused while OFF");
    CHECK(vbond_premium_credit(&S, src, pool, UINT64_MAX, 0, UINT64_MAX, 0, &a) ==
              VBOND_ERR_DISABLED,
          "OFF is checked before the premium is computed");

    CHECK(sw(JURIS, true, 1, 78) == VBOND_ERR_SIG && !S.enabled && S.switch_epoch == 0,
          "bad manifest signature");
    CHECK(sw(JURIS + 1, true, 1, GATE) == VBOND_ERR_JURIS && !S.enabled,
          "manifest for another jurisdiction");
    CHECK(sw(JURIS, true, 0, GATE) == VBOND_ERR_EPOCH && !S.enabled, "epoch 0 is stale");
    uint8_t sig[32];
    vbond_switch_msg(JURIS, true, 1, m);
    sign(GATE, m, sizeof m, sig);
    CHECK(vbond_enable(&S, JURIS, true, 1, sig, 31) == VBOND_ERR_SIG, "short signature");
    CHECK(vbond_enable(&S, JURIS, true, 1, NULL, 32) == VBOND_ERR_ARG &&
              vbond_enable(NULL, JURIS, true, 1, sig, 32) == VBOND_ERR_ARG,
          "missing arguments");
    CHECK(vbond_enable(&S, JURIS, false, 1, sig, 32) == VBOND_ERR_SIG,
          "the flag is covered by the signature");
    CHECK(vbond_enable(&S, JURIS, true, 2, sig, 32) == VBOND_ERR_SIG,
          "the epoch is covered by the signature");
    CHECK(vbond_enable(&S, JURIS, true, 1, sig, 32) == VBOND_OK && S.enabled && S.switch_epoch == 1,
          "signed manifest switches ON");
    CHECK(vbond_enable(&S, JURIS, true, 1, sig, 32) == VBOND_ERR_EPOCH, "replayed manifest");
    CHECK(vbond_issue(&S, OWN(0), ub[0], uv[0], 10, 13, 0, &sn) == VBOND_OK && sn == 1 && inv(),
          "issue works while ON");
    CHECK(sw(JURIS, false, 5, GATE) == VBOND_OK && !S.enabled && S.switch_epoch == 5,
          "a later manifest switches OFF");
    CHECK(sw(JURIS, true, 4, GATE) == VBOND_ERR_EPOCH && !S.enabled, "an older manifest is stale");
    CHECK(vbond_issue(&S, OWN(1), ub[1], uv[1], 10, 13, 0, NULL) == VBOND_ERR_DISABLED,
          "issue refused after OFF");
    CHECK(tx(1, OWN(0), OWN(1), uv[1]) == VBOND_ERR_DISABLED, "transfer refused while OFF");
    CHECK(vbond_pool_credit(&S, src, pool, 1000, 0) == VBOND_ERR_DISABLED,
          "pool credit refused after OFF");
    move(src, pool, 1000);
    vbond_dist_t o;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.paid == 1000 && bal(uv[0]) == 1000,
          "distribution of what is already in the pool works while OFF");
    CHECK(vbond_redeem(&S, 1, OWN(0), ub[0], 233) == VBOND_OK && bal(ub[0]) == 1000000 && inv(),
          "redemption works while OFF (leaving is never blocked)");
    S.L = NULL;
    CHECK(sw(JURIS, true, 9, GATE) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;
}

static void test_issue(void)
{
    setup();
    static const uint32_t ks[3] = {13, 17, 21};
    static const uint64_t fk[3] = {233, 1597, 10946};
    for (uint32_t i = 0; i < 3; i++) {
        uint64_t sn = 0, seq = L.seq;
        CHECK(vbond_issue(&S, OWN(i), ub[i], uv[i], 100 + i, ks[i], 50 + i, &sn) == VBOND_OK &&
                  sn == i + 1 && L.seq == seq + 1,
              "issue for each allowed k: one posting");
        const vbond_bond_t *b = vbond_get(&S, sn);
        CHECK(b && b == &S.bond[i] && b->used && b->term == i && b->owner == OWN(i) &&
                  b->payee == uv[i] && b->serial == sn && b->qty == 100 + i && b->start == 50 + i &&
                  b->maturity == 50 + i + fk[i] && b->nonce == 0 && b->last_paid == 0,
              "bond record: maturity = start + F(k)");
        const pay_journal_t *j = last();
        CHECK(j->n_lines == 2 && j->lines[0].account == ub[i] &&
                  j->lines[0].d_debit == -(int64_t) (100 + i) && j->lines[0].d_credit == 0 &&
                  j->lines[1].account == lock && j->lines[1].d_debit == (int64_t) (100 + i) &&
                  j->lines[1].d_credit == 0 && j->kind == PAY_KIND_TRANSFER && j->tick == 50 + i,
              "one ledger transfer: owner -qty, lock +qty");
        CHECK(bal(ub[i]) == 1000000 - 100 - i && inv(), "V-Bills moved to the lock");
    }
    CHECK(bal(lock) == 303 && S.next_serial == 4 && S.seq == 3 && S.posted == 3,
          "lock holds the sum");
    CHECK(!strcmp(last()->e2e, "VBOND-2") &&
              !strcmp(L.journal[(L.seq - 3) % PAY_JOURNAL_MAX].e2e, "VBOND-0"),
          "e2e VBOND-<seq>");
    {
        /* identifiers: SHA-256(seed || tag || seq LE) */
        uint8_t in[41], want[32], u[32];
        char uetr[PAY_UETR_LEN + 1];
        for (int i = 0; i < 32; i++) in[i] = C.seed[i];
        in[32] = 'I';
        for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (i == 0 ? 2 : 0);
        sha256(in, sizeof in, want);
        in[32] = 'U';
        sha256(in, sizeof in, u);
        pay_uetr_from_random(u, uetr);
        CHECK(!memcmp(last()->idem_key, want, 32) && !strcmp(last()->uetr, uetr),
              "idempotency key and UETR derived from the seed and seq");
    }
    uint64_t seq = L.seq, sn = 99;
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 12, 0, &sn) == VBOND_ERR_TERM && sn == 99 &&
              vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 22, 0, &sn) == VBOND_ERR_TERM &&
              vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 0, 0, &sn) == VBOND_ERR_TERM &&
              L.seq == seq,
          "k outside the allowed set is refused and posts nothing");
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], 0, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), ub[3], uv[3], LM + 1, 13, 0, &sn) == VBOND_ERR_ARG,
          "qty 0 or above the line cap");
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], LM, 13, 0, &sn) == VBOND_ERR_FUNDS &&
              vbond_issue(&S, OWN(3), ub[3], uv[3], 1000001, 13, 0, &sn) == VBOND_ERR_FUNDS &&
              S.last_ledger == PAY_ERR_FUNDS && L.seq == seq,
          "not enough V-Bills");
    CHECK(vbond_issue(&S, OWN(3), ub[4], uv[3], 10, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), ub[3], uv[4], 10, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), uv[3], uv[3], 10, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), ub[3], ub[3], 10, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), 9999, uv[3], 10, 13, 0, &sn) == VBOND_ERR_ARG &&
              vbond_issue(&S, OWN(3), ub[3], 9999, 10, 13, 0, &sn) == VBOND_ERR_ARG,
          "accounts must be the owner's, of the right asset");
    CHECK(vbond_issue(&S, 7, lock, other, 1, 13, 0, &sn) == VBOND_ERR_ARG,
          "the lock account is never a party, even for its own owner");
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 13, UINT64_MAX - 232, &sn) ==
                  VBOND_ERR_OVERFLOW &&
              L.seq == seq,
          "maturity past 2^64 is refused");
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 13, UINT64_MAX - 233, &sn) == VBOND_OK &&
              vbond_get(&S, sn)->maturity == UINT64_MAX,
          "maturity exactly 2^64 - 1");
    CHECK(vbond_issue(NULL, OWN(3), ub[3], uv[3], 10, 13, 0, &sn) == VBOND_ERR_ARG, "no module");
    S.L = NULL;
    CHECK(vbond_issue(&S, OWN(3), ub[3], uv[3], 10, 13, 0, &sn) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;

    /* fill the table */
    setup();
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
        vbond_issue(&S, OWN(i % NU), ub[i % NU], uv[i % NU], 1, 13, 0, NULL);
    seq = L.seq;
    CHECK(vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 13, 0, &sn) == VBOND_ERR_FULL && L.seq == seq,
          "table full, nothing posted");
    CHECK(vbond_get(&S, 64) == &S.bond[63] && vbond_get(&S, 65) == NULL &&
              vbond_get(&S, 0) == NULL && vbond_get(NULL, 1) == NULL && inv(),
          "64 bonds, serials 1..64");
    CHECK(vbond_redeem(&S, 10, OWN(9), ub2[9], 233) == VBOND_OK && !S.bond[9].used,
          "redeem frees a slot");
    CHECK(vbond_issue(&S, OWN(0), ub[0], uv[0], 3, 17, 1, &sn) == VBOND_OK && sn == 65 &&
              vbond_get(&S, 65) == &S.bond[9] && vbond_get(&S, 10) == NULL && inv(),
          "the freed slot is reused with a fresh serial");
}

static void test_transfer(void)
{
    setup();
    uint64_t sn;
    vbond_issue(&S, OWN(0), ub[0], uv[0], 500, 17, 10, &sn);
    uint8_t m[VBOND_TX_MSG];
    vbond_transfer_msg(0x04030201u, 0x0807060504030201ull, 0x14131211u, 0x24232221u,
                       0x3837363534333231ull, m);
    CHECK(!memcmp(m, "zxv/vbond-tx/v1", 15) && m[15] == 1 && m[18] == 4 && m[19] == 1 &&
              m[26] == 8 && m[27] == 0x11 && m[30] == 0x14 && m[31] == 0x21 && m[34] == 0x24 &&
              m[35] == 0x31 && m[42] == 0x38,
          "transfer message layout");

    uint64_t seq = L.seq, lk = bal(lock);
    uint8_t sig[32], good[32];
    txsig(OWN(1), sn, OWN(1), uv[1], 0, sig);
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], sig, 32) == VBOND_ERR_SIG &&
              S.bond[0].owner == OWN(0),
          "signed by the receiver, not the owner: refused");
    txsig(OWN(0), sn, OWN(1), uv[1], 1, sig);
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], sig, 32) == VBOND_ERR_SIG, "wrong nonce");
    txsig(OWN(0), sn, OWN(1), uv[1], 0, good);
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(2), uv[2], good, 32) == VBOND_ERR_SIG,
          "the receiver is covered by the signature");
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], good, 31) == VBOND_ERR_SIG,
          "short signature");
    CHECK(vbond_transfer(&S, sn, OWN(1), OWN(1), uv[1], good, 32) == VBOND_ERR_OWNER,
          "only the current owner can transfer");
    CHECK(vbond_transfer(&S, sn + 1, OWN(0), OWN(1), uv[1], good, 32) == VBOND_ERR_NOT_FOUND,
          "unknown serial");
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[2], good, 32) == VBOND_ERR_ARG &&
              vbond_transfer(&S, sn, OWN(0), OWN(1), ub[1], good, 32) == VBOND_ERR_ARG &&
              vbond_transfer(&S, sn, OWN(0), 7, other, good, 32) == VBOND_ERR_SIG,
          "the new payee must be the new owner's Vino account");
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], NULL, 32) == VBOND_ERR_ARG &&
              vbond_transfer(NULL, sn, OWN(0), OWN(1), uv[1], good, 32) == VBOND_ERR_ARG,
          "missing arguments");
    CHECK(S.bond[0].owner == OWN(0) && S.bond[0].payee == uv[0] && S.bond[0].nonce == 0,
          "refusals change nothing");
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], good, 32) == VBOND_OK &&
              S.bond[0].owner == OWN(1) && S.bond[0].payee == uv[1] && S.bond[0].nonce == 1 &&
              L.seq == seq && bal(lock) == lk && S.bond[0].qty == 500 &&
              S.bond[0].maturity == 10 + 1597 && inv(),
          "good transfer: owner and payee change, the V-Bills stay locked, no posting");
    CHECK(vbond_transfer(&S, sn, OWN(0), OWN(1), uv[1], good, 32) == VBOND_ERR_OWNER,
          "replayed by the old owner");
    txsig(OWN(0), sn, OWN(1), uv[1], 0, sig);
    CHECK(vbond_transfer(&S, sn, OWN(1), OWN(1), uv[1], sig, 32) == VBOND_ERR_SIG,
          "an old signature for the new owner fails");
    CHECK(tx(sn, OWN(1), OWN(1), uvb[1]) == VBOND_OK && S.bond[0].nonce == 2 &&
              S.bond[0].owner == OWN(1) && S.bond[0].payee == uvb[1],
          "self-transfer re-registers the payee");
    txsig(OWN(1), sn, OWN(2), uv[2], 2, sig);
    CHECK(vbond_transfer(&S, sn, OWN(1), OWN(2), uv[2], sig, 32) == VBOND_OK &&
              vbond_transfer(&S, sn, OWN(2), OWN(2), uv[2], sig, 32) == VBOND_ERR_SIG &&
              S.bond[0].nonce == 3,
          "onward transfer; its signature cannot be replayed");
    S.L = NULL;
    CHECK(tx(sn, OWN(2), OWN(3), uv[3]) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;

    /* redeem by the new owner */
    CHECK(vbond_redeem(&S, sn, OWN(0), ub[0], 1607) == VBOND_ERR_OWNER &&
              vbond_redeem(&S, sn, OWN(1), ub[1], 1607) == VBOND_ERR_OWNER,
          "old owners cannot redeem");
    CHECK(vbond_redeem(&S, sn, OWN(2), ub2[2], 1606) == VBOND_ERR_IMMATURE && L.seq == seq,
          "maturity - 1 refused");
    CHECK(vbond_redeem(&S, sn, OWN(2), ub[1], 1607) == VBOND_ERR_ARG &&
              vbond_redeem(&S, sn, OWN(2), uv[2], 1607) == VBOND_ERR_ARG &&
              vbond_redeem(&S, sn, OWN(2), 9999, 1607) == VBOND_ERR_ARG,
          "payee V-Bill account must be the owner's");
    CHECK(vbond_redeem(&S, sn, OWN(2), ub2[2], 1607) == VBOND_OK && bal(ub2[2]) == 500 &&
              bal(lock) == 0 && L.seq == seq + 1 && last()->lines[0].account == lock &&
              last()->lines[0].d_debit == -500 && last()->lines[1].account == ub2[2] &&
              last()->lines[1].d_debit == 500 && last()->tick == 1607 && !S.bond[0].used &&
              S.bond[0].qty == 0 && inv(),
          "at maturity: the V-Bills go to the owner's chosen account in one posting");
    CHECK(tx(sn, OWN(2), OWN(3), uv[3]) == VBOND_ERR_NOT_FOUND, "transfer after redeem refused");
    CHECK(vbond_redeem(&S, sn, OWN(2), ub2[2], 5000) == VBOND_ERR_NOT_FOUND,
          "second redeem refused");
    CHECK(vbond_redeem(NULL, sn, OWN(2), ub2[2], 5000) == VBOND_ERR_ARG, "no module");
    S.L = NULL;
    CHECK(vbond_redeem(&S, sn, OWN(2), ub2[2], 5000) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;

    /* the lock account cannot be redeemed into, even by its owner */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 5, 13, 0, &sn);
    S.bond[0].owner = 7;
    CHECK(vbond_redeem(&S, sn, 7, lock, 300) == VBOND_ERR_ARG, "lock as payee refused");
    S.bond[0].owner = OWN(0);
    /* a ledger refusal on redeem: the lock drained outside the module */
    move(lock, vbx, 5);
    CHECK(vbond_redeem(&S, sn, OWN(0), ub[0], 300) == VBOND_ERR_FUNDS && S.bond[0].used,
          "ledger refusal keeps the bond");
}

static void test_premium(void)
{
    setup();
    uint64_t a = 99;
    CHECK(vbond_premium(&S, 90, 100, 7, &a) == VBOND_OK && a == 0, "below the floor: 0");
    a = 99;
    CHECK(vbond_premium(&S, 100, 100, 7, &a) == VBOND_OK && a == 0, "at the floor: 0");
    CHECK(vbond_premium(&S, 110, 100, 7, &a) == VBOND_OK && a == 35, "(110-100)*7 * 1/2 = 35");
    CHECK(vbond_premium(&S, 101, 100, 7, &a) == VBOND_OK && a == 3, "floor(7/2) = 3");
    CHECK(vbond_premium(&S, 101, 100, 0, &a) == VBOND_OK && a == 0, "qty 0");
    CHECK(vbond_premium(NULL, 101, 100, 7, &a) == VBOND_ERR_ARG &&
              vbond_premium(&S, 101, 100, 7, NULL) == VBOND_ERR_ARG,
          "missing arguments");
    S.cfg.share_num = 2;
    S.cfg.share_den = 3;
    CHECK(vbond_premium(&S, 110, 100, 1, &a) == VBOND_OK && a == 6, "10 * 2/3 = 6 (floor)");
    CHECK(vbond_premium(&S, 111, 100, 1, &a) == VBOND_OK && a == 7, "11 * 2/3 = 7 (floor)");
    CHECK(vbond_premium(&S, 1000, 999, 5, &a) == VBOND_OK && a == 3, "5 * 2/3 = 3 (floor)");
    S.cfg.share_num = 1;
    S.cfg.share_den = (uint64_t) 1 << 30;
    CHECK(vbond_premium(&S, ((uint64_t) 1 << 40) + 5, 5, (uint64_t) 1 << 40, &a) == VBOND_OK &&
              a == (uint64_t) 1 << 50,
          "a 2^80 premium * 2^-30 is exact");
    S.cfg.share_den = 2;
    a = 9;
    CHECK(vbond_premium(&S, ((uint64_t) 1 << 40) + 5, 5, (uint64_t) 1 << 40, &a) ==
                  VBOND_ERR_OVERFLOW &&
              a == 0,
          "a quotient above 64 bits overflows");
    S.cfg.share_num = 2;
    S.cfg.share_den = 3;
    CHECK(vbond_premium(&S, 3ull << 31, 0, 1ull << 32, &a) == VBOND_ERR_OVERFLOW,
          "q * num above 64 bits overflows");
    S.cfg.share_num = 3;
    S.cfg.share_den = 3;
    CHECK(vbond_premium(&S, 1ull << 32, 0, 1ull << 32, &a) == VBOND_ERR_OVERFLOW,
          "q * num + part above 64 bits overflows");
    S.cfg.share_num = S.cfg.share_den = 1;
    CHECK(vbond_premium(&S, LM, 0, 1, &a) == VBOND_OK && a == LM, "exactly the line cap");
    CHECK(vbond_premium(&S, LM + 1, 0, 1, &a) == VBOND_ERR_OVERFLOW, "above the line cap");
    S.cfg.share_num = 1;
    S.cfg.share_den = 2;

    /* credit */
    uint64_t seq = L.seq;
    a = 99;
    CHECK(vbond_premium_credit(&S, src, pool, 90, 100, 7, 3, &a) == VBOND_OK && a == 0 &&
              L.seq == seq && bal(pool) == 0,
          "a zero premium posts nothing");
    CHECK(vbond_premium_credit(&S, src, pool, 110, 100, 7, 3, &a) == VBOND_OK && a == 35 &&
              bal(pool) == 35 && bal(src) == 1000000000 - 35 && L.seq == seq + 1 &&
              last()->lines[0].account == src && last()->lines[1].account == pool &&
              last()->tick == 3 && inv(),
          "premium credit: one posting src -> pool");
    CHECK(vbond_premium_credit(&S, src, pool, 110, 100, 7, 3, NULL) == VBOND_OK && bal(pool) == 70,
          "amount output optional");
    S.cfg.share_num = S.cfg.share_den = 1;
    CHECK(vbond_premium_credit(&S, src, pool, LM + 1, 0, 1, 3, &a) == VBOND_ERR_OVERFLOW && a == 0,
          "overflow propagates");
    CHECK(vbond_premium_credit(&S, uv[0], pool, 1000, 0, 1, 3, &a) == VBOND_ERR_FUNDS && a == 0,
          "source without funds");
    CHECK(vbond_premium_credit(&S, src, ub[0], 1000, 0, 1, 3, &a) == VBOND_ERR_ARG && a == 0,
          "pool must be a Vino account");
    CHECK(vbond_premium_credit(NULL, src, pool, 1000, 0, 1, 3, &a) == VBOND_ERR_ARG, "no module");
    S.L = NULL;
    CHECK(vbond_premium_credit(&S, src, pool, 1000, 0, 1, 3, &a) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;

    /* pool credit */
    seq = L.seq;
    CHECK(vbond_pool_credit(&S, src, pool, 1000, 4) == VBOND_OK && bal(pool) == 1070 &&
              L.seq == seq + 1,
          "pool credit");
    vbond_issue(&S, OWN(0), ub[0], uv[0], 5, 13, 0, NULL);
    CHECK(vbond_pool_credit(&S, src, src, 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, pool, 0, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, pool, LM + 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, ub[0], pool, 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, ub[0], 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, uv[0], 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, 9999, 1, 4) == VBOND_ERR_ARG &&
              vbond_pool_credit(NULL, src, pool, 1, 4) == VBOND_ERR_ARG,
          "pool credit arguments (a bond payee is never a pool)");
    raw(src, (int64_t) LM, 0, viss2, 0, (int64_t) LM);
    CHECK(vbond_pool_credit(&S, src, pool, LM, 4) == VBOND_OK, "exactly the line cap");
    pay_ledger_set_flags(&L, pool2, PAY_ACCT_FROZEN);
    CHECK(vbond_pool_credit(&S, src, pool2, 1, 4) == VBOND_ERR_ARG, "frozen pool refused");
    S.L = NULL;
    CHECK(vbond_pool_credit(&S, src, pool, 1, 4) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;
    CHECK(inv(), "ledger check after credits");
}

static void test_distribute(void)
{
    vbond_dist_t o;
    uint64_t sn;
    setup();
    memset(&o, 0x44, sizeof o);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_EMPTY && o.pool == 0 && o.paid == 0 &&
              o.postings == 0,
          "empty pool, no bonds: pays nothing");
    move(src, pool, 1000);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_NOHOLD && o.pool == 1000 &&
              bal(pool) == 1000,
          "no live bond: nothing paid");
    vbond_issue(&S, OWN(0), ub[0], uv[0], 10, 13, 5, &sn);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 10, 17, 5, &sn);
    vbond_issue(&S, OWN(2), ub[2], uv[2], 10, 21, 5, &sn);
    S.bond[0].last_paid = S.bond[5].last_paid = 9;
    CHECK(vbond_distribute(&S, pool, 4, &o) == VBOND_ERR_NOHOLD, "bonds not yet started");
    uint64_t seq = L.seq;
    CHECK(vbond_distribute(&S, pool, 5, &o) == VBOND_OK && o.pool == 1000 && o.units == 127760 &&
              o.paid == 1000 && o.payees == 3 && o.postings == 1 && L.seq == seq + 1,
          "term-weighted distribution, one posting");
    CHECK(bal(uv[0]) == 18 && bal(uv[1]) == 125 && bal(uv[2]) == 857 && bal(pool) == 0 &&
              S.bond[0].last_paid == 18 && S.bond[1].last_paid == 125 &&
              S.bond[2].last_paid == 857 && S.bond[5].last_paid == 0 && inv(),
          "18 / 125 / 857: the leftover unit to the largest remainder");
    const pay_journal_t *j = last();
    CHECK(j->n_lines == 4 && j->lines[0].account == pool && j->lines[0].d_debit == -1000 &&
              j->lines[1].account == uv[0] && j->lines[1].d_debit == 18 &&
              j->lines[3].account == uv[2] && j->lines[3].d_debit == 857 && j->tick == 5,
          "posting lines: pool first, then payees in slot order");
    CHECK(vbond_distribute(&S, pool, 6, &o) == VBOND_ERR_EMPTY && o.pool == 0,
          "pool drained: next distribution pays nothing");

    /* three uneven bonds: two leftover units */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 3, 17, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 7, 13, 0, NULL);
    vbond_issue(&S, OWN(2), ub[2], uv[2], 2, 21, 0, NULL);
    move(src, pool, 100);
    CHECK(vbond_distribute(&S, pool, 0, NULL) == VBOND_OK && bal(uv[0]) == 17 && bal(uv[1]) == 6 &&
              bal(uv[2]) == 77 && bal(pool) == 0 && inv(),
          "17 / 6 / 77 with two leftovers");

    /* equal remainders: the tie goes to the lower slot */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 5, 13, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 5, 13, 0, NULL);
    move(src, pool, 1001);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && bal(uv[0]) == 501 && bal(uv[1]) == 500 &&
              o.units == 2330,
          "tie to the lower slot");
    /* the tie is decided by slot, not serial: free slot 0 and reissue */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 5, 13, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 5, 13, 0, NULL);
    vbond_issue(&S, OWN(2), ub[2], uv[2], 5, 13, 0, NULL);
    vbond_redeem(&S, 1, OWN(0), ub[0], 233);
    vbond_issue(&S, OWN(3), ub[3], uv[3], 5, 13, 0, NULL); /* slot 0, serial 4 */
    move(src, pool, 1000);
    CHECK(vbond_distribute(&S, pool, 233, &o) == VBOND_OK && bal(uv[3]) == 334 &&
              bal(uv[1]) == 333 && bal(uv[2]) == 333,
          "a matured, unredeemed bond still shares; ties by slot");

    /* weights overridden: pro rata by qty */
    ledger();
    cfg();
    C.weight[0] = C.weight[1] = C.weight[2] = 1;
    vbond_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
    vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 13, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 3, 21, 0, NULL);
    move(src, pool, 400);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.units == 4 && bal(uv[0]) == 100 &&
              bal(uv[1]) == 300,
          "operator weights: equal weights pay by qty only");

    /* not-started bonds are excluded; a zero share gets no line */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 13, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 1000, 21, 10, NULL);
    vbond_issue(&S, OWN(2), ub[2], uv[2], 1000, 21, 0, NULL);
    move(src, pool, 10);
    S.bond[0].last_paid = 5;
    CHECK(vbond_distribute(&S, pool, 9, &o) == VBOND_OK && bal(uv[1]) == 0 && bal(uv[0]) == 0 &&
              bal(uv[2]) == 10 && o.payees == 1 && last()->n_lines == 2 &&
              S.bond[0].last_paid == 0 && S.bond[2].last_paid == 10,
          "a bond starting later is excluded; zero shares get no line");
    move(src, pool, 10);
    CHECK(vbond_distribute(&S, pool, 10, &o) == VBOND_OK && bal(uv[1]) == 5 && bal(uv[2]) == 15,
          "it shares once started");

    /* batching: 10 payees -> 7 + 3 */
    setup();
    for (int i = 0; i < NU; i++) vbond_issue(&S, OWN(i), ub[i], uv[i], 1, 13, 0, NULL);
    move(src, pool, 1000);
    seq = L.seq;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.postings == 2 && o.payees == 10 &&
              o.paid == 1000 && L.seq == seq + 2 && last()->n_lines == 4 &&
              L.journal[(L.seq - 2) % PAY_JOURNAL_MAX].n_lines == PAY_MAX_LINES &&
              L.journal[(L.seq - 2) % PAY_JOURNAL_MAX].lines[0].d_debit == -700 &&
              last()->lines[0].d_debit == -300,
          "batched: 7 payees then 3");
    bool ok = true;
    for (int i = 0; i < NU; i++) ok = ok && bal(uv[i]) == 100 && S.bond[i].last_paid == 100;
    CHECK(ok && bal(pool) == 0 && inv(), "every payee got 100");
    /* exactly 7 payees: one posting */
    setup();
    for (int i = 0; i < 7; i++) vbond_issue(&S, OWN(i), ub[i], uv[i], 1, 13, 0, NULL);
    move(src, pool, 700);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.postings == 1 && o.payees == 7,
          "7 payees fit one posting");
    /* 8 payees: two postings */
    setup();
    for (int i = 0; i < 8; i++) vbond_issue(&S, OWN(i), ub[i], uv[i], 1, 13, 0, NULL);
    move(src, pool, 800);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.postings == 2 && o.payees == 8 &&
              last()->n_lines == 2 && bal(uv[7]) == 100,
          "8 payees: 7 + 1");
    /* the same payee for two bonds */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 13, 0, NULL);
    vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 21, 0, NULL);
    move(src, pool, 11179);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && bal(uv[0]) == 11179 &&
              S.bond[0].last_paid == 233 && S.bond[1].last_paid == 10946,
          "two bonds, one payee");

    /* argument and pool checks */
    vbond_issue(&S, OWN(1), ub[1], uv[1], 1, 13, 0, NULL); /* slot 2 */
    CHECK(vbond_distribute(&S, uv[1], 0, &o) == VBOND_ERR_ARG &&
              vbond_pool_credit(&S, src, uv[1], 1, 0) == VBOND_ERR_ARG,
          "the payee of a later slot is never the pool");
    CHECK(vbond_distribute(&S, uv[0], 0, &o) == VBOND_ERR_ARG, "a bond payee is never the pool");
    CHECK(vbond_distribute(&S, ub[1], 0, &o) == VBOND_ERR_ARG &&
              vbond_distribute(&S, 9999, 0, &o) == VBOND_ERR_ARG &&
              vbond_distribute(NULL, pool, 0, &o) == VBOND_ERR_ARG,
          "pool must be a Vino account");
    move(src, pool2, 5);
    pay_ledger_set_flags(&L, pool2, PAY_ACCT_FROZEN);
    CHECK(vbond_distribute(&S, pool2, 0, &o) == VBOND_ERR_ARG && bal(pool2) == 5,
          "frozen pool refused");
    S.L = NULL;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;

    /* pool above the line cap */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 1, 13, 0, NULL);
    raw(pool, (int64_t) LM, 0, viss2, 0, (int64_t) LM);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && bal(uv[0]) == LM,
          "exactly the line cap pays");
    raw(pool, (int64_t) LM, 0, viss2, 0, (int64_t) LM);
    raw(pool, 1, 0, viss2, 0, 1);
    seq = L.seq;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_OVERFLOW && L.seq == seq,
          "a pool above the line cap overflows");

    /* units overflow */
    setup();
    vbond_issue(&S, OWN(0), ub[0], uv[0], 4, 13, 0, NULL);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 4, 13, 0, NULL);
    move(src, pool, 10);
    S.cfg.weight[0] = (uint64_t) 1 << 62;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_OVERFLOW, "qty * weight overflows");
    S.cfg.weight[0] = (uint64_t) 1 << 61;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_OVERFLOW, "sum of units overflows");
    S.cfg.weight[0] = ((uint64_t) 1 << 61) - 1;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_OK && o.units == UINT64_MAX - 7 &&
              bal(uv[0]) == 5 && bal(uv[1]) == 5,
          "units just below 2^64");

    /* partial: the second batch is refused by the ledger */
    setup();
    for (int i = 0; i < 8; i++) vbond_issue(&S, OWN(i), ub[i], uv[i], 1, 13, 0, NULL);
    for (int i = 0; i < 7; i++) raw(uv[7], (int64_t) LM, 0, viss2, 0, (int64_t) LM);
    raw(uv[7], (int64_t) LM - 50, 0, viss2, 0, (int64_t) LM - 50);
    move(src, pool, 800);
    S.bond[7].last_paid = 3;
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_PARTIAL && o.postings == 1 &&
              o.paid == 700 && o.payees == 7 && bal(pool) == 100 && S.bond[0].last_paid == 100 &&
              S.bond[6].last_paid == 100 && S.bond[7].last_paid == 0 &&
              S.last_ledger == PAY_ERR_OVERFLOW && inv(),
          "a refused later batch: the first stands, VBOND_ERR_PARTIAL");
    /* the first batch refused: a plain ledger error */
    setup();
    vbond_issue(&S, OWN(7), ub[7], uv[7], 1, 13, 0, NULL);
    for (int i = 0; i < 7; i++) raw(uv[7], (int64_t) LM, 0, viss2, 0, (int64_t) LM);
    raw(uv[7], (int64_t) LM - 50, 0, viss2, 0, (int64_t) LM - 50);
    move(src, pool, 800);
    CHECK(vbond_distribute(&S, pool, 0, &o) == VBOND_ERR_LEDGER && o.postings == 0 && o.paid == 0 &&
              bal(pool) == 800,
          "the first batch refused: VBOND_ERR_LEDGER");
}

static void test_check(void)
{
    setup();
    uint64_t sn;
    vbond_issue(&S, OWN(0), ub[0], uv[0], 10, 13, 0, &sn);
    vbond_issue(&S, OWN(1), ub[1], uv[1], 20, 21, 7, &sn);
    CHECK(vbond_check(&S) == VBOND_OK, "check passes");
    CHECK(vbond_check(NULL) == VBOND_ERR_ARG, "no module");
    S.bond[1].qty++;
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "tampered qty");
    S.bond[1].qty--;
    S.bond[1].maturity++;
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "tampered maturity");
    S.bond[1].maturity--;
    S.bond[1].term = 1;
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "tampered term");
    S.bond[1].term = 3;
    S.bond[1].maturity = 7; /* what F(k[3]) = F(0) = 0 would give */
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "term out of range");
    S.bond[1].maturity = 7 + 10946;
    S.bond[1].term = 2;
    S.bond[1].start = UINT64_MAX;
    S.bond[1].maturity = 10945; /* what a wrapped sum would give */
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "maturity overflow");
    S.bond[1].start = 7;
    S.bond[1].maturity = 7 + 10946;
    CHECK(vbond_check(&S) == VBOND_OK, "restored");
    /* a wrapped sum equal to the lock balance is still caught */
    S.bond[0].qty = UINT64_MAX;
    S.bond[1].qty = 31;
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "qty sum overflow");
    S.bond[0].qty = 10;
    S.bond[1].qty = 20;
    S.bond[5].qty = 99; /* unused slots do not count */
    CHECK(vbond_check(&S) == VBOND_OK, "unused slots ignored");
    move(lock, vbx, 1);
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "lock drained outside the module");
    move(vbx, lock, 1);
    CHECK(vbond_check(&S) == VBOND_OK, "restored");
    L.acct[uv[0]].debit++;
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "a broken ledger");
    L.acct[uv[0]].debit--;
    uint32_t lk = S.cfg.lock;
    S.cfg.lock = uv[0];
    CHECK(vbond_check(&S) == VBOND_ERR_STATE, "lock not a V-Bill account");
    S.cfg.lock = lk;
    S.L = NULL;
    CHECK(vbond_check(&S) == VBOND_ERR_ARG, "no ledger");
    S.L = &L;
    CHECK(vbond_check(&S) == VBOND_OK && pay_ledger_verify_chain(&L), "final check and chain");
}

int main(void)
{
    test_init();
    test_switch();
    test_issue();
    test_transfer();
    test_premium();
    test_distribute();
    test_check();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
