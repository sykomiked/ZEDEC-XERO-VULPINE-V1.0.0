/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Solstice's languages against the C its model runs (solstice_rules.h).
 * Slots: @num.solstice_month_days, @num.solstice_weekday,
 * @num.solstice_next_occurrence, @logic.solstice_events_overlap,
 * @logic.solstice_event_ok, and an event's day through almanac,
 * @time.almanac_days. */
#include "solstice_rules.h"

/* One Lingua call per row (zxapp_lingua.c L1): every row of the C's own
 * answers through FORTRAN 77, BASIC and Lingua IR, which must agree. */
static bool sol_lingua_same(zxapp_t *app, const char *entry, int64_t y, int64_t m, int64_t d,
                            uint32_t nargs, int64_t want)
{
    int64_t yv[1] = {y}, mv[1] = {m}, dv[1] = {d}, w[1] = {want};
    zxapp_arg_t a[3] = {{"year", yv, 1}, {"month", mv, 1}, {"day", dv, 1}};
    if (want < 0 || wave_same(app, entry, a, nargs, w, 1, false)) return want >= 0;
    printf("    %s %lld-%lld-%lld\n", entry, (long long) y, (long long) m, (long long) d);
    return false;
}

static int32_t sol_lingua_lp(zxapp_t *app, const char *entry, int64_t y, int64_t m, int64_t d,
                             uint32_t nargs)
{
    int64_t yv[1] = {y}, mv[1] = {m}, dv[1] = {d};
    zxapp_arg_t a[3] = {{"year", yv, 1}, {"month", mv, 1}, {"day", dv, 1}};
    return wave_lp(app, entry, a, nargs);
}

APP_TEST(solstice, 15, 43)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* the C's own calendar: every day 1970..9999 by its day number (1970-01-01 a Thursday) */
    bool cal = true;
    for (int64_t day = 0; day <= SOLSTICE_DAY_MAX && cal; day += day < 800000 ? 1 : 97) {
        int64_t y, m, dd;
        cal = solstice_date_of(day, &y, &m, &dd) && solstice_day_of(y, m, dd) == day &&
              solstice_weekday(y, m, dd) == (day + 3) % 7 + 1 && dd <= solstice_month_days(y, m);
        if (!cal) printf("    day %lld\n", (long long) day);
    }
    CHECK(cal, "Solstice: the C's weekday and month lengths agree with the day count, 1970..9999");
    CHECK(solstice_month_days(2000, 2) == 29 && solstice_month_days(1900, 2) == 28 &&
              solstice_month_days(2024, 2) == 29 && solstice_month_days(2023, 2) == 28 &&
              solstice_month_days(2023, 4) == 30 && solstice_month_days(2023, 12) == 31 &&
              solstice_month_days(SOLSTICE_YEAR_MAX, 1) == 31 &&
              solstice_month_days(SOLSTICE_YEAR_MAX + 1, 1) == SOLSTICE_REFUSED &&
              solstice_month_days(0, 1) == SOLSTICE_REFUSED &&
              solstice_month_days(-1, 1) == SOLSTICE_REFUSED &&
              solstice_month_days(2023, 0) == SOLSTICE_REFUSED &&
              solstice_month_days(2023, 13) == SOLSTICE_REFUSED,
          "Solstice: month_days, the C's own edges");
    CHECK(solstice_weekday(1, 1, 1) == 1 && solstice_weekday(1970, 1, 1) == 4 &&
              solstice_weekday(2000, 2, 29) == 2 && solstice_weekday(2026, 10, 25) == 7 &&
              solstice_weekday(1900, 2, 29) == SOLSTICE_REFUSED &&
              solstice_weekday(2023, 4, 31) == SOLSTICE_REFUSED &&
              solstice_weekday(2023, 4, 0) == SOLSTICE_REFUSED &&
              solstice_weekday(SOLSTICE_YEAR_MAX, 12, 31) > 0 &&
              solstice_weekday(SOLSTICE_YEAR_MAX + 1, 1, 1) == SOLSTICE_REFUSED,
          "Solstice: weekday, the C's own edges");

    /* month_days (SO1): FORTRAN 77 = BASIC = Lingua IR = C, one row per call, 64 calls */
    static const int64_t MY[][2] = {{1, 1},    {1, 2},     {4, 2},     {100, 2},
                                    {400, 2},  {1900, 2},  {2000, 2},  {2023, 2},
                                    {2024, 2}, {2023, 4},  {2023, 7},  {2023, 8},
                                    {2023, 9}, {2023, 12}, {9999, 12}, {SOLSTICE_YEAR32_MAX, 2}};
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t y =
            r < sizeof MY / sizeof MY[0] ? MY[r][0] : wrand(1, r % 2 ? 3000 : SOLSTICE_YEAR32_MAX);
        int64_t m = r < sizeof MY / sizeof MY[0] ? MY[r][1] : wrand(1, 12);
        all = sol_lingua_same(app, "month_days", y, m, 0, 2, solstice_month_days(y, m)) && all;
    }
    CHECK(all, "Solstice: month_days, FORTRAN 77 = BASIC = Lingua IR = C, 64 months");
    CHECK(sol_lingua_lp(app, "month_days", SOLSTICE_YEAR32_MAX + 1, 2, 0, 2) == ZXAPP_BOTH &&
              solstice_month_days(SOLSTICE_YEAR32_MAX + 1, 2) == 29,
          "Solstice: month_days past nine digits: FORTRAN 77 refuses, BASIC answers: BOTH");
    CHECK(sol_lingua_lp(app, "month_days", SOLSTICE_YEAR_MAX, 12, 0, 2) == ZXAPP_BOTH,
          "Solstice: month_days at eighteen digits: FORTRAN 77 traps, BASIC answers: BOTH");
    CHECK(sol_lingua_lp(app, "month_days", 0, 1, 0, 2) == ZXAPP_NEITHER &&
              sol_lingua_lp(app, "month_days", 2023, 13, 0, 2) == ZXAPP_NEITHER &&
              sol_lingua_lp(app, "month_days", SOLSTICE_YEAR_MAX + 1, 1, 0, 2) == ZXAPP_NEITHER,
          "Solstice: month_days, year 0, month 13 or past 18 digits: every language refuses");

    /* weekday (SO2): the same three and the C, 64 dates */
    static const int64_t WD[][3] = {{1, 1, 1},
                                    {1970, 1, 1},
                                    {2000, 1, 1},
                                    {2000, 2, 29},
                                    {2024, 2, 29},
                                    {2026, 10, 21},
                                    {2026, 10, 25},
                                    {1900, 3, 1},
                                    {9999, 12, 31},
                                    {1600, 2, 29},
                                    {2023, 1, 31},
                                    {2023, 3, 1},
                                    {SOLSTICE_YEAR32_MAX, 12, 31},
                                    {SOLSTICE_YEAR32_MAX, 1, 1}};
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t y, m, dd;
        if (r < sizeof WD / sizeof WD[0]) {
            y = WD[r][0], m = WD[r][1], dd = WD[r][2];
        } else {
            y = wrand(1, r % 2 ? 3000 : SOLSTICE_YEAR32_MAX);
            m = wrand(1, 12);
            dd = wrand(1, solstice_month_days(y, m));
        }
        all = sol_lingua_same(app, "weekday", y, m, dd, 3, solstice_weekday(y, m, dd)) && all;
    }
    CHECK(all, "Solstice: weekday, FORTRAN 77 = BASIC = Lingua IR = C, 64 dates");
    CHECK(sol_lingua_lp(app, "weekday", SOLSTICE_YEAR32_MAX + 1, 1, 1, 3) == ZXAPP_BOTH,
          "Solstice: weekday past nine digits: FORTRAN 77 refuses, BASIC answers: BOTH");
    CHECK(sol_lingua_lp(app, "weekday", 2023, 2, 29, 3) == ZXAPP_NEITHER &&
              sol_lingua_lp(app, "weekday", 2023, 4, 0, 3) == ZXAPP_NEITHER &&
              sol_lingua_lp(app, "weekday", 0, 1, 1, 3) == ZXAPP_NEITHER &&
              sol_lingua_lp(app, "weekday", -5, 1, 1, 3) == ZXAPP_NEITHER,
          "Solstice: weekday, no such date: every language refuses");

    /* next_occurrence (SO3): KRON = WebAssembly = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i % 2 ? 30000 : SOLSTICE_DAY_MAX);
        b[i] = wrand(1, i % 3 ? 400 : SOLSTICE_DAY_MAX);
        c[i] = wrand(0, i % 2 ? 30000 : SOLSTICE_DAY_MAX);
    }
    a[0] = 100, b[0] = 7, c[0] = 50;                            /* before the first */
    a[1] = 100, b[1] = 7, c[1] = 100;                           /* on it */
    a[2] = 100, b[2] = 7, c[2] = 101;                           /* the day after */
    a[3] = 100, b[3] = 7, c[3] = 107;                           /* on the second */
    a[4] = 0, b[4] = 1, c[4] = SOLSTICE_DAY_MAX;                /* every day, the last */
    a[5] = SOLSTICE_DAY_MAX, b[5] = 1, c[5] = 0;                /* the last day first */
    a[6] = 5, b[6] = SOLSTICE_DAY_MAX, c[6] = 6;                /* past year 9999 */
    a[7] = 0, b[7] = SOLSTICE_DAY_MAX, c[7] = SOLSTICE_DAY_MAX; /* exactly one repeat */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = solstice_next(a[i], b[i], c[i]);
        CHECK(want[i] >= c[i] && want[i] >= a[i] && (want[i] - a[i]) % b[i] == 0 &&
                  (want[i] == a[i] || want[i] - b[i] < c[i]),
              "Solstice: next_occurrence row %u is the first repeat on or after today", i);
    }
    CHECK(want[0] == 100 && want[1] == 100 && want[2] == 107 && want[3] == 107 &&
              want[4] == SOLSTICE_DAY_MAX && want[5] == SOLSTICE_DAY_MAX &&
              want[6] == 5 + SOLSTICE_DAY_MAX && want[7] == SOLSTICE_DAY_MAX,
          "Solstice: next_occurrence, the C's own edges");
    zxapp_arg_t na[3] = {{"start", a, WAVE_ROWS}, {"every", b, WAVE_ROWS}, {"today", c, WAVE_ROWS}};
    CHECK(wave_same(app, "next_occurrence", na, 3, want, WAVE_ROWS, false),
          "Solstice: next_occurrence, KRON = WebAssembly = C");
    {
        /* past 9999-12-31, or a start before 1970: C++ traps, KRON answers: BOTH */
        static const int64_t s2[2] = {100, 100}, e2[2] = {7, 7},
                             t2[2] = {101, SOLSTICE_DAY_MAX + 1}, sn[2] = {100, -1},
                             tn[2] = {101, 5}, e0[2] = {7, 0};
        zxapp_arg_t oa[3] = {{"start", s2, 2}, {"every", e2, 2}, {"today", t2, 2}};
        zxapp_arg_t ga[3] = {{"start", sn, 2}, {"every", e2, 2}, {"today", tn, 2}};
        zxapp_arg_t za[3] = {{"start", s2, 2}, {"every", e0, 2}, {"today", tn, 2}};
        CHECK(solstice_next(100, 7, SOLSTICE_DAY_MAX + 1) == SOLSTICE_REFUSED &&
                  wave_lp(app, "next_occurrence", oa, 3) == ZXAPP_BOTH,
              "Solstice: next_occurrence past year 9999: C++ refuses, KRON answers: BOTH");
        CHECK(solstice_next(-1, 7, 5) == SOLSTICE_REFUSED &&
                  wave_lp(app, "next_occurrence", ga, 3) == ZXAPP_BOTH,
              "Solstice: next_occurrence from before 1970: C++ refuses, KRON answers: BOTH");
        CHECK(solstice_next(100, 0, 5) == SOLSTICE_REFUSED &&
                  wave_lp(app, "next_occurrence", za, 3) != ZXAPP_TRUE,
              "Solstice: next_occurrence every 0 days: never one answer");
    }

    /* events_overlap (SO4): Eve = KRON = C, margins of every size and sign */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-SOLSTICE_SECONDS_MAX, SOLSTICE_SECONDS_MAX);
        b[i] = i % 4 ? wrand(-7200, 7200) : wrand(-SOLSTICE_SECONDS_MAX, SOLSTICE_SECONDS_MAX);
        if (i % 3 == 0) a[i] = wrand(-3600, 3600);
    }
    a[0] = 1, b[0] = 1;                                       /* one second shared */
    a[1] = 0, b[1] = 5;                                       /* one ends as the other starts */
    a[2] = 5, b[2] = 0;                                       /* and the other way round */
    a[3] = SOLSTICE_SECONDS_MAX, b[3] = SOLSTICE_SECONDS_MAX; /* every second there is */
    a[4] = -1, b[4] = 7;                                      /* apart */
    a[5] = 3600, b[5] = 3600;                                 /* the same hour */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(solstice_overlap(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              !solstice_overlap(SOLSTICE_SECONDS_MAX + 1, 1),
          "Solstice: events_overlap, the C's own edges");
    zxapp_arg_t oa[2] = {{"aover", a, WAVE_ROWS}, {"bover", b, WAVE_ROWS}};
    CHECK(wave_same(app, "events_overlap", oa, 2, want, WAVE_ROWS, true),
          "Solstice: events_overlap, Eve = KRON = C");
    {
        static const int64_t past[2] = {1, SOLSTICE_SECONDS_MAX + 1}, one[2] = {1, 1};
        zxapp_arg_t pa[2] = {{"aover", past, 2}, {"bover", one, 2}};
        zxapp_arg_t qa[2] = {{"aover", one, 2}, {"bover", past, 2}};
        CHECK(wave_lp(app, "events_overlap", pa, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "events_overlap", qa, 2) == ZXAPP_BOTH,
              "Solstice: a margin past year 9999: Eve FALSE, KRON TRUE: BOTH");
    }

    /* event_ok (SO5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = wrand(0, SOLSTICE_SECONDS_MAX);
        c[i] = b[i] + wrand(1, 86400);
        d[i] = wrand(0, i % 2 ? 7 : 9999999);
        e[i] = wrand(0, 9999);
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 0, e[0] = 0;           /* every least */
    a[1] = 999999999, b[1] = 999999999999, c[1] = 999999999999; /* every most */
    d[1] = 9999999, e[1] = 9999;
    a[2] = 1000000000;             /* event max + 1 */
    b[3] = INT64_C(1000000000000); /* start max + 1 */
    c[4] = INT64_C(1000000000000); /* finish max + 1 */
    d[5] = 10000000;               /* every max + 1 */
    e[6] = 10000;                  /* alarm max + 1 */
    e[7] = -9999;                  /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(solstice_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Solstice: event_ok, the C's own edges");
    zxapp_arg_t ea[5] = {{"event", a, WAVE_ROWS},
                         {"start", b, WAVE_ROWS},
                         {"finish", c, WAVE_ROWS},
                         {"every", d, WAVE_ROWS},
                         {"alarm", e, WAVE_ROWS}};
    CHECK(wave_same(app, "event_ok", ea, 5, want, WAVE_ROWS, true), "Solstice: event_ok, MYRA = C");

    /* an event's day, through almanac.days: floor(seconds / 86400) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SOLSTICE_SECONDS_MAX);
        want[i] = a[i] / SOLSTICE_DAY_S;
    }
    a[0] = 0, want[0] = 0;
    a[1] = SOLSTICE_DAY_S - 1, want[1] = 0;
    a[2] = SOLSTICE_DAY_S, want[2] = 1;
    a[3] = SOLSTICE_SECONDS_MAX, want[3] = SOLSTICE_DAY_MAX;
    zxapp_arg_t da[1] = {{"seconds", a, WAVE_ROWS}};
    CHECK(wave_same(app, "almanac.days", da, 1, want, WAVE_ROWS, false),
          "Solstice: an event's day, almanac.days, WebAssembly = KRON = C");
}
