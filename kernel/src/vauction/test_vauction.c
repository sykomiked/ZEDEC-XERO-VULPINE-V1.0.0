/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vauction.c — V-Bill issuance auctions (vauction.h A1-A8) on a real
 * pay_ledger: hand-worked clearings (oversubscribed, undersubscribed,
 * marginal pro rata with leftover, supply falling between rungs, reserve at
 * and below the floor), the dilution bound Q(p) at q and q+1, every receipt
 * and quote outcome, locks, refunds, resumable settlement, the audit ring,
 * and the ledger invariants after every step. The signature hook is a
 * stand-in (SHA-256 keyed by the signer); production passes ML-DSA. */
#include <stdio.h>
#include <string.h>
#include "vauction.h"
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

#define NB 6
static pay_ledger_t L;
static vauc_t A;
static uint16_t usd, vbl, other;
static uint32_t fi, rs, es, vi, holder, bf[NB], bv[NB], oth;
static uint64_t nfund, minted;
static const uint8_t SEED[32] = {9, 8, 7, [31] = 0xC4};

/* stand-in signature: SHA-256(signer || 0x5A || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[160];
    memset(b, 0, sizeof b);
    memcpy(b, &who, 4);
    b[4] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    uint8_t want[32];
    (*(int *) ctx)++;
    if (sl != 32 || n > 150) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}
static int nverify;

static pay_status_t fund(uint32_t from, uint32_t to, uint64_t amt)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    pay_status_t st = pay_ledger_issue(&L, &r, from, to, amt, &rc);
    if (st == PAY_OK && from == fi) minted += amt;
    return st;
}

/* a posting whose e2e collides with the module's next one */
static void block_e2e(uint64_t seq)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "VAUC-%llu", (unsigned long long) seq);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEF;
    pay_ledger_issue(&L, &r, fi, bf[NB - 1], 1, &rc);
    minted += 1;
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

static vauc_config_t cfg0(void)
{
    vauc_config_t c;
    memset(&c, 0, sizeof c);
    c.floor_num = 1;
    c.floor_den = 1;
    c.backing = 1; /* p >= 1 = k: the floor never binds */
    c.cap = 100;
    c.p_start = 200;
    c.p_step = 10;
    c.p_min = 100;
    c.max_age = 10;
    c.min_quotes = 3;
    return c;
}

static void setup(const vauc_config_t *c, uint64_t reserve, uint64_t n0)
{
    pay_ledger_init(&L, NULL);
    minted = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    pay_ledger_add_asset(&L, "OTH", 0, PAY_ASSET_UNIT, &other);
    fi = open_acct(usd, 1, PAY_ACCT_ISSUER);
    rs = open_acct(usd, 2, 0);
    es = open_acct(usd, 3, 0);
    vi = open_acct(vbl, 4, PAY_ACCT_ISSUER);
    holder = open_acct(vbl, 5, 0);
    oth = open_acct(other, 6, 0);
    for (int i = 0; i < NB; i++) {
        bf[i] = open_acct(usd, 10 + i, 0);
        bv[i] = open_acct(vbl, 10 + i, 0);
        fund(fi, bf[i], 1000000);
    }
    if (reserve) fund(fi, rs, reserve);
    if (n0) fund(vi, holder, n0);
    nverify = 0;
    vauc_status_t st = vauc_init(&A, &L, c, rs, es, vi, verify, &nverify, SEED);
    if (st != VAUC_OK) printf("setup: init %d\n", st);
}

/* the invariants: ledger balanced, no fiat created or burned, escrow holds
 * exactly the open locks, V-Bills outstanding == the issuer's CREDIT */
static bool inv(void)
{
    uint64_t d, c, vd, vc, lockd = 0;
    int64_t e, ve;
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, &d, &c, &e);
    pay_ledger_totals(&L, vbl, PAY_CAP_FINANCIAL, &vd, &vc, &ve);
    for (uint32_t i = 0; i < A.n_bids; i++)
        if (!A.bid[i].settled) lockd += A.bid[i].locked;
    return pay_ledger_check(&L) && pay_ledger_verify_chain(&L) && d == minted && c == minted &&
           e == 0 && deb(es) == lockd && vd == vc && vc == vauc_outstanding(&A) && ve == 0;
}

static vauc_status_t bid(int i, uint64_t qty, uint64_t limit)
{
    return vauc_bid(&A, bf[i], bv[i], qty, limit, 1, NULL);
}

static void test_msgs(void)
{
    uint8_t m[VAUC_QUOTE_MSG], ref[VAUC_QUOTE_MSG];
    memcpy(ref, "zxv/vauc-quote/v1", 17);
    for (int i = 0; i < 4; i++) ref[17 + i] = (uint8_t) (0xA1B2C3D4u >> (8 * i));
    for (int i = 0; i < 8; i++) {
        ref[21 + i] = (uint8_t) (0x0102030405060708ull >> (8 * i));
        ref[29 + i] = (uint8_t) (0x1122334455667788ull >> (8 * i));
    }
    vauc_quote_msg(0xA1B2C3D4u, 0x0102030405060708ull, 0x1122334455667788ull, m);
    CHECK(!memcmp(m, ref, sizeof m), "the quote message is exactly the documented bytes");

    uint8_t id[32], r[VAUC_RCPT_MSG], rr[VAUC_RCPT_MSG];
    for (int i = 0; i < 32; i++) id[i] = (uint8_t) (0x40 + i);
    memcpy(rr, "zxv/vauc-rcpt/v1", 16);
    for (int i = 0; i < 4; i++) {
        rr[16 + i] = (uint8_t) (0x01020304u >> (8 * i));
        rr[52 + i] = (uint8_t) (0x0A0B0C0Du >> (8 * i));
    }
    memcpy(rr + 20, id, 32);
    for (int i = 0; i < 8; i++) {
        rr[56 + i] = (uint8_t) (0x1112131415161718ull >> (8 * i));
        rr[64 + i] = (uint8_t) (0x2122232425262728ull >> (8 * i));
    }
    vauc_receipt_msg(0x01020304u, id, 0x0A0B0C0Du, 0x1112131415161718ull, 0x2122232425262728ull, r);
    CHECK(!memcmp(r, rr, sizeof r), "the receipt message is exactly the documented bytes");
}

static void test_config(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    CHECK(A.reserve == rs && A.escrow == es && A.issuer == vi && A.fiat == usd && A.vbill == vbl &&
              A.state == VAUC_IDLE && !memcmp(A.seed, SEED, 32),
          "init binds the accounts and assets");
    vauc_t B;
    CHECK(vauc_init(&B, &L, &c, rs, es, vi, verify, NULL, NULL) == VAUC_OK, "seed optional");
    CHECK(vauc_init(NULL, &L, &c, rs, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, NULL, &c, rs, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, &L, NULL, rs, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, &L, &c, rs, es, vi, NULL, NULL, SEED) == VAUC_ERR_ARG,
          "missing arguments refused");
    CHECK(vauc_init(&B, &L, &c, 999, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, &L, &c, rs, 999, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, &L, &c, rs, es, 999, verify, NULL, SEED) == VAUC_ERR_ARG,
          "unknown accounts refused");
    CHECK(vauc_init(&B, &L, &c, rs, rs, vi, verify, NULL, SEED) == VAUC_ERR_ARG,
          "reserve and escrow must differ");
    CHECK(vauc_init(&B, &L, &c, rs, bv[0], vi, verify, NULL, SEED) == VAUC_ERR_ARG,
          "escrow of another asset refused");
    CHECK(vauc_init(&B, &L, &c, rs, es, fi, verify, NULL, SEED) == VAUC_ERR_ARG,
          "an issuer of the fiat asset is not the V-Bill issuer");
    CHECK(vauc_init(&B, &L, &c, fi, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG &&
              vauc_init(&B, &L, &c, rs, fi, vi, verify, NULL, SEED) == VAUC_ERR_ARG,
          "reserve or escrow may not be an issuer account");
    CHECK(vauc_init(&B, &L, &c, rs, es, holder, verify, NULL, SEED) == VAUC_ERR_ARG,
          "the V-Bill issuer must carry the ISSUER flag");

    vauc_config_t bad;
#define BAD(field, val, msg)                                                                       \
    do {                                                                                           \
        bad = cfg0();                                                                              \
        bad.field = val;                                                                           \
        CHECK(vauc_set_config(&A, &bad) == VAUC_ERR_ARG &&                                         \
                  vauc_init(&B, &L, &bad, rs, es, vi, verify, NULL, SEED) == VAUC_ERR_ARG,         \
              msg);                                                                                \
    } while (0)
    BAD(floor_num, 0, "floor_num 0 refused");
    BAD(floor_den, 0, "floor_den 0 refused");
    BAD(backing, 0, "backing 0 refused");
    BAD(cap, 0, "cap 0 refused");
    BAD(cap, PAY_BAL_MAX, "cap at PAY_BAL_MAX refused");
    BAD(p_min, 0, "p_min 0 refused");
    BAD(p_step, 0, "step 0 refused");
    BAD(p_start, 99, "p_start below p_min refused");
    BAD(min_quotes, 0, "min_quotes 0 refused");
    BAD(min_quotes, VAUC_MAX_QUOTES + 1, "min_quotes above the quote slots refused");
    BAD(p_start, 100 + 10 * VAUC_MAX_RUNGS, "VAUC_MAX_RUNGS + 1 rungs refused");
    BAD(p_start, 100 + 10 * (VAUC_MAX_RUNGS - 1) + 1, "a partial last step counts as a rung");
    bad = cfg0();
    bad.floor_num = 1ull << 32;
    bad.backing = 1ull << 32;
    CHECK(vauc_set_config(&A, &bad) == VAUC_ERR_ARG, "floor_num * backing must fit 64 bits");
    bad.backing = (1ull << 32) - 1;
    CHECK(vauc_set_config(&A, &bad) == VAUC_OK, "just fitting is fine");
    bad = cfg0();
    bad.p_start = 100 + 10 * (VAUC_MAX_RUNGS - 1);
    bad.cap = PAY_BAL_MAX - 1;
    bad.min_quotes = VAUC_MAX_QUOTES;
    CHECK(vauc_set_config(&A, &bad) == VAUC_OK && A.cfg.p_start == bad.p_start,
          "exactly VAUC_MAX_RUNGS rungs, the largest cap and every quote slot are fine");
    bad = cfg0();
    bad.p_start = bad.p_min = 1;
    CHECK(vauc_set_config(&A, &bad) == VAUC_OK, "a one-rung ladder at p_min 1 is fine");
    CHECK(vauc_set_config(NULL, &bad) == VAUC_ERR_ARG && vauc_set_config(&A, NULL) == VAUC_ERR_ARG,
          "null refused");
    c = cfg0();
    vauc_set_config(&A, &c);
    vauc_open(&A);
    CHECK(vauc_set_config(&A, &c) == VAUC_ERR_STATE, "no config change while OPEN");
    vauc_clear(&A, 1);
    CHECK(vauc_set_config(&A, &c) == VAUC_ERR_STATE, "nor while CLEARED");
    vauc_settle(&A, 1);
    CHECK(vauc_set_config(&A, &c) == VAUC_OK, "between epochs it is allowed");
}

static void test_dilution(void)
{
    vauc_config_t c = cfg0();
    c.backing = 100;
    c.cap = 700;
    setup(&c, 0, 0);
    /* R = 50000, N = 300, f = 1, backing 100: excess 20000 */
    CHECK(vauc_dilution_q(&A, 50000, 300, 80) == 1000, "Q(80) = 20000 / 20 = 1000");
    CHECK(50000 + 1000 * 80 >= 100 * (300 + 1000) && !(50000 + 1001 * 80 >= 100 * (300 + 1001)),
          "q = 1000 keeps the floor exactly, q + 1 breaks it");
    CHECK(vauc_dilution_q(&A, 50000, 300, 70) == 666, "Q(70) = floor(20000 / 30) = 666");
    CHECK(50000 + 666 * 70 >= 100 * 966 && 50000 + 667 * 70 < 100 * 967, "666 holds, 667 breaks");
    CHECK(vauc_dilution_q(&A, 50000, 300, 100) == VAUC_UNBOUNDED &&
              vauc_dilution_q(&A, 50000, 300, 101) == VAUC_UNBOUNDED,
          "p >= backing / f: every q passes");
    CHECK(vauc_dilution_q(&A, 50000, 300, 99) == 20000, "Q(99) = 20000");
    CHECK(vauc_dilution_q(&A, 50000, 300, 0) == 200, "Q(0) = 20000 / 100");
    CHECK(vauc_supply(&A, 50000, 300, 80) == 700 && vauc_supply(&A, 50000, 300, 60) == 500 &&
              vauc_supply(&A, 50000, 300, 100) == 700,
          "S = min(cap, Q)");
    CHECK(vauc_dilution_q(&A, 30000, 300, 80) == 0 &&
              vauc_dilution_q(&A, 30000, 300, 100) == VAUC_UNBOUNDED,
          "exactly at the floor: nothing below backing, anything at it");
    CHECK(vauc_dilution_q(&A, 29999, 300, 80) == 0 && vauc_dilution_q(&A, 29999, 300, 1000) == 0 &&
              vauc_supply(&A, 29999, 300, 1000) == 0,
          "below the floor Q = 0 whatever the price");
    CHECK(vauc_dilution_q(&A, 30100, 300, 99) == 100 && vauc_dilution_q(&A, 30099, 300, 99) == 99,
          "the floor of the quotient is exact");

    c.floor_num = 3;
    c.floor_den = 2; /* k = 300, c = 2p: floor R >= 150 N */
    vauc_set_config(&A, &c);
    CHECK(vauc_dilution_q(&A, 50000, 300, 60) == 55, "f = 3/2: Q(60) = 10000 / 180 = 55");
    CHECK((50000 + 55 * 60) * 2 >= 300 * (300 + 55) && (50000 + 56 * 60) * 2 < 300 * (300 + 56),
          "55 holds, 56 breaks");
    CHECK(vauc_dilution_q(&A, 50000, 300, 150) == VAUC_UNBOUNDED &&
              vauc_dilution_q(&A, 50000, 300, 149) == 5000,
          "the bound turns unbounded exactly at p*den = num*backing");
    CHECK(vauc_dilution_q(&A, 45000, 300, 1) == 0 &&
              vauc_dilution_q(&A, 45000, 300, 150) == VAUC_UNBOUNDED &&
              vauc_dilution_q(&A, 44999, 300, 150) == 0,
          "f = 3/2 floor at R = 45000");

    c.floor_num = 1;
    c.floor_den = 1ull << 62;
    c.backing = 2;
    vauc_set_config(&A, &c);
    CHECK(vauc_dilution_q(&A, 1ull << 63, 0, 0) == VAUC_UNBOUNDED,
          "a quotient past 64 bits is unbounded");
    CHECK(vauc_dilution_q(NULL, 1, 1, 1) == 0 && vauc_supply(NULL, 1, 1, 1) == 0 &&
              vauc_demand(NULL, 1) == 0 && vauc_outstanding(NULL) == 0,
          "null instance gives zero");
}

static void test_oversubscribed(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    CHECK(bid(0, 1, 150) == VAUC_ERR_STATE, "no bids before the epoch opens");
    CHECK(vauc_clear(&A, 1) == VAUC_ERR_STATE && vauc_settle(&A, 1) == VAUC_ERR_STATE &&
              vauc_cancel(&A, 1) == VAUC_ERR_STATE,
          "nothing to clear, settle or cancel in IDLE");
    CHECK(vauc_open(&A) == VAUC_OK && A.epoch == 1 && A.state == VAUC_OPEN, "epoch 1 opens");
    CHECK(vauc_open(&A) == VAUC_ERR_STATE, "it cannot open twice");
    uint32_t ix = 99;
    CHECK(vauc_bid(&A, bf[0], bv[0], 60, 180, 1, &ix) == VAUC_OK && ix == 0, "A 60 @ 180");
    CHECK(deb(bf[0]) == 1000000 - 10800 && deb(es) == 10800 && inv(), "60 * 180 locked in escrow");
    CHECK(bid(1, 50, 150) == VAUC_OK && bid(2, 40, 150) == VAUC_OK && bid(3, 30, 120) == VAUC_OK,
          "B 50 @ 150, C 40 @ 150, D 30 @ 120");
    CHECK(inv() && A.n_bids == 4 && A.bid[3].seq == 3 && A.locked == 10800 + 7500 + 6000 + 3600,
          "four bids locked");
    CHECK(vauc_demand(&A, 180) == 60 && vauc_demand(&A, 181) == 0 && vauc_demand(&A, 150) == 150 &&
              vauc_demand(&A, 120) == 180,
          "D(p) counts bids with limit >= p");
    CHECK(vauc_settle(&A, 1) == VAUC_ERR_STATE, "no settlement before clearing");
    CHECK(vauc_clear(&A, 1) == VAUC_OK && A.state == VAUC_CLEARED, "cleared");
    CHECK(A.cur.price == 150 && A.cur.qty == 100 && A.cur.winners == 3,
          "D(150) = 150 >= cap 100: clears at 150");
    CHECK(A.bid[0].fill == 60 && A.bid[1].fill == 23 && A.bid[2].fill == 17 && A.bid[3].fill == 0,
          "A full (limit >= 160); B, C share 40 pro rata: 22 + 17, leftover to B (earlier)");
    CHECK(bid(4, 1, 200) == VAUC_ERR_STATE && vauc_clear(&A, 1) == VAUC_ERR_STATE &&
              vauc_open(&A) == VAUC_ERR_STATE,
          "no bids, re-clearing or opening once cleared");
    uint64_t r0 = deb(rs), lseq = L.seq, aseq = A.seq;
    CHECK(vauc_settle(&A, 2) == VAUC_OK && A.state == VAUC_SETTLED, "settled");
    CHECK(L.seq == lseq + 4 && A.seq == aseq + 4, "exactly one posting per bid");
    {
        /* identifiers: SHA-256(seed || tag || seq) */
        uint8_t in[41], d[32];
        char uetr[PAY_UETR_LEN + 1];
        const pay_journal_t *jj = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
        memcpy(in, SEED, 32);
        in[32] = 'U';
        for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) ((aseq + 3) >> (8 * i));
        sha256(in, sizeof in, d);
        pay_uetr_from_random(d, uetr);
        in[32] = 'I';
        sha256(in, sizeof in, d);
        char e2e[32];
        snprintf(e2e, sizeof e2e, "VAUC-%llu", (unsigned long long) (aseq + 3));
        CHECK(!strcmp(jj->uetr, uetr) && !memcmp(jj->idem_key, d, 32) && !strcmp(jj->e2e, e2e),
              "UETR, idempotency key and e2e derive from the seed and the sequence");
    }
    CHECK(deb(rs) - r0 == 150 * 100 && deb(es) == 0, "the reserve gains price * qty; escrow empty");
    CHECK(deb(bf[0]) == 1000000 - 9000 && deb(bf[1]) == 1000000 - 3450 &&
              deb(bf[2]) == 1000000 - 2550 && deb(bf[3]) == 1000000,
          "each pays fill * 150; the rest and the loser are refunded");
    CHECK(deb(bv[0]) == 60 && deb(bv[1]) == 23 && deb(bv[2]) == 17 && deb(bv[3]) == 0 &&
              vauc_outstanding(&A) == 100,
          "V-Bills issued to the winners");
    CHECK(inv(), "invariants after settlement");
    const vauc_record_t *r = vauc_record(&A, 1);
    CHECK(r && r->epoch == 1 && r->price == 150 && r->qty == 100 && r->winners == 3 &&
              r->r_before == 0 && r->r_after == 15000 && r->n_before == 0 && r->n_after == 100 &&
              r->floor_ok && !r->cancelled,
          "the audit record");
    CHECK(vauc_floor_ok(&A, 2), "the floor holds after settlement");
    CHECK(vauc_settle(&A, 2) == VAUC_ERR_STATE && vauc_cancel(&A, 2) == VAUC_ERR_STATE &&
              vauc_clear(&A, 2) == VAUC_ERR_STATE && bid(0, 1, 150) == VAUC_ERR_STATE,
          "nothing runs twice");
    const pay_journal_t *j = &L.journal[(L.seq - 4) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 5 && j->kind == PAY_KIND_ISSUE && !strncmp(j->e2e, "VAUC-", 5),
          "a winning bid settles in one five-line posting");
    j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->n_lines == 2 && j->kind == PAY_KIND_RETURN, "a loser gets one refund posting");
    CHECK(vauc_open(&A) == VAUC_OK && A.epoch == 2 && A.n_bids == 0 && A.locked == 0 &&
              A.cur.epoch == 2 && A.cur.qty == 0,
          "the next epoch starts clean");
}

static void test_ties(void)
{
    {
        vauc_config_t c0 = cfg0();
        setup(&c0, 0, 0);
        vauc_open(&A);
        bid(1, 50, 150);
        bid(0, 60, 160);
        bid(2, 40, 150);
        vauc_clear(&A, 1);
        CHECK(A.cur.price == 150 && A.bid[1].fill == 60 && A.bid[0].fill == 23 &&
                  A.bid[2].fill == 17,
              "a limit exactly at the previous rung (160) fills in full");
        vauc_settle(&A, 1);
        CHECK(deb(bv[0]) == 60 && inv(), "settled");
    }
    vauc_config_t c = cfg0();
    c.cap = 11;
    for (int order = 0; order < 2; order++) {
        setup(&c, 0, 0);
        vauc_open(&A);
        int x = order ? 2 : 1, y = order ? 1 : 2;
        bid(x, 10, 150);
        bid(y, 10, 150);
        vauc_clear(&A, 1);
        CHECK(A.cur.price == 150 && A.bid[0].fill == 6 && A.bid[1].fill == 5,
              "equal bids: 5 each, the leftover unit to the earlier submission");
        vauc_settle(&A, 1);
        CHECK(deb(bv[x]) == 6 && deb(bv[y]) == 5 && inv(), "whoever bid first gets the unit");
    }
    /* the first rung clears: everyone at or above p_start shares */
    c.cap = 100;
    setup(&c, 0, 0);
    vauc_open(&A);
    bid(0, 200, 250);
    bid(1, 100, 200);
    bid(2, 50, 190);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 200 && A.bid[0].fill == 67 && A.bid[1].fill == 33 && A.bid[2].fill == 0,
          "p_start clears: 66 + 33 pro rata, leftover to the first");
    vauc_settle(&A, 1);
    CHECK(deb(bf[0]) == 1000000 - 67 * 200 && deb(rs) == 20000 && inv(),
          "a limit above p_start still pays the uniform price");
    /* leftover several units, spread in order */
    c.cap = 5;
    setup(&c, 0, 0);
    vauc_open(&A);
    for (int i = 0; i < 4; i++) bid(i, 3, 150);
    vauc_clear(&A, 1);
    CHECK(A.bid[0].fill == 2 && A.bid[1].fill == 1 && A.bid[2].fill == 1 && A.bid[3].fill == 1 &&
              A.cur.winners == 4,
          "5 over 4 x 3: 1 each, then one leftover unit to the first in order");
    c.cap = 7;
    setup(&c, 0, 0);
    vauc_open(&A);
    for (int i = 0; i < 4; i++) bid(i, 3, 150);
    vauc_clear(&A, 1);
    CHECK(A.bid[0].fill == 2 && A.bid[1].fill == 2 && A.bid[2].fill == 2 && A.bid[3].fill == 1,
          "7 over 4 x 3: 1 each, three leftovers to the first three");
    vauc_settle(&A, 1);
    CHECK(deb(rs) == 7 * 150 && inv(), "settled");
}

static void test_undersubscribed(void)
{
    vauc_config_t c = cfg0();
    c.cap = 1000;
    setup(&c, 0, 0);
    vauc_open(&A);
    bid(0, 60, 180);
    bid(1, 50, 150);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 100 && A.cur.qty == 110 && A.bid[0].fill == 60 && A.bid[1].fill == 50 &&
              A.cur.winners == 2,
          "never reached: clears at p_min with D(p_min), all filled");
    vauc_settle(&A, 1);
    CHECK(deb(rs) == 11000 && deb(bf[0]) == 1000000 - 6000 && inv(), "everyone pays 100");
    /* a bid exactly at the price pays its whole lock: four lines, no refund line */
    vauc_open(&A);
    bid(2, 50, 100);
    bid(3, 5, 101);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 100 && A.bid[0].fill == 50 && A.bid[1].fill == 5, "both fill at 100");
    vauc_settle(&A, 1);
    CHECK(L.journal[(L.seq - 2) % PAY_JOURNAL_MAX].n_lines == 4 &&
              L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].n_lines == 5 &&
              deb(bf[2]) == 1000000 - 5000 && inv(),
          "limit == price: no refund line");
    /* no bids at all */
    vauc_open(&A);
    CHECK(vauc_clear(&A, 2) == VAUC_OK && A.cur.price == 100 && A.cur.qty == 0, "empty epoch");
    CHECK(vauc_settle(&A, 2) == VAUC_OK && vauc_record(&A, 3)->qty == 0 && inv(), "settles empty");
    /* the ladder lands on p_min with a short last step */
    c.p_start = 205;
    c.cap = 30;
    vauc_set_config(&A, &c);
    vauc_open(&A);
    bid(0, 30, 100);
    vauc_clear(&A, 3);
    CHECK(A.cur.price == 100 && A.cur.qty == 30, "205, 195, ..., 105, then exactly 100");
    vauc_settle(&A, 3);
    vauc_open(&A);
    bid(0, 30, 104);
    bid(1, 30, 105);
    vauc_clear(&A, 4);
    CHECK(A.cur.price == 105 && A.bid[1].fill == 30 && A.bid[0].fill == 0,
          "105 is a rung, 104 is not reached before 100... and 105 clears");
    vauc_settle(&A, 4);
    CHECK(inv(), "invariants");
}

static void test_floor_binds(void)
{
    vauc_config_t c = cfg0();
    c.backing = 100;
    c.cap = 2000;
    c.p_start = 120;
    c.p_min = 50;
    /* R = 50000, N = 300: Q(p) = 20000 / (100 - p) */
    setup(&c, 50000, 300);
    CHECK(vauc_floor_ok(&A, 1), "50000 >= 100 * 300");
    vauc_open(&A);
    bid(0, 400, 90);
    bid(1, 300, 80);
    bid(2, 100, 70);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 70 && A.cur.qty == 666 && A.cur.r_before == 50000 && A.cur.n_before == 300,
          "S(80) = 1000 > D 700, S(70) = 666 <= D 800: clears at 70 for Q(70)");
    CHECK(A.bid[0].fill == 381 && A.bid[1].fill == 285 && A.bid[2].fill == 0,
          "D(80) = 700 > 666: the limit >= 80 bids share 666 (380 + 285 + 1), the 70 bid none");
    block_e2e(A.seq + 1);
    CHECK(vauc_settle(&A, 1) == VAUC_ERR_LEDGER && A.bid[0].settled, "interrupted after bid 0");
    CHECK(vauc_settle(&A, 1) == VAUC_OK, "the floor re-check counts only what is left to issue");
    CHECK(deb(rs) == 50000 + 666 * 70 && vauc_outstanding(&A) == 966 && vauc_floor_ok(&A, 1) &&
              inv(),
          "issued exactly up to the floor: 96620 >= 96600");
    const vauc_record_t *r = vauc_record(&A, 1);
    CHECK(r->r_after == 96620 && r->n_after == 966 && r->floor_ok, "record after");

    /* full fills above the marginal rung, the rest shared at the margin */
    setup(&c, 50000, 300);
    vauc_open(&A);
    bid(0, 300, 90);
    bid(1, 400, 70);
    bid(2, 300, 70);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 70 && A.cur.qty == 666 && A.bid[0].fill == 300 && A.bid[1].fill == 210 &&
              A.bid[2].fill == 156,
          "D(80) = 300 <= 666: 300 full; 366 over 700 at the margin: 209 + 156 + 1");
    vauc_settle(&A, 1);
    CHECK(vauc_floor_ok(&A, 1) && inv(), "floor holds");

    /* exactly at the floor: only p >= backing has supply; the clock stops */
    setup(&c, 30000, 300);
    vauc_open(&A);
    bid(0, 50, 110);
    bid(1, 50, 60);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 100 && A.cur.qty == 50 && A.bid[0].fill == 50 && A.bid[1].fill == 0,
          "S(90) = 0: clears at the lowest rung with supply, 100, filling D(100)");
    vauc_settle(&A, 1);
    CHECK(deb(rs) == 35000 && vauc_outstanding(&A) == 350 && vauc_floor_ok(&A, 1) && inv(),
          "35000 >= 100 * 350");

    /* below the floor: nothing issues, every lock comes back */
    setup(&c, 29999, 300);
    CHECK(!vauc_floor_ok(&A, 1), "29999 < 30000");
    vauc_open(&A);
    bid(0, 50, 120);
    bid(1, 50, 200);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 50 && A.cur.qty == 0 && A.cur.winners == 0 && A.bid[1].fill == 0,
          "reserve below the floor issues nothing");
    vauc_settle(&A, 1);
    CHECK(deb(rs) == 29999 && deb(bf[0]) == 1000000 && deb(bf[1]) == 1000000 &&
              vauc_outstanding(&A) == 300 && inv(),
          "all refunded, nothing burned");
    CHECK(!vauc_record(&A, 1)->floor_ok, "the record shows the floor still short");
}

/* ---- A1 ---- */
enum { GOLD = 7, CORN = 9, C1 = 100, C2 = 101, O1 = 200, O2 = 201, O3 = 202, O4 = 203 };

static vauc_status_t quote(uint32_t o, uint32_t com, uint64_t price, uint64_t tick)
{
    uint8_t m[VAUC_QUOTE_MSG], sig[32];
    vauc_quote_msg(com, price, tick, m);
    sign(o, m, sizeof m, sig);
    return vauc_quote(&A, o, com, price, tick, sig, 32);
}

static void mkid(uint8_t id[32], uint32_t n)
{
    memset(id, 0, 32);
    memcpy(id, &n, 4);
    id[31] = 0x77;
}

static vauc_status_t receipt_by(uint32_t signer, uint32_t cust, uint32_t n, uint32_t com,
                                uint64_t qty, uint64_t expiry, uint64_t now)
{
    uint8_t id[32], m[VAUC_RCPT_MSG], sig[32];
    mkid(id, n);
    vauc_receipt_msg(cust, id, com, qty, expiry, m);
    sign(signer, m, sizeof m, sig);
    return vauc_receipt_add(&A, cust, id, com, qty, expiry, now, sig, 32);
}

static vauc_status_t receipt(uint32_t cust, uint32_t n, uint32_t com, uint64_t qty, uint64_t expiry,
                             uint64_t now)
{
    return receipt_by(cust, cust, n, com, qty, expiry, now);
}

static uint64_t counted(uint32_t n, uint64_t now)
{
    uint8_t id[32];
    mkid(id, n);
    return vauc_receipt_counted(&A, id, now);
}

static void collateral(void)
{
    CHECK(vauc_custodian_set(&A, C1, 2000) == VAUC_OK && vauc_custodian_set(&A, C2, 0) == VAUC_OK,
          "custodians with haircuts");
    for (uint32_t o = O1; o <= O4; o++) vauc_oracle_add(&A, o);
    CHECK(vauc_feed_add(&A, GOLD, 10) == VAUC_OK && vauc_feed_add(&A, CORN, 1) == VAUC_OK, "feeds");
}

static void test_quotes(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    collateral();
    uint64_t med = 1;
    CHECK(vauc_median(&A, GOLD, 100, &med) == VAUC_ERR_QUOTES && med == 0, "no quotes yet");
    CHECK(quote(O1, GOLD, 500, 100) == VAUC_OK && quote(O2, GOLD, 520, 100) == VAUC_OK,
          "two quotes");
    CHECK(vauc_median(&A, GOLD, 100, &med) == VAUC_ERR_QUOTES, "two fresh < min_quotes 3");
    CHECK(quote(O3, GOLD, 480, 100) == VAUC_OK && vauc_median(&A, GOLD, 100, &med) == VAUC_OK &&
              med == 500,
          "480, 500, 520: median 500");
    CHECK(quote(O4, GOLD, 900, 99) == VAUC_OK && vauc_median(&A, GOLD, 100, &med) == VAUC_OK &&
              med == 500,
          "480, 500, 520, 900: the LOWER median, 500");
    CHECK(vauc_median(&A, GOLD, 109, &med) == VAUC_OK && med == 500,
          "at 109 the tick-99 quote is exactly max_age old: still fresh");
    CHECK(vauc_median(&A, GOLD, 110, &med) == VAUC_OK && med == 500,
          "at 110 the 900 quote is stale: 480, 500, 520");
    CHECK(vauc_median(&A, GOLD, 111, &med) == VAUC_ERR_QUOTES, "at 111 everything is stale");
    CHECK(vauc_median(&A, GOLD, 99, &med) == VAUC_ERR_QUOTES,
          "quotes from the future are not fresh");
    CHECK(quote(O1, GOLD, 10, 105) == VAUC_OK && vauc_median(&A, GOLD, 105, &med) == VAUC_OK &&
              med == 480,
          "O1 moves to 10: 10, 480, 520, 900 -> 480");
    CHECK(quote(O2, GOLD, 11, 105) == VAUC_OK && vauc_median(&A, GOLD, 105, &med) == VAUC_OK &&
              med == 11,
          "10, 11, 480, 900 -> 11");
    CHECK(quote(O1, GOLD, 600, 105) == VAUC_ERR_STALE &&
              quote(O1, GOLD, 600, 104) == VAUC_ERR_STALE,
          "a quote must be newer than the oracle's last");
    CHECK(vauc_median(&A, GOLD, 105, &med) == VAUC_OK && med == 11,
          "a refused quote changes nothing");
    uint8_t m[VAUC_QUOTE_MSG], sig[32];
    vauc_quote_msg(GOLD, 700, 106, m);
    sign(O2, m, sizeof m, sig);
    CHECK(vauc_quote(&A, O1, GOLD, 700, 106, sig, 32) == VAUC_ERR_SIG, "O2 cannot sign for O1");
    sign(O1, m, sizeof m, sig);
    CHECK(vauc_quote(&A, O1, GOLD, 701, 106, sig, 32) == VAUC_ERR_SIG &&
              vauc_quote(&A, O1, GOLD, 700, 107, sig, 32) == VAUC_ERR_SIG &&
              vauc_quote(&A, O1, CORN, 700, 106, sig, 32) == VAUC_ERR_SIG,
          "the signature covers commodity, price and tick");
    CHECK(vauc_quote(&A, O1, GOLD, 700, 106, sig, 31) == VAUC_ERR_SIG, "short signature");
    CHECK(vauc_quote(&A, O1, GOLD, 700, 106, NULL, 32) == VAUC_ERR_ARG &&
              vauc_quote(NULL, O1, GOLD, 700, 106, sig, 32) == VAUC_ERR_ARG &&
              quote(O1, GOLD, 0, 106) == VAUC_ERR_ARG,
          "missing signature or zero price refused");
    CHECK(quote(999, GOLD, 5, 106) == VAUC_ERR_UNKNOWN &&
              quote(O1, 999, 5, 106) == VAUC_ERR_UNKNOWN,
          "unknown oracle or commodity");
    CHECK(vauc_quote(&A, O1, GOLD, 700, 106, sig, 32) == VAUC_OK && A.feed[0].price[0] == 700,
          "the right signature is accepted");
    CHECK(vauc_median(&A, 999, 1, &med) == VAUC_ERR_UNKNOWN &&
              vauc_median(&A, GOLD, 1, NULL) == VAUC_ERR_ARG &&
              vauc_median(NULL, GOLD, 1, &med) == VAUC_ERR_ARG,
          "median arguments");
    CHECK(quote(O1, CORN, 3, 0) == VAUC_OK, "a first quote may carry tick 0");

    CHECK(vauc_oracle_add(&A, O1) == VAUC_ERR_ARG && vauc_oracle_add(NULL, 1) == VAUC_ERR_ARG,
          "an oracle is registered once");
    for (uint32_t o = 0; o < VAUC_MAX_QUOTES - 4; o++)
        CHECK(vauc_oracle_add(&A, 300 + o) == VAUC_OK, "more oracles");
    CHECK(vauc_oracle_add(&A, 400) == VAUC_ERR_FULL, "oracle table full");
    CHECK(vauc_feed_add(&A, GOLD, 5) == VAUC_ERR_ARG && vauc_feed_add(&A, 50, 0) == VAUC_ERR_ARG &&
              vauc_feed_add(NULL, 50, 1) == VAUC_ERR_ARG,
          "a feed once, with a nonzero scale");
    CHECK(vauc_feed_add(&A, 50, 1) == VAUC_OK && vauc_feed_add(&A, 51, 1) == VAUC_OK &&
              vauc_feed_add(&A, 52, 1) == VAUC_ERR_FULL,
          "feed table full");

    /* all eight oracles: lower median of eight */
    c.min_quotes = VAUC_MAX_QUOTES;
    vauc_set_config(&A, &c);
    static const uint64_t px[VAUC_MAX_QUOTES] = {80, 10, 70, 20, 60, 30, 50, 40};
    for (uint32_t o = 0; o < VAUC_MAX_QUOTES; o++) quote(A.oracle[o], 50, px[o], 1);
    CHECK(vauc_median(&A, 50, 1, &med) == VAUC_OK && med == 40, "10..80 in any order: 40");
}

static void test_receipts(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 1000, 0);
    collateral();
    quote(O1, GOLD, 500, 100);
    quote(O2, GOLD, 520, 100);
    quote(O3, GOLD, 480, 100);
    quote(O4, GOLD, 900, 100);
    nverify = 0;
    CHECK(receipt(C1, 1, GOLD, 30, 200, 100) == VAUC_OK && nverify == 1, "receipt 1, one check");
    CHECK(counted(1, 100) == 1200, "30 * 500 / 10 = 1500, less 20%: 1200");
    uint64_t R = 0;
    CHECK(vauc_reserve(&A, 100, &R) == VAUC_OK && R == 1000 + 1200, "R = fiat + receipts");
    CHECK(vauc_custodian_set(&A, C1, 1234) == VAUC_OK && counted(1, 100) == 1314,
          "haircut 12.34%: floor(1500 * 8766 / 10000) = 1314");
    CHECK(receipt(C1, 2, GOLD, 33, 200, 100) == VAUC_OK && counted(2, 100) == 1446,
          "33 * 500 / 10 = 1650 -> floor(1650 * 0.8766) = 1446");
    CHECK(receipt(C2, 3, GOLD, 3, 200, 100) == VAUC_OK && counted(3, 100) == 150,
          "no haircut: 150");
    CHECK(receipt(C2, 4, GOLD, 1, 200, 100) == VAUC_OK && counted(4, 100) == 50 &&
              vauc_custodian_set(&A, C2, 10000) == VAUC_OK && counted(4, 100) == 0 &&
              counted(3, 100) == 0,
          "a 100% haircut counts nothing");
    vauc_custodian_set(&A, C2, 0);
    CHECK(receipt(C2, 5, GOLD, 1, 200, 100) == VAUC_OK, "receipt 5");
    CHECK(counted(5, 100) == 50 && counted(5, 110) == 50 && counted(5, 111) == 0,
          "stale quotes: counts 0 (fail closed)");
    CHECK(counted(5, 99) == 0, "quotes from the future do not count");
    CHECK(vauc_reserve(&A, 111, &R) == VAUC_OK && R == 1000, "stale: only fiat");

    /* too few quotes for corn */
    CHECK(receipt(C2, 6, CORN, 10, 200, 100) == VAUC_OK && counted(6, 100) == 0,
          "no corn quotes: 0");
    quote(O1, CORN, 7, 100);
    quote(O2, CORN, 8, 100);
    CHECK(counted(6, 100) == 0, "two corn quotes < 3: 0");
    quote(O3, CORN, 9, 100);
    CHECK(counted(6, 100) == 80, "three: 10 * 8 / 1");

    /* expiry */
    CHECK(receipt(C2, 7, GOLD, 2, 150, 100) == VAUC_OK && counted(7, 105) == 100, "receipt 7");
    quote(O1, GOLD, 500, 140);
    quote(O2, GOLD, 500, 140);
    quote(O3, GOLD, 500, 140);
    CHECK(counted(7, 149) == 100 && counted(7, 150) == 0 && counted(7, 151) == 0,
          "expired at its expiry tick");
    CHECK(receipt(C2, 8, GOLD, 2, 100, 100) == VAUC_ERR_EXPIRED &&
              receipt(C2, 8, GOLD, 2, 99, 100) == VAUC_ERR_EXPIRED,
          "an already expired receipt is refused");
    CHECK(receipt(C2, 8, GOLD, 2, 101, 100) == VAUC_OK, "one tick of life is fine");

    /* duplicates, signatures, unknowns */
    CHECK(receipt(C2, 1, GOLD, 30, 200, 100) == VAUC_ERR_DUP, "the same id twice is refused");
    CHECK(receipt(C1, 1, GOLD, 99, 300, 100) == VAUC_ERR_DUP, "even with other terms");
    {
        uint8_t i2[32], m2[VAUC_RCPT_MSG], s2[32];
        mkid(i2, 1);
        i2[31] ^= 0x80; /* differs from receipt 1 in the last byte only */
        vauc_receipt_msg(C2, i2, GOLD, 1, 101, m2);
        sign(C2, m2, sizeof m2, s2);
        CHECK(vauc_receipt_add(&A, C2, i2, GOLD, 1, 101, 100, s2, 32) == VAUC_OK,
              "every byte of the id counts");
    }
    CHECK(receipt_by(C2, C1, 9, GOLD, 1, 200, 100) == VAUC_ERR_SIG, "C2 cannot sign C1's receipt");
    uint8_t id[32], m[VAUC_RCPT_MSG], sig[32];
    mkid(id, 9);
    vauc_receipt_msg(C1, id, GOLD, 5, 200, m);
    sign(C1, m, sizeof m, sig);
    CHECK(vauc_receipt_add(&A, C1, id, GOLD, 6, 200, 100, sig, 32) == VAUC_ERR_SIG &&
              vauc_receipt_add(&A, C1, id, GOLD, 5, 201, 100, sig, 32) == VAUC_ERR_SIG &&
              vauc_receipt_add(&A, C1, id, CORN, 5, 200, 100, sig, 32) == VAUC_ERR_SIG &&
              vauc_receipt_add(&A, C1, id, GOLD, 5, 200, 100, sig, 30) == VAUC_ERR_SIG,
          "the signature covers qty, expiry and commodity");
    id[5] ^= 1;
    CHECK(vauc_receipt_add(&A, C1, id, GOLD, 5, 200, 100, sig, 32) == VAUC_ERR_SIG, "and the id");
    id[5] ^= 1;
    CHECK(vauc_receipt_add(&A, C1, id, GOLD, 5, 200, 100, sig, 32) == VAUC_OK, "the right one");
    CHECK(receipt(999, 10, GOLD, 1, 200, 100) == VAUC_ERR_UNKNOWN &&
              receipt(C1, 10, 999, 1, 200, 100) == VAUC_ERR_UNKNOWN,
          "unknown custodian or commodity refused");
    CHECK(receipt(C1, 10, GOLD, 0, 200, 100) == VAUC_ERR_ARG &&
              vauc_receipt_add(&A, C1, NULL, GOLD, 1, 200, 100, sig, 32) == VAUC_ERR_ARG &&
              vauc_receipt_add(&A, C1, id, GOLD, 1, 200, 100, NULL, 32) == VAUC_ERR_ARG &&
              vauc_receipt_add(NULL, C1, id, GOLD, 1, 200, 100, sig, 32) == VAUC_ERR_ARG,
          "zero qty or missing pieces refused");
    CHECK(vauc_custodian_set(&A, C1, 10001) == VAUC_ERR_ARG &&
              vauc_custodian_set(NULL, C1, 1) == VAUC_ERR_ARG,
          "haircut above 100% refused");

    /* dispute -> quarantine (now 150: O1-O3 quote 500, O4 is stale) */
    uint8_t q[4][32];
    CHECK(vauc_quarantine_list(&A, q, 4) == 0, "nothing quarantined");
    CHECK(counted(2, 150) == 1446 && counted(1, 150) == 1314, "receipts 1 and 2 count");
    mkid(id, 2);
    CHECK(vauc_receipt_dispute(&A, id) == VAUC_OK && counted(2, 150) == 0,
          "a disputed receipt stops counting");
    CHECK(vauc_receipt_dispute(&A, id) == VAUC_ERR_STATE, "it is quarantined once");
    CHECK(vauc_quarantine_list(&A, q, 4) == 1 && !memcmp(q[0], id, 32), "and listed in quarantine");
    mkid(id, 1);
    vauc_receipt_dispute(&A, id);
    memset(q, 0, sizeof q);
    CHECK(vauc_quarantine_list(&A, q, 1) == 2 && !memcmp(q[0], id, 32) && q[1][0] == 0,
          "the count is total; only max ids are copied");
    uint8_t one[1][32];
    CHECK(vauc_quarantine_list(&A, one, 1) == 2 && !memcmp(one[0], id, 32), "a one-row buffer");
    CHECK(vauc_quarantine_list(&A, NULL, 4) == 2 && vauc_quarantine_list(NULL, q, 4) == 0,
          "count only");
    mkid(id, 77);
    CHECK(vauc_receipt_dispute(&A, id) == VAUC_ERR_UNKNOWN &&
              vauc_receipt_dispute(&A, NULL) == VAUC_ERR_ARG &&
              vauc_receipt_dispute(NULL, id) == VAUC_ERR_ARG,
          "unknown receipt");
    CHECK(vauc_receipt_counted(&A, id, 150) == 0 && vauc_receipt_counted(&A, NULL, 150) == 0 &&
              vauc_receipt_counted(NULL, id, 150) == 0,
          "unknown counts 0");
    /* R at 150: fiat 1000 + r3 150 + r4 50 + r5 50 + r6 80? corn quotes at 100 are stale + r8
     * expired (101) + r9 C1 5*50*0.8766 = 219 */
    CHECK(vauc_reserve(&A, 150, &R) == VAUC_OK && R == 1000 + 150 + 50 + 50 + 219,
          "R sums only live, priced, undisputed receipts");
    CHECK(vauc_reserve(&A, 150, NULL) == VAUC_ERR_ARG &&
              vauc_reserve(NULL, 150, &R) == VAUC_ERR_ARG,
          "reserve arguments");

    /* custodian table */
    for (uint32_t i = 0; i < VAUC_MAX_CUSTODIANS - 2; i++)
        CHECK(vauc_custodian_set(&A, 500 + i, i) == VAUC_OK, "more custodians");
    CHECK(vauc_custodian_set(&A, 600, 0) == VAUC_ERR_FULL, "custodian table full");
    CHECK(vauc_custodian_set(&A, 505, 77) == VAUC_OK && A.cust[7].haircut_bps == 77,
          "an existing custodian is updated even when the table is full");
}

static void test_receipt_slots(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    collateral();
    for (uint32_t i = 0; i < VAUC_MAX_RECEIPTS; i++)
        CHECK(receipt(C2, 1000 + i, GOLD, 1, 200, 100) == VAUC_OK, "fill the receipt table");
    CHECK(receipt(C2, 2000, GOLD, 1, 300, 100) == VAUC_ERR_FULL, "table full");
    uint8_t id[32];
    mkid(id, 1000);
    vauc_receipt_dispute(&A, id);
    CHECK(receipt(C2, 2000, GOLD, 1, 300, 199) == VAUC_ERR_FULL, "nothing expired yet at 199");
    CHECK(receipt(C2, 1001, GOLD, 1, 300, 200) == VAUC_ERR_DUP,
          "an expired receipt still held cannot be renewed under the same id");
    CHECK(receipt(C2, 2000, GOLD, 1, 300, 200) == VAUC_OK && A.rcpt[1].used &&
              A.rcpt[1].expiry == 300 && A.rcpt[0].quarantined,
          "at 200 the first expired slot is reused, never the quarantined one");
    for (uint32_t i = 1; i < VAUC_MAX_RECEIPTS - 1; i++)
        CHECK(receipt(C2, 2000 + i, GOLD, 1, 300, 200) == VAUC_OK, "reuse every expired slot");
    CHECK(receipt(C2, 3000, GOLD, 1, 300, 200) == VAUC_ERR_FULL &&
              vauc_quarantine_list(&A, NULL, 0) == 1,
          "full again: the quarantined slot is kept for audit");
    CHECK(receipt(C2, 1001, GOLD, 1, 200, 200) == VAUC_ERR_EXPIRED,
          "a replay of the dropped receipt is expired and refused");
}

static void test_overflow(void)
{
    vauc_config_t c = cfg0();
    c.max_age = 1000;
    setup(&c, 0, 0);
    collateral();
    for (uint32_t o = O1; o <= O3; o++) quote(o, CORN, 1ull << 40, 10);
    CHECK(receipt(C2, 1, CORN, 1ull << 30, 5000, 10) == VAUC_OK && counted(1, 10) == 0,
          "a value past 64 bits counts 0 (fail closed)");
    for (uint32_t o = O1; o <= O3; o++) quote(o, GOLD, 1ull << 33, 20);
    CHECK(receipt(C2, 2, GOLD, 10ull << 30, 5000, 10) == VAUC_OK && counted(2, 20) == 1ull << 63,
          "2^63 counts");
    uint64_t R = 7;
    CHECK(vauc_reserve(&A, 20, &R) == VAUC_OK && R == 1ull << 63, "R = 2^63");
    CHECK(receipt(C2, 3, GOLD, 10ull << 30, 5000, 10) == VAUC_OK &&
              vauc_reserve(&A, 20, &R) == VAUC_ERR_OVERFLOW && R == 1ull << 63,
          "2^64 overflows R");
    CHECK(!vauc_floor_ok(&A, 20), "floor_ok fails closed");
    vauc_open(&A);
    bid(0, 10, 150);
    CHECK(vauc_clear(&A, 20) == VAUC_ERR_OVERFLOW && A.state == VAUC_OPEN,
          "clearing refuses and stays OPEN");
    CHECK(vauc_clear(&A, 5) == VAUC_OK && A.cur.qty == 10, "at tick 5 gold is unpriced: clears");
    CHECK(vauc_settle(&A, 20) == VAUC_ERR_OVERFLOW && A.state == VAUC_CLEARED && inv(),
          "settlement re-values R and refuses on overflow");
    CHECK(vauc_settle(&A, 5) == VAUC_OK && inv(), "settles where R fits");
    vauc_open(&A);
    bid(0, 10, 150);
    CHECK(vauc_cancel(&A, 20) == VAUC_OK && vauc_record(&A, 2)->r_after == 0 &&
              !vauc_record(&A, 2)->floor_ok,
          "a record whose R overflows shows 0 and a failed floor check");

    /* R just fits, R + q*p does not */
    setup(&c, 0, 0);
    collateral();
    for (uint32_t o = O1; o <= O3; o++) quote(o, CORN, UINT64_MAX, 10);
    receipt(C2, 1, CORN, 1, 5000, 10);
    CHECK(vauc_reserve(&A, 10, &R) == VAUC_OK && R == UINT64_MAX, "R = 2^64 - 1");
    vauc_open(&A);
    bid(0, 10, 150);
    CHECK(vauc_clear(&A, 10) == VAUC_OK && A.cur.qty == 10, "cleared");
    CHECK(vauc_settle(&A, 10) == VAUC_ERR_FLOOR && inv(), "R + q*p would overflow: refused");
}

static void test_bids(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    vauc_open(&A);
    uint32_t ix = 77;
    uint64_t seq = L.seq;
    CHECK(vauc_bid(&A, bf[0], bv[0], 10001, 100, 1, &ix) == VAUC_ERR_LEDGER &&
              A.last_ledger == PAY_ERR_FUNDS && ix == 77 && A.n_bids == 0 && A.locked == 0 &&
              deb(bf[0]) == 1000000 && L.seq == seq && inv(),
          "insufficient funds: refused, nothing recorded or locked");
    CHECK(vauc_bid(&A, bf[0], bv[0], 10000, 100, 1, &ix) == VAUC_OK && ix == 0 && deb(bf[0]) == 0 &&
              inv(),
          "exactly all funds can be locked");
    CHECK(bid(1, 0, 150) == VAUC_ERR_ARG && bid(1, 1, 99) == VAUC_ERR_ARG,
          "zero qty or a limit below p_min refused");
    CHECK(vauc_bid(&A, bv[1], bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, bf[1], bf[1], 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, 999, bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, bf[1], 999, 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, oth, bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG,
          "accounts of the wrong asset refused");
    CHECK(vauc_bid(&A, es, bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, rs, bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG &&
              vauc_bid(&A, bf[1], vi, 1, 150, 1, NULL) == VAUC_ERR_ARG,
          "the module's own accounts cannot bid");
    CHECK(vauc_bid(NULL, bf[1], bv[1], 1, 150, 1, NULL) == VAUC_ERR_ARG, "null");
    CHECK(bid(1, 1ull << 62, 8) == VAUC_ERR_ARG, "qty * limit past 64 bits refused");
    CHECK(bid(1, 1, 100) == VAUC_OK && A.bid[1].locked == 100 && A.locked == 1000100,
          "a bid at p_min is fine");
    for (uint32_t i = 2; i < VAUC_MAX_BIDS; i++) CHECK(bid(2, 1, 100) == VAUC_OK, "fill bids");
    CHECK(bid(2, 1, 100) == VAUC_ERR_FULL && A.n_bids == VAUC_MAX_BIDS, "bid table full");
    CHECK(vauc_cancel(&A, 3) == VAUC_OK && deb(bf[0]) == 1000000 && deb(bf[2]) == 1000000 &&
              deb(es) == 0 && inv(),
          "cancel refunds every lock");

    /* the epoch's total lock stays below PAY_BAL_MAX (one line moves at most 2^59) */
    setup(&c, 0, 0);
    for (int i = 0; i < 7; i++) fund(fi, bf[4], 1ull << 59);
    vauc_open(&A);
    for (int i = 0; i < 7; i++) CHECK(bid(4, 1ull << 52, 128) == VAUC_OK, "2^59 locked");
    CHECK(A.locked == 7ull << 59 && bid(5, 1ull << 52, 128) == VAUC_ERR_ARG,
          "an eighth 2^59 would reach 2^62: refused");
    CHECK(bid(5, (1ull << 52) - 1, 128) == VAUC_ERR_LEDGER && A.last_ledger == PAY_ERR_FUNDS,
          "just below passes the bound, then needs funds");
    CHECK(inv(), "invariants");
}

static void test_settle_floor(void)
{
    vauc_config_t c = cfg0();
    c.backing = 100;
    c.cap = 2000;
    c.p_start = 120;
    c.p_min = 50;
    setup(&c, 49950, 300);
    collateral();
    quote(O1, GOLD, 500, 1);
    quote(O2, GOLD, 500, 1);
    quote(O3, GOLD, 500, 1);
    receipt(C2, 1, GOLD, 1, 100, 1); /* worth 50: R = 50000 */
    vauc_open(&A);
    bid(0, 400, 90);
    bid(1, 300, 80);
    bid(2, 100, 70);
    vauc_clear(&A, 1);
    CHECK(A.cur.price == 70 && A.cur.qty == 666 && A.cur.r_before == 50000,
          "a receipt counts in R like fiat");
    uint8_t id[32];
    mkid(id, 1);
    vauc_receipt_dispute(&A, id);
    uint64_t seq = L.seq;
    CHECK(vauc_settle(&A, 1) == VAUC_ERR_FLOOR && L.seq == seq && A.state == VAUC_CLEARED &&
              A.mode == 0,
          "the receipt is disputed: 96570 < 96600, settlement refused, nothing posted");
    CHECK(vauc_cancel(&A, 2) == VAUC_OK && A.state == VAUC_SETTLED, "the operator cancels");
    const vauc_record_t *r = vauc_record(&A, 1);
    CHECK(r && r->cancelled && r->qty == 0 && r->winners == 0 && r->r_after == 49950 &&
              r->n_after == 300 && r->floor_ok,
          "the cancelled epoch is recorded");
    CHECK(deb(bf[0]) == 1000000 && deb(bf[1]) == 1000000 && deb(bf[2]) == 1000000 &&
              A.bid[0].fill == 0 && vauc_outstanding(&A) == 300 && inv(),
          "every lock refunded, nothing issued");
}

static void test_resume(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    vauc_open(&A);
    bid(0, 60, 180);
    bid(1, 50, 150);
    bid(2, 40, 150);
    bid(3, 30, 120);
    vauc_clear(&A, 1);
    block_e2e(A.seq + 1);
    CHECK(vauc_settle(&A, 2) == VAUC_ERR_LEDGER && A.last_ledger == PAY_ERR_DUP_UETR &&
              A.state == VAUC_CLEARED && A.bid[0].settled && !A.bid[1].settled && inv(),
          "a refused posting stops settlement after the first bid; invariants hold");
    CHECK(vauc_cancel(&A, 2) == VAUC_ERR_STATE, "no cancel once settlement has posted");
    CHECK(vauc_settle(&A, 3) == VAUC_OK && deb(rs) == 15000 && deb(bv[1]) == 23 &&
              deb(bv[0]) == 60 && vauc_outstanding(&A) == 100 && inv(),
          "settlement resumes where it stopped: each bid exactly once");

    /* a cancel that is interrupted */
    vauc_open(&A);
    bid(0, 10, 150);
    bid(1, 10, 150);
    bid(2, 10, 150);
    block_e2e(A.seq + 1);
    CHECK(vauc_cancel(&A, 4) == VAUC_ERR_LEDGER && A.bid[0].settled && !A.bid[1].settled && inv(),
          "cancel stops on a refused refund");
    CHECK(bid(3, 1, 150) == VAUC_ERR_STATE && vauc_clear(&A, 4) == VAUC_ERR_STATE &&
              vauc_settle(&A, 4) == VAUC_ERR_STATE,
          "a cancelling epoch takes no bids, no clearing, no settlement");
    CHECK(vauc_cancel(&A, 5) == VAUC_OK && deb(bf[1]) == 1000000 - 23 * 150 &&
              deb(bf[2]) == 1000000 - 17 * 150 && vauc_record(&A, 2)->cancelled && inv(),
          "cancel resumes and refunds the rest");

    /* cancel straight from OPEN */
    vauc_open(&A);
    bid(0, 10, 150);
    CHECK(vauc_cancel(&A, 6) == VAUC_OK && A.state == VAUC_SETTLED && inv(), "cancel while open");
    CHECK(vauc_settle(NULL, 1) == VAUC_ERR_ARG && vauc_cancel(NULL, 1) == VAUC_ERR_ARG &&
              vauc_clear(NULL, 1) == VAUC_ERR_ARG && vauc_open(NULL) == VAUC_ERR_ARG,
          "null");
}

static void test_history(void)
{
    vauc_config_t c = cfg0();
    setup(&c, 0, 0);
    CHECK(vauc_record(&A, 0) == NULL && vauc_record(&A, 1) == NULL && vauc_record(NULL, 1) == NULL,
          "no records yet");
    for (uint64_t e = 1; e <= VAUC_HISTORY + 1; e++) {
        vauc_open(&A);
        bid(0, e, 150);
        vauc_clear(&A, e);
        vauc_settle(&A, e);
    }
    CHECK(vauc_record(&A, 1) == NULL, "epoch 1 has left the ring");
    CHECK(vauc_record(&A, 2) && vauc_record(&A, 2)->qty == 2 &&
              vauc_record(&A, VAUC_HISTORY + 1)->qty == VAUC_HISTORY + 1 &&
              vauc_record(&A, VAUC_HISTORY + 1)->price == 100,
          "the last VAUC_HISTORY epochs are kept");
    CHECK(vauc_record(&A, VAUC_HISTORY + 2) == NULL, "a future epoch has no record");
    CHECK(deb(rs) == 100 * (VAUC_HISTORY + 1) * (VAUC_HISTORY + 2) / 2 && inv(),
          "every epoch's proceeds are in the reserve");
}

int main(void)
{
    test_msgs();
    test_config();
    test_dilution();
    test_oversubscribed();
    test_ties();
    test_undersubscribed();
    test_floor_binds();
    test_quotes();
    test_receipts();
    test_receipt_slots();
    test_overflow();
    test_bids();
    test_settle_floor();
    test_resume();
    test_history();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail != 0;
}
