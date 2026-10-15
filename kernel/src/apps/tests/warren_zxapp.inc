/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warren's languages against the C its model runs (warren_rules.h).
 * Slots: @logic.warren_change_ok, @logic.warren_approval_ok,
 * @num.warren_backoff, @logic.warren_record_ok, and each consumer's share
 * of the energy through the Banker, @num.banker_share. */
#include "warren_rules.h"

APP_TEST(warren, 11, 20)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* change_ok (WR2): Sutra = KRON = C, every flag pattern, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 16 ? (i >> 3) & 1 : wrand(0, 1);
        b[i] = i < 16 ? (i >> 2) & 1 : wrand(0, 1);
        c[i] = i < 16 ? (i >> 1) & 1 : wrand(0, 1);
        d[i] = i < 16 ? i & 1 : wrand(0, 1);
        want[i] = T_(warren_change_ok(a[i], b[i], c[i], d[i]));
    }
    CHECK(warren_change_ok(1, 0, 0, 1) && warren_change_ok(1, 1, 0, 0) &&
              warren_change_ok(1, 1, 1, 1) && !warren_change_ok(1, 1, 0, 1) &&
              !warren_change_ok(0, 0, 1, 1) && !warren_change_ok(2, 0, 0, 1) &&
              !warren_change_ok(1, 1, 2, 1) && !warren_change_ok(1, -1, 0, 1),
          "Warren: the C rule's own edges (WR2)");
    zxapp_arg_t ca[4] = {{"owner", a, WAVE_ROWS},
                         {"regulated", b, WAVE_ROWS},
                         {"approved", c, WAVE_ROWS},
                         {"enable", d, WAVE_ROWS}};
    CHECK(wave_same(app, "change_ok", ca, 4, want, WAVE_ROWS, true),
          "Warren: change_ok, Sutra = KRON = C");
    {
        /* a flag that is no flag: Sutra reads 2 as no, KRON as yes; -1 Sutra refuses */
        static const int64_t one[2] = {1, 1}, zero[2] = {0, 0}, two[2] = {1, 2}, neg[2] = {1, -1};
        zxapp_arg_t ta[4] = {
            {"owner", two, 2}, {"regulated", zero, 2}, {"approved", zero, 2}, {"enable", one, 2}};
        CHECK(wave_lp(app, "change_ok", ta, 4) == ZXAPP_BOTH,
              "Warren: owner 2: Sutra FALSE, KRON TRUE: BOTH");
        zxapp_arg_t na[4] = {
            {"owner", one, 2}, {"regulated", one, 2}, {"approved", neg, 2}, {"enable", one, 2}};
        CHECK(wave_lp(app, "change_ok", na, 4) == ZXAPP_BOTH,
              "Warren: approved -1: Sutra refuses, KRON answers: BOTH");
    }

    /* approval_ok (WR3): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (i >> 1) & 1 : wrand(0, 1);
        b[i] = i < 4 ? i & 1 : wrand(0, 1);
        want[i] = T_(warren_approval_ok(a[i], b[i]));
    }
    CHECK(warren_approval_ok(1, 1) && !warren_approval_ok(1, 0) && !warren_approval_ok(0, 1) &&
              !warren_approval_ok(2, 1) && !warren_approval_ok(1, -1),
          "Warren: the C rule's own edges (WR3)");
    zxapp_arg_t pa[2] = {{"owner", a, WAVE_ROWS}, {"approved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "approval_ok", pa, 2, want, WAVE_ROWS, true),
          "Warren: approval_ok, Eve = KRON = C");
    {
        /* approval 2 is no flag: Eve's record refuses it, KRON reads above 0 as yes */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2};
        zxapp_arg_t ta[2] = {{"owner", one, 2}, {"approved", two, 2}};
        CHECK(wave_lp(app, "approval_ok", ta, 2) == ZXAPP_BOTH,
              "Warren: approved 2: Eve FALSE, KRON TRUE: BOTH");
    }

    /* backoff (WR4): BASIC (Hatchling) = KRON = C, a whole schedule per call, 64 calls */
    static const int64_t EDGE[][3] = {
        {1, 1, 1},  {32, 1, 86400}, {32, 3600, 86400}, {8, 2, 60},    {5, 2, 3},
        {32, 1, 1}, {1, 3600, 1},   {20, 1, 86400},    {32, 3600, 1}, {17, 7, 86400},
    };
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t t[1], s[1], m[1];
        if (r < sizeof EDGE / sizeof EDGE[0]) {
            t[0] = EDGE[r][0], s[0] = EDGE[r][1], m[0] = EDGE[r][2];
        } else {
            t[0] = wrand(1, 32), s[0] = wrand(1, 3600), m[0] = wrand(1, 86400);
        }
        for (int64_t k = 1; k <= t[0]; k++) want[k - 1] = warren_backoff(k, t[0], s[0], m[0]);
        zxapp_arg_t ba[3] = {{"tries", t, 1}, {"base", s, 1}, {"most", m, 1}};
        if (!wave_same(app, "backoff", ba, 3, want, (uint32_t) t[0], false)) {
            all = false;
            printf("    backoff tries %lld base %lld most %lld\n", (long long) t[0],
                   (long long) s[0], (long long) m[0]);
        }
    }
    CHECK(all, "Warren: backoff, BASIC = KRON = C, 64 schedules");
    CHECK(warren_backoff(1, 8, 2, 60) == 2 && warren_backoff(5, 8, 2, 60) == 32 &&
              warren_backoff(6, 8, 2, 60) == 60 && warren_backoff(8, 8, 2, 60) == 60 &&
              warren_backoff(32, 32, 3600, 86400) == 86400 &&
              warren_backoff(9, 8, 2, 60) == WARREN_REFUSED &&
              warren_backoff(0, 8, 2, 60) == WARREN_REFUSED &&
              warren_backoff(1, 33, 2, 60) == WARREN_REFUSED &&
              warren_backoff(1, 0, 2, 60) == WARREN_REFUSED &&
              warren_backoff(1, 8, 0, 60) == WARREN_REFUSED &&
              warren_backoff(1, 8, 3601, 86400) == WARREN_REFUSED &&
              warren_backoff(1, 8, 2, 86401) == WARREN_REFUSED &&
              warren_backoff(1, 8, 2, 0) == WARREN_REFUSED &&
              warren_backoff(1, 8, 2, INT64_MAX) == WARREN_REFUSED,
          "Warren: the C rule's own edges (WR4)");
    {
        /* past the domain BASIC refuses, KRON answers: held */
        static const int64_t t33[1] = {33}, t8[1] = {8}, b2[1] = {2}, b0[1] = {0}, m60[1] = {60},
                             m86401[1] = {86401};
        zxapp_arg_t xa[3] = {{"tries", t33, 1}, {"base", b2, 1}, {"most", m60, 1}};
        CHECK(wave_lp(app, "backoff", xa, 3) == ZXAPP_BOTH,
              "Warren: 33 tries: BASIC refuses, KRON answers: BOTH");
        zxapp_arg_t za[3] = {{"tries", t8, 1}, {"base", b0, 1}, {"most", m60, 1}};
        CHECK(wave_lp(app, "backoff", za, 3) == ZXAPP_BOTH,
              "Warren: base 0: BASIC refuses, KRON answers: BOTH");
        zxapp_arg_t ma[3] = {{"tries", t8, 1}, {"base", b2, 1}, {"most", m86401, 1}};
        CHECK(wave_lp(app, "backoff", ma, 3) == ZXAPP_BOTH,
              "Warren: most past a day: BASIC refuses, KRON answers: BOTH");
    }

    /* record_ok (WR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = wrand(-12, 12);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-1200, 1200);
        e[i] = wrand(-1200000000000000, 1200000000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 99, b[1] = 9, c[1] = 9, d[1] = 999, e[1] = 999999999999999;
    a[2] = 100, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1;
    a[3] = 1, b[3] = 10, c[3] = 1, d[3] = 1, e[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1, e[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 1000, e[5] = 1;
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = 1000000000000000;
    a[7] = -99, b[7] = -9, c[7] = -9, d[7] = -999, e[7] = -999999999999999;
    a[8] = 3, b[8] = 1, c[8] = 1, d[8] = 2, e[8] = 1700000000000;
    a[9] = INT64_MAX, b[9] = 1, c[9] = 1, d[9] = 1, e[9] = INT64_MIN;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(warren_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE &&
              want[7] == ZXAPP_TRUE && want[8] == ZXAPP_TRUE,
          "Warren: the C rule's own edges (WR5)");
    zxapp_arg_t ra[5] = {{"engine", a, WAVE_ROWS},
                         {"enabled", b, WAVE_ROWS},
                         {"regulated", c, WAVE_ROWS},
                         {"restarts", d, WAVE_ROWS},
                         {"changed", e, WAVE_ROWS}};
    CHECK(wave_same(app, "record_ok", ra, 5, want, WAVE_ROWS, true),
          "Warren: an engine makes a ^ZXWARREN record, MYRA = C");

    /* banker.share, used for each consumer's energy: KRON = C++ = the model's C */
    {
        static int64_t whole[1];
        int64_t sum = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = i ? wrand(0, 99999999) : 0; /* microjoules; one consumer drew none */
            sum += a[i];
        }
        whole[0] = sum;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = warren_share_x100(a[i], sum);
        zxapp_arg_t sa[2] = {{"size", a, WAVE_ROWS}, {"total", whole, 1}};
        CHECK(want[0] == 0 && want[1] > 0 && want[1] < 10000 &&
                  wave_same(app, "banker.share", sa, 2, want, WAVE_ROWS, false),
              "Warren: each consumer's share, through banker.share");
        CHECK(warren_share_x100(5, 0) == WARREN_REFUSED && warren_share_x100(1, 0) == 10000 &&
                  warren_share_x100(0, 0) == 0 && warren_share_x100(-1, 10) == WARREN_REFUSED &&
                  warren_share_x100(11, 10) == WARREN_REFUSED &&
                  warren_share_x100(INT64_MAX, INT64_MAX) == WARREN_REFUSED &&
                  warren_share_x100(1, 3) == 3333,
              "Warren: the C share's own edges");
    }
}
