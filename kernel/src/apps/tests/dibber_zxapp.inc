/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Dibber's languages, witnessed, row by row against dibber_rules.h, the C
 * the model runs; the parts it composes (furrow, solstice, weatherglass,
 * understory) resolved and called; and the BOTH of bed 4, read at 18 and
 * at 41 percent on one day. Slots: @num.dibber_bed_water_litres,
 * @logic.dibber_water_now_ok, @logic.dibber_watering_limit_ok,
 * @num.dibber_seed_supply_cost, @num.dibber_harvest_gdd_days,
 * @num.dibber_bed_family_repeats, @logic.dibber_plot_bed_record_ok; and,
 * used, @num.furrow_range_total, @num.solstice_next_occurrence,
 * @num.weatherglass_temp_f_tenths, @num.weatherglass_warn_flags,
 * @logic.weatherglass_forecast_ok, @logic.understory_over_budget,
 * @num.understory_rolling_total. */
#include "dibber_rules.h"

/* weatherglass_model.c's warnings and reading record, written out here
 * (this file cannot include a model's header: it reaches zxm.h) */
static int64_t dib_warnings(int64_t temp, int64_t wind)
{
    if (temp < -1000 || temp > 1000 || wind < 0 || wind > 2000) return -1;
    return (temp <= 0 ? 1 : 0) + (temp >= 350 ? 2 : 0) + (wind >= 172 ? 4 : 0);
}

static bool dib_reading_ok(int64_t stamp, int64_t temp, int64_t wind, int64_t rain)
{
    return stamp >= -99999999999 && stamp <= 99999999999 && temp >= -9999 && temp <= 9999 &&
           wind >= -9999 && wind <= 9999 && rain >= -99999 && rain <= 99999;
}

/* bed_water_litres witnessed: every row the reduced rational of DR1, and the
 * two languages agree (TRUE) */
static bool dib_litres(zxapp_t *a, const int64_t *area, const int64_t *depth, uint32_t rows)
{
    zxapp_arg_t xa[2] = {{"area", area, rows}, {"depth", depth, rows}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "bed_water_litres", xa, 2, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    bed_water_litres: rc %d lp %d n %u (%s)\n", rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++) {
        int64_t num = 0, den = 0;
        if (!dibber_bed_litres(area[i], depth[i], &num, &den) || o[i].num != num ||
            o[i].den != den) {
            printf("    bed_water_litres row %u: %lld/%lld, want %lld/%lld\n", i,
                   (long long) o[i].num, (long long) o[i].den, (long long) num, (long long) den);
            return false;
        }
    }
    return true;
}

APP_TEST(dibber, 46, 149)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[7][2] = {{"furrow.range_total", "furrow"},
                                           {"solstice.next_occurrence", "solstice"},
                                           {"weatherglass.temp_f_tenths", "weatherglass"},
                                           {"weatherglass.warn_flags", "weatherglass"},
                                           {"weatherglass.forecast_ok", "weatherglass"},
                                           {"understory.over_budget", "understory"},
                                           {"understory.rolling_total", "understory"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 10 && app->n == 46u &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Dibber: composed of 4 parts, 10 own lines and %u of theirs, within %u", app->n - 10u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 7u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Dibber: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* DR1: bed_water_litres, Trebago = KRON = C, exact */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 5000) : wrand(0, DIBBER_AREA_MAX);
        b[i] = i % 3u ? wrand(0, 60) : wrand(0, DIBBER_DEPTH_MAX);
    }
    a[0] = 0, b[0] = 0, a[1] = DIBBER_AREA_MAX, b[1] = DIBBER_DEPTH_MAX, a[2] = 3, b[2] = 1;
    a[3] = 100, b[3] = 25;
    CHECK(dib_litres(app, a, b, WAVE_ROWS), "Dibber: bed_water_litres, Trebago = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) { /* whole litres */
        a[i] = 100 * wrand(0, 9999);
        b[i] = wrand(0, DIBBER_DEPTH_MAX);
        want[i] = a[i] / 100 * b[i];
    }
    zxapp_arg_t la[2] = {{"area", a, WAVE_ROWS}, {"depth", b, WAVE_ROWS}};
    CHECK(wave_same(app, "bed_water_litres", la, 2, want, WAVE_ROWS, false),
          "Dibber: bed_water_litres in whole litres");
    {
        static const int64_t one[2] = {1, 1}, abig[2] = {1, DIBBER_AREA_MAX + 1},
                             dbig[2] = {1, DIBBER_DEPTH_MAX + 1}, dneg[2] = {1, -1};
        int64_t n = 0, dd = 0;
        zxapp_arg_t w1[2] = {{"area", abig, 2}, {"depth", one, 2}};
        zxapp_arg_t w2[2] = {{"area", one, 2}, {"depth", dbig, 2}};
        zxapp_arg_t w3[2] = {{"area", one, 2}, {"depth", dneg, 2}};
        CHECK(!dibber_bed_litres(DIBBER_AREA_MAX + 1, 1, &n, &dd) &&
                  !dibber_bed_litres(1, -1, &n, &dd) &&
                  wave_lp(app, "bed_water_litres", w1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "bed_water_litres", w2, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "bed_water_litres", w3, 2) == ZXAPP_BOTH,
              "Dibber: a bed or a depth outside: Trebago refuses, KRON answers: BOTH");
    }

    /* DR2: water_now_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-500000, 500000) : wrand(-DIBBER_EXCESS_MAX, DIBBER_EXCESS_MAX);
        b[i] = i % 3u ? 0 : wrand(0, 1);
        want[i] = T_(dibber_water_now_ok(a[i], b[i]));
    }
    a[0] = 0, b[0] = 0, a[1] = 1, b[1] = 0, a[2] = -DIBBER_EXCESS_MAX, b[2] = 0;
    a[3] = -5, b[3] = 1;
    for (uint32_t i = 0; i < 4u; i++) want[i] = T_(dibber_water_now_ok(a[i], b[i]));
    zxapp_arg_t wa[2] = {{"excess", a, WAVE_ROWS}, {"frost", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "water_now_ok", wa, 2, want, WAVE_ROWS, true),
          "Dibber: water_now_ok, Eve = KRON = C");
    {
        static const int64_t z[2] = {0, 0}, far[2] = {0, -DIBBER_EXCESS_MAX - 1};
        zxapp_arg_t f1[2] = {{"excess", far, 2}, {"frost", z, 2}};
        CHECK(!dibber_water_now_ok(-DIBBER_EXCESS_MAX - 1, 0) &&
                  wave_lp(app, "water_now_ok", f1, 2) == ZXAPP_BOTH,
              "Dibber: an excess past 18 digits: Eve no, KRON yes: BOTH");
    }

    /* DR3: watering_limit_ok, Eve = C (one language: unwitnessed) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 45);
        b[i] = wrand(-2, 16);
        want[i] = T_(dibber_watering_limit_ok(a[i], b[i]));
    }
    a[0] = 1, b[0] = 1, a[1] = DIBBER_ONE_MAX, b[1] = DIBBER_TIMES_MAX;
    a[2] = DIBBER_ONE_MAX + 1, b[2] = 1;
    for (uint32_t i = 0; i < 3u; i++) want[i] = T_(dibber_watering_limit_ok(a[i], b[i]));
    zxapp_arg_t ea[2] = {{"depth", a, WAVE_ROWS}, {"times", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "watering_limit_ok", ea, 2, want, WAVE_ROWS, true),
          "Dibber: watering_limit_ok, Eve = C");

    /* DR4: seed_supply_cost, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 200) : wrand(0, DIBBER_COUNT_MAX);
        b[i] = i % 3u ? wrand(0, 5000) : wrand(0, DIBBER_PRICE_MAX);
    }
    a[0] = 0, b[0] = 0, a[1] = DIBBER_COUNT_MAX, b[1] = DIBBER_PRICE_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dibber_supply_cost(a[i], b[i]);
    zxapp_arg_t sa[2] = {{"count", a, WAVE_ROWS}, {"price", b, WAVE_ROWS}};
    CHECK(want[1] == INT64_C(999998999000001) &&
              wave_same(app, "seed_supply_cost", sa, 2, want, WAVE_ROWS, false),
          "Dibber: seed_supply_cost, Sutra = Trebago = C");
    {
        static const int64_t one[2] = {1, 1}, cbig[2] = {1, DIBBER_COUNT_MAX + 1},
                             pbig[2] = {1, DIBBER_PRICE_MAX + 1};
        zxapp_arg_t s1[2] = {{"count", cbig, 2}, {"price", one, 2}};
        zxapp_arg_t s2[2] = {{"count", one, 2}, {"price", pbig, 2}};
        CHECK(dibber_supply_cost(DIBBER_COUNT_MAX + 1, 1) == DIBBER_REFUSED &&
                  wave_lp(app, "seed_supply_cost", s1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "seed_supply_cost", s2, 2) == ZXAPP_BOTH,
              "Dibber: past Sutra's pictures: Sutra refuses, Trebago answers: BOTH");
    }

    /* DR5: harvest_gdd_days, Trebago = C, sixteen runs of 64 days */
    for (uint32_t t = 0; t < 16u; t++) {
        int64_t base = t ? wrand(0, DIBBER_BASE_MAX) : 100, total = 0, need;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-DIBBER_MEAN_MAX, DIBBER_MEAN_MAX);
            if (t == 1) a[i] = i % 2u ? DIBBER_MEAN_MAX : -DIBBER_MEAN_MAX;
            total += a[i] > base ? a[i] - base : 0;
        }
        if (!total) a[0] = base + 1, total = 1;
        need = t == 2 ? total : wrand(1, total < DIBBER_NEED_MAX ? total : DIBBER_NEED_MAX);
        int64_t w = dibber_harvest_days(a, WAVE_ROWS, base, need);
        zxapp_arg_t ha[3] = {{"mean", a, WAVE_ROWS}, {"base", &base, 1}, {"need", &need, 1}};
        CHECK(w >= 1 && w <= (int64_t) WAVE_ROWS &&
                  wave_same(app, "harvest_gdd_days", ha, 3, &w, 1, false),
              "Dibber: harvest_gdd_days base %lld need %lld = day %lld, Trebago = C",
              (long long) base, (long long) need, (long long) w);
    }
    {
        static const int64_t cold[3] = {50, 60, 70};
        int64_t base = 100, need = 1;
        zxapp_arg_t ha[3] = {{"mean", cold, 3}, {"base", &base, 1}, {"need", &need, 1}};
        CHECK(dibber_harvest_days(cold, 3, 100, 1) == DIBBER_REFUSED &&
                  wave_lp(app, "harvest_gdd_days", ha, 3) != ZXAPP_TRUE,
              "Dibber: never warm enough in the days given: no date");
    }

    /* DR6: bed_family_repeats, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 6);
        b[i] = wrand(2020, 2027);
        c[i] = wrand(1, 9);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dibber_family_repeats(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {{"bed", a, WAVE_ROWS}, {"year", b, WAVE_ROWS}, {"family", c, WAVE_ROWS}};
    CHECK(wave_same(app, "bed_family_repeats", ga, 3, want, WAVE_ROWS, false),
          "Dibber: bed_family_repeats, KRON = C");

    /* DR7: plot_bed_record_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(1, DIBBER_PLOT_MAX) : wrand(-1200000, 1200000);
        b[i] = i % 5u ? wrand(1, DIBBER_BED_MAX) : wrand(-12000, 12000);
        c[i] = i % 6u ? wrand(1, DIBBER_AREA_MAX) : wrand(-INT64_C(1200000000), 1200000000);
        want[i] = T_(dibber_plot_record_ok(a[i], b[i], c[i]));
    }
    a[0] = 1, b[0] = 4, c[0] = 120, a[1] = DIBBER_PLOT_MAX + 1, b[1] = 1, c[1] = 1;
    for (uint32_t i = 0; i < 2u; i++) want[i] = T_(dibber_plot_record_ok(a[i], b[i], c[i]));
    zxapp_arg_t pa[3] = {{"plot", a, WAVE_ROWS}, {"bed", b, WAVE_ROWS}, {"area", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "plot_bed_record_ok", pa, 3, want, WAVE_ROWS, true),
          "Dibber: plot_bed_record_ok, MYRA = C");

    /* the parts, called through Dibber as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 64);
        b[i] = wrand(1, 20);
        c[i] = b[i] + wrand(0, 40);
        d[i] = wrand(0, INT64_C(99999900)); /* a bed's week, centilitres */
    }
    {
        int64_t tot = 0;
        zxapp_arg_t sh[4] = {{"pos", a, WAVE_ROWS},
                             {"lo", b, WAVE_ROWS},
                             {"hi", c, WAVE_ROWS},
                             {"cell", d, WAVE_ROWS}};
        CHECK(furrow_range_total(a, b, c, d, WAVE_ROWS, &tot) &&
                  wave_same(app, "furrow.range_total", sh, 4, &tot, 1, false),
              "Dibber: furrow.range_total, the bed sheet's column");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SOLSTICE_DAY_MAX / 2);
        b[i] = wrand(1, 28);
        c[i] = a[i] + wrand(-30, 400);
        if (c[i] < 0) c[i] = 0;
        want[i] = solstice_next(a[i], b[i], c[i]);
    }
    zxapp_arg_t na[3] = {{"start", a, WAVE_ROWS}, {"every", b, WAVE_ROWS}, {"today", c, WAVE_ROWS}};
    CHECK(wave_same(app, "solstice.next_occurrence", na, 3, want, WAVE_ROWS, false),
          "Dibber: solstice.next_occurrence, a repeating task's next day");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 5 * wrand(-200, 200); /* whole tenths of a degree F */
        want[i] = a[i] * 9 / 5 + 320;
    }
    zxapp_arg_t ta[1] = {{"celsius", a, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.temp_f_tenths", ta, 1, want, WAVE_ROWS, false),
          "Dibber: weatherglass.temp_f_tenths, the frost shown in F");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(0, 2000);
        if (i == 0) a[i] = 0, b[i] = 0;
        if (i == 1) a[i] = -15, b[i] = 172;
        want[i] = dib_warnings(a[i], b[i]);
    }
    zxapp_arg_t fa[2] = {{"temp", a, WAVE_ROWS}, {"wind", b, WAVE_ROWS}};
    CHECK(want[0] == 1 && want[1] == 5 &&
              wave_same(app, "weatherglass.warn_flags", fa, 2, want, WAVE_ROWS, false),
          "Dibber: weatherglass.warn_flags, the frost warning");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(120000000000));
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(0, 12000);
        d[i] = wrand(0, 120000);
        want[i] = T_(dib_reading_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ra[4] = {{"stamp", a, WAVE_ROWS},
                         {"temp", b, WAVE_ROWS},
                         {"wind", c, WAVE_ROWS},
                         {"rain", d, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.forecast_ok", ra, 4, want, WAVE_ROWS, true),
          "Dibber: weatherglass.forecast_ok, a reading that fits");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-INT64_C(9999999999), INT64_C(9999999999));
        if (i == 0) a[i] = 0;
        if (i == 1) a[i] = 1;
        want[i] = T_(understory_over_budget(a[i]));
    }
    zxapp_arg_t oa[1] = {{"excess", a, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE &&
              wave_same(app, "understory.over_budget", oa, 1, want, WAVE_ROWS, true),
          "Dibber: understory.over_budget, the week's water");
    {
        int64_t width = DIBBER_WEEK;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, INT64_C(99999900));
        zxapp_arg_t wk[2] = {{"amount", a, WAVE_ROWS}, {"width", &width, 1}};
        CHECK(understory_rolling(a, WAVE_ROWS, width, want) == 0 &&
                  wave_same(app, "understory.rolling_total", wk, 2, want, WAVE_ROWS, false),
              "Dibber: understory.rolling_total, the week of 7 days");
    }

    /* DR8, the BOTH test: bed 4 read at 18 and at 41 percent on one day:
     * BOTH, the planting suggestion held until a re-test */
    {
        static const int64_t two[2] = {18, 41}, dry[1] = {18}, moist[2] = {41, 55};
        lpres_state_t st = dibber_soil_state(two, 2);
        CHECK(st == LPRES_STATE_BOTH && (int) st == 3 &&
                  lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Dibber: bed 4 at 18 and 41 percent on one day: BOTH");
        CHECK(dibber_soil_state(dry, 1) == LPRES_STATE_FALSE &&
                  dibber_soil_state(moist, 2) == LPRES_STATE_TRUE &&
                  dibber_soil_state(NULL, 0) == LPRES_STATE_NEITHER,
              "Dibber: readings that agree, or none: FALSE, TRUE, NEITHER");
    }
}
