/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Understory's languages against the C its model runs (understory_rules.h).
 * Slots: @num.understory_energy_cost, @logic.understory_over_budget,
 * @num.understory_rolling_total, @logic.understory_water_entry_ok and
 * @logic.understory_land_entry_ok. */
#include "understory_rules.h"

APP_TEST(understory, 21, 93)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* energy_cost (UR1): Sutra = C++ = C inside the pictures, every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, UNDERSTORY_MJ_MAX);
        b[i] = wrand(0, UNDERSTORY_TARIFF_MAX);
    }
    a[0] = 0, b[0] = 0;                                     /* nothing */
    a[1] = UNDERSTORY_MJ_MAX, b[1] = UNDERSTORY_TARIFF_MAX; /* max */
    a[2] = UNDERSTORY_MJ_PER_KWH, b[2] = 25;                /* one kWh */
    a[3] = UNDERSTORY_MJ_PER_KWH - 1, b[3] = 1;             /* just under: floored to 0 */
    a[4] = UNDERSTORY_MJ_PER_KWH / 2, b[4] = 25;            /* half: 12, never rounded up */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = understory_energy_cost(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 277777499 && want[2] == 25 && want[3] == 0 && want[4] == 12 &&
              understory_energy_cost(UNDERSTORY_MJ_MAX + 1, 1) < 0 &&
              understory_energy_cost(1, UNDERSTORY_TARIFF_MAX + 1) < 0 &&
              understory_energy_cost(-1, 1) < 0 && understory_energy_cost(1, -1) < 0,
          "Understory: the C energy cost's own edges (UR1)");
    zxapp_arg_t ca[2] = {{"mj", a, WAVE_ROWS}, {"tariff", b, WAVE_ROWS}};
    CHECK(wave_same(app, "energy_cost", ca, 2, want, WAVE_ROWS, false),
          "Understory: energy_cost, Sutra = C++ = C");
    {
        /* past 9(12) mJ: Sutra refuses, C++ answers to 64 bits */
        static const int64_t m2[2] = {3600000000, UNDERSTORY_MJ_MAX + 1}, t2[2] = {25, 25};
        zxapp_arg_t xa[2] = {{"mj", m2, 2}, {"tariff", t2, 2}};
        CHECK(wave_lp(app, "energy_cost", xa, 2) == ZXAPP_BOTH,
              "Understory: energy past 9(12): Sutra refuses, C++ answers: BOTH");
        /* a negative tariff: both refuse, no disagreement */
        static const int64_t m3[2] = {3600000000, 3600000000}, t3[2] = {25, -1};
        zxapp_arg_t ya[2] = {{"mj", m3, 2}, {"tariff", t3, 2}};
        CHECK(wave_lp(app, "energy_cost", ya, 2) != ZXAPP_BOTH &&
                  wave_lp(app, "energy_cost", ya, 2) != ZXAPP_TRUE,
              "Understory: a negative tariff is refused by both");
    }

    /* over_budget (UR2): Eve = KRON = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = i % 2 ? wrand(-UNDERSTORY_EXCESS_MAX, 0) : wrand(1, UNDERSTORY_EXCESS_MAX);
    a[0] = 0;                     /* exactly the budget */
    a[1] = 1;                     /* one past */
    a[2] = -1;                    /* one under */
    a[3] = UNDERSTORY_EXCESS_MAX; /* max */
    a[4] = -UNDERSTORY_EXCESS_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(understory_over_budget(a[i]));
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE &&
              !understory_over_budget(UNDERSTORY_EXCESS_MAX + 1),
          "Understory: the C budget rule's own edges (UR2)");
    zxapp_arg_t oa[1] = {{"excess", a, WAVE_ROWS}};
    CHECK(wave_same(app, "over_budget", oa, 1, want, WAVE_ROWS, true),
          "Understory: over_budget, Eve = KRON = C");
    {
        /* an excess past 18 digits: Eve refuses it (FALSE), KRON reads it as over */
        static const int64_t e2[2] = {5, UNDERSTORY_EXCESS_MAX + 1};
        zxapp_arg_t xa[1] = {{"excess", e2, 2}};
        CHECK(wave_lp(app, "over_budget", xa, 1) == ZXAPP_BOTH,
              "Understory: an excess past 18 digits: Eve FALSE, KRON TRUE: BOTH");
    }

    /* rolling_total (UR3): Trebago = C (one language: NEITHER), widths 1, 3, 7 and past n */
    static const int64_t widths[4] = {1, 3, 7, WAVE_ROWS + 5};
    for (uint32_t w = 0; w < 4; w++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 999999999999);
        a[0] = 0;
        a[1] = 999999999999;
        int64_t wd[1] = {widths[w]};
        CHECK(understory_rolling(a, WAVE_ROWS, wd[0], want) == 0, "Understory: rolling in range");
        zxapp_arg_t ra[2] = {{"amount", a, WAVE_ROWS}, {"width", wd, 1}};
        CHECK(wave_same(app, "rolling_total", ra, 2, want, WAVE_ROWS, false),
              "Understory: rolling_total over %lld readings, Trebago = C", (long long) wd[0]);
    }
    {
        int64_t out[3] = {0, 0, 0};
        static const int64_t am[3] = {5, -1, 7}, big[2] = {INT64_MAX, 1}, ok3[3] = {1, 2, 3};
        static const int64_t w0[1] = {0}, w2[1] = {2}, wmax[1] = {UNDERSTORY_WIDTH_MAX + 1};
        zxapp_arg_t na[2] = {{"amount", am, 3}, {"width", w2, 1}};
        zxapp_arg_t za[2] = {{"amount", ok3, 3}, {"width", w0, 1}};
        zxapp_arg_t wa[2] = {{"amount", ok3, 3}, {"width", wmax, 1}};
        zxapp_arg_t ba[2] = {{"amount", big, 2}, {"width", w2, 1}};
        CHECK(understory_rolling(am, 3, 2, out) < 0 && understory_rolling(ok3, 3, 0, out) < 0 &&
                  understory_rolling(ok3, 3, UNDERSTORY_WIDTH_MAX + 1, out) < 0 &&
                  understory_rolling(big, 2, 2, out) < 0 && out[0] == 0,
              "Understory: the C refuses a negative amount, a width outside 1..999999 and a sum "
              "past 64 bits, writing nothing");
        CHECK(wave_lp(app, "rolling_total", na, 2) != ZXAPP_TRUE &&
                  wave_lp(app, "rolling_total", na, 2) != ZXAPP_BOTH &&
                  wave_lp(app, "rolling_total", za, 2) != ZXAPP_TRUE &&
                  wave_lp(app, "rolling_total", wa, 2) != ZXAPP_TRUE &&
                  wave_lp(app, "rolling_total", ba, 2) != ZXAPP_TRUE,
              "Understory: Trebago refuses them too");
    }

    /* water_entry_ok (UR4): MYRA = C, every edge, sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-UNDERSTORY_DAY_MAX - 2, UNDERSTORY_DAY_MAX + 2);
        b[i] = wrand(-11, 11);
        c[i] = wrand(-UNDERSTORY_LITRES_MAX - 2, UNDERSTORY_LITRES_MAX + 2);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;
    a[1] = UNDERSTORY_DAY_MAX, b[1] = 4, c[1] = UNDERSTORY_LITRES_MAX;
    a[2] = UNDERSTORY_DAY_MAX + 1, b[2] = 1, c[2] = 1;
    a[3] = 20000, b[3] = 10, c[3] = 1;
    a[4] = 20000, b[4] = 1, c[4] = UNDERSTORY_LITRES_MAX + 1;
    a[5] = -UNDERSTORY_DAY_MAX, b[5] = -9, c[5] = -UNDERSTORY_LITRES_MAX;
    a[6] = -UNDERSTORY_DAY_MAX - 1, b[6] = 1, c[6] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(understory_water_entry_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_FALSE && !understory_water_ok(-1, 1, 1) &&
              !understory_water_ok(1, 5, 1) && !understory_water_ok(1, 1, -1) &&
              understory_water_ok(1, 4, 1),
          "Understory: the C water record's own edges, and the model's stricter meaning (UR4)");
    zxapp_arg_t wa4[3] = {
        {"day", a, WAVE_ROWS}, {"source", b, WAVE_ROWS}, {"litres", c, WAVE_ROWS}};
    CHECK(wave_same(app, "water_entry_ok", wa4, 3, want, WAVE_ROWS, true),
          "Understory: water_entry_ok, MYRA = C");

    /* land_entry_ok (UR5): MYRA = C, every edge, sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-UNDERSTORY_PARCEL_MAX - 2, UNDERSTORY_PARCEL_MAX + 2);
        b[i] = wrand(-11, 11);
        c[i] = wrand(-UNDERSTORY_AREA_MAX - 2, UNDERSTORY_AREA_MAX + 2);
    }
    a[0] = 1, b[0] = 1, c[0] = 0;
    a[1] = UNDERSTORY_PARCEL_MAX, b[1] = 5, c[1] = UNDERSTORY_AREA_MAX;
    a[2] = UNDERSTORY_PARCEL_MAX + 1, b[2] = 1, c[2] = 5;
    a[3] = 7, b[3] = 10, c[3] = 5;
    a[4] = 7, b[4] = 1, c[4] = UNDERSTORY_AREA_MAX + 1;
    a[5] = 0, b[5] = 0, c[5] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(understory_land_entry_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              !understory_land_ok(0, 1, 1) && !understory_land_ok(1, 6, 1) &&
              !understory_land_ok(1, 1, -1) && understory_land_ok(1, 5, 0),
          "Understory: the C land record's own edges, and the model's stricter meaning (UR5)");
    zxapp_arg_t la[3] = {{"parcel", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS}, {"area", c, WAVE_ROWS}};
    CHECK(wave_same(app, "land_entry_ok", la, 3, want, WAVE_ROWS, true),
          "Understory: land_entry_ok, MYRA = C");
}
