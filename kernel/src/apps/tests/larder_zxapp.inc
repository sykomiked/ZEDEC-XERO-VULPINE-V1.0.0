/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Larder's languages against the C its model runs (larder_rules.h).
 * Slots: @num.larder_category_spent, @logic.larder_category_over,
 * @num.larder_unspent_carry; the month's totals are @num.coffer_holdings. */
#include "larder_rules.h"

APP_TEST(larder, 10, 82)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* category_spent (LR1): Sutra = Trebago = C over every size of balance */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 1000000 : LARDER_MINOR_MAX);
        b[i] = wrand(0, i < 32 ? 1000000 : LARDER_MINOR_MAX);
    }
    a[0] = 0, b[0] = 0;                               /* nothing spent */
    a[1] = 0, b[1] = LARDER_MINOR_MAX;                /* the most spent */
    a[2] = LARDER_MINOR_MAX, b[2] = 0;                /* the most refunded */
    a[3] = LARDER_MINOR_MAX, b[3] = LARDER_MINOR_MAX; /* both at the max */
    a[4] = 500, b[4] = 200;                           /* a net refund */
    a[5] = 100, b[5] = 350;                           /* spent 250 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = larder_category_spent(a[i], b[i]);
        CHECK(want[i] != LARDER_REFUSED, "Larder: spent row %u in range", i);
    }
    CHECK(want[0] == 0 && want[1] == LARDER_MINOR_MAX && want[2] == -LARDER_MINOR_MAX &&
              want[3] == 0 && want[4] == -300 && want[5] == 250,
          "Larder: category_spent, the C's own edges");
    zxapp_arg_t sa[2] = {{"opening", a, WAVE_ROWS}, {"closing", b, WAVE_ROWS}};
    CHECK(wave_same(app, "category_spent", sa, 2, want, WAVE_ROWS, false),
          "Larder: category_spent, Sutra = Trebago = C");
    {
        /* max + 1: a balance past 18 digits: Sutra refuses, Trebago answers */
        static const int64_t o[2] = {0, 0}, c[2] = {5, LARDER_MINOR_MAX + 1};
        zxapp_arg_t xa[2] = {{"opening", o, 2}, {"closing", c, 2}};
        CHECK(larder_category_spent(0, LARDER_MINOR_MAX + 1) == LARDER_REFUSED &&
                  wave_lp(app, "category_spent", xa, 2) == ZXAPP_BOTH,
              "Larder: a balance past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* negative: a holder account below zero: Sutra refuses, Trebago answers */
        static const int64_t o2[2] = {0, -1}, c2[2] = {5, 5};
        zxapp_arg_t ya[2] = {{"opening", o2, 2}, {"closing", c2, 2}};
        CHECK(larder_category_spent(-1, 5) == LARDER_REFUSED &&
                  wave_lp(app, "category_spent", ya, 2) == ZXAPP_BOTH,
              "Larder: a holder below zero: BOTH");
        /* a difference past 64 bits: both refuse */
        static const int64_t o3[2] = {0, INT64_MIN}, c3[2] = {5, 1};
        zxapp_arg_t za[2] = {{"opening", o3, 2}, {"closing", c3, 2}};
        CHECK(larder_category_spent(INT64_MIN, 1) == LARDER_REFUSED &&
                  wave_lp(app, "category_spent", za, 2) == ZXAPP_NEITHER,
              "Larder: a difference past 64 bits: both refuse: NEITHER");
    }

    /* category_over_budget (LR2): Eve = Sutra = C, each field swept past its range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 32 ? wrand(-2, 1000000) : wrand(LARDER_MINOR_MAX - 3, LARDER_MINOR_MAX);
        b[i] = i < 32 ? wrand(-1000, 1000) : wrand(-LARDER_MINOR_MAX, LARDER_MINOR_MAX);
        want[i] = T_(larder_category_over_budget(a[i], b[i]));
    }
    a[0] = 100, b[0] = 1;                             /* one minor unit over */
    a[1] = 100, b[1] = 0;                             /* exactly at the budget */
    a[2] = 0, b[2] = 500;                             /* no budget set: never over */
    a[3] = LARDER_MINOR_MAX, b[3] = LARDER_MINOR_MAX; /* both at the max */
    a[4] = -5, b[4] = 10;                             /* a negative budget */
    a[5] = 1, b[5] = -LARDER_MINOR_MAX;               /* far within */
    for (uint32_t i = 0; i < 6; i++) want[i] = T_(larder_category_over_budget(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Larder: category_over_budget, the C's own edges");
    zxapp_arg_t oa[2] = {{"budget", a, WAVE_ROWS}, {"excess", b, WAVE_ROWS}};
    CHECK(wave_same(app, "category_over", oa, 2, want, WAVE_ROWS, true),
          "Larder: category_over_budget, Eve = Sutra = C");
    {
        /* max + 1: an excess past 18 digits: Eve says FALSE, Sutra refuses */
        static const int64_t bu[2] = {100, 100}, ex[2] = {1, LARDER_MINOR_MAX + 1};
        zxapp_arg_t xa[2] = {{"budget", bu, 2}, {"excess", ex, 2}};
        CHECK(!larder_category_over_budget(100, LARDER_MINOR_MAX + 1) &&
                  wave_lp(app, "category_over", xa, 2) == ZXAPP_BOTH,
              "Larder: an excess past 18 digits: Eve FALSE, Sutra refuses: BOTH");
        /* a budget past 18 digits: the same */
        static const int64_t bu2[2] = {100, LARDER_MINOR_MAX + 1}, ex2[2] = {1, 1};
        zxapp_arg_t ya[2] = {{"budget", bu2, 2}, {"excess", ex2, 2}};
        CHECK(!larder_category_over_budget(LARDER_MINOR_MAX + 1, 1) &&
                  wave_lp(app, "category_over", ya, 2) == ZXAPP_BOTH,
              "Larder: a budget past 18 digits: BOTH");
    }

    /* unspent_carry (LR3): 64 months of eight categories, each an exact sum, Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t bu[8], sp[8];
        for (uint32_t i = 0; i < 8; i++) {
            bu[i] = r == 0   ? 0
                    : r == 1 ? LARDER_MINOR_MAX
                             : wrand(0, r < 32 ? 100000 : LARDER_MINOR_MAX);
            sp[i] = r == 0   ? 0
                    : r == 1 ? (i ? 0 : -LARDER_MINOR_MAX) /* a refund of 18 digits */
                             : wrand(-1000, r < 32 ? 150000 : LARDER_MINOR_MAX);
        }
        int64_t s = larder_unspent_carry(bu, sp, 8);
        CHECK(s != LARDER_REFUSED, "Larder: carry month %u in range", r);
        zxapp_arg_t ca[2] = {{"budget", bu, 8}, {"spent", sp, 8}};
        CHECK(wave_same(app, "unspent_carry", ca, 2, &s, 1, false),
              "Larder: unspent_carry month %u, Trebago = C", r);
    }
    {
        static const int64_t bu[4] = {1000, 500, 0, 300}, sp[4] = {400, 700, 50, -100},
                             w[1] = {1000};
        zxapp_arg_t ca[2] = {{"budget", bu, 4}, {"spent", sp, 4}};
        CHECK(larder_unspent_carry(bu, sp, 4) == 1000 &&
                  wave_same(app, "unspent_carry", ca, 2, w, 1, false),
              "Larder: an overspent category carries nothing and takes nothing");
        static const int64_t top[2] = {INT64_MAX, 0}, z[2] = {0, 0}, tw[1] = {INT64_MAX};
        zxapp_arg_t ta[2] = {{"budget", top, 2}, {"spent", z, 2}};
        CHECK(wave_same(app, "unspent_carry", ta, 2, tw, 1, false),
              "Larder: unspent_carry at INT64_MAX");
        /* a sum past 64 bits, a budget below 0, a difference past 64 bits: refused */
        static const int64_t ob[2] = {INT64_MAX, 1};
        zxapp_arg_t xa[2] = {{"budget", ob, 2}, {"spent", z, 2}};
        CHECK(larder_unspent_carry(ob, z, 2) == LARDER_REFUSED &&
                  wave_lp(app, "unspent_carry", xa, 2) == ZXAPP_NEITHER,
              "Larder: unspent_carry past 64 bits: refused");
        static const int64_t nb[2] = {5, -1};
        zxapp_arg_t ya[2] = {{"budget", nb, 2}, {"spent", z, 2}};
        CHECK(larder_unspent_carry(nb, z, 2) == LARDER_REFUSED &&
                  wave_lp(app, "unspent_carry", ya, 2) == ZXAPP_NEITHER,
              "Larder: a budget below 0: refused");
        static const int64_t db[2] = {1, 0}, ds[2] = {INT64_MIN, 0};
        zxapp_arg_t za[2] = {{"budget", db, 2}, {"spent", ds, 2}};
        CHECK(larder_unspent_carry(db, ds, 2) == LARDER_REFUSED &&
                  wave_lp(app, "unspent_carry", za, 2) == ZXAPP_NEITHER,
              "Larder: a difference past 64 bits: refused");
    }

    /* the month's totals are Coffer's exact sum (coffer.holdings) */
    {
        static const int64_t sp[3] = {40000, 12500, -300}, w[1] = {52200};
        zxapp_arg_t ha = {"balance", sp, 3};
        CHECK(wave_same(app, "coffer.holdings", &ha, 1, w, 1, false),
              "Larder: the month's spending through coffer.holdings");
    }
}
