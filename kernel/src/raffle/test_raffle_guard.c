/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_raffle_guard.c — host tests for raffle_guard.h (ASan/UBSan). */
#include "raffle_guard.h"
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
static bool adult(void *ctx, const uint8_t owner[RAFFLE_ID_LEN])
{
    (void) ctx;
    g_gate_calls++;
    return owner[0] != 0xEE; /* owner 0xEE.. is the "under age" test player */
}

#define DAY 86400u

int main(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    c.mode = RAFFLE_MODE_PAID;
    raffle_guard_policy_t pol;
    raffle_guard_policy_default(&pol);
    CHECK(pol.confirm_above.num == 1 && pol.confirm_above.den == 10 && pol.cooling_off == DAY &&
              pol.require_limits && pol.age_gate == NULL,
          "protections on by default, no age gate = fail closed");

    uint8_t id[32];
    memset(id, 7, sizeof id);
    raffle_player_t pl;
    raffle_player_init(&pl, id);
    uint64_t fee = 0, now = 100 * DAY;
    raffle_rate_t r1 = {1, 100}, r10 = {1, 10}, r25 = {1, 4}, r50 = {1, 2}, r60 = {3, 5},
                  out = {0, 1}, half_pct = {1, 200};

    /* G1: out by default. */
    CHECK(pl.rate.num == 0, "player starts opted out");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == RAFFLE_ERR_NOT_IN,
          "not opted in");
    CHECK(raffle_player_set_rate(&c, &pl, r60) == RAFFLE_ERR_RATE, "60%% refused");
    CHECK(raffle_player_set_rate(&c, &pl, half_pct) == RAFFLE_ERR_RATE, "0.5%% refused");
    CHECK(raffle_player_set_rate(&c, &pl, r1) == RAFFLE_OK, "opt in at 1%%");

    /* G6: no gate -> refused; gate says no -> refused. */
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == RAFFLE_ERR_AGE,
          "no age gate: refused");
    pol.age_gate = adult;
    raffle_player_t kid;
    uint8_t kid_id[32];
    memset(kid_id, 0xEE, sizeof kid_id);
    raffle_player_init(&kid, kid_id);
    raffle_player_set_rate(&c, &kid, r1);
    raffle_player_set_limits(&pol, &kid, 5000, 5000, now);
    CHECK(raffle_guard_check(&pol, &c, &kid, 100000, now, 1, 0, &fee) == RAFFLE_ERR_AGE,
          "gate refuses");

    /* G3: limits first. */
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == RAFFLE_ERR_NO_LIMITS,
          "limits required");
    CHECK(raffle_player_set_limits(&pol, &pl, 0, 10, now) == RAFFLE_ERR_ARG, "zero limit refused");
    CHECK(raffle_player_set_limits(&pol, &pl, 3000, 5000, now) == RAFFLE_OK, "limits set");
    CHECK(pl.day_limit == 3000 && pl.epoch_limit == 5000, "first setting is immediate");

    /* Spend: 1% of 100,000 = 1,000 per entry; day limit 3,000. */
    for (int i = 0; i < 3; i++) {
        CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == RAFFLE_OK &&
                  fee == 1000,
              "entry %d ok", i);
        raffle_guard_commit(&pol, &pl, now, 1, fee);
    }
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now, 1, 0, &fee) == RAFFLE_ERR_DAY_LIMIT,
          "4th passes the day limit");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now + DAY, 1, 0, &fee) == RAFFLE_OK,
          "next day ok");
    raffle_guard_commit(&pol, &pl, now + DAY, 1, fee);
    raffle_guard_commit(&pol, &pl, now + DAY, 1, fee); /* epoch 1 total now 5,000 */
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now + DAY, 1, 0, &fee) ==
              RAFFLE_ERR_EPOCH_LIMIT,
          "epoch limit");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, now + DAY, 2, 0, &fee) == RAFFLE_OK,
          "a new epoch resets");

    /* G4: lowering is immediate, raising waits a day. */
    uint64_t t = now + 2 * DAY;
    CHECK(raffle_player_set_limits(&pol, &pl, 2000, 5000, t) == RAFFLE_OK && pl.day_limit == 2000,
          "lowering immediate");
    raffle_player_set_limits(&pol, &pl, 9000, 9000, t);
    CHECK(raffle_player_day_limit(&pl, t) == 2000 &&
              raffle_player_day_limit(&pl, t + DAY - 1) == 2000,
          "raise not in force during cooling-off");
    CHECK(raffle_player_day_limit(&pl, t + DAY) == 9000 &&
              raffle_player_epoch_limit(&pl, t + DAY) == 9000,
          "raise in force after cooling-off");
    raffle_guard_commit(&pol, &pl, t, 3, 2000);
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t + 10, 3, 0, &fee) == RAFFLE_ERR_DAY_LIMIT,
          "old limit holds while queued");
    raffle_player_set_limits(&pol, &pl, 1000, 9000, t + 20);
    CHECK(raffle_player_day_limit(&pl, t + 2 * DAY) == 1000, "lowering cancels a queued raise");

    /* G2: above 10%% the exact fee must be confirmed. */
    raffle_player_set_limits(&pol, &pl, 1000000, 1000000, t);
    uint64_t t2 = t + 3 * DAY;
    CHECK(raffle_player_set_rate(&c, &pl, r10) == RAFFLE_OK, "10%%");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 0, &fee) == RAFFLE_OK && fee == 10000,
          "10%% needs no confirmation");
    CHECK(raffle_player_set_rate(&c, &pl, r25) == RAFFLE_OK, "25%%");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 0, &fee) == RAFFLE_ERR_CONFIRM &&
              fee == 25000,
          "25%% asks to confirm 25,000");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 24999, &fee) == RAFFLE_ERR_CONFIRM,
          "a different amount is not a confirmation");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 25000, &fee) == RAFFLE_OK,
          "exact amount confirmed");
    CHECK(raffle_player_set_rate(&c, &pl, r50) == RAFFLE_OK &&
              raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 50000, &fee) == RAFFLE_OK &&
              fee == 50000,
          "50%% with confirmation");
    c.max_rate = r25;
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2, 9, 50000, &fee) == RAFFLE_ERR_RATE,
          "a deployment that lowers its max refuses the old rate");
    c.max_rate = r50;

    /* G5: self-exclusion only extends. */
    raffle_player_exclude(&pl, t2, 30 * DAY);
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2 + DAY, 9, 50000, &fee) ==
              RAFFLE_ERR_EXCLUDED,
          "excluded");
    raffle_player_exclude(&pl, t2, DAY); /* an attempt to shorten */
    CHECK(pl.excluded_until == t2 + 30 * DAY, "cannot shorten");
    CHECK(raffle_guard_check(&pol, &c, &pl, 100000, t2 + 30 * DAY, 9, 50000, &fee) == RAFFLE_OK,
          "ends on time");
    raffle_player_exclude(&pl, t2, UINT64_MAX);
    CHECK(pl.excluded_until == UINT64_MAX, "permanent exclusion");

    /* G1: opting out is immediate. */
    raffle_player_t p2;
    raffle_player_init(&p2, id);
    raffle_player_set_rate(&c, &p2, r1);
    CHECK(raffle_player_set_rate(&c, &p2, out) == RAFFLE_OK && p2.rate.num == 0, "opt out");
    CHECK(raffle_guard_check(&pol, &c, &p2, 100000, t2, 9, 0, &fee) == RAFFLE_ERR_NOT_IN,
          "out again");

    /* The jurisdiction switch still governs. */
    c.jurisdiction_permits = false;
    raffle_player_set_rate(&c, &p2, r1);
    CHECK(raffle_guard_check(&pol, &c, &p2, 100000, t2, 9, 0, &fee) == RAFFLE_ERR_DISABLED,
          "switch off");
    CHECK(g_gate_calls > 0, "the gate was consulted");

    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
