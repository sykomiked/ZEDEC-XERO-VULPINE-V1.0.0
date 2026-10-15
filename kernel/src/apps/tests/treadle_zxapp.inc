/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Treadle's languages against the C its model runs (treadle_rules.h). */
#include "treadle_rules.h"

APP_TEST(treadle, 6, 24)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* next_run (TR1): Sutra = KRON = C++ = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = i % 4 ? wrand(1, 240) : wrand(1, 1440);
        c[i] = i % 3 ? wrand(0, 1439) : a[i] + (i % 2 && a[i] < 1439); /* on or by the start */
    }
    a[0] = 0, b[0] = 1, c[0] = 0;       /* every minute, at midnight */
    a[1] = 0, b[1] = 1440, c[1] = 1439; /* once a day: tomorrow's */
    a[2] = 480, b[2] = 60, c[2] = 0;    /* before its start */
    a[3] = 480, b[3] = 60, c[3] = 480;  /* on its start */
    a[4] = 480, b[4] = 60, c[4] = 481;  /* a minute past: the next hour */
    a[5] = 480, b[5] = 60, c[5] = 1380; /* on a run late in the day */
    a[6] = 480, b[6] = 60, c[6] = 1381; /* past the day's last run: tomorrow */
    a[7] = 1439, b[7] = 1, c[7] = 1439; /* the day's last minute */
    a[8] = 1439, b[8] = 1440, c[8] = 0; /* the largest start, the longest period */
    a[9] = 0, b[9] = 7, c[9] = 1434;    /* the day's last run of seven */
    a[10] = 0, b[10] = 7, c[10] = 1436; /* after it: tomorrow at 0 is 1440 */
    a[11] = 600, b[11] = 1000, c[11] = 601;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = treadle_next_run(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 1440 && want[2] == 480 && want[3] == 480 && want[4] == 540 &&
              want[5] == 1380 && want[6] == 1920 && want[7] == 1439 && want[8] == 1439 &&
              want[9] == 1435 && want[10] == 1440 && want[11] == 2040 &&
              treadle_next_run(0, 1, 1440) == -1 && treadle_next_run(1440, 1, 0) == -1 &&
              treadle_next_run(0, 0, 0) == -1 && treadle_next_run(0, 1441, 0) == -1 &&
              treadle_next_run(-1, 1, 0) == -1 && treadle_next_run(0, 1, -1) == -1,
          "Treadle: the C rule's own edges (TR1)");
    zxapp_arg_t na[3] = {{"start", a, WAVE_ROWS}, {"period", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "next_run", na, 3, want, WAVE_ROWS, false),
          "Treadle: a task's next run, Sutra = KRON = C++ = C");
    {
        static const int64_t ns[2] = {480, 480}, np[2] = {60, 60}, nn[2] = {481, 1440};
        zxapp_arg_t nb[3] = {{"start", ns, 2}, {"period", np, 2}, {"now", nn, 2}};
        CHECK(wave_lp(app, "next_run", nb, 3) == ZXAPP_BOTH,
              "Treadle: a minute past the day: C++ refuses, Sutra and KRON answer: BOTH, held");
        static const int64_t gs[2] = {480, 480}, gp[2] = {60, 60}, gn[2] = {481, -1};
        zxapp_arg_t gb[3] = {{"start", gs, 2}, {"period", gp, 2}, {"now", gn, 2}};
        CHECK(wave_lp(app, "next_run", gb, 3) == ZXAPP_BOTH,
              "Treadle: a negative minute: C++ and Sutra refuse, KRON answers: BOTH, held");
        static const int64_t ms[2] = {480, 480}, mp[2] = {60, 0}, mn[2] = {481, 481};
        zxapp_arg_t mb[3] = {{"start", ms, 2}, {"period", mp, 2}, {"now", mn, 2}};
        CHECK(wave_lp(app, "next_run", mb, 3) == ZXAPP_NEITHER,
              "Treadle: a period of 0: every language refuses: NEITHER, nothing runs");
    }

    /* budget_ok (TR2): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5 ? wrand(0, 999999999) : wrand(-2, 2);
        b[i] = i % 3 ? wrand(-1000, 999999999) : wrand(-2, 2);
    }
    a[0] = 0, b[0] = 0;                 /* nothing spent, nothing left: fits */
    a[1] = 999999999, b[1] = 999999999; /* the largest each */
    a[2] = 10, b[2] = -1;               /* one token over the cap */
    a[3] = -1, b[3] = 0;                /* a negative cost */
    a[4] = 500, b[4] = 0;               /* exactly the cap */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(treadle_budget_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_TRUE &&
              !treadle_budget_ok(1000000000, 0) && !treadle_budget_ok(0, 1000000000),
          "Treadle: the C rule's own edges (TR2)");
    zxapp_arg_t ba[2] = {{"cost", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "budget_ok", ba, 2, want, WAVE_ROWS, true),
          "Treadle: a run within the owner's budget, Eve = KRON = C");
    {
        static const int64_t bc[2] = {10, 1000000000}, bl[2] = {0, 0};
        zxapp_arg_t bb[2] = {{"cost", bc, 2}, {"left", bl, 2}};
        CHECK(wave_lp(app, "budget_ok", bb, 2) == ZXAPP_BOTH,
              "Treadle: a cost past nine digits: Eve refuses, KRON answers: BOTH, held");
        static const int64_t lc[2] = {10, 10}, ll[2] = {0, 1000000000};
        zxapp_arg_t lb[2] = {{"cost", lc, 2}, {"left", ll, 2}};
        CHECK(wave_lp(app, "budget_ok", lb, 2) == ZXAPP_BOTH,
              "Treadle: a budget left past nine digits: Eve refuses, KRON answers: BOTH, held");
    }

    /* task_ok (TR3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-10000, 10000);
        d[i] = wrand(-10000, 10000);
        e[i] = i % 2 ? wrand(-999999999, 999999999) : wrand(-1000000001, 1000000001);
        f[i] = wrand(-10, 10);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;
    a[1] = 999, b[1] = 9, c[1] = 9999, d[1] = 9999, e[1] = 999999999, f[1] = 9;
    a[2] = 1000, b[2] = 9, c[2] = 9999, d[2] = 9999, e[2] = 999999999, f[2] = 9;
    a[3] = 999, b[3] = 10, c[3] = 9999, d[3] = 9999, e[3] = 999999999, f[3] = 9;
    a[4] = 999, b[4] = 9, c[4] = 10000, d[4] = 9999, e[4] = 999999999, f[4] = 9;
    a[5] = 999, b[5] = 9, c[5] = 9999, d[5] = 10000, e[5] = 999999999, f[5] = 9;
    a[6] = 999, b[6] = 9, c[6] = 9999, d[6] = 9999, e[6] = 1000000000, f[6] = 9;
    a[7] = 999, b[7] = 9, c[7] = 9999, d[7] = 9999, e[7] = 999999999, f[7] = 10;
    a[8] = -999, b[8] = -9, c[8] = -9999, d[8] = -9999, e[8] = -999999999, f[8] = -9;
    a[9] = 1, b[9] = 0, c[9] = 480, d[9] = 60, e[9] = 500, f[9] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(treadle_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[3] == ZXAPP_FALSE &&
              want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE &&
              want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE && want[9] == ZXAPP_TRUE,
          "Treadle: the C rule's own edges (TR3)");
    zxapp_arg_t ta[6] = {{"task", a, WAVE_ROWS},  {"kind", b, WAVE_ROWS},
                         {"start", c, WAVE_ROWS}, {"period", d, WAVE_ROWS},
                         {"cost", e, WAVE_ROWS},  {"approved", f, WAVE_ROWS}};
    CHECK(wave_same(app, "task_ok", ta, 6, want, WAVE_ROWS, true),
          "Treadle: a task makes a ^ZXTREADLE record, MYRA = C");
}
