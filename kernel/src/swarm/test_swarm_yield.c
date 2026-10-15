/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_yield.c — known-answer and property tests for token yield
 * (Y1-Y6 in swarm_yield.h). Known answers are worked by hand from the rules;
 * the property test checks the wide arithmetic against the host's __int128. */
#include <stdio.h>
#include <string.h>
#include "swarm_yield.h"
#ifdef YIELD_WITH_PAY
#    include "pay_assure.h"
#endif

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int same(const swarm_yield_t *a, const swarm_yield_t *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

static void test_empty(void)
{
    swarm_yield_t y, z;
    memset(&y, 0x5A, sizeof y);
    swarm_yield_init(&y);
    CHECK(y.tokens == 0 && y.wasted == 0 && y.work_milli == 0, "init zeroes amounts");
    CHECK(y.tasks == 0 && y.passed == 0 && y.first_try == 0, "init zeroes counts");
    CHECK(swarm_yield_per_k(&y) == 0, "no tokens: yield 0");
    CHECK(swarm_yield_rate(&y, 1000000) == 0, "no tokens: rate 0");
    CHECK(swarm_yield_first_try_milli(&y) == 0, "no tasks: first-try 0");
    CHECK(swarm_yield_waste_milli(&y) == 0, "no tokens: waste 0");
    swarm_yield_init(&z);
    CHECK(swarm_yield_cmp(&y, &z) == 0, "two empty yields are equal");
    swarm_yield_init(NULL); /* must not crash */
    CHECK(swarm_yield_per_k(NULL) == 0 && swarm_yield_rate(NULL, 5) == 0, "NULL reads 0");
    CHECK(swarm_yield_first_try_milli(NULL) == 0 && swarm_yield_waste_milli(NULL) == 0,
          "NULL shares 0");
}

static void test_args(void)
{
    swarm_yield_t y, before;
    swarm_yield_init(&y);
    CHECK(swarm_yield_task(&y, 100, 1, 2000, 1000) == SWARM_OK, "a good task");
    before = y;
    CHECK(swarm_yield_task(NULL, 100, 1, 2000, 1000) == SWARM_ERR_ARG, "NULL yield");
    CHECK(swarm_yield_task(&y, 0, 1, 2000, 1000) == SWARM_ERR_ARG, "zero tokens");
    CHECK(swarm_yield_task(&y, 100, 0, 2000, 1000) == SWARM_ERR_ARG, "zero attempts");
    CHECK(swarm_yield_task(&y, 100, 1, 2000, 1001) == SWARM_ERR_ARG, "share above 1");
    CHECK(same(&y, &before), "refused tasks change nothing");
    CHECK(swarm_yield_task(&y, 1, 1, 1800, 1000) == SWARM_OK, "share exactly 1 is allowed");
    CHECK(swarm_yield_merge(NULL, &y) == SWARM_ERR_ARG, "merge into NULL");
    CHECK(swarm_yield_merge(&y, NULL) == SWARM_ERR_ARG, "merge from NULL");
}

/* Y2: the gate is r * l >= 1.8 (milli x milli >= 1,800,000) */
static void test_gate(void)
{
    swarm_yield_t y;
    swarm_yield_init(&y);
    CHECK(swarm_yield_task(&y, 10, 1, 1800, 1000) == SWARM_OK, "r 1.8 l 1");
    CHECK(y.work_milli == 1800 && y.passed == 1 && y.wasted == 0, "exactly 1.8 passes, 1800");
    CHECK(swarm_yield_task(&y, 20, 1, 1799, 1000) == SWARM_OK, "r 1.799 l 1");
    CHECK(y.work_milli == 1800 && y.passed == 1 && y.wasted == 20, "1.799 fails, tokens wasted");
    CHECK(swarm_yield_task(&y, 30, 1, 2000, 900) == SWARM_OK, "r 2 l 0.9");
    CHECK(y.work_milli == 3600 && y.passed == 2, "2 x 0.9 = 1.8 passes, earns 1800");
    CHECK(swarm_yield_task(&y, 40, 1, 3000, 599) == SWARM_OK, "r 3 l 0.599");
    CHECK(y.work_milli == 3600 && y.wasted == 60, "3 x 0.599 = 1.797 fails");
    CHECK(swarm_yield_task(&y, 50, 1, 2501, 999) == SWARM_OK, "r 2.501 l 0.999");
    /* 2501 * 999 = 2,498,499; / 1000 = 2498 (rounded down) */
    CHECK(y.work_milli == 3600 + 2498, "work rounds down");
    CHECK(y.tokens == 150 && y.tasks == 5 && y.passed == 3, "every token and task counted");
}

/* Y1-Y4: the owner's point in numbers. A: 300 tok/s but three tries,
 * 1200 tokens a task. B: 150 tok/s, right first time, 400 tokens. */
static void test_retries_lose(void)
{
    swarm_yield_t a, b;
    swarm_yield_init(&a);
    swarm_yield_init(&b);
    CHECK(swarm_yield_task(&a, 1200, 3, 2000, 1000) == SWARM_OK, "A's task");
    CHECK(swarm_yield_task(&b, 400, 1, 2000, 1000) == SWARM_OK, "B's task");
    CHECK(swarm_yield_per_k(&a) == 1666, "A: 2000 * 1000 / 1200 = 1666");
    CHECK(swarm_yield_per_k(&b) == 5000, "B: 2000 * 1000 / 400 = 5000");
    CHECK(swarm_yield_rate(&a, 300000) == 500, "A: 2000 * 300 / 1200 = 500 per s");
    CHECK(swarm_yield_rate(&b, 150000) == 750, "B: 2000 * 150 / 400 = 750 per s");
    CHECK(swarm_yield_cmp(&a, &b) == -1 && swarm_yield_cmp(&b, &a) == 1, "B yields more");
    CHECK(swarm_yield_cmp_rate(&a, 300000, &b, 150000) == -1, "B does more per second");
    CHECK(swarm_yield_cmp_rate(&b, 150000, &a, 300000) == 1, "and the reverse");
    /* A at 450 tok/s draws level: 2000 * 450 / 1200 = 750 */
    CHECK(swarm_yield_cmp_rate(&a, 450000, &b, 150000) == 0, "A at 3x the speed ties");
    CHECK(swarm_yield_cmp_rate(&a, 450001, &b, 150000) == 1, "and just above wins");
    CHECK(a.first_try == 0 && b.first_try == 1, "first try only without retries");
}

static void test_shares(void)
{
    swarm_yield_t y;
    swarm_yield_init(&y);
    CHECK(swarm_yield_task(&y, 100, 1, 2000, 1000) == SWARM_OK, "pass first time");
    CHECK(swarm_yield_task(&y, 300, 2, 2000, 1000) == SWARM_OK, "pass on the second");
    CHECK(swarm_yield_task(&y, 600, 1, 1000, 1000) == SWARM_OK, "never passes");
    CHECK(swarm_yield_first_try_milli(&y) == 333, "1 of 3 first time");
    CHECK(swarm_yield_waste_milli(&y) == 600, "600 of 1000 tokens wasted");
    CHECK(y.passed == 2 && y.first_try == 1, "a failed first attempt is not a first-try pass");
    CHECK(swarm_yield_per_k(&y) == 4000, "4000 work over 1000 tokens");
}

static void test_merge(void)
{
    swarm_yield_t a, b, before;
    swarm_yield_init(&a);
    swarm_yield_init(&b);
    swarm_yield_task(&a, 100, 1, 2000, 1000);
    swarm_yield_task(&b, 300, 2, 3000, 1000);
    swarm_yield_task(&b, 50, 1, 100, 100);
    swarm_yield_task(&b, 10, 1, 2000, 1000);
    CHECK(swarm_yield_merge(&a, &b) == SWARM_OK, "merge");
    CHECK(a.tokens == 460 && a.wasted == 50 && a.work_milli == 7000, "amounts add");
    CHECK(a.tasks == 4 && a.passed == 3 && a.first_try == 2, "counts add");
    /* each field's overflow is refused, and nothing changes */
    for (int f = 0; f < 6; f++) {
        swarm_yield_t big;
        swarm_yield_init(&big);
        uint64_t *field[6] = {&big.tokens, &big.wasted, &big.work_milli,
                              &big.tasks,  &big.passed, &big.first_try};
        *field[f] = UINT64_MAX;
        before = a;
        CHECK(swarm_yield_merge(&a, &big) == SWARM_ERR_ARG, "merge overflow refused");
        CHECK(same(&a, &before), "refused merge changes nothing");
    }
}

static void test_overflow(void)
{
    swarm_yield_t y, before;
    swarm_yield_init(&y);
    y.tokens = UINT64_MAX - 5;
    before = y;
    CHECK(swarm_yield_task(&y, 6, 1, 2000, 1000) == SWARM_ERR_ARG, "token overflow refused");
    CHECK(same(&y, &before), "unchanged");
    CHECK(swarm_yield_task(&y, 5, 1, 2000, 1000) == SWARM_OK, "exactly to the top is fine");

    swarm_yield_init(&y);
    y.work_milli = UINT64_MAX - 1799;
    before = y;
    CHECK(swarm_yield_task(&y, 1, 1, 1801, 1000) == SWARM_ERR_ARG, "work overflow refused");
    CHECK(same(&y, &before), "unchanged");
    CHECK(swarm_yield_task(&y, 1, 1, 1799, 1000) == SWARM_OK, "a failing task adds no work");
    y.work_milli = UINT64_MAX;
    CHECK(swarm_yield_task(&y, 1, 1, 0, 1000) == SWARM_OK, "even at the top of work");
    CHECK(y.work_milli == UINT64_MAX, "work unchanged");

    swarm_yield_init(&y);
    y.wasted = UINT64_MAX;
    before = y;
    CHECK(swarm_yield_task(&y, 1, 1, 0, 0) == SWARM_ERR_ARG, "waste overflow refused");
    CHECK(same(&y, &before), "unchanged");
    CHECK(swarm_yield_task(&y, 1, 1, 2000, 1000) == SWARM_OK, "a passing task wastes nothing");

    swarm_yield_init(&y);
    y.tasks = UINT64_MAX;
    before = y;
    CHECK(swarm_yield_task(&y, 1, 1, 2000, 1000) == SWARM_ERR_ARG, "task count overflow");
    CHECK(same(&y, &before), "unchanged");
}

static void test_wide(void)
{
    swarm_yield_t y;
    swarm_yield_init(&y);
    y.tokens = 1;
    y.work_milli = 1ULL << 33;
    /* 2^33 * 2^33 = 2^66 does not fit 64 bits, but 2^66 / 1000 does */
    CHECK(swarm_yield_rate(&y, 1ULL << 33) == 73786976294838206ULL, "2^66 / 1000 exact");
    y.work_milli = UINT64_MAX;
    CHECK(swarm_yield_rate(&y, UINT64_MAX) == UINT64_MAX, "too big: saturates");
    CHECK(swarm_yield_per_k(&y) == UINT64_MAX, "per_k saturates");
    y.tokens = 999;
    /* (2^64 - 1) * 1000 = 999 * 2^64 + (2^64 - 1000): over 999 it is >= 2^64 */
    CHECK(swarm_yield_per_k(&y) == UINT64_MAX, "quotient of exactly 2^64 or more saturates");
    CHECK(swarm_yield_rate(&y, 999999) == UINT64_MAX, "rate saturates too");
    CHECK(swarm_yield_rate(&y, 999) == 18446744073709551ULL, "(2^64 - 1) * 999 / 999 / 1000");
    y.tokens = 1000;
    CHECK(swarm_yield_per_k(&y) == UINT64_MAX, "(2^64 - 1) * 1000 / 1000 exact");
    y.tokens = 1001;
    /* floor((2^64 - 1) * 1000 / 1001) */
    CHECK(swarm_yield_per_k(&y) == 18428315757951600014ULL, "128-bit product, 64-bit quotient");
}

static uint64_t rnd_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return rnd_state;
}

static uint64_t pick(void)
{
    switch (rnd() % 4) {
    case 0:
        return rnd();
    case 1:
        return rnd() >> (rnd() % 64);
    case 2:
        return rnd() % 5000;
    default:
        return UINT64_MAX - rnd() % 3;
    }
}

/* Y5: every result against the host's 128-bit arithmetic */
static void test_property(void)
{
    typedef unsigned __int128 u128;
    for (int i = 0; i < 200000; i++) {
        swarm_yield_t a, b;
        swarm_yield_init(&a);
        swarm_yield_init(&b);
        a.tokens = pick();
        a.work_milli = pick();
        b.tokens = pick();
        b.work_milli = pick();
        uint64_t sa = pick(), sb = pick();

        uint64_t want_k = 0, want_r = 0;
        if (a.tokens) {
            u128 k = (u128) a.work_milli * 1000u / a.tokens;
            want_k = k > UINT64_MAX ? UINT64_MAX : (uint64_t) k;
            u128 r = (u128) a.work_milli * sa / a.tokens / 1000u;
            want_r = r > UINT64_MAX ? UINT64_MAX : (uint64_t) r;
        }
        if (swarm_yield_per_k(&a) != want_k) {
            CHECK(0, "per_k matches 128-bit");
            break;
        }
        if (swarm_yield_rate(&a, sa) != want_r) {
            CHECK(0, "rate matches 128-bit");
            break;
        }
        /* cross products need 192 bits: compare as (q, remainder) pairs */
        uint64_t wa = a.tokens ? a.work_milli : 0, ta = a.tokens ? a.tokens : 1;
        uint64_t wb = b.tokens ? b.work_milli : 0, tb = b.tokens ? b.tokens : 1;
        u128 na = (u128) wa * sa, nb = (u128) wb * sb;
        u128 qa = na / ta, qb = nb / tb, ra = na % ta, rb = nb % tb;
        int want_c;
        if (qa != qb)
            want_c = qa < qb ? -1 : 1;
        else {
            /* ra/ta vs rb/tb, both < 1: ra*tb vs rb*ta fits 128 bits */
            u128 x = (u128) (uint64_t) ra * tb, z = (u128) (uint64_t) rb * ta;
            want_c = x < z ? -1 : x > z ? 1 : 0;
        }
        if (swarm_yield_cmp_rate(&a, sa, &b, sb) != want_c) {
            CHECK(0, "cmp_rate matches exact comparison");
            break;
        }
        u128 ya = (u128) wa * tb, yb = (u128) wb * ta;
        int want_y = ya < yb ? -1 : ya > yb ? 1 : 0;
        if (swarm_yield_cmp(&a, &b) != want_y) {
            CHECK(0, "cmp matches 128-bit");
            break;
        }
        uint64_t part = pick(), whole = pick();
        if (part > whole) {
            uint64_t t = part;
            part = whole;
            whole = t;
        }
        swarm_yield_t s;
        swarm_yield_init(&s);
        s.tokens = whole;
        s.wasted = part;
        s.tasks = whole;
        s.first_try = part;
        uint32_t want_s = whole ? (uint32_t) ((u128) part * 1000u / whole) : 0;
        if (swarm_yield_waste_milli(&s) != want_s || swarm_yield_first_try_milli(&s) != want_s) {
            CHECK(0, "shares match 128-bit");
            break;
        }
    }
}

static int bar_is(const swarm_yield_bar_t *b, uint64_t o, uint64_t h, uint64_t l, uint64_t c,
                  uint64_t tok, uint64_t work, uint64_t tasks)
{
    return b->open == o && b->high == h && b->low == l && b->close == c && b->tokens == tok &&
           b->work_milli == work && b->tasks == tasks;
}

/* Y7: candles, worked by hand */
static void test_chart(void)
{
    static swarm_yield_chart_t c;
    swarm_yield_bar_t b;
    memset(&c, 0x77, sizeof c);
    swarm_yield_chart_init(&c);
    CHECK(c.count == 0 && c.epochs == 0 && c.head == 0 && c.last_close == 0, "init");
    CHECK(c.cur.tokens == 0 && c.open == 0 && c.high == 0 && c.low == 0, "init open epoch");
    int clear = 1;
    for (uint32_t i = 0; i < SWARM_YIELD_BARS; i++)
        clear &= bar_is(&c.bars[i], 0, 0, 0, 0, 0, 0, 0);
    CHECK(clear, "init clears every candle");
    CHECK(!swarm_yield_chart_bar(&c, 0, &b), "no candle yet");

    /* epoch 1: yields 5000 (2000/400), 1666 (2000/1200), 0 (fails), 8000 (4000/500) */
    CHECK(swarm_yield_chart_task(&c, 400, 1, 2000, 1000) == SWARM_OK, "t1");
    CHECK(swarm_yield_chart_task(&c, 1200, 3, 2000, 1000) == SWARM_OK, "t2");
    CHECK(swarm_yield_chart_task(&c, 100, 1, 1000, 1000) == SWARM_OK, "t3");
    CHECK(swarm_yield_chart_task(&c, 500, 1, 4000, 1000) == SWARM_OK, "t4");
    CHECK(swarm_yield_chart_close(&c) == SWARM_OK, "close 1");
    CHECK(swarm_yield_chart_bar(&c, 0, &b), "candle 1");
    /* close: 8000 work over 2200 tokens = 3636 */
    CHECK(bar_is(&b, 5000, 8000, 0, 3636, 2200, 8000, 4), "candle 1 OHLC and volume");
    CHECK(c.last_close == 3636 && c.count == 1 && c.epochs == 1, "after close 1");
    CHECK(c.cur.tasks == 0 && c.open == 0 && c.high == 0 && c.low == 0, "next epoch empty");

    /* epoch 2: nothing: a flat candle at the last close */
    CHECK(swarm_yield_chart_close(&c) == SWARM_OK, "close 2");
    CHECK(swarm_yield_chart_bar(&c, 0, &b), "candle 2");
    CHECK(bar_is(&b, 3636, 3636, 3636, 3636, 0, 0, 0), "flat candle, no volume");

    /* epoch 3: one task, open = high = low = close */
    CHECK(swarm_yield_chart_task(&c, 300, 1, 3000, 1000) == SWARM_OK, "t5");
    CHECK(swarm_yield_chart_close(&c) == SWARM_OK, "close 3");
    CHECK(swarm_yield_chart_bar(&c, 0, &b) && bar_is(&b, 10000, 10000, 10000, 10000, 300, 3000, 1),
          "one-task candle");
    CHECK(swarm_yield_chart_bar(&c, 2, &b) && b.close == 3636, "two back is candle 1");
    CHECK(!swarm_yield_chart_bar(&c, 3, &b), "only three held");

    /* epoch 4: the first task is not the high: open 2000, high 9000, low 2000 */
    CHECK(swarm_yield_chart_task(&c, 1000, 1, 2000, 1000) == SWARM_OK, "t6");
    CHECK(swarm_yield_chart_task(&c, 200, 1, 1800, 1000) == SWARM_OK, "t7");
    CHECK(swarm_yield_chart_close(&c) == SWARM_OK, "close 4");
    /* close: 3800 over 1200 = 3166 */
    CHECK(swarm_yield_chart_bar(&c, 0, &b) && bar_is(&b, 2000, 9000, 2000, 3166, 1200, 3800, 2),
          "high after open");

    /* refusals change nothing */
    swarm_yield_chart_t before = c;
    CHECK(swarm_yield_chart_task(&c, 0, 1, 2000, 1000) == SWARM_ERR_ARG, "bad task refused");
    c.cur.tokens = UINT64_MAX;
    before = c;
    CHECK(swarm_yield_chart_task(&c, 1, 1, 2000, 1000) == SWARM_ERR_ARG, "overflow refused");
    CHECK(memcmp(&c, &before, sizeof c) == 0, "chart unchanged");
    swarm_yield_init(&c.cur);
    CHECK(swarm_yield_chart_task(NULL, 1, 1, 2000, 1000) == SWARM_ERR_ARG, "NULL chart");
    CHECK(swarm_yield_chart_close(NULL) == SWARM_ERR_ARG, "NULL close");
    CHECK(!swarm_yield_chart_bar(NULL, 0, &b) && !swarm_yield_chart_bar(&c, 0, NULL), "NULL bar");
    swarm_yield_chart_init(NULL);
    c.epochs = UINT64_MAX;
    CHECK(swarm_yield_chart_close(&c) == SWARM_ERR_ARG, "epoch counter full");
}

/* Y7: the ring keeps the last SWARM_YIELD_BARS candles, newest first */
static void test_chart_ring(void)
{
    static swarm_yield_chart_t c;
    swarm_yield_bar_t b;
    swarm_yield_chart_init(&c);
    for (uint32_t e = 1; e <= SWARM_YIELD_BARS + 6; e++) {
        CHECK(swarm_yield_chart_task(&c, e, 1, 2000, 1000) == SWARM_OK, "ring task");
        CHECK(swarm_yield_chart_close(&c) == SWARM_OK, "ring close");
    }
    CHECK(c.count == SWARM_YIELD_BARS && c.epochs == SWARM_YIELD_BARS + 6, "ring full");
    CHECK(swarm_yield_chart_bar(&c, 0, &b) && b.tokens == SWARM_YIELD_BARS + 6, "newest");
    CHECK(swarm_yield_chart_bar(&c, 1, &b) && b.tokens == SWARM_YIELD_BARS + 5, "one back");
    CHECK(swarm_yield_chart_bar(&c, SWARM_YIELD_BARS - 1, &b) && b.tokens == 7, "oldest held");
    CHECK(!swarm_yield_chart_bar(&c, SWARM_YIELD_BARS, &b), "older are gone");
}

/* Y7: low <= close <= high and low <= open <= high, every candle */
static void test_chart_property(void)
{
    static swarm_yield_chart_t c;
    swarm_yield_bar_t b;
    swarm_yield_chart_init(&c);
    for (int e = 0; e < 2000; e++) {
        uint32_t n = (uint32_t) (rnd() % 6);
        uint64_t tok = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t t = 1 + rnd() % 100000;
            if (swarm_yield_chart_task(&c, t, 1 + (uint32_t) (rnd() % 3), (uint32_t) (rnd() % 6000),
                                       (uint32_t) (rnd() % 1001)) == SWARM_OK)
                tok += t;
        }
        swarm_yield_chart_close(&c);
        if (!swarm_yield_chart_bar(&c, 0, &b) || b.low > b.close || b.close > b.high ||
            b.low > b.open || b.open > b.high || b.tokens != tok) {
            CHECK(0, "candle ordering and volume");
            break;
        }
    }
}

/* Y8 */
static void test_weights(void)
{
    swarm_yield_t ys[3];
    uint64_t w[3] = {9, 9, 9};
    for (int i = 0; i < 3; i++) swarm_yield_init(&ys[i]);
    swarm_yield_task(&ys[0], 1000, 1, 2000, 1000); /* 2000 work */
    swarm_yield_task(&ys[1], 1000, 1, 6000, 1000); /* 6000 work */
    swarm_yield_task(&ys[2], 1000, 1, 1000, 1000); /* fails: 0 */
    CHECK(swarm_yield_weights(SWARM_YIELD_LINK_OFF, ys, 3, w), "off");
    CHECK(w[0] == 1 && w[1] == 1 && w[2] == 1, "off: even split");
    CHECK(swarm_yield_weights(SWARM_YIELD_LINK_WORK, ys, 3, w), "work");
    CHECK(w[0] == 2000 && w[1] == 6000 && w[2] == 0, "work: verified work");
    CHECK(!swarm_yield_weights((swarm_yield_link_t) 2, ys, 3, w), "unknown mode");
    CHECK(!swarm_yield_weights(SWARM_YIELD_LINK_WORK, NULL, 3, w), "NULL yields");
    CHECK(!swarm_yield_weights(SWARM_YIELD_LINK_WORK, ys, 3, NULL), "NULL weights");
    CHECK(!swarm_yield_weights(SWARM_YIELD_LINK_WORK, ys, 0, w), "no models");
#ifdef YIELD_WITH_PAY
    /* fed to the node-bounty pool's own split: 8000 split 1:3:0, no cap bite */
    uint64_t out[3] = {0, 0, 0};
    uint64_t left = pay_commons_split(8000, w, 3, (pay_rat_t){1, 1}, out);
    CHECK(out[0] == 2000 && out[1] == 6000 && out[2] == 0 && left == 0, "pool split by work");
    /* the anti-monopoly cap still binds: at most half each */
    left = pay_commons_split(8000, w, 3, (pay_rat_t){1, 2}, out);
    CHECK(out[1] <= 4000 && out[0] + out[1] + out[2] + left == 8000, "cap binds, nothing minted");
    printf("  pool link checked against pay_commons_split\n");
#endif
}

int main(void)
{
    test_empty();
    test_args();
    test_gate();
    test_retries_lose();
    test_shares();
    test_merge();
    test_overflow();
    test_wide();
    test_property();
    test_chart();
    test_chart_ring();
    test_chart_property();
    test_weights();
    if (failures) {
        printf("test_swarm_yield: %d FAILED\n", failures);
        return 1;
    }
    printf("test_swarm_yield: all passed\n");
    return 0;
}
