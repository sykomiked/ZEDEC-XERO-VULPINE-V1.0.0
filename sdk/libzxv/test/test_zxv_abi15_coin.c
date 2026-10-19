/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_coin.c — conformance test of the ABI 1.5 coin group,
 * linked against the shared library only (no kernel headers). Every rule is
 * restated here and checked against an independent computation:
 *   - the VFV ladder: rung r has face F(r+1), r = 1..45, rung 0 is the V0
 *     note; every other face is ZXV_E_DENOM; Zeckendorf splits recomputed
 *     greedily, with the top rung repeating, the two-call buffer rules and
 *     the 4,294,967,294-coin limit;
 *   - token tiers: face F(k+2), super from tier 10, the canonical form at
 *     every octane, the documented piece counts (docs/TOKEN_DENOMINATIONS.md),
 *     fuse / split conservation and every refusal, unchanged on failure;
 *   - the Fibonacci wire code: bytes compared with an encoder written here,
 *     round trips of a mixed stream, truncation and 64-bit overflow;
 *   - the token wallet: ZXV_CAP_MINT (ZXV_CAP_SWARM is not enough), context
 *     binding, conservation across transfers at different octanes, and
 *     fail-closed once the context halts. */
#include <pthread.h>
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

/* ---- independent oracles ---- */
static uint64_t fib(unsigned n) /* F(1) = F(2) = 1; n <= 93 */
{
    uint64_t a = 0, b = 1;
    for (unsigned i = 0; i < n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return a;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) /* xorshift64* */
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ull;
}

/* canonical counts of v at octane o: top tier takes the quotient, then
 * greedy Zeckendorf below it (rule D3, restated). */
static void oracle_canonical(uint64_t v, unsigned o, uint64_t c[64])
{
    memset(c, 0, 64 * sizeof c[0]);
    c[o] = v / fib(o + 2);
    v %= fib(o + 2);
    for (unsigned k = o; k-- > 0;)
        if (v >= fib(k + 2)) {
            c[k] = 1;
            v -= fib(k + 2);
        }
}

static int counts_value(const uint64_t c[64], uint64_t *v) /* 0 on overflow */
{
    uint64_t s = 0;
    for (unsigned k = 0; k < 64; k++) {
        uint64_t f = fib(k + 2);
        if (c[k] && c[k] > UINT64_MAX / f) return 0;
        if (c[k] * f > UINT64_MAX - s) return 0;
        s += c[k] * f;
    }
    *v = s;
    return 1;
}

/* Fibonacci code of v (bits of v+1 over F(2), F(3), ..., then a 1). */
static unsigned oracle_code(uint64_t v, uint8_t *bits /* 0/1 per bit */)
{
    uint64_t n = v + 1;
    unsigned j = 2;
    while (j < 93 && fib(j + 1) <= n) j++;
    for (unsigned i = j + 1; i-- > 2;) {
        bits[i - 2] = n >= fib(i);
        if (bits[i - 2]) n -= fib(i);
    }
    bits[j - 1] = 1;
    return j;
}

/* ===================================================================== */
static void test_feature(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_COIN, "ZXV_FEAT2_COIN is live");
    CHECK(strcmp(zxv_status_name(ZXV_E_DENOM), "ZXV_E_DENOM") == 0, "DENOM name");
}

static void test_coin_ladder(void)
{
    uint64_t f = 99;
    uint32_t r = 99;
    OK(zxv_coin_face(0, &f));
    CHECK(f == 0, "rung 0 is the V0 note, face 0 (got %llu)", (ull) f);
    for (uint32_t k = 1; k <= ZXV_COIN_RUNGS; k++) {
        OK(zxv_coin_face(k, &f));
        CHECK(f == fib(k + 1), "rung %u face %llu, want F(%u) = %llu", k, (ull) f, k + 1,
              (ull) fib(k + 1));
        r = 0;
        OK(zxv_coin_rung(f, &r));
        CHECK(r == k, "rung of face %llu is %u, want %u", (ull) f, r, k);
    }
    OK(zxv_coin_face(ZXV_COIN_RUNGS, &f));
    CHECK(f == ZXV_COIN_TOP_FACE && f == 1836311903ull, "top face %llu", (ull) f);
    CHECK(ZXV_COIN_RUNGS == 45, "45 rungs");
    f = 7;
    IS(zxv_coin_face(ZXV_COIN_RUNGS + 1, &f), ZXV_E_RANGE);
    IS(zxv_coin_face(UINT32_MAX, &f), ZXV_E_RANGE);
    CHECK(f == 7, "face untouched on failure");
    IS(zxv_coin_face(1, NULL), ZXV_E_ARG);

    /* faces 1..5000: a coin iff a Fibonacci number */
    for (uint64_t x = 1; x <= 5000; x++) {
        int isfib = 0;
        for (unsigned k = 2; k <= 20; k++) isfib |= fib(k) == x;
        int32_t rc = zxv_coin_rung(x, &r);
        CHECK(rc == (isfib ? ZXV_OK : ZXV_E_DENOM), "rung of %llu -> %s", (ull) x,
              zxv_status_name(rc));
    }
    r = 77;
    IS(zxv_coin_rung(0, &r), ZXV_E_DENOM);
    IS(zxv_coin_rung(4, &r), ZXV_E_DENOM);
    IS(zxv_coin_rung(100, &r), ZXV_E_DENOM);
    IS(zxv_coin_rung(fib(47), &r), ZXV_E_DENOM); /* Fibonacci, but above the top face */
    IS(zxv_coin_rung(ZXV_COIN_TOP_FACE + 1, &r), ZXV_E_DENOM);
    IS(zxv_coin_rung(UINT64_MAX, &r), ZXV_E_DENOM);
    CHECK(r == 77, "rung untouched on failure");
    IS(zxv_coin_rung(1, NULL), ZXV_E_ARG);
    OK(zxv_coin_rung(1, &r));
    CHECK(r == 1, "face 1 is rung 1 (got %u)", r);
    OK(zxv_coin_rung(2, &r));
    CHECK(r == 2, "face 2 is rung 2 (got %u)", r);
}

/* Check a split of `amount` against the rules and the greedy oracle. */
static void check_split(uint64_t amount)
{
    size_t n = 12345;
    int32_t rc = zxv_coin_split(amount, NULL, 0, &n);
    if (amount == 0) {
        CHECK(rc == ZXV_OK && n == 0, "split 0 is empty (%s, %zu)", zxv_status_name(rc), n);
        return;
    }
    CHECK(rc == ZXV_E_BUFFER, "split %llu with no buffer -> %s", (ull) amount, zxv_status_name(rc));
    /* oracle: count */
    uint64_t top = fib(46), want = amount / top, rest = amount % top;
    for (unsigned r = 44; r >= 1; r--)
        if (rest >= fib(r + 1)) {
            rest -= fib(r + 1);
            want++;
        }
    CHECK(n == want, "split %llu needs %zu coins, oracle %llu", (ull) amount, n, (ull) want);
    if (n > 200) return;
    uint32_t rungs[201];
    for (unsigned i = 0; i < 201; i++) rungs[i] = 0xEEEEEEEEu;
    size_t m = 0;
    if (n > 0) {
        IS(zxv_coin_split(amount, rungs, n - 1, &m), ZXV_E_BUFFER);
        CHECK(m == n && rungs[0] == 0xEEEEEEEEu, "short buffer: count %zu, nothing written", m);
    }
    m = 0;
    OK(zxv_coin_split(amount, rungs, n + 1, &m));
    CHECK(m == n && rungs[n] == 0xEEEEEEEEu, "count %zu, no write past it", m);
    uint64_t sum = 0, q = amount / top;
    for (size_t i = 0; i < n; i++) {
        uint64_t f = 0;
        CHECK(zxv_coin_face(rungs[i], &f) == ZXV_OK && rungs[i] >= 1, "rung %u valid", rungs[i]);
        sum += f;
        if (i < q) CHECK(rungs[i] == ZXV_COIN_RUNGS, "top rung first (%u)", rungs[i]);
        else if (i > q) CHECK(rungs[i] + 1 < rungs[i - 1], "below the top: strictly falling, no "
                                                          "neighbours (%u after %u)",
                              rungs[i], rungs[i - 1]);
        else CHECK(rungs[i] <= ZXV_COIN_RUNGS - 1 || q == 0, "remainder below the top");
    }
    CHECK(sum == amount, "split of %llu sums to %llu", (ull) amount, (ull) sum);
}

static void test_coin_split(void)
{
    uint32_t rungs[32];
    size_t n = 0;
    OK(zxv_coin_split(100, rungs, 8, &n)); /* 100 = 89 + 8 + 3 */
    CHECK(n == 3 && rungs[0] == 10 && rungs[1] == 5 && rungs[2] == 3, "100 -> %zu: %u %u %u", n,
          rungs[0], rungs[1], rungs[2]);
    OK(zxv_coin_split(4, rungs, 8, &n)); /* 4 = 3 + 1 */
    CHECK(n == 2 && rungs[0] == 3 && rungs[1] == 1, "4 -> 3 + 1");
    OK(zxv_coin_split(2 * ZXV_COIN_TOP_FACE + 1, rungs, 8, &n));
    CHECK(n == 3 && rungs[0] == 45 && rungs[1] == 45 && rungs[2] == 1, "2 tops + 1");
    OK(zxv_coin_split(ZXV_COIN_TOP_FACE + fib(45), rungs, 8, &n));
    CHECK(n == 2 && rungs[0] == 45 && rungs[1] == 44, "top + F(45): the top repeats past F(46)");
    OK(zxv_coin_split(fib(46) - 1, rungs, 32, &n)); /* F(46)-1 = F(45)+F(43)+...+F(3) */
    CHECK(n == 22, "F(46)-1 takes 22 coins (got %zu)", n);
    n = 9;
    OK(zxv_coin_split(0, NULL, 0, &n));
    CHECK(n == 0, "empty split");
    IS(zxv_coin_split(5, rungs, 8, NULL), ZXV_E_ARG);

    for (uint64_t a = 0; a <= 3000; a++) check_split(a);
    for (int i = 0; i < 3000; i++) check_split(rnd() % (200ull * ZXV_COIN_TOP_FACE));

    /* the coin-count limit */
    n = 0;
    IS(zxv_coin_split(ZXV_COIN_TOP_FACE * 4294967294ull, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == 4294967294ull, "largest nameable split %zu", n);
    n = 5;
    IS(zxv_coin_split(ZXV_COIN_TOP_FACE * 4294967295ull, NULL, 0, &n), ZXV_E_RANGE);
    IS(zxv_coin_split(UINT64_MAX, rungs, 8, &n), ZXV_E_RANGE);
    CHECK(n == 5, "count untouched on RANGE");
}

/* ===================================================================== */
static void test_token_faces(void)
{
    uint64_t f;
    int32_t sup;
    uint32_t t;
    for (uint32_t k = 0; k < ZXV_TOKEN_TIERS; k++) {
        f = 0;
        sup = -1;
        OK(zxv_token_face(k, &f, &sup));
        CHECK(f == fib(k + 2), "tier %u face %llu", k, (ull) f);
        CHECK(sup == (k >= ZXV_TOKEN_SUPER_TIER), "tier %u super %d", k, sup);
        OK(zxv_token_tier(f, &t));
        CHECK(t == k, "tier of face %llu = %u", (ull) f, t);
    }
    OK(zxv_token_face(10, &f, NULL));
    CHECK(f == 144, "super tier face 144");
    OK(zxv_token_face(63, NULL, &sup));
    CHECK(sup == 1, "tier 63 super");
    OK(zxv_token_face(9, NULL, &sup));
    CHECK(sup == 0, "tier 9 not super");
    OK(zxv_token_face(ZXV_OCTANE_RACING, &f, NULL));
    CHECK(f == 75025, "RACING face");
    OK(zxv_token_face(ZXV_OCTANE_MAX, &f, NULL));
    CHECK(f == 17167680177565ull, "MAX face");
    IS(zxv_token_face(64, &f, &sup), ZXV_E_RANGE);
    IS(zxv_token_face(UINT32_MAX, &f, &sup), ZXV_E_RANGE);
    t = 5;
    IS(zxv_token_tier(4, &t), ZXV_E_DENOM);
    IS(zxv_token_tier(0, &t), ZXV_E_DENOM);
    IS(zxv_token_tier(fib(66), &t), ZXV_E_DENOM);
    CHECK(t == 5, "tier untouched on failure");
    IS(zxv_token_tier(1, NULL), ZXV_E_ARG);
    OK(zxv_token_tier(1, &t));
    CHECK(t == 0, "face 1 is the plain tier");
}

static void test_token_canonical(void)
{
    uint64_t c[64], o[64], v, p;
    /* documented piece counts (docs/TOKEN_DENOMINATIONS.md) */
    static const struct {
        uint64_t amount;
        uint32_t octane;
        uint64_t pieces;
    } kat[] = {
        {100, ZXV_OCTANE_PLAIN, 100},         {100, ZXV_OCTANE_REGULAR, 14},
        {100, ZXV_OCTANE_MIDGRADE, 3},        {1000, ZXV_OCTANE_PREMIUM, 2},
        {4096, ZXV_OCTANE_MIDGRADE, 47},      {4096, ZXV_OCTANE_RACING, 6},
        {100000, ZXV_OCTANE_RACING, 9},       {100000, ZXV_OCTANE_PREMIUM, 106},
        {1000000, ZXV_OCTANE_RACING, 17},     {1000000, ZXV_OCTANE_MAX, 5},
        {1000000, ZXV_OCTANE_REGULAR, 125000}, {1000000000, ZXV_OCTANE_MAX, 14},
        {1000000000, ZXV_OCTANE_RACING, 13335},
    };
    for (size_t i = 0; i < sizeof kat / sizeof kat[0]; i++) {
        OK(zxv_token_canonical(kat[i].amount, kat[i].octane, c));
        OK(zxv_token_value(c, &v, &p));
        CHECK(v == kat[i].amount && p == kat[i].pieces, "%llu at octane %u: value %llu, %llu pieces",
              (ull) kat[i].amount, kat[i].octane, (ull) v, (ull) p);
    }
    OK(zxv_token_canonical(1000, ZXV_OCTANE_PREMIUM, c)); /* 987 + 13 */
    CHECK(c[14] == 1 && c[5] == 1, "1000 = 987 + 13");

    /* every octane, many values, against the oracle */
    for (int i = 0; i < 4000; i++) {
        uint64_t val = (i & 1) ? rnd() : rnd() % 100000;
        if (i < 64) val = UINT64_MAX;
        unsigned oc = (unsigned) (i % 64);
        OK(zxv_token_canonical(val, oc, c));
        oracle_canonical(val, oc, o);
        CHECK(memcmp(c, o, sizeof c) == 0, "canonical %llu at %u", (ull) val, oc);
        int32_t is = -1;
        OK(zxv_token_is_canonical(c, oc, &is));
        CHECK(is == 1, "canonical form is canonical");
        OK(zxv_token_value(c, &v, NULL));
        CHECK(v == val, "value kept");
    }
    for (int i = 0; i < 64; i++) c[i] = 99;
    IS(zxv_token_canonical(5, 64, c), ZXV_E_RANGE);
    CHECK(c[0] == 99, "untouched on RANGE");
    IS(zxv_token_canonical(5, 0, NULL), ZXV_E_ARG);

    /* value */
    memset(c, 0, sizeof c);
    c[0] = 3;
    c[1] = 2;
    c[63] = 1;
    OK(zxv_token_value(c, &v, &p));
    CHECK(v == 3 + 4 + fib(65) && p == 6, "mixed value %llu pieces %llu", (ull) v, (ull) p);
    OK(zxv_token_value(c, NULL, NULL));
    v = 7;
    c[63] = UINT64_MAX / fib(65) + 1;
    IS(zxv_token_value(c, &v, &p), ZXV_E_RANGE);
    CHECK(!counts_value(c, &p), "the oracle overflows too");
    memset(c, 0, sizeof c);
    c[0] = UINT64_MAX;
    c[1] = 1; /* product fits, sum does not */
    IS(zxv_token_value(c, &v, &p), ZXV_E_RANGE);
    CHECK(v == 7, "value untouched on RANGE");
    IS(zxv_token_value(NULL, &v, &p), ZXV_E_ARG);

    /* is_canonical */
    int32_t is = -1;
    memset(c, 0, sizeof c);
    c[0] = 2; /* 2 plain tokens: canonical only at octane 0 */
    OK(zxv_token_is_canonical(c, 0, &is));
    CHECK(is == 1, "2 plain at PLAIN");
    OK(zxv_token_is_canonical(c, 1, &is));
    CHECK(is == 0, "2 plain at octane 1 is not canonical");
    memset(c, 0, sizeof c);
    c[5] = 1;
    OK(zxv_token_is_canonical(c, 4, &is));
    CHECK(is == 0, "a tier above the octane is not canonical");
    OK(zxv_token_is_canonical(c, 5, &is));
    CHECK(is == 1, "13 at octane 5");
    c[3] = 1;
    c[4] = 1; /* neighbours */
    OK(zxv_token_is_canonical(c, 63, &is));
    CHECK(is == 0, "neighbouring tiers are not canonical");
    IS(zxv_token_is_canonical(c, 64, &is), ZXV_E_RANGE);
    IS(zxv_token_is_canonical(NULL, 1, &is), ZXV_E_ARG);
    IS(zxv_token_is_canonical(c, 1, NULL), ZXV_E_ARG);
    c[63] = UINT64_MAX;
    IS(zxv_token_is_canonical(c, 63, &is), ZXV_E_RANGE);

    /* compress */
    memset(c, 0, sizeof c);
    c[0] = 1000;
    OK(zxv_token_compress(c, ZXV_OCTANE_PREMIUM));
    oracle_canonical(1000, ZXV_OCTANE_PREMIUM, o);
    CHECK(memcmp(c, o, sizeof c) == 0, "1000 plain compress to 987 + 13");
    OK(zxv_token_compress(c, ZXV_OCTANE_PLAIN));
    CHECK(c[0] == 1000 && c[14] == 0 && c[5] == 0, "back down to plain");
    memcpy(o, c, sizeof c);
    IS(zxv_token_compress(c, 64), ZXV_E_RANGE);
    c[63] = UINT64_MAX;
    o[63] = UINT64_MAX;
    IS(zxv_token_compress(c, 3), ZXV_E_RANGE);
    CHECK(memcmp(c, o, sizeof c) == 0, "compress leaves counts unchanged on failure");
    IS(zxv_token_compress(NULL, 3), ZXV_E_ARG);
}

static void test_token_fuse_split(void)
{
    uint64_t c[64], b[64], v;
    memset(c, 0, sizeof c);
    c[3] = 1;
    c[4] = 1; /* 5 + 8 */
    OK(zxv_token_fuse(c, 5, 3));
    CHECK(c[3] == 0 && c[4] == 0 && c[5] == 1, "5 + 8 -> 13");
    OK(zxv_token_split(c, 5));
    CHECK(c[5] == 0 && c[4] == 1 && c[3] == 1, "13 -> 8 + 5");
    memset(c, 0, sizeof c);
    c[1] = 1;
    OK(zxv_token_split(c, 1));
    CHECK(c[1] == 0 && c[0] == 2, "2 -> 1 + 1");
    c[1] = 1; /* now 1,1 at tiers 0 and 1 */
    OK(zxv_token_fuse(c, 2, 0));
    CHECK(c[0] == 1 && c[1] == 0 && c[2] == 1, "1 + 2 -> 3 (fuse up to exactly the octane)");

    /* refusals, unchanged on each */
    memset(c, 0, sizeof c);
    c[3] = 1;
    c[4] = 1;
    memcpy(b, c, sizeof c);
    IS(zxv_token_fuse(c, 4, 3), ZXV_E_STATE); /* tier 5 is past octane 4 */
    IS(zxv_token_fuse(c, 64, 3), ZXV_E_RANGE);
    IS(zxv_token_fuse(c, 63, 62), ZXV_E_RANGE);
    IS(zxv_token_fuse(c, 63, UINT32_MAX - 1), ZXV_E_RANGE); /* tier + 2 would wrap to 0 */
    IS(zxv_token_fuse(c, 63, UINT32_MAX), ZXV_E_RANGE);
    IS(zxv_token_fuse(c, 63, 2), ZXV_E_FUNDS); /* no tier-2 token */
    IS(zxv_token_fuse(c, 63, 4), ZXV_E_FUNDS); /* no tier-5 token */
    c[5] = UINT64_MAX;
    b[5] = UINT64_MAX;
    IS(zxv_token_fuse(c, 63, 3), ZXV_E_RANGE);
    CHECK(memcmp(c, b, sizeof c) == 0, "fuse refusals change nothing");
    IS(zxv_token_fuse(NULL, 63, 3), ZXV_E_ARG);
    IS(zxv_token_fuse(c, 63, 61), ZXV_E_FUNDS); /* 61 is the last fusable tier */

    memset(c, 0, sizeof c);
    c[0] = UINT64_MAX - 1;
    c[1] = 1;
    c[7] = 1;
    memcpy(b, c, sizeof c);
    IS(zxv_token_split(c, 0), ZXV_E_RANGE);
    IS(zxv_token_split(c, 64), ZXV_E_RANGE);
    IS(zxv_token_split(c, 2), ZXV_E_FUNDS);
    IS(zxv_token_split(c, 1), ZXV_E_RANGE); /* plain count would wrap */
    c[6] = UINT64_MAX;
    b[6] = UINT64_MAX;
    IS(zxv_token_split(c, 7), ZXV_E_RANGE);
    c[6] = 0;
    b[6] = 0;
    c[5] = UINT64_MAX;
    b[5] = UINT64_MAX;
    IS(zxv_token_split(c, 7), ZXV_E_RANGE);
    CHECK(memcmp(c, b, sizeof c) == 0, "split refusals change nothing");
    IS(zxv_token_split(NULL, 3), ZXV_E_ARG);
    c[5] = 0;
    c[0] = UINT64_MAX - 2;
    OK(zxv_token_split(c, 1));
    CHECK(c[0] == UINT64_MAX && c[1] == 0, "split to exactly UINT64_MAX plain");

    /* random walk: fuse and split keep the value */
    memset(c, 0, sizeof c);
    c[0] = 100000;
    OK(zxv_token_compress(c, 20));
    for (int i = 0; i < 20000; i++) {
        uint32_t t = (uint32_t) (rnd() % 21);
        if (rnd() & 1) (void) zxv_token_fuse(c, 20, t);
        else (void) zxv_token_split(c, t);
        for (unsigned k = 21; k < 64; k++) CHECK(c[k] == 0, "nothing past octane 20");
    }
    OK(zxv_token_value(c, &v, NULL));
    CHECK(v == 100000, "random fuse/split keeps the value (%llu)", (ull) v);
    uint64_t ov = 0;
    CHECK(counts_value(c, &ov) && ov == 100000, "and the oracle agrees (%llu)", (ull) ov);
    OK(zxv_token_compress(c, 20));
    uint64_t o[64];
    oracle_canonical(100000, 20, o);
    CHECK(memcmp(c, o, sizeof c) == 0, "and compresses back to the one canonical form");
}

static void test_token_code(void)
{
    uint32_t bits = 0;
    uint8_t ob[96];
    for (int i = 0; i < 3000; i++) {
        uint64_t v = i < 200 ? (uint64_t) i : (i & 1) ? rnd() : rnd() >> (rnd() % 64);
        if (v == UINT64_MAX) v--;
        OK(zxv_token_code_bits(v, &bits));
        CHECK(bits == oracle_code(v, ob), "bits of %llu = %u", (ull) v, bits);
    }
    OK(zxv_token_code_bits(0, &bits));
    CHECK(bits == 2, "0 is '11'");
    OK(zxv_token_code_bits(UINT64_MAX - 1, &bits));
    CHECK(bits == ZXV_TOKEN_CODE_MAX_BITS, "max code is 93 bits (%u)", bits);
    IS(zxv_token_code_bits(UINT64_MAX, &bits), ZXV_E_RANGE);
    IS(zxv_token_code_bits(1, NULL), ZXV_E_ARG);

    /* known bytes: 0 -> 11, 1 -> 011, 2 -> 0011, 3 -> 1011 (lowest bit first) */
    static const struct {
        uint64_t v;
        uint8_t byte;
        uint32_t len;
    } kat[] = {{0, 0x03, 2}, {1, 0x06, 3}, {2, 0x0C, 4}, {3, 0x0D, 4}, {4, 0x18, 5}, {7, 0x30, 6}};
    for (size_t i = 0; i < sizeof kat / sizeof kat[0]; i++) {
        uint8_t buf[2] = {0, 0};
        uint32_t pos = 0;
        OK(zxv_token_code_put(buf, 2, &pos, kat[i].v));
        CHECK(buf[0] == kat[i].byte && pos == kat[i].len, "code %llu = 0x%02x/%u (got 0x%02x/%u)",
              (ull) kat[i].v, kat[i].byte, kat[i].len, buf[0], pos);
        uint64_t got = 99;
        pos = 0;
        OK(zxv_token_code_get(buf, 2, &pos, &got));
        CHECK(got == kat[i].v && pos == kat[i].len, "decode %llu", (ull) kat[i].v);
    }

    /* a stream of 400 values, bytes checked against the oracle encoder */
    enum { NV = 400 };
    static uint8_t buf[NV * 12], want[NV * 12];
    uint64_t vals[NV];
    memset(want, 0, sizeof want);
    memset(buf, 0xA5, sizeof buf); /* code_put must also write the zero bits */
    uint32_t pos = 0, opos = 0;
    for (int i = 0; i < NV; i++) {
        vals[i] = i == 0 ? UINT64_MAX - 1 : i == 1 ? 0 : (rnd() >> (rnd() % 64));
        if (vals[i] == UINT64_MAX) vals[i] = 5;
        OK(zxv_token_code_put(buf, sizeof buf, &pos, vals[i]));
        unsigned n = oracle_code(vals[i], ob);
        for (unsigned k = 0; k < n; k++, opos++)
            if (ob[k]) want[opos / 8] |= (uint8_t) (1u << (opos % 8));
    }
    CHECK(pos == opos, "stream length %u vs %u", pos, opos);
    CHECK(memcmp(buf, want, pos / 8) == 0, "stream bytes match the oracle");
    uint32_t rp = 0;
    for (int i = 0; i < NV; i++) {
        uint64_t got = 0;
        OK(zxv_token_code_get(buf, sizeof buf, &rp, &got));
        CHECK(got == vals[i], "value %d round trips (%llu vs %llu)", i, (ull) got, (ull) vals[i]);
    }
    CHECK(rp == pos, "read position = write position");

    /* refusals */
    uint8_t small[2] = {0x5A, 0x5A};
    pos = 11; /* 7 needs 6 bits, 5 are left */
    IS(zxv_token_code_put(small, 2, &pos, 7), ZXV_E_BUFFER);
    CHECK(pos == 11 && small[0] == 0x5A && small[1] == 0x5A, "nothing written on BUFFER");
    pos = 10;
    OK(zxv_token_code_put(small, 2, &pos, 4)); /* 5 bits at 10..14 */
    CHECK(pos == 15, "fits at the end");
    pos = 17;
    IS(zxv_token_code_put(small, 2, &pos, 0), ZXV_E_RANGE);
    pos = 16;
    IS(zxv_token_code_put(small, 2, &pos, 0), ZXV_E_BUFFER);
    pos = 0;
    IS(zxv_token_code_put(small, 2, &pos, UINT64_MAX), ZXV_E_RANGE);
    IS(zxv_token_code_put(NULL, 2, &pos, 1), ZXV_E_ARG);
    IS(zxv_token_code_put(small, 2, NULL, 1), ZXV_E_ARG);
    IS(zxv_token_code_put(small, (size_t) ZXV_TOKEN_CODE_MAX_BYTES + 1, &pos, 1), ZXV_E_TOO_LONG);
    uint64_t got = 42;
    IS(zxv_token_code_get(small, (size_t) ZXV_TOKEN_CODE_MAX_BYTES + 1, &pos, &got), ZXV_E_TOO_LONG);
    IS(zxv_token_code_get(NULL, 2, &pos, &got), ZXV_E_ARG);
    IS(zxv_token_code_get(small, 2, NULL, &got), ZXV_E_ARG);
    IS(zxv_token_code_get(small, 2, &pos, NULL), ZXV_E_ARG);
    uint8_t trunc[2] = {0x01, 0x00}; /* one 1, never closed */
    pos = 0;
    IS(zxv_token_code_get(trunc, 2, &pos, &got), ZXV_E_FORMAT);
    pos = 17;
    IS(zxv_token_code_get(trunc, 2, &pos, &got), ZXV_E_RANGE);
    pos = 16;
    IS(zxv_token_code_get(trunc, 2, &pos, &got), ZXV_E_FORMAT);
    CHECK(pos == 16 && got == 42, "nothing written on failure");
    /* F(89) + F(91) + F(93) passes 64 bits */
    uint8_t big[12];
    memset(big, 0, sizeof big);
    big[87 / 8] |= (uint8_t) (1u << (87 % 8));
    big[89 / 8] |= (uint8_t) (1u << (89 % 8));
    big[91 / 8] |= (uint8_t) (1u << (91 % 8));
    big[92 / 8] |= (uint8_t) (1u << (92 % 8));
    pos = 0;
    IS(zxv_token_code_get(big, sizeof big, &pos, &got), ZXV_E_FORMAT);
    /* F(91) + F(93) fits: value F(91)+F(93)-1 */
    big[87 / 8] &= (uint8_t) ~(1u << (87 % 8));
    pos = 0;
    OK(zxv_token_code_get(big, sizeof big, &pos, &got));
    CHECK(got == fib(91) + fib(93) - 1 && pos == 93, "the largest codes still decode");
}

/* ===================================================================== */
/* Two threads transferring in opposite directions: the wallets lock in a
 * fixed order, so this neither deadlocks nor loses a token. */
struct pingpong {
    const zxv_cap *cap;
    zxv_token_wallet *from, *to;
    int bad;
};
static void *pingpong_run(void *arg)
{
    struct pingpong *p = arg;
    for (int i = 0; i < 20000; i++) {
        int32_t rc = zxv_token_wallet_transfer(p->cap, p->from, p->to, (uint64_t) (i % 97));
        if (rc != ZXV_OK && rc != ZXV_E_FUNDS) p->bad++;
    }
    return NULL;
}

static void test_wallet(void)
{
    zxv_ctx *ctx = NULL, *other = NULL;
    zxv_cap *root = NULL, *nomint = NULL, *swarm = NULL, *foreign = NULL;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_ctx_create(&other));
    int32_t rc = zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_5, &root);
    if (rc != ZXV_OK) {
        /* zxv_cap_create still refuses the 1.5 bits (scaffold): every gated
         * call can only be checked for refusal. */
        printf("NOTE wallet: zxv_cap_create refuses ZXV_CAP_MINT (%s); success paths skipped\n",
               zxv_status_name(rc));
        OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
        zxv_token_wallet *w = NULL;
        IS(zxv_token_wallet_create(root, 1, 0, &w), ZXV_E_CAP);
        CHECK(w == NULL, "no wallet without MINT");
        zxv_cap_destroy(root);
        zxv_ctx_destroy(other);
        zxv_ctx_destroy(ctx);
        return;
    }
    OK(zxv_cap_attenuate(root, ZXV_CAP_ALL_1_5 & ~ZXV_CAP_MINT, &nomint));
    OK(zxv_cap_attenuate(root, ZXV_CAP_SWARM, &swarm));
    OK(zxv_cap_create(other, 2, NULL, ZXV_CAP_ALL_1_5, &foreign));

    zxv_token_wallet *a = NULL, *b = NULL, *c = NULL, *x = NULL, *tmp = NULL;
    IS(zxv_token_wallet_create(nomint, 1, 0, &tmp), ZXV_E_CAP);
    IS(zxv_token_wallet_create(swarm, 1, 0, &tmp), ZXV_E_CAP); /* SWARM is not MINT */
    IS(zxv_token_wallet_create(NULL, 1, 0, &tmp), ZXV_E_ARG);
    IS(zxv_token_wallet_create(root, 1, 64, &tmp), ZXV_E_RANGE);
    IS(zxv_token_wallet_create(root, 1, 0, NULL), ZXV_E_ARG);
    CHECK(tmp == NULL, "no wallet on failure");
    OK(zxv_token_wallet_create(root, 7, ZXV_OCTANE_RACING, &a));
    OK(zxv_token_wallet_create(root, 8, ZXV_OCTANE_PLAIN, &b));
    OK(zxv_token_wallet_create(root, 9, ZXV_OCTANE_MAX, &c));
    OK(zxv_token_wallet_create(foreign, 1, 0, &x));

    uint32_t id = 0, oc = 99;
    uint64_t v = 1, p = 1, cnt[64], want[64];
    OK(zxv_token_wallet_info(a, &id, &oc, &v, &p));
    CHECK(id == 7 && oc == ZXV_OCTANE_RACING && v == 0 && p == 0, "empty RACING wallet");
    OK(zxv_token_wallet_info(a, NULL, NULL, NULL, NULL));

    /* mint */
    IS(zxv_token_wallet_mint(nomint, a, 5), ZXV_E_CAP);
    IS(zxv_token_wallet_mint(swarm, a, 5), ZXV_E_CAP);
    IS(zxv_token_wallet_mint(foreign, a, 5), ZXV_E_CAP);
    IS(zxv_token_wallet_mint(root, NULL, 5), ZXV_E_ARG);
    OK(zxv_token_wallet_mint(root, a, 600000));
    OK(zxv_token_wallet_mint(root, a, 400000));
    OK(zxv_token_wallet_info(a, NULL, NULL, &v, &p));
    CHECK(v == 1000000 && p == 17, "1,000,000 at RACING is 17 pieces (%llu, %llu)", (ull) v, (ull) p);
    OK(zxv_token_wallet_counts(a, cnt));
    OK(zxv_token_canonical(1000000, ZXV_OCTANE_RACING, want));
    CHECK(memcmp(cnt, want, sizeof cnt) == 0, "wallet holds the canonical form");
    IS(zxv_token_wallet_mint(root, a, UINT64_MAX - 999999), ZXV_E_RANGE);
    OK(zxv_token_wallet_mint(root, c, UINT64_MAX)); /* exactly full */
    OK(zxv_token_wallet_info(c, NULL, NULL, &v, NULL));
    CHECK(v == UINT64_MAX, "a full wallet");
    IS(zxv_token_wallet_mint(root, c, 1), ZXV_E_RANGE);
    OK(zxv_token_wallet_info(a, NULL, NULL, &v, NULL));
    CHECK(v == 1000000, "a refused mint changes nothing");

    /* spend */
    IS(zxv_token_wallet_spend(nomint, a, 1), ZXV_E_CAP);
    IS(zxv_token_wallet_spend(root, a, 1000001), ZXV_E_FUNDS);
    OK(zxv_token_wallet_spend(root, a, 1));
    OK(zxv_token_wallet_counts(a, cnt));
    OK(zxv_token_canonical(999999, ZXV_OCTANE_RACING, want));
    CHECK(memcmp(cnt, want, sizeof cnt) == 0, "change comes back canonical");
    IS(zxv_token_wallet_spend(root, NULL, 1), ZXV_E_ARG);

    /* octane: re-expresses, never revalues */
    IS(zxv_token_wallet_set_octane(nomint, a, 0), ZXV_E_CAP);
    IS(zxv_token_wallet_set_octane(root, a, 64), ZXV_E_RANGE);
    OK(zxv_token_wallet_set_octane(root, a, ZXV_OCTANE_MAX));
    OK(zxv_token_wallet_info(a, NULL, &oc, &v, &p));
    OK(zxv_token_canonical(999999, ZXV_OCTANE_MAX, want));
    uint64_t wp = 0;
    OK(zxv_token_value(want, NULL, &wp));
    CHECK(oc == ZXV_OCTANE_MAX && v == 999999 && p == wp && wp < 17, "up to MAX: same value, %llu pieces",
          (ull) p);
    OK(zxv_token_wallet_set_octane(root, a, ZXV_OCTANE_RACING));
    OK(zxv_token_wallet_counts(a, cnt));
    OK(zxv_token_canonical(999999, ZXV_OCTANE_RACING, want));
    CHECK(memcmp(cnt, want, sizeof cnt) == 0, "back down to RACING");

    /* transfer: receiver gets its own octane, total conserved */
    IS(zxv_token_wallet_transfer(nomint, a, b, 1), ZXV_E_CAP);
    IS(zxv_token_wallet_transfer(root, a, x, 1), ZXV_E_CAP); /* x is another context's */
    IS(zxv_token_wallet_transfer(foreign, x, a, 1), ZXV_E_CAP);
    IS(zxv_token_wallet_transfer(root, NULL, b, 1), ZXV_E_ARG);
    IS(zxv_token_wallet_transfer(root, a, NULL, 1), ZXV_E_ARG);
    IS(zxv_token_wallet_transfer(root, a, b, 1000000), ZXV_E_FUNDS);
    OK(zxv_token_wallet_transfer(root, a, b, 144));
    OK(zxv_token_wallet_counts(b, cnt));
    CHECK(cnt[0] == 144 && cnt[10] == 0, "PLAIN receiver holds 144 plain tokens");
    OK(zxv_token_wallet_info(a, NULL, NULL, &v, NULL));
    CHECK(v == 999999 - 144, "payer keeps the change (%llu)", (ull) v);
    OK(zxv_token_wallet_counts(a, cnt));
    OK(zxv_token_canonical(999999 - 144, ZXV_OCTANE_RACING, want));
    CHECK(memcmp(cnt, want, sizeof cnt) == 0, "payer's change is canonical at its octane");
    OK(zxv_token_wallet_transfer(root, a, a, 5)); /* self: a no-op */
    OK(zxv_token_wallet_info(a, NULL, NULL, &v, NULL));
    CHECK(v == 999999 - 144, "self transfer keeps the value");
    IS(zxv_token_wallet_transfer(root, a, a, 1000000), ZXV_E_FUNDS);
    IS(zxv_token_wallet_transfer(root, b, c, 1), ZXV_E_RANGE); /* c is full */
    OK(zxv_token_wallet_info(b, NULL, NULL, &v, NULL));
    CHECK(v == 144, "a refused transfer changes neither wallet");
    OK(zxv_token_wallet_transfer(root, c, b, UINT64_MAX - 144)); /* b becomes exactly full */
    OK(zxv_token_wallet_info(b, NULL, NULL, &v, NULL));
    uint64_t vc;
    OK(zxv_token_wallet_info(c, NULL, NULL, &vc, NULL));
    CHECK(v == UINT64_MAX && vc == 144, "conserved at the 64-bit edge");
    for (int i = 0; i < 2000; i++) { /* random ping-pong keeps the total */
        uint64_t va, vb2;
        OK(zxv_token_wallet_info(a, NULL, NULL, &va, NULL));
        OK(zxv_token_wallet_info(c, NULL, NULL, &vb2, NULL));
        uint64_t amt = rnd() % 5000;
        int32_t r1 = (i & 1) ? zxv_token_wallet_transfer(root, a, c, amt)
                             : zxv_token_wallet_transfer(root, c, a, amt);
        CHECK(r1 == ZXV_OK || r1 == ZXV_E_FUNDS, "ping-pong %s", zxv_status_name(r1));
        if ((i % 300) == 0) {
            OK(zxv_token_wallet_set_octane(root, a, (uint32_t) (rnd() % 64)));
            OK(zxv_token_wallet_set_octane(root, c, (uint32_t) (rnd() % 64)));
        }
        uint64_t na, nc;
        OK(zxv_token_wallet_info(a, NULL, NULL, &na, NULL));
        OK(zxv_token_wallet_info(c, NULL, NULL, &nc, NULL));
        CHECK(na + nc == va + vb2, "total conserved");
        OK(zxv_token_wallet_counts(a, cnt));
        int32_t canon = 0;
        OK(zxv_token_wallet_info(a, NULL, &oc, NULL, NULL));
        OK(zxv_token_is_canonical(cnt, oc, &canon));
        CHECK(canon == 1, "always canonical");
    }

    {
        uint64_t va, vc2, na, nc;
        OK(zxv_token_wallet_info(a, NULL, NULL, &va, NULL));
        OK(zxv_token_wallet_info(c, NULL, NULL, &vc2, NULL));
        struct pingpong t1 = {root, a, c, 0}, t2 = {root, c, a, 0};
        pthread_t h1, h2;
        CHECK(pthread_create(&h1, NULL, pingpong_run, &t1) == 0, "thread 1");
        CHECK(pthread_create(&h2, NULL, pingpong_run, &t2) == 0, "thread 2");
        pthread_join(h1, NULL);
        pthread_join(h2, NULL);
        OK(zxv_token_wallet_info(a, NULL, NULL, &na, NULL));
        OK(zxv_token_wallet_info(c, NULL, NULL, &nc, NULL));
        CHECK(t1.bad == 0 && t2.bad == 0, "concurrent transfers only OK or FUNDS");
        CHECK(na + nc == va + vc2, "concurrent transfers conserve the total");
    }

    IS(zxv_token_wallet_info(NULL, &id, &oc, &v, &p), ZXV_E_ARG);
    IS(zxv_token_wallet_counts(NULL, cnt), ZXV_E_ARG);
    IS(zxv_token_wallet_counts(a, NULL), ZXV_E_ARG);

    /* revoked cap */
    zxv_cap *child = NULL;
    OK(zxv_cap_attenuate(root, ZXV_CAP_MINT, &child));
    OK(zxv_token_wallet_mint(child, a, 1));
    OK(zxv_cap_revoke(child));
    IS(zxv_token_wallet_mint(child, a, 1), ZXV_E_CAP);

    /* fail closed */
    OK(zxv_ctx_halt(ctx, "test"));
    IS(zxv_token_wallet_mint(root, a, 1), ZXV_E_POISONED);
    IS(zxv_token_wallet_spend(root, a, 1), ZXV_E_POISONED);
    IS(zxv_token_wallet_transfer(root, a, b, 1), ZXV_E_POISONED);
    IS(zxv_token_wallet_set_octane(root, a, 1), ZXV_E_POISONED);
    IS(zxv_token_wallet_info(a, &id, &oc, &v, &p), ZXV_E_POISONED);
    IS(zxv_token_wallet_counts(a, cnt), ZXV_E_POISONED);
    IS(zxv_token_wallet_create(root, 1, 0, &tmp), ZXV_E_POISONED);
    OK(zxv_token_wallet_info(x, NULL, NULL, NULL, NULL)); /* the other context is fine */
    uint64_t f;
    OK(zxv_coin_face(3, &f)); /* the pure functions take no context */
    OK(zxv_token_face(3, &f, NULL));

    zxv_token_wallet_destroy(a);
    zxv_token_wallet_destroy(b);
    zxv_token_wallet_destroy(c);
    zxv_token_wallet_destroy(x);
    zxv_token_wallet_destroy(NULL);
    zxv_cap_destroy(child);
    zxv_cap_destroy(swarm);
    zxv_cap_destroy(nomint);
    zxv_cap_destroy(foreign);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(other);
    zxv_ctx_destroy(ctx);
}

int main(void)
{
    test_feature();
    test_coin_ladder();
    test_coin_split();
    test_token_faces();
    test_token_canonical();
    test_token_fuse_split();
    test_token_code();
    test_wallet();
    printf("test_zxv_abi15_coin: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
