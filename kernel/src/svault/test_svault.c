/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_svault.c — the protocol solvency vault (svault.h S1-S6) on a real
 * pay_ledger: fee payments through pay_ledger_pay_with_fee feed the four
 * bucket accounts, the vault sweeps a third of the reserve share with an
 * exact carry, buys V-Bills under the 1/3 cap with fee cash only, is left out
 * of the redemption floor, absorbs first loss, and is audited after each
 * step. Integer only: no floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "svault.h"
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
static svault_t V;
static uint16_t usd, vbl;
static uint32_t fiss, viss, payer, payee, bucket[PAY_ASSURE_BUCKETS], cash, vvb, sfiat, svb, other;
static uint64_t nreq;

/* a fresh request with unique identifiers (derived like vhub's P4 ids) */
static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'S';
    sha256(in, sizeof in, d);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "SV-%llu", (unsigned long long) nreq);
    return &r;
}

static pay_status_t issue(uint32_t from, uint32_t to, uint64_t amt)
{
    pay_receipt_t rc;
    return pay_ledger_issue(&L, mk(), from, to, amt, &rc);
}

static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}

static uint64_t deb(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}
static uint64_t cred(uint32_t a)
{
    return pay_ledger_account(&L, a)->credit;
}

/* N V-Bills outstanding, all held by the seller */
static void setup(uint64_t n)
{
    pay_ledger_init(&L, NULL);
    nreq = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    fiss = open_acct(usd, 1, PAY_ACCT_ISSUER);
    payer = open_acct(usd, 2, 0);
    payee = open_acct(usd, 3, 0);
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) bucket[i] = open_acct(usd, 10 + i, 0);
    cash = open_acct(usd, 20, 0);
    viss = open_acct(vbl, 30, PAY_ACCT_ISSUER);
    vvb = open_acct(vbl, 20, 0);
    sfiat = open_acct(usd, 40, 0);
    svb = open_acct(vbl, 40, 0);
    other = open_acct(vbl, 41, 0);
    issue(fiss, payer, (uint64_t) 1 << 50);
    if (n) issue(viss, svb, n);
    CHECK(svault_init(&V, &L, bucket[PAY_ASSURE_RESERVE_FLOOR], cash, viss, vvb) == SVAULT_OK,
          "vault set up");
}

/* put `amt` straight into the reserve-floor bucket */
static void fund_reserve(uint64_t amt)
{
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), payer, bucket[PAY_ASSURE_RESERVE_FLOOR], amt, &rc);
}

static void test_init(void)
{
    setup(30);
    CHECK(V.ready && V.reserve == bucket[0] && V.cash == cash && V.vbill_issuer == viss &&
              V.vbills == vvb && V.last_ledger == PAY_OK,
          "init records the four accounts");
    CHECK(svault_check(&V, &L), "a fresh vault audits clean");
    uint32_t r = bucket[0], bad = L.n_accounts;
    CHECK(svault_init(NULL, &L, r, cash, viss, vvb) == SVAULT_ERR_ARG &&
              svault_init(&V, NULL, r, cash, viss, vvb) == SVAULT_ERR_ARG,
          "NULL vault or ledger refused");
    CHECK(svault_init(&V, &L, bad, cash, viss, vvb) == SVAULT_ERR_STATE &&
              svault_init(&V, &L, r, bad, viss, vvb) == SVAULT_ERR_STATE &&
              svault_init(&V, &L, r, cash, bad, vvb) == SVAULT_ERR_STATE &&
              svault_init(&V, &L, r, cash, viss, bad) == SVAULT_ERR_STATE,
          "a missing account refused");
    CHECK(!V.ready && svault_room(&V, &L) == 0 && !svault_check(&V, &L),
          "a refused init leaves no usable vault");
    L.acct[cash].active = false;
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_ERR_STATE, "inactive account refused");
    L.acct[cash].active = true;
    CHECK(svault_init(&V, &L, r, r, viss, vvb) == SVAULT_ERR_STATE, "reserve == cash refused");
    CHECK(svault_init(&V, &L, r, cash, vvb, vvb) == SVAULT_ERR_STATE, "issuer == vbills refused");
    CHECK(svault_init(&V, &L, r, vvb, viss, other) == SVAULT_ERR_STATE,
          "cash of another asset than the reserve refused");
    CHECK(svault_init(&V, &L, r, cash, viss, sfiat) == SVAULT_ERR_STATE,
          "vault V-Bill account of another asset than the issuer refused");
    CHECK(svault_init(&V, &L, r, cash, fiss, payee) == SVAULT_ERR_STATE,
          "fiat and V-Bill sides on one asset refused");
    CHECK(svault_init(&V, &L, r, cash, other, vvb) == SVAULT_ERR_STATE,
          "an issuer without the ISSUER flag refused");
    pay_ledger_set_flags(&L, r, PAY_ACCT_ISSUER);
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_ERR_STATE, "issuer reserve refused");
    pay_ledger_set_flags(&L, r, 0);
    pay_ledger_set_flags(&L, cash, PAY_ACCT_ISSUER);
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_ERR_STATE, "issuer cash refused");
    pay_ledger_set_flags(&L, cash, 0);
    pay_ledger_set_flags(&L, vvb, PAY_ACCT_ISSUER);
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_ERR_STATE, "issuer vbills refused");
    pay_ledger_set_flags(&L, vvb, 0);
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_OK, "flags restored: accepted");
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), payer, cash, 1, &rc);
    CHECK(svault_init(&V, &L, r, cash, viss, vvb) == SVAULT_ERR_STATE,
          "cash not empty: refused (it would not be fee-funded)");
    CHECK(svault_init(&V, &L, r, payee, viss, vvb) == SVAULT_OK, "another empty cash accepted");
    pay_ledger_transfer(&L, mk(), svb, vvb, 1, &rc);
    CHECK(svault_init(&V, &L, r, payee, viss, vvb) == SVAULT_ERR_STATE,
          "vault V-Bills not empty: refused");
    CHECK(svault_init(&V, &L, r, payee, viss, other) == SVAULT_OK && V.vbills == other,
          "another empty V-Bill account accepted");
    V.share_total = 7;
    CHECK(svault_init(&V, &L, r, payee, viss, other) == SVAULT_OK && V.share_total == 0,
          "init clears every counter");
}

static void test_carry(void)
{
    uint64_t p = 99, r = 99;
    CHECK(svault_carry(0, 0, &p, &r) && p == 0 && r == 0, "0 -> 0 rem 0");
    CHECK(svault_carry(0, 5, &p, &r) && p == 1 && r == 2, "5 -> 1 rem 2");
    CHECK(svault_carry(2, 1, &p, &r) && p == 1 && r == 0, "1 + carry 2 -> 1 rem 0");
    CHECK(svault_carry(2, 0, &p, &r) && p == 0 && r == 2, "carry alone below 3 stays");
    p = r = 99;
    CHECK(!svault_carry(3, 0, &p, &r) && p == 99 && r == 99, "carry 3 is corrupt: untouched");
    CHECK(!svault_carry(1, UINT64_MAX, &p, &r) && p == 99, "share + carry overflow refused");
    CHECK(svault_carry(0, UINT64_MAX, &p, &r) && p == 6148914691236517205ull && r == 0,
          "UINT64_MAX / 3 exact");
    CHECK(svault_carry(2, UINT64_MAX - 2, &p, &r) && p == 6148914691236517205ull && r == 0,
          "largest sum with a carry");
    CHECK(!svault_carry(0, 1, NULL, &r) && !svault_carry(0, 1, &p, NULL), "NULL outputs");
    CHECK(svault_cap(0) == 0 && svault_cap(2) == 0 && svault_cap(3) == 1 && svault_cap(29) == 9 &&
              svault_cap(30) == 10 && svault_cap(32) == 10 && svault_cap(33) == 11,
          "cap is floor(N / 3)");
}

/* S1 fed by real fee payments */
static void test_sweep_fees(void)
{
    setup(3000000);
    uint64_t total = 0, fee, parts[PAY_ASSURE_BUCKETS];
    const uint64_t amt[] = {1125, 2250, 3375, 11250, 1124, 999999, 7, 123456789, 33750, 1125};
    int n = (int) (sizeof amt / sizeof amt[0]);
    bool ok = true, audit = true;
    for (int i = 0; i < n; i++) {
        pay_receipt_t rc;
        uint64_t before = deb(bucket[0]);
        pay_status_t ps =
            pay_ledger_pay_with_fee(&L, mk(), payer, payee, amt[i], bucket, 0, 0, 0, 0, &rc, &fee);
        ok = ok && ps == PAY_OK;
        pay_assure_split(fee, parts);
        ok = ok && deb(bucket[0]) - before == parts[0];
        svault_status_t st = svault_sweep_split(&V, &L, mk(), parts);
        ok = ok && (st == SVAULT_OK || st == SVAULT_ZERO);
        total += parts[0];
        audit = audit && svault_check(&V, &L) && V.share_total == total &&
                V.swept_total == total / 3 && V.carry == total % 3 && deb(cash) == total / 3;
    }
    CHECK(ok, "every fee payment fed the reserve bucket and swept");
    CHECK(audit, "after every step: swept == floor(total reserve share / 3), conserved");
    CHECK(total > 0 && V.overflow_total == 0, "no overflow while the vault has room");

    /* a sequence of odd shares: 1, 1, 1 then 5, 7 */
    setup(3000000);
    fund_reserve(1000);
    uint64_t seq0 = L.seq;
    CHECK(svault_sweep(&V, &L, mk(), 1) == SVAULT_ZERO && L.seq == seq0 && V.carry == 1,
          "share 1: nothing posts, carry 1");
    CHECK(svault_sweep(&V, &L, mk(), 1) == SVAULT_ZERO && V.carry == 2, "share 1: carry 2");
    CHECK(svault_sweep(&V, &L, mk(), 1) == SVAULT_OK && L.seq == seq0 + 1 && deb(cash) == 1 &&
              V.carry == 0 && V.swept_total == 1,
          "third share 1: one unit moves in one transfer");
    CHECK(svault_sweep(&V, &L, mk(), 5) == SVAULT_OK && deb(cash) == 2 && V.carry == 2,
          "share 5: 1 more, carry 2");
    CHECK(svault_sweep(&V, &L, mk(), 7) == SVAULT_OK && deb(cash) == 5 && V.carry == 0 &&
              V.share_total == 15 && deb(bucket[0]) == 995,
          "share 7 + 2 = 9: 3 more; 15 total -> exactly 5");
    CHECK(L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].kind == PAY_KIND_TRANSFER,
          "a sweep is posted as a transfer");
    CHECK(svault_sweep(&V, &L, mk(), 0) == SVAULT_ZERO && V.share_total == 15, "share 0: no-op");
    CHECK(svault_check(&V, &L), "conserved");

    /* from a fee: split(100) = 50 / 25 / 15 / 10 -> part 16, carry 2 */
    CHECK(svault_sweep_fee(&V, &L, mk(), 100) == SVAULT_OK && deb(cash) == 21 && V.carry == 2 &&
              V.share_total == 65,
          "sweep_fee sweeps a third of the fee's reserve share");
    CHECK(svault_sweep_fee(&V, &L, mk(), 0) == SVAULT_ZERO, "fee 0: nothing");
    CHECK(svault_sweep_split(&V, &L, mk(), NULL) == SVAULT_ERR_ARG, "NULL parts");
    CHECK(svault_check(&V, &L), "conserved");
}

static void test_sweep_refusals(void)
{
    setup(30);
    fund_reserve(10);
    svault_t keep = V;
    CHECK(svault_sweep(&V, &L, NULL, 3) == SVAULT_ERR_ARG &&
              svault_sweep(NULL, &L, mk(), 3) == SVAULT_ERR_ARG &&
              svault_sweep(&V, NULL, mk(), 3) == SVAULT_ERR_ARG,
          "NULL arguments refused");
    CHECK(svault_sweep(&V, &L, mk(), 33) == SVAULT_ERR_LEDGER && V.last_ledger == PAY_ERR_FUNDS,
          "reserve cannot cover the part: the ledger refuses");
    CHECK(V.carry == keep.carry && V.share_total == 0 && V.swept_total == 0 && deb(cash) == 0,
          "a refused sweep changes nothing, not even the carry");
    pay_posting_req_t *r = mk();
    CHECK(svault_sweep(&V, &L, r, 3) == SVAULT_OK && deb(cash) == 1, "sweep 3 -> 1");
    CHECK(svault_sweep(&V, &L, r, 3) == SVAULT_ERR_LEDGER && V.last_ledger == PAY_DUPLICATE &&
              V.swept_total == 1 && V.share_total == 3,
          "a replayed request posts nothing and counts nothing");
    V.share_total = UINT64_MAX - 1;
    CHECK(svault_sweep(&V, &L, mk(), 2) == SVAULT_ERR_OVERFLOW && V.share_total == UINT64_MAX - 1,
          "share_total overflow refused");
    CHECK(svault_sweep(&V, &L, mk(), 1) == SVAULT_ZERO && V.share_total == UINT64_MAX,
          "share_total may reach UINT64_MAX exactly");
    V.share_total = 3;
    V.carry = 3;
    CHECK(svault_sweep(&V, &L, mk(), 0) == SVAULT_ERR_STATE && V.carry == 3,
          "a corrupt carry is refused");
    V.carry = 0;
    V.ready = false;
    CHECK(svault_sweep(&V, &L, mk(), 3) == SVAULT_ERR_ARG, "a vault not set up refuses");
    V.ready = true;
    uint32_t na = L.n_accounts;
    L.n_accounts = vvb; /* the vault's V-Bill account no longer resolves */
    CHECK(svault_sweep(&V, &L, mk(), 3) == SVAULT_ERR_ARG && svault_room(&V, &L) == 0 &&
              !svault_balances(&V, &L, 0, 0, 0, 0),
          "a ledger without the vault's accounts refuses");
    L.n_accounts = na;
    CHECK(svault_balances(&V, &L, 0, 0, 0, 0), "restored");
}

static void test_cap(void)
{
    setup(31); /* cap 10 */
    fund_reserve(1000);
    uint64_t n, h, c, res;
    CHECK(svault_room(&V, &L) == 10, "N = 31: room floor(31/3) = 10");
    CHECK(svault_balances(&V, &L, &n, &h, &c, &res) && n == 31 && h == 0 && c == 0 && res == 1000,
          "balances read from the ledger");
    CHECK(svault_sweep(&V, &L, mk(), 300) == SVAULT_OK && deb(cash) == 100, "100 swept");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 10, 9) == SVAULT_OK && svault_room(&V, &L) == 1,
          "h = 9: room 1");
    uint64_t res0 = deb(bucket[0]);
    CHECK(svault_sweep(&V, &L, mk(), 30) == SVAULT_OK && deb(cash) == 20 &&
              deb(bucket[0]) == res0 - 10,
          "room 1: the part still moves");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 10, 5) == SVAULT_OK && V.last_qty == 1 &&
              svault_room(&V, &L) == 0,
          "buy stops at the cap: h = 10");
    res0 = deb(bucket[0]);
    uint64_t seq0 = L.seq;
    CHECK(svault_sweep(&V, &L, mk(), 30) == SVAULT_OVERFLOW && L.seq == seq0 &&
              deb(bucket[0]) == res0 && deb(cash) == 10 && V.overflow_total == 10,
          "room 0: nothing moves, the part is overflow kept in the reserve");
    CHECK(svault_sweep(&V, &L, mk(), 2) == SVAULT_ZERO && V.overflow_total == 10 && V.carry == 2,
          "at the cap a zero part is still ZERO");
    CHECK(svault_sweep(&V, &L, mk(), 1) == SVAULT_OVERFLOW && V.overflow_total == 11 &&
              V.carry == 0,
          "the carry completes into overflow");
    CHECK(svault_check(&V, &L) && V.swept_total + V.overflow_total == V.share_total / 3,
          "swept + overflow == floor(total / 3)");
    issue(viss, other, 2); /* N = 33: cap 11 */
    CHECK(svault_room(&V, &L) == 1, "N grows: room again");
    CHECK(svault_sweep(&V, &L, mk(), 3) == SVAULT_OK && deb(cash) == 11, "sweeping resumes");
    pay_receipt_t rc;
    pay_ledger_redeem(&L, mk(), svb, viss, 10, &rc); /* N = 23: cap 7 < h = 10 */
    CHECK(svault_room(&V, &L) == 0, "h above the cap after redemptions: room 0, not negative");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 5) == SVAULT_ZERO, "no buy above the cap");
    CHECK(svault_check(&V, &L), "conserved");
}

static void test_buy(void)
{
    setup(3000);
    fund_reserve(1000);
    svault_sweep(&V, &L, mk(), 300); /* cash 100, room 1000 */
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 0, 5) == SVAULT_ERR_ARG, "price 0 refused");
    uint64_t seq0 = L.seq;
    V.last_qty = V.last_cost = 9;
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 7, 0) == SVAULT_ZERO && L.seq == seq0 &&
              V.last_qty == 0 && V.last_cost == 0,
          "max_qty 0: ZERO, nothing posted");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 101, 5) == SVAULT_ZERO && L.seq == seq0,
          "price above cash: ZERO");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 100, 5) == SVAULT_OK && V.last_qty == 1 &&
              V.last_cost == 100 && deb(cash) == 0,
          "price == cash buys exactly one");
    svault_sweep(&V, &L, mk(), 300);
    seq0 = L.seq;
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 7, 50) == SVAULT_OK && V.last_qty == 14 &&
              V.last_cost == 98 && deb(cash) == 2 && L.seq == seq0 + 1,
          "limited by cash: floor(100 / 7) = 14, 2 left in the vault");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 4 && j->kind == PAY_KIND_TRADE && j->lines[0].account == cash &&
              j->lines[0].d_debit == -98 && j->lines[1].account == sfiat &&
              j->lines[1].d_debit == 98 && j->lines[2].account == svb &&
              j->lines[2].d_debit == -14 && j->lines[3].account == vvb && j->lines[3].d_debit == 14,
          "one posting of exactly four lines");
    CHECK(deb(sfiat) == 198 && deb(vvb) == 15 && deb(svb) == 2985, "seller paid, vault holds 15");
    svault_sweep(&V, &L, mk(), 300);
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 3) == SVAULT_OK && V.last_qty == 3 &&
              deb(cash) == 99,
          "limited by max_qty");
    CHECK(V.spent_total == 201 && V.vbills_bought == 18 && svault_check(&V, &L),
          "spent and bought counted; conserved");

    /* limited by room: N = 30, cap 10 */
    setup(30);
    fund_reserve(1000);
    svault_sweep(&V, &L, mk(), 3000);
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 100) == SVAULT_OK && V.last_qty == 10 &&
              deb(cash) == 990,
          "limited by room: 10");
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 100) == SVAULT_ZERO, "room 0: ZERO");
    CHECK(svault_check(&V, &L), "conserved");

    /* refusals */
    setup(30);
    fund_reserve(1000);
    svault_sweep(&V, &L, mk(), 300);
    const uint32_t mine[4] = {bucket[0], cash, viss, vvb};
    bool ok = true;
    for (int i = 0; i < 4; i++)
        ok = ok && svault_buy(&V, &L, mk(), mine[i], svb, 1, 1) == SVAULT_ERR_ARG &&
             svault_buy(&V, &L, mk(), sfiat, mine[i], 1, 1) == SVAULT_ERR_ARG;
    CHECK(ok, "a seller account may not be one of the vault's own");
    CHECK(svault_buy(&V, &L, NULL, sfiat, svb, 1, 1) == SVAULT_ERR_ARG &&
              svault_buy(NULL, &L, mk(), sfiat, svb, 1, 1) == SVAULT_ERR_ARG,
          "NULL refused");
    CHECK(svault_buy(&V, &L, mk(), sfiat, other, 1, 5) == SVAULT_ERR_LEDGER &&
              V.last_ledger == PAY_ERR_FUNDS && V.spent_total == 0 && V.vbills_bought == 0 &&
              V.last_qty == 0 && deb(cash) == 100,
          "a seller without V-Bills: the ledger refuses, nothing counted");
    CHECK(svault_buy(&V, &L, mk(), payer, viss + 100, 1, 5) == SVAULT_ERR_LEDGER &&
              V.last_ledger == PAY_ERR_NO_ACCOUNT,
          "an unknown seller account: refused");
    pay_posting_req_t *r = mk();
    CHECK(svault_buy(&V, &L, r, sfiat, svb, 1, 5) == SVAULT_OK, "buy 5");
    CHECK(svault_buy(&V, &L, r, sfiat, svb, 1, 5) == SVAULT_ERR_LEDGER &&
              V.last_ledger == PAY_DUPLICATE && V.vbills_bought == 5 && V.spent_total == 5,
          "a replayed buy counts nothing");
    V.vbills_bought = UINT64_MAX - 1;
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 2) == SVAULT_ERR_OVERFLOW && deb(vvb) == 5,
          "bought counter overflow refused before posting");
    V.vbills_bought = UINT64_MAX - 1;
    V.vbills_burned = UINT64_MAX - 6;
    CHECK(svault_buy(&V, &L, mk(), sfiat, svb, 1, 1) == SVAULT_OK &&
              V.vbills_bought == UINT64_MAX && svault_check(&V, &L),
          "the counter may reach UINT64_MAX exactly");
}

static void test_floor(void)
{
    /* reserve >= f * (N - h) * backing, f = fn / fd */
    CHECK(zxv_verify_vault_solvency(600, 10, 4, 1, 1, 100), "600 >= 1 * 6 * 100: exact boundary");
    CHECK(!zxv_verify_vault_solvency(599, 10, 4, 1, 1, 100), "599 < 600");
    CHECK(zxv_verify_vault_solvency(300, 10, 4, 1, 2, 100), "half floor: 300 >= 300");
    CHECK(!zxv_verify_vault_solvency(299, 10, 4, 1, 2, 100), "299 < 300");
    CHECK(zxv_verify_vault_solvency(200, 10, 4, 1, 3, 100) &&
              !zxv_verify_vault_solvency(199, 10, 4, 1, 3, 100),
          "a third: 200 exactly (reserve * 3 >= 600)");
    CHECK(!zxv_verify_vault_solvency(1000000, 10, 4, 1, 0, 1), "fd 0 fails closed");
    CHECK(!zxv_verify_vault_solvency(1000000, 4, 5, 1, 1, 1), "h > N fails closed");
    CHECK(!zxv_verify_vault_solvency(100, 10, 4, 0, 0, 1) &&
              !zxv_verify_vault_solvency(100, 4, 5, 0, 1, 1),
          "fd 0 or h > N fail closed even with a zero floor");
    CHECK(zxv_verify_vault_solvency(0, 7, 7, 1, 1, 100), "no outside V-Bills: 0 reserve suffices");
    CHECK(!zxv_verify_vault_solvency(0, 8, 7, 1, 1, 1) &&
              zxv_verify_vault_solvency(1, 8, 7, 1, 1, 1),
          "one outside V-Bill needs one unit");
    CHECK(!zxv_verify_vault_solvency(599, 10, 3, 6, 7, 100) &&
              zxv_verify_vault_solvency(600, 10, 3, 6, 7, 100),
          "vault holdings excluded: 6/7 * 7 * 100 = 600");
    const uint64_t M = UINT64_MAX;
    CHECK(!zxv_verify_vault_solvency(M, M, 0, M, M, M), "rhs above 2^128 (high limb): false");
    CHECK(!zxv_verify_vault_solvency(M, (uint64_t) 1 << 63, 0, 3, M, M),
          "rhs above 2^128 (carry out of the middle limb): false");
    CHECK(zxv_verify_vault_solvency(M, M, 0, 1, M, 1) &&
              zxv_verify_vault_solvency(M, M, 0, M, M, 1),
          "M * M >= M * M");
    CHECK(zxv_verify_vault_solvency(M, (uint64_t) 1 << 32, 0, (uint64_t) 1 << 32, M, M - 1) &&
              !zxv_verify_vault_solvency(M - 1, (uint64_t) 1 << 32, 0, (uint64_t) 1 << 32, M, M),
          "128-bit products: M*M >= 2^64*(M-1); (M-1)*M < 2^64*M");
    CHECK(!zxv_verify_vault_solvency(M, 3, 0, (uint64_t) 1 << 63, 1, (uint64_t) 1 << 63) &&
              zxv_verify_vault_solvency(M, 3, 0, (uint64_t) 1 << 63, M, (uint64_t) 1 << 63),
          "3 * 2^126 against M and M * M");
    CHECK(zxv_verify_vault_solvency((uint64_t) 1 << 63, 1, 0, (uint64_t) 1 << 63, M, M) &&
              !zxv_verify_vault_solvency(((uint64_t) 1 << 63) - 1, 1, 0, (uint64_t) 1 << 63, M, M),
          "exact 128-bit boundary: 2^63 * M");

    uint64_t num = 9, den = 9;
    CHECK(svault_redemption_floor(600, 10, 4, &num, &den) == SVAULT_OK && num == 600 && den == 6,
          "floor = reserve / (N - h)");
    CHECK(svault_redemption_floor(600, 10, 10, &num, &den) == SVAULT_ZERO && num == 0 && den == 0,
          "N == h: no outside holder, 0/0 and ZERO");
    num = den = 9;
    CHECK(svault_redemption_floor(600, 10, 11, &num, &den) == SVAULT_ERR_ARG && num == 9,
          "h > N refused");
    CHECK(svault_redemption_floor(600, 10, 4, NULL, &den) == SVAULT_ERR_ARG &&
              svault_redemption_floor(600, 10, 4, &num, NULL) == SVAULT_ERR_ARG,
          "NULL outputs");

    /* from the ledger: the vault's V-Bills are not in the base */
    setup(30);
    fund_reserve(2000);
    svault_sweep(&V, &L, mk(), 300); /* cash 100 is not reserve */
    svault_buy(&V, &L, mk(), sfiat, svb, 10, 10);
    CHECK(deb(bucket[0]) == 1900 && deb(vvb) == 10, "reserve 1900, h 10, N 30");
    CHECK(svault_floor(&V, &L, &num, &den) == SVAULT_OK && num == 1900 && den == 20,
          "floor 1900 / 20, vault excluded");
    CHECK(svault_verify_solvency(&V, &L, 1, 1, 95) && !svault_verify_solvency(&V, &L, 1, 1, 96),
          "1900 >= 20 * 95, < 20 * 96");
    CHECK(!svault_verify_solvency(&V, &L, 1, 0, 1), "fd 0 from the ledger: false");
    svault_t blank;
    memset(&blank, 0, sizeof blank);
    CHECK(!svault_verify_solvency(&blank, &L, 1, 1, 1) &&
              svault_floor(&blank, &L, &num, &den) == SVAULT_ERR_ARG,
          "a vault not set up: false / refused");
}

static void test_first_loss(void)
{
    setup(30);
    fund_reserve(2000);
    svault_sweep(&V, &L, mk(), 300);
    svault_buy(&V, &L, mk(), sfiat, svb, 10, 10);
    uint64_t res0 = deb(bucket[0]), cash0 = deb(cash), u = 99;
    CHECK(svault_first_loss(&V, &L, mk(), 4, &u) == SVAULT_OK && u == 0 && deb(vvb) == 6 &&
              cred(viss) == 26 && V.vbills_burned == 4,
          "partial: 4 burned, N and h fall by 4, nothing uncovered");
    CHECK(L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].kind == PAY_KIND_REDEEM, "posted as a redeem");
    CHECK(deb(bucket[0]) == res0 && deb(cash) == cash0, "never touches reserve or cash");
    CHECK(svault_check(&V, &L), "conserved");
    pay_ledger_set_flags(&L, vvb, PAY_ACCT_FROZEN);
    u = 0;
    CHECK(svault_first_loss(&V, &L, mk(), 3, &u) == SVAULT_ERR_LEDGER && u == 3 &&
              V.last_ledger == PAY_ERR_POLICY && V.vbills_burned == 4 && deb(vvb) == 6,
          "a refused burn absorbs nothing");
    pay_ledger_set_flags(&L, vvb, 0);
    CHECK(svault_first_loss(&V, &L, mk(), 20, &u) == SVAULT_OK && u == 14 && deb(vvb) == 0 &&
              cred(viss) == 20 && V.vbills_burned == 10,
          "full: all 6 burned, 14 left uncovered");
    CHECK(svault_first_loss(&V, &L, mk(), 5, &u) == SVAULT_ZERO && u == 5, "h 0: ZERO, all left");
    CHECK(svault_first_loss(&V, &L, mk(), 0, NULL) == SVAULT_ZERO, "loss 0: ZERO");
    CHECK(svault_check(&V, &L) && deb(bucket[0]) == res0, "conserved; reserve untouched");
    u = 0;
    CHECK(svault_first_loss(&V, &L, NULL, 5, &u) == SVAULT_ERR_ARG && u == 5 &&
              svault_first_loss(NULL, &L, mk(), 5, NULL) == SVAULT_ERR_ARG,
          "NULL refused");
    svault_sweep(&V, &L, mk(), 300);
    svault_buy(&V, &L, mk(), sfiat, svb, 10, 3);
    CHECK(svault_first_loss(&V, &L, mk(), 3, NULL) == SVAULT_OK && deb(vvb) == 0,
          "exactly h: all burned");
}

static void test_check(void)
{
    setup(30);
    fund_reserve(2000);
    svault_sweep(&V, &L, mk(), 301); /* swept 100, carry 1 */
    svault_buy(&V, &L, mk(), sfiat, svb, 10, 4);
    CHECK(svault_check(&V, &L), "clean");
    CHECK(!svault_check(NULL, &L) && !svault_check(&V, NULL), "NULL");
    svault_t k = V;
    V.carry = 4;
    V.share_total = 304;
    CHECK(!svault_check(&V, &L), "carry 4 (3 * 100 + 4 == 304) is still corrupt");
    V = k;
    V.carry = 3;
    V.share_total = 303;
    CHECK(!svault_check(&V, &L), "carry 3 is corrupt even when the sum matches");
    V = k;
    V.carry = 2;
    V.share_total = 302;
    CHECK(svault_check(&V, &L), "carry 2 with a matching total is fine");
    V = k;
    V.share_total = 300;
    CHECK(!svault_check(&V, &L), "total off by the carry");
    V = k;
    V.share_total = 302;
    CHECK(!svault_check(&V, &L), "total off by one");
    V = k;
    V.overflow_total = UINT64_MAX; /* 100 + (2^64 - 1) wraps to 99 */
    V.share_total = 3 * 99 + 1;
    CHECK(!svault_check(&V, &L), "swept + overflow overflow refused");
    V = k;
    /* 3 * s wraps: s = 2^63 + 34 (3s mod 2^64 = 2^63 + 102), swept 100 */
    V.overflow_total = ((uint64_t) 1 << 63) + 34 - 100;
    V.share_total = ((uint64_t) 1 << 63) + 102 + 1;
    CHECK(!svault_check(&V, &L), "3 * (swept + overflow) overflow refused");
    V = k;
    /* 3s + carry wraps: s = 6148914691236517205 (3s = 2^64 - 1), carry 1 -> 0 */
    V.overflow_total = 6148914691236517205ull - 100;
    V.share_total = 0;
    CHECK(!svault_check(&V, &L), "3s + carry overflow refused");
    V = k;
    V.spent_total = 101;
    CHECK(!svault_check(&V, &L), "spent above swept");
    V = k;
    V.spent_total = 39;
    CHECK(!svault_check(&V, &L), "cash != swept - spent");
    V = k;
    V.vbills_burned = 5;
    CHECK(!svault_check(&V, &L), "burned above bought");
    V = k;
    V.vbills_bought = 5;
    CHECK(!svault_check(&V, &L), "holdings != bought - burned");
    V = k;
    /* money that did not come from a sweep */
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), payer, cash, 1, &rc);
    CHECK(!svault_check(&V, &L), "cash not from a sweep breaks conservation");
    pay_ledger_transfer(&L, mk(), cash, payer, 1, &rc);
    CHECK(svault_check(&V, &L), "restored");
    pay_ledger_transfer(&L, mk(), svb, vvb, 1, &rc);
    CHECK(!svault_check(&V, &L), "V-Bills not bought break conservation");
    pay_ledger_transfer(&L, mk(), vvb, svb, 1, &rc);
    /* a CREDIT on the vault's accounts */
    pay_ledger_set_flags(&L, cash, PAY_ACCT_ISSUER);
    issue(cash, payee, 5);
    CHECK(deb(cash) == 60 && !svault_check(&V, &L), "cash CREDIT breaks the audit");
    pay_ledger_redeem(&L, mk(), payee, cash, 5, &rc);
    pay_ledger_set_flags(&L, cash, 0);
    CHECK(svault_check(&V, &L), "restored");
    pay_ledger_set_flags(&L, vvb, PAY_ACCT_ISSUER);
    issue(vvb, other, 5);
    CHECK(deb(vvb) == 4 && !svault_check(&V, &L), "V-Bill CREDIT breaks the audit");
}

int main(void)
{
    test_init();
    test_carry();
    test_sweep_fees();
    test_sweep_refusals();
    test_cap();
    test_buy();
    test_floor();
    test_first_loss();
    test_check();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
