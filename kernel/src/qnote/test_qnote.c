/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qnote.c — Q-Note lots (qnote.h Q1-Q8) on a real pay_ledger: a claim
 * minted and quarantined by mint.c opens a lot, the lot's Q-Notes sell by
 * Dutch uniform-price auction with proceeds to the reserve, the loss burns
 * solvency-vault V-Bills first (svault.c S5), salvage recoveries redeem the
 * notes pro rata, and the audit holds after every step. A deterministic
 * randomized loop checks clearing and recovery against a reference model.
 * Integer only: no floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "qnote.h"
#include "../mint/mint.h"

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
static qnote_t Q;
static svault_t V;
static uint16_t usd, vbl, qa[QNOTE_MAX_LOTS + 2];
static uint32_t fiss, reserve, escrow, cash, viss, vvb, seller, sellerv, salv;
static uint32_t qiss[QNOTE_MAX_LOTS + 2];
#define NB 6
static uint32_t bf[NB], bq[NB]; /* bidder fiat / Q-Note (lot 0's asset) */
static uint64_t nreq;
static const uint8_t SEED[32] = {9, 1, 9, [31] = 0x99};

static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'T';
    pay_sha3_256(in, sizeof in, d);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "QT-%llu", (unsigned long long) nreq);
    return &r;
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
static void give(uint32_t to, uint64_t amt)
{
    pay_receipt_t rc;
    pay_ledger_issue(&L, mk(), fiss, to, amt, &rc);
}
static void xfer(uint32_t a, uint32_t b, uint64_t amt)
{
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), a, b, amt, &rc);
}

/* account 0 is bidder 0's Q-Note account (a holder), or with payee_first
 * set, bidder 0's fiat account (a payee): loops must start at 0 */
static int payee_first;

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    nreq = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    for (uint32_t i = 0; i < QNOTE_MAX_LOTS + 2; i++) {
        char code[8];
        snprintf(code, sizeof code, "QN%u", i);
        pay_ledger_add_asset(&L, code, 0, PAY_ASSET_UNIT, &qa[i]);
    }
    if (payee_first)
        bf[0] = open_acct(usd, 100, 0);
    else
        bq[0] = open_acct(qa[0], 100, 0);
    for (uint32_t i = 0; i < QNOTE_MAX_LOTS + 2; i++)
        qiss[i] = open_acct(qa[i], 900 + i, PAY_ACCT_ISSUER);
    fiss = open_acct(usd, 1, PAY_ACCT_ISSUER);
    reserve = open_acct(usd, 2, 0);
    escrow = open_acct(usd, 3, 0);
    cash = open_acct(usd, 4, 0);
    viss = open_acct(vbl, 5, PAY_ACCT_ISSUER);
    vvb = open_acct(vbl, 4, 0);
    seller = open_acct(usd, 6, 0);
    sellerv = open_acct(vbl, 6, 0);
    salv = open_acct(usd, 7, 0);
    for (int i = 0; i < NB; i++) {
        if (i || !payee_first) bf[i] = open_acct(usd, 100 + (uint32_t) i, 0);
        if (i || payee_first) bq[i] = open_acct(qa[0], 100 + (uint32_t) i, 0);
        give(bf[i], 10000000);
    }
    give(salv, 100000000);
    CHECK(qnote_init(&Q, &L, reserve, escrow, SEED) == QNOTE_OK, "qnote set up");
    CHECK(svault_init(&V, &L, reserve, cash, viss, vvb) == SVAULT_OK, "vault set up");
}

/* the vault buys `h` V-Bills (1 fiat each) from the seller */
static void vault_holds(uint64_t h)
{
    pay_receipt_t rc;
    pay_ledger_issue(&L, mk(), viss, sellerv, 3 * h + 3, &rc);
    give(reserve, 3 * h);
    svault_sweep(&V, &L, mk(), 3 * h);
    svault_buy(&V, &L, mk(), seller, sellerv, 1, h);
}

static uint8_t CL[32];
static const uint8_t *claim(uint8_t b)
{
    memset(CL, 0, sizeof CL);
    CL[0] = b;
    CL[31] = 0xC1;
    return CL;
}

static void test_init(void)
{
    setup();
    CHECK(Q.ready && Q.reserve == reserve && Q.escrow == escrow && Q.fiat == usd &&
              memcmp(Q.seed, SEED, 32) == 0 && Q.last_ledger == PAY_OK,
          "init records the accounts");
    CHECK(qnote_check(&Q), "fresh audits clean");
    CHECK(bq[0] == 0, "bidder 0's Q-Note account is account 0 (holder loops start at 0)");
    qnote_t q2;
    CHECK(qnote_init(NULL, &L, reserve, escrow, SEED) == QNOTE_ERR_ARG &&
              qnote_init(&q2, NULL, reserve, escrow, SEED) == QNOTE_ERR_ARG,
          "NULL refused");
    CHECK(qnote_init(&q2, &L, reserve, reserve, SEED) == QNOTE_ERR_ARG, "reserve == escrow");
    CHECK(qnote_init(&q2, &L, reserve, vvb, SEED) == QNOTE_ERR_ARG, "assets differ");
    CHECK(qnote_init(&q2, &L, fiss, escrow, SEED) == QNOTE_ERR_ARG &&
              qnote_init(&q2, &L, reserve, fiss, SEED) == QNOTE_ERR_ARG,
          "issuer accounts refused");
    CHECK(qnote_init(&q2, &L, L.n_accounts, escrow, SEED) == QNOTE_ERR_ARG &&
              qnote_init(&q2, &L, reserve, L.n_accounts, SEED) == QNOTE_ERR_ARG,
          "missing accounts refused");
    CHECK(qnote_init(&q2, &L, reserve, bf[0], SEED) == QNOTE_ERR_ARG, "escrow not empty refused");
    CHECK(!q2.ready && !qnote_check(&q2) && !qnote_check(NULL), "refused init is not usable");
    CHECK(qnote_init(&q2, &L, reserve, cash, NULL) == QNOTE_OK && q2.seed[0] == 0,
          "NULL seed is zeros");
}

static void test_open(void)
{
    setup();
    uint32_t lot = 99;
    const uint32_t I = qiss[0];
    CHECK(qnote_lot_open(&Q, claim(1), 0, 10, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG, "face 0");
    CHECK(qnote_lot_open(&Q, claim(1), PAY_BAL_MAX, 10, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "face 2^62");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 0, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG, "unit face 0");
    CHECK(qnote_lot_open(&Q, claim(1), 9, 10, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "face below one unit: no units");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, I, 10, 1, 0, &lot) == QNOTE_ERR_ARG, "p_min 0");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, I, 10, 0, 1, &lot) == QNOTE_ERR_ARG, "step 0");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, I, 5, 1, 6, &lot) == QNOTE_ERR_ARG,
          "p_min above p_start");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, I, 11, 1, 1, &lot) == QNOTE_ERR_ARG,
          "p_start above the unit face");
    CHECK(qnote_lot_open(&Q, claim(1), 100000, 10000, I, 4097, 1, 1, &lot) == QNOTE_ERR_ARG &&
              qnote_lot_open(&Q, claim(1), 100000, 10000, I, 8193, 2, 1, &lot) == QNOTE_ERR_ARG &&
              qnote_lot_open(&Q, claim(1), 100000, 10000, I, 8192, 2, 1, &lot) == QNOTE_ERR_ARG,
          "4097 rungs refused (incl. a shorter last step)");
    CHECK(qnote_lot_open(NULL, claim(1), 100, 10, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG &&
              qnote_lot_open(&Q, NULL, 100, 10, I, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "NULL");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, fiss, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "an issuer of the fiat asset refused");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, bq[0], 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "a non-issuer refused");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, L.n_accounts, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "missing issuer");
    pay_receipt_t rc;
    pay_ledger_issue(&L, mk(), qiss[9], bq[0], 1, &rc); /* wrong asset: refused by the ledger */
    uint32_t q9 = open_acct(qa[9], 50, 0);
    pay_ledger_issue(&L, mk(), qiss[9], q9, 1, &rc);
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, qiss[9], 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "an issuer with CREDIT outstanding refused");
    uint32_t i8b = open_acct(qa[8], 52, PAY_ACCT_ISSUER);
    pay_ledger_issue(&L, mk(), i8b, qiss[8], 1, &rc);
    CHECK(cred(qiss[8]) == 0 && deb(qiss[8]) == 1 &&
              qnote_lot_open(&Q, claim(1), 100, 10, qiss[8], 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "an issuer already holding notes refused");
    CHECK(lot == 99 && Q.lot[0].state == QNOTE_FREE, "nothing opened");
    CHECK(qnote_lot_open(&Q, claim(1), 100005, 10000, I, 4096, 1, 1, &lot) == QNOTE_OK && lot == 0,
          "4096 rungs (the maximum) accepted");
    const qnote_lot_t *k = qnote_lot(&Q, 0);
    CHECK(k && k->state == QNOTE_OPEN && k->face == 100005 && k->units == 10 && k->dust == 5 &&
              k->unit_face == 10000 && k->issuer == I && k->asset == qa[0] && k->p_start == 4096 &&
              k->p_step == 1 && k->p_min == 1,
          "lot recorded: 10 units of 100.00, dust 5");
    CHECK(qnote_lot_open(&Q, claim(1), 100, 10, qiss[1], 10, 1, 1, &lot) == QNOTE_ERR_DUP,
          "the same claim opens one lot");
    {
        uint8_t c31[32];
        memcpy(c31, claim(1), 32);
        c31[31] ^= 1;
        CHECK(qnote_lot_open(&Q, c31, 100, 10, open_acct(qa[8], 54, PAY_ACCT_ISSUER), 10, 1, 1,
                             &lot) == QNOTE_OK &&
                  lot == 1,
              "a claim differing only in its last byte is another claim");
        Q.lot[1].state = QNOTE_FREE; /* give the slot back for the tests below */
    }
    uint32_t i2 = open_acct(qa[0], 51, PAY_ACCT_ISSUER);
    CHECK(qnote_lot_open(&Q, claim(2), 100, 10, i2, 10, 1, 1, &lot) == QNOTE_ERR_ARG,
          "another issuer of a lot's asset refused");
    CHECK(qnote_lot_open(&Q, claim(2), 10, 10, qiss[1], 10, 1, 10, NULL) == QNOTE_OK &&
              qnote_lot(&Q, 1)->units == 1 && qnote_lot(&Q, 1)->dust == 0,
          "one unit, one rung, NULL index");
    bool ok = true;
    for (uint32_t i = 2; i < QNOTE_MAX_LOTS; i++)
        ok = ok &&
             qnote_lot_open(&Q, claim((uint8_t) (10 + i)), 50, 10, qiss[i], 10, 3, 2, &lot) ==
                 QNOTE_OK &&
             lot == i;
    CHECK(ok, "lots up to the table size");
    CHECK(qnote_lot_open(&Q, claim(40), 50, 10, open_acct(qa[8], 53, PAY_ACCT_ISSUER), 10, 1, 1,
                         &lot) == QNOTE_ERR_FULL,
          "table full");
    CHECK(qnote_lot(&Q, QNOTE_MAX_LOTS) == NULL && qnote_lot(NULL, 0) == NULL, "lot lookup");
    CHECK(qnote_check(&Q), "audit clean");
}

/* lot 0: U = 10 units of face 100, ladder 100, 90, ..., 50 */
static uint32_t lot0(uint64_t face)
{
    uint32_t lot = 99;
    qnote_lot_open(&Q, claim(1), face, 100, qiss[0], 100, 10, 50, &lot);
    return lot;
}

static void test_bids(void)
{
    setup();
    uint32_t lot = lot0(1000), idx = 99;
    uint64_t seq0 = L.seq;
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 0, 100, 1, &idx) == QNOTE_ERR_ARG, "qty 0");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 11, 100, 1, &idx) == QNOTE_ERR_ARG, "qty above U");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 49, 1, &idx) == QNOTE_ERR_ARG, "limit below p_min");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 101, 1, &idx) == QNOTE_ERR_ARG,
          "limit above p_start");
    CHECK(qnote_bid(&Q, lot, bq[0], bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, bf[0], bf[1], 1, 100, 1, &idx) == QNOTE_ERR_ARG,
          "accounts of the wrong asset");
    uint32_t other = open_acct(qa[1], 100, 0);
    CHECK(qnote_bid(&Q, lot, bf[0], other, 1, 100, 1, &idx) == QNOTE_ERR_ARG,
          "a Q-Note account of another lot's asset");
    CHECK(qnote_bid(&Q, lot, escrow, bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, reserve, bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, bf[0], qiss[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG,
          "escrow / reserve / the issuer refused");
    CHECK(qnote_bid(&Q, lot, L.n_accounts, bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, bf[0], L.n_accounts, 1, 100, 1, &idx) == QNOTE_ERR_ARG,
          "missing accounts");
    CHECK(qnote_bid(&Q, 5, bf[0], bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, QNOTE_MAX_LOTS, bf[0], bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(NULL, lot, bf[0], bq[0], 1, 100, 1, &idx) == QNOTE_ERR_ARG,
          "unknown lot / NULL");
    uint32_t poor = open_acct(usd, 300, 0);
    give(poor, 99);
    CHECK(qnote_bid(&Q, lot, poor, bq[0], 1, 100, 1, &idx) == QNOTE_ERR_LEDGER &&
              Q.last_ledger == PAY_ERR_FUNDS && Q.lot[0].n_bids == 0,
          "a lock the ledger refuses records nothing");
    CHECK(qnote_bid(&Q, lot, poor, bq[0], 1, 99, 1, &idx) == QNOTE_OK && idx == 0 &&
              deb(poor) == 0 && deb(escrow) == 99 && Q.lot[0].locked == 99,
          "exactly the funds: locked");
    CHECK(L.seq == seq0 + 2, "one posting for funding, one for the lock, none for the refusals");
    CHECK(qnote_check(&Q), "escrow == locks");
    bool ok = true;
    for (uint32_t i = 1; i < QNOTE_MAX_BIDS; i++)
        ok = ok && qnote_bid(&Q, lot, bf[i % NB], bq[i % NB], 1, 50, 1, &idx) == QNOTE_OK &&
             idx == i;
    CHECK(ok && qnote_bid(&Q, lot, bf[0], bq[0], 1, 50, 1, &idx) == QNOTE_ERR_FULL, "bids full");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 50, 1, NULL) == QNOTE_ERR_FULL, "still full");
    CHECK(qnote_check(&Q), "audit");
    /* the lock total must stay below 2^62 */
    setup();
    uint32_t big = qiss[0];
    CHECK(qnote_lot_open(&Q, claim(1), PAY_BAL_MAX - 1, ((uint64_t) 1 << 61), big,
                         ((uint64_t) 1 << 61), 1ull << 60, 1, &lot) == QNOTE_OK &&
              Q.lot[0].units == 1,
          "a lot with a huge unit face");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 1ull << 61, 1, &idx) == QNOTE_ERR_LEDGER,
          "lock above the bidder's funds");
    Q.lot[0].locked = PAY_BAL_MAX - 5;
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 5, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, bf[0], bq[0], 1, 4, 1, &idx) != QNOTE_ERR_ARG,
          "lot locks reaching 2^62 refused");
    Q.lot[0].locked = UINT64_MAX - 2;
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1, 5, 1, &idx) == QNOTE_ERR_ARG, "lock sum overflow");
    setup();
    CHECK(qnote_lot_open(&Q, claim(1), PAY_BAL_MAX - 1, 4, qiss[0], 4, 1, 4, &lot) == QNOTE_OK,
          "2^60 units");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 1ull << 60, 4, 1, &idx) == QNOTE_ERR_ARG &&
              qnote_bid(&Q, lot, bf[0], bq[0], (1ull << 60) - 1, 4, 1, &idx) == QNOTE_ERR_LEDGER,
          "qty U + 1 refused; qty U locks 2^62 - 4 (the ledger then checks funds)");
}

static uint64_t fill(uint32_t i)
{
    return Q.lot[0].bid[i].fill;
}

static void test_clear(void)
{
    /* a: oversubscribed at the first rung: 6 and 8 share 10 */
    setup();
    uint32_t lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 6, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 8, 100, 1, 0);
    CHECK(qnote_demand(&Q, lot, 100) == 14 && qnote_demand(&Q, lot, 101) == 0 &&
              qnote_demand(&Q, 7, 50) == 0,
          "demand");
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 100 && Q.lot[0].sold == 10 &&
              Q.lot[0].unsold == 0 && Q.lot[0].winners == 2,
          "clears at the first rung");
    CHECK(fill(0) == 5 && fill(1) == 5, "6*10/14 = 4 (+1 leftover), 8*10/14 = 5");
    CHECK(qnote_clear(&Q, lot) == QNOTE_ERR_STATE &&
              qnote_bid(&Q, lot, bf[2], bq[2], 1, 100, 1, 0) == QNOTE_ERR_STATE,
          "cleared: no more bids or clearing");
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_OK && deb(reserve) == 1000 && deb(escrow) == 0 &&
              deb(bq[0]) == 5 && deb(bq[1]) == 5 && cred(qiss[0]) == 10 &&
              deb(bf[0]) == 10000000 - 500 && deb(bf[1]) == 10000000 - 500,
          "settled at one price: 1000 to the reserve, the rest refunded");
    CHECK(Q.lot[0].proceeds == 1000 && Q.lot[0].loss == 0 && Q.reserve_in == 1000 &&
              qnote_check(&Q),
          "no loss: face 1000 fully paid");

    /* b: clears at a marginal rung */
    setup();
    lot = lot0(1050);                               /* 10 units, dust 50 */
    qnote_bid(&Q, lot, bf[0], bq[0], 4, 100, 1, 0); /* A */
    qnote_bid(&Q, lot, bf[1], bq[1], 3, 95, 1, 0);  /* B */
    qnote_bid(&Q, lot, bf[2], bq[2], 6, 80, 1, 0);  /* C */
    qnote_bid(&Q, lot, bf[3], bq[3], 2, 80, 1, 0);  /* D */
    qnote_bid(&Q, lot, bf[4], bq[4], 5, 70, 1, 0);  /* E */
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 80 && Q.lot[0].sold == 10,
          "D(90) = 7 < 10 <= D(80) = 15: price 80");
    CHECK(fill(0) == 4 && fill(1) == 3 && fill(2) == 3 && fill(3) == 0 && fill(4) == 0 &&
              Q.lot[0].winners == 3,
          "A, B in full; C and D share 3: floors 2 and 0, the leftover to C");
    uint64_t seq0 = L.seq;
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_OK && L.seq == seq0 + 5, "one posting per bid");
    const pay_journal_t *j = &L.journal[(seq0) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 5 && j->kind == PAY_KIND_ISSUE && j->lines[0].account == escrow &&
              j->lines[0].d_debit == -400 && j->lines[1].account == reserve &&
              j->lines[1].d_debit == 320 && j->lines[2].account == qiss[0] &&
              j->lines[2].d_credit == 4 && j->lines[3].account == bq[0] &&
              j->lines[3].d_debit == 4 && j->lines[4].account == bf[0] && j->lines[4].d_debit == 80,
          "A: escrow -400, reserve +320, issuer CREDIT +4, notes +4, refund 80");
    j = &L.journal[(seq0 + 3) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 2 && j->kind == PAY_KIND_RETURN && j->lines[0].d_debit == -160 &&
              j->lines[1].account == bf[3] && j->lines[1].d_debit == 160,
          "D lost: one refund posting");
    j = &L.journal[(seq0 + 2) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 5 && j->lines[1].d_debit == 240 && j->lines[4].d_debit == 240,
          "C: 3 at 80 out of a 480 lock");
    CHECK(deb(reserve) == 800 && deb(escrow) == 0 && Q.lot[0].loss == 250 && qnote_check(&Q),
          "proceeds 800 to the reserve; loss = 1050 - 800 = 250");
    uint64_t loss = 0;
    CHECK(qnote_loss(&Q, lot, &loss) == QNOTE_OK && loss == 250, "loss reported");

    /* c: undersubscribed -> p_min, unsold cancelled */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 3, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 4, 50, 1, 0);
    CHECK(qnote_loss(&Q, lot, &loss) == QNOTE_ERR_STATE, "no loss before settlement");
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 50 && Q.lot[0].sold == 7 &&
              Q.lot[0].unsold == 3 && fill(0) == 3 && fill(1) == 4,
          "no rung clears: everyone fills at p_min; 3 units cancelled");
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_OK && deb(reserve) == 350 && cred(qiss[0]) == 7 &&
              Q.lot[0].loss == 650 && qnote_check(&Q),
          "350 to the reserve, only the sold units exist, loss 650");
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_ERR_STATE, "settled twice refused");

    /* d: a last step shorter than step lands on p_min */
    setup();
    qnote_lot_open(&Q, claim(1), 1000, 100, qiss[0], 100, 30, 15, &lot);
    qnote_bid(&Q, lot, bf[0], bq[0], 5, 39, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 5, 16, 1, 0);
    qnote_bid(&Q, lot, bf[2], bq[2], 5, 15, 1, 0);
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 15 && fill(0) == 4 &&
              fill(1) == 3 && fill(2) == 3,
          "100, 70, 40, 15: clears at 15; all three share 10 (3 each, leftover to A)");
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_OK && deb(reserve) == 150 && qnote_check(&Q),
          "settled");

    /* e: no bids at all */
    setup();
    lot = lot0(1000);
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 50 && Q.lot[0].sold == 0 &&
              Q.lot[0].unsold == 10 && Q.lot[0].winners == 0,
          "no bids: nothing sold");
    CHECK(qnote_settle(&Q, lot, 1) == QNOTE_OK && Q.lot[0].loss == 1000 && Q.lot[0].proceeds == 0 &&
              qnote_check(&Q),
          "the whole face is loss");
    /* f: exactly at p_min with demand == U */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 10, 50, 1, 0);
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 50 && Q.lot[0].sold == 10 &&
              fill(0) == 10,
          "demand == supply on the last rung clears");
    CHECK(qnote_settle(&Q, lot, 1) == QNOTE_OK &&
              L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].n_lines == 4 && deb(escrow) == 0,
          "paid at its limit: no refund line");
    /* g: clears at the second rung, every bid above prev fills, the rung shares */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 2, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 1, 90, 1, 0);
    qnote_bid(&Q, lot, bf[2], bq[2], 1, 90, 1, 0);
    qnote_bid(&Q, lot, bf[3], bq[3], 10, 90, 1, 0);
    CHECK(qnote_clear(&Q, lot) == QNOTE_OK && Q.lot[0].price == 90 && fill(0) == 2 &&
              fill(1) == 1 && fill(2) == 1 && fill(3) == 6,
          "8 shared over 12: floors 0, 0, 6; leftovers to the first two");
    CHECK(qnote_clear(NULL, 0) == QNOTE_ERR_ARG && qnote_settle(NULL, 0, 0) == QNOTE_ERR_ARG &&
              qnote_settle(&Q, 3, 0) == QNOTE_ERR_ARG && qnote_clear(&Q, 3) == QNOTE_ERR_ARG,
          "unknown lot");
}

static void test_settle_resume(void)
{
    setup();
    uint32_t lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 4, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 4, 100, 1, 0);
    qnote_bid(&Q, lot, bf[2], bq[2], 4, 60, 1, 0);
    CHECK(qnote_settle(&Q, lot, 1) == QNOTE_ERR_STATE, "settle before clear refused");
    qnote_clear(&Q, lot); /* 100: 8; 90..70: 8; 60: 12 -> A, B full, C 2 */
    CHECK(Q.lot[0].price == 60 && fill(2) == 2, "price 60");
    L.acct[bq[1]].active = false;
    CHECK(qnote_settle(&Q, lot, 2) == QNOTE_ERR_LEDGER && Q.last_ledger == PAY_ERR_NO_ACCOUNT &&
              Q.lot[0].state == QNOTE_CLEARED && Q.lot[0].bid[0].settled &&
              !Q.lot[0].bid[1].settled,
          "a refused posting stops settlement; the lot stays CLEARED");
    CHECK(qnote_check(&Q) && Q.lot[0].proceeds == 240 && deb(escrow) == 640 &&
              Q.lot[0].locked == 640,
          "the audit holds mid-settlement");
    CHECK(qnote_loss(&Q, lot, NULL) == QNOTE_ERR_ARG, "loss NULL");
    L.acct[bq[1]].active = true;
    CHECK(qnote_settle(&Q, lot, 3) == QNOTE_OK && deb(escrow) == 0 && deb(reserve) == 600 &&
              Q.lot[0].proceeds == 600 && Q.lot[0].loss == 400 && qnote_check(&Q),
          "settlement resumes where it stopped");
}

static void test_first_loss(void)
{
    setup();
    vault_holds(20);
    uint32_t lot = lot0(1234 + 0);
    uint64_t b = 9, u = 9;
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_ERR_STATE,
          "first loss before settlement refused");
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1); /* no bids: loss 1234 */
    CHECK(qnote_first_loss(&Q, lot, NULL, 100, &b, &u) == QNOTE_ERR_ARG &&
              qnote_first_loss(&Q, lot, &V, 0, &b, &u) == QNOTE_ERR_ARG &&
              qnote_first_loss(&Q, 6, &V, 100, &b, &u) == QNOTE_ERR_ARG,
          "NULL vault / backing 0 / unknown lot");
    uint64_t n0 = cred(viss), res0 = deb(reserve);
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_OK && b == 13 && u == 0,
          "ceil(1234 / 100) = 13 V-Bills burned, nothing uncovered");
    CHECK(L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].kind == PAY_KIND_REDEEM &&
              L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].tick == 0 &&
              strncmp(L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].e2e, "QNOTE-", 6) == 0,
          "the burn is one REDEEM posting with qnote's identifiers");
    CHECK(deb(vvb) == 7 && cred(viss) == n0 - 13 && deb(reserve) == res0 && svault_check(&V, &L),
          "the vault's V-Bills absorb it; the reserve is untouched");
    CHECK(Q.lot[0].fl_done && Q.lot[0].fl_vbills == 13 && Q.lot[0].fl_burned == 13 &&
              Q.lot[0].fl_covered == 1234 && Q.lot[0].fl_uncovered == 0 && qnote_check(&Q),
          "recorded");
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_ERR_STATE, "once per lot");
    /* partial: the vault holds 7 */
    qnote_lot_open(&Q, claim(2), 2000, 100, qiss[1], 100, 10, 50, &lot);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    CHECK(qnote_first_loss(&Q, lot, &V, 100, NULL, NULL) == QNOTE_OK && deb(vvb) == 0 &&
              Q.lot[1].fl_vbills == 20 && Q.lot[1].fl_burned == 7 && Q.lot[1].fl_covered == 700 &&
              Q.lot[1].fl_uncovered == 1300,
          "vault short: 7 burned covers 700, 1300 left for slashed bonds / salvage");
    /* empty vault */
    qnote_lot_open(&Q, claim(3), 500, 100, qiss[2], 100, 10, 50, &lot);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    CHECK(qnote_first_loss(&Q, lot, &V, 7, &b, &u) == QNOTE_OK && b == 0 && u == 500 &&
              Q.lot[2].fl_vbills == 72,
          "empty vault: nothing burned, all uncovered (ceil(500/7) = 72)");
    /* no loss */
    qnote_lot_open(&Q, claim(4), 100, 100, qiss[3], 100, 10, 50, &lot);
    qnote_bid(&Q, lot, bf[0], bq[0], 1, 100, 1, 0); /* wrong asset for lot 3 */
    uint32_t q3 = open_acct(qa[3], 100, 0);
    qnote_bid(&Q, lot, bf[0], q3, 1, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    b = u = 9;
    CHECK(Q.lot[3].loss == 0 && qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_ZERO &&
              b == 0 && u == 0 && Q.lot[3].fl_done,
          "no loss: ZERO");
    /* the vault refuses */
    qnote_lot_open(&Q, claim(5), 300, 100, qiss[4], 100, 10, 50, &lot);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    vault_holds(5);
    svault_t bad = V;
    bad.ready = false;
    CHECK(qnote_first_loss(&Q, lot, &bad, 100, &b, &u) == QNOTE_ERR_VAULT && !Q.lot[4].fl_done &&
              Q.lot[4].fl_vbills == 0,
          "a vault not set up: refused, nothing recorded");
    pay_ledger_set_flags(&L, vvb, PAY_ACCT_FROZEN);
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_ERR_VAULT &&
              Q.last_ledger == PAY_ERR_POLICY && !Q.lot[4].fl_done,
          "the ledger refuses the burn: nothing recorded");
    pay_ledger_set_flags(&L, vvb, 0);
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_OK && b == 3 && u == 0,
          "then it goes through");
    CHECK(qnote_check(&Q) && svault_check(&V, &L), "conserved");
}

static void test_recover(void)
{
    setup();
    uint32_t lot = lot0(1000); /* 10 units of face 100 */
    qnote_bid(&Q, lot, bf[0], bq[0], 5, 60, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 3, 60, 1, 0);
    qnote_bid(&Q, lot, bf[2], bq[2], 1, 60, 1, 0);
    CHECK(qnote_recover(&Q, lot, salv, 100, 1) == QNOTE_ERR_STATE, "recover before settle");
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1); /* sold 9 at 50 = 450 */
    CHECK(Q.lot[0].sold == 9 && Q.lot[0].price == 50 && Q.lot[0].loss == 550, "9 sold at 50");
    uint64_t r0 = deb(reserve), s0 = deb(salv), f0 = deb(bf[0]), f1 = deb(bf[1]), f2 = deb(bf[2]);
    CHECK(qnote_recover(&Q, lot, salv, 0, 1) == QNOTE_ERR_ARG &&
              qnote_recover(&Q, lot, escrow, 5, 1) == QNOTE_ERR_ARG &&
              qnote_recover(&Q, lot, reserve, 5, 1) == QNOTE_ERR_ARG &&
              qnote_recover(&Q, lot, bq[0], 5, 1) == QNOTE_ERR_ARG &&
              qnote_recover(&Q, lot, L.n_accounts, 5, 1) == QNOTE_ERR_ARG &&
              qnote_recover(&Q, 5, salv, 5, 1) == QNOTE_ERR_ARG,
          "bad arguments");
    /* 450: T = 4 of H = 9: floors 5*4/9=2, 3*4/9=1, 1*4/9=0, leftover 1 to bq[0] */
    uint64_t sq = L.seq;
    CHECK(qnote_recover(&Q, lot, salv, 450, 2) == QNOTE_OK && L.seq == sq + 3,
          "recover 450: one inbound posting, one per holder paid (none for a 0 share)");
    CHECK(L.journal[sq % PAY_JOURNAL_MAX].n_lines == 2 &&
              L.journal[(sq + 1) % PAY_JOURNAL_MAX].n_lines == 4 &&
              L.journal[(sq + 1) % PAY_JOURNAL_MAX].kind == PAY_KIND_REDEEM &&
              L.journal[(sq + 1) % PAY_JOURNAL_MAX].tick == 2,
          "inbound: source and escrow only (no excess line); redemptions have 4 lines");
    CHECK(deb(bq[0]) == 2 && deb(bq[1]) == 2 && deb(bq[2]) == 1 && cred(qiss[0]) == 5 &&
              deb(bf[0]) == f0 + 300 && deb(bf[1]) == f1 + 100 && deb(bf[2]) == f2,
          "4 units redeemed at par: 3 + 1 (+0); paid 100 each");
    CHECK(Q.lot[0].carry == 50 && deb(escrow) == 50 && Q.lot[0].paid == 400 &&
              Q.lot[0].retired == 4 && deb(salv) == s0 - 450 && deb(reserve) == r0,
          "50 carried in escrow");
    CHECK(qnote_check(&Q), "audit");
    /* 50 + 1000: room = 5 * 100 - 50 = 450 -> 450 to holders, 600 excess to the reserve */
    CHECK(qnote_recover(&Q, lot, salv, 1000, 3) == QNOTE_OK && cred(qiss[0]) == 0 &&
              Q.lot[0].carry == 0 && Q.lot[0].paid == 900 && Q.lot[0].retired == 9 &&
              Q.lot[0].excess == 550 && deb(reserve) == r0 + 550,
          "holders capped at the face they hold (900); 550 beyond goes to the reserve");
    CHECK(deb(bf[0]) == f0 + 500 && deb(bf[1]) == f1 + 300 && deb(bf[2]) == f2 + 100 &&
              deb(bq[0]) + deb(bq[1]) + deb(bq[2]) == 0,
          "every holder got exactly 100 per unit");
    sq = L.seq;
    CHECK(qnote_recover(&Q, lot, salv, 77, 4) == QNOTE_OK && L.seq == sq + 1 &&
              L.journal[sq % PAY_JOURNAL_MAX].n_lines == 2 &&
              L.journal[sq % PAY_JOURNAL_MAX].lines[1].account == reserve &&
              deb(reserve) == r0 + 627 && Q.lot[0].excess == 627 && Q.lot[0].recovered == 1527,
          "no notes left: all of it to the reserve");
    CHECK(Q.reserve_in == 450 + 627 && qnote_check(&Q), "reserve inflow = proceeds + excess");
    sq = L.seq;
    CHECK(qnote_close(&Q, lot, 5) == QNOTE_OK && L.seq == sq && Q.lot[0].written_off == 0 &&
              Q.lot[0].closed_carry == 0 && qnote_check(&Q),
          "close with nothing carried");
    CHECK(qnote_recover(&Q, lot, salv, 1, 6) == QNOTE_ERR_STATE &&
              qnote_close(&Q, lot, 6) == QNOTE_ERR_STATE,
          "closed: no recovery, no second close");
    uint64_t loss = 0;
    CHECK(qnote_loss(&Q, lot, &loss) == QNOTE_OK && loss == 550, "loss stays reported when closed");

    /* transferred notes, missing payees, foreign notes, refused postings */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 6, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 4, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    uint32_t nq = open_acct(qa[0], 777, 0); /* owner 777 has no fiat account */
    xfer(bq[1], nq, 1);
    uint64_t seq0 = L.seq;
    CHECK(qnote_recover(&Q, lot, salv, 300, 2) == QNOTE_ERR_PAYEE && L.seq == seq0,
          "a holder without a payee refuses the whole recovery before anything posts");
    uint32_t nf = open_acct(usd, 777, 0);
    (void) open_acct(usd, 777, 0); /* a second one: the first is the payee */
    CHECK(qnote_recover(&Q, lot, salv, 300, 2) == QNOTE_OK && deb(nq) == 1 && deb(nf) == 0 &&
              deb(bq[0]) == 4 && deb(bq[1]) == 2,
          "T = 3 of 10: floors 6*3/10 = 1, 3*3/10 = 0, 1*3/10 = 0; leftovers to bq[0], bq[1]");
    CHECK(qnote_check(&Q), "audit");
    pay_ledger_set_flags(&L, bq[1], PAY_ACCT_FROZEN);
    f0 = deb(bf[0]);
    CHECK(qnote_recover(&Q, lot, salv, 300, 3) == QNOTE_ERR_LEDGER &&
              Q.last_ledger == PAY_ERR_POLICY,
          "a frozen holder's redemption is refused");
    CHECK(deb(bf[0]) == f0 + 200 && Q.lot[0].carry == 100 && deb(escrow) == 100 &&
              Q.lot[0].retired == 5 && qnote_check(&Q),
          "earlier holders were paid; the frozen holder's share stays in the carry");
    pay_ledger_set_flags(&L, bq[1], 0);
    CHECK(qnote_recover(&Q, lot, salv, 5, 4) == QNOTE_OK && Q.lot[0].carry == 5 &&
              Q.lot[0].retired == 6 && deb(bq[0]) == 1 && deb(bq[1]) == 2,
          "the next recovery redistributes the carried 100 pro rata (1 unit) and keeps 5");
    pay_receipt_t rc;
    uint32_t fiss2 = open_acct(qa[0], 778, PAY_ACCT_ISSUER);
    pay_ledger_issue(&L, mk(), fiss2, bq[2], 1, &rc);
    seq0 = L.seq;
    CHECK(qnote_recover(&Q, lot, salv, 5, 5) == QNOTE_ERR_STATE && L.seq == seq0,
          "notes from another issuer of the asset: refused before posting");
    pay_ledger_redeem(&L, mk(), bq[2], fiss2, 1, &rc);
    uint32_t poor = open_acct(usd, 301, 0);
    CHECK(qnote_recover(&Q, lot, poor, 5, 5) == QNOTE_ERR_LEDGER &&
              Q.last_ledger == PAY_ERR_FUNDS && Q.lot[0].recovered == 605,
          "a source without funds: nothing recorded");
    uint64_t r1 = deb(reserve);
    CHECK(qnote_close(&Q, lot, 6) == QNOTE_OK && deb(reserve) == r1 + 5 && deb(escrow) == 0 &&
              Q.lot[0].closed_carry == 5 && Q.lot[0].written_off == 4 && cred(qiss[0]) == 4,
          "close: the carry goes to the reserve; 4 units stay outstanding, written off");
    CHECK(qnote_check(&Q), "audit after close");
    CHECK(qnote_close(&Q, 7, 6) == QNOTE_ERR_ARG, "close unknown lot");
    /* a close the ledger refuses */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 10, 100, 1, 0);
    CHECK(qnote_close(&Q, lot, 1) == QNOTE_ERR_STATE, "close before settlement");
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    qnote_recover(&Q, lot, salv, 50, 2);
    pay_ledger_set_flags(&L, escrow, PAY_ACCT_FROZEN);
    CHECK(qnote_close(&Q, lot, 3) == QNOTE_ERR_LEDGER && Q.lot[0].state == QNOTE_SETTLED &&
              Q.lot[0].carry == 50,
          "a refused close changes nothing");
    pay_ledger_set_flags(&L, escrow, 0);
    CHECK(qnote_close(&Q, lot, 3) == QNOTE_OK && Q.reserve_in == 1050 && qnote_check(&Q), "close");
    /* overflow guards */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 10, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    Q.lot[0].recovered = UINT64_MAX;
    CHECK(qnote_recover(&Q, lot, salv, 1, 2) == QNOTE_ERR_OVERFLOW, "recovered overflow");
    Q.lot[0].recovered = 0;
    Q.lot[0].excess = UINT64_MAX;
    Q.lot[0].carry = 1000;
    CHECK(qnote_recover(&Q, lot, salv, 1, 2) == QNOTE_ERR_OVERFLOW, "excess overflow");
    Q.lot[0].excess = 0;
    Q.reserve_in = UINT64_MAX;
    CHECK(qnote_recover(&Q, lot, salv, 1, 2) == QNOTE_ERR_OVERFLOW, "reserve inflow overflow");
    Q.reserve_in = 1000;
    Q.lot[0].carry = 0;
    Q.lot[0].unit_face = UINT64_MAX;
    CHECK(qnote_recover(&Q, lot, salv, 1, 2) == QNOTE_ERR_OVERFLOW, "room overflow");
    Q.lot[0].unit_face = 100;
    CHECK(qnote_check(&Q), "restored");
}

/* Q5 payee selection: skips inactive, non-fiat, issuer, escrow and reserve */
static void test_payees(void)
{
    setup();
    uint32_t lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 10, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    /* holders owned by the fiat issuer's owner (1), the reserve's (2), the
     * escrow's (3), an owner whose first account is the note account (555)
     * and one whose first fiat account is inactive (556) */
    uint32_t h1 = open_acct(qa[0], 1, 0), h2 = open_acct(qa[0], 2, 0), h3 = open_acct(qa[0], 3, 0);
    uint32_t h5 = open_acct(qa[0], 555, 0);
    uint32_t dead = open_acct(usd, 556, 0), h6 = open_acct(qa[0], 556, 0);
    uint32_t p1 = open_acct(usd, 1, 0), p2 = open_acct(usd, 2, 0), p3 = open_acct(usd, 3, 0);
    uint32_t p5 = open_acct(usd, 555, 0), p6 = open_acct(usd, 556, 0);
    L.acct[dead].active = false;
    xfer(bq[0], h1, 1);
    xfer(bq[0], h2, 1);
    xfer(bq[0], h3, 1);
    xfer(bq[0], h5, 1);
    xfer(bq[0], h6, 1);
    uint64_t r0 = deb(reserve);
    CHECK(qnote_recover(&Q, lot, salv, 1000, 2) == QNOTE_OK && cred(qiss[0]) == 0,
          "all 10 units redeemed");
    CHECK(deb(p1) == 100 && deb(p2) == 100 && deb(p3) == 100 && deb(p5) == 100 && deb(p6) == 100 &&
              deb(bf[0]) == 10000000 - 1000 + 500,
          "each payee is the owner's first usable fiat account");
    CHECK(deb(reserve) == r0 && deb(escrow) == 0 && L.acct[dead].debit == 0 && qnote_check(&Q),
          "nothing paid to the reserve, escrow or an inactive account");
    /* the payee at account index 0 */
    payee_first = 1;
    setup();
    CHECK(bf[0] == 0 && bq[0] != 0, "bidder 0's fiat account is account 0");
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 10, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    CHECK(qnote_recover(&Q, lot, salv, 1000, 2) == QNOTE_OK && deb(bf[0]) == 10000000 &&
              cred(qiss[0]) == 0,
          "a payee at account 0 is found and paid");
    payee_first = 0;
    /* a holder account that is inactive is not counted: its notes are
     * outside what the ledger shows as live holders */
    setup();
    lot = lot0(1000);
    qnote_bid(&Q, lot, bf[0], bq[0], 5, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 5, 100, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    L.acct[bq[1]].active = false;
    uint64_t seq0 = L.seq;
    CHECK(qnote_recover(&Q, lot, salv, 100, 2) == QNOTE_ERR_STATE && L.seq == seq0,
          "inactive holder: holdings do not add up, refused before posting");
    L.acct[bq[1]].active = true;
    /* the issuer holding its own notes is not a holder */
    pay_receipt_t rc;
    uint32_t i2 = open_acct(qa[0], 901, PAY_ACCT_ISSUER);
    pay_ledger_issue(&L, mk(), i2, qiss[0], 1, &rc); /* qiss[0] DEBIT 1 */
    CHECK(deb(qiss[0]) == 1, "the lot issuer holds a note of another issuer");
    CHECK(qnote_recover(&Q, lot, salv, 100, 3) == QNOTE_OK && cred(qiss[0]) == 9 &&
              deb(qiss[0]) == 1,
          "the lot's own issuer is never paid as a holder");
}

static void test_check_tamper(void)
{
    setup();
    uint32_t lot = lot0(1050);
    qnote_bid(&Q, lot, bf[0], bq[0], 6, 100, 1, 0);
    qnote_bid(&Q, lot, bf[1], bq[1], 6, 60, 1, 0);
    qnote_clear(&Q, lot);
    qnote_settle(&Q, lot, 1);
    qnote_recover(&Q, lot, salv, 250, 2);
    qnote_lot_open(&Q, claim(2), 500, 100, qiss[1], 100, 10, 50, 0);
    uint32_t q1 = open_acct(qa[1], 100, 0);
    qnote_bid(&Q, 1, bf[0], q1, 2, 70, 1, 0);
    CHECK(qnote_check(&Q), "clean");
    qnote_t k = Q;
#define TAMPER(stmt, msg)                                                                          \
    do {                                                                                           \
        Q = k;                                                                                     \
        stmt;                                                                                      \
        CHECK(!qnote_check(&Q), msg);                                                              \
    } while (0)
    TAMPER(Q.lot[0].carry++, "carry vs escrow");
    TAMPER(Q.lot[1].locked++, "locks vs escrow");
    TAMPER(Q.lot[1].bid[0].locked++, "bid locks vs lot locks");
    TAMPER(Q.lot[0].proceeds++, "proceeds");
    TAMPER(Q.lot[0].price++, "price");
    TAMPER(Q.lot[0].retired++, "retired vs CREDIT");
    TAMPER(Q.lot[0].paid++, "paid vs retired");
    TAMPER(Q.lot[0].sold++, "sold");
    TAMPER(Q.lot[0].unsold++, "unsold");
    TAMPER(Q.lot[0].loss++, "loss");
    TAMPER(Q.lot[0].excess++, "excess vs reserve inflow");
    TAMPER(Q.lot[0].closed_carry++, "closed carry vs reserve inflow");
    TAMPER(Q.reserve_in++, "reserve inflow");
    TAMPER(Q.lot[0].dust++, "dust");
    TAMPER((Q.lot[0].dust = 100, Q.lot[0].face += 50, Q.lot[0].loss += 50), "dust >= unit face");
    TAMPER(Q.lot[0].units++, "units");
    TAMPER(Q.lot[0].units = 0, "units 0");
    TAMPER(Q.lot[0].unit_face = 0, "unit face 0");
    TAMPER(Q.lot[0].unit_face = UINT64_MAX, "units * unit face overflow");
    TAMPER((Q.lot[0].dust = UINT64_MAX), "+ dust overflow");
    TAMPER(Q.lot[0].bid[0].fill = 7, "fill above qty");
    TAMPER(Q.lot[1].bid[0].fill = 3, "fill above qty on an open lot");
    TAMPER(Q.lot[0].bid[1].settled = false, "unsettled bid after settlement");
    TAMPER(Q.lot[0].state = 5, "bad state");
    TAMPER(Q.lot[0].state = QNOTE_CLOSED, "closed with a carry");
    TAMPER((Q.lot[0].fl_burned = 1), "burned above requested");
    TAMPER((Q.lot[0].fl_covered = 1), "first-loss figures without first loss");
    TAMPER((Q.lot[0].fl_done = true), "first loss done but figures do not add up");
    TAMPER(Q.lot[0].issuer = L.n_accounts, "issuer missing");
    TAMPER(Q.lot[0].price = UINT64_MAX, "fills * price overflow");
    TAMPER((Q.lot[0].retired = UINT64_MAX), "retired above fills");
    TAMPER((Q.lot[0].retired = 1ull << 63, Q.lot[0].paid = 0), "retired * face overflow");
    TAMPER((Q.lot[1].bid[0].settled = true), "a settled bid while OPEN without its notes");
    TAMPER(Q.ready = false, "not ready");
    Q = k;
    pay_receipt_t rc;
    {
        /* carry above what the outstanding notes can absorb, with the escrow
         * made to match: only the carry bound catches it */
        uint64_t room = cred(qiss[0]) * 100, add = room + 1 - Q.lot[0].carry;
        Q.lot[0].carry = room + 1;
        pay_ledger_transfer(&L, mk(), bf[3], escrow, add, &rc);
        CHECK(!qnote_check(&Q), "carry above the outstanding face");
        pay_ledger_transfer(&L, mk(), escrow, bf[3], add, &rc);
        Q = k;
        CHECK(qnote_check(&Q), "restored");
    }
    pay_ledger_transfer(&L, mk(), bf[3], escrow, 1, &rc);
    CHECK(!qnote_check(&Q), "fiat in escrow from outside");
    pay_ledger_transfer(&L, mk(), escrow, bf[3], 1, &rc);
    CHECK(qnote_check(&Q), "restored");
    pay_ledger_issue(&L, mk(), qiss[0], bq[3], 1, &rc);
    CHECK(!qnote_check(&Q), "Q-Notes issued outside the auction");
    pay_ledger_redeem(&L, mk(), bq[3], qiss[0], 1, &rc);
    pay_ledger_set_flags(&L, escrow, PAY_ACCT_ISSUER);
    pay_ledger_issue(&L, mk(), escrow, bf[3], 1, &rc);
    CHECK(!qnote_check(&Q), "escrow CREDIT");
    pay_ledger_redeem(&L, mk(), bf[3], escrow, 1, &rc);
    pay_ledger_set_flags(&L, escrow, 0);
    CHECK(qnote_check(&Q), "restored");
    uint32_t na = L.n_accounts;
    L.n_accounts = escrow;
    CHECK(!qnote_check(&Q), "escrow missing");
    L.n_accounts = na;
    /* overflow in the sums */
    Q = k;
    Q.lot[2] = Q.lot[0];
    Q.lot[2].claim[0] = 77;
    Q.lot[2].proceeds = UINT64_MAX;
    CHECK(!qnote_check(&Q), "a corrupt copy");
#undef TAMPER
}

/* randomized: clearing and recovery against a reference */
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(uint64_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return n ? rs % n : 0;
}

static void test_random(void)
{
    bool clear_ok = true, money_ok = true, audit = true, rec_ok = true;
    int cleared_full = 0, under = 0;
    for (int round = 0; round < 60; round++) {
        setup();
        uint64_t u = 1 + rnd(200), U = 1 + rnd(40), dust = rnd(u);
        uint64_t F = U * u + dust;
        uint64_t pmin = 1 + rnd(u), pstart = pmin + rnd(u - pmin + 1), step = 1 + rnd(pstart);
        uint32_t lot;
        if (qnote_lot_open(&Q, claim((uint8_t) round), F, u, qiss[0], pstart, step, pmin, &lot) !=
            QNOTE_OK) {
            clear_ok = false;
            continue;
        }
        int nb = (int) rnd(QNOTE_MAX_BIDS + 1);
        uint64_t qty[QNOTE_MAX_BIDS], lim[QNOTE_MAX_BIDS];
        uint32_t who[QNOTE_MAX_BIDS];
        for (int i = 0; i < nb; i++) {
            qty[i] = 1 + rnd(U);
            lim[i] = pmin + rnd(pstart - pmin + 1);
            who[i] = (uint32_t) rnd(NB);
            if (qnote_bid(&Q, lot, bf[who[i]], bq[who[i]], qty[i], lim[i], 1, 0) != QNOTE_OK)
                clear_ok = false;
        }
        audit = audit && qnote_check(&Q);
        /* reference clearing */
        uint64_t ladder[QNOTE_MAX_RUNGS];
        int nr = 0;
        for (uint64_t p = pstart;; p -= step) {
            ladder[nr++] = p;
            if (p - pmin <= step) break;
        }
        if (ladder[nr - 1] != pmin) ladder[nr++] = pmin;
        uint64_t price = pmin, want[QNOTE_MAX_BIDS] = {0}, sold = 0;
        int found = -1;
        for (int r = 0; r < nr && found < 0; r++) {
            uint64_t d = 0;
            for (int i = 0; i < nb; i++) d += lim[i] >= ladder[r] ? qty[i] : 0;
            if (d >= U) found = r;
        }
        if (found >= 0) {
            price = ladder[found];
            uint64_t hi = found ? ladder[found - 1] : UINT64_MAX, full = 0, tot = 0, given = 0;
            for (int i = 0; i < nb; i++) {
                if (lim[i] >= hi) {
                    want[i] = qty[i];
                    full += qty[i];
                } else if (lim[i] >= price) {
                    tot += qty[i];
                }
            }
            for (int i = 0; i < nb; i++)
                if (lim[i] >= price && lim[i] < hi) {
                    want[i] = (U - full) * qty[i] / tot;
                    given += want[i];
                }
            for (int i = 0; i < nb && full + given < U; i++)
                if (lim[i] >= price && lim[i] < hi) {
                    want[i]++;
                    given++;
                }
            sold = U;
            cleared_full++;
        } else {
            for (int i = 0; i < nb; i++) want[i] = qty[i], sold += qty[i];
            under++;
        }
        uint64_t r0 = deb(reserve);
        qnote_clear(&Q, lot);
        clear_ok = clear_ok && Q.lot[0].price == price && Q.lot[0].sold == sold &&
                   Q.lot[0].unsold == U - sold;
        for (int i = 0; i < nb; i++) clear_ok = clear_ok && Q.lot[0].bid[i].fill == want[i];
        audit = audit && qnote_check(&Q);
        qnote_settle(&Q, lot, 2);
        money_ok = money_ok && deb(reserve) == r0 + sold * price && deb(escrow) == 0 &&
                   cred(qiss[0]) == sold &&
                   Q.lot[0].loss == (F > sold * price ? F - sold * price : 0);
        uint64_t tot_f = 0;
        for (int i = 0; i < NB; i++) tot_f += deb(bf[i]);
        money_ok = money_ok && tot_f == (uint64_t) NB * 10000000 - sold * price;
        audit = audit && qnote_check(&Q) && pay_ledger_check(&L);
        /* recoveries against a model */
        uint64_t carry = 0, retired = 0, paid = 0, excess = 0;
        for (int k = 0; k < 6; k++) {
            /* shuffle some notes around first */
            int a = (int) rnd(NB), b = (int) rnd(NB);
            if (deb(bq[a]) && a != b) xfer(bq[a], bq[b], 1 + rnd(deb(bq[a])));
            uint64_t H = sold - retired, amt = 1 + rnd(u * 5 + 3);
            uint64_t room = H * u - carry, x = amt < room ? amt : room;
            uint64_t hold[NB], payb[NB];
            for (int i = 0; i < NB; i++) hold[i] = deb(bq[i]), payb[i] = deb(bf[i]);
            excess += amt - x;
            carry += x;
            uint64_t T = carry / u, g = 0, r[NB];
            for (int i = 0; i < NB; i++) r[i] = H ? T * hold[i] / H : 0, g += r[i];
            for (int i = 0; i < NB && g < T; i++)
                if (hold[i]) r[i]++, g++;
            uint64_t res = deb(reserve);
            rec_ok = rec_ok && qnote_recover(&Q, lot, salv, amt, 3) == QNOTE_OK;
            for (int i = 0; i < NB; i++)
                rec_ok = rec_ok && deb(bq[i]) == hold[i] - r[i] && deb(bf[i]) == payb[i] + r[i] * u;
            carry -= T * u;
            retired += T;
            paid += T * u;
            rec_ok = rec_ok && deb(reserve) == res + (amt - x) && Q.lot[0].carry == carry &&
                     Q.lot[0].retired == retired && Q.lot[0].paid == paid &&
                     Q.lot[0].excess == excess && paid <= sold * u && paid <= F;
            audit = audit && qnote_check(&Q) && pay_ledger_check(&L);
        }
        if (rnd(2)) {
            uint64_t res = deb(reserve);
            rec_ok = rec_ok && qnote_close(&Q, lot, 4) == QNOTE_OK && deb(reserve) == res + carry &&
                     deb(escrow) == 0;
            audit = audit && qnote_check(&Q);
        }
    }
    CHECK(clear_ok, "random: price, quantities and every fill match the reference");
    CHECK(money_ok, "random: proceeds to the reserve, escrow empty, notes == sold, fiat conserved");
    CHECK(rec_ok, "random: recoveries redeem pro rata, carry and cap match the model");
    CHECK(audit, "random: audit and ledger hold after every step");
    CHECK(cleared_full > 5 && under > 5, "random: both clearing branches exercised");
}

/* mint -> quarantine -> Q-Note lot -> auction -> first loss -> recovery */
static bool fake_verify(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                        const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    (void) signer;
    uint8_t d[32];
    pay_sha3_256(msg, len, d);
    return sig_len == 32 && memcmp(d, sig, 32) == 0;
}

static void test_mint_handoff(void)
{
    setup();
    static mint_t M;
    uint32_t vino_iss = open_acct(L.vfv_asset, 60, PAY_ACCT_ISSUER);
    uint32_t holder_vb = open_acct(vbl, 61, 0);
    CHECK(mint_init(&M, &L, vino_iss, viss, 100, false, fake_verify, NULL, SEED) == MINT_OK,
          "mint on the vault's V-Bill issuer");
    mint_juris_set(&M, 7, true, 0, 1000000);
    mint_attestor_set(&M, 5, true, 0);
    mint_unit_set(&M, MINT_KIND_EWHR, 1, 1);
    mint_capacity_t c;
    memset(&c, 0, sizeof c);
    c.attestor = 5;
    c.id[0] = 0xEE;
    c.kind = MINT_KIND_EWHR;
    c.juris = 7;
    c.qty = 2050;
    c.expiry = 100;
    uint8_t m[MINT_CLAIM_MSG], s[32];
    mint_claim_msg(&c, m);
    pay_sha3_256(m, sizeof m, s);
    CHECK(mint_issue(&M, &c, s, 32, MINT_TARGET_VBILL, holder_vb, 1, 0) == MINT_OK &&
              deb(holder_vb) == 20,
          "20 V-Bills minted against a 20.50 receipt");
    vault_holds(9);
    uint64_t face = 0;
    CHECK(mint_quarantine(&M, c.id, &face) == MINT_OK && face == 2000 && deb(holder_vb) == 20,
          "the receipt fails audit: quarantined, the holder keeps the V-Bills");
    uint32_t lot;
    CHECK(qnote_lot_open(&Q, c.id, face, 100, qiss[0], 100, 10, 10, &lot) == QNOTE_OK &&
              Q.lot[lot].units == 20,
          "a lot of 20 Q-Notes of 1.00 face");
    CHECK(qnote_lot_open(&Q, c.id, face, 100, qiss[1], 100, 10, 10, &lot) == QNOTE_ERR_DUP,
          "one lot per claim");
    CHECK(qnote_bid(&Q, lot, bf[0], bq[0], 12, 40, 1, 0) == QNOTE_OK &&
              qnote_bid(&Q, lot, bf[1], bq[1], 10, 30, 1, 0) == QNOTE_OK,
          "bids lock (mint and qnote share a seed: their ledger ids are domain-separated)");
    qnote_clear(&Q, lot);
    uint64_t r0 = deb(reserve);
    qnote_settle(&Q, lot, 2);
    CHECK(Q.lot[lot].price == 30 && Q.lot[lot].sold == 20 && deb(reserve) == r0 + 600 &&
              Q.lot[lot].loss == 1400,
          "salvage auction clears at 0.30: 6.00 to the reserve, loss 14.00");
    uint64_t b, u, n0 = cred(viss);
    CHECK(qnote_first_loss(&Q, lot, &V, 100, &b, &u) == QNOTE_OK && b == 9 && u == 500 &&
              cred(viss) == n0 - 9 && deb(holder_vb) == 20,
          "the vault burns its 9 V-Bills first; 5.00 left for slashed bonds; holders untouched");
    CHECK(qnote_recover(&Q, lot, salv, 2500, 3) == QNOTE_OK && cred(qiss[0]) == 0 &&
              Q.lot[lot].paid == 2000 && Q.lot[lot].excess == 500,
          "salvage of 25.00: Q-Note holders repaid their 20.00 face, 5.00 to the reserve");
    CHECK(qnote_check(&Q) && svault_check(&V, &L) && mint_check(&M) && pay_ledger_check(&L),
          "every audit holds");
}

int main(void)
{
    test_init();
    test_open();
    test_bids();
    test_clear();
    test_settle_resume();
    test_first_loss();
    test_recover();
    test_payees();
    test_check_tamper();
    test_random();
    test_mint_handoff();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
