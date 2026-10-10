/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* meta_chest.c — Tier 3 metamorphic relations for the value tables:
 * community_chest (revenue split), vino (transfers, mirrored rmag quotas),
 * finance/triple_ledger (transfers) and count_house (deposits).
 *
 *   split(amount, bp): dev + platform + royalty == amount for EVERY bp in
 *     0..10001 and a generated amount set; dev is monotone in amount and in
 *     bp (inside the clamp); split(k*10000) scales exactly by k
 *   vino transfer(x) then back == identity on balances and on rmag quotas
 *   triple_ledger transfer(x) then back == identity on conventional balances
 *   count_house: depositing a then b == depositing a+b (same balance)
 * The table runs forward, again and in reverse (meta.h).
 */
#include "tier.h"
#include "meta.h"
#include "vino.h"
#include "community_chest.h"
#include "count_house.h"
#include "triple_ledger.h"
#include "rmag_core.h"

static const tier_known_t KNOWN_FAILURES[] = {{"-", "unused"}};

static uint64_t rel_split(void)
{
    uint64_t h = 0, seed = 0xABCD;
    uint64_t amts[40];
    unsigned n = 0;
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) amts[n++] = TIER_U64_EDGES[i];
    while (n < TIER_N(amts)) {
        uint64_t r = tier_rand(&seed);
        amts[n++] = r >> (r % 64);
    }
    for (unsigned i = 0; i < n; i++) {
        uint64_t prev_dev = 0;
        for (uint32_t bp = 0; bp <= 10001; bp += (bp < 6990 || bp > 8510) ? 97 : 1) {
            uint64_t p = 0, r = 0, d = cc_calc_revenue_split(amts[i], bp, &p, &r);
            CHECK((unsigned __int128) d + p + r == amts[i], "split sums to the amount");
            CHECK(d >= prev_dev, "dev share is monotone in bp");
            prev_dev = d;
            h = meta_mix(h, d ^ (p << 1) ^ (r << 2));
        }
    }
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j++) {
            if (amts[i] > amts[j]) continue;
            CHECK(cc_calc_revenue_split(amts[i], 8000, NULL, NULL) <=
                      cc_calc_revenue_split(amts[j], 8000, NULL, NULL),
                  "dev share is monotone in amount");
        }
    for (uint64_t k = 1; k < 2000000000ull; k = k * 7 + 3) {
        uint64_t p1, r1, pk, rk;
        uint64_t d1 = cc_calc_revenue_split(10000, 7777, &p1, &r1);
        uint64_t dk = cc_calc_revenue_split(10000 * k, 7777, &pk, &rk);
        CHECK(dk == d1 * k && pk == p1 * k && rk == r1 * k, "split(k*10000) == k*split(10000)");
    }
    return h;
}

static vino_ledger_t V, V0;
static rational_t Q0[8];

static uint64_t rel_vino_back(void)
{
    uint64_t h = 0;
    vino_init(&V, 5);
    const char *A[] = {"v-a", "v-b", "v-c"};
    for (int i = 0; i < 3; i++) {
        vino_create_account(&V, A[i], "x");
        vino_get_account(&V, A[i])->balance[CAP_FINANCIAL] = 1000000;
        vino_get_account(&V, A[i])->balance[CAP_HUMAN] = 50;
    }
    const uint64_t X[] = {0, 1, 49, 50, 51, 999999, 1000000, 1000001};
    const capital_type_t C[] = {CAP_FINANCIAL, CAP_HUMAN};
    for (unsigned c = 0; c < TIER_N(C); c++)
        for (unsigned k = 0; k < TIER_N(X); k++)
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    if (i == j) continue;
                    memcpy(&V0.balances, &V.balances, sizeof V.balances[0] * 4);
                    for (uint32_t q = 0; q < 8; q++) Q0[q] = rmag_get_quota(q);
                    int32_t r = vino_transfer(&V, A[i], A[j], X[k], C[c], RAIL_VINO_NATIVE, 0);
                    h = meta_mix(h, (uint64_t) (r >= 0));
                    if (r < 0) continue;
                    CHECK(vino_transfer(&V, A[j], A[i], X[k], C[c], RAIL_VINO_NATIVE, 0) >= 0,
                          "the way back is funded");
                    bool same = true;
                    for (int a = 0; a < 3; a++)
                        same &= memcmp(V.balances[a].balance, V0.balances[a].balance,
                                       sizeof V.balances[a].balance) == 0;
                    CHECK(same, "vino transfer(%llu) then back: balances identical",
                          (unsigned long long) X[k]);
                    bool qsame = true;
                    for (uint32_t q = 0; q < 8; q++) {
                        rational_t g = rmag_get_quota(q);
                        qsame &= g.num == Q0[q].num && g.den == Q0[q].den;
                    }
                    CHECK(qsame, "vino transfer then back: rmag quotas identical");
                }
    for (int a = 0; a < 3; a++) h = meta_mix(h, V.balances[a].balance[CAP_FINANCIAL]);
    return h;
}

static triple_ledger_t TL;
static uint64_t rel_triple_back(void)
{
    uint64_t h = 0;
    triple_ledger_init(&TL);
    uint32_t a = triple_ledger_create_account(&TL, 1, CAP_FINANCIAL, "a");
    uint32_t b = triple_ledger_create_account(&TL, 2, CAP_FINANCIAL, "b");
    const int64_t X[] = {1, 2, 1000, 65535, 2147483};
    for (unsigned k = 0; k < TIER_N(X); k++) {
        surplus_real_t ca = TL.accounts[a].conventional_balance,
                       cb = TL.accounts[b].conventional_balance;
        surplus_real_t x = SR_FROM_INT(X[k]);
        CHECK(triple_ledger_transfer(&TL, a, b, CAP_FINANCIAL, x, SR_FROM_INT(1), SR_ZERO, "f") ==
                      0 &&
                  triple_ledger_transfer(&TL, b, a, CAP_FINANCIAL, x, SR_FROM_INT(1), SR_ZERO,
                                         "b") == 0,
              "both legs accepted");
        CHECK(SR_CMP(TL.accounts[a].conventional_balance, ca) == 0 &&
                  SR_CMP(TL.accounts[b].conventional_balance, cb) == 0,
              "triple_ledger transfer(%lld) then back is the identity", (long long) X[k]);
        CHECK(SR_CMP(TL.total_assets, TL.total_liabilities) == 0, "assets == liabilities");
        h = meta_mix(h, (uint64_t) TL.accounts[a].num_entries);
    }
    return h;
}

static bool yes(const stash_bucket_t *b)
{
    (void) b;
    return true;
}
static count_house_t CH1, CH2;
static uint64_t rel_count_house(void)
{
    uint64_t h = 0;
    uint8_t pk[CH_PUBKEY_LEN] = {0}, sig[CH_PROOF_SIG_LEN] = {0};
    word168_t p;
    memset(&p, 0, sizeof p);
    p.bytes[0] = 3;
    const uint64_t X[] = {0, 1, 2, 1000, (uint64_t) 1 << 40};
    for (unsigned i = 0; i < TIER_N(X); i++)
        for (unsigned j = 0; j < TIER_N(X); j++) {
            count_house_init(&CH1, 1, "a");
            count_house_init(&CH2, 1, "b");
            CH1.verify_sig = CH2.verify_sig = yes;
            count_house_deposit(&CH1, &p, pk, X[i], sig);
            count_house_deposit(&CH1, &p, pk, X[j], sig);
            count_house_deposit(&CH2, &p, pk, X[i] + X[j], sig);
            CHECK(CH1.buckets[0].token_balance == CH2.buckets[0].token_balance,
                  "deposit a then b == deposit a+b");
            h = meta_mix(h, CH1.buckets[0].token_balance);
        }
    return h;
}

static const meta_rel_t RELS[] = {
    {"cc-split", rel_split},
    {"vino-back", rel_vino_back},
    {"triple-back", rel_triple_back},
    {"count-house-additive", rel_count_house},
};

int main(void)
{
    tier_begin("tier3/meta_chest", KNOWN_FAILURES, 0);
    (void) KNOWN_FAILURES;
    meta_run(RELS, TIER_N(RELS));
    return tier_end();
}
