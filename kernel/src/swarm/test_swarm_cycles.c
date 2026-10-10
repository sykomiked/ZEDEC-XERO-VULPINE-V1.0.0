/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_cycles.c — known-answer tests for harmonic cycles, the Venn
 * overlap rule and the quality gate. Expected values are worked by hand. */
#include <stdio.h>
#include "swarm_budget.h"
#include "swarm_harmonic.h"
#include "swarm_overlap.h"
#include "swarm_quality.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_harmonics(void)
{
    const uint32_t period[11] = {27720, 13860, 9240, 6930, 5544, 4620,
                                 3960,  3465,  3080, 2772, 2520};
    for (uint32_t n = 1; n <= 11; n++) {
        CHECK(swarm_harmonic_period(n) == period[n - 1], "period 27720 / n");
        CHECK(swarm_harmonic_period(n) * n == 27720u, "every period is exact");
    }
    CHECK(swarm_harmonic_period(12) == 0 && swarm_harmonic_period(0) == 0, "only 1 .. 11");
    CHECK(swarm_harmonics_due(0) == 0x7FFu, "all eleven line up at tick 0");
    CHECK(swarm_harmonics_due(27720) == 0x7FFu, "and again at the fundamental");
    CHECK(swarm_harmonics_due(2520) == (1u << 10), "tick 2520: only the 11th");
    /* 13860 is a multiple of the periods of harmonics 2, 4, 6, 8, 10. */
    CHECK(swarm_harmonics_due(13860) == 0x2AAu, "tick 13860: the even harmonics");
    CHECK(swarm_harmonics_due(1) == 0, "tick 1: nothing due");
    CHECK(swarm_harmonics_due((uint64_t) 27720 * 1000003u) == 0x7FFu, "large ticks");
    CHECK(swarm_harmonic_band(11) == SWARM_BAND_REFLEX &&
              swarm_harmonic_band(9) == SWARM_BAND_REFLEX,
          "9-11 reflex");
    CHECK(swarm_harmonic_band(8) == SWARM_BAND_THOUGHT &&
              swarm_harmonic_band(4) == SWARM_BAND_THOUGHT,
          "4-8 thought");
    CHECK(swarm_harmonic_band(1) == SWARM_BAND_GROWTH &&
              swarm_harmonic_band(3) == SWARM_BAND_GROWTH,
          "1-3 growth");
}

static void test_overlap(void)
{
    /* L = 2, T = 2100, no market: 1400, 350, 350. */
    swarm_budget_t b;
    swarm_budget_init(&b, 2, 2100);
    swarm_budget_register(&b, 1, 0);
    swarm_budget_register(&b, 2, 1);
    swarm_budget_register(&b, 3, 1);
    swarm_budget_begin_cycle(&b);

    swarm_overlap_t o;
    swarm_overlap_init(&o);
    uint64_t k = swarm_overlap_key(5, 77, 99);
    uint64_t k2 = swarm_overlap_key(5, 77, 100);
    CHECK(k != k2, "different questions, different keys");
    CHECK(k == swarm_overlap_key(5, 77, 99), "keys are deterministic");
    int32_t j = swarm_overlap_request(&o, k, 1);
    CHECK(swarm_overlap_request(&o, k, 2) == j && swarm_overlap_request(&o, k, 3) == j,
          "V1 one shared job");
    CHECK(swarm_overlap_request(&o, k, 2) == j && o.job[j].num_sharers == 3,
          "joining twice is a no-op");
    int32_t j2 = swarm_overlap_request(&o, k2, 1);
    CHECK(j2 != j, "a different question is a different job");

    /* V3: cost 2000 -> shares 667, 667, 666; model 2 has only 350. */
    CHECK(swarm_overlap_settle(&o, &b, (uint32_t) j, 2000) == SWARM_ERR_FULL, "V3 unaffordable");
    CHECK(swarm_budget_remaining(&b, 1) == 1400, "V3 nobody charged");

    /* V2: cost 100 -> 34, 33, 33; 200 tokens saved. */
    CHECK(swarm_overlap_settle(&o, &b, (uint32_t) j, 100) == SWARM_OK, "V2 settle");
    CHECK(swarm_budget_remaining(&b, 1) == 1366 && swarm_budget_remaining(&b, 2) == 317 &&
              swarm_budget_remaining(&b, 3) == 317,
          "V2 each pays a third");
    CHECK(o.tokens_saved == 200, "V2 200 tokens not spent twice");
    CHECK(swarm_overlap_settle(&o, &b, (uint32_t) j, 100) == SWARM_ERR_ARG, "a job settles once");
    CHECK(swarm_overlap_request(&o, k, 1) != j, "a settled key opens a new job");
}

static void test_quality(void)
{
    CHECK(swarm_quality_passes(2000, 900), "2.0 x 0.9 = 1.8 passes");
    CHECK(!swarm_quality_passes(2000, 899), "just under fails");
    CHECK(!swarm_quality_passes(1700, 1000), "fully verified but too narrow fails");
    CHECK(!swarm_quality_passes(9000, 1001), "a share above 1 is rejected");
    CHECK(swarm_quality_max_passes(4) == 8, "Q2 F(6) = 8 passes for 4 levels");
    CHECK(swarm_quality_next(2000, 950, 0, 4) == SWARM_Q_PRESENT, "Q1 present");
    CHECK(swarm_quality_next(2000, 500, 7, 4) == SWARM_Q_REVISE, "Q2 revise");
    CHECK(swarm_quality_next(2000, 500, 8, 4) == SWARM_Q_PRESENT_FLAGGED, "Q3 flagged");

    swarm_market_t m;
    swarm_market_init(&m);
    swarm_market_join(&m, 1, 10);
    CHECK(swarm_quality_credit(&m, 1, SWARM_CAP_HUMAN, 40, 2000, 500) == SWARM_ERR_ARG,
          "Q4 rushed work earns nothing");
    CHECK(swarm_market_trader(&m, 1)->value_cycle == 0, "Q4 nothing credited");
    CHECK(swarm_quality_credit(&m, 1, SWARM_CAP_HUMAN, 40, 2000, 900) == SWARM_OK &&
              swarm_market_trader(&m, 1)->value_cycle == 40,
          "Q4 checked work earns");
}

int main(void)
{
    printf("=== test_swarm_cycles ===\n");
    test_harmonics();
    test_overlap();
    test_quality();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
