/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Poultice's languages, witnessed, row by row against poultice_rules.h,
 * the C the model runs; the parts it composes (solstice, bowline,
 * wellspring, watchfire, cartulary, weatherglass) resolved and called;
 * and the BOTH of two carers' heart rates for one minute. Slots:
 * @logic.poultice_dose_in_limit, @num.poultice_dose_in_limit_n,
 * @num.poultice_care_month_cost, @logic.poultice_care_node_ok; and, used,
 * @num.solstice_next_occurrence, @logic.solstice_events_overlap,
 * @num.solstice_month_days, @num.bowline_unskipped_day,
 * @logic.bowline_overdue, @num.wellspring_band, @num.wellspring_week,
 * @logic.wellspring_log_ok, @logic.watchfire_medical_id_ok,
 * @logic.watchfire_sos_contact_ok, @num.cartulary_class_year_cell,
 * @num.weatherglass_week_temps. */
#include "poultice_rules.h"

APP_TEST(poultice, 72, 148)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: six parts, every part entry it calls resolves to its part */
    static const char *const used[12][2] = {{"solstice.next_occurrence", "solstice"},
                                            {"solstice.events_overlap", "solstice"},
                                            {"solstice.month_days", "solstice"},
                                            {"bowline.unskipped_day", "bowline"},
                                            {"bowline.overdue", "bowline"},
                                            {"wellspring.band", "wellspring"},
                                            {"wellspring.week", "wellspring"},
                                            {"wellspring.log_ok", "wellspring"},
                                            {"watchfire.medical_id_ok", "watchfire"},
                                            {"watchfire.sos_contact_ok", "watchfire"},
                                            {"cartulary.class_year_cell", "cartulary"},
                                            {"weatherglass.week_temps", "weatherglass"}};
    CHECK(app->resolved && app->nuse == 6 && app->composed == 7 && app->n == 72u &&
              app->n <= ZXAPP_LOGIC_MAX,
          "Poultice: composed of 6 parts, 7 own lines and %u of theirs, within %u", app->n - 7u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 12u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Poultice: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* PR2: dose_in_limit, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-50, 50) : wrand(-POULTICE_DOSE_MAX, POULTICE_DOSE_MAX);
        b[i] = wrand(-3, POULTICE_DAY_MAX);
        c[i] = i % 3u ? wrand(-600, 600) : wrand(-POULTICE_WAIT_MAX, POULTICE_WAIT_MAX);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0; /* every limit met exactly */
        if (i == 1) a[i] = POULTICE_DOSE_MAX, b[i] = POULTICE_DAY_MAX, c[i] = POULTICE_WAIT_MAX;
        if (i == 2) a[i] = -1, b[i] = 3, c[i] = 60; /* over the per-dose maximum */
        if (i == 3) a[i] = 5, b[i] = -1, c[i] = 60; /* over the daily maximum */
        if (i == 4) a[i] = 5, b[i] = 3, c[i] = -1;  /* too soon after the last */
        if (i == 5) a[i] = 5, b[i] = 3, c[i] = 60;  /* within */
        want[i] = T_(poultice_dose_in_limit(a[i], b[i], c[i]));
    }
    zxapp_arg_t da[3] = {
        {"dosegap", a, WAVE_ROWS}, {"daygap", b, WAVE_ROWS}, {"waitgap", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              wave_same(app, "dose_in_limit", da, 3, want, WAVE_ROWS, true),
          "Poultice: dose_in_limit, Eve = KRON = C");

    /* PR3: dose_in_limit_n, KRON = Trebago = C, and the same rule as the truth */
    bool same = true;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(-POULTICE_DAY_LOW, POULTICE_DAY_MAX);
        want[i] = poultice_dose_in_limit_n(a[i], b[i], c[i]);
        same = same && want[i] >= 0 && (want[i] == 1) == poultice_dose_in_limit(a[i], b[i], c[i]);
    }
    CHECK(same && wave_same(app, "dose_in_limit_n", da, 3, want, WAVE_ROWS, false),
          "Poultice: dose_in_limit_n, KRON = Trebago = C, 1 exactly when dose_in_limit");
    {
        /* a margin past its range: Eve FALSE, KRON TRUE; Trebago refuses, KRON 1 */
        static const int64_t big[2] = {0, POULTICE_DOSE_MAX + 1}, zero[2] = {0, 0},
                             wide[2] = {0, POULTICE_DAY_MAX + 1},
                             low[2] = {0, -POULTICE_DAY_LOW - 1};
        zxapp_arg_t p1[3] = {{"dosegap", big, 2}, {"daygap", zero, 2}, {"waitgap", zero, 2}};
        zxapp_arg_t p2[3] = {{"dosegap", zero, 2}, {"daygap", wide, 2}, {"waitgap", zero, 2}};
        zxapp_arg_t p3[3] = {{"dosegap", zero, 2}, {"daygap", zero, 2}, {"waitgap", big, 2}};
        zxapp_arg_t p4[3] = {{"dosegap", zero, 2}, {"daygap", low, 2}, {"waitgap", zero, 2}};
        CHECK(!poultice_dose_in_limit(big[1], 0, 0) &&
                  poultice_dose_in_limit_n(big[1], 0, 0) == POULTICE_REFUSED &&
                  wave_lp(app, "dose_in_limit", p1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit", p2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit", p3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit_n", p1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit_n", p2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit_n", p3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "dose_in_limit_n", p4, 3) == ZXAPP_BOTH,
              "Poultice: a margin past its range: Eve and Trebago hold it, KRON answers: BOTH");
    }

    /* PR1: the margins a proposal gives, the wait capped */
    {
        int64_t m[3];
        poultice_margins(500, 1000, 2, 4, 300, 240, m);
        bool ok1 = m[0] == 500 && m[1] == 2 && m[2] == 60;
        poultice_margins(1000, 1000, 5, 4, POULTICE_WAIT_MAX, 240, m);
        CHECK(ok1 && m[0] == 0 && m[1] == -1 && m[2] == POULTICE_WAIT_MAX &&
                  !poultice_dose_in_limit(m[0], m[1], m[2]),
              "Poultice: PR1 margins: a fifth dose in a day of four is not within");
    }

    /* PR4: care_month_cost, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, POULTICE_PRICE_MAX);
        b[i] = wrand(0, POULTICE_PER_DAY);
        c[i] = wrand(28, 31);
        if (i == 0) a[i] = 0, b[i] = 3, c[i] = 30;                                 /* free */
        if (i == 1) a[i] = POULTICE_PRICE_MAX, b[i] = POULTICE_PER_DAY, c[i] = 99; /* the most */
        if (i == 2) a[i] = 1250, b[i] = 3, c[i] = 30;                              /* 1125.00 */
        if (i == 3) a[i] = 7, b[i] = 2, c[i] = 0;                                  /* no days */
        want[i] = poultice_month_cost(a[i], b[i], c[i]);
    }
    zxapp_arg_t ca[3] = {{"price", a, WAVE_ROWS}, {"perday", b, WAVE_ROWS}, {"days", c, WAVE_ROWS}};
    CHECK(want[1] == INT64_C(98900999901099) && want[2] == 112500 &&
              wave_same(app, "care_month_cost", ca, 3, want, WAVE_ROWS, false),
          "Poultice: care_month_cost, Sutra = Trebago = C");
    {
        static const int64_t pr[2] = {1, POULTICE_PRICE_MAX + 1}, one[2] = {1, 1},
                             thirty[2] = {30, 30}, per[2] = {1, POULTICE_PER_DAY + 1},
                             neg[2] = {1, -1};
        zxapp_arg_t p1[3] = {{"price", pr, 2}, {"perday", one, 2}, {"days", thirty, 2}};
        zxapp_arg_t p2[3] = {{"price", one, 2}, {"perday", per, 2}, {"days", thirty, 2}};
        zxapp_arg_t p3[3] = {{"price", neg, 2}, {"perday", one, 2}, {"days", thirty, 2}};
        CHECK(poultice_month_cost(pr[1], 1, 30) == POULTICE_REFUSED &&
                  poultice_month_cost(1, per[1], 30) == POULTICE_REFUSED &&
                  poultice_month_cost(-1, 1, 30) == POULTICE_REFUSED &&
                  wave_lp(app, "care_month_cost", p1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "care_month_cost", p2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "care_month_cost", p3, 3) == ZXAPP_NEITHER,
              "Poultice: past Sutra's pictures Trebago answers: BOTH; a negative price: both "
              "refuse");
    }

    /* PR5: care_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(0, 12);
        c[i] = wrand(0, 12000);
        d[i] = wrand(0, 1200000);
        e[i] = wrand(0, 12000);
        f[i] = wrand(0, 120);
        g[i] = wrand(0, INT64_C(1200000000));
        if (i == 0)
            a[i] = 9999, b[i] = 3, c[i] = 9999, d[i] = 999999, e[i] = 1439, f[i] = 99,
            g[i] = 999999999; /* the most */
        if (i == 1) a[i] = 1, b[i] = 3, c[i] = 1, d[i] = 20000, e[i] = 600, f[i] = 1, g[i] = 72;
        if (i == 2) a[i] = 1, b[i] = 3, c[i] = 1, d[i] = 20000, e[i] = 600, f[i] = 100, g[i] = 72;
        if (i == 3)
            a[i] = 1, b[i] = 2, c[i] = 1, d[i] = 20000, e[i] = 600, f[i] = 1,
            g[i] = 1000000000; /* a dose past 9 digits */
        want[i] = T_(poultice_node_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t na[7] = {{"patient", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS},
                         {"item", c, WAVE_ROWS},    {"day", d, WAVE_ROWS},
                         {"minute", e, WAVE_ROWS},  {"carer", f, WAVE_ROWS},
                         {"value", g, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "care_node_ok", na, 7, want, WAVE_ROWS, true),
          "Poultice: care_node_ok, MYRA = C");

    /* the parts, called through Poultice as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 30000);
        b[i] = wrand(1, 30);
        c[i] = wrand(0, 40000);
        want[i] = solstice_next(a[i], b[i], c[i]);
    }
    zxapp_arg_t sa[3] = {{"start", a, WAVE_ROWS}, {"every", b, WAVE_ROWS}, {"today", c, WAVE_ROWS}};
    CHECK(wave_same(app, "solstice.next_occurrence", sa, 3, want, WAVE_ROWS, false),
          "Poultice: solstice.next_occurrence, a repeating dose's next day");
    for (uint32_t i = 0; i < 8u; i++) {
        int64_t day = wrand(0, 30000), skip = wrand(0, BOWLINE_SKIP_MAX),
                w = bowline_unskipped(day, skip);
        zxapp_arg_t ua[2] = {{"day", &day, 1}, {"skip", &skip, 1}};
        CHECK(w >= day && wave_same(app, "bowline.unskipped_day", ua, 2, &w, 1, false),
              "Poultice: bowline.unskipped_day row %u, moved past skipped weekdays", i);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-7200, 7200) * 60;
        b[i] = wrand(-7200, 7200) * 60;
        want[i] = T_(solstice_overlap(a[i], b[i]));
    }
    zxapp_arg_t oa[2] = {{"aover", a, WAVE_ROWS}, {"bover", b, WAVE_ROWS}};
    CHECK(wave_same(app, "solstice.events_overlap", oa, 2, want, WAVE_ROWS, true),
          "Poultice: solstice.events_overlap, two dose windows");
    for (uint32_t i = 0; i < 6u; i++) {
        int64_t y = wrand(1970, 2100), mo = wrand(1, 12), w = solstice_month_days(y, mo);
        zxapp_arg_t ma[2] = {{"year", &y, 1}, {"month", &mo, 1}};
        CHECK(wave_same(app, "solstice.month_days", ma, 2, &w, 1, false),
              "Poultice: solstice.month_days row %u, a month's days for its costs", i);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-30, 400);
        b[i] = wrand(0, 1);
        want[i] = T_(bowline_overdue(a[i], b[i]));
    }
    zxapp_arg_t va[2] = {{"late", a, WAVE_ROWS}, {"open", b, WAVE_ROWS}};
    CHECK(wave_same(app, "bowline.overdue", va, 2, want, WAVE_ROWS, true),
          "Poultice: bowline.overdue, a clinician review past its day");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, POULTICE_READING);
        b[i] = wrand(0, 500);
        c[i] = b[i] + wrand(0, 500);
        want[i] = a[i] < b[i] ? -1 : a[i] > c[i] ? 1 : 0;
    }
    zxapp_arg_t ba[3] = {{"reading", a, WAVE_ROWS}, {"lo", b, WAVE_ROWS}, {"hi", c, WAVE_ROWS}};
    CHECK(wave_same(app, "wellspring.band", ba, 3, want, WAVE_ROWS, false),
          "Poultice: wellspring.band, a reading against its declared range");
    {
        int64_t sum = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 100000000), sum += a[i];
        zxapp_arg_t wa[1] = {{"amount", a, WAVE_ROWS}};
        CHECK(wave_same(app, "wellspring.week", wa, 1, &sum, 1, false),
              "Poultice: wellspring.week, a week's doses or a month's costs, exact");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1200000);
        b[i] = wrand(1, 4);
        c[i] = wrand(0, INT64_C(1200000000));
        want[i] = T_(a[i] <= 999999 && c[i] <= 999999999);
    }
    zxapp_arg_t la[3] = {{"day", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS}, {"amount", c, WAVE_ROWS}};
    CHECK(wave_same(app, "wellspring.log_ok", la, 3, want, WAVE_ROWS, true),
          "Poultice: wellspring.log_ok, a heart-rate entry fits Wellspring's log");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 12);
        b[i] = wrand(0, 1200);
        c[i] = i % 4u ? wrand(0, INT64_C(1800000000000)) : INT64_C(999999999999999) + (i % 8u) / 4u;
        want[i] = T_(watchfire_medical_fits(a[i], b[i], c[i]));
    }
    zxapp_arg_t ia[3] = {{"field", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.medical_id_ok", ia, 3, want, WAVE_ROWS, true),
          "Poultice: watchfire.medical_id_ok, the medical ID held as typed");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 120);
        b[i] = wrand(0, INT64_C(12000000000));
        c[i] = wrand(0, 120);
        d[i] = wrand(0, INT64_C(1800000000000));
        want[i] = T_(watchfire_contact_fits(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ka[4] = {{"seq", a, WAVE_ROWS},
                         {"peer", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.sos_contact_ok", ka, 4, want, WAVE_ROWS, true),
          "Poultice: watchfire.sos_contact_ok, each carer's record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 5), b[i] = wrand(20000, 20006);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cartulary_cell(a, b, WAVE_ROWS, i);
    zxapp_arg_t ga[2] = {{"cls", a, WAVE_ROWS}, {"year", b, WAVE_ROWS}};
    CHECK(wave_same(app, "cartulary.class_year_cell", ga, 2, want, WAVE_ROWS, false),
          "Poultice: cartulary.class_year_cell, the symptom grid, symptom by day");
    {
        int64_t lo = POULTICE_READING, hi = 0, sum = 0, span[3];
        for (uint32_t i = 0; i < 14u; i++) {
            a[i] = wrand(40, 180);
            lo = a[i] < lo ? a[i] : lo;
            hi = a[i] > hi ? a[i] : hi;
            sum += a[i];
        }
        span[0] = lo, span[1] = hi, span[2] = sum / 14;
        zxapp_arg_t ta[1] = {{"temp", a, 14}};
        CHECK(wave_same(app, "weatherglass.week_temps", ta, 1, span, 3, false),
              "Poultice: weatherglass.week_temps, a week's heart rates: low, high, floored mean");
    }

    /* PR6, the BOTH test: two carers log heart rate 72 and 128 bpm for the
     * same minute. The reading is BOTH; both nodes fit (the carer is in the
     * key, so neither overwrites the other) and both are kept; nothing
     * joined with it is TRUE, so the escalation goes out. */
    static const int64_t two[2] = {72, 128}, one[2] = {72, 72};
    lpres_state_t st = poultice_reading_state(two, 2);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3, "Poultice: 72 and 128 for one minute: BOTH");
    CHECK(lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE &&
              poultice_reading_state(one, 2) == LPRES_STATE_TRUE &&
              poultice_reading_state(one, 0) == LPRES_STATE_NEITHER,
          "Poultice: one value TRUE, none NEITHER, BOTH never TRUE");
    {
        static const int64_t pat[2] = {1, 1}, kind[2] = {3, 3}, item[2] = {1, 1},
                             day[2] = {20748, 20748}, min[2] = {600, 600}, carer[2] = {1, 2},
                             tw[2] = {ZXAPP_TRUE, ZXAPP_TRUE};
        zxapp_arg_t ba2[7] = {{"patient", pat, 2}, {"kind", kind, 2},  {"item", item, 2},
                              {"day", day, 2},     {"minute", min, 2}, {"carer", carer, 2},
                              {"value", two, 2}};
        CHECK(wave_same(app, "care_node_ok", ba2, 7, tw, 2, true),
              "Poultice: both readings are nodes of their own, both kept");
    }
}
