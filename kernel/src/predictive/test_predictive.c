/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_predictive.c — predictive_multi advances the open-system dynamics
 * (Q_t) and honours the cost trajectory; single-step bounds are ordered. */
#include <stdio.h>
#include "predictive_model.h"

static int failures;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static multi_prediction_t mp, mp_cost;

int main(void)
{
    predictive_config_t cfg;
    predictive_config_init(&cfg);
    m5_coords_t c = {0};
    c.r = SR_FROM_INT(2);
    c.ell = SR_FROM_INT(1);
    surplus_real_t u = SR_DIV(SR_ONE, SR_FROM_INT(2));

    predictive_multi(&mp, &c, u, SR_FROM_INT(10), NULL, NULL, 20, &cfg);
    CHECK(mp.num_steps == 20, "20 steps forecast");
    CHECK(SR_CMP(mp.sustainability_index, SR_ZERO) > 0,
          "stock Q grows under positive surplus (index > 0; it was stuck at 0)");
    CHECK(SR_CMP(mp.sustainability_index, SR_ONE) <= 0, "Q stays under its ceiling");

    surplus_real_t cost[20];
    for (int i = 0; i < 20; i++) cost[i] = SR_FROM_INT(1000);
    predictive_multi(&mp_cost, &c, u, SR_FROM_INT(10), NULL, cost, 20, &cfg);
    CHECK(SR_CMP(mp_cost.sustainability_index, mp.sustainability_index) < 0,
          "a large cost trajectory lowers the stock (cost is no longer ignored)");

    predictive_multi(&mp, &c, u, SR_FROM_INT(10), NULL, NULL, 1000, &cfg);
    CHECK(mp.num_steps == MAX_HORIZON, "horizon clamped to MAX_HORIZON");

    prediction_t p = predictive_count(&c, u, SR_FROM_INT(10), &cfg);
    CHECK(SR_CMP(p.lower_bound, p.predicted_count) <= 0 &&
              SR_CMP(p.predicted_count, p.upper_bound) <= 0,
          "lower <= central <= upper");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
