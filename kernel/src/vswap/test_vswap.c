/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vswap.c — the Player Card swap (vswap.h W1-W6, T1-T4) on a real
 * pay_ledger: the signed jurisdiction switch, par and exact rates, the
 * ratio-never-falls property over a deterministic LCG run, dust, funds, the
 * vault exclusion, a reserve loss, lock tiers with exit notice, the weighted
 * dividend with leftovers, reinvest, batching and the epoch check. The
 * signature hook is a stand-in (SHA-256 keyed by the signer id); production
 * passes ML-DSA verification. Integers only: no floating point. */
#include <stdio.h>
#include <string.h>
#include "vswap.h"
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
#define JURIS 0x0050534Eu /* "NSP\0", an opaque test code */
#define NU    6

static pay_ledger_t L;
static vswap_t S;
static vswap_cfg_t C;
static uint16_t vno, vbl;
static uint32_t viss, reserve, pool, loss, biss, vault, lockM, lockD, biss2;
static uint32_t uv[NU], ub[NU]; /* users' Vino and V-Bill accounts */
static uint64_t nfund;

static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    b[0] = (uint8_t) who;
    b[1] = 0x5A;
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

/* a test posting with a chosen e2e (NULL = FUND-n) */
static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb,
                        const char *e2e)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    if (e2e)
        snprintf(r.e2e, sizeof r.e2e, "%s", e2e);
    else
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
    return raw(to, (int64_t) amt, 0, viss, 0, (int64_t) amt, NULL);
}
static pay_status_t vbi(uint32_t to, uint64_t amt) /* V-Bills issued outside vswap */
{
    return raw(to, (int64_t) amt, 0, biss, 0, (int64_t) amt, NULL);
}
static pay_status_t move(uint32_t from, uint32_t to, uint64_t amt)
{
    return raw(from, -(int64_t) amt, 0, to, (int64_t) amt, 0, NULL);
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
static uint64_t R(void)
{
    return bal(reserve);
}
static uint64_t N(void)
{
    return pay_ledger_account(&L, biss)->credit;
}

static vswap_status_t sw(uint32_t juris, bool en, uint64_t epoch, uint32_t signer)
{
    uint8_t m[VSWAP_SWITCH_MSG], sig[32];
    vswap_switch_msg(juris, en, epoch, m);
    sign(signer, m, sizeof m, sig);
    return vswap_enable(&S, juris, en, epoch, sig, 32);
}

static void ledger(void)
{
    pay_ledger_init(&L, NULL);
    pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    viss = open_acct(vno, 1, PAY_ACCT_ISSUER);
    reserve = open_acct(vno, 2, 0);
    pool = open_acct(vno, 3, 0);
    loss = open_acct(vno, 4, 0);
    biss = open_acct(vbl, 5, PAY_ACCT_ISSUER);
    vault = open_acct(vbl, 6, 0);
    lockM = open_acct(vbl, 7, 0);
    lockD = open_acct(vbl, 8, 0);
    biss2 = open_acct(vbl, 9, PAY_ACCT_ISSUER);
    for (int i = 0; i < NU; i++) {
        uv[i] = open_acct(vno, 100 + (uint32_t) i, 0);
        ub[i] = open_acct(vbl, 100 + (uint32_t) i, 0);
        fund(uv[i], 1000000);
    }
}

static void cfg(void)
{
    vswap_cfg_default(&C);
    C.vino = vno;
    C.vbill = vbl;
    C.reserve = reserve;
    C.issuer = biss;
    C.vault = vault;
    C.lock[VSWAP_TIER_MEDIUM] = lockM;
    C.lock[VSWAP_TIER_DEEP] = lockD;
    C.gateway = GATE;
    C.juris = JURIS;
    C.verify = verify;
    for (int i = 0; i < 32; i++) C.seed[i] = (uint8_t) (i * 7 + 1);
}

/* a fresh ledger and an enabled swap */
static void setup(void)
{
    ledger();
    cfg();
    vswap_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
}

static bool inv(void)
{
    return pay_ledger_check(&L) && vswap_epoch_check(&S) == VSWAP_OK;
}

static const pay_journal_t *last(void)
{
    return &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
}

static void test_init(void)
{
    vswap_cfg_t d;
    memset(&d, 0x55, sizeof d);
    vswap_cfg_default(&d);
    CHECK(d.vault == VSWAP_NONE && d.lock[0] == VSWAP_NONE && d.lock[1] == VSWAP_NONE &&
              d.lock[2] == VSWAP_NONE && d.weight[0] == 1 && d.weight[1] == 2 && d.weight[2] == 3 &&
              d.par_num == 1 && d.par_den == 1 && d.k_medium == 12 && d.k_deep == 20 &&
              d.verify == NULL && d.juris == 0 && d.seed[31] == 0,
          "defaults: no vault, weights 1/2/3, par 1/1, k 12 and 20");
    vswap_cfg_default(NULL);
    CHECK(vswap_fib(0) == 0 && vswap_fib(1) == 1 && vswap_fib(2) == 1 && vswap_fib(3) == 2 &&
              vswap_fib(10) == 55 && vswap_fib(12) == 144 && vswap_fib(20) == 6765 &&
              vswap_fib(90) == 2880067194370816120ull && vswap_fib(91) == 0,
          "Fibonacci table");

    ledger();
    cfg();
    CHECK(vswap_init(&S, &L, &C) == VSWAP_OK && !S.enabled && S.period[1] == 144 &&
              S.period[2] == 6765 && S.period[0] == 0 && S.L == &L && S.cfg.lock[0] == VSWAP_NONE &&
              S.seq == 0,
          "init: switch off, notice periods 0 / 144 / 6765");
    CHECK(vswap_init(NULL, &L, &C) == VSWAP_ERR_ARG && vswap_init(&S, NULL, &C) == VSWAP_ERR_ARG &&
              vswap_init(&S, &L, NULL) == VSWAP_ERR_ARG,
          "missing arguments");
    struct {
        int field;
        uint64_t val;
        vswap_status_t want;
        const char *what;
    } t[] = {
        {0, 0, VSWAP_ERR_ARG, "no verify hook"},
        {1, 0, VSWAP_ERR_ARG, "par num 0"},
        {2, 0, VSWAP_ERR_ARG, "par den 0"},
        {3, 0, VSWAP_ERR_ARG, "vino == vbill"},
        {4, 0, VSWAP_ERR_ARG, "reserve not a Vino account"},
        {5, 0, VSWAP_ERR_ARG, "issuer not a V-Bill account"},
        {6, 0, VSWAP_ERR_ARG, "issuer without the ISSUER flag"},
        {7, 0, VSWAP_ERR_ARG, "vault not a V-Bill account"},
        {8, 0, VSWAP_ERR_ARG, "vault == issuer"},
        {9, 0, VSWAP_ERR_ARG, "medium lock not a V-Bill account"},
        {10, 0, VSWAP_ERR_ARG, "deep lock == issuer"},
        {11, 0, VSWAP_ERR_ARG, "deep lock == vault"},
        {12, 0, VSWAP_ERR_ARG, "medium lock == deep lock"},
        {13, 0, VSWAP_ERR_ARG, "weight 0"},
        {14, 0, VSWAP_ERR_ARG, "weight above the maximum"},
        {15, 0, VSWAP_ERR_ARG, "k_medium 0"},
        {16, 0, VSWAP_ERR_ARG, "deep not longer than medium (F1 == F2)"},
        {17, 0, VSWAP_ERR_ARG, "k_deep above the table"},
        {18, 0, VSWAP_ERR_ARG, "medium lock missing"},
        {19, 0, VSWAP_ERR_ARG, "reserve missing"},
        {20, 0, VSWAP_ERR_ARG, "deep weight 0"},
        {21, 0, VSWAP_ERR_ARG, "medium lock == vault"},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        cfg();
        switch (t[i].field) {
        case 0:
            C.verify = NULL;
            break;
        case 1:
            C.par_num = 0;
            break;
        case 2:
            C.par_den = 0;
            break;
        case 3:
            C.vbill = C.vino;
            break;
        case 4:
            C.reserve = ub[0];
            break;
        case 5:
            C.issuer = viss;
            break;
        case 6:
            C.issuer = ub[0];
            break;
        case 7:
            C.vault = uv[0];
            break;
        case 8:
            C.vault = biss;
            break;
        case 9:
            C.lock[1] = uv[0];
            break;
        case 10:
            C.lock[2] = biss;
            break;
        case 11:
            C.lock[2] = vault;
            break;
        case 12:
            C.lock[2] = lockM;
            break;
        case 13:
            C.weight[0] = 0;
            break;
        case 14:
            C.weight[1] = VSWAP_WEIGHT_MAX + 1;
            break;
        case 15:
            C.k_medium = 0;
            break;
        case 16:
            C.k_medium = 1, C.k_deep = 2;
            break;
        case 17:
            C.k_deep = VSWAP_FIB_KMAX + 1;
            break;
        case 18:
            C.lock[1] = VSWAP_NONE;
            break;
        case 19:
            C.reserve = 999;
            break;
        case 20:
            C.weight[2] = 0;
            break;
        case 21:
            C.lock[1] = vault;
            break;
        }
        CHECK(vswap_init(&S, &L, &C) == t[i].want, t[i].what);
    }
    cfg();
    C.weight[0] = C.weight[1] = C.weight[2] = VSWAP_WEIGHT_MAX;
    C.k_medium = 2;
    C.k_deep = 3;
    C.vault = VSWAP_NONE;
    CHECK(vswap_init(&S, &L, &C) == VSWAP_OK && S.period[1] == 1 && S.period[2] == 2,
          "maximum weights, F(2) = 1 and F(3) = 2, no vault: accepted");
    cfg();
    C.k_deep = VSWAP_FIB_KMAX;
    CHECK(vswap_init(&S, &L, &C) == VSWAP_OK, "k_deep at the table's end accepted");
    vbi(lockD, 5);
    cfg();
    CHECK(vswap_init(&S, &L, &C) == VSWAP_ERR_STATE, "a lock account must start empty");
    move(lockD, ub[0], 5);
    vbi(lockM, 5);
    CHECK(vswap_init(&S, &L, &C) == VSWAP_ERR_STATE, "the medium one too");
}

static void test_switch(void)
{
    ledger();
    cfg();
    vswap_init(&S, &L, &C);
    vswap_quote_t q;
    uint32_t id;
    vbi(ub[0], 10);
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 100, 1, &q) == VSWAP_ERR_DISABLED &&
              vswap_lock(&S, ub[0], uv[0], 1, 5, false, 1, &id) == VSWAP_ERR_DISABLED &&
              vswap_dividend(&S, pool, NULL, 0, false, 1, NULL, NULL) == VSWAP_ERR_DISABLED,
          "off by default: swap in, lock and dividend refused");
    CHECK(sw(JURIS, true, 1, GATE + 1) == VSWAP_ERR_SIG, "another key cannot switch it on");
    CHECK(sw(JURIS + 1, true, 1, GATE) == VSWAP_ERR_JURIS, "a manifest for another jurisdiction");
    CHECK(sw(JURIS, true, 0, GATE) == VSWAP_ERR_EPOCH, "epoch 0 is not above the start");
    uint8_t m[VSWAP_SWITCH_MSG], sig[32];
    vswap_switch_msg(JURIS, false, 1, m);
    sign(GATE, m, sizeof m, sig);
    CHECK(vswap_enable(&S, JURIS, true, 1, sig, 32) == VSWAP_ERR_SIG,
          "a signature over enable = 0 does not enable");
    vswap_switch_msg(JURIS, true, 1, m);
    sign(GATE, m, sizeof m, sig);
    CHECK(vswap_enable(&S, JURIS, true, 1, sig, 31) == VSWAP_ERR_SIG &&
              vswap_enable(&S, JURIS, true, 1, NULL, 32) == VSWAP_ERR_ARG &&
              vswap_enable(NULL, JURIS, true, 1, sig, 32) == VSWAP_ERR_ARG,
          "short or missing signature");
    CHECK(vswap_enable(&S, JURIS, true, 2, sig, 32) == VSWAP_ERR_SIG, "signed epoch is bound");
    CHECK(!S.enabled && S.switch_epoch == 0, "refusals change nothing");
    CHECK(vswap_enable(&S, JURIS, true, 1, sig, 32) == VSWAP_OK && S.enabled && S.switch_epoch == 1,
          "a signed manifest at epoch 1 switches it on");
    CHECK(sw(JURIS, false, 1, GATE) == VSWAP_ERR_EPOCH && S.enabled, "a stale epoch is refused");
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 100, 1, &q) == VSWAP_OK, "on: swap in works");
    CHECK(sw(JURIS, false, 2, GATE) == VSWAP_OK && !S.enabled && S.switch_epoch == 2,
          "a later epoch switches it off");
    uint64_t seq = L.seq;
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 100, 2, &q) == VSWAP_ERR_DISABLED && L.seq == seq,
          "off: swap in refused, nothing posts");
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 3, 2, &q) == VSWAP_OK && L.seq == seq + 1,
          "off: swap out still works (leaving is never blocked)");
    CHECK(vswap_quote_in(&S, 100, &q) == VSWAP_OK, "quotes work whatever the switch says");
    CHECK(sw(JURIS, true, 7, GATE) == VSWAP_OK && S.enabled, "and back on at a later epoch");

    uint8_t ref[VSWAP_SWITCH_MSG];
    memcpy(ref, "zxv/vswap-sw/v1", 15);
    ref[15] = 0x04, ref[16] = 0x03, ref[17] = 0x02, ref[18] = 0x01, ref[19] = 1;
    for (int i = 0; i < 8; i++) ref[20 + i] = (uint8_t) (0x1122334455667788ull >> (8 * i));
    vswap_switch_msg(0x01020304u, true, 0x1122334455667788ull, m);
    CHECK(!memcmp(m, ref, sizeof m), "the manifest bytes are exactly as documented");
    vswap_switch_msg(0x01020304u, false, 0x1122334455667788ull, m);
    CHECK(m[19] == 0, "enable = 0 is byte 0");
}

static void test_par_and_rate(void)
{
    ledger();
    cfg();
    C.par_num = 3;
    C.par_den = 2; /* 3 Vino per 2 V-Bills */
    vswap_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
    vswap_quote_t q;
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && q.par && q.num == 3 && q.den == 2,
          "nothing outstanding: par");
    uint64_t seq = L.seq;
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 1, 1, &q) == VSWAP_ERR_DUST && L.seq == seq,
          "1 Vino buys no whole V-Bill at 3/2: dust, nothing posts");
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 10, 1, &q) == VSWAP_OK && q.vbills == 6 && q.vino == 9 &&
              q.par,
          "10 Vino at 3/2: 6 V-Bills for 9 Vino, 1 stays with the user");
    CHECK(bal(uv[0]) == 1000000 - 9 && R() == 9 && bal(ub[0]) == 6 && N() == 6 &&
              L.seq == seq + 1 && inv(),
          "one posting: user -9, reserve +9, issuer issues 6");
    const pay_journal_t *j = last();
    CHECK(j->n_lines == 4 && j->kind == PAY_KIND_ISSUE, "four lines, an issue");
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && !q.par && q.num == 9 && q.den == 6,
          "now the rate is R/B = 9/6 exactly");
    CHECK(vswap_swap_in(&S, uv[1], ub[1], 10, 2, &q) == VSWAP_OK && q.vbills == 6 && q.vino == 9,
          "10 Vino at 9/6: 6 V-Bills for 9");
    CHECK(vswap_swap_in(&S, uv[1], ub[1], 11, 2, &q) == VSWAP_OK && q.vbills == 7 && q.vino == 11,
          "11 at 18/12: floor(11*12/18) = 7 V-Bills, ceil(7*18/12) = 11");

    /* R == 0 with B > 0: par for buying, nothing for selling */
    ledger();
    cfg();
    C.par_num = 5;
    C.par_den = 1;
    vswap_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
    vbi(ub[2], 100);
    CHECK(vswap_quote_in(&S, 12, &q) == VSWAP_OK && q.par && q.vbills == 2 && q.vino == 10,
          "an empty reserve: par 5/1, 12 buys 2 for 10");
    CHECK(vswap_quote_out(&S, 100, &q) == VSWAP_ERR_DUST && q.vino == 0,
          "selling into an empty reserve gives nothing: dust");
    CHECK(vswap_swap_out(&S, ub[2], uv[2], 100, 1, &q) == VSWAP_ERR_DUST && bal(ub[2]) == 100,
          "and posts nothing");
    /* B == 0 with R > 0: par */
    ledger();
    cfg();
    vswap_init(&S, &L, &C);
    fund(reserve, 50);
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && q.par && q.num == 1 && q.den == 1,
          "a reserve with no V-Bills: par");
}

static void test_exact(void)
{
    setup();
    fund(reserve, 1000);
    vbi(ub[5], 300);
    vswap_quote_t q;
    CHECK(vswap_quote_in(&S, 100, &q) == VSWAP_OK && !q.par && q.num == 1000 && q.den == 300 &&
              q.vbills == 30 && q.vino == 100,
          "R/B = 1000/300: 100 buys 30 for exactly 100");
    CHECK(vswap_quote_in(&S, 7, &q) == VSWAP_OK && q.vbills == 2 && q.vino == 7,
          "7 buys 2 for ceil(2000/300) = 7");
    CHECK(vswap_quote_in(&S, 4, &q) == VSWAP_OK && q.vbills == 1 && q.vino == 4,
          "4 buys 1 for ceil(1000/300) = 4");
    CHECK(vswap_quote_in(&S, 6, &q) == VSWAP_OK && q.vbills == 1 && q.vino == 4,
          "6 buys only 1, charged 4: 2 stay with the user");
    CHECK(vswap_quote_in(&S, 3, &q) == VSWAP_ERR_DUST, "3 buys nothing");
    CHECK(vswap_quote_in(&S, 0, &q) == VSWAP_ERR_ARG &&
              vswap_quote_in(&S, VSWAP_LINE_MAX + 1, &q) == VSWAP_ERR_ARG &&
              vswap_quote_in(NULL, 5, &q) == VSWAP_ERR_ARG &&
              vswap_quote_in(&S, 5, NULL) == VSWAP_ERR_ARG,
          "zero, too large or missing arguments");
    uint64_t r0 = R(), b0 = N();
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 7, 1, NULL) == VSWAP_OK && bal(uv[0]) == 1000000 - 7 &&
              bal(ub[0]) == 2 && R() == 1007 && N() == 302,
          "the swap does what the quote said");
    CHECK(R() * b0 >= r0 * N(), "the ratio did not fall: 1007*300 >= 1000*302");
    CHECK(vswap_quote_out(&S, 2, &q) == VSWAP_OK && q.vino == 6 && q.vbills == 2 && q.num == 1007 &&
              q.den == 302 && !q.par,
          "2 V-Bills redeem for floor(2014/302) = 6 (less than the 7 paid)");
    CHECK(vswap_quote_out(&S, 0, &q) == VSWAP_ERR_ARG &&
              vswap_quote_out(&S, VSWAP_LINE_MAX + 1, &q) == VSWAP_ERR_ARG &&
              vswap_quote_out(NULL, 1, &q) == VSWAP_ERR_ARG &&
              vswap_quote_out(&S, 1, NULL) == VSWAP_ERR_ARG,
          "zero, too large or missing arguments (out)");
    CHECK(vswap_quote_out(&S, 303, &q) == VSWAP_ERR_FUNDS &&
              vswap_quote_out(&S, 302, &q) == VSWAP_OK && q.vino == 1007,
          "no more than B can be redeemed; all of B takes all of R");
    r0 = R(), b0 = N();
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 2, 2, &q) == VSWAP_OK && q.vino == 6 &&
              bal(uv[0]) == 1000000 - 1 && bal(ub[0]) == 0 && R() == 1001 && N() == 300 && inv(),
          "swap out: 2 V-Bills redeemed, 6 Vino paid, one posting");
    CHECK(last()->n_lines == 4 && last()->kind == PAY_KIND_REDEEM, "a redeem of four lines");
    CHECK(R() * b0 >= r0 * N(), "the ratio did not fall on the way out");
    uint64_t seq = L.seq;
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 1, 3, &q) == VSWAP_ERR_FUNDS && L.seq == seq,
          "redeeming V-Bills not held: funds, nothing posts");
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 1000004, 3, &q) == VSWAP_ERR_FUNDS && L.seq == seq &&
              S.last_ledger == PAY_ERR_FUNDS,
          "buying with more Vino than held: funds");
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 999999, 3, &q) == VSWAP_OK, "all of it is fine");

    /* identifiers: SHA-256(seed || tag || seq), e2e VSWAP-<seq> */
    uint8_t in[41], want[32];
    char uetr[PAY_UETR_LEN + 1], e2e[40];
    memcpy(in, C.seed, 32);
    in[32] = 'I';
    uint64_t sq = S.seq - 1;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (sq >> (8 * i));
    sha256(in, sizeof in, want);
    CHECK(!memcmp(last()->idem_key, want, 32), "idempotency key = SHA-256(seed || I || seq)");
    in[32] = 'U';
    sha256(in, sizeof in, want);
    pay_uetr_from_random(want, uetr);
    snprintf(e2e, sizeof e2e, "VSWAP-%llu", (unsigned long long) sq);
    CHECK(!strcmp(last()->uetr, uetr) && !strcmp(last()->e2e, e2e),
          "UETR from SHA-256(seed || U || seq); e2e VSWAP-<seq>; failed posts use a seq");
    CHECK(S.seq == 5 && S.posted == 3, "five attempts reached the ledger, three posted");
}

static void test_parties(void)
{
    setup();
    vswap_quote_t q;
    uint64_t seq = L.seq;
    CHECK(vswap_swap_in(&S, ub[0], ub[0], 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(&S, reserve, ub[0], 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(&S, uv[0], uv[0], 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(&S, uv[0], biss, 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(&S, uv[0], vault, 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(&S, uv[0], 999, 10, 1, &q) == VSWAP_ERR_ARG &&
              vswap_swap_in(NULL, uv[0], ub[0], 10, 1, &q) == VSWAP_ERR_ARG && L.seq == seq,
          "wrong assets, the reserve, the issuer, the vault or a missing account refused");
    CHECK(vswap_swap_in(&S, uv[0], lockM, 10, 1, &q) == VSWAP_ERR_LOCKED &&
              vswap_swap_out(&S, lockD, uv[0], 1, 1, &q) == VSWAP_ERR_LOCKED &&
              vswap_swap_out(NULL, ub[0], uv[0], 1, 1, &q) == VSWAP_ERR_ARG,
          "a lock account is never a swap party");
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 0, 1, &q) == VSWAP_ERR_ARG, "zero amount");
    /* overflow at par 1 / 2^40 */
    ledger();
    cfg();
    C.par_den = (uint64_t) 1 << 40;
    vswap_init(&S, &L, &C);
    sw(JURIS, true, 1, GATE);
    CHECK(vswap_quote_in(&S, (uint64_t) 1 << 30, &q) == VSWAP_ERR_OVERFLOW,
          "2^70 V-Bills does not fit 64 bits");
    CHECK(vswap_quote_in(&S, ((uint64_t) 1 << 19) + 1, &q) == VSWAP_ERR_OVERFLOW,
          "just above the ledger's line limit");
    CHECK(vswap_quote_in(&S, (uint64_t) 1 << 19, &q) == VSWAP_OK && q.vbills == VSWAP_LINE_MAX &&
              q.vino == (uint64_t) 1 << 19,
          "exactly at the line limit is fine");
    pay_ledger_set_flags(&L, uv[0], PAY_ACCT_FROZEN);
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 1, 1, &q) == VSWAP_ERR_LEDGER &&
              S.last_ledger == PAY_ERR_POLICY,
          "a frozen account: the ledger refuses");
    /* h > N: the vault holds V-Bills of another issuer */
    setup();
    raw(vault, 10, 0, biss2, 0, 10, NULL);
    CHECK(vswap_rate(&S, &q) == VSWAP_ERR_STATE && vswap_quote_in(&S, 5, &q) == VSWAP_ERR_STATE &&
              vswap_quote_out(&S, 5, &q) == VSWAP_ERR_STATE &&
              vswap_rate(NULL, &q) == VSWAP_ERR_ARG && vswap_rate(&S, NULL) == VSWAP_ERR_ARG,
          "vault holding above N fails closed");
    vbi(ub[0], 10);
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && q.par, "h == N: B = 0, par");
}

static void test_vault_and_loss(void)
{
    setup();
    fund(reserve, 1200);
    vbi(ub[5], 300);
    vbi(vault, 100); /* N = 400, h = 100, B = 300 */
    vswap_quote_t q;
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && q.num == 1200 && q.den == 300,
          "the vault is excluded: rate 1200 / (400 - 100)");
    vswap_t T;
    vswap_cfg_t c2 = C;
    c2.vault = VSWAP_NONE;
    vswap_init(&T, &L, &c2);
    CHECK(vswap_rate(&T, &q) == VSWAP_OK && q.num == 1200 && q.den == 400,
          "with no vault configured the base is all 400");
    CHECK(vswap_quote_out(&S, 30, &q) == VSWAP_OK && q.vino == 120 &&
              vswap_quote_out(&T, 30, &q) == VSWAP_OK && q.vino == 90,
          "so 30 V-Bills redeem for 120, not 90");

    /* loss: a V-Bill can redeem for less than it cost */
    setup();
    vswap_quote_t qi, qo;
    CHECK(vswap_swap_in(&S, uv[0], ub[0], 1000, 1, &qi) == VSWAP_OK && qi.vbills == 1000 &&
              qi.vino == 1000 && qi.par,
          "first buyer: 1000 at par 1/1");
    CHECK(move(reserve, loss, 250) == PAY_OK && R() == 750, "the reserve loses 250");
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 400, 2, &qo) == VSWAP_OK && qo.vino == 300 &&
              qo.num == 750 && qo.den == 1000,
          "400 V-Bills bought for 400 now redeem for 300");
    CHECK(vswap_swap_in(&S, uv[1], ub[1], 450, 3, &qi) == VSWAP_OK && qi.vbills == 600 &&
              qi.vino == 450 && inv(),
          "and the next buyer pays the lower price: 450 buys 600");
}

static uint64_t lcg = 12345;
static uint64_t rnd(uint64_t m)
{
    lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
    return (lcg >> 33) % m;
}

static void test_ratio_never_falls(void)
{
    setup();
    fund(reserve, 777);
    vbi(ub[5], 500);
    vbi(vault, 50);
    uint32_t bad = 0, posted = 0, dust = 0, steps = 600;
    for (uint32_t k = 0; k < steps; k++) {
        uint32_t u = (uint32_t) rnd(5);
        uint64_t r0 = R(), b0 = N() - bal(vault);
        vswap_quote_t q, p;
        vswap_status_t st;
        if (rnd(2) || bal(ub[u]) == 0) {
            uint64_t a = rnd(4) ? 1 + rnd(5000) : 1 + rnd(2);
            vswap_quote_in(&S, a, &p);
            st = vswap_swap_in(&S, uv[u], ub[u], a, k, &q);
            if (st == VSWAP_OK && (q.vino > a || q.vino != p.vino || q.vbills != p.vbills ||
                                   q.vino * q.den < q.vbills * q.num))
                bad++;
        } else {
            uint64_t v = 1 + rnd(bal(ub[u]));
            vswap_quote_out(&S, v, &p);
            st = vswap_swap_out(&S, ub[u], uv[u], v, k, &q);
            if (st == VSWAP_OK && (q.vino != p.vino || q.vino * q.den > v * q.num)) bad++;
        }
        if (st == VSWAP_OK) posted++;
        if (st == VSWAP_ERR_DUST) dust++;
        if (st != VSWAP_OK && st != VSWAP_ERR_DUST) bad++;
        uint64_t r1 = R(), b1 = N() - bal(vault);
        if (b0 && b1 && r1 * b0 < r0 * b1) bad++;
        if (!inv()) bad++;
    }
    printf("  ratio run: %u steps, %u posted, %u dust, %u bad\n", steps, posted, dust, bad);
    CHECK(bad == 0 && posted > 400 && dust > 0, "the reserve ratio never fell over the LCG run");
    CHECK(pay_ledger_verify_chain(&L), "the provenance chain verifies after the run");
}

static void test_lock(void)
{
    setup();
    vswap_quote_t q;
    uint32_t id = 99, id2;
    vswap_swap_in(&S, uv[0], ub[0], 1000, 1, &q);
    CHECK(vswap_lock(&S, ub[0], uv[0], VSWAP_TIER_LOW, 10, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(&S, ub[0], uv[0], 3, 10, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(&S, uv[0], uv[0], 1, 10, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(&S, ub[0], ub[0], 1, 10, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(&S, lockD, uv[0], 1, 10, false, 1, &id) == VSWAP_ERR_LOCKED &&
              vswap_lock(&S, ub[0], uv[0], 1, 0, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(&S, ub[0], uv[0], 1, VSWAP_LINE_MAX + 1, false, 1, &id) == VSWAP_ERR_ARG &&
              vswap_lock(NULL, ub[0], uv[0], 1, 10, false, 1, &id) == VSWAP_ERR_ARG && id == 99,
          "LOW is not a lock; bad tier, accounts or amounts refused");
    uint64_t seq = L.seq;
    CHECK(vswap_lock(&S, ub[0], uv[0], 1, 1001, false, 1, &id) == VSWAP_ERR_FUNDS && L.seq == seq &&
              !S.pos[0].used,
          "locking more than held: funds, no position");
    CHECK(vswap_lock(&S, ub[0], uv[0], VSWAP_TIER_MEDIUM, 400, true, 5, &id) == VSWAP_OK &&
              id == 0 && bal(ub[0]) == 600 && bal(lockM) == 400 && L.seq == seq + 1 && inv(),
          "400 locked in MEDIUM: one transfer to the lock account");
    vswap_pos_t *p = &S.pos[0];
    CHECK(p->used && p->reinvest && !p->noticed && p->tier == 1 && p->owner == ub[0] &&
              p->payee == uv[0] && p->vbills == 400 && p->opened == 5,
          "the position record");
    CHECK(vswap_lock(&S, ub[0], uv[0], VSWAP_TIER_DEEP, 100, false, 6, &id2) == VSWAP_OK &&
              id2 == 1 && bal(lockD) == 100 && bal(ub[0]) == 500 && inv(),
          "100 in DEEP");
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 501, 7, &q) == VSWAP_ERR_FUNDS,
          "locked V-Bills cannot be swapped out");
    CHECK(vswap_quote_out(&S, 1000, &q) == VSWAP_OK, "but they still count in the base");

    CHECK(vswap_unlock(&S, 0, ub[0], 1000000) == VSWAP_ERR_LOCKED, "no notice: locked");
    CHECK(vswap_notice(&S, 0, ub[1], 10) == VSWAP_ERR_ARG &&
              vswap_notice(&S, VSWAP_MAX_POS, ub[0], 10) == VSWAP_ERR_ARG &&
              vswap_notice(&S, 5, ub[0], 10) == VSWAP_ERR_ARG &&
              vswap_notice(NULL, 0, ub[0], 10) == VSWAP_ERR_ARG,
          "notice by another owner or on an empty slot refused");
    CHECK(vswap_notice(&S, 0, ub[0], 10) == VSWAP_OK && p->noticed && p->notice_tick == 10,
          "notice filed at tick 10");
    CHECK(vswap_notice(&S, 0, ub[0], 11) == VSWAP_ERR_STATE && p->notice_tick == 10,
          "a second notice changes nothing");
    sw(JURIS, false, 2, GATE); /* leaving works with the switch off */
    seq = L.seq;
    CHECK(vswap_unlock(&S, 0, ub[0], 10 + 144 - 1) == VSWAP_ERR_LOCKED && L.seq == seq,
          "notice + period - 1: still locked");
    CHECK(vswap_unlock(&S, 0, ub[1], 10 + 144) == VSWAP_ERR_ARG &&
              vswap_unlock(NULL, 0, ub[0], 10 + 144) == VSWAP_ERR_ARG,
          "only the owner unlocks");
    CHECK(vswap_unlock(&S, 0, ub[0], 10 + 144) == VSWAP_OK && bal(ub[0]) == 900 &&
              bal(lockM) == 0 && !p->used && L.seq == seq + 1 && inv(),
          "notice + period: unlocked, every V-Bill back, nothing forfeited");
    CHECK(vswap_unlock(&S, 0, ub[0], 10 + 144) == VSWAP_ERR_ARG, "the slot is free");
    CHECK(vswap_notice(&S, 1, ub[0], 20) == VSWAP_OK &&
              vswap_unlock(&S, 1, ub[0], 20 + 6765 - 1) == VSWAP_ERR_LOCKED &&
              vswap_unlock(&S, 1, ub[0], 20 + 6765 + 9) == VSWAP_OK && bal(ub[0]) == 1000,
          "DEEP: 6765 ticks of notice");
    sw(JURIS, true, 3, GATE);
    CHECK(vswap_lock(&S, ub[0], uv[0], 1, 10, false, 30, NULL) == VSWAP_OK && S.pos[0].used,
          "the freed slot is reused; id is optional");
    CHECK(vswap_notice(&S, 0, ub[0], UINT64_MAX - 5) == VSWAP_OK &&
              vswap_unlock(&S, 0, ub[0], UINT64_MAX) == VSWAP_ERR_LOCKED,
          "a notice whose end does not fit 64 bits stays locked (fail closed)");
    pay_ledger_set_flags(&L, lockM, PAY_ACCT_FROZEN);
    S.pos[0].notice_tick = 0;
    CHECK(vswap_unlock(&S, 0, ub[0], 500) == VSWAP_ERR_LEDGER && S.pos[0].used,
          "a refused unlock keeps the position");
    pay_ledger_set_flags(&L, lockM, 0);

    /* the table fills */
    setup();
    vbi(ub[1], 100);
    uint32_t ok = 0;
    for (uint32_t i = 0; i < VSWAP_MAX_POS; i++)
        ok += vswap_lock(&S, ub[1], uv[1], 1 + (i & 1), 1, false, 1, &id) == VSWAP_OK && id == i;
    seq = L.seq;
    CHECK(ok == VSWAP_MAX_POS &&
              vswap_lock(&S, ub[1], uv[1], 1, 1, false, 1, &id) == VSWAP_ERR_FULL && L.seq == seq &&
              inv(),
          "64 positions; the 65th is refused before posting");
}

static void test_epoch_check(void)
{
    setup();
    vswap_quote_t q;
    vswap_swap_in(&S, uv[0], ub[0], 100, 1, &q);
    vswap_lock(&S, ub[0], uv[0], 1, 30, false, 1, NULL);
    vswap_lock(&S, ub[0], uv[0], 2, 20, false, 1, NULL);
    vswap_lock(&S, ub[0], uv[0], 2, 5, false, 1, NULL);
    CHECK(vswap_epoch_check(&S) == VSWAP_OK, "sums match");
    S.pos[2].vbills++;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a tampered DEEP position is caught");
    S.pos[2].vbills--;
    S.pos[0].vbills--;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a tampered MEDIUM position is caught");
    S.pos[0].vbills++;
    S.pos[1].tier = 1;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a position moved between tiers is caught");
    S.pos[1].tier = 0;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a position in tier LOW is caught");
    S.pos[1].tier = 3;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "an unknown tier is caught");
    S.pos[1].tier = 2;
    S.pos[1].used = false;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a dropped position is caught");
    S.pos[1].used = true;
    S.pos[1].vbills = UINT64_MAX;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "an overflowing sum is caught");
    S.pos[1].vbills = 20;
    CHECK(vswap_epoch_check(&S) == VSWAP_OK && vswap_epoch_check(NULL) == VSWAP_ERR_ARG,
          "restored: passes");
    L.acct[ub[3]].equity++;
    CHECK(vswap_epoch_check(&S) == VSWAP_ERR_STATE, "a broken ledger fails the epoch check");
    L.acct[ub[3]].equity--;
    vswap_t T = S;
    T.cfg.lock[2] = 999;
    CHECK(vswap_epoch_check(&T) == VSWAP_ERR_STATE, "a missing lock account fails");
}

/* holders: alice 10, bob 20 (LOW); carol MEDIUM 5; dave DEEP 4 */
static void div_setup(void)
{
    setup();
    vbi(ub[0], 10);
    vbi(ub[1], 20);
    vbi(ub[2], 5);
    vbi(ub[3], 4);
    vswap_lock(&S, ub[2], uv[2], 1, 5, false, 1, NULL);
    vswap_lock(&S, ub[3], uv[3], 2, 4, false, 1, NULL);
}

static void test_dividend(void)
{
    div_setup();
    vswap_holder_t h[3] = {{ub[0], uv[0]}, {ub[1], uv[1]}, {vault, loss}};
    uint64_t paid[3] = {7, 7, 7}, seq = L.seq;
    vswap_div_t d;
    CHECK(vswap_dividend(&S, pool, h, 2, false, 2, paid, &d) == VSWAP_ERR_EMPTY && L.seq == seq &&
              d.postings == 0 && d.pool == 0,
          "an empty pool pays nothing");
    fund(pool, 100);
    seq = L.seq;
    /* U = 10 + 20 + 5*2 + 4*3 = 52; floors 19, 38, 19, 23 (99); remainders
     * 12, 24, 12, 4: the leftover unit goes to bob */
    CHECK(vswap_dividend(&S, pool, h, 2, false, 2, paid, &d) == VSWAP_OK && d.pool == 100 &&
              d.units == 52 && d.paid == 100 && d.postings == 1 && d.reinvest == 0 && d.vbills == 0,
          "100 over 52 weight units in one posting");
    CHECK(paid[0] == 19 && paid[1] == 39 && paid[2] == 7 && S.pos[0].last_vino == 19 &&
              S.pos[1].last_vino == 23,
          "19, 39, 19, 23: floors plus the leftover by largest remainder");
    CHECK(bal(uv[0]) == 1000019 && bal(uv[1]) == 1000039 && bal(uv[2]) == 1000019 &&
              bal(uv[3]) == 1000023 && bal(pool) == 0 && L.seq == seq + 1 && inv(),
          "the whole pool moved, sum exact");
    CHECK(last()->n_lines == 5 && last()->kind == PAY_KIND_TRANSFER, "pool line + four payees");

    /* ties go to the lower index */
    div_setup();
    vswap_holder_t e[3] = {{ub[0], uv[0]}, {ub[4], uv[4]}, {ub[5], uv[5]}};
    vbi(ub[4], 10);
    vbi(ub[5], 10);
    vswap_notice(&S, 0, ub[2], 1);
    vswap_notice(&S, 1, ub[3], 1);
    vswap_unlock(&S, 0, ub[2], 1000);
    vswap_unlock(&S, 1, ub[3], 10000);
    fund(pool, 2);
    CHECK(vswap_dividend(&S, pool, e, 3, false, 3, paid, NULL) == VSWAP_OK && paid[0] == 1 &&
              paid[1] == 1 && paid[2] == 0,
          "2 over three equal holders: the first two get one each");
    fund(pool, 1);
    vbi(ub[5], 1); /* units 10, 10, 11: remainders 10, 10, 11 */
    CHECK(vswap_dividend(&S, pool, e, 3, false, 4, paid, NULL) == VSWAP_OK && paid[0] == 0 &&
              paid[1] == 0 && paid[2] == 1,
          "1 over 10/10/11: the largest remainder wins over the lower index");

    /* the vault: included by default, excluded by the flag */
    div_setup();
    vbi(vault, 30);
    vswap_holder_t v[2] = {{ub[0], uv[0]}, {vault, loss}};
    S.pos[0].used = S.pos[1].used = false; /* only holders here */
    move(lockM, ub[2], 5);
    move(lockD, ub[3], 4);
    fund(pool, 40);
    CHECK(vswap_dividend(&S, pool, v, 2, false, 5, paid, &d) == VSWAP_OK && paid[0] == 10 &&
              paid[1] == 30 && bal(loss) == 30 && d.units == 40,
          "included: the vault's 30 of 40 units take 30");
    fund(pool, 40);
    CHECK(vswap_dividend(&S, pool, v, 2, true, 6, paid, &d) == VSWAP_OK && paid[0] == 40 &&
              paid[1] == 0 && d.units == 10 && bal(loss) == 30,
          "excluded: all 40 to the other holder");
    vswap_holder_t z[1] = {{ub[4], uv[4]}};
    fund(pool, 5);
    seq = L.seq;
    CHECK(vswap_dividend(&S, pool, z, 1, false, 7, paid, &d) == VSWAP_ERR_NOHOLD && L.seq == seq,
          "no weight: nothing paid");
    CHECK(vswap_dividend(&S, pool, v, 2, true, 7, NULL, NULL) == VSWAP_OK &&
              bal(uv[0]) == 1000000 + 55,
          "paid and out are optional");
}

static void test_dividend_args(void)
{
    div_setup();
    fund(pool, 100);
    vswap_holder_t h[2] = {{ub[0], uv[0]}, {ub[1], uv[1]}};
    uint64_t seq = L.seq;
#define DIV(hh, n) vswap_dividend(&S, pool, hh, n, false, 1, NULL, NULL)
    CHECK(vswap_dividend(NULL, pool, h, 2, false, 1, NULL, NULL) == VSWAP_ERR_ARG &&
              DIV(NULL, 1) == VSWAP_ERR_ARG && DIV(h, VSWAP_MAX_HOLD + 1) == VSWAP_ERR_ARG,
          "missing arguments, too many holders");
    CHECK(vswap_dividend(&S, ub[0], h, 2, false, 1, NULL, NULL) == VSWAP_ERR_ARG &&
              vswap_dividend(&S, reserve, h, 2, false, 1, NULL, NULL) == VSWAP_ERR_ARG,
          "the pool must be a Vino account and never the reserve");
    vswap_holder_t b[][2] = {
        {{uv[0], uv[0]}, {ub[1], uv[1]}}, {{biss, uv[0]}, {ub[1], uv[1]}},
        {{lockM, uv[0]}, {ub[1], uv[1]}}, {{ub[0], ub[0]}, {ub[1], uv[1]}},
        {{ub[0], pool}, {ub[1], uv[1]}},  {{ub[0], uv[0]}, {ub[0], uv[1]}},
        {{ub[0], uv[0]}, {lockD, uv[1]}},
    };
    uint32_t refused = 0;
    for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++) refused += DIV(b[i], 2) == VSWAP_ERR_ARG;
    CHECK(refused == sizeof b / sizeof b[0] && L.seq == seq,
          "holders: wrong asset, the issuer, a lock account, a bad payee, the pool, duplicates");
    pay_ledger_set_flags(&L, pool, PAY_ACCT_FROZEN);
    CHECK(DIV(h, 2) == VSWAP_ERR_ARG, "a frozen pool is refused up front");
    pay_ledger_set_flags(&L, pool, 0);
    S.pos[1].payee = pool;
    CHECK(DIV(h, 2) == VSWAP_ERR_ARG, "a position paying into the pool is refused");
    S.pos[1].payee = uv[3];
    CHECK(DIV(h, 0) == VSWAP_OK && bal(uv[2]) == 1000045 && bal(uv[3]) == 1000055,
          "no holders listed: the positions alone (10 and 12 units of 22)");

    /* the pool above the line limit */
    div_setup();
    fund(pool, VSWAP_LINE_MAX);
    fund(pool, 1);
    CHECK(DIV(h, 2) == VSWAP_ERR_OVERFLOW, "a pool above 2^59 is refused");
    move(pool, loss, 1);
    CHECK(DIV(h, 2) == VSWAP_OK && bal(pool) == 0, "exactly 2^59 is paid");
    /* weight units that do not fit 64 bits */
    div_setup();
    S.cfg.weight[0] = 31; /* 31 * 2^59 < 2^64 <= 32 * 2^59 */
    vbi(ub[0], VSWAP_LINE_MAX);
    vbi(ub[1], VSWAP_LINE_MAX - 1);
    fund(pool, 10);
    CHECK(DIV(h, 1) == VSWAP_OK, "(2^59 + 10) * 31 fits 64 bits");
    fund(pool, 10);
    CHECK(DIV(h, 2) == VSWAP_ERR_OVERFLOW, "two of them do not");
    for (int i = 0; i < 3; i++) vbi(ub[0], VSWAP_LINE_MAX);
    fund(pool, 10);
    CHECK(DIV(h, 1) == VSWAP_ERR_OVERFLOW, "and one larger balance alone does not");
    /* the rate fails closed during a dividend */
    div_setup();
    fund(pool, 10);
    raw(vault, 1000, 0, biss2, 0, 1000, NULL);
    CHECK(DIV(h, 2) == VSWAP_ERR_STATE, "h > N stops the dividend");
#undef DIV
}

static void test_reinvest(void)
{
    div_setup();
    fund(reserve, 39); /* N = 39, R = 39: rate 1 */
    S.pos[0].reinvest = true;
    S.pos[1].reinvest = true;
    move(reserve, loss, 13); /* R = 26, B = 39: 2 Vino per 3 V-Bills */
    fund(pool, 100);
    vswap_holder_t h[2] = {{ub[0], uv[0]}, {ub[1], uv[1]}};
    uint64_t paid[2], seq = L.seq, r0 = R(), b0 = N();
    vswap_div_t d;
    /* shares 19, 39, 19, 23. carol reinvests 19: v = floor(19*39/26) = 28,
     * a' = ceil(28*26/39) = 19 (18.67), 0 left. dave 23: v = floor(23*39/26)
     * = 34, a' = ceil(34*26/39) = 23 (22.67), 0 left. */
    CHECK(vswap_dividend(&S, pool, h, 2, false, 9, paid, &d) == VSWAP_OK && d.reinvest == 42 &&
              d.vbills == 62 && d.paid == 58 && d.postings == 2,
          "two reinvesting positions: one reinvest posting, one payout posting");
    CHECK(S.pos[0].vbills == 33 && S.pos[0].last_vbills == 28 && S.pos[0].last_vino == 0 &&
              S.pos[1].vbills == 38 && S.pos[1].last_vbills == 34 && bal(lockM) == 33 &&
              bal(lockD) == 38 && bal(uv[2]) == 1000000 && paid[0] == 19 && paid[1] == 39,
          "the V-Bills join the same positions, in the lock accounts");
    CHECK(R() == 26 + 42 && N() == 39 + 62 && bal(pool) == 0 && L.seq == seq + 2 && inv(),
          "reserve and issuer moved; the epoch check passes");
    CHECK(R() * b0 >= r0 * N(), "reinvesting never lowers the ratio");

    /* a share that buys a V-Bill and leaves change, and one that buys none */
    div_setup();
    fund(reserve, 39 * 10); /* R/B = 390/39 = 10 Vino per V-Bill */
    S.pos[0].reinvest = true;
    S.pos[1].reinvest = true;
    fund(pool, 52 * 2 + 5); /* 109: shares 20.96.. -> floor 20, 41, 20, 25 */
    vswap_holder_t one[1] = {{ub[0], uv[0]}};
    uint64_t p1[1];
    seq = L.seq;
    CHECK(vswap_dividend(&S, pool, one, 1, false, 9, p1, &d) == VSWAP_OK && d.postings == 2,
          "change and dust");
    /* U = 10 + 10 + 12 = 32; 109*10/32 = 34 r 2, 34 r 2, 109*12/32 = 40 r 28;
     * sum 108, leftover to dave: 41. carol 34 -> 3 V-Bills for 30, 4 back;
     * dave 41 -> 4 for 40, 1 back. */
    CHECK(p1[0] == 34 && S.pos[0].last_vbills == 3 && S.pos[0].last_vino == 4 &&
              S.pos[1].last_vbills == 4 && S.pos[1].last_vino == 1 && d.reinvest == 70 &&
              d.paid == 39 && bal(uv[2]) == 1000004 && bal(uv[3]) == 1000001 && inv(),
          "34 buys 3 at 10 with 4 back; 41 buys 4 with 1 back");
    fund(pool, 3);
    CHECK(vswap_dividend(&S, pool, one, 1, false, 10, p1, &d) == VSWAP_OK && d.vbills == 0 &&
              d.postings == 1 && S.pos[0].last_vbills == 0 &&
              S.pos[0].last_vino + p1[0] + S.pos[1].last_vino == 3,
          "shares too small to buy a V-Bill are paid out whole");
    CHECK(vswap_quote_in(&S, 1, NULL) == VSWAP_ERR_ARG, "quote needs an output");

    /* the reinvest V-Bills would not fit a line */
    div_setup();
    S.cfg.par_den = (uint64_t) 1 << 56; /* B > 0 but R == 0: par 1 / 2^56 */
    S.pos[0].reinvest = true;
    fund(pool, 100);
    CHECK(vswap_dividend(&S, pool, h, 2, false, 9, NULL, &d) == VSWAP_ERR_OVERFLOW &&
              d.postings == 0 && bal(pool) == 100,
          "a reinvest past the line limit refuses the whole dividend");
    S.cfg.par_den = (uint64_t) 1 << 62; /* 19 * 2^62 does not fit 64 bits */
    CHECK(vswap_dividend(&S, pool, h, 2, false, 9, NULL, &d) == VSWAP_ERR_OVERFLOW &&
              d.postings == 0 && bal(pool) == 100,
          "a reinvest that does not fit 64 bits is refused, not paid out as dust");
    S.cfg.par_den = (uint64_t) 1 << 54; /* 19 and 23 * 2^54 fit 2^59, 42 * 2^54 not */
    S.pos[1].reinvest = true;
    CHECK(vswap_dividend(&S, pool, h, 2, false, 9, NULL, &d) == VSWAP_ERR_OVERFLOW,
          "and so does a sum past it");
    S.pos[1].reinvest = false;
    CHECK(vswap_dividend(&S, pool, h, 2, false, 9, NULL, &d) == VSWAP_OK &&
              S.pos[0].last_vbills == 19ull << 54 && inv(),
          "one alone fits");
}

static void test_batches(void)
{
    setup();
    vswap_holder_t h[10];
    uint64_t paid[10];
    for (int i = 0; i < 10; i++) {
        uint32_t b = open_acct(vbl, 300 + (uint32_t) i, 0);
        vbi(b, (uint64_t) i + 1); /* units 1..10, U = 55 */
        h[i] = (vswap_holder_t){b, uv[i % NU]};
    }
    fund(pool, 1000);
    uint64_t seq = L.seq;
    vswap_div_t d;
    CHECK(vswap_dividend(&S, pool, h, 10, false, 1, paid, &d) == VSWAP_OK && d.postings == 2 &&
              L.seq == seq + 2 && bal(pool) == 0 && d.paid == 1000 && inv(),
          "ten payees: two postings (7 + 3)");
    uint64_t sum = 0;
    for (int i = 0; i < 10; i++) sum += paid[i];
    /* 1000*k/55: floors 18 36 54 72 90 109 127 145 163 181 = 995, remainders
     * 10 20 30 40 50 5 15 25 35 45: the five leftover units go to k = 5, 10,
     * 4, 9, 3 */
    CHECK(sum == 1000 && paid[0] == 18 && paid[2] == 55 && paid[3] == 73 && paid[4] == 91 &&
              paid[5] == 109 && paid[8] == 164 && paid[9] == 182,
          "the exact split across batches");

    /* a later batch refused: the first stands and is reported */
    fund(pool, 1000);
    char e2e[40];
    snprintf(e2e, sizeof e2e, "VSWAP-%llu", (unsigned long long) S.seq + 1);
    raw(uv[0], 1, 0, viss, 0, 1, e2e); /* takes the second batch's e2e */
    CHECK(vswap_dividend(&S, pool, h, 10, false, 2, paid, &d) == VSWAP_ERR_PARTIAL &&
              d.postings == 1 && paid[0] == 18 && paid[6] == 127 && paid[7] == 0 && paid[9] == 0 &&
              d.paid == 18 + 36 + 55 + 73 + 91 + 109 + 127 && bal(pool) == 1000 - d.paid && inv(),
          "partial: batch one paid and reported, the rest still in the pool");
    /* the first posting refused: nothing at all */
    snprintf(e2e, sizeof e2e, "VSWAP-%llu", (unsigned long long) S.seq);
    raw(uv[0], 1, 0, viss, 0, 1, e2e);
    uint64_t pb = bal(pool);
    CHECK(vswap_dividend(&S, pool, h, 10, false, 3, paid, &d) == VSWAP_ERR_LEDGER &&
              d.postings == 0 && bal(pool) == pb && paid[0] == 0,
          "first batch refused: a plain ledger error, nothing moved");

    /* a refused reinvest posting refuses everything */
    div_setup();
    S.pos[0].reinvest = true;
    fund(pool, 100);
    snprintf(e2e, sizeof e2e, "VSWAP-%llu", (unsigned long long) S.seq);
    raw(uv[0], 1, 0, viss, 0, 1, e2e);
    vswap_holder_t two[2] = {{ub[0], uv[0]}, {ub[1], uv[1]}};
    CHECK(vswap_dividend(&S, pool, two, 2, false, 4, NULL, &d) == VSWAP_ERR_LEDGER &&
              d.postings == 0 && bal(pool) == 100 && S.pos[0].vbills == 5 &&
              S.pos[0].last_vbills == 0,
          "reinvest refused: nothing posted, positions unchanged");
    pay_ledger_set_flags(&L, reserve, PAY_ACCT_FROZEN); /* irrelevant to an inflow */
    CHECK(vswap_dividend(&S, pool, two, 2, false, 5, NULL, &d) == VSWAP_OK && d.postings == 2,
          "and the retry pays");
}

/* boundaries and details the other tests do not reach */
static void test_edges(void)
{
    setup();
    vswap_quote_t q;
    memset(&q, 0x77, sizeof q);
    CHECK(vswap_rate(&S, &q) == VSWAP_OK && q.vbills == 0 && q.vino == 0,
          "the rate leaves the amounts zero");
    CHECK(vswap_quote_in(&S, VSWAP_LINE_MAX, &q) == VSWAP_OK && q.vbills == VSWAP_LINE_MAX,
          "an offer of exactly 2^59 at par 1/1 is accepted");
    vbi(ub[0], 10);
    CHECK(vswap_quote_out(&S, VSWAP_LINE_MAX, &q) == VSWAP_ERR_FUNDS,
          "2^59 V-Bills is a valid amount, just more than B");
    CHECK(vswap_lock(&S, ub[0], uv[0], 1, VSWAP_LINE_MAX, false, 1, NULL) == VSWAP_ERR_FUNDS,
          "locking exactly 2^59 is a valid amount, just not held");
    fund(reserve, 20);
    uint64_t v0 = bal(uv[0]);
    CHECK(vswap_swap_out(&S, ub[0], uv[0], 5, 1, NULL) == VSWAP_OK && bal(uv[0]) == v0 + 10,
          "swap out without a quote output");
    vswap_t T = S;
    T.cfg.reserve = 999;
    CHECK(vswap_rate(&T, &q) == VSWAP_ERR_STATE, "a reserve account gone: fail closed");
    T = S;
    T.cfg.issuer = uv[0];
    CHECK(vswap_rate(&T, &q) == VSWAP_ERR_STATE, "an issuer of the wrong asset: fail closed");

    /* duplicates anywhere in the list; a position at slot 0 paying the pool */
    div_setup();
    fund(pool, 10);
    vswap_holder_t d3[3] = {{ub[0], uv[0]}, {ub[1], uv[1]}, {ub[1], uv[2]}};
    CHECK(vswap_dividend(&S, pool, d3, 3, false, 1, NULL, NULL) == VSWAP_ERR_ARG,
          "a duplicate at index 2 of index 1 is refused");
    S.pos[0].payee = pool;
    CHECK(vswap_dividend(&S, pool, d3, 2, false, 1, NULL, NULL) == VSWAP_ERR_ARG,
          "position 0 paying into the pool is refused");
    S.pos[0].payee = uv[2];

    /* exactly VSWAP_MAX_HOLD holders */
    setup();
    static vswap_holder_t many[VSWAP_MAX_HOLD];
    for (uint32_t i = 0; i < VSWAP_MAX_HOLD; i++)
        many[i] = (vswap_holder_t){open_acct(vbl, 400 + i, 0), uv[0]};
    vbi(many[63].vbill, 3);
    fund(pool, 9);
    uint64_t p64[VSWAP_MAX_HOLD];
    CHECK(vswap_dividend(&S, pool, many, VSWAP_MAX_HOLD, false, 1, p64, NULL) == VSWAP_OK &&
              p64[63] == 9 && p64[0] == 0,
          "64 holders is the maximum, not above it");

    /* exactly seven payees: one posting, no empty second one */
    setup();
    vswap_holder_t h7[7];
    for (uint32_t i = 0; i < 7; i++) {
        h7[i] = (vswap_holder_t){open_acct(vbl, 500 + i, 0), uv[i % NU]};
        vbi(h7[i].vbill, 1);
    }
    fund(pool, 70);
    vswap_div_t d;
    uint64_t seq = L.seq;
    CHECK(vswap_dividend(&S, pool, h7, 7, false, 1, NULL, &d) == VSWAP_OK && d.postings == 1 &&
              L.seq == seq + 1 && last()->n_lines == 8,
          "seven payees fill one posting exactly");

    /* last_vino / last_vbills are cleared for every position */
    div_setup();
    fund(pool, 100);
    vswap_holder_t one[1] = {{ub[0], uv[0]}};
    vswap_dividend(&S, pool, one, 1, false, 1, NULL, NULL);
    CHECK(S.pos[1].last_vino == 38, "dave's 12 of 32 units of 100: 37 plus the leftover");
    fund(pool, 1);
    vbi(ub[0], 100); /* alice's 110 units take the single unit */
    CHECK(vswap_dividend(&S, pool, one, 1, false, 2, NULL, NULL) == VSWAP_OK &&
              S.pos[1].last_vino == 0 && S.pos[0].last_vino == 0,
          "a later dividend that pays them nothing clears both");

    /* a reinvest of exactly 2^59 V-Bills; MEDIUM alone posts four lines */
    setup();
    vbi(ub[2], 5);
    vswap_lock(&S, ub[2], uv[2], 1, 5, true, 1, NULL);
    S.cfg.par_den = (uint64_t) 1 << 56; /* R == 0: par 1 / 2^56 */
    fund(pool, 8);
    seq = L.seq;
    CHECK(vswap_dividend(&S, pool, NULL, 0, false, 3, NULL, &d) == VSWAP_OK &&
              d.vbills == VSWAP_LINE_MAX && d.postings == 1 && L.seq == seq + 1 &&
              last()->n_lines == 4 && inv(),
          "8 Vino at 1/2^56 is exactly 2^59 V-Bills: allowed; no empty DEEP line");
}

int main(void)
{
    test_init();
    test_switch();
    test_par_and_rate();
    test_exact();
    test_parties();
    test_vault_and_loss();
    test_ratio_never_falls();
    test_lock();
    test_epoch_check();
    test_dividend();
    test_dividend_args();
    test_reinvest();
    test_batches();
    test_edges();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
