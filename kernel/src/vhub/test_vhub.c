/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vhub.c — the V-Bill hub demo (vhub.h H1-H6) on a real pay_ledger:
 * three test currencies, signed pegs, exact swaps in one posting, backing
 * limits, a capital-control limit, every rule outcome and the quarantine log.
 * The signature hook is a stand-in (SHA-256 keyed by the issuer's secret);
 * production passes ML-DSA-87 verification. */
#include <stdio.h>
#include <string.h>
#include "vhub.h"
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

static pay_ledger_t L;
static vhub_t H;
static lpx_qrec_t QS[2];
enum { A = 0, B = 1, C = 2 };
static uint16_t asset[3], vbl;
static uint32_t issuer[3], pool[3], vfl[3], vissuer, alice, bob, carol, dave;
static uint64_t nfund;

/* stand-in signature: SHA-256(secret(issuer) || msg) */
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

static pay_status_t issue(uint32_t from, uint32_t to, uint64_t amt)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    return pay_ledger_issue(&L, &r, from, to, amt, &rc);
}

static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}

static int64_t bal(uint32_t a)
{
    return pay_ledger_account(&L, a)->equity;
}

static vhub_status_t peg(uint8_t c, uint64_t num, uint64_t den, uint64_t epoch, uint32_t signer)
{
    uint8_t m[VHUB_PEG_MSG], sig[32];
    vhub_peg_msg(c, num, den, epoch, m);
    sign(signer, m, sizeof m, sig);
    return vhub_set_peg(&H, c, num, den, epoch, sig, 32);
}

static lpx_t rule_ok(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    (void) ctx;
    (void) sw;
    (void) side;
    return lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_POTENTIAL);
}
/* a rule whose verdict the test sets */
static lpx_t verdict;
static int verdict_side = -1;
static uint32_t seen_side[2];
static lpx_t rule_set(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    (void) ctx;
    (void) sw;
    seen_side[side & 1]++;
    if (verdict_side >= 0 && side != verdict_side)
        return lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    return verdict;
}

static vhub_swap_t sw(uint8_t f, uint8_t t, uint32_t from, uint32_t to, uint64_t amt)
{
    vhub_swap_t s;
    memset(&s, 0, sizeof s);
    s.from_ccy = f;
    s.to_ccy = t;
    s.sender = from;
    s.receiver = to;
    s.amount = amt;
    return s;
}

static void setup(void)
{
    static const uint8_t seed[32] = {1, 2, 3};
    pay_ledger_init(&L, NULL);
    const char *code[3] = {"ZTA", "ZTB", "ZTC"};
    for (int c = 0; c < 3; c++) {
        pay_ledger_add_asset(&L, code[c], 2, PAY_ASSET_UNIT, &asset[c]);
        issuer[c] = open_acct(asset[c], 10 + c, PAY_ACCT_ISSUER);
        pool[c] = open_acct(asset[c], 20 + c, 0);
    }
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    vissuer = open_acct(vbl, 30, PAY_ACCT_ISSUER);
    for (int c = 0; c < 3; c++) vfl[c] = open_acct(vbl, 40 + c, 0);
    alice = open_acct(asset[A], 50, 0);
    bob = open_acct(asset[B], 51, 0);
    carol = open_acct(asset[C], 52, 0);
    dave = open_acct(asset[A], 53, 0);
    issue(issuer[A], alice, 1000000);
    issue(issuer[A], dave, 1000000);
    issue(issuer[B], pool[B], 500000);
    issue(issuer[C], pool[C], 500000);
    issue(issuer[A], pool[A], 500000);
    for (int c = 0; c < 3; c++) issue(vissuer, vfl[c], 10000);
    vhub_init(&H, &L, vbl, verify, NULL, seed, QS, 2);
    for (int c = 0; c < 3; c++) vhub_add_ccy(&H, (uint8_t) c, asset[c], issuer[c], pool[c], vfl[c]);
}

static void test_pegs(void)
{
    setup();
    vhub_swap_t s = sw(A, B, alice, bob, 35000);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_ARG, "no swap before the pegs are set");
    CHECK(peg(A, 350, 1, 1, issuer[B]) == VHUB_ERR_SIG, "another issuer cannot set A's peg");
    CHECK(peg(A, 0, 1, 1, issuer[A]) == VHUB_ERR_ARG && peg(A, 1, 0, 1, issuer[A]) == VHUB_ERR_ARG,
          "zero num or den refused");
    CHECK(peg(A, 350, 1, 1, issuer[A]) == VHUB_OK, "A pegged at 350 per V-Bill");
    CHECK(peg(A, 351, 1, 1, issuer[A]) == VHUB_ERR_EPOCH, "same epoch refused");
    CHECK(peg(A, 351, 1, 0, issuer[A]) == VHUB_ERR_EPOCH, "older epoch refused");
    CHECK(H.ccy[A].num == 350 && H.ccy[A].peg_epoch == 1, "a refused update changes nothing");
    uint8_t m[VHUB_PEG_MSG], sig[32];
    vhub_peg_msg(A, 351, 1, 2, m);
    sign(issuer[A], m, sizeof m, sig);
    CHECK(vhub_set_peg(&H, A, 352, 1, 2, sig, 32) == VHUB_ERR_SIG,
          "a signature over other numbers is refused");
    sig[0] ^= 1;
    CHECK(vhub_set_peg(&H, A, 351, 1, 2, sig, 32) == VHUB_ERR_SIG, "a flipped bit is refused");
    CHECK(vhub_set_peg(&H, A, 351, 1, 2, sig, 31) == VHUB_ERR_SIG, "a short signature is refused");
    CHECK(vhub_set_peg(&H, 7, 1, 1, 1, sig, 32) == VHUB_ERR_ARG &&
              vhub_set_peg(&H, VHUB_MAX_CCY, 1, 1, 1, sig, 32) == VHUB_ERR_ARG &&
              vhub_set_peg(&H, A, 1, 1, 3, NULL, 32) == VHUB_ERR_ARG,
          "unknown slot or missing signature refused");
    CHECK(vhub_add_ccy(&H, A, 0, 0, 0, 0) == VHUB_ERR_ARG &&
              vhub_add_ccy(&H, VHUB_MAX_CCY, 0, 0, 0, 0) == VHUB_ERR_ARG,
          "a slot cannot be registered twice");
    uint8_t ref[VHUB_PEG_MSG];
    memcpy(ref, "zxv/vhub-peg/v1", 15);
    ref[15] = 2;
    for (int i = 0; i < 8; i++) {
        ref[16 + i] = (uint8_t) (0x0102030405060708ull >> (8 * i));
        ref[24 + i] = (uint8_t) (9ull >> (8 * i));
        ref[32 + i] = (uint8_t) (0xA0B0ull >> (8 * i));
    }
    vhub_peg_msg(2, 0x0102030405060708ull, 9, 0xA0B0, m);
    CHECK(!memcmp(m, ref, sizeof m), "the signed peg message is exactly the documented bytes");
}

static vhub_limit_t LIM;

static void ready(void)
{
    setup();
    peg(A, 350, 1, 1, issuer[A]); /* 350 A cents per V-Bill */
    peg(B, 120, 1, 1, issuer[B]); /* 120 B cents per V-Bill */
    peg(C, 9, 4, 1, issuer[C]);   /* 9 C cents per 4 V-Bills */
}

static void test_swaps(void)
{
    ready();
    uint64_t seq = L.seq;
    vhub_swap_t s = sw(A, B, alice, bob, 35000);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_WAIT && L.seq == seq,
          "no rules at all: wait, nothing posts");
    memset(&LIM, 0, sizeof LIM);
    LIM.limit = 100000;
    CHECK(vhub_add_rule(&H, A, vhub_rule_limit, vhub_limit_commit, &LIM) == VHUB_OK, "limit on A");
    CHECK(vhub_swap(&H, &s, 1) == VHUB_WAIT && L.seq == seq,
          "the receiving currency has no rule: still wait (fail closed)");
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    vhub_add_rule(&H, C, rule_ok, NULL, NULL);

    CHECK(vhub_swap(&H, &s, 1) == VHUB_OK && s.vbills == 100 && s.out == 12000,
          "35000 A -> 100 V-Bills -> 12000 B");
    CHECK(bal(alice) == 1000000 - 35000 && bal(pool[A]) == 500000 + 35000 &&
              bal(vfl[A]) == 10000 - 100 && bal(vfl[B]) == 10000 + 100 &&
              bal(pool[B]) == 500000 - 12000 && bal(bob) == 12000,
          "all six accounts moved in one posting");
    CHECK(L.seq == seq + 1 && pay_ledger_check(&L) && pay_ledger_verify_chain(&L),
          "one posting; every asset balanced; the chain verifies");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 6 && j->kind == PAY_KIND_TRADE && !strncmp(j->e2e, "VHUB-0", 6),
          "the journal entry has the six lines");

    s = sw(A, B, alice, bob, 35001);
    CHECK(vhub_swap(&H, &s, 2) == VHUB_ERR_INEXACT, "35001 A is not an exact V-Bill amount");
    s = sw(A, B, alice, bob, 35001);
    s.vbills = 7, s.out = 999; /* outputs only: stale values must not leak in */
    seq = L.seq;
    CHECK(vhub_swap(&H, &s, 2) == VHUB_ERR_INEXACT && L.seq == seq,
          "35001 A is refused at the first step whatever the output fields held");
    CHECK(vhub_quote(&H, A, B, 35001) == 35000 && vhub_quote(&H, A, B, 349) == 0,
          "the quote rounds down to the exact amount");

    /* round trip returns exactly what went out */
    s = sw(B, A, bob, alice, 12000);
    CHECK(vhub_swap(&H, &s, 3) == VHUB_OK && s.out == 35000 && bal(alice) == 1000000 &&
              bal(bob) == 0 && bal(vfl[A]) == 10000 && bal(vfl[B]) == 10000,
          "B -> A gives back exactly 35000: nothing leaks");

    /* the cross rate comes from the two pegs: A -> C direct equals A -> B -> C */
    s = sw(A, C, alice, carol, 3500);
    CHECK(vhub_swap(&H, &s, 4) == VHUB_ERR_INEXACT, "3500 A is 10 V-Bills, 22.5 C cents: refused");
    s = sw(A, C, alice, carol, 14000);
    CHECK(vhub_swap(&H, &s, 4) == VHUB_OK && s.vbills == 40 && s.out == 90,
          "14000 A -> 40 V-Bills -> 90 C");
    uint64_t direct = s.out;
    vhub_swap_t s1 = sw(A, B, alice, bob, 14000), s2;
    vhub_swap(&H, &s1, 5);
    s2 = sw(B, C, bob, carol, s1.out);
    CHECK(vhub_swap(&H, &s2, 6) == VHUB_OK && s2.out == direct,
          "A -> B -> C equals A -> C (one implied cross rate)");
    CHECK(vhub_quote(&H, A, C, 3499) == 2800 && vhub_quote(&H, A, A, 1000) == 0,
          "A -> C steps of 1400 (9/4 needs four V-Bills); same currency has no quote");
}

static void test_quote_brute(void)
{
    /* the quote against a brute-force search over small pegs */
    static const uint64_t pg[][2] = {{350, 1}, {120, 1}, {9, 4}, {6, 10}, {7, 3}, {1, 1}, {12, 18}};
    uint32_t n = sizeof pg / sizeof pg[0], bad = 0, cases = 0;
    for (uint32_t x = 0; x < n; x++)
        for (uint32_t y = 0; y < n; y++) {
            H.ccy[A].num = pg[x][0];
            H.ccy[A].den = pg[x][1];
            H.ccy[B].num = pg[y][0];
            H.ccy[B].den = pg[y][1];
            for (uint64_t want = 0; want <= 400; want++) {
                uint64_t best = 0;
                for (uint64_t a = want; a > 0; a--) {
                    if ((a * pg[x][1]) % pg[x][0]) continue;
                    uint64_t v = a * pg[x][1] / pg[x][0];
                    if ((v * pg[y][0]) % pg[y][1]) continue;
                    best = a;
                    break;
                }
                cases++;
                if (vhub_quote(&H, A, B, want) != best) bad++;
            }
        }
    CHECK(bad == 0, "quote equals brute force over 49 peg pairs x 401 amounts");
    printf("  quote brute force: %u cases, %u wrong\n", cases, bad);
}

static void test_limit_and_backing(void)
{
    ready();
    memset(&LIM, 0, sizeof LIM);
    LIM.limit = 105000; /* 300 V-Bills of A per sender per epoch */
    vhub_add_rule(&H, A, vhub_rule_limit, vhub_limit_commit, &LIM);
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    vhub_swap_t s = sw(A, B, alice, bob, 35000);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_OK, "first 35000 within the limit");
    s = sw(A, B, alice, bob, 77000);
    uint64_t seq = L.seq;
    CHECK(vhub_swap(&H, &s, 2) == VHUB_REFUSED && L.seq == seq &&
              lpx_base(H.last_tag) == LPX_B_FALSE,
          "35000 + 77000 passes the 105000 limit: refused, nothing posts");
    s = sw(A, B, alice, bob, 70000);
    CHECK(vhub_swap(&H, &s, 3) == VHUB_OK, "35000 + 70000 is exactly the limit: allowed");
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 4) == VHUB_REFUSED, "one V-Bill more is refused");
    s = sw(A, B, dave, bob, 105000);
    CHECK(vhub_swap(&H, &s, 5) == VHUB_OK, "the limit is per sender");
    s = sw(A, B, dave, bob, 350);
    CHECK(vhub_swap(&H, &s, 5) == VHUB_REFUSED, "and holds for the second sender too");
    s = sw(A, B, dave, bob, 105350);
    H.epoch++;
    CHECK(vhub_swap(&H, &s, 5) == VHUB_REFUSED, "an amount above the limit on its own is refused");
    H.epoch++;
    s = sw(A, B, alice, bob, 35000);
    CHECK(vhub_swap(&H, &s, 6) == VHUB_OK, "a new epoch starts every sender at zero");

    /* backing: B's V-Bill float holds what was deposited; A's float ran down */
    memset(&LIM, 0, sizeof LIM);
    LIM.limit = (uint64_t) 1 << 40;
    int64_t fa = bal(vfl[A]);
    s = sw(A, B, alice, bob, (uint64_t) (fa + 1) * 350);
    seq = L.seq;
    CHECK(vhub_swap(&H, &s, 7) == VHUB_ERR_LEDGER && H.last_ledger == PAY_ERR_FUNDS && L.seq == seq,
          "a swap past A's V-Bill backing is refused by the ledger");
    issue(vissuer, vfl[A], 100000);
    issue(issuer[A], alice, 40000000);
    s = sw(A, B, alice, bob, 350 * 5000);
    CHECK(vhub_swap(&H, &s, 8) == VHUB_ERR_LEDGER && bal(pool[B]) >= 0 && L.seq == seq + 2,
          "a payout past B's pool is refused: the peg pays only while backed");
    /* a frozen receiver: the ledger refuses the whole swap */
    pay_ledger_set_flags(&L, alice, PAY_ACCT_FROZEN);
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 9) == VHUB_ERR_LEDGER, "a frozen sender refuses the posting");
    pay_ledger_set_flags(&L, alice, 0);

    /* the limit's sender table: a 17th sender is refused, not untracked */
    ready();
    memset(&LIM, 0, sizeof LIM);
    LIM.limit = 1000000;
    vhub_add_rule(&H, A, vhub_rule_limit, vhub_limit_commit, &LIM);
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    uint32_t ok = 0;
    for (uint32_t i = 0; i < VHUB_LIMIT_SENDERS; i++) {
        uint32_t a = open_acct(asset[A], 100 + i, 0);
        issue(issuer[A], a, 350);
        s = sw(A, B, a, bob, 350);
        ok += vhub_swap(&H, &s, 10) == VHUB_OK;
    }
    uint32_t extra = open_acct(asset[A], 200, 0);
    issue(issuer[A], extra, 350);
    s = sw(A, B, extra, bob, 350);
    CHECK(ok == VHUB_LIMIT_SENDERS && vhub_swap(&H, &s, 11) == VHUB_REFUSED,
          "16 senders tracked; the 17th is refused");
    H.epoch++;
    CHECK(vhub_swap(&H, &s, 12) == VHUB_OK, "a new epoch clears the table");
    CHECK(vhub_rule_limit(NULL, &s, 0) == lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL),
          "no limit state: neither (wait)");
}

static void test_rules(void)
{
    ready();
    vhub_add_rule(&H, A, rule_set, NULL, NULL);
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    struct {
        uint8_t base, pol;
        vhub_status_t want;
        const char *what;
    } t[] = {
        {LPX_B_TRUE, LPX_P_NEUTRAL, VHUB_OK, "true/neutral proceeds"},
        {LPX_B_TRUE, LPX_P_ENTROPIC, VHUB_WAIT, "a weakening truth waits"},
        {LPX_B_BOTH, LPX_P_NEUTRAL, VHUB_ESCALATE, "both escalates to a person"},
        {LPX_B_BOTH, LPX_P_NEGENTROPIC, VHUB_WAIT, "a converging dispute waits"},
        {LPX_B_NEITHER, LPX_P_NEUTRAL, VHUB_WAIT, "neither waits"},
        {LPX_B_FALSE, LPX_P_NEUTRAL, VHUB_REFUSED, "false refuses"},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        verdict = lpx_make(t[i].base, t[i].pol, LPX_PH_POTENTIAL);
        vhub_swap_t s = sw(A, B, alice, bob, 350);
        uint64_t seq = L.seq;
        vhub_status_t r = vhub_swap(&H, &s, 1);
        CHECK(r == t[i].want && (r == VHUB_OK) == (L.seq == seq + 1), t[i].what);
    }
    /* the worst verdict wins across both currencies */
    vhub_add_rule(&H, B, rule_set, NULL, NULL);
    verdict_side = 1;
    verdict = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    vhub_swap_t s = sw(A, B, alice, bob, 350);
    memset(seen_side, 0, sizeof seen_side);
    CHECK(vhub_swap(&H, &s, 2) == VHUB_REFUSED && seen_side[0] == 1 && seen_side[1] == 1,
          "the receiving side's rule runs with side 1 and can refuse");
    verdict_side = -1;
    verdict = lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    CHECK(vhub_swap(&H, &s, 2) == VHUB_ESCALATE, "escalate on both sides stays escalate");

    /* forbidden: refused and quarantined; it outranks a refusal */
    verdict_side = 1;
    verdict = lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_EMERGING);
    uint64_t seq = L.seq;
    CHECK(vhub_swap(&H, &s, 3) == VHUB_FORBIDDEN && H.qlog.n == 1 && L.seq == seq,
          "forbidden: nothing posts, one quarantine record");
    CHECK(lpx_qlog_verify(&H.qlog) && lpx_forbidden(H.qlog.rec[0].tag) &&
              H.qlog.rec[0].submitter == alice,
          "the record names the sender and its chain verifies");
    CHECK(vhub_swap(&H, &s, 4) == VHUB_FORBIDDEN && H.qlog.n == 2, "second record");
    verdict = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    CHECK(vhub_swap(&H, &s, 5) == VHUB_HALTED && L.seq == seq,
          "a full quarantine log halts every swap (fail closed)");

    ready();
    CHECK(vhub_add_rule(&H, A, NULL, NULL, NULL) == VHUB_ERR_ARG &&
              vhub_add_rule(&H, 6, rule_ok, NULL, NULL) == VHUB_ERR_ARG,
          "a missing rule function or slot is refused");
    for (unsigned i = 0; i < VHUB_MAX_RULES; i++) vhub_add_rule(&H, A, rule_ok, NULL, NULL);
    CHECK(vhub_add_rule(&H, A, rule_ok, NULL, NULL) == VHUB_ERR_FULL, "rule table full");
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    s = sw(A, A, alice, alice, 350);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_ARG, "a swap into the same currency is refused");
    s = sw(A, B, alice, bob, 0);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_ARG, "a zero amount is refused");
    s = sw(A, 5, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_ARG && vhub_swap(NULL, &s, 1) == VHUB_ERR_ARG &&
              vhub_swap(&H, NULL, 1) == VHUB_ERR_ARG,
          "unknown currency or missing arguments refused");
    s = sw(A, B, alice, bob, PAY_BAL_MAX);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_ARG, "an amount at the ledger's maximum is refused");
    /* a peg change applies to the next swap */
    CHECK(peg(B, 240, 1, 2, issuer[B]) == VHUB_OK, "B's issuer halves B against the V-Bill");
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 2) == VHUB_OK && s.out == 240, "the next swap uses the new peg");
}

/* identifiers, the quarantine subject, severity order and commit hooks */
static uint32_t commits[2];
static void count_commit(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    (void) ctx;
    (void) sw;
    commits[side & 1]++;
}
static lpx_t verdict2;
static lpx_t rule_set2(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    (void) ctx;
    (void) sw;
    (void) side;
    return verdict2;
}

static void ref41(const uint8_t seed[32], uint8_t tag, uint64_t seq, uint8_t out[32])
{
    uint8_t in[41];
    memcpy(in, seed, 32);
    in[32] = tag;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (seq >> (8 * i));
    sha256(in, sizeof in, out);
}

static void test_details(void)
{
    static const uint8_t seed[32] = {1, 2, 3};
    ready();
    vhub_add_rule(&H, A, rule_ok, count_commit, NULL);
    vhub_add_rule(&H, B, rule_ok, count_commit, NULL);
    memset(commits, 0, sizeof commits);
    uint8_t want[32];
    char uetr[PAY_UETR_LEN + 1];
    for (uint64_t n = 0; n < 3; n++) {
        vhub_swap_t s = sw(A, B, alice, bob, 350);
        CHECK(H.seq == n && vhub_swap(&H, &s, 1) == VHUB_OK && H.seq == n + 1 && H.posted == n + 1,
              "one identifier and one posting per swap");
        const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
        ref41(seed, 'I', n, want);
        CHECK(!memcmp(j->idem_key, want, 32), "idempotency key = SHA-256(seed || I || n)");
        ref41(seed, 'U', n, want);
        pay_uetr_from_random(want, uetr);
        CHECK(!strcmp(j->uetr, uetr), "UETR from SHA-256(seed || U || n)");
    }
    CHECK(commits[0] == 3 && commits[1] == 3, "both sides' commit hooks run once per posted swap");
    vhub_swap_t s = sw(A, B, alice, bob, 351);
    vhub_swap(&H, &s, 1);
    CHECK(commits[0] == 3 && H.seq == 3, "a refused swap commits nothing and uses no identifier");

    /* init: a NULL seed is zeros; NULL hub is ignored */
    vhub_t z;
    static const uint8_t zero[32];
    memset(&z, 0x55, sizeof z);
    vhub_init(&z, &L, 7, verify, NULL, NULL, NULL, 0);
    CHECK(!memcmp(z.seed, zero, 32) && z.vbill == 7 && z.seq == 0 && z.L == &L && z.qlog.cap == 0,
          "init with no seed and no quarantine storage");
    vhub_init(NULL, &L, 0, verify, NULL, seed, NULL, 0);
    uint8_t seed2[32] = {1, 2, 3};
    seed2[31] = 9;
    vhub_init(&z, &L, 7, verify, NULL, seed2, NULL, 0);
    CHECK(!memcmp(z.seed, seed2, 32), "all 32 seed bytes are kept");

    /* severity: the worst verdict decides, whichever side and order */
    struct {
        uint8_t b1, p1, b2, p2;
        vhub_status_t want;
    } t[] = {
        {LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_B_FALSE, LPX_P_NEUTRAL, VHUB_REFUSED},
        {LPX_B_FALSE, LPX_P_NEUTRAL, LPX_B_NEITHER, LPX_P_NEUTRAL, VHUB_REFUSED},
        {LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_B_BOTH, LPX_P_NEUTRAL, VHUB_ESCALATE},
        {LPX_B_BOTH, LPX_P_NEUTRAL, LPX_B_NEITHER, LPX_P_NEUTRAL, VHUB_ESCALATE},
        {LPX_B_BOTH, LPX_P_NEUTRAL, LPX_B_FALSE, LPX_P_NEUTRAL, VHUB_REFUSED},
        {LPX_B_FALSE, LPX_P_NEUTRAL, LPX_B_BOTH, LPX_P_NEUTRAL, VHUB_REFUSED},
        {LPX_B_FALSE, LPX_P_NEUTRAL, LPX_B_TRUE, LPX_P_FORBIDDEN, VHUB_FORBIDDEN},
        {LPX_B_TRUE, LPX_P_FORBIDDEN, LPX_B_FALSE, LPX_P_NEUTRAL, VHUB_FORBIDDEN},
        {LPX_B_TRUE, LPX_P_NEUTRAL, LPX_B_TRUE, LPX_P_NEUTRAL, VHUB_OK},
    };
    for (unsigned k = 0; k < sizeof t / sizeof t[0]; k++) {
        ready();
        lpx_qrec_t q[1];
        lpx_qlog_init(&H.qlog, q, 1);
        vhub_add_rule(&H, A, rule_set, NULL, NULL);
        vhub_add_rule(&H, B, rule_set2, NULL, NULL);
        verdict_side = -1;
        verdict = lpx_make(t[k].b1, t[k].p1, LPX_PH_POTENTIAL);
        verdict2 = lpx_make(t[k].b2, t[k].p2, LPX_PH_EMERGING);
        s = sw(A, B, alice, bob, 350);
        vhub_status_t r = vhub_swap(&H, &s, 1);
        lpx_t worst_tag = (k % 2 == 0) ? verdict2 : verdict;
        if (t[k].want == VHUB_OK) worst_tag = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
        CHECK(r == t[k].want && H.last_tag == worst_tag, "the worst verdict and its tag win");
    }
    /* a refusal on one side outranks a missing rule on the other */
    ready();
    vhub_add_rule(&H, A, rule_set, NULL, NULL);
    verdict = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_REFUSED, "refused beats wait for a missing rule");
    verdict = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_WAIT &&
              H.last_tag == lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL),
          "a missing rule is neither: wait");

    /* the quarantine subject: SHA-256 of amount, epoch, sender, receiver, ccys */
    ready();
    vhub_add_rule(&H, A, rule_ok, NULL, NULL);
    vhub_add_rule(&H, B, rule_set, NULL, NULL);
    verdict_side = -1;
    verdict = lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_EMERGING);
    H.epoch = 0x0102030405060708ull;
    s = sw(A, B, 0x0A0B0C0D, 0x01020304, 700);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_FORBIDDEN, "forbidden");
    uint8_t m[26], subj[32];
    for (int i = 0; i < 8; i++) {
        m[i] = (uint8_t) (700ull >> (8 * i));
        m[8 + i] = (uint8_t) (0x0102030405060708ull >> (8 * i));
    }
    for (int i = 0; i < 4; i++) {
        m[16 + i] = (uint8_t) (0x0A0B0C0Du >> (8 * i));
        m[20 + i] = (uint8_t) (0x01020304u >> (8 * i));
    }
    m[24] = A;
    m[25] = B;
    sha256(m, sizeof m, subj);
    CHECK(H.qlog.n == 1 && !memcmp(H.qlog.rec[0].subject, subj, 32) && H.qlog.rec[0].reason == 1 &&
              H.qlog.rec[0].tag == verdict,
          "the quarantine record holds the documented subject, reason 1 and the rule's tag");

    /* the limit on the receiving currency only counts its outflows */
    ready();
    vhub_limit_t lb;
    memset(&lb, 0, sizeof lb);
    lb.limit = 120;
    vhub_add_rule(&H, A, rule_ok, NULL, NULL);
    vhub_add_rule(&H, B, vhub_rule_limit, vhub_limit_commit, &lb);
    s = sw(A, B, alice, bob, 3500);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_OK && s.out == 1200 && lb.n == 0,
          "B's limit ignores inflows (1200 in, limit 120, nothing tallied)");
    s = sw(B, A, bob, alice, 240);
    CHECK(vhub_swap(&H, &s, 2) == VHUB_REFUSED, "and refuses B outflows above it");
    s = sw(B, A, bob, alice, 120);
    CHECK(vhub_swap(&H, &s, 3) == VHUB_OK && lb.n == 1 && lb.used[0] == 120 && lb.sender[0] == bob,
          "an outflow at the limit posts and is tallied");
    /* a full table: commit records nothing more */
    vhub_limit_t full;
    memset(&full, 0, sizeof full);
    full.limit = 1000;
    full.n = VHUB_LIMIT_SENDERS;
    s.epoch = 0;
    vhub_limit_commit(&full, &s, 0);
    CHECK(full.n == VHUB_LIMIT_SENDERS, "commit on a full table adds nothing");
    vhub_limit_commit(NULL, &s, 0);
    vhub_limit_commit(&full, NULL, 0);

    /* V-Bill amounts at the ledger's limit are refused */
    ready();
    vhub_add_rule(&H, A, rule_ok, NULL, NULL);
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    H.ccy[A].num = 1;
    H.ccy[A].den = (uint64_t) 1 << 40;
    H.ccy[B].num = 1;
    H.ccy[B].den = 1;
    s = sw(A, B, alice, bob, (uint64_t) 1 << 22);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_INEXACT, "2^62 V-Bills is at the ledger maximum");
    s = sw(A, B, alice, bob, ((uint64_t) 1 << 22) - 1);
    uint64_t q0 = L.seq;
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_LEDGER && L.seq == q0,
          "just below it the hub tries, and the ledger refuses (no backing)");
    H.ccy[B].num = 1;
    H.ccy[B].den = 2; /* only the V-Bill amount is at the maximum */
    s = sw(A, B, alice, bob, (uint64_t) 1 << 22);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_INEXACT && s.out == (uint64_t) 1 << 61,
          "2^62 V-Bills is refused even when the payout is smaller");
    H.ccy[A].num = 1;
    H.ccy[A].den = 1;
    H.ccy[B].num = (uint64_t) 1 << 62;
    H.ccy[B].den = 1;
    s = sw(A, B, alice, bob, 1);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_ERR_INEXACT && s.vbills == 1,
          "a payout of 2^62 is refused on its own");

    /* two commit hooks on one currency both run */
    ready();
    vhub_add_rule(&H, A, rule_ok, count_commit, NULL);
    vhub_add_rule(&H, A, rule_ok, count_commit, NULL);
    vhub_add_rule(&H, B, rule_ok, NULL, NULL);
    memset(commits, 0, sizeof commits);
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_OK && commits[0] == 2 && commits[1] == 0,
          "every commit hook of a side runs");
    /* equal severities keep the first rule's tag */
    ready();
    vhub_add_rule(&H, A, rule_set, NULL, NULL);
    vhub_add_rule(&H, B, rule_set2, NULL, NULL);
    verdict_side = -1;
    verdict = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    verdict2 = lpx_make(LPX_B_FALSE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
    s = sw(A, B, alice, bob, 350);
    CHECK(vhub_swap(&H, &s, 1) == VHUB_REFUSED && H.last_tag == verdict,
          "of two refusals the first one is reported");
    /* a full sender table still serves the senders it holds */
    vhub_limit_t ft;
    memset(&ft, 0, sizeof ft);
    ft.limit = 1000;
    ft.n = VHUB_LIMIT_SENDERS;
    for (uint32_t i = 0; i < VHUB_LIMIT_SENDERS; i++) ft.sender[i] = 1000 + i;
    s = sw(A, B, 1000, bob, 350);
    CHECK(lpx_action(vhub_rule_limit(&ft, &s, 0)) == LPX_ACT_PROCEED,
          "the first tracked sender of a full table is still allowed");
    s.sender = 999;
    CHECK(lpx_action(vhub_rule_limit(&ft, &s, 0)) == LPX_ACT_REFUSE, "an untracked one is refused");
}

int main(void)
{
    test_pegs();
    test_swaps();
    test_quote_brute();
    test_limit_and_backing();
    test_rules();
    test_details();
    printf("test_vhub: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
