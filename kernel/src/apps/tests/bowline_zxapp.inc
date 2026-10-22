/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bowline's languages against the C its model runs (bowline_rules.h).
 * Slots: @num.bowline_unskipped_day, @logic.bowline_overdue,
 * @logic.bowline_task_record_ok, and what it uses:
 * @num.solstice_next_occurrence and @num.witness_kind_count. */
#include "bowline_rules.h"

/* One Lingua call per row (zxapp_lingua.c L1): BASIC and KRON against the C. */
static bool bow_same(zxapp_t *app, int64_t day, int64_t skip)
{
    int64_t dv[1] = {day}, sv[1] = {skip}, w[1] = {bowline_unskipped(day, skip)};
    zxapp_arg_t a[2] = {{"day", dv, 1}, {"skip", sv, 1}};
    if (w[0] >= 0 && wave_same(app, "unskipped_day", a, 2, w, 1, false)) return true;
    printf("    unskipped_day %lld %lld\n", (long long) day, (long long) skip);
    return false;
}

static int32_t bow_lp(zxapp_t *app, int64_t day, int64_t skip)
{
    int64_t dv[1] = {day}, sv[1] = {skip};
    zxapp_arg_t a[2] = {{"day", dv, 1}, {"skip", sv, 1}};
    return wave_lp(app, "unskipped_day", a, 2);
}

APP_TEST(bowline, 21, 45)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], h[WAVE_ROWS], want[WAVE_ROWS];

    /* the C's own rule (BW1), every day of four weeks with every mask */
    bool rule = true;
    for (int64_t day = 0; day < 28 && rule; day++)
        for (int64_t skip = 0; skip <= BOWLINE_SKIP_MAX && rule; skip++) {
            int64_t u = bowline_unskipped(day, skip);
            rule = u >= day && u - day < 7 && ((skip >> bowline_weekday0(u)) & 1) == 0;
            for (int64_t k = day; k < u && rule; k++)
                rule = ((skip >> bowline_weekday0(k)) & 1) == 1;
            if (!rule) printf("    day %lld skip %lld\n", (long long) day, (long long) skip);
        }
    CHECK(rule, "Bowline: unskipped_day is the first day not skipped, 28 days by 127 masks");
    CHECK(bowline_unskipped(0, 0) == 0 && bowline_unskipped(0, 8) == 1 &&
              bowline_unskipped(2, 96) == 4 && bowline_unskipped(0, 126) == 4 &&
              bowline_unskipped(SOLSTICE_DAY_MAX, 0) == SOLSTICE_DAY_MAX &&
              bowline_unskipped(SOLSTICE_DAY_MAX, 16) == BOWLINE_REFUSED &&
              bowline_unskipped(0, 127) == BOWLINE_REFUSED &&
              bowline_unskipped(-1, 0) == BOWLINE_REFUSED &&
              bowline_unskipped(SOLSTICE_DAY_MAX + 1, 0) == BOWLINE_REFUSED &&
              bowline_unskipped(0, -1) == BOWLINE_REFUSED,
          "Bowline: unskipped_day, the C's own edges");

    /* unskipped_day (BW1): BASIC = KRON = C, one row per call, 64 calls */
    static const int64_t DS[][2] = {{0, 0},
                                    {0, 8},
                                    {2, 96},
                                    {3, 96},
                                    {0, 126},
                                    {20750, 31},
                                    {20751, 96},
                                    {SOLSTICE_DAY_MAX, 0},
                                    {SOLSTICE_DAY_MAX - 6, 126}};
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t day = r < sizeof DS / sizeof DS[0] ? DS[r][0]
                                                   : wrand(0, r % 2 ? 40000 : SOLSTICE_DAY_MAX - 6);
        int64_t skip = r < sizeof DS / sizeof DS[0] ? DS[r][1] : wrand(0, BOWLINE_SKIP_MAX);
        all = bow_same(app, day, skip) && all;
    }
    CHECK(all, "Bowline: unskipped_day, BASIC = KRON = C, 64 days");
    CHECK(bow_lp(app, 0, 127) == ZXAPP_BOTH && bowline_unskipped(0, 127) == BOWLINE_REFUSED,
          "Bowline: every weekday skipped: BASIC refuses, KRON answers: BOTH");
    CHECK(bow_lp(app, SOLSTICE_DAY_MAX, 16) == ZXAPP_BOTH,
          "Bowline: moved past 9999-12-31: BASIC refuses, KRON answers: BOTH");

    /* overdue (BW2): Eve = KRON = C, margins of every size and sign */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(-400, 400) : wrand(-SOLSTICE_DAY_MAX, SOLSTICE_DAY_MAX);
        b[i] = i % 4 ? 1 : 0;
    }
    a[0] = 1, b[0] = 1;                /* a day late */
    a[1] = 0, b[1] = 1;                /* due today */
    a[2] = SOLSTICE_DAY_MAX, b[2] = 1; /* as late as can be */
    a[3] = 5, b[3] = 0;                /* done */
    a[4] = -3, b[4] = 1;               /* not due yet */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(bowline_overdue(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              !bowline_overdue(SOLSTICE_DAY_MAX + 1, 1) && !bowline_overdue(1, 2),
          "Bowline: overdue, the C's own edges");
    zxapp_arg_t oa[2] = {{"late", a, WAVE_ROWS}, {"open", b, WAVE_ROWS}};
    CHECK(wave_same(app, "overdue", oa, 2, want, WAVE_ROWS, true),
          "Bowline: overdue, Eve = KRON = C");
    {
        static const int64_t past[2] = {1, SOLSTICE_DAY_MAX + 1}, one[2] = {1, 1}, two[2] = {1, 2};
        zxapp_arg_t pa[2] = {{"late", past, 2}, {"open", one, 2}};
        zxapp_arg_t qa[2] = {{"late", one, 2}, {"open", two, 2}};
        CHECK(wave_lp(app, "overdue", pa, 2) == ZXAPP_BOTH,
              "Bowline: a margin past 9999-12-31: Eve FALSE, KRON TRUE: BOTH");
        CHECK(wave_lp(app, "overdue", qa, 2) == ZXAPP_BOTH,
              "Bowline: open of 2 is no flag: Eve FALSE, KRON TRUE: BOTH");
    }

    /* task_record_ok (BW3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = wrand(1, BOWLINE_LIST_MAX);
        c[i] = wrand(0, SOLSTICE_DAY_MAX);
        d[i] = c[i] + wrand(0, 6);
        e[i] = wrand(0, i % 2 ? 30 : 9999);
        f[i] = i % 2 ? 1 : 7;
        g[i] = wrand(0, BOWLINE_SKIP_MAX);
        h[i] = i % 5 ? 0 : 1;
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 1, g[0] = 0, h[0] = 0; /* least */
    a[1] = 999999999, b[1] = 999, c[1] = 9999999, d[1] = 9999999;                   /* most */
    e[1] = 9999, f[1] = 9, g[1] = 999, h[1] = 9;
    a[2] = 1000000000; /* task max + 1 */
    b[3] = 1000;       /* list max + 1 */
    c[4] = 10000000;   /* start max + 1 */
    d[5] = 10000000;   /* due max + 1 */
    e[6] = 10000;      /* every max + 1 */
    f[7] = 10;         /* unit max + 1 */
    g[8] = 1000;       /* skip max + 1 */
    h[9] = 10;         /* done max + 1 */
    b[10] = -999;      /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(bowline_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i], h[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE && want[10] == ZXAPP_TRUE,
          "Bowline: task_record_ok, the C's own edges");
    zxapp_arg_t ra[8] = {{"task", a, WAVE_ROWS}, {"list", b, WAVE_ROWS},  {"start", c, WAVE_ROWS},
                         {"due", d, WAVE_ROWS},  {"every", e, WAVE_ROWS}, {"unit", f, WAVE_ROWS},
                         {"skip", g, WAVE_ROWS}, {"done", h, WAVE_ROWS}};
    CHECK(wave_same(app, "task_record_ok", ra, 8, want, WAVE_ROWS, true),
          "Bowline: task_record_ok, MYRA = C");

    /* a repeating task's next day, through solstice.next_occurrence (SO3) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 40000);
        b[i] = i % 2 ? wrand(1, 30) : 7 * wrand(1, 52);
        c[i] = wrand(0, 40000);
        want[i] = solstice_next(a[i], b[i], c[i]);
    }
    zxapp_arg_t na[3] = {{"start", a, WAVE_ROWS}, {"every", b, WAVE_ROWS}, {"today", c, WAVE_ROWS}};
    CHECK(wave_same(app, "solstice.next_occurrence", na, 3, want, WAVE_ROWS, false),
          "Bowline: a task's next repeat, solstice.next_occurrence, KRON = WebAssembly = C");

    /* the tasks in one list, through witness.kind_count (WR2) */
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t lists[32], wl[1];
        uint32_t n = 1u + r % 32u;
        for (uint32_t i = 0; i < n; i++) lists[i] = wrand(1, 6);
        wl[0] = r == 0 ? BOWLINE_LIST_MAX : wrand(1, 6); /* a list no task is in */
        int64_t cnt = witness_kind_count(lists, n, wl[0]);
        zxapp_arg_t ka[2] = {{"kind", lists, n}, {"want", wl, 1}};
        all = wave_same(app, "witness.kind_count", ka, 2, &cnt, 1, false) && all;
    }
    CHECK(all, "Bowline: the tasks in a list, witness.kind_count, KRON = Trebago = C, 64 lists");
    {
        static const int64_t k2[2] = {1, 2}, wants[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"kind", k2, 2}, {"want", wants, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ba, 2) == ZXAPP_BOTH,
              "Bowline: two lists asked at once: Trebago refuses, KRON sums them: BOTH");
    }
}
