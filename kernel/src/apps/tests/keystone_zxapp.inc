/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Keystone's languages against the C its model runs (keystone_rules.h).
 * Slots: @logic.keystone_setting_ok, @logic.keystone_energy_budget_ok and,
 * used as it is, Crown's @num.crown_allot. */
#include "crown_rules.h"
#include "keystone_rules.h"

APP_TEST(keystone, 11, 4)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* setting_ok (KS1): Eve = KRON = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, KEYSTONE_LEVEL_MAX + 2);
        b[i] = wrand(-2, KEYSTONE_LEVEL_MAX + 2);
        c[i] = wrand(KEYSTONE_TEXT_MIN - 2, KEYSTONE_TEXT_MAX + 2);
    }
    a[0] = 0, b[0] = 0, c[0] = KEYSTONE_TEXT_MIN;                                   /* least */
    a[1] = KEYSTONE_LEVEL_MAX, b[1] = KEYSTONE_LEVEL_MAX, c[1] = KEYSTONE_TEXT_MAX; /* max */
    a[2] = KEYSTONE_LEVEL_MAX + 1, b[2] = 50, c[2] = 100;                           /* max + 1 */
    a[3] = 50, b[3] = KEYSTONE_LEVEL_MAX + 1, c[3] = 100;
    a[4] = 50, b[4] = 50, c[4] = KEYSTONE_TEXT_MAX + 1;
    a[5] = -1, b[5] = 50, c[5] = 100; /* negative */
    a[6] = 50, b[6] = -1, c[6] = 100;
    a[7] = 50, b[7] = 50, c[7] = KEYSTONE_TEXT_MIN - 1; /* below half size */
    a[8] = 50, b[8] = 50, c[8] = 0;
    a[9] = -1000000000000, b[9] = 1000000000000, c[9] = -100; /* far out, as KRON holds */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(keystone_setting_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE,
          "Keystone: the C setting limits' own edges (KS1)");
    zxapp_arg_t sa[3] = {
        {"brightness", a, WAVE_ROWS}, {"volume", b, WAVE_ROWS}, {"textsize", c, WAVE_ROWS}};
    CHECK(wave_same(app, "setting_ok", sa, 3, want, WAVE_ROWS, true),
          "Keystone: setting_ok, Eve = KRON = C");

    /* energy_budget_ok (KS2): Eve = KRON = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4 ? wrand(1, KEYSTONE_WINDOW_MAX) : wrand(-1, KEYSTONE_WINDOW_MAX + 2);
        c[i] = i % 5 ? wrand(1, KEYSTONE_MJ_MAX) : wrand(-1, KEYSTONE_MJ_MAX + 2);
        b[i] = wrand(-1, c[i] > 0 && c[i] <= KEYSTONE_MJ_MAX ? c[i] : KEYSTONE_MJ_MAX + 2);
    }
    a[0] = 1, b[0] = 1, c[0] = 1;                                               /* least */
    a[1] = KEYSTONE_WINDOW_MAX, b[1] = KEYSTONE_MJ_MAX, c[1] = KEYSTONE_MJ_MAX; /* max */
    a[2] = 0, b[2] = 5, c[2] = 5;                                               /* clear */
    a[3] = KEYSTONE_WINDOW_MAX + 1, b[3] = 5, c[3] = 5;                         /* window + 1 */
    a[4] = 1000, b[4] = 0, c[4] = 5;                                            /* nothing */
    a[5] = 1000, b[5] = KEYSTONE_MJ_MAX + 1, c[5] = KEYSTONE_MJ_MAX + 1;        /* 9(9) + 1 */
    a[6] = 1000, b[6] = 10, c[6] = 10;                                          /* all of it */
    a[7] = -1, b[7] = -1, c[7] = -1;                                            /* negative */
    a[8] = 1000, b[8] = 10, c[8] = 0;                                           /* no device */
    a[9] = 1000000000000, b[9] = -1000000000000, c[9] = 1000000000000;          /* far out */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(keystone_energy_budget_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE && !keystone_energy_budget_ok(1000, 10, 9),
          "Keystone: the C budget limits' own edges (KS2)");
    zxapp_arg_t ba[3] = {
        {"window", a, WAVE_ROWS}, {"spend", b, WAVE_ROWS}, {"whole", c, WAVE_ROWS}};
    CHECK(wave_same(app, "energy_budget_ok", ba, 3, want, WAVE_ROWS, true),
          "Keystone: energy_budget_ok, Eve = KRON = C");
    {
        /* a spend past the whole: Eve holds each range alone (TRUE), KRON relates them */
        static const int64_t w2[2] = {1000, 1000}, s2[2] = {5, 10}, t2[2] = {9, 9};
        zxapp_arg_t xa[3] = {{"window", w2, 2}, {"spend", s2, 2}, {"whole", t2, 2}};
        CHECK(wave_lp(app, "energy_budget_ok", xa, 3) == ZXAPP_BOTH,
              "Keystone: an app's budget past the device's: Eve TRUE, KRON FALSE: BOTH");
        static const int64_t s3[2] = {5, KEYSTONE_MJ_MAX + 1}, t3[2] = {9, KEYSTONE_MJ_MAX + 1};
        zxapp_arg_t ya[3] = {{"window", w2, 2}, {"spend", s3, 2}, {"whole", t3, 2}};
        CHECK(wave_lp(app, "energy_budget_ok", ya, 3) != ZXAPP_BOTH,
              "Keystone: past 9(9) millijoules: both refuse, no disagreement");
    }

    /* crown.allot (KS3): the device's budget split across apps, Sutra = Trebago = C */
    static const int64_t whole_of[4] = {1, 1000, 86400, KEYSTONE_MJ_MAX};
    for (uint32_t u = 0; u < 4; u++) {
        int64_t whole = whole_of[u], total = 0;
        uint32_t n = 2u + u * 4u; /* 2, 6, 10 and 14 apps */
        for (uint32_t i = 0; i < n; i++) a[i] = u == 3 ? wrand(1, 1000000) : wrand(0, 9);
        a[0] = u == 0 ? 0 : a[0] + 1; /* an app with no weight, then one with some */
        a[1] = a[1] + 1;
        CHECK(crown_allot(whole, a, n, want, b, &total) == 0, "Keystone: split %u in range", u);
        int64_t sum = 0;
        for (uint32_t i = 0; i < n; i++) sum += want[i];
        CHECK(sum == whole, "Keystone: split %u sums to the device's budget exactly", u);
        int64_t un[1] = {whole}, tn[1] = {total};
        zxapp_arg_t aa[4] = {{"units", un, 1}, {"weight", a, n}, {"total", tn, 1}, {"extra", b, n}};
        CHECK(wave_same(app, "crown.allot", aa, 4, want, n, false),
              "Keystone: %lld mJ across %u apps through crown.allot, Sutra = Trebago = C",
              (long long) whole, n);
    }
    {
        /* a total the weights do not give: Sutra answers with it, Trebago refuses: held */
        static const int64_t un[1] = {1000}, w3[3] = {1, 1, 1}, t4[1] = {4}, e3[3] = {1, 0, 0};
        zxapp_arg_t aa[4] = {
            {"units", un, 1}, {"weight", w3, 3}, {"total", t4, 1}, {"extra", e3, 3}};
        CHECK(wave_lp(app, "crown.allot", aa, 4) == ZXAPP_BOTH,
              "Keystone: a wrong total: Sutra answers, Trebago refuses: BOTH");
    }
}
