/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_engine.c — the libzxv facade: compress, route, decompress, and
 * the whole pipeline obligations -> net legs -> ledger with fees. */
#include <stdio.h>
#include <string.h>
#include "zxv_engine.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static zxv_engine_t E, E2;
static const uint8_t SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

static void open_engine(zxv_engine_t *e, uint32_t members)
{
    zxv_engine_init(e, "USD", 840, 2, SEED);
    for (uint32_t i = 0; i < members; i++) {
        uint32_t m;
        zxv_engine_add_member(e, &m);
    }
}

static void net_of(uint32_t members, const zxv_obligation_t *o, uint32_t n, int64_t *net)
{
    for (uint32_t m = 0; m < members; m++) net[m] = 0;
    for (uint32_t i = 0; i < n; i++) {
        net[o[i].from] -= (int64_t) o[i].amount;
        net[o[i].to] += (int64_t) o[i].amount;
    }
}

static void test_compress(void)
{
    open_engine(&E, 8);
    zxv_obligation_t in[AB_MAX_OBLIGATIONS], out[AB_MAX_MEMBERS];
    uint32_t k = 0;

    /* a pure cycle cancels completely */
    zxv_obligation_t cyc[3] = {{0, 1, 500}, {1, 2, 500}, {2, 0, 500}};
    CHECK(zxv_compress(&E, cyc, 3, out, AB_MAX_MEMBERS, &k) == ZXV_E_OK && k == 0,
          "A->B->C->A of equal amounts nets to nothing");

    /* random books: nets kept, at most members-1 legs, gross never grows */
    bool ok = true, fewer = true;
    for (int round = 0; round < 500 && ok; round++) {
        uint32_t n = 1 + (uint32_t) (rnd() % 200);
        uint64_t gross_in = 0, gross_out = 0;
        for (uint32_t i = 0; i < n; i++) {
            in[i].from = (uint32_t) (rnd() % 8);
            do in[i].to = (uint32_t) (rnd() % 8);
            while (in[i].to == in[i].from);
            in[i].amount = 1 + rnd() % 1000000;
            gross_in += in[i].amount;
        }
        if (zxv_compress(&E, in, n, out, AB_MAX_MEMBERS, &k) != ZXV_E_OK) {
            ok = false;
            break;
        }
        int64_t a[8], b[8];
        net_of(8, in, n, a);
        net_of(8, out, k, b);
        for (int m = 0; m < 8; m++) ok &= a[m] == b[m];
        for (uint32_t i = 0; i < k; i++) gross_out += out[i].amount;
        fewer &= k <= 7 && gross_out <= gross_in;
    }
    CHECK(ok, "500 random books: every net position kept exactly");
    CHECK(fewer, "at most members-1 legs, and gross never grows");

    /* refusals leave the output untouched */
    zxv_obligation_t keep = {7, 7, 7};
    out[0] = keep;
    zxv_obligation_t bad[2] = {{0, 1, 5}, {3, 3, 5}};
    CHECK(zxv_compress(&E, bad, 2, out, AB_MAX_MEMBERS, &k) == ZXV_E_ARG &&
              memcmp(&out[0], &keep, sizeof keep) == 0,
          "self-obligation refused, output untouched");
    bad[1] = (zxv_obligation_t){0, 9, 5};
    CHECK(zxv_compress(&E, bad, 2, out, AB_MAX_MEMBERS, &k) == ZXV_E_ARG, "unknown member");
    bad[1] = (zxv_obligation_t){0, 1, 0};
    CHECK(zxv_compress(&E, bad, 2, out, AB_MAX_MEMBERS, &k) == ZXV_E_ARG, "zero amount");
    bad[1] = (zxv_obligation_t){0, 1, ((uint64_t) 1 << 59) + 1};
    CHECK(zxv_compress(&E, bad, 2, out, AB_MAX_MEMBERS, &k) == ZXV_E_ARG, "over the leg limit");
    zxv_obligation_t two[2] = {{0, 1, 5}, {2, 3, 5}};
    CHECK(zxv_compress(&E, two, 2, out, 1, &k) == ZXV_E_FULL, "too small an output");
    CHECK(zxv_compress(&E, two, 2, out, 2, &k) == ZXV_E_OK && k == 2, "exact output size fits");
    CHECK(zxv_compress(&E, 0, 0, out, 2, &k) == ZXV_E_OK && k == 0, "empty book");
}

static void test_route(void)
{
    open_engine(&E, 5);
    zxv_corridor_t c[] = {
        {0, 1, 100, 1000}, /* A->B cheap */
        {1, 0, 300, 1000}, /* B->A dear: the metric is asymmetric */
        {0, 2, 900, 1000}, /* A->C direct, dear */
        {1, 2, 200, 1000}, /* A->B->C = 300 < 900 */
        {2, 3, 50, 10},    /* C->D small capacity */
        {1, 3, 400, 5000}, /* B->D */
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) zxv_engine_add_corridor(&E, &c[i]);
    uint32_t p[8], n;
    uint64_t ab, ba;
    CHECK(zxv_route(&E, 0, 1, 10, p, 8, &n, &ab) == ZXV_E_OK && n == 2 && ab == 100, "d(A->B)");
    CHECK(zxv_route(&E, 1, 0, 10, p, 8, &n, &ba) == ZXV_E_OK && n == 2 && ba == 300, "d(B->A)");
    CHECK(ab != ba, "d(A->B) != d(B->A)");
    uint64_t cost;
    CHECK(zxv_route(&E, 0, 2, 10, p, 8, &n, &cost) == ZXV_E_OK && n == 3 && p[0] == 0 &&
              p[1] == 1 && p[2] == 2 && cost == 300,
          "two cheap hops beat one dear one");
    CHECK(zxv_route(&E, 0, 3, 10, p, 8, &n, &cost) == ZXV_E_OK && cost == 350 && n == 4,
          "A->D through C while C->D has capacity");
    CHECK(zxv_route(&E, 0, 3, 11, p, 8, &n, &cost) == ZXV_E_OK && cost == 500 && n == 3 &&
              p[1] == 1,
          "over C->D's capacity the route moves to B->D");
    CHECK(zxv_route(&E, 0, 3, 5001, p, 8, &n, &cost) == ZXV_E_NO_ROUTE, "nothing carries 5001");
    CHECK(zxv_route(&E, 3, 0, 1, p, 8, &n, &cost) == ZXV_E_NO_ROUTE, "D has no way out");
    CHECK(zxv_route(&E, 4, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 1 && cost == 0,
          "to itself costs nothing");
    CHECK(zxv_route(&E, 0, 2, 10, p, 2, &n, &cost) == ZXV_E_FULL, "path buffer too small");
    CHECK(zxv_route(&E, 0, 9, 1, p, 8, &n, &cost) == ZXV_E_ARG, "unknown member");

    /* ties: equal cost goes to fewer hops, then to the lower predecessor */
    open_engine(&E, 5);
    zxv_corridor_t t[] = {{0, 4, 100, 9}, {0, 1, 50, 9}, {1, 4, 50, 9}, {0, 3, 60, 9},
                          {0, 2, 60, 9},  {3, 4, 40, 9}, {2, 4, 40, 9}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) zxv_engine_add_corridor(&E, &t[i]);
    CHECK(zxv_route(&E, 0, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 2 && cost == 100,
          "equal cost: the direct corridor wins on hops");
    t[0].fee_ppm = 101;
    open_engine(&E, 5);
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) zxv_engine_add_corridor(&E, &t[i]);
    CHECK(zxv_route(&E, 0, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 3 && cost == 100 && p[1] == 1,
          "equal cost and hops: lowest predecessor id");

    /* ties found late: a cheaper-to-reach member processed first, then an
     * equal-cost path through a lower id; then fewer hops found later */
    open_engine(&E, 5);
    zxv_corridor_t u[] = {{0, 3, 10, 9}, {3, 4, 90, 9}, {0, 2, 50, 9}, {2, 4, 50, 9}};
    for (unsigned i = 0; i < sizeof u / sizeof u[0]; i++) zxv_engine_add_corridor(&E, &u[i]);
    CHECK(zxv_route(&E, 0, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 3 && cost == 100 && p[1] == 2,
          "a tie found later through a lower id replaces the first");
    open_engine(&E, 5);
    zxv_corridor_t h[] = {{0, 1, 10, 9}, {1, 2, 10, 9}, {2, 4, 10, 9}, {0, 3, 25, 9}, {3, 4, 5, 9}};
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) zxv_engine_add_corridor(&E, &h[i]);
    CHECK(zxv_route(&E, 0, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 3 && cost == 30 && p[1] == 3,
          "a tie found later with fewer hops replaces the first");
    open_engine(&E, 5);
    zxv_corridor_t g[] = {{0, 1, 10, 9}, {1, 4, 20, 9}, {0, 3, 10, 9}, {3, 4, 30, 9}};
    for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++) zxv_engine_add_corridor(&E, &g[i]);
    CHECK(zxv_route(&E, 0, 4, 1, p, 8, &n, &cost) == ZXV_E_OK && n == 3 && cost == 30 && p[1] == 1,
          "a dearer path found later never replaces a cheaper one");

    zxv_corridor_t bad = {0, 0, 1, 1};
    CHECK(zxv_engine_add_corridor(&E, &bad) == ZXV_E_ARG, "self corridor refused");
    bad = (zxv_corridor_t){0, 1, ZXV_FEE_PPM_MAX + 1, 1};
    CHECK(zxv_engine_add_corridor(&E, &bad) == ZXV_E_ARG, "fee over 100% refused");
    bad = (zxv_corridor_t){0, 7, 1, 1};
    CHECK(zxv_engine_add_corridor(&E, &bad) == ZXV_E_ARG, "unknown member refused");
}

static void test_decompress(void)
{
    open_engine(&E, 4);
    CHECK(zxv_engine_fund(&E, 0, 1000000, 1) == ZXV_E_OK, "fund A");
    CHECK(zxv_engine_fund(&E, 1, 50000, 1) == ZXV_E_OK, "fund B");
    CHECK(zxv_engine_check(&E), "issuer's claim == everything held");

    zxv_obligation_t legs[3] = {{0, 2, 400000}, {0, 3, 123457}, {1, 2, 49000}};
    uint64_t fees = 0;
    CHECK(zxv_decompress(&E, legs, 3, 2, &fees) == ZXV_E_OK, "three legs post");
    uint64_t want = pay_assure_fee(400000 + 123457) /* A's carry runs across its legs */ +
                    pay_assure_fee(49000);
    CHECK(fees == want, "fees == floor(g * 8889 / 10^7) with each payer's carry");
    CHECK(zxv_engine_balance(&E, 2) == 449000 && zxv_engine_balance(&E, 3) == 123457,
          "payees receive the amount, no fee taken from them");
    CHECK(zxv_engine_balance(&E, 0) == 1000000 - 523457 - pay_assure_fee(523457),
          "payer pays amount + fee");
    uint64_t b = 0;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) b += zxv_engine_bucket(&E, i);
    CHECK(b == fees && E.fees == fees, "the four buckets hold exactly the fees");
    CHECK(zxv_engine_check(&E) && pay_ledger_verify_chain(&E.L), "books balance, chain verifies");

    /* G2: a payer short of amount + fee bound is refused before anything posts */
    uint8_t head[PAY_HASH_LEN];
    memcpy(head, E.L.chain_head, sizeof head);
    uint64_t seq = E.seq, bal1 = zxv_engine_balance(&E, 1);
    zxv_obligation_t short_leg = {1, 0, bal1};
    CHECK(zxv_decompress(&E, &short_leg, 1, 3, &fees) == ZXV_E_FUNDS && fees == 0,
          "cannot pay the whole balance: the fee would not fit");
    short_leg.amount = bal1 - pay_assure_fee(bal1) - 1;
    CHECK(zxv_decompress(&E, &short_leg, 1, 3, &fees) == ZXV_E_OK, "amount + fee bound fits");
    seq = E.seq;
    memcpy(head, E.L.chain_head, sizeof head);
    zxv_obligation_t two[2] = {{0, 1, 1000}, {0, 1, zxv_engine_balance(&E, 0)}};
    CHECK(zxv_decompress(&E, two, 2, 4, &fees) == ZXV_E_FUNDS && E.seq == seq &&
              memcmp(head, E.L.chain_head, sizeof head) == 0,
          "the batch is refused whole: nothing posted");

    /* G2: a refusal part-way rolls back what already posted */
    open_engine(&E, 3);
    zxv_engine_fund(&E, 0, 100000, 1);
    zxv_engine_fund(&E, 1, 100000, 1);
    pay_ledger_set_flags(&E.L, E.acct[1], PAY_ACCT_FROZEN); /* B cannot send */
    zxv_obligation_t mixed[2] = {{0, 2, 30000}, {1, 2, 20000}};
    CHECK(zxv_decompress(&E, mixed, 2, 5, &fees) == ZXV_E_LEDGER && fees == 0,
          "second leg refused by the ledger");
    CHECK(zxv_engine_balance(&E, 0) == 100000 && zxv_engine_balance(&E, 2) == 0 &&
              zxv_engine_bucket(&E, 0) == 0,
          "first leg rolled back: no partial state");
    CHECK(zxv_engine_check(&E) && pay_ledger_verify_chain(&E.L), "books balance after rollback");
    CHECK(E.fees == 0, "no fee counted for a rolled-back batch");

    /* withdrawals leave through the issuer */
    open_engine(&E, 1);
    zxv_engine_fund(&E, 0, 500, 1);
    CHECK(zxv_engine_withdraw(&E, 0, 501, 2) == ZXV_E_FUNDS, "cannot withdraw more than held");
    CHECK(zxv_engine_withdraw(&E, 0, 500, 2) == ZXV_E_OK && zxv_engine_balance(&E, 0) == 0,
          "withdraw all");
    CHECK(zxv_engine_check(&E) && E.L.acct[E.issuer].credit == 0, "issuer claim back to 0");
    CHECK(zxv_engine_fund(&E, 0, 0, 1) == ZXV_E_ARG && zxv_engine_fund(&E, 3, 1, 1) == ZXV_E_ARG,
          "fund refuses 0 and unknown members");
    zxv_obligation_t self = {0, 0, 1};
    CHECK(zxv_decompress(&E, &self, 1, 1, &fees) == ZXV_E_ARG, "self leg refused");
}

/* The whole pipeline, many times: random obligations are compressed, each
 * net leg is checked to have a route, and the legs settle with fees. Every
 * member ends at funding + net position - fees paid; the books balance. */
static void test_pipeline(void)
{
    bool ok = true;
    uint64_t total_fees = 0, gross = 0, netted = 0;
    for (int round = 0; round < 200 && ok; round++) {
        const uint32_t M = 12;
        open_engine(&E, M);
        for (uint32_t a = 0; a < M; a++)
            for (uint32_t b = 0; b < M; b++)
                if (a != b) {
                    zxv_corridor_t c = {a, b, (uint32_t) (rnd() % 5000), 1ull << 40};
                    zxv_engine_add_corridor(&E, &c);
                }
        for (uint32_t m = 0; m < M; m++) zxv_engine_fund(&E, m, 10000000, 0);
        zxv_obligation_t in[300], out[AB_MAX_MEMBERS];
        uint32_t n = 50 + (uint32_t) (rnd() % 250), k;
        for (uint32_t i = 0; i < n; i++) {
            in[i].from = (uint32_t) (rnd() % M);
            do in[i].to = (uint32_t) (rnd() % M);
            while (in[i].to == in[i].from);
            in[i].amount = 1 + rnd() % 20000;
            gross += in[i].amount;
        }
        ok &= zxv_compress(&E, in, n, out, AB_MAX_MEMBERS, &k) == ZXV_E_OK;
        for (uint32_t i = 0; i < k && ok; i++) {
            uint32_t p[AB_MAX_MEMBERS], np;
            ok &= zxv_route(&E, out[i].from, out[i].to, out[i].amount, p, AB_MAX_MEMBERS, &np, 0) ==
                      ZXV_E_OK &&
                  p[0] == out[i].from && p[np - 1] == out[i].to;
            netted += out[i].amount;
        }
        uint64_t fees;
        ok &= zxv_decompress(&E, out, k, 1, &fees) == ZXV_E_OK;
        total_fees += fees;
        int64_t net[12];
        net_of(M, in, n, net);
        uint64_t paid_fee[12] = {0};
        uint64_t gross_out[12] = {0};
        for (uint32_t i = 0; i < k; i++) gross_out[out[i].from] += out[i].amount;
        for (uint32_t m = 0; m < M; m++)
            paid_fee[m] = gross_out[m] ? pay_assure_fee(gross_out[m]) : 0;
        for (uint32_t m = 0; m < M; m++)
            ok &= (int64_t) zxv_engine_balance(&E, m) == 10000000 + net[m] - (int64_t) paid_fee[m];
        ok &= zxv_engine_check(&E);
    }
    CHECK(ok, "200 rounds: compress -> route -> decompress, every balance exact, books balance");
    CHECK(netted < gross && total_fees > 0, "netting moved less than the gross; fees collected");
    printf("pipeline: gross %llu, settled after netting %llu, fees %llu\n",
           (unsigned long long) gross, (unsigned long long) netted,
           (unsigned long long) total_fees);
}

static void test_engine_edges(void)
{
    CHECK(zxv_engine_init(0, "USD", 840, 2, SEED) == ZXV_E_ARG, "init(NULL)");
    CHECK(zxv_engine_init(&E, "usd", 840, 2, SEED) == ZXV_E_ARG, "bad ISO alpha refused");
    CHECK(zxv_engine_init(&E, "SWC", 0, 0, SEED) == ZXV_E_OK, "a plain unit engine");
    uint32_t m;
    for (uint32_t i = 0; i < ZXV_ENGINE_MAX_MEMBERS; i++) zxv_engine_add_member(&E, &m);
    CHECK(E.n_members == ZXV_ENGINE_MAX_MEMBERS && zxv_engine_add_member(&E, &m) == ZXV_E_FULL,
          "member capacity");
    CHECK(zxv_engine_balance(&E, 999) == 0 && zxv_engine_bucket(&E, 9) == 0, "out of range reads");

    /* determinism: the same calls give the same chain on every machine */
    open_engine(&E, 3);
    open_engine(&E2, 3);
    zxv_obligation_t l = {0, 1, 777};
    zxv_engine_fund(&E, 0, 5000, 1);
    zxv_engine_fund(&E2, 0, 5000, 1);
    zxv_decompress(&E, &l, 1, 2, 0);
    zxv_decompress(&E2, &l, 1, 2, 0);
    CHECK(memcmp(E.L.chain_head, E2.L.chain_head, sizeof E.L.chain_head) == 0,
          "same seed and calls, same chain hash");
}

/* Every refusal is the exact status the header promises, at the exact
 * boundary: one past the last member, one past each limit, and the limit
 * itself accepted. */
static void test_boundaries(void)
{
    const uint64_t LEG = (uint64_t) 1 << 59;
    open_engine(&E, 3);
    uint32_t m;
    CHECK(zxv_engine_add_member(&E, 0) == ZXV_E_ARG && zxv_engine_add_member(0, &m) == ZXV_E_ARG,
          "add_member(NULL)");
    CHECK(E.L.n_accounts == 1 + PAY_ASSURE_BUCKETS + 3, "issuer, four buckets, three members");
    bool distinct = true;
    for (uint32_t b = 0; b < PAY_ASSURE_BUCKETS; b++) {
        const pay_account_t *a = pay_ledger_account(&E.L, E.bucket[b]);
        distinct &= E.bucket[b] != E.issuer && a && a->flags == PAY_ACCT_COMMONS;
        for (uint32_t c = 0; c < b; c++) distinct &= E.bucket[b] != E.bucket[c];
    }
    CHECK(distinct, "four distinct commons bucket accounts");
    const pay_account_t *ma = pay_ledger_account(&E.L, E.acct[0]);
    CHECK(ma && ma->flags == 0 && ma->owner == 1, "a member account is plain and owned by m+1");

    /* fund / withdraw */
    CHECK(zxv_engine_fund(&E, 3, 1, 1) == ZXV_E_ARG, "fund: one past the last member");
    CHECK(zxv_engine_fund(&E, 0, LEG + 1, 1) == ZXV_E_ARG, "fund: over the leg limit");
    CHECK(zxv_engine_fund(&E, 0, LEG, 1) == ZXV_E_OK, "fund: the leg limit itself");
    CHECK(zxv_engine_fund(0, 0, 1, 1) == ZXV_E_ARG, "fund(NULL)");
    CHECK(zxv_engine_withdraw(&E, 3, 1, 1) == ZXV_E_ARG, "withdraw: one past the last member");
    CHECK(zxv_engine_withdraw(&E, 0, 0, 1) == ZXV_E_ARG, "withdraw 0");
    CHECK(zxv_engine_withdraw(0, 0, 1, 1) == ZXV_E_ARG, "withdraw(NULL)");
    CHECK(zxv_engine_withdraw(&E, 0, LEG + 1, 1) == ZXV_E_ARG, "withdraw: over the leg limit");
    uint64_t s0 = E.seq;
    CHECK(zxv_engine_withdraw(&E, 0, 1, 2) == ZXV_E_OK &&
              zxv_engine_withdraw(&E, 0, 1, 2) == ZXV_E_OK &&
              zxv_engine_fund(&E, 1, 5, 2) == ZXV_E_OK && E.seq == s0 + 3,
          "each posting takes the next sequence number");
    CHECK(zxv_engine_withdraw(&E, 0, LEG - 2, 3) == ZXV_E_OK && zxv_engine_balance(&E, 0) == 0,
          "withdraw: the leg limit's worth");
    CHECK(zxv_engine_check(&E), "books balance");

    /* zxv_engine_check catches a tampered ledger */
    E.L.acct[E.acct[1]].debit += 1;
    CHECK(!zxv_engine_check(&E), "a member balance edited behind the ledger's back");
    E.L.acct[E.acct[1]].debit -= 1;
    uint32_t iss = E.issuer;
    E.issuer = PAY_MAX_ACCOUNTS + 1;
    CHECK(!zxv_engine_check(&E), "no issuer account");
    E.issuer = iss;
    CHECK(zxv_engine_check(&E) && !zxv_engine_check(0), "restored; check(NULL) is false");

    /* corridors */
    zxv_corridor_t c = {0, 3, 1, 1};
    CHECK(zxv_engine_add_corridor(&E, &c) == ZXV_E_ARG, "corridor to one past the last member");
    c = (zxv_corridor_t){3, 0, 1, 1};
    CHECK(zxv_engine_add_corridor(&E, &c) == ZXV_E_ARG, "corridor from one past the last member");
    CHECK(zxv_engine_add_corridor(&E, 0) == ZXV_E_ARG &&
              zxv_engine_add_corridor(0, &c) == ZXV_E_ARG,
          "add_corridor(NULL)");
    c = (zxv_corridor_t){0, 1, ZXV_FEE_PPM_MAX, 1};
    bool all = true;
    for (uint32_t i = 0; i < ZXV_ENGINE_MAX_CORRIDORS; i++)
        all &= zxv_engine_add_corridor(&E, &c) == ZXV_E_OK;
    CHECK(all && E.n_cor == ZXV_ENGINE_MAX_CORRIDORS,
          "a fee of exactly 100%; corridors up to the limit");
    CHECK(zxv_engine_add_corridor(&E, &c) == ZXV_E_FULL, "one corridor too many");

    /* route */
    uint32_t p[8], n;
    uint64_t cost;
    CHECK(zxv_route(&E, 3, 0, 1, p, 8, &n, &cost) == ZXV_E_ARG,
          "route from one past the last member");
    CHECK(zxv_route(&E, 0, 3, 1, p, 8, &n, &cost) == ZXV_E_ARG,
          "route to one past the last member");
    CHECK(zxv_route(&E, 0, 0, 1, p, 0, &n, &cost) == ZXV_E_ARG, "route into an empty buffer");
    CHECK(zxv_route(0, 0, 0, 1, p, 1, &n, &cost) == ZXV_E_ARG &&
              zxv_route(&E, 0, 0, 1, 0, 1, &n, &cost) == ZXV_E_ARG &&
              zxv_route(&E, 0, 0, 1, p, 1, 0, &cost) == ZXV_E_ARG,
          "route(NULL ...)");
    CHECK(zxv_route(&E, 2, 2, 1, p, 1, &n, 0) == ZXV_E_OK && n == 1 && p[0] == 2,
          "a one-member path fits a one-entry buffer");
    CHECK(zxv_route(&E, 0, 1, 1, p, 2, &n, &cost) == ZXV_E_OK && n == 2 && cost == ZXV_FEE_PPM_MAX,
          "a path exactly as long as the buffer");

    /* compress */
    zxv_obligation_t out[AB_MAX_MEMBERS], ok1 = {0, 1, 5};
    uint32_t k;
    zxv_obligation_t first_bad[2] = {{0, 0, 5}, {0, 1, 5}};
    CHECK(zxv_compress(&E, first_bad, 2, out, 4, &k) == ZXV_E_ARG,
          "the first obligation is checked too");
    zxv_obligation_t edge[2] = {{0, 3, 5}, {3, 0, 5}};
    CHECK(zxv_compress(&E, &edge[0], 1, out, 4, &k) == ZXV_E_ARG &&
              zxv_compress(&E, &edge[1], 1, out, 4, &k) == ZXV_E_ARG,
          "compress: one past the last member, either side");
    CHECK(zxv_compress(0, &ok1, 1, out, 4, &k) == ZXV_E_ARG &&
              zxv_compress(&E, 0, 1, out, 4, &k) == ZXV_E_ARG &&
              zxv_compress(&E, &ok1, 1, 0, 4, &k) == ZXV_E_ARG &&
              zxv_compress(&E, &ok1, 1, out, 4, 0) == ZXV_E_ARG,
          "compress(NULL ...)");
    static zxv_obligation_t big[AB_MAX_OBLIGATIONS + 1];
    for (uint32_t i = 0; i <= AB_MAX_OBLIGATIONS; i++)
        big[i] = (zxv_obligation_t){i % 2, 1 - i % 2, 1};
    CHECK(zxv_compress(&E, big, AB_MAX_OBLIGATIONS + 1, out, 4, &k) == ZXV_E_ARG,
          "one obligation over the abacus limit");
    CHECK(zxv_compress(&E, big, AB_MAX_OBLIGATIONS, out, 4, &k) == ZXV_E_OK && k == 0,
          "exactly the abacus limit (and it nets to nothing)");
    zxv_obligation_t lim = {0, 1, LEG};
    CHECK(zxv_compress(&E, &lim, 1, out, 4, &k) == ZXV_E_OK && k == 1 && out[0].amount == LEG,
          "an obligation of exactly the leg limit");
    zxv_obligation_t sentinel = {9, 9, 9};
    out[1] = sentinel;
    CHECK(zxv_compress(&E, &ok1, 1, out, 4, &k) == ZXV_E_OK && k == 1 &&
              memcmp(&out[1], &sentinel, sizeof sentinel) == 0,
          "compress writes exactly k entries");

    /* a full house of members still compresses */
    open_engine(&E2, ZXV_ENGINE_MAX_MEMBERS);
    zxv_obligation_t ring[ZXV_ENGINE_MAX_MEMBERS];
    for (uint32_t i = 0; i < ZXV_ENGINE_MAX_MEMBERS; i++)
        ring[i] = (zxv_obligation_t){i, (i + 1) % ZXV_ENGINE_MAX_MEMBERS, 10 + i};
    CHECK(zxv_compress(&E2, ring, ZXV_ENGINE_MAX_MEMBERS, out, AB_MAX_MEMBERS, &k) == ZXV_E_OK &&
              k > 0 && k < ZXV_ENGINE_MAX_MEMBERS,
          "every member slot in use");

    /* decompress */
    uint64_t fees = 99;
    CHECK(zxv_decompress(0, &ok1, 1, 1, &fees) == ZXV_E_ARG && fees == 0, "decompress(NULL)");
    CHECK(zxv_decompress(&E, 0, 1, 1, 0) == ZXV_E_ARG, "decompress: NULL legs");
    CHECK(zxv_decompress(&E, big, ZXV_ENGINE_MAX_LEGS + 1, 1, 0) == ZXV_E_ARG, "one leg too many");
    CHECK(zxv_decompress(&E, &edge[0], 1, 1, 0) == ZXV_E_ARG &&
              zxv_decompress(&E, &edge[1], 1, 1, 0) == ZXV_E_ARG,
          "decompress: one past the last member, either side");
    zxv_obligation_t zero = {0, 1, 0}, over = {0, 1, LEG + 1};
    CHECK(zxv_decompress(&E, &zero, 1, 1, 0) == ZXV_E_ARG &&
              zxv_decompress(&E, &over, 1, 1, 0) == ZXV_E_ARG,
          "decompress: zero and over-limit legs");
    open_engine(&E, 2);
    zxv_engine_fund(&E, 0, LEG, 1);
    zxv_engine_fund(&E, 0, LEG, 1);
    CHECK(zxv_decompress(&E, &lim, 1, 2, &fees) == ZXV_E_ARG && fees == 0,
          "a leg whose amount + fee would pass the line limit is refused up front");
    /* the largest a with a + fee(a) + 1 <= 2^59, by bisection */
    uint64_t lo = 1, hi = LEG;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (mid + pay_assure_fee(mid) + 1 <= LEG)
            lo = mid;
        else
            hi = mid - 1;
    }
    zxv_obligation_t top = {0, 1, lo};
    CHECK(zxv_decompress(&E, &top, 1, 2, &fees) == ZXV_E_OK &&
              zxv_engine_balance(&E, 1) == top.amount && fees == pay_assure_fee(top.amount),
          "the largest leg whose amount + fee bound fits posts");
    top.amount++;
    CHECK(zxv_decompress(&E, &top, 1, 2, &fees) == ZXV_E_ARG, "one more is refused");
    open_engine(&E, 2);
    zxv_engine_fund(&E, 0, 1000, 1);
    static zxv_obligation_t many[ZXV_ENGINE_MAX_LEGS];
    for (uint32_t i = 0; i < ZXV_ENGINE_MAX_LEGS; i++) many[i] = ok1;
    CHECK(zxv_decompress(&E, many, ZXV_ENGINE_MAX_LEGS, 2, &fees) == ZXV_E_FUNDS,
          "exactly the leg limit is not refused as an argument error");

    /* rollback of several postings, without touching what came before */
    open_engine(&E, 4);
    zxv_engine_fund(&E, 0, 100000, 1);
    zxv_engine_fund(&E, 1, 100000, 1);
    zxv_engine_fund(&E, 2, 100000, 1);
    pay_ledger_set_flags(&E.L, E.acct[2], PAY_ACCT_FROZEN);
    uint64_t before = E.seq;
    zxv_obligation_t three[3] = {{0, 3, 30000}, {1, 3, 20000}, {2, 3, 10000}};
    CHECK(zxv_decompress(&E, three, 3, 5, &fees) == ZXV_E_LEDGER, "third leg refused");
    CHECK(zxv_engine_balance(&E, 0) == 100000 && zxv_engine_balance(&E, 1) == 100000 &&
              zxv_engine_balance(&E, 2) == 100000 && zxv_engine_balance(&E, 3) == 0,
          "both posted legs reversed, the funding before them kept");
    CHECK(E.seq == before + 4, "two postings and two reversals, each with its own number");
    CHECK(zxv_engine_check(&E) && pay_ledger_verify_chain(&E.L),
          "books balance after a two-leg rollback");
    pay_ledger_set_flags(&E.L, E.acct[2], 0);
    CHECK(zxv_decompress(&E, three, 3, 6, &fees) == ZXV_E_OK && zxv_engine_balance(&E, 3) == 60000,
          "after the hold lifts the same batch posts");
}

int main(void)
{
    test_compress();
    test_route();
    test_decompress();
    test_pipeline();
    test_engine_edges();
    test_boundaries();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
