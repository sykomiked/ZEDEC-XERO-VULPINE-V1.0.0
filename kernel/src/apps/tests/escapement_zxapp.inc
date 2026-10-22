/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Escapement's languages, witnessed, row by row against escapement_rules.h,
 * the C the model runs; the parts it composes (treadle, solstice,
 * tallystick, sundial) resolved and called; and the BOTH of two clocks.
 * Slots: @num.escapement_offset_median, @logic.escapement_job_may_run,
 * @num.escapement_timesheet_week, @num.escapement_shift_clashes,
 * @logic.escapement_shift_node_ok; and, used, @num.treadle_next_run,
 * @logic.treadle_budget_ok, @logic.treadle_task_ok, @num.solstice_weekday,
 * @logic.solstice_events_overlap, @num.tallystick_next_doc_number,
 * @num.sundial_zone_minute. */
#include "escapement_rules.h"
#include "sundial_rules.h"
#include "tallystick_rules.h"
#include "treadle_rules.h"

APP_TEST(escapement, 39, 172)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[7][2] = {
        {"treadle.next_run", "treadle"},         {"treadle.budget_ok", "treadle"},
        {"treadle.task_ok", "treadle"},          {"solstice.weekday", "solstice"},
        {"solstice.events_overlap", "solstice"}, {"tallystick.next_doc_number", "tallystick"},
        {"sundial.zone_minute", "sundial"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 8 && app->n == 39,
          "Escapement: composed of 4 parts, 8 own lines and 31 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 7u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Escapement: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 5 && !org[0].from, "Escapement: itself and four parts");

    /* ER1: offset_median, one vote a call, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        uint32_t n = (uint32_t) wrand(1, 9);
        int64_t w = 0;
        for (uint32_t k = 0; k < n; k++)
            a[k] =
                i % 2u ? wrand(-3000, 3000) : wrand(-ESCAPEMENT_OFFSET_MAX, ESCAPEMENT_OFFSET_MAX);
        if (i == 0) n = 1, a[0] = 0;                                                    /* zero */
        if (i == 1) n = 2, a[0] = ESCAPEMENT_OFFSET_MAX, a[1] = ESCAPEMENT_OFFSET_MAX;  /* max */
        if (i == 2) n = 2, a[0] = -ESCAPEMENT_OFFSET_MAX, a[1] = ESCAPEMENT_OFFSET_MAX; /* least */
        if (i == 3) n = 3, a[0] = 0, a[1] = 2500, a[2] = 100;       /* the vote */
        if (i == 4) n = 4, a[0] = 5, a[1] = 5, a[2] = 5, a[3] = -1; /* ties */
        CHECK(escapement_offset_median(a, n, &w), "Escapement: ER1 row %u answers", i);
        zxapp_arg_t ma[1] = {{"offsets", a, n}};
        CHECK(wave_same(app, "offset_median", ma, 1, &w, 1, false),
              "Escapement: offset_median row %u, Trebago = KRON = C (%lld)", i, (long long) w);
    }
    {
        /* all 256 peers at once, the most net.peers lists */
        int64_t w = 0;
        static int64_t big[ESCAPEMENT_PEERS_MAX];
        for (uint32_t k = 0; k < ESCAPEMENT_PEERS_MAX; k++) big[k] = wrand(-500, 500);
        zxapp_arg_t ma[1] = {{"offsets", big, ESCAPEMENT_PEERS_MAX}};
        CHECK(escapement_offset_median(big, ESCAPEMENT_PEERS_MAX, &w) &&
                  wave_same(app, "offset_median", ma, 1, &w, 1, false),
              "Escapement: offset_median of 256 peers");
        /* past a day, or 257 offsets: Trebago refuses, KRON answers */
        static int64_t past[2] = {0, ESCAPEMENT_OFFSET_MAX + 1},
                       under[3] = {-ESCAPEMENT_OFFSET_MAX - 1, 0, 0},
                       many[ESCAPEMENT_PEERS_MAX + 1u];
        zxapp_arg_t p1[1] = {{"offsets", past, 2}}, p2[1] = {{"offsets", under, 3}},
                    p3[1] = {{"offsets", many, ESCAPEMENT_PEERS_MAX + 1u}};
        CHECK(
            !escapement_offset_median(past, 2, &w) && !escapement_offset_median(under, 3, &w) &&
                !escapement_offset_median(many, ESCAPEMENT_PEERS_MAX + 1u, &w) &&
                wave_lp(app, "offset_median", p1, 1) == ZXAPP_BOTH &&
                wave_lp(app, "offset_median", p2, 1) == ZXAPP_BOTH &&
                wave_lp(app, "offset_median", p3, 1) == ZXAPP_BOTH,
            "Escapement: an offset past a day or 257 of them: Trebago refuses, KRON answers: BOTH");
    }

    /* ER2: job_may_run, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-5, 5) : wrand(0, ESCAPEMENT_SPAN_MAX);
        b[i] = i % 2u ? wrand(-5, 5) : wrand(0, ESCAPEMENT_SPAN_MAX);
        c[i] = i % 2u ? wrand(-5, 5) : wrand(0, ESCAPEMENT_SPAN_MAX);
        if (i == 0) a[i] = 0, b[i] = 1, c[i] = 0; /* the window just open, the deadline now */
        if (i == 1)
            a[i] = ESCAPEMENT_SPAN_MAX, b[i] = ESCAPEMENT_SPAN_MAX, c[i] = ESCAPEMENT_SPAN_MAX;
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 5;  /* the window closed */
        if (i == 3) a[i] = -1, b[i] = 9, c[i] = 9; /* not open yet */
        if (i == 4) a[i] = 3, b[i] = 9, c[i] = -1; /* the deadline passed */
        want[i] = T_(escapement_job_may_run(a[i], b[i], c[i]));
    }
    zxapp_arg_t ja[3] = {{"opened", a, WAVE_ROWS}, {"closes", b, WAVE_ROWS}, {"due", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "job_may_run", ja, 3, want, WAVE_ROWS, true),
          "Escapement: job_may_run, Eve = KRON = C");
    {
        static const int64_t o[2] = {0, ESCAPEMENT_SPAN_MAX + 1}, one[2] = {1, 1}, z[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"opened", o, 2}, {"closes", one, 2}, {"due", z, 2}};
        zxapp_arg_t xb[3] = {{"opened", z, 2}, {"closes", o, 2}, {"due", z, 2}};
        CHECK(!escapement_job_may_run(o[1], 1, 0) &&
                  wave_lp(app, "job_may_run", xa, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "job_may_run", xb, 3) == ZXAPP_BOTH,
              "Escapement: a margin past 366 days: Eve refuses, KRON answers: BOTH");
    }

    /* ER3: timesheet_week, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, ESCAPEMENT_WEEK_MIN);
        b[i] = wrand(a[i], ESCAPEMENT_WEEK_MIN);
        c[i] = wrand(0, b[i] - a[i] < ESCAPEMENT_UNPAID_MAX ? b[i] - a[i] : ESCAPEMENT_UNPAID_MAX);
        d[i] = wrand(0, ESCAPEMENT_WEEK_MIN - (b[i] - a[i] - c[i]));
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0;                   /* zero */
        if (i == 1) a[i] = 0, b[i] = ESCAPEMENT_WEEK_MIN, c[i] = 0, d[i] = 0; /* the whole week */
        if (i == 2) a[i] = 100, b[i] = 1099, c[i] = ESCAPEMENT_UNPAID_MAX, d[i] = 0; /* break max */
        if (i == 3) a[i] = 600, b[i] = 600, c[i] = 0, d[i] = ESCAPEMENT_WEEK_MIN;    /* full */
        want[i] = escapement_week_after(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t ta[4] = {{"start", a, WAVE_ROWS},
                         {"finish", b, WAVE_ROWS},
                         {"unpaid", c, WAVE_ROWS},
                         {"prior", d, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[1] == ESCAPEMENT_WEEK_MIN && want[2] == 0 &&
              want[3] == ESCAPEMENT_WEEK_MIN &&
              wave_same(app, "timesheet_week", ta, 4, want, WAVE_ROWS, false),
          "Escapement: timesheet_week, Sutra = Trebago = C");
    {
        /* a shift past the week: Sutra's pictures answer, Trebago refuses */
        static const int64_t s1[2] = {0, 20000}, f1[2] = {60, 20060}, z[2] = {0, 0};
        zxapp_arg_t xa[4] = {
            {"start", s1, 2}, {"finish", f1, 2}, {"unpaid", z, 2}, {"prior", z, 2}};
        CHECK(escapement_week_after(20000, 20060, 0, 0) == ESCAPEMENT_REFUSED &&
                  wave_lp(app, "timesheet_week", xa, 4) == ZXAPP_BOTH,
              "Escapement: a shift past the week: BOTH");
        /* the week's sum past 10080 minutes, and a shift ending before it starts: both refuse */
        static const int64_t s2[2] = {0, 0}, f2[2] = {60, 60}, p2[2] = {0, 10021}, s3[2] = {0, 600},
                             f3[2] = {60, 500};
        zxapp_arg_t ya[4] = {
            {"start", s2, 2}, {"finish", f2, 2}, {"unpaid", z, 2}, {"prior", p2, 2}};
        zxapp_arg_t yb[4] = {
            {"start", s3, 2}, {"finish", f3, 2}, {"unpaid", z, 2}, {"prior", z, 2}};
        CHECK(escapement_week_after(0, 60, 0, 10021) == ESCAPEMENT_REFUSED &&
                  escapement_week_after(600, 500, 0, 0) == ESCAPEMENT_REFUSED &&
                  wave_lp(app, "timesheet_week", ya, 4) != ZXAPP_TRUE &&
                  wave_lp(app, "timesheet_week", yb, 4) != ZXAPP_TRUE,
              "Escapement: a week past 10080 minutes, or a shift backwards: refused");
    }

    /* ER4: shift_clashes, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 4);
        b[i] = wrand(0, ESCAPEMENT_WEEK_MIN - 600);
        c[i] = b[i] + wrand(0, 600);
    }
    a[1] = a[0], b[1] = b[0], c[1] = c[0] + 1; /* the same shift twice: a clash */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = escapement_clashes(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ca[3] = {
        {"person", a, WAVE_ROWS}, {"start", b, WAVE_ROWS}, {"finish", c, WAVE_ROWS}};
    CHECK(want[0] >= 1 && wave_same(app, "shift_clashes", ca, 3, want, WAVE_ROWS, false),
          "Escapement: shift_clashes, KRON = C");
    {
        /* back to back is no clash; one inside another is */
        static const int64_t p[4] = {1, 1, 1, 2}, s[4] = {480, 1020, 500, 480},
                             fin[4] = {1020, 1080, 600, 1020};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = escapement_clashes(p, s, fin, 4, i);
        zxapp_arg_t xa[3] = {{"person", p, 4}, {"start", s, 4}, {"finish", fin, 4}};
        CHECK(w[0] == 1 && w[1] == 0 && w[2] == 1 && w[3] == 0 &&
                  wave_same(app, "shift_clashes", xa, 3, w, 4, false),
              "Escapement: back to back is no clash, one inside another is");
    }

    /* ER5: shift_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(0, 12000);
        c[i] = wrand(0, 1200);
        d[i] = wrand(0, 120000);
        e[i] = wrand(0, 120000);
        f[i] = wrand(-120000, 120000);
        if (i == 0)
            a[i] = 999999, b[i] = 9999, c[i] = 999, d[i] = 99999, e[i] = 99999, f[i] = 99999;
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 1000, d[i] = 1, e[i] = 1, f[i] = 1;
        want[i] = T_(escapement_node_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"person", a, WAVE_ROWS}, {"week", b, WAVE_ROWS},
                         {"shift", c, WAVE_ROWS},  {"start", d, WAVE_ROWS},
                         {"finish", e, WAVE_ROWS}, {"worked", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "shift_node_ok", na, 6, want, WAVE_ROWS, true),
          "Escapement: shift_node_ok, MYRA = C");

    /* the parts, called through Escapement as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(1, 1440);
        c[i] = wrand(0, 1439);
        want[i] = treadle_next_run(a[i], b[i], c[i]);
    }
    zxapp_arg_t ra[3] = {{"start", a, WAVE_ROWS}, {"period", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "treadle.next_run", ra, 3, want, WAVE_ROWS, false),
          "Escapement: treadle.next_run, a timed job's next run");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(-5, 999999999);
        want[i] = T_(treadle_budget_ok(a[i], b[i]));
    }
    zxapp_arg_t ba[2] = {{"cost", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "treadle.budget_ok", ba, 2, want, WAVE_ROWS, true),
          "Escapement: treadle.budget_ok, a job's cost within the budget");
    {
        static const int64_t tk[2] = {7, 1000}, kd[2] = {0, 0}, st[2] = {480, 0}, pe[2] = {60, 1},
                             co[2] = {500, 1}, ap[2] = {0, 0};
        int64_t w[2] = {T_(treadle_record_fits(7, 0, 480, 60, 500, 0)),
                        T_(treadle_record_fits(1000, 0, 0, 1, 1, 0))};
        zxapp_arg_t ka[6] = {{"task", tk, 2},   {"kind", kd, 2}, {"start", st, 2},
                             {"period", pe, 2}, {"cost", co, 2}, {"approved", ap, 2}};
        CHECK(w[0] == ZXAPP_TRUE && w[1] == ZXAPP_FALSE &&
                  wave_same(app, "treadle.task_ok", ka, 6, w, 2, true),
              "Escapement: treadle.task_ok, a job's task record");
    }
    {
        /* a week of shifts: Monday 2026-10-19 .. Sunday 2026-10-25 (Lingua reads one row a call) */
        for (int64_t day = 19; day <= 25; day++) {
            int64_t y = 2026, m = 10, w = solstice_weekday(y, m, day);
            zxapp_arg_t wa[3] = {{"year", &y, 1}, {"month", &m, 1}, {"day", &day, 1}};
            CHECK(w == day - 18 && wave_same(app, "solstice.weekday", wa, 3, &w, 1, false),
                  "Escapement: solstice.weekday of 2026-10-%lld", (long long) day);
        }
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        /* two shifts' margins in seconds, as escapement_clashes gives them */
        a[i] = (wrand(0, 1200) - wrand(0, 1200)) * 60;
        b[i] = (wrand(0, 1200) - wrand(0, 1200)) * 60;
        want[i] = T_(solstice_overlap(a[i], b[i]));
    }
    zxapp_arg_t oa[2] = {{"aover", a, WAVE_ROWS}, {"bover", b, WAVE_ROWS}};
    CHECK(wave_same(app, "solstice.events_overlap", oa, 2, want, WAVE_ROWS, true),
          "Escapement: solstice.events_overlap, two shifts");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i ? wrand(0, TSTK_NUMBER_MAX - 1) : TSTK_NUMBER_MAX - 1;
        want[i] = tstk_next_number(a[i]);
    }
    zxapp_arg_t da[1] = {{"last", a, WAVE_ROWS}};
    CHECK(want[0] == TSTK_NUMBER_MAX &&
              wave_same(app, "tallystick.next_doc_number", da, 1, want, WAVE_ROWS, false),
          "Escapement: tallystick.next_doc_number, a timesheet's number");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(SUNDIAL_OFFSET_MIN, SUNDIAL_OFFSET_MAX);
        want[i] = sundial_zone_minute(a[i], b[i]);
    }
    zxapp_arg_t za[2] = {{"utc", a, WAVE_ROWS}, {"offset", b, WAVE_ROWS}};
    CHECK(wave_same(app, "sundial.zone_minute", za, 2, want, WAVE_ROWS, false),
          "Escapement: sundial.zone_minute, the owner's local minute");

    /* ER6, the BOTH test: device A reads 12:00:00.000, B 12:00:02.500, the
     * tolerance 500 ms: the time is BOTH and two devices cannot vote; a
     * third at 12:00:00.100 makes the median (100 ms) the vote, the time
     * still BOTH until the owner accepts it, and no reading is changed */
    {
        int64_t off[3] = {0, 2500, 100}, keep[3] = {0, 2500, 100}, vote = -1;
        bool voted = true;
        lpres_state_t st = escapement_clock_state(off, 2, 500, &vote, &voted);
        CHECK(st == LPRES_STATE_BOTH && st == 3 && !voted,
              "Escapement: two clocks 2.5 s apart: BOTH, no vote");
        st = escapement_clock_state(off, 3, 500, &vote, &voted);
        CHECK(st == LPRES_STATE_BOTH && voted && vote == 100 && !memcmp(off, keep, sizeof off),
              "Escapement: three devices vote the median, 100 ms; no clock overwritten");
        zxapp_arg_t va[1] = {{"offsets", off, 3}};
        CHECK(wave_same(app, "offset_median", va, 1, &vote, 1, false),
              "Escapement: the vote, Trebago = KRON = C");
        CHECK(lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Escapement: a job on a BOTH clock does not run");
        CHECK(escapement_clock_state(off, 2, 2500, &vote, &voted) == LPRES_STATE_TRUE &&
                  escapement_clock_state(off, 0, 500, &vote, &voted) == LPRES_STATE_NEITHER,
              "Escapement: within the tolerance TRUE, no clock NEITHER");
    }
}
