/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sicily.c — the Sicily Account view, Player Card tiers, the steward
 * threshold and org card programs (sicily.h S1-S7). The view runs on a real
 * pay_ledger with liquid, escrow and issuer accounts. The signature hook is a
 * stand-in (SHA-256 keyed by the signer's id); production passes ML-DSA
 * verification. */
#include <stdio.h>
#include <string.h>
#include "sicily.h"
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

/* stand-in signature: SHA-256(secret(who) || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[8 + 64];
    memset(b, 0, sizeof b);
    memcpy(b, &who, 4);
    b[4] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static uint32_t verifies;
static void *seen_ctx;
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    uint8_t want[32];
    verifies++;
    seen_ctx = ctx;
    if (sl != 32 || n > 64) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

/* ===================== S1: the view on a real ledger ===================== */
static pay_ledger_t L;
static uint64_t nfund;

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

static uint32_t open_acct(uint16_t as, uint32_t owner, pay_cap_t cap, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, cap, flags, &a);
    return a;
}

static void test_view(void)
{
    uint16_t sic, other;
    pay_ledger_init(&L, NULL);
    pay_ledger_add_asset(&L, "SIC", 2, PAY_ASSET_UNIT, &sic);
    pay_ledger_add_asset(&L, "OTH", 2, PAY_ASSET_UNIT, &other);
    enum { ME = 50, YOU = 51, BANK = 10 };
    uint32_t liq1 = open_acct(sic, ME, PAY_CAP_FINANCIAL, 0); /* account 0 is mine */
    uint32_t bank = open_acct(sic, BANK, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER);
    uint32_t bank_h = open_acct(sic, BANK, PAY_CAP_HUMAN, PAY_ACCT_ISSUER);
    uint32_t liq2 = open_acct(sic, ME, PAY_CAP_HUMAN, 0);
    uint32_t esc = open_acct(sic, ME, PAY_CAP_FINANCIAL, 0);
    uint32_t esc2 = open_acct(sic, ME, PAY_CAP_FINANCIAL, 0);
    uint32_t mine = open_acct(sic, ME, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER); /* my own issuance */
    uint32_t you = open_acct(sic, YOU, PAY_CAP_FINANCIAL, 0);
    uint32_t oth_bank = open_acct(other, BANK, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER);
    uint32_t oth_me = open_acct(other, ME, PAY_CAP_FINANCIAL, 0);
    CHECK(issue(bank, liq1, 70000) == PAY_OK, "fund liquid 1");
    CHECK(issue(bank_h, liq2, 5000) == PAY_OK, "fund liquid 2");
    CHECK(issue(bank, esc, 20000) == PAY_OK, "fund escrow");
    CHECK(liq1 == 0, "first account");
    CHECK(issue(mine, you, 30000) == PAY_OK, "I issue 300.00 of store credit");
    CHECK(issue(bank, you, 1234) == PAY_OK, "someone else's balance");
    CHECK(issue(oth_bank, oth_me, 999999) == PAY_OK, "another asset of mine");

    sic_view_t v;
    uint32_t el[1] = {esc};
    CHECK(sic_view(&L, ME, sic, el, 1, 400, 1000, &v) == SIC_OK, "view builds");
    CHECK(v.liquid == 75000 && v.escrow == 20000 && v.assets == 95000,
          "Neo-Roman: liquid over two capital forms plus escrow");
    CHECK(v.liabilities == 30000, "New California: my issuer account's CREDIT");
    CHECK(v.net_worth == 65000 && v.equity == 65000, "Sicilian Crown derived as A - L");
    CHECK(v.spendable == 74000 && v.pending_in == 400 && v.pending_out == 1000,
          "escrow is not spendable; pending in is shown, not counted");
    int64_t eq_sum = 0;
    for (uint32_t i = 0; i < L.n_accounts; i++)
        if (L.acct[i].owner == ME && L.acct[i].asset == sic) eq_sum += L.acct[i].equity;
    CHECK(eq_sum == v.equity, "derived equity equals the ledger's EQUITY rail");

    CHECK(sic_view(&L, ME, sic, NULL, 0, 0, 0, &v) == SIC_OK && v.liquid == 95000 &&
              v.escrow == 0 && v.spendable == 95000,
          "without the escrow list everything is liquid");
    CHECK(sic_view(&L, ME, sic, el, 1, 0, 80000, &v) == SIC_OK && v.spendable == 0 &&
              v.net_worth == 65000,
          "spendable floors at 0");
    CHECK(sic_view(&L, YOU, sic, NULL, 0, 0, 0, &v) == SIC_OK && v.liquid == 31234 &&
              v.liabilities == 0 && v.net_worth == 31234,
          "another owner's view");
    CHECK(sic_view(&L, ME, other, NULL, 0, 0, 0, &v) == SIC_OK && v.liquid == 999999,
          "another asset's view");
    CHECK(sic_view(&L, 77, sic, NULL, 0, 0, 0, &v) == SIC_OK && v.assets == 0 && v.net_worth == 0,
          "an owner with nothing");

    uint32_t two[2] = {esc2, esc};
    CHECK(issue(bank, esc2, 300) == PAY_OK, "fund a second escrow");
    CHECK(sic_view(&L, ME, sic, two, 2, 0, 0, &v) == SIC_OK && v.escrow == 20300 &&
              v.liquid == 75000,
          "two escrow accounts, the second listed last");
    CHECK(sic_view(&L, ME, sic, two, 1, 0, 0, &v) == SIC_OK && v.escrow == 300 && v.liquid == 95000,
          "only the listed ones are escrow");
    two[0] = esc;
    two[1] = you;
    CHECK(sic_view(&L, ME, sic, two, 2, 0, 0, &v) == SIC_ERR_ARG, "a bad second entry");
    {
        pay_posting_req_t r;
        pay_receipt_t rc;
        memset(&r, 0, sizeof r);
        nfund++;
        snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx",
                 (unsigned long long) nfund);
        snprintf(r.e2e, sizeof r.e2e, "BACK-%llu", (unsigned long long) nfund);
        memcpy(r.idem_key, &nfund, sizeof nfund);
        CHECK(pay_ledger_redeem(&L, &r, esc2, bank, 300, &rc) == PAY_OK, "return the 3.00");
    }
    uint32_t bad[1] = {you};
    v.liquid = 5;
    CHECK(sic_view(&L, ME, sic, bad, 1, 0, 0, &v) == SIC_ERR_ARG && v.liquid == 0,
          "escrow of another owner is refused, out zeroed");
    bad[0] = oth_me;
    CHECK(sic_view(&L, ME, sic, bad, 1, 0, 0, &v) == SIC_ERR_ARG, "escrow of another asset");
    bad[0] = 4000;
    CHECK(sic_view(&L, ME, sic, bad, 1, 0, 0, &v) == SIC_ERR_ARG, "escrow id not an account");
    uint32_t many[SIC_MAX_ESCROW + 1];
    for (uint32_t i = 0; i <= SIC_MAX_ESCROW; i++) many[i] = esc;
    CHECK(sic_view(&L, ME, sic, many, SIC_MAX_ESCROW + 1, 0, 0, &v) == SIC_ERR_ARG,
          "too many escrow ids");
    CHECK(sic_view(&L, ME, sic, many, SIC_MAX_ESCROW, 0, 0, &v) == SIC_OK && v.escrow == 20000,
          "a full list (repeats harmless)");
    CHECK(sic_view(&L, ME, sic, NULL, 1, 0, 0, &v) == SIC_ERR_ARG, "NULL list with a count");
    CHECK(sic_view(NULL, ME, sic, NULL, 0, 0, 0, &v) == SIC_ERR_ARG, "NULL ledger");
    CHECK(sic_view(&L, ME, sic, NULL, 0, 0, 0, NULL) == SIC_ERR_ARG, "NULL out");
    /* an inactive account is skipped */
    L.acct[liq2].active = false;
    CHECK(sic_view(&L, ME, sic, el, 1, 0, 0, &v) == SIC_OK && v.liquid == 70000,
          "inactive account skipped");
    L.acct[liq2].active = true;

    /* overflow of the sums (balances poked past what the ledger would hold) */
    static pay_ledger_t saved;
    saved = L;
    L.acct[esc].debit = 0;
    L.acct[liq1].debit = UINT64_MAX - 10;
    L.acct[liq2].debit = 10;
    CHECK(sic_view(&L, ME, sic, NULL, 0, 0, 0, &v) == SIC_ERR_OVERFLOW,
          "liquid sum fits but A - L does not");
    L.acct[liq2].debit = 11;
    CHECK(sic_view(&L, ME, sic, NULL, 0, 0, 0, &v) == SIC_ERR_OVERFLOW, "liquid sum overflows");
    L = saved;
    L.acct[esc].debit = UINT64_MAX;
    CHECK(sic_view(&L, ME, sic, el, 1, 0, 0, &v) == SIC_ERR_OVERFLOW, "escrow+liquid overflows");
    L = saved;
    L.acct[esc].debit = UINT64_MAX - 5;
    L.acct[liq1].debit = 0;
    L.acct[liq2].debit = 0;
    L.acct[mine].credit = UINT64_MAX;
    CHECK(sic_view(&L, ME, sic, el, 1, 0, 0, &v) == SIC_OK && v.escrow == UINT64_MAX - 5 &&
              v.net_worth == -5,
          "escrow sums on its own");
    L.acct[esc].credit = 1; /* not a real ledger state: CREDIT sum overflow */
    CHECK(sic_view(&L, ME, sic, el, 1, 0, 0, &v) == SIC_ERR_OVERFLOW, "credit sum overflows");
    L = saved;
}

static void test_view_compute(void)
{
    sic_view_t v;
    CHECK(sic_view_compute(100, 50, 30, 7, 99, &v) == SIC_OK && v.assets == 150 &&
              v.liabilities == 30 && v.net_worth == 120 && v.equity == 120 && v.spendable == 1 &&
              v.pending_in == 7 && v.pending_out == 99 && v.liquid == 100 && v.escrow == 50,
          "compute");
    CHECK(sic_view_compute(100, 0, 0, 0, 100, &v) == SIC_OK && v.spendable == 0,
          "pending equal to liquid");
    CHECK(sic_view_compute(100, 0, 0, 0, 101, &v) == SIC_OK && v.spendable == 0,
          "pending above liquid floors at 0");
    CHECK(sic_view_compute(0, 0, 0, 0, 0, &v) == SIC_OK && v.spendable == 0 && v.net_worth == 0,
          "zero");
    CHECK(sic_view_compute(10, 0, 30, 0, 0, &v) == SIC_OK && v.net_worth == -20 && v.equity == -20,
          "negative net worth");
    CHECK(sic_view_compute(30, 0, 30, 0, 0, &v) == SIC_OK && v.net_worth == 0, "even");
    const uint64_t M = (uint64_t) INT64_MAX;
    CHECK(sic_view_compute(M, 0, 0, 0, 0, &v) == SIC_OK && v.net_worth == INT64_MAX,
          "INT64_MAX net worth fits");
    CHECK(sic_view_compute(M, 1, 0, 0, 0, &v) == SIC_ERR_OVERFLOW && v.assets == 0,
          "one more does not");
    CHECK(sic_view_compute(M + 1, 0, 1, 0, 0, &v) == SIC_OK && v.net_worth == INT64_MAX,
          "large assets with liabilities");
    CHECK(sic_view_compute(0, 0, M, 0, 0, &v) == SIC_OK && v.net_worth == -INT64_MAX,
          "-INT64_MAX fits");
    CHECK(sic_view_compute(0, 0, M + 1, 0, 0, &v) == SIC_ERR_OVERFLOW, "below it does not");
    CHECK(sic_view_compute(1, 0, M + 1, 0, 0, &v) == SIC_OK && v.net_worth == -INT64_MAX,
          "liabilities minus assets");
    CHECK(sic_view_compute(UINT64_MAX, 1, 0, 0, 0, &v) == SIC_ERR_OVERFLOW, "A overflows");
    CHECK(sic_view_compute(UINT64_MAX, 0, UINT64_MAX - 3, 0, 0, &v) == SIC_OK && v.net_worth == 3 &&
              v.assets == UINT64_MAX,
          "A at UINT64_MAX is fine when L is close");
    CHECK(sic_view_compute(1, 1, 1, 1, 1, NULL) == SIC_ERR_ARG, "NULL out");
}

/* ===================== S2 / S3: tiers ===================== */
static void test_tiers(void)
{
    sic_tier_table_t d;
    sic_tier_table_default(&d);
    sic_tier_table_default(NULL);
    static const uint64_t C[4] = {100, 250, 500, 800}, B[4] = {1000, 10000, 100000, 1000000};
    static const uint32_t U[4] = {9000, 9500, 9800, 9950};
    for (int k = 0; k < 4; k++) {
        CHECK(d.t[k].cred == C[k] && d.t[k].bond == B[k] && d.t[k].uptime_bps == U[k],
              "default table");
        sic_tier_t want = (sic_tier_t) (k + 1), below = (sic_tier_t) k;
        CHECK(sic_tier_eval(NULL, C[k], B[k], U[k], 0) == want, "all three exactly at threshold");
        CHECK(sic_tier_eval(&d, C[k], B[k], U[k], 0) == want, "explicit default table");
        CHECK(sic_tier_eval(NULL, C[k] - 1, B[k], U[k], 0) == below, "credibility x-1");
        CHECK(sic_tier_eval(NULL, C[k], B[k] - 1, U[k], 0) == below, "bond x-1");
        CHECK(sic_tier_eval(NULL, C[k], B[k], U[k] - 1, 0) == below, "uptime x-1");
    }
    CHECK(sic_tier_eval(NULL, 0, 0, 0, 0) == SIC_TIER_COPPER, "nothing is Copper");
    CHECK(sic_tier_eval(NULL, UINT64_MAX, UINT64_MAX, 10000, UINT64_MAX) == SIC_TIER_PLATINUM,
          "everything is Platinum, never Curzi");
    CHECK(sic_tier_eval(NULL, UINT64_MAX, UINT64_MAX, 10001, 0) == SIC_TIER_COPPER,
          "malformed uptime fails closed");
    /* a tier needs every tier below it */
    sic_tier_table_t odd = d;
    odd.t[0].cred = 1000;
    CHECK(sic_tier_eval(&odd, 999, B[3], U[3], 0) == SIC_TIER_COPPER,
          "the climb stops at the first rung not met");
    CHECK(sic_tier_eval(&odd, 1000, B[3], U[3], 0) == SIC_TIER_PLATINUM, "then continues");
    odd = d;
    odd.t[3].uptime_bps = 9000;
    CHECK(sic_tier_eval(&odd, 800, B[3], 9799, 0) == SIC_TIER_SILVER,
          "a low Platinum bar does not skip Gold");

    /* giving cap: at most floor(cred/3), so at most 1/4 of the total */
    CHECK(sic_effective_cred(300, 99) == 399, "giving below the cap counts fully");
    CHECK(sic_effective_cred(300, 100) == 400, "giving at the cap");
    CHECK(sic_effective_cred(300, 101) == 400, "giving above the cap is capped");
    CHECK(sic_effective_cred(302, 1000) == 402, "cap is floor(cred/3)");
    CHECK(sic_effective_cred(0, 1000) == 0, "giving alone counts for nothing");
    CHECK(sic_effective_cred(UINT64_MAX - 1, 0) == UINT64_MAX - 1, "no giving");
    CHECK(sic_effective_cred(UINT64_MAX - 10, 10) == UINT64_MAX, "exactly fits");
    CHECK(sic_effective_cred(UINT64_MAX - 10, 11) == UINT64_MAX, "saturates");
    CHECK(sic_effective_cred(UINT64_MAX - 10, 20) == UINT64_MAX, "saturates, never wraps");
    int capped = 1;
    for (uint64_t c = 0; c < 3000; c++) {
        uint64_t e = sic_effective_cred(c, UINT64_MAX), g = e - c;
        if (g * 4 > e || g != c / 3) capped = 0;
    }
    CHECK(capped, "giving is never above a quarter of the effective score");
    CHECK(sic_tier_eval(NULL, 75, 1000, 9000, 25) == SIC_TIER_BRONZE, "75 + 25 reaches Bronze");
    CHECK(sic_tier_eval(NULL, 75, 1000, 9000, 24) == SIC_TIER_COPPER, "75 + 24 does not");
    CHECK(sic_tier_eval(NULL, 74, 1000, 9000, 1000) == SIC_TIER_COPPER,
          "74 + capped 24 does not: giving > cred/3 ignored");
    CHECK(sic_tier_eval(NULL, 600, 1000000, 9950, 200) == SIC_TIER_PLATINUM, "600 + 200");
    CHECK(sic_tier_eval(NULL, 599, 1000000, 9950, 1000) == SIC_TIER_GOLD, "599 + 199");

    static const char *const nm[6][2] = {{"Copper", "Obsidian"}, {"Bronze", "Carnelian"},
                                         {"Silver", "Jade"},     {"Gold", "Lapis"},
                                         {"Platinum", "Ruby"},   {"Curzi", "Admas"}};
    for (int t = 0; t < 6; t++)
        CHECK(!strcmp(sic_tier_name((sic_tier_t) t, 0), nm[t][0]) &&
                  !strcmp(sic_tier_name((sic_tier_t) t, 1), nm[t][1]),
              "tier names");
    CHECK(!strcmp(sic_tier_name(SIC_TIER_COUNT, 0), ""), "out of range tier");
    CHECK(!strcmp(sic_tier_name(SIC_TIER_GOLD, 2), ""), "out of range name");

    const uint32_t br = SIC_PERK_FREE_REGISTRATION | SIC_PERK_STOREFRONTS,
                   si = br | SIC_PERK_FORWARDS | SIC_PERK_IP_NOTES, pl = si | SIC_PERK_XB_LICENSING;
    CHECK(sic_tier_perks(SIC_TIER_COPPER) == 0, "Copper has no perks");
    CHECK(sic_tier_perks(SIC_TIER_BRONZE) == br, "Bronze: registration, storefronts");
    CHECK(sic_tier_perks(SIC_TIER_SILVER) == si, "Silver adds forwards and IP notes");
    CHECK(sic_tier_perks(SIC_TIER_GOLD) == si, "Gold adds none");
    CHECK(sic_tier_perks(SIC_TIER_PLATINUM) == pl, "Platinum adds cross-border licensing");
    CHECK(sic_tier_perks(SIC_TIER_CURZI) == pl, "Curzi carries Platinum's set, nothing more");
    CHECK(sic_tier_perks(SIC_TIER_COUNT) == 0, "out of range");
}

/* ===================== S4: stewards ===================== */
static sic_steward_t S;
static const uint32_t KEYS[5] = {101, 102, 103, 104, 105}; /* 0 is the founder */
static int ctxv;

static sic_status_t act(uint8_t op, uint32_t id, uint32_t steward, uint32_t signer, uint64_t tick)
{
    const sic_proposal_t *p = 0;
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++)
        if (S.p[i].id == id && S.p[i].state != SIC_PROP_FREE) p = &S.p[i];
    uint8_t m[SIC_STEWARD_MSG], sig[32], params[32] = {0};
    sic_steward_msg(op, id, p ? p->kind : 1, p ? p->params : params, m);
    sign(signer, m, sizeof m, sig);
    return op == SIC_STEWARD_OP_APPROVE ? sic_steward_approve(&S, id, steward, sig, 32, tick)
                                        : sic_steward_cancel(&S, id, steward, sig, 32, tick);
}
static sic_status_t ap(uint32_t id, uint32_t st, uint64_t tick)
{
    return act(SIC_STEWARD_OP_APPROVE, id, st, KEYS[st], tick);
}
static sic_status_t cn(uint32_t id, uint32_t st, uint64_t tick)
{
    return act(SIC_STEWARD_OP_CANCEL, id, st, KEYS[st], tick);
}

static uint32_t prop(uint8_t kind, uint64_t tick)
{
    uint8_t h[32];
    memset(h, kind, sizeof h);
    h[0] = (uint8_t) tick;
    uint32_t id = 0;
    sic_steward_propose(&S, kind, h, tick, &id);
    return id;
}

static void test_steward_init(void)
{
    uint32_t k[5];
    memcpy(k, KEYS, sizeof k);
    CHECK(sic_steward_init(NULL, k, 0, 0, verify, NULL) == SIC_ERR_ARG, "NULL s");
    CHECK(sic_steward_init(&S, NULL, 0, 0, verify, NULL) == SIC_ERR_ARG, "NULL keys");
    CHECK(sic_steward_init(&S, k, 0, 0, NULL, NULL) == SIC_ERR_ARG, "NULL verify");
    CHECK(sic_steward_init(&S, k, 5, 0, verify, NULL) == SIC_ERR_ARG, "founder index 5");
    CHECK(sic_steward_init(&S, k, 4, 0, verify, NULL) == SIC_OK && S.founder == 4, "index 4");
    CHECK(sic_steward_init(&S, k, 0, SIC_TIMELOCK, verify, NULL) == SIC_ERR_ARG,
          "a window of 7 days could never execute");
    CHECK(sic_steward_init(&S, k, 0, SIC_TIMELOCK + 1, verify, NULL) == SIC_OK &&
              S.window == SIC_TIMELOCK + 1,
          "a window just above 7 days");
    CHECK(sic_steward_init(&S, k, 0, 0, verify, NULL) == SIC_OK && S.window == SIC_WINDOW_DEFAULT &&
              S.window == 60ull * 86400 && S.next_id == 1,
          "default window 60 days");
    for (uint32_t i = 0; i < 5; i++)
        for (uint32_t j = i + 1; j < 5; j++) {
            memcpy(k, KEYS, sizeof k);
            k[j] = k[i];
            CHECK(sic_steward_init(&S, k, 0, 0, verify, NULL) == SIC_ERR_ARG,
                  "the founder may not hold two keys");
        }
    CHECK(sic_steward_init(&S, KEYS, 0, 0, verify, &ctxv) == SIC_OK, "init");
    for (uint32_t i = 0; i < 5; i++) CHECK(S.key[i] == KEYS[i], "keys stored");

    uint8_t m[SIC_STEWARD_MSG], ph[32];
    for (int i = 0; i < 32; i++) ph[i] = (uint8_t) (0xC0 + i);
    sic_steward_msg('A', 0x01020304u, 3, ph, m);
    CHECK(!memcmp(m, "zxv/sic-steward/v1", 18) && m[18] == 'A' && m[19] == 4 && m[20] == 3 &&
              m[21] == 2 && m[22] == 1 && m[23] == 3 && !memcmp(m + 24, ph, 32),
          "steward message layout");
}

static void test_steward(void)
{
    const uint64_t D = SIC_DAY, T0 = 1000;
    sic_steward_init(&S, KEYS, 0, 0, verify, &ctxv);
    uint8_t h[32] = {7};
    h[31] = 0x31;
    uint32_t id = 99;
    CHECK(sic_steward_propose(&S, 0, h, T0, &id) == SIC_ERR_ARG && id == 99, "kind 0");
    CHECK(sic_steward_propose(&S, 5, h, T0, &id) == SIC_ERR_ARG, "kind 5");
    CHECK(sic_steward_propose(&S, 1, NULL, T0, &id) == SIC_ERR_ARG, "NULL params");
    CHECK(sic_steward_propose(NULL, 1, h, T0, &id) == SIC_ERR_ARG, "NULL s");
    CHECK(sic_steward_propose(&S, SIC_ACT_MASTER_SEED, h, T0, &id) == SIC_OK && id == 1,
          "first id is 1");
    CHECK(sic_steward_propose(&S, SIC_ACT_ARBITRATION, h, T0, NULL) == SIC_OK,
          "NULL out_id allowed");
    CHECK(sic_steward_state(&S, 2, T0) == SIC_PROP_OPEN, "second id is 2");
    CHECK(!memcmp(S.p[0].params, h, 32) && S.p[0].kind == 1 && S.p[1].kind == 4 &&
              S.p[0].proposed == T0 && S.p[0].approved == 0 && S.p[0].cancel == 0,
          "the proposal is stored as given");
    CHECK(sic_steward_state(&S, 1, T0) == SIC_PROP_OPEN && sic_steward_approvals(&S, 1) == 0 &&
              ap(1, 4, T0) == SIC_OK && sic_steward_approvals(&S, 1) == 1,
          "id 1 is a real proposal");

    /* the founder alone */
    uint32_t a = prop(SIC_ACT_TOP_ROUTING, T0);
    CHECK(a == 3, "third id");
    seen_ctx = 0;
    CHECK(ap(a, 0, T0) == SIC_OK && seen_ctx == &ctxv, "founder approves; ctx passed");
    CHECK(sic_steward_execute(&S, a, T0 + 8 * D) == SIC_ERR_THRESHOLD,
          "founder alone cannot execute after the time lock");
    CHECK(ap(a, 0, T0 + 1) == SIC_DUPLICATE && sic_steward_approvals(&S, a) == 1,
          "a repeated founder approval is not counted");
    CHECK(sic_steward_execute(&S, a, T0 + 30 * D) == SIC_ERR_THRESHOLD, "nor at day 30");
    CHECK(ap(a, 1, T0 + 2) == SIC_OK && sic_steward_approvals(&S, a) == 2, "2 of 5");
    CHECK(sic_steward_execute(&S, a, T0 + 30 * D) == SIC_ERR_THRESHOLD,
          "2 approvals at day 30 still cannot execute");
    CHECK(ap(a, 1, T0 + 3) == SIC_DUPLICATE && sic_steward_approvals(&S, a) == 2,
          "duplicate of a second steward");
    /* forged and misdirected signatures */
    CHECK(act(SIC_STEWARD_OP_APPROVE, a, 2, KEYS[0], T0 + 4) == SIC_ERR_SIG,
          "founder's key signing as steward 2 is refused");
    CHECK(act(SIC_STEWARD_OP_CANCEL, a, 2, KEYS[2], T0 + 4) == SIC_OK &&
              sic_steward_state(&S, a, T0 + 4) == SIC_PROP_OPEN &&
              sic_steward_approvals(&S, a) == 2,
          "a cancel vote is not an approval");
    uint8_t m[SIC_STEWARD_MSG], sig[32];
    sic_steward_msg(SIC_STEWARD_OP_APPROVE, a, SIC_ACT_TOP_ROUTING, h, m); /* wrong params */
    sign(KEYS[2], m, sizeof m, sig);
    CHECK(sic_steward_approve(&S, a, 2, sig, 32, T0 + 4) == SIC_ERR_SIG, "wrong params hash");
    const sic_proposal_t *pa = 0;
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++)
        if (S.p[i].id == a) pa = &S.p[i];
    sic_steward_msg(SIC_STEWARD_OP_APPROVE, a, SIC_ACT_MASTER_SEED, pa->params, m);
    sign(KEYS[2], m, sizeof m, sig);
    CHECK(sic_steward_approve(&S, a, 2, sig, 32, T0 + 4) == SIC_ERR_SIG, "wrong kind");
    sic_steward_msg(SIC_STEWARD_OP_APPROVE, a + 1, SIC_ACT_TOP_ROUTING, pa->params, m);
    sign(KEYS[2], m, sizeof m, sig);
    CHECK(sic_steward_approve(&S, a, 2, sig, 32, T0 + 4) == SIC_ERR_SIG, "wrong id");
    sic_steward_msg(SIC_STEWARD_OP_APPROVE, a, SIC_ACT_TOP_ROUTING, pa->params, m);
    sign(KEYS[2], m, sizeof m, sig);
    CHECK(sic_steward_approve(&S, a, 2, sig, 31, T0 + 4) == SIC_ERR_SIG, "short signature");
    CHECK(sic_steward_approve(&S, a, 2, NULL, 32, T0 + 4) == SIC_ERR_ARG, "NULL signature");
    CHECK(sic_steward_approve(&S, a, 5, sig, 32, T0 + 4) == SIC_ERR_ARG, "steward index 5");
    CHECK(sic_steward_approve(NULL, a, 2, sig, 32, T0 + 4) == SIC_ERR_ARG, "NULL s");
    CHECK(sic_steward_approvals(&S, a) == 2, "none of those counted");
    CHECK(sic_steward_approve(&S, a, 2, sig, 32, T0 - 1) == SIC_ERR_ARG,
          "a tick before the proposal");
    CHECK(sic_steward_approve(&S, a, 2, sig, 32, T0 + 4) == SIC_OK &&
              sic_steward_approvals(&S, a) == 3,
          "3 of 5");
    CHECK(sic_steward_execute(&S, a, T0 + 7 * D - 1) == SIC_ERR_TIMELOCK,
          "3 approvals one tick before 7 days");
    CHECK(sic_steward_execute(&S, a, T0 - 1) == SIC_ERR_ARG, "execute before proposal tick");
    CHECK(sic_steward_execute(&S, a, T0) == SIC_ERR_TIMELOCK, "at the proposal tick");
    CHECK(sic_steward_state(&S, a, T0 + 7 * D) == SIC_PROP_OPEN, "still open");
    CHECK(sic_steward_execute(&S, a, T0 + 7 * D) == SIC_OK, "3 of 5 at exactly 7 days");
    CHECK(sic_steward_state(&S, a, T0 + 7 * D) == SIC_PROP_EXECUTED, "executed");
    CHECK(sic_steward_execute(&S, a, T0 + 7 * D) == SIC_ERR_STATE, "only once");
    CHECK(ap(a, 3, T0 + 7 * D) == SIC_ERR_STATE, "no approvals after execution");
    CHECK(sic_steward_execute(&S, 999, T0) == SIC_ERR_NOT_FOUND, "unknown id");
    CHECK(sic_steward_execute(&S, 0, T0) == SIC_ERR_NOT_FOUND, "id 0");
    CHECK(sic_steward_execute(NULL, a, T0) == SIC_ERR_ARG, "NULL s");
    CHECK(sic_steward_state(&S, 999, T0) == SIC_PROP_FREE && sic_steward_approvals(&S, 999) == 0,
          "unknown id reads FREE");
    CHECK(sic_steward_state(NULL, 1, T0) == SIC_PROP_FREE && sic_steward_approvals(NULL, 1) == 0,
          "NULL reads FREE");

    /* four approvals including the founder, any three suffice */
    uint32_t b = prop(SIC_ACT_BASKET_PARAMS, T0);
    CHECK(ap(b, 2, T0) == SIC_OK && ap(b, 3, T0) == SIC_OK && ap(b, 4, T0) == SIC_OK,
          "three non-founder stewards");
    CHECK(sic_steward_execute(&S, b, T0 + 10 * D) == SIC_OK, "execute without the founder");

    /* cancel by three */
    uint32_t c = prop(SIC_ACT_MASTER_SEED, T0);
    CHECK(ap(c, 0, T0) == SIC_OK && ap(c, 1, T0) == SIC_OK && ap(c, 2, T0) == SIC_OK, "approved");
    CHECK(cn(c, 3, T0 + 1) == SIC_OK && cn(c, 4, T0 + 1) == SIC_OK, "two cancel votes");
    CHECK(cn(c, 4, T0 + 2) == SIC_DUPLICATE, "a duplicate cancel vote is not counted");
    CHECK(sic_steward_state(&S, c, T0 + 2) == SIC_PROP_OPEN, "two cancels do not cancel");
    CHECK(cn(c, 0, T0 + 3) == SIC_OK, "third cancel vote (the founder's)");
    CHECK(sic_steward_state(&S, c, T0 + 3) == SIC_PROP_CANCELLED, "three cancel");
    CHECK(sic_steward_execute(&S, c, T0 + 8 * D) == SIC_ERR_STATE,
          "a cancelled proposal cannot execute despite 3 approvals");
    CHECK(cn(c, 1, T0 + 4) == SIC_ERR_STATE, "nor be voted on");
    /* an approval signature cannot be replayed as a cancel */
    uint32_t r = prop(SIC_ACT_ARBITRATION, T0);
    const sic_proposal_t *pr = 0;
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++)
        if (S.p[i].id == r) pr = &S.p[i];
    sic_steward_msg(SIC_STEWARD_OP_APPROVE, r, pr->kind, pr->params, m);
    sign(KEYS[1], m, sizeof m, sig);
    CHECK(sic_steward_cancel(&S, r, 1, sig, 32, T0) == SIC_ERR_SIG, "approve sig is not a cancel");
    CHECK(sic_steward_approve(&S, r, 1, sig, 32, T0) == SIC_OK, "it is an approval");
    CHECK(cn(r, 1, T0) == SIC_OK && pr->cancel == 2 && pr->approved == 2,
          "the same steward may vote both ways; tallies are separate");

    /* expiry */
    uint32_t e = prop(SIC_ACT_TOP_ROUTING, T0);
    CHECK(ap(e, 0, T0) == SIC_OK && ap(e, 1, T0) == SIC_OK, "two approvals");
    const uint64_t W = SIC_WINDOW_DEFAULT;
    CHECK(sic_steward_state(&S, e, T0 + W - 1) == SIC_PROP_OPEN, "open one tick before window");
    CHECK(sic_steward_state(&S, e, T0 + W) == SIC_PROP_EXPIRED, "expired at the window");
    CHECK(sic_steward_state(&S, e, T0 - 1) == SIC_PROP_OPEN, "an earlier tick reads open");
    CHECK(ap(e, 2, T0 + W - 1) == SIC_OK, "third approval just in time");
    CHECK(sic_steward_execute(&S, e, T0 + W) == SIC_ERR_EXPIRED, "too late to execute");
    CHECK(sic_steward_state(&S, e, T0) == SIC_PROP_EXPIRED, "the expiry is recorded");
    CHECK(sic_steward_execute(&S, e, T0 + W - 1) == SIC_ERR_STATE, "and final");
    uint32_t f = prop(SIC_ACT_TOP_ROUTING, T0);
    CHECK(cn(f, 1, T0 + W) == SIC_ERR_EXPIRED, "no cancel votes after expiry either");
    uint32_t g = prop(SIC_ACT_TOP_ROUTING, T0);
    CHECK(ap(g, 0, T0) == SIC_OK && ap(g, 1, T0) == SIC_OK && ap(g, 2, T0) == SIC_OK, "3 of 5");
    CHECK(sic_steward_execute(&S, g, T0 + W - 1) == SIC_OK, "last tick of the window works");

    /* proposals near the end of time never wrap */
    const uint64_t END = UINT64_MAX - 10;
    uint32_t z = prop(SIC_ACT_TOP_ROUTING, END);
    CHECK(ap(z, 0, UINT64_MAX) == SIC_OK && ap(z, 1, UINT64_MAX) == SIC_OK &&
              ap(z, 2, UINT64_MAX) == SIC_OK,
          "approvals at the last tick");
    CHECK(sic_steward_execute(&S, z, UINT64_MAX) == SIC_ERR_TIMELOCK,
          "10 ticks are not 7 days even at the end of time");
    CHECK(sic_steward_state(&S, z, UINT64_MAX) == SIC_PROP_OPEN, "and it has not expired");
}

static void test_steward_slots(void)
{
    const uint64_t T0 = 5000;
    sic_steward_init(&S, KEYS, 0, SIC_TIMELOCK + 100, verify, NULL);
    uint32_t ids[SIC_MAX_PROPOSALS];
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++) ids[i] = prop(SIC_ACT_BASKET_PARAMS, T0 + i);
    CHECK(ids[0] == 1 && ids[SIC_MAX_PROPOSALS - 1] == SIC_MAX_PROPOSALS, "16 open proposals");
    uint8_t h[32] = {1};
    uint32_t id = 0;
    CHECK(sic_steward_propose(&S, 1, h, T0 + 50, &id) == SIC_ERR_FULL && id == 0,
          "a 17th is refused while all are open");
    CHECK(sic_steward_propose(&S, 1, h, T0 + SIC_TIMELOCK + 99, &id) == SIC_ERR_FULL,
          "one tick before the first expires");
    CHECK(sic_steward_propose(&S, 1, h, T0 + SIC_TIMELOCK + 100, &id) == SIC_OK && id == 17,
          "an expired slot is reused");
    CHECK(sic_steward_state(&S, 1, T0) == SIC_PROP_FREE, "the old id is gone");
    CHECK(sic_steward_state(&S, 2, T0 + 1) == SIC_PROP_OPEN, "the next is kept");
    /* a finished slot is reused at any time */
    CHECK(ap(2, 0, T0 + 1) == SIC_OK && ap(2, 1, T0 + 1) == SIC_OK && ap(2, 2, T0 + 1) == SIC_OK &&
              sic_steward_execute(&S, 2, T0 + 1 + SIC_TIMELOCK) == SIC_OK,
          "execute id 2");
    CHECK(sic_steward_propose(&S, 1, h, T0 + 60, &id) == SIC_OK && id == 18,
          "an executed slot is reused");
    CHECK(sic_steward_propose(&S, 1, h, T0 + 60, &id) == SIC_ERR_FULL, "full again");
    /* a slot proposed at a later tick than the caller's is not expired */
    sic_steward_init(&S, KEYS, 0, SIC_TIMELOCK + 100, verify, NULL);
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++) prop(SIC_ACT_BASKET_PARAMS, 1000000);
    CHECK(sic_steward_propose(&S, 1, h, 0, &id) == SIC_ERR_FULL,
          "proposals from the future are not expired");
    /* the id counter never wraps to 0 */
    sic_steward_init(&S, KEYS, 0, 0, verify, NULL);
    S.next_id = UINT32_MAX;
    CHECK(sic_steward_propose(&S, 1, h, 1, &id) == SIC_OK && id == UINT32_MAX, "last id");
    CHECK(sic_steward_propose(&S, 1, h, 1, &id) == SIC_ERR_ARG, "ids exhausted");
}

/* ===================== S5-S7: org programs ===================== */
static sic_org_t O;
enum { ORG = 7, SIGNER = 900 };

static sic_status_t open_org(sic_org_t *o, uint8_t scale, uint64_t num, uint64_t den,
                             uint64_t deposit, const uint64_t *floors)
{
    uint8_t m[SIC_ORG_MSG], sig[32];
    sic_org_manifest_msg(ORG, scale, num, den, 1, m);
    sign(SIGNER, m, sizeof m, sig);
    return sic_org_open(o, ORG, SIGNER, scale, num, den, 1, deposit, floors, sig, 32, verify,
                        &ctxv);
}

static void test_org_open(void)
{
    uint8_t m[SIC_ORG_MSG], sig[32];
    sic_org_manifest_msg(0x0A0B0C0Du, 3, 0x1122334455667788ull, 9, 0x01020304u, m);
    CHECK(!memcmp(m, "zxv/sic-org/v1", 14) && m[14] == 0x0D && m[17] == 0x0A && m[18] == 3 &&
              m[19] == 0x88 && m[26] == 0x11 && m[27] == 9 && m[34] == 0 && m[35] == 4 &&
              m[38] == 1,
          "manifest layout");
    CHECK(sic_org_floor_default[0] == 1000 && sic_org_floor_default[1] == 10000 &&
              sic_org_floor_default[2] == 100000 && sic_org_floor_default[3] == 1000000 &&
              sic_org_floor_default[4] == 10000000,
          "default floors");
    for (uint8_t s = 0; s < SIC_ORG_SCALES; s++) {
        uint64_t f = sic_org_floor_default[s];
        CHECK(open_org(&O, s, 1, 10, f - 1, NULL) == SIC_ERR_FUNDS, "deposit below the floor");
        CHECK(open_org(&O, s, 1, 10, f, NULL) == SIC_OK && O.floor == f && O.escrow == f &&
                  O.scale == s,
              "deposit at the floor");
    }
    CHECK(open_org(&O, SIC_ORG_SCALES, 1, 10, 1u << 30, NULL) == SIC_ERR_ARG, "scale 5");
    CHECK(open_org(&O, 0, 1, 0, 5000, NULL) == SIC_ERR_ARG, "den 0");
    CHECK(open_org(&O, 0, 11, 10, 5000, NULL) == SIC_ERR_ARG, "sweep above 100%");
    CHECK(open_org(&O, 0, 10, 10, 5000, NULL) == SIC_OK, "sweep of 100%");
    CHECK(open_org(&O, 0, 0, 10, 5000, NULL) == SIC_OK, "no sweep");
    const uint64_t fl[SIC_ORG_SCALES] = {5, 6, 7, 8, 9};
    CHECK(open_org(&O, 2, 1, 3, 6, fl) == SIC_ERR_FUNDS, "operator floors");
    CHECK(open_org(&O, 2, 1, 3, 7, fl) == SIC_OK && O.floor == 7, "operator floor met");
    seen_ctx = 0;
    CHECK(open_org(&O, 1, 3, 7, 20000, NULL) == SIC_OK && O.org_id == ORG && O.signer == SIGNER &&
              O.version == 1 && O.sweep_num == 3 && O.sweep_den == 7 && O.owed == 0 &&
              O.sweep_rem == 0 && O.n_members == 0 && seen_ctx == &ctxv,
          "opened");
    /* the manifest signature binds every field */
    sic_org_manifest_msg(ORG, 1, 3, 7, 1, m);
    sign(SIGNER, m, sizeof m, sig);
    sic_org_t t;
    CHECK(sic_org_open(&t, ORG, SIGNER, 1, 4, 7, 1, 20000, NULL, sig, 32, verify, NULL) ==
              SIC_ERR_SIG,
          "a different sweep rate");
    CHECK(sic_org_open(&t, ORG, SIGNER, 1, 3, 7, 2, 20000, NULL, sig, 32, verify, NULL) ==
              SIC_ERR_SIG,
          "a different version");
    CHECK(sic_org_open(&t, ORG, SIGNER + 1, 1, 3, 7, 1, 20000, NULL, sig, 32, verify, NULL) ==
              SIC_ERR_SIG,
          "a different signer");
    CHECK(sic_org_open(&t, ORG, SIGNER, 1, 3, 7, 1, 20000, NULL, NULL, 32, verify, NULL) ==
              SIC_ERR_ARG,
          "NULL sig");
    CHECK(sic_org_open(&t, ORG, SIGNER, 1, 3, 7, 1, 20000, NULL, sig, 32, NULL, NULL) ==
              SIC_ERR_ARG,
          "NULL verify");
    CHECK(sic_org_open(NULL, ORG, SIGNER, 1, 3, 7, 1, 20000, NULL, sig, 32, verify, NULL) ==
              SIC_ERR_ARG,
          "NULL org");
    CHECK(sic_org_open(&t, ORG, SIGNER, 1, 3, 7, 1, 20000, NULL, sig, 32, verify, NULL) == SIC_OK,
          "the signed manifest");
}

static void test_org_sweep(void)
{
    open_org(&O, 0, 3, 7, 1000, NULL);
    uint64_t sum_net = 0, sum_fee = 0, sum_gross = 0, sum_swept = 0;
    int each = 1;
    for (uint64_t i = 0; i < 2000; i++) {
        uint64_t g = i * 1237 + (i % 13) * 99991 + 11, fee = 0, sw = 0;
        if (sic_org_sweep(&O, g, &fee, &sw) != SIC_OK) each = 0;
        if (fee != pay_assure_fee(g)) each = 0;
        sum_gross += g;
        sum_fee += fee;
        sum_net += g - fee;
        sum_swept += sw;
        if (sum_swept != sum_net * 3 / 7 || O.sweep_rem != sum_net * 3 % 7) each = 0;
    }
    CHECK(each, "every prefix sweeps exactly floor(sum(net) * 3 / 7), fee taken first");
    CHECK(O.swept_total == sum_swept && O.gross_total == sum_gross && O.fee_total == sum_fee &&
              O.escrow == 1000 + sum_swept,
          "totals");
    CHECK(sum_fee > 0, "the fee was really taken");
    uint64_t fee = 7, sw = 7;
    open_org(&O, 0, 1, 3, 1000, NULL);
    CHECK(sic_org_sweep(&O, 1, &fee, &sw) == SIC_OK && fee == 0 && sw == 0 && O.sweep_rem == 1,
          "1 unit: no fee, carried");
    CHECK(sic_org_sweep(&O, 1, NULL, NULL) == SIC_OK && O.sweep_rem == 2 && O.escrow == 1000,
          "2 carried");
    CHECK(sic_org_sweep(&O, 1, &fee, &sw) == SIC_OK && sw == 1 && O.sweep_rem == 0 &&
              O.escrow == 1001,
          "the third unit sweeps one");
    CHECK(sic_org_sweep(&O, 1125, &fee, &sw) == SIC_OK && fee == 1 && sw == 374 && O.sweep_rem == 2,
          "fee before the sweep: (1125 - 1) / 3");
    CHECK(sic_org_sweep(&O, 0, &fee, &sw) == SIC_OK && fee == 0 && sw == 0, "a zero sale");
    open_org(&O, 0, 0, 1, 1000, NULL);
    CHECK(sic_org_sweep(&O, 1000000, &fee, &sw) == SIC_OK && sw == 0 && fee == 888 &&
              O.escrow == 1000,
          "no sweep");
    open_org(&O, 0, 5, 5, 1000, NULL);
    CHECK(sic_org_sweep(&O, 1000000, &fee, &sw) == SIC_OK && sw == 1000000 - 888 &&
              O.escrow == 1000 + 999112,
          "100% sweeps the whole net");
    /* overflow refuses and changes nothing */
    open_org(&O, 0, 1, 1, 1000, NULL);
    O.escrow = pay_assure_fee(UINT64_MAX) + 1;
    sic_org_t before = O;
    CHECK(sic_org_sweep(&O, UINT64_MAX, &fee, &sw) == SIC_ERR_OVERFLOW &&
              !memcmp(&before, &O, sizeof O),
          "escrow would overflow");
    O.escrow = pay_assure_fee(UINT64_MAX);
    CHECK(sic_org_sweep(&O, UINT64_MAX, &fee, &sw) == SIC_OK &&
              sw == UINT64_MAX - pay_assure_fee(UINT64_MAX) && O.escrow == UINT64_MAX,
          "the largest sale fills the escrow exactly");
    open_org(&O, 0, 0, 1, 1000, NULL);
    CHECK(sic_org_sweep(&O, UINT64_MAX, NULL, NULL) == SIC_OK, "gross total at max");
    CHECK(sic_org_sweep(&O, 1, NULL, NULL) == SIC_ERR_OVERFLOW && O.gross_total == UINT64_MAX,
          "gross total would overflow");
    CHECK(sic_org_sweep(&O, 0, NULL, NULL) == SIC_OK, "zero still fits");
    /* corrupted state is refused */
    open_org(&O, 0, 1, 3, 1000, NULL);
    O.sweep_rem = 3;
    CHECK(sic_org_sweep(&O, 10, NULL, NULL) == SIC_ERR_ARG, "remainder at den");
    O.sweep_rem = 2;
    CHECK(sic_org_sweep(&O, 10, NULL, NULL) == SIC_OK && O.sweep_rem == 0 && O.escrow == 1004,
          "remainder below den");
    O.sweep_den = 0;
    O.sweep_rem = 0;
    CHECK(sic_org_sweep(&O, 10, NULL, NULL) == SIC_ERR_ARG, "den 0");
    O.sweep_den = 1;
    O.sweep_num = UINT64_MAX;
    O.escrow = 0;
    CHECK(sic_org_sweep(&O, 3, &fee, &sw) == SIC_ERR_OVERFLOW, "quotient above 64 bits");
    O.sweep_num = 3;
    CHECK(sic_org_sweep(&O, 3, &fee, &sw) == SIC_OK && sw == 9, "a corrupt rate fitting 64 bits");
    CHECK(sic_org_sweep(NULL, 3, NULL, NULL) == SIC_ERR_ARG, "NULL org");
    CHECK(sic_org_deposit(NULL, 1) == SIC_ERR_ARG, "deposit NULL");
    O.escrow = UINT64_MAX - 1;
    CHECK(sic_org_deposit(&O, 1) == SIC_OK && O.escrow == UINT64_MAX, "deposit to the max");
    CHECK(sic_org_deposit(&O, 1) == SIC_ERR_OVERFLOW && O.escrow == UINT64_MAX, "overflow");
}

static void test_org_rewards(void)
{
    /* floor 1000; escrow 1500 */
    open_org(&O, 0, 1, 10, 1500, NULL);
    const uint32_t U1 = 501, U2 = 502;
    CHECK(sic_org_award(&O, U1, 10) == SIC_ERR_MEMBER, "users opt in first");
    CHECK(sic_org_redeem(&O, 0, 1) == SIC_ERR_MEMBER && sic_org_leave(&O, 0) == SIC_ERR_MEMBER,
          "user 0 is not a member of an empty program");
    CHECK(sic_org_join(&O, U1) == SIC_OK && sic_org_join(&O, U2) == SIC_OK && O.n_members == 2,
          "two members");
    CHECK(sic_org_join(&O, U1) == SIC_OK && O.n_members == 2, "joining twice adds no entry");
    CHECK(sic_org_award(&O, U1, 0) == SIC_ERR_ARG, "zero points");
    CHECK(!sic_org_paused(&O), "not paused");
    CHECK(sic_org_award(&O, U1, 301) == SIC_OK && O.owed == 301, "award");
    CHECK(sic_org_award(&O, U2, 200) == SIC_ERR_PAUSED, "1000 + 301 + 200 > 1500");
    CHECK(sic_org_award(&O, U2, 199) == SIC_OK && O.owed == 500 && O.escrow == 1500,
          "exactly at floor + owed + points");
    CHECK(!sic_org_paused(&O), "escrow == floor + owed is not paused");
    CHECK(sic_org_award(&O, U2, 1) == SIC_ERR_PAUSED && sic_org_points(&O, U2) == 199,
          "one more pauses");
    /* sales lift the escrow, rewards resume */
    uint64_t sw = 0;
    CHECK(sic_org_sweep(&O, 2000, NULL, &sw) == SIC_OK && sw == 199 && O.escrow == 1699,
          "sweep (2000 - 1) / 10");
    CHECK(sic_org_award(&O, U2, 199) == SIC_OK && sic_org_points(&O, U2) == 398 && O.owed == 699,
          "rewards resume");
    /* the escrow falls below the floor: new rewards pause, earned ones are paid */
    O.escrow = 900; /* e.g. an off-module loss the operator mirrors in */
    CHECK(sic_org_paused(&O), "below the floor: paused");
    CHECK(sic_org_award(&O, U1, 1) == SIC_ERR_PAUSED, "no new rewards");
    CHECK(sic_org_redeem(&O, U1, 302) == SIC_ERR_FUNDS, "not more than earned");
    CHECK(sic_org_redeem(&O, U1, 301) == SIC_OK && O.escrow == 599 && O.owed == 398 &&
              sic_org_points(&O, U1) == 0,
          "earned points are paid below the floor");
    CHECK(sic_org_redeem(&O, U2, 0) == SIC_ERR_ARG, "zero redeem");
    CHECK(sic_org_redeem(&O, U2, 1) == SIC_OK && sic_org_points(&O, U2) == 397, "redeem one");
    CHECK(sic_org_redeem(&O, 999, 1) == SIC_ERR_MEMBER, "unknown user");
    CHECK(sic_org_redeem(&O, U2, 100) == SIC_OK && sic_org_points(&O, U2) == 297 && O.owed == 297 &&
              O.escrow == 498,
          "partial redeem");
    O.escrow = 296; /* corrupt: escrow below what is owed */
    CHECK(sic_org_redeem(&O, U2, 297) == SIC_ERR_FUNDS, "never more than escrow holds");
    CHECK(sic_org_redeem(&O, U2, 296) == SIC_OK && O.escrow == 0 && O.owed == 1, "up to escrow");
    CHECK(sic_org_redeem(NULL, U2, 1) == SIC_ERR_ARG, "NULL org");

    /* leaving: no new awards, earned points kept, root tier untouched */
    open_org(&O, 0, 1, 10, 5000, NULL);
    sic_tier_t before = sic_tier_eval(NULL, 300, 20000, 9600, 10);
    sic_org_join(&O, U1);
    CHECK(sic_org_award(&O, U1, 1) == SIC_OK && sic_org_points(&O, U1) == 1, "award one");
    CHECK(sic_org_award(&O, U1, 49) == SIC_OK, "award");
    CHECK(sic_org_leave(&O, U1) == SIC_OK, "leave");
    CHECK(sic_tier_eval(NULL, 300, 20000, 9600, 10) == before && before == SIC_TIER_SILVER,
          "leaving keeps the root tier (no org input reaches it)");
    CHECK(sic_org_points(&O, U1) == 50 && O.owed == 50, "earned points kept");
    CHECK(sic_org_award(&O, U1, 1) == SIC_ERR_MEMBER, "no awards after leaving");
    CHECK(sic_org_leave(&O, U1) == SIC_ERR_MEMBER, "leaving twice");
    CHECK(sic_org_leave(&O, 77) == SIC_ERR_MEMBER, "leaving unknown");
    CHECK(sic_org_redeem(&O, U1, 50) == SIC_OK && O.owed == 0, "a leaver still redeems");
    CHECK(sic_org_join(&O, U1) == SIC_OK && sic_org_award(&O, U1, 5) == SIC_OK &&
              sic_org_points(&O, U1) == 5 && O.n_members == 1,
          "rejoining reuses the entry");
    CHECK(sic_org_leave(&O, U1) == SIC_OK && sic_org_join(&O, U1) == SIC_OK &&
              sic_org_points(&O, U1) == 5,
          "rejoining keeps earned points");
    CHECK(sic_org_join(NULL, 1) == SIC_ERR_ARG && sic_org_leave(NULL, 1) == SIC_ERR_ARG &&
              sic_org_award(NULL, 1, 1) == SIC_ERR_ARG,
          "NULL org");
    CHECK(sic_org_points(NULL, 1) == 0 && sic_org_points(&O, 12345) == 0 && sic_org_paused(NULL),
          "points of nothing");
    /* the member table */
    open_org(&O, 0, 1, 10, 5000, NULL);
    for (uint32_t i = 0; i < SIC_ORG_MAX_MEMBERS; i++) sic_org_join(&O, 1000 + i);
    CHECK(O.n_members == SIC_ORG_MAX_MEMBERS, "full");
    CHECK(sic_org_join(&O, 5000) == SIC_ERR_FULL, "one more is refused");
    CHECK(sic_org_join(&O, 1000 + SIC_ORG_MAX_MEMBERS - 1) == SIC_OK, "a member rejoins");
    CHECK(sic_org_award(&O, 1000 + SIC_ORG_MAX_MEMBERS - 1, 3) == SIC_OK &&
              sic_org_points(&O, 1000 + SIC_ORG_MAX_MEMBERS - 1) == 3,
          "the last member is found");
    /* overflowing gates pause rather than wrap */
    O.floor = UINT64_MAX;
    O.escrow = UINT64_MAX;
    CHECK(sic_org_paused(&O), "floor + owed overflow reads paused");
    CHECK(sic_org_award(&O, 1000, 1) == SIC_ERR_PAUSED, "floor + owed overflow");
    O.floor = 0;
    O.owed = 3;
    O.escrow = UINT64_MAX;
    CHECK(!sic_org_paused(&O), "large escrow");
    CHECK(sic_org_award(&O, 1000, UINT64_MAX - 3) == SIC_OK, "need exactly UINT64_MAX");
    O.owed = 3;
    CHECK(sic_org_award(&O, 1000, UINT64_MAX - 2) == SIC_ERR_PAUSED, "need + points overflows");
}

int main(void)
{
    test_view();
    test_view_compute();
    test_tiers();
    test_steward_init();
    test_steward();
    test_steward_slots();
    test_org_open();
    test_org_sweep();
    test_org_rewards();
    printf("test_sicily: PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
