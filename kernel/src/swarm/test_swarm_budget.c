/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_budget.c — known-answer tests for the Fibonacci tokens-per-cycle
 * allocator. Every expected value below is worked by hand from the rule in
 * swarm_budget.h, not read back from the code under test. */
#include <stdio.h>
#include "swarm_budget.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t allot(const swarm_budget_t *b, uint32_t id)
{
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].model_id == id) return b->slots[i].allotted;
    return (uint64_t) -1;
}

static uint64_t total_allotted(const swarm_budget_t *b)
{
    uint64_t t = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) t += b->slots[i].allotted;
    return t;
}

static void test_fibonacci(void)
{
    /* OEIS A000045 */
    const uint64_t want[12] = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};
    for (uint32_t n = 1; n <= 12; n++) CHECK(swarm_fib(n) == want[n - 1], "F(n) matches A000045");
    CHECK(swarm_fib(0) == 0, "F(0) rejected as 0");
    CHECK(swarm_fib(93) == 12200160415121876738ULL, "F(93), the largest 64-bit Fibonacci");
    CHECK(swarm_fib(94) == 0, "F(94) overflow rejected");
}

static void test_capacity_and_weight(void)
{
    const uint32_t cap[8] = {1, 2, 3, 5, 8, 13, 21, 34};
    for (uint32_t d = 0; d < 8; d++) CHECK(swarm_level_capacity(d) == cap[d], "R1 capacity F(d+2)");
    CHECK(swarm_level_capacity(8) == 0, "capacity out of range");
    /* L = 4: weights 5 : 3 : 2 : 1 */
    CHECK(swarm_level_weight(0, 4) == 5, "R2 weight d0 L4");
    CHECK(swarm_level_weight(1, 4) == 3, "R2 weight d1 L4");
    CHECK(swarm_level_weight(2, 4) == 2, "R2 weight d2 L4");
    CHECK(swarm_level_weight(3, 4) == 1, "R2 weight d3 L4");
    CHECK(swarm_level_weight(4, 4) == 0, "R2 weight out of range");
    /* L = 8: top weight F(9) = 34 */
    CHECK(swarm_level_weight(0, 8) == 34, "R2 weight d0 L8");
}

static void test_full_swarm_split(void)
{
    /* L = 4, T = 1100, levels filled to capacity 1, 2, 3, 5.
     * Level budgets 1100 * {5,3,2,1}/11 = 500, 300, 200, 100.
     * Level 2: 200 / 3 = 66 r 2 -> 67, 67, 66. Level 3: 100 / 5 = 20 each. */
    swarm_budget_t b;
    CHECK(swarm_budget_init(&b, 4, 1100) == SWARM_OK, "init");
    uint32_t id = 1;
    const uint32_t per_level[4] = {1, 2, 3, 5};
    for (uint32_t d = 0; d < 4; d++)
        for (uint32_t k = 0; k < per_level[d]; k++)
            CHECK(swarm_budget_register(&b, id++, d) == SWARM_OK, "register within capacity");
    CHECK(swarm_budget_register(&b, 99, 0) == SWARM_ERR_FULL, "R1 rejects a 2nd conductor");
    CHECK(swarm_budget_register(&b, 1, 3) == SWARM_ERR_DUPLICATE, "duplicate id rejected");

    CHECK(swarm_budget_begin_cycle(&b) == SWARM_OK, "begin");
    CHECK(b.level_budget[0] == 500 && b.level_budget[1] == 300 && b.level_budget[2] == 200 &&
              b.level_budget[3] == 100,
          "level budgets 500/300/200/100");
    CHECK(allot(&b, 1) == 500, "conductor 500");
    CHECK(allot(&b, 2) == 150 && allot(&b, 3) == 150, "level 1: 150 each");
    CHECK(allot(&b, 4) == 67 && allot(&b, 5) == 67 && allot(&b, 6) == 66, "level 2: 67, 67, 66");
    for (uint32_t i = 7; i <= 11; i++) CHECK(allot(&b, i) == 20, "level 3: 20 each");
    CHECK(total_allotted(&b) == 1100, "R5 sums exactly to T");
}

static void test_rounding_small_total(void)
{
    /* L = 4, T = 10, one model per level. Quotas 50/11, 30/11, 20/11, 10/11:
     * floors 4, 2, 1, 0 (sum 7); remainders 6, 8, 9, 10 (in elevenths) ->
     * the 3 leftover tokens go to d3, d2, d1. Result 4, 3, 2, 1. */
    swarm_budget_t b;
    swarm_budget_init(&b, 4, 10);
    for (uint32_t d = 0; d < 4; d++) swarm_budget_register(&b, 10 + d, d);
    swarm_budget_begin_cycle(&b);
    CHECK(allot(&b, 10) == 4 && allot(&b, 11) == 3 && allot(&b, 12) == 2 && allot(&b, 13) == 1,
          "largest remainder gives 4, 3, 2, 1");
    CHECK(total_allotted(&b) == 10, "R5 exact with rounding");
}

static void test_empty_levels_redistribute(void)
{
    /* L = 4, T = 600, only levels 0 and 3 active: weights 5 + 1 = 6 -> 500, 100. */
    swarm_budget_t b;
    swarm_budget_init(&b, 4, 600);
    swarm_budget_register(&b, 1, 0);
    swarm_budget_register(&b, 2, 1);
    swarm_budget_register(&b, 3, 3);
    swarm_budget_set_active(&b, 2, false);
    swarm_budget_begin_cycle(&b);
    CHECK(allot(&b, 1) == 500, "R3 conductor 500");
    CHECK(allot(&b, 2) == 0, "parked model gets nothing");
    CHECK(allot(&b, 3) == 100, "R3 level 3 gets 100");
    CHECK(total_allotted(&b) == 600, "no stranded tokens");
}

static void test_consume_and_expiry(void)
{
    swarm_budget_t b;
    swarm_budget_init(&b, 2, 300); /* L = 2: weights F(3) = 2, F(2) = 1 */
    swarm_budget_register(&b, 1, 0);
    swarm_budget_register(&b, 2, 1);
    uint64_t g = 0;
    CHECK(swarm_budget_consume(&b, 1, 5, &g) == SWARM_ERR_NO_CYCLE, "no spending outside a cycle");

    swarm_budget_begin_cycle(&b);
    CHECK(allot(&b, 1) == 200 && allot(&b, 2) == 100, "L2 split 200 / 100");
    CHECK(swarm_budget_consume(&b, 2, 60, &g) == SWARM_OK && g == 60, "grant within allotment");
    CHECK(swarm_budget_consume(&b, 2, 60, &g) == SWARM_OK && g == 40, "grant capped at allotment");
    CHECK(swarm_budget_remaining(&b, 2) == 0, "model 2 exhausted");
    CHECK(swarm_budget_consume(&b, 7, 1, &g) == SWARM_ERR_NO_MODEL, "unknown model rejected");
    swarm_budget_consume(&b, 1, 150, &g);
    CHECK(swarm_budget_end_cycle(&b) == SWARM_OK, "end cycle");
    CHECK(b.last_unused == 50, "R6 50 unused tokens reported");

    swarm_budget_begin_cycle(&b);
    CHECK(allot(&b, 1) == 200 && allot(&b, 2) == 100, "R6 no carry into the next cycle");
    CHECK(b.cycle == 2, "cycle counter");
}

static void test_args(void)
{
    swarm_budget_t b;
    CHECK(swarm_budget_init(0, 4, 10) == SWARM_ERR_ARG, "NULL budget");
    CHECK(swarm_budget_init(&b, 0, 10) == SWARM_ERR_ARG, "zero levels");
    CHECK(swarm_budget_init(&b, 9, 10) == SWARM_ERR_ARG, "too many levels");
    CHECK(swarm_budget_init(&b, 4, SWARM_MAX_TOKENS_PER_CYCLE + 1) == SWARM_ERR_ARG,
          "rate too large");
    swarm_budget_init(&b, 3, 10);
    CHECK(swarm_budget_register(&b, 1, 3) == SWARM_ERR_ARG, "level beyond L");
}

static void test_max_rate_no_overflow(void)
{
    /* L = 8, T = 2^48, one model per level: must still sum exactly to T. */
    swarm_budget_t b;
    swarm_budget_init(&b, 8, SWARM_MAX_TOKENS_PER_CYCLE);
    for (uint32_t d = 0; d < 8; d++) swarm_budget_register(&b, 100 + d, d);
    swarm_budget_begin_cycle(&b);
    CHECK(total_allotted(&b) == SWARM_MAX_TOKENS_PER_CYCLE, "exact at the maximum rate");
    CHECK(allot(&b, 100) > allot(&b, 101), "top level weighs most");
}

int main(void)
{
    printf("=== test_swarm_budget ===\n");
    test_fibonacci();
    test_capacity_and_weight();
    test_full_swarm_split();
    test_rounding_small_total();
    test_empty_levels_redistribute();
    test_consume_and_expiry();
    test_args();
    test_max_rate_no_overflow();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
