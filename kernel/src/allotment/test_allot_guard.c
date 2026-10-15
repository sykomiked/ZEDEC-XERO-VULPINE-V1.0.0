/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_allot_guard.c — host tests for allot_guard.h (ASan/UBSan). */
#include "allot_guard.h"
#include <stdio.h>
#include <string.h>

static unsigned g_pass, g_fail;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static int g_gate_calls;
static bool adult(void *ctx, const uint8_t owner[ALLOT_ID_LEN])
{
    (void) ctx;
    g_gate_calls++;
    return owner[0] != 0xEE; /* owner 0xEE.. is the "under age" test player */
}

#define DAY 86400u

int main(void)
{
    allot_config_t c;
    allot_config_default(&c);
    c.jurisdiction_permits = true;
    c.mode = ALLOT_MODE_PAID;
    allot_guard_policy_t pol;
    allot_guard_policy_default(&pol);
    CHECK(pol.confirm_above.num == 1 && pol.confirm_above.den == 10 && pol.cooling_off == DAY &&
              pol.require_limits && pol.age_gate == NULL,
          "protections on by default, no age gate = fail closed");

    uint8_t id[32];
    memset(id, 7, sizeof id);
    allot_player_t pl;
    allot_player_init(&pl, id);
    uint64_t fee = 0, now = 100 * DAY;
    allot_rate_t r1 = {1, 100}, r10 = {1, 10}, r25 = {1, 4}, r50 = {1, 2}, r60 = {3, 5},
                  out = {0, 1}, half_pct = {1, 200};

    /* G1: out by default. */
    CHECK(pl.rate.num == 0, "player starts opted out");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == ALLOT_ERR_NOT_IN,
          "not opted in");
    CHECK(allot_player_set_rate(&c, &pl, r60) == ALLOT_ERR_RATE, "60%% refused");
    CHECK(allot_player_set_rate(&c, &pl, half_pct) == ALLOT_ERR_RATE, "0.5%% refused");
    CHECK(allot_player_set_rate(&c, &pl, r1) == ALLOT_OK, "opt in at 1%%");

    /* G6: no gate -> refused; gate says no -> refused. */
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == ALLOT_ERR_AGE,
          "no age gate: refused");
    pol.age_gate = adult;
    allot_player_t kid;
    uint8_t kid_id[32];
    memset(kid_id, 0xEE, sizeof kid_id);
    allot_player_init(&kid, kid_id);
    allot_player_set_rate(&c, &kid, r1);
    allot_player_set_limits(&pol, &kid, 5000, 5000, now);
    CHECK(allot_guard_check(&pol, &c, &kid, 100000, now, 1, 0, &fee) == ALLOT_ERR_AGE,
          "gate refuses");

    /* G3: limits first. */
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == ALLOT_ERR_NO_LIMITS,
          "limits required");
    CHECK(allot_player_set_limits(&pol, &pl, 0, 10, now) == ALLOT_ERR_ARG, "zero limit refused");
    CHECK(allot_player_set_limits(&pol, &pl, 3000, 5000, now) == ALLOT_OK, "limits set");
    CHECK(pl.day_limit == 3000 && pl.epoch_limit == 5000, "first setting is immediate");

    /* Spend: 1% of 100,000 = 1,000 per entry; day limit 3,000. */
    for (int i = 0; i < 3; i++) {
        CHECK(allot_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == ALLOT_OK &&
                  fee == 1000,
              "entry %d ok", i);
        allot_guard_commit(&pol, &pl, now, 1, fee);
    }
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == ALLOT_ERR_DAY_LIMIT,
          "4th passes the day limit");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now + DAY, 1, 0, &fee) == ALLOT_OK,
          "next day ok");
    allot_guard_commit(&pol, &pl, now + DAY, 1, fee);
    allot_guard_commit(&pol, &pl, now + DAY, 1, fee); /* epoch 1 total now 5,000 */
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now + DAY, 1, 0, &fee) ==
              ALLOT_ERR_EPOCH_LIMIT,
          "epoch limit");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, now + DAY, 2, 0, &fee) == ALLOT_OK,
          "a new epoch resets");

    /* G4: lowering is immediate, raising waits a day. */
    uint64_t t = now + 2 * DAY;
    CHECK(allot_player_set_limits(&pol, &pl, 2000, 5000, t) == ALLOT_OK && pl.day_limit == 2000,
          "lowering immediate");
    allot_player_set_limits(&pol, &pl, 9000, 9000, t);
    CHECK(allot_player_day_limit(&pl, t) == 2000 &&
              allot_player_day_limit(&pl, t + DAY - 1) == 2000,
          "raise not in force during cooling-off");
    CHECK(allot_player_day_limit(&pl, t + DAY) == 9000 &&
              allot_player_epoch_limit(&pl, t + DAY) == 9000,
          "raise in force after cooling-off");
    allot_guard_commit(&pol, &pl, t, 3, 2000);
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t + 10, 3, 0, &fee) == ALLOT_ERR_DAY_LIMIT,
          "old limit holds while queued");
    allot_player_set_limits(&pol, &pl, 1000, 9000, t + 20);
    CHECK(allot_player_day_limit(&pl, t + 2 * DAY) == 1000, "lowering cancels a queued raise");

    /* G2: above 10%% the exact fee must be confirmed. */
    allot_player_set_limits(&pol, &pl, 1000000, 1000000, t);
    uint64_t t2 = t + 3 * DAY;
    CHECK(allot_player_set_rate(&c, &pl, r10) == ALLOT_OK, "10%%");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 0, &fee) == ALLOT_OK && fee == 10000,
          "10%% needs no confirmation");
    CHECK(allot_player_set_rate(&c, &pl, r25) == ALLOT_OK, "25%%");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 0, &fee) == ALLOT_ERR_CONFIRM &&
              fee == 25000,
          "25%% asks to confirm 25,000");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 24999, &fee) == ALLOT_ERR_CONFIRM,
          "a different amount is not a confirmation");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 25000, &fee) == ALLOT_OK,
          "exact amount confirmed");
    CHECK(allot_player_set_rate(&c, &pl, r50) == ALLOT_OK &&
              allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 50000, &fee) == ALLOT_OK &&
              fee == 50000,
          "50%% with confirmation");
    c.max_rate = r25;
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2, 9, 50000, &fee) == ALLOT_ERR_RATE,
          "a deployment that lowers its max refuses the old rate");
    c.max_rate = r50;

    /* G5: self-exclusion only extends. */
    allot_player_exclude(&pl, t2, 30 * DAY);
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2 + DAY, 9, 50000, &fee) ==
              ALLOT_ERR_EXCLUDED,
          "excluded");
    allot_player_exclude(&pl, t2, DAY); /* an attempt to shorten */
    CHECK(pl.excluded_until == t2 + 30 * DAY, "cannot shorten");
    CHECK(allot_guard_check(&pol, &c, &pl, 100000, t2 + 30 * DAY, 9, 50000, &fee) == ALLOT_OK,
          "ends on time");
    allot_player_exclude(&pl, t2, UINT64_MAX);
    CHECK(pl.excluded_until == UINT64_MAX, "permanent exclusion");

    /* G1: opting out is immediate. */
    allot_player_t p2;
    allot_player_init(&p2, id);
    allot_player_set_rate(&c, &p2, r1);
    CHECK(allot_player_set_rate(&c, &p2, out) == ALLOT_OK && p2.rate.num == 0, "opt out");
    CHECK(allot_guard_check(&pol, &c, &p2, 100000, t2, 9, 0, &fee) == ALLOT_ERR_NOT_IN,
          "out again");

    /* The jurisdiction switch still governs. */
    c.jurisdiction_permits = false;
    allot_player_set_rate(&c, &p2, r1);
    CHECK(allot_guard_check(&pol, &c, &p2, 100000, t2, 9, 0, &fee) == ALLOT_ERR_DISABLED,
          "switch off");
    CHECK(g_gate_calls > 0, "the gate was consulted");

    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
