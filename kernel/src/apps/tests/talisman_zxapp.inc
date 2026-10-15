/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talisman's languages against the C its model runs (talisman_rules.h).
 * Slots: @num.talisman_reach_x100, @logic.talisman_earned,
 * @num.talisman_earned_n. */
#include "talisman_rules.h"

APP_TEST(talisman, 6, 76)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* reach_x100 (TL1): Sutra = Trebago = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, i < 32 ? 100000 : TALISMAN_VALUE_MAX);
        a[i] = i % 3 ? wrand(0, b[i]) : wrand(0, TALISMAN_VALUE_MAX);
    }
    a[0] = 0, b[0] = 1;                                   /* nothing yet */
    a[1] = TALISMAN_VALUE_MAX, b[1] = TALISMAN_VALUE_MAX; /* every max: met */
    a[2] = 1, b[2] = TALISMAN_VALUE_MAX;                  /* the least of the most: 0 */
    a[3] = TALISMAN_VALUE_MAX, b[3] = 1;                  /* far past: held at 10000 */
    a[4] = 1, b[4] = 3;                                   /* 33.33.. floored */
    a[5] = 2, b[5] = 3;                                   /* 66.66.. floored, never rounded up */
    a[6] = 99, b[6] = 100;                                /* one short */
    a[7] = 100, b[7] = 100;                               /* exactly met */
    a[8] = 101, b[8] = 100;                               /* one past: held */
    a[9] = 50, b[9] = 100;                                /* the Bronze card's credibility */
    a[10] = 10001, b[10] = 10000;                         /* 10001 before the hold: 10000 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = talisman_reach_x100(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 10000 && want[2] == 0 && want[3] == 10000 && want[4] == 3333 &&
              want[5] == 6666 && want[6] == 9900 && want[7] == 10000 && want[8] == 10000 &&
              want[9] == 5000 && want[10] == 10000,
          "Talisman: the C rule's own edges (TL1)");
    CHECK(talisman_reach_x100(1, 0) == TALISMAN_REFUSED &&
              talisman_reach_x100(-1, 5) == TALISMAN_REFUSED &&
              talisman_reach_x100(TALISMAN_VALUE_MAX + 1, 5) == TALISMAN_REFUSED &&
              talisman_reach_x100(5, TALISMAN_VALUE_MAX + 1) == TALISMAN_REFUSED &&
              talisman_reach_x100(5, -1) == TALISMAN_REFUSED,
          "Talisman: the C refuses outside 9(9) and a need of 0");
    zxapp_arg_t ra[2] = {{"have", a, WAVE_ROWS}, {"need", b, WAVE_ROWS}};
    CHECK(wave_same(app, "reach_x100", ra, 2, want, WAVE_ROWS, false),
          "Talisman: reach_x100, Sutra = Trebago = C");
    {
        /* outside 9(9), or a need of 0: Sutra refuses, Trebago answers */
        static const int64_t h[5][2] = {{1, 1}, {1, 1000000000}, {1, -1}, {1, 1}, {1, 1}};
        static const int64_t n[5][2] = {{1, 0}, {1, 1}, {1, 1}, {1, -1}, {1, 1000000000}};
        static const char *const why[5] = {"a need of 0", "have max + 1", "a negative have",
                                           "a negative need", "need max + 1"};
        for (uint32_t e = 0; e < 5; e++) {
            zxapp_arg_t xa[2] = {{"have", h[e], 2}, {"need", n[e], 2}};
            CHECK(wave_lp(app, "reach_x100", xa, 2) == ZXAPP_BOTH,
                  "Talisman: reach_x100 with %s: Sutra refuses, Trebago answers: BOTH", why[e]);
        }
    }

    /* earned (TL2) and earned_n (TL3): margins, Eve = KRON = C and Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4 ? wrand(0, 1000000) : wrand(-1000000, 1000000);
        b[i] = i % 5 ? wrand(0, 10000000) : wrand(-10000000, 10000000);
        c[i] = i % 3 ? wrand(0, TALISMAN_BPS_MAX) : wrand(-TALISMAN_BPS_MAX, TALISMAN_BPS_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                                         /* each just met */
    a[1] = TALISMAN_MARGIN_MAX, b[1] = TALISMAN_MARGIN_MAX, c[1] = 10000; /* every max */
    a[2] = -1, b[2] = 0, c[2] = 0;                                        /* credibility short */
    a[3] = 0, b[3] = -1, c[3] = 0;                                        /* bond short */
    a[4] = 0, b[4] = 0, c[4] = -1;                                        /* uptime short */
    a[5] = -1, b[5] = -1, c[5] = -1;                                      /* every input missing */
    a[6] = -TALISMAN_MARGIN_MAX, b[6] = 5, c[6] = 5;                      /* the widest short */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(talisman_earned(a[i], b[i], c[i]));
    CHECK(want[0] == T_(true) && want[1] == T_(true) && want[2] == T_(false) &&
              want[5] == T_(false),
          "Talisman: the C rule's own edges (TL2)");
    zxapp_arg_t ea[3] = {{"cred", a, WAVE_ROWS}, {"bond", b, WAVE_ROWS}, {"uptime", c, WAVE_ROWS}};
    CHECK(wave_same(app, "earned", ea, 3, want, WAVE_ROWS, true),
          "Talisman: earned, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = talisman_earned_n(a[i], b[i], c[i]);
    CHECK(wave_same(app, "earned_n", ea, 3, want, WAVE_ROWS, false),
          "Talisman: earned_n, Trebago = KRON = C");
    {
        /* past the ranges (max + 1): Eve's record and Trebago's bounds refuse
         * the tier, KRON reads any margin at or above 0 as met: BOTH */
        static const int64_t m0[2] = {0, 0}, up[2] = {0, TALISMAN_BPS_MAX + 1},
                             big[2] = {0, TALISMAN_MARGIN_MAX + 1};
        zxapp_arg_t u1[3] = {{"cred", m0, 2}, {"bond", m0, 2}, {"uptime", up, 2}};
        zxapp_arg_t u2[3] = {{"cred", big, 2}, {"bond", m0, 2}, {"uptime", m0, 2}};
        zxapp_arg_t u3[3] = {{"cred", m0, 2}, {"bond", big, 2}, {"uptime", m0, 2}};
        CHECK(!talisman_earned(0, 0, TALISMAN_BPS_MAX + 1) &&
                  !talisman_earned(TALISMAN_MARGIN_MAX + 1, 0, 0) &&
                  !talisman_earned(0, TALISMAN_MARGIN_MAX + 1, 0),
              "Talisman: the C refuses past the ranges too");
        CHECK(wave_lp(app, "earned", u1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "earned_n", u1, 3) == ZXAPP_BOTH,
              "Talisman: an uptime margin past 10000 bps: held (BOTH)");
        CHECK(wave_lp(app, "earned", u2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "earned_n", u2, 3) == ZXAPP_BOTH,
              "Talisman: a credibility margin past 18 digits: held (BOTH)");
        CHECK(wave_lp(app, "earned", u3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "earned_n", u3, 3) == ZXAPP_BOTH,
              "Talisman: a bond margin past 18 digits: held (BOTH)");
    }
}
