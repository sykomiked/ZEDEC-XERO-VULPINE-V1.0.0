/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_fee.c — conformance test of the ABI 1.5 fee group, linked
 * against the shared library only (no kernel headers). The fee is recomputed
 * here without 128-bit arithmetic, by splitting the gross at 10^7:
 *   g = h * 10^7 + l  =>  floor((g * 8889 + r) / 10^7)
 *                       = h * 8889 + floor((l * 8889 + r) / 10^7)
 * (l * 8889 + r < 2^37), and the bucket split by the same trick at 100.
 * Known answers (from kernel/src/pay/test_pay.c and by hand) pin the edges:
 * 1124 -> 0, 1125 -> 1, UINT64_MAX -> 16397310807120420, the reserve floor
 * taking every remainder unit, and the four buckets always summing to the
 * fee. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define OK(call)                                                                   \
    do {                                                                           \
        int32_t rc_ = (call);                                                      \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_));             \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want);         \
    } while (0)

typedef unsigned long long ull;

static uint64_t rng_state = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ull;
}

/* ---- oracles ---- */
static uint64_t o_fee_carry(uint64_t r, uint64_t g, uint64_t *rem)
{
    uint64_t h = g / 10000000u, l = g % 10000000u;
    uint64_t x = l * 8889u + r;
    if (rem) *rem = x % 10000000u;
    return h * 8889u + x / 10000000u;
}
static uint64_t o_fee(uint64_t g)
{
    return o_fee_carry(0, g, NULL);
}
static uint64_t o_share(uint64_t fee, unsigned pct) /* floor(fee * pct / 100) */
{
    return (fee / 100u) * pct + (fee % 100u) * pct / 100u;
}

static void check_split(uint64_t fee)
{
    uint64_t out[4] = {1, 2, 3, 4};
    OK(zxv_fee_split(fee, out));
    uint64_t d = o_share(fee, 25), i = o_share(fee, 15), g = o_share(fee, 10);
    CHECK(out[ZXV_FEE_VBILL_DIVIDEND] == d && out[ZXV_FEE_INFRA_BOUNTY] == i &&
              out[ZXV_FEE_REGEN_CAPITAL] == g,
          "split %llu: %llu %llu %llu", (ull) fee, (ull) out[1], (ull) out[2], (ull) out[3]);
    /* the reserve floor is everything else: its half plus every remainder unit */
    CHECK(out[ZXV_FEE_RESERVE_FLOOR] == fee - d - i - g, "reserve of %llu", (ull) fee);
    CHECK(out[0] + out[1] + out[2] + out[3] == fee, "buckets sum to %llu", (ull) fee);
    CHECK(out[0] >= o_share(fee, 50) && out[0] - o_share(fee, 50) <= 3,
          "reserve = 50%% + at most 3 remainder units (%llu of %llu)", (ull) out[0], (ull) fee);
}

static void test_feature(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_FEE, "ZXV_FEAT2_FEE is live");
    CHECK(ZXV_FEE_NUM == 8889u && ZXV_FEE_DEN == 10000000u, "0.08889%%");
}

static void test_fee(void)
{
    static const struct {
        uint64_t g, fee;
    } kat[] = {
        {0, 0},
        {1, 0},
        {1124, 0},
        {1125, 1},
        {2249, 1},
        {2250, 2},
        {1000000, 888},
        {10000000, 8889},
        {100000000, 88890},
        {UINT64_MAX, 16397310807120420ull},
        {UINT64_MAX - 1, 16397310807120420ull},
        {1ull << 63, 8198655403560210ull},
    };
    for (size_t k = 0; k < sizeof kat / sizeof kat[0]; k++) {
        uint64_t f = 99;
        OK(zxv_fee_assure(kat[k].g, &f));
        CHECK(f == kat[k].fee, "fee(%llu) = %llu, want %llu", (ull) kat[k].g, (ull) f,
              (ull) kat[k].fee);
        CHECK(o_fee(kat[k].g) == kat[k].fee, "oracle agrees on %llu", (ull) kat[k].g);
    }
    IS(zxv_fee_assure(5, NULL), ZXV_E_ARG);

    /* the step edges: the smallest gross paying k units is ceil(k * 10^7 / 8889) */
    for (uint64_t k = 1; k < 5000; k++) {
        uint64_t edge = (k * 10000000u + 8888u) / 8889u, f;
        OK(zxv_fee_assure(edge, &f));
        CHECK(f == k, "fee(%llu) = %llu, want %llu", (ull) edge, (ull) f, (ull) k);
        OK(zxv_fee_assure(edge - 1, &f));
        CHECK(f == k - 1, "fee(%llu) = %llu, want %llu", (ull) (edge - 1), (ull) f, (ull) (k - 1));
    }
    for (int i = 0; i < 64; i++)
        for (int d = -3; d <= 3; d++) {
            uint64_t g = (1ull << i) + (uint64_t) (int64_t) d, f;
            OK(zxv_fee_assure(g, &f));
            CHECK(f == o_fee(g), "fee(2^%d%+d)", i, d);
        }
    for (int i = 0; i < 100000; i++) {
        uint64_t g = (i & 1) ? rnd() : rnd() >> (rnd() % 64), f;
        OK(zxv_fee_assure(g, &f));
        CHECK(f == o_fee(g), "fee(%llu) = %llu, oracle %llu", (ull) g, (ull) f, (ull) o_fee(g));
        CHECK(f < g || g == 0, "fee below the gross");
    }
}

static void test_carry(void)
{
    uint64_t f = 5, r = 6;
    OK(zxv_fee_assure_carry(0, 1124, &f, &r));
    CHECK(f == 0 && r == 9991236, "1124 leaves a 9,991,236 carry (%llu, %llu)", (ull) f, (ull) r);
    OK(zxv_fee_assure_carry(r, 1, &f, &r));
    CHECK(f == 1 && r == 125, "one more unit pays it: fee 1, carry 125 (%llu, %llu)", (ull) f, (ull) r);
    OK(zxv_fee_assure_carry(9999999, UINT64_MAX, &f, &r));
    uint64_t ro, fo = o_fee_carry(9999999, UINT64_MAX, &ro);
    CHECK(f == fo && r == ro, "top edge with the largest carry");
    OK(zxv_fee_assure_carry(0, 5, NULL, NULL));
    f = 11;
    r = 12;
    IS(zxv_fee_assure_carry(ZXV_FEE_DEN, 1, &f, &r), ZXV_E_RANGE);
    IS(zxv_fee_assure_carry(UINT64_MAX, 1, &f, &r), ZXV_E_RANGE);
    CHECK(f == 11 && r == 12, "a corrupt carry writes nothing");
    OK(zxv_fee_assure_carry(ZXV_FEE_DEN - 1, 0, &f, &r));
    CHECK(f == 0 && r == ZXV_FEE_DEN - 1, "zero gross keeps the carry");

    for (int i = 0; i < 50000; i++) {
        uint64_t rin = rnd() % ZXV_FEE_DEN, g = rnd() >> (rnd() % 64);
        OK(zxv_fee_assure_carry(rin, g, &f, &r));
        fo = o_fee_carry(rin, g, &ro);
        CHECK(f == fo && r == ro && r < ZXV_FEE_DEN, "carry %llu + %llu", (ull) rin, (ull) g);
    }

    /* many micro-charges pay exactly what one charge of their sum pays */
    for (int run = 0; run < 20; run++) {
        uint64_t rem = 0, total_fee = 0, total_g = 0;
        int ok = 1;
        for (int i = 0; i < 5000; i++) {
            uint64_t g = run < 10 ? 1 + rnd() % 1124 : rnd() % 1000000;
            OK(zxv_fee_assure_carry(rem, g, &f, &rem));
            total_fee += f;
            total_g += g;
            uint64_t whole;
            OK(zxv_fee_assure(total_g, &whole));
            if (whole != total_fee) ok = 0;
        }
        CHECK(ok, "run %d: the carry keeps every prefix exact", run);
        if (run < 10) CHECK(total_fee > 0, "sub-unit charges still pay in total");
    }
}

static void test_split(void)
{
    uint64_t out[4];
    static const struct {
        uint64_t fee, r, d, i, g;
    } kat[] = {
        {0, 0, 0, 0, 0},     {1, 1, 0, 0, 0},        {3, 3, 0, 0, 0},
        {4, 3, 1, 0, 0},     {7, 5, 1, 1, 0},        {10, 6, 2, 1, 1},
        {88, 45, 22, 13, 8}, {99, 52, 24, 14, 9},    {100, 50, 25, 15, 10},
        {101, 51, 25, 15, 10}, {8889, 4446, 2222, 1333, 888},
        {UINT64_MAX, 9223372036854775809ull, 4611686018427387903ull,
         2767011611056432742ull, 1844674407370955161ull},
    };
    for (size_t k = 0; k < sizeof kat / sizeof kat[0]; k++) {
        OK(zxv_fee_split(kat[k].fee, out));
        CHECK(out[ZXV_FEE_RESERVE_FLOOR] == kat[k].r && out[ZXV_FEE_VBILL_DIVIDEND] == kat[k].d &&
                  out[ZXV_FEE_INFRA_BOUNTY] == kat[k].i && out[ZXV_FEE_REGEN_CAPITAL] == kat[k].g,
              "split(%llu) = %llu %llu %llu %llu", (ull) kat[k].fee, (ull) out[0], (ull) out[1],
              (ull) out[2], (ull) out[3]);
    }
    for (uint64_t fee = 0; fee <= 20000; fee++) check_split(fee);
    for (int i = 0; i < 50000; i++) check_split(rnd() >> (rnd() % 64));
    check_split(UINT64_MAX);
    check_split(UINT64_MAX - 99);
    IS(zxv_fee_split(100, NULL), ZXV_E_ARG);

    /* a gross through the whole pipeline: fee then buckets */
    uint64_t f;
    OK(zxv_fee_assure(123456789012ull, &f));
    CHECK(f == 109740739, "fee of 123,456,789,012 (%llu)", (ull) f);
    OK(zxv_fee_split(f, out));
    CHECK(out[0] == 54870372 && out[1] == 27435184 && out[2] == 16461110 && out[3] == 10974073,
          "its buckets %llu %llu %llu %llu", (ull) out[0], (ull) out[1], (ull) out[2], (ull) out[3]);
}

static void test_policy(void)
{
    uint32_t pct[4] = {0};
    int32_t allow = -1;
    uint64_t rn[4], rd[4], mn = 0, md = 0, cn = 0, cd = 0;
    memset(rn, 0xFF, sizeof rn);
    memset(rd, 0xFF, sizeof rd);
    OK(zxv_fee_policy_default(pct, &allow, rn, rd, &mn, &md, &cn, &cd));
    CHECK(pct[ZXV_FEE_RESERVE_FLOOR] == 50 && pct[ZXV_FEE_VBILL_DIVIDEND] == 25 &&
              pct[ZXV_FEE_INFRA_BOUNTY] == 15 && pct[ZXV_FEE_REGEN_CAPITAL] == 10,
          "50/25/15/10");
    CHECK(pct[0] + pct[1] + pct[2] + pct[3] == 100, "shares sum to 100");
    CHECK(allow == 0, "the fee is a floor by default");
    CHECK(rn[ZXV_FEE_UNIT_MONEY] == 1 && rd[ZXV_FEE_UNIT_MONEY] == 1, "money rate 1/1");
    for (int u = ZXV_FEE_UNIT_COMPUTE; u < ZXV_FEE_UNITS; u++)
        CHECK(rn[u] == 0 && rd[u] == 1, "unit %d rate unset (0/1)", u);
    CHECK(mn == 1 && md == 1, "multiplier 1/1");
    CHECK(cn == 8 && cd == 21, "commons cap 8/21");
    OK(zxv_fee_policy_default(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    /* the shares agree with zxv_fee_split on a multiple of 100 */
    uint64_t out[4];
    OK(zxv_fee_split(1000000, out));
    for (int b = 0; b < ZXV_FEE_BUCKETS; b++)
        CHECK(out[b] == 10000ull * pct[b], "bucket %d is its share", b);
}

int main(void)
{
    test_feature();
    test_fee();
    test_carry();
    test_split();
    test_policy();
    printf("test_zxv_abi15_fee: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
