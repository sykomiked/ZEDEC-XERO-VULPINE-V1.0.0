/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_capmkt.c — host test for the capacity market.
 *
 * Checks the uniform-price double auction against a brute-force unit-level
 * reference on thousands of random books, and the fairness properties:
 * individual rationality, one price per round, maximal volume, no unfilled
 * order that wanted to trade at the price (except providers rationed by the
 * anti-monopoly cap), order-independence, supply/demand comparative statics,
 * the provider cap, escrow conservation, pay on delivery with proof chains,
 * the exact 0.08889% assurance fee (against pay_assure_fee), and the absence of any time
 * charge.
 *
 * BUILD (run from kernel/):
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Isrc/pay -Isrc/tensor -Isrc/mlkem \
 *     -Isrc/swarm src/capmkt/test_capmkt.c src/capmkt/capmkt.c src/pay/pay_util.c \
 *     src/pay/pay_assure.c src/swarm/swarm_market.c src/swarm/swarm_budget.c \
 *     src/swarm/swarm_emotion.c src/tensor/zt.c src/mlkem/keccak.c -lm \
 *     -o /tmp/test_capmkt && /tmp/test_capmkt
 */
#include <stdio.h>
#include <string.h>

#include "capmkt.h"
#include "../pay/pay_assure.h"

static int g_pass, g_fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, msg);                                  \
        }                                                                                          \
    } while (0)

static cm_market_t g_m;
static const cm_key_t K = {CM_RES_COMPUTE, CM_TENOR_DAY, 0};

static void id_of(uint8_t id[CM_ID_BYTES], int n)
{
    memset(id, 0, CM_ID_BYTES);
    id[0] = (uint8_t) n;
    id[1] = (uint8_t) (n >> 8);
    id[15] = 0xA5;
}

static uint64_t g_seed = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 7;
    g_seed ^= g_seed << 17;
    return g_seed;
}

/* verifier: evidence[0] must be 0xEE */
static bool verify_ok(void *ctx, const cm_contract_t *c, const cm_proof_t *p)
{
    (void) ctx;
    (void) c;
    return p->evidence[0] == 0xEE;
}

static void fresh(void)
{
    cm_params_t p;
    cm_params_default(&p);
    p.verify = verify_ok;
    cm_init(&g_m, &p);
}

static void test_fee(void)
{
    printf("assurance fee = floor(a * 8889 / 10^7)\n");
    CHECK(cm_fee_assure(0) == 0 && cm_fee_assure(1124) == 0 && cm_fee_assure(1125) == 1 &&
              cm_fee_assure(2250) == 2,
          "small values: 1124 -> 0, 1125 -> 1");
    CHECK(cm_fee_assure(10000000) == 8889 && cm_fee_assure(1000000) == 888, "0.08889 percent");
    int ok = 1;
    for (uint64_t a = 0; a < 20000; a++)
        if (cm_fee_assure(a) != pay_assure_fee(a)) ok = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t a = rnd();
        if (cm_fee_assure(a) != pay_assure_fee(a)) ok = 0;
    }
    CHECK(cm_fee_assure(UINT64_MAX) == 16397310807120420ull, "largest amount");
    CHECK(ok, "agrees with pay_assure_fee on 40000 values");
}

static void test_basic(void)
{
    printf("a worked example\n");
    fresh();
    uint8_t A[16], B[16], C[16], X[16], Y[16], Z[16];
    id_of(A, 1), id_of(B, 2), id_of(C, 3), id_of(X, 11), id_of(Y, 12), id_of(Z, 13);
    uint64_t T = 1000, EXP = T + 3600000;
    cm_deposit(&g_m, X, 1000000), cm_deposit(&g_m, Y, 1000000), cm_deposit(&g_m, Z, 1000000);
    uint32_t oa, ob, oc, ox, oy, oz;
    CHECK(cm_ask(&g_m, A, K, 10, 100, EXP, &oa) == CM_OK, "ask A 10 @100");
    CHECK(cm_ask(&g_m, B, K, 10, 120, EXP, &ob) == CM_OK, "ask B 10 @120");
    CHECK(cm_ask(&g_m, C, K, 10, 150, EXP, &oc) == CM_OK, "ask C 10 @150");
    CHECK(cm_bid(&g_m, X, K, 15, 140, EXP, &ox) == CM_OK, "bid X 15 @140");
    CHECK(cm_bid(&g_m, Y, K, 10, 110, EXP, &oy) == CM_OK, "bid Y 10 @110");
    CHECK(cm_bid(&g_m, Z, K, 5, 200, EXP, &oz) == CM_OK, "bid Z 5 @200");
    CHECK(cm_account(&g_m, X)->locked == 15 * 140, "bid locks qty x limit");
    static cm_result_t r;
    CHECK(cm_clear(&g_m, K, T, &r) == CM_OK, "clear");
    printf("  volume %llu at %llu VFV/unit (interval [%llu, %llu]), %u providers\n",
           (unsigned long long) r.volume, (unsigned long long) r.price, (unsigned long long) r.lo,
           (unsigned long long) r.hi, r.providers);
    CHECK(r.volume == 20, "volume where supply meets demand");
    CHECK(r.lo == 120 && r.hi == 140 && r.price == 130, "uniform price = midpoint of [120,140]");
    CHECK(cm_account(&g_m, X)->escrow == 15 * 130 && cm_account(&g_m, X)->locked == 0,
          "X: 15 units escrowed at 130, lock released");
    CHECK(cm_account(&g_m, X)->available == 1000000 - 15 * 130, "X refunded the limit difference");
    CHECK(cm_account(&g_m, Y)->locked == 10 * 110 && cm_account(&g_m, Y)->escrow == 0,
          "Y (bid below price) stays on the book");
    CHECK(cm_order(&g_m, oc) && cm_order(&g_m, oc)->filled == 0, "C (ask above price) unfilled");
    CHECK(cm_conserved(&g_m), "conserved after clearing");
    /* pay on delivery */
    const cm_contract_t *c = 0;
    for (uint32_t i = 1; i <= 8 && !c; i++) {
        const cm_contract_t *t = cm_contract(&g_m, i);
        if (t && !memcmp(t->buyer, X, 16) && !memcmp(t->provider, B, 16)) c = t;
    }
    CHECK(c && c->qty == 10, "contract X <- B for 10 units");
    if (!c) return;
    cm_proof_t pr;
    memset(&pr, 0, sizeof pr);
    pr.contract_id = c->id;
    pr.seq = 1;
    pr.units = 4;
    pr.at_ms = T + 1000;
    memcpy(pr.prev, c->proof_tip, 32);
    pr.evidence[0] = 0xEE;
    uint64_t b0 = cm_account(&g_m, B)->available;
    CHECK(cm_deliver(&g_m, &pr) == CM_OK, "4 units delivered");
    uint64_t pay = 4 * 130, t = cm_fee_assure(pay);
    CHECK(cm_account(&g_m, B)->available == b0 + pay - t && g_m.commons == t,
          "provider paid, assurance fee to the fee pool");
    CHECK(cm_deliver(&g_m, &pr) == CM_ERR_PROOF, "same proof again: refused");
    cm_proof_t p2 = pr;
    p2.seq = 2;
    cm_proof_hash(&pr, p2.prev);
    p2.evidence[0] = 0;
    CHECK(cm_deliver(&g_m, &p2) == CM_ERR_PROOF, "evidence refused by verifier");
    p2.evidence[0] = 0xEE;
    p2.units = 7;
    CHECK(cm_deliver(&g_m, &p2) == CM_ERR_ARG, "more than contracted: refused");
    p2.units = 3;
    p2.at_ms = c->end_ms + 1;
    CHECK(cm_deliver(&g_m, &p2) == CM_ERR_LATE, "after the window: not paid");
    p2.at_ms = T + 2000;
    CHECK(cm_deliver(&g_m, &p2) == CM_OK, "3 more units");
    CHECK(cm_conserved(&g_m), "conserved after delivery");
    /* window ends: undelivered escrow back, exactly, whenever expire runs */
    uint64_t x0 = cm_account(&g_m, X)->available;
    uint64_t left = c->escrow;
    CHECK(left == 3 * 130, "3 undelivered units in escrow");
    static cm_market_t later;
    memcpy(&later, &g_m, sizeof later);
    CHECK(cm_expire(&g_m, c->end_ms) >= 1, "expire at the end");
    CHECK(cm_expire(&later, c->end_ms + 365ull * 86400000ull) >= 1, "expire a year late");
    CHECK(cm_account(&g_m, X)->available == x0 + left + 5 * 130 &&
              cm_account(&later, X)->available == cm_account(&g_m, X)->available,
          "refund is exact and time-independent (no interest, no fee)");
    CHECK(cm_account(&g_m, B)->missed == 3, "provider's missed units recorded (no fine)");
    CHECK(cm_conserved(&g_m), "conserved after expiry");
    /* cancel returns the lock */
    uint64_t y0 = cm_account(&g_m, Y)->available;
    CHECK(cm_cancel(&g_m, Y, oy) == CM_OK && cm_account(&g_m, Y)->available == y0 + 1100,
          "cancel returns the lock");
    CHECK(cm_conserved(&g_m), "conserved after cancel");
    (void) oa, (void) ob, (void) ox, (void) oz;
}

static void test_rules(void)
{
    printf("order rules\n");
    fresh();
    uint8_t A[16], X[16];
    id_of(A, 1), id_of(X, 2);
    uint32_t o;
    cm_deposit(&g_m, X, 1000);
    CHECK(cm_bid(&g_m, X, K, 11, 100, 10, &o) == CM_ERR_FUNDS, "bid above deposit refused");
    CHECK(cm_bid(&g_m, X, K, 1ull << 40, 1ull << 30, 10, &o) == CM_ERR_OVERFLOW,
          "overflow refused");
    CHECK(cm_bid(&g_m, X, K, 10, 100, 10, &o) == CM_OK, "bid within deposit");
    CHECK(cm_ask(&g_m, X, K, 10, 50, 10, &o) == CM_ERR_SELF, "no self-trading");
    cm_key_t bad = {9, 1, 0};
    CHECK(cm_ask(&g_m, A, bad, 10, 50, 10, &o) == CM_ERR_ARG, "unknown resource");
    CHECK(cm_withdraw(&g_m, X, 1) == CM_ERR_FUNDS, "locked funds cannot be withdrawn");
    /* regions are separate markets */
    cm_key_t R2 = {CM_RES_COMPUTE, CM_TENOR_DAY, 2};
    cm_ask(&g_m, A, R2, 10, 10, 10, &o);
    static cm_result_t r;
    cm_clear(&g_m, K, 1, &r);
    CHECK(r.volume == 0, "ask in region 2 does not meet a bid in region 0");
    /* expired orders leave the book and release their locks */
    cm_clear(&g_m, K, 11, &r);
    CHECK(cm_account(&g_m, X)->locked == 0 && cm_account(&g_m, X)->available == 1000,
          "expired bid released");
    CHECK(cm_conserved(&g_m), "conserved");
}

static void test_cap(void)
{
    printf("anti-monopoly cap\n");
    fresh();
    uint8_t A[16], B[16], C[16], X[16];
    id_of(A, 1), id_of(B, 2), id_of(C, 3), id_of(X, 9);
    uint32_t o;
    cm_deposit(&g_m, X, 100000);
    /* A is big; B and C are small, all willing at the same price */
    cm_ask(&g_m, A, K, 1000, 99, 100, &o);
    cm_ask(&g_m, B, K, 10, 99, 100, &o);
    cm_ask(&g_m, C, K, 10, 99, 100, &o);
    cm_bid(&g_m, X, K, 100, 150, 100, &o);
    static cm_result_t r;
    cm_clear(&g_m, K, 1, &r);
    uint64_t sa = 0, sb = 0, sc = 0;
    for (uint32_t i = 0; i < r.nfills; i++) {
        const cm_contract_t *c = 0;
        for (uint32_t j = 1; j <= 16; j++) {
            const cm_contract_t *t = cm_contract(&g_m, j);
            if (t && t->ask_id == r.fills[i].ask_id) c = t;
        }
        if (!c) continue;
        if (!memcmp(c->provider, A, 16)) sa += r.fills[i].qty;
        if (!memcmp(c->provider, B, 16)) sb += r.fills[i].qty;
        if (!memcmp(c->provider, C, 16)) sc += r.fills[i].qty;
    }
    printf("  volume %llu: A %llu, B %llu, C %llu (cap %llu, overflow %llu)\n",
           (unsigned long long) r.volume, (unsigned long long) sa, (unsigned long long) sb,
           (unsigned long long) sc, (unsigned long long) r.cap, (unsigned long long) r.overflow);
    CHECK(r.volume == 100, "buyers are not rationed by the cap");
    CHECK(sb == 10 && sc == 10, "small providers are served in full");
    CHECK(r.cap == 38 && sa == 80 && r.overflow == 42,
          "big provider: 38 by first claim, 42 only because nobody else could");
    /* A undercuts by one unit: price competition, not crowding out */
    fresh();
    cm_deposit(&g_m, X, 100000);
    cm_ask(&g_m, A, K, 1000, 98, 100, &o);
    cm_ask(&g_m, B, K, 10, 99, 100, &o);
    cm_ask(&g_m, C, K, 10, 99, 100, &o);
    cm_bid(&g_m, X, K, 100, 150, 100, &o);
    cm_clear(&g_m, K, 1, &r);
    CHECK(r.price == 98 && r.providers == 1, "cheaper supplier sets the price below the others");
    cm_quote_t q;
    cm_quote(&g_m, K, 1, &q);
    CHECK(q.concentrated, "quote flags the market as concentrated");
}

/* ---- randomised fairness properties against a unit-level reference ---- */
#define MAXU 400
static void test_random(void)
{
    printf("random books: fairness properties\n");
    int bad_vol = 0, bad_ir = 0, bad_uni = 0, bad_envy = 0, bad_cap = 0, bad_cons = 0, bad_perm = 0,
        bad_stat = 0, traded = 0, three = 0, capped = 0;
    int trials = 3000;
    for (int t = 0; t < trials; t++) {
        int na = 1 + (int) (rnd() % 8), nb = 1 + (int) (rnd() % 8), np = 1 + (int) (rnd() % 5);
        uint64_t aq[8], ap[8], bq[8], bp[8];
        int aown[8];
        for (int i = 0; i < na; i++) {
            aq[i] = 1 + rnd() % 20;
            ap[i] = 50 + rnd() % 100;
            aown[i] = (int) (rnd() % (uint64_t) np);
        }
        for (int i = 0; i < nb; i++) {
            bq[i] = 1 + rnd() % 20;
            bp[i] = 50 + rnd() % 100;
        }
        /* reference: unit curves */
        uint64_t su[MAXU], du[MAXU];
        int ns = 0, nd = 0;
        for (int i = 0; i < na; i++)
            for (uint64_t u = 0; u < aq[i]; u++) su[ns++] = ap[i];
        for (int i = 0; i < nb; i++)
            for (uint64_t u = 0; u < bq[i]; u++) du[nd++] = bp[i];
        for (int i = 1; i < ns; i++)
            for (int j = i; j > 0 && su[j] < su[j - 1]; j--) {
                uint64_t x = su[j];
                su[j] = su[j - 1];
                su[j - 1] = x;
            }
        for (int i = 1; i < nd; i++)
            for (int j = i; j > 0 && du[j] > du[j - 1]; j--) {
                uint64_t x = du[j];
                du[j] = du[j - 1];
                du[j - 1] = x;
            }
        uint64_t qref = 0;
        while ((int) qref < ns && (int) qref < nd && su[qref] <= du[qref]) qref++;

        uint64_t price[2] = {0, 0}, vol[2] = {0, 0};
        uint64_t sold_by[2][8];
        memset(sold_by, 0, sizeof sold_by);
        for (int pass = 0; pass < 2; pass++) {
            fresh();
            int order[16], n = 0;
            for (int i = 0; i < na; i++) order[n++] = i;
            for (int i = 0; i < nb; i++) order[n++] = 100 + i;
            if (pass == 1) /* shuffle submission order */
                for (int i = n - 1; i > 0; i--) {
                    int j = (int) (rnd() % (uint64_t) (i + 1)), x = order[i];
                    order[i] = order[j];
                    order[j] = x;
                }
            for (int i = 0; i < nb; i++) {
                uint8_t id[16];
                id_of(id, 50 + i);
                cm_deposit(&g_m, id, 100000);
            }
            uint32_t aid[8], bid[8];
            for (int k = 0; k < n; k++) {
                uint8_t id[16];
                int o = order[k];
                if (o < 100) {
                    id_of(id, 1 + aown[o]);
                    cm_ask(&g_m, id, K, aq[o], ap[o], 10, &aid[o]);
                } else {
                    id_of(id, 50 + (o - 100));
                    cm_bid(&g_m, id, K, bq[o - 100], bp[o - 100], 10, &bid[o - 100]);
                }
            }
            static cm_result_t r;
            cm_clear(&g_m, K, 1, &r);
            if (pass == 0) {
                traded += r.volume > 0;
                three += r.providers >= 3;
                capped += r.overflow > 0 || (r.providers >= 2 && r.cap < r.volume);
            }
            price[pass] = r.price;
            vol[pass] = r.volume;
            if (r.volume != qref) bad_vol++;
            if (!cm_conserved(&g_m)) bad_cons++;
            /* per-order fills from the result */
            uint64_t af[8] = {0}, bf[8] = {0};
            for (uint32_t f = 0; f < r.nfills; f++) {
                for (int i = 0; i < na; i++)
                    if (r.fills[f].ask_id == aid[i]) af[i] += r.fills[f].qty;
                for (int i = 0; i < nb; i++)
                    if (r.fills[f].bid_id == bid[i]) bf[i] += r.fills[f].qty;
            }
            for (int i = 0; i < na; i++) {
                if (af[i] && ap[i] > r.price) bad_ir++;
                sold_by[pass][aown[i]] += af[i];
            }
            for (int i = 0; i < nb; i++)
                if (bf[i] && bp[i] < r.price) bad_ir++;
            for (uint32_t f = 1; f <= r.nfills; f++) {
                const cm_contract_t *c = cm_contract(&g_m, f);
                if (c && c->price != r.price) bad_uni++;
            }
            /* no unfilled bid above the price; an unfilled ask below the
             * price only if its provider reached its cap */
            for (int i = 0; i < nb; i++)
                if (bf[i] < bq[i] && bp[i] > r.price && r.volume) bad_envy++;
            for (int i = 0; i < na; i++)
                if (r.volume && af[i] < aq[i] && ap[i] < r.price &&
                    sold_by[pass][aown[i]] < r.cap && r.overflow == 0)
                    bad_envy++;
            if (r.providers >= 3 && r.overflow == 0)
                for (int p = 0; p < np; p++)
                    if (sold_by[pass][p] > r.cap) bad_cap++;
            if (r.providers >= 3 && r.cap > 1 && r.cap * 21 > r.volume * 8) bad_cap++;
            /* comparative statics on the first pass: more demand, price up;
             * more supply, price down */
            if (pass == 0 && r.volume) {
                static cm_result_t r2;
                uint8_t id[16];
                id_of(id, 90);
                cm_deposit(&g_m, id, 1000000);
                static cm_market_t save;
                memcpy(&save, &g_m, sizeof save);
                /* rerun the same book with an extra high bid */
                fresh();
                for (int i = 0; i < nb; i++) {
                    id_of(id, 50 + i);
                    cm_deposit(&g_m, id, 100000);
                }
                for (int i = 0; i < na; i++) {
                    id_of(id, 1 + aown[i]);
                    cm_ask(&g_m, id, K, aq[i], ap[i], 10, 0);
                }
                for (int i = 0; i < nb; i++) {
                    id_of(id, 50 + i);
                    cm_bid(&g_m, id, K, bq[i], bp[i], 10, 0);
                }
                id_of(id, 90);
                cm_deposit(&g_m, id, 1000000);
                cm_bid(&g_m, id, K, 5, 200, 10, 0);
                cm_auction(&g_m, K, 1, &r2);
                if (r2.price < r.price) bad_stat++;
                id_of(id, 91);
                cm_ask(&g_m, id, K, 30, 40, 10, 0);
                cm_auction(&g_m, K, 1, &r2);
                uint64_t with_demand = r2.price;
                (void) with_demand;
                fresh();
                for (int i = 0; i < nb; i++) {
                    id_of(id, 50 + i);
                    cm_deposit(&g_m, id, 100000);
                }
                for (int i = 0; i < na; i++) {
                    id_of(id, 1 + aown[i]);
                    cm_ask(&g_m, id, K, aq[i], ap[i], 10, 0);
                }
                for (int i = 0; i < nb; i++) {
                    id_of(id, 50 + i);
                    cm_bid(&g_m, id, K, bq[i], bp[i], 10, 0);
                }
                id_of(id, 91);
                cm_ask(&g_m, id, K, 5, 40, 10, 0);
                cm_auction(&g_m, K, 1, &r2);
                if (r2.price > r.price) bad_stat++;
                memcpy(&g_m, &save, sizeof save);
            }
        }
        /* prices are distinct only by chance; with equal prices time
         * priority may legitimately change who fills, but never the price
         * or the volume */
        if (price[0] != price[1] || vol[0] != vol[1]) bad_perm++;
    }
    printf("  %d random books x 2 submission orders: %d traded, %d with 3+ providers at the "
           "price, %d where the cap applied\n",
           trials, traded, three, capped);
    CHECK(traded > trials / 2 && three > 100 && capped > 100, "the random books exercise the cap");
    CHECK(bad_vol == 0, "volume equals the unit-level supply/demand intersection");
    CHECK(bad_ir == 0, "no buyer pays above its limit, no provider gets below its ask");
    CHECK(bad_uni == 0, "one price for every trade in a round");
    CHECK(bad_envy == 0, "no unfilled order wanted to trade at the price (except capped)");
    CHECK(bad_cap == 0, "no provider above max(1, floor(8/21 volume)) when 3+ compete");
    CHECK(bad_cons == 0, "escrow conservation");
    CHECK(bad_perm == 0, "price and volume independent of submission order");
    CHECK(bad_stat == 0, "more demand never lowers the price; more supply never raises it");
}

static void test_digest(void)
{
    printf("round digest\n");
    fresh();
    uint8_t A[16], X[16];
    id_of(A, 1), id_of(X, 2);
    cm_deposit(&g_m, X, 10000);
    uint32_t o1, o2;
    cm_ask(&g_m, A, K, 5, 10, 100, &o1);
    cm_bid(&g_m, X, K, 5, 20, 100, &o2);
    uint8_t d1[32], d2[32];
    cm_round_digest(&g_m, K, 1, d1);
    static cm_market_t copy;
    memcpy(&copy, &g_m, sizeof copy);
    cm_round_digest(&copy, K, 1, d2);
    CHECK(!memcmp(d1, d2, 32), "same order set, same digest");
    cm_cancel(&copy, X, o2);
    cm_round_digest(&copy, K, 1, d2);
    CHECK(memcmp(d1, d2, 32), "different order set, different digest");
    static cm_result_t r;
    cm_clear(&g_m, K, 1, &r);
    const cm_contract_t *c = cm_contract(&g_m, 1);
    CHECK(c && !memcmp(c->round_digest, d1, 32), "contract records the round it cleared in");
}

int main(void)
{
    printf("=== test_capmkt: capacity market (uniform-price double auction) ===\n");
    test_fee();
    test_basic();
    test_rules();
    test_cap();
    test_digest();
    test_random();
    printf("test_capmkt: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
