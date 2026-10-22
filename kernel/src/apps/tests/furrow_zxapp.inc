/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Furrow's languages against the C its model runs (furrow_rules.h).
 * Slots: @num.furrow_range_total, @num.furrow_money_cell,
 * @logic.furrow_formula_ok, @logic.furrow_cell_record_ok, and, used as it
 * is, @num.coffer_holdings. */
#include "furrow_rules.h"

APP_TEST(furrow, 11, 40)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* range_total (FW1): 64 ranges of one column, each an exact sum, Trebago = KRON = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t pos[16], lo[16], hi[16], cell[16], tot[1];
        uint32_t n = 2u + r % 15u;
        int64_t l = wrand(1, 12), h = l + wrand(-2, 10);
        for (uint32_t i = 0; i < n; i++) {
            pos[i] = (int64_t) i + 1;
            lo[i] = l;
            hi[i] = h;
            cell[i] = r == 0   ? 0
                      : r == 1 ? INT64_C(576460752303423487)
                      : r == 2 ? -INT64_C(576460752303423487)
                               : wrand(-1000000, 1000000);
        }
        if (r == 3) lo[0] = 1, hi[0] = FURROW_ROW_MAX; /* the whole column for one cell */
        CHECK(furrow_range_total(pos, lo, hi, cell, n, &tot[0]), "Furrow: range %u in the C", r);
        zxapp_arg_t ra[4] = {{"pos", pos, n}, {"lo", lo, n}, {"hi", hi, n}, {"cell", cell, n}};
        CHECK(wave_same(app, "range_total", ra, 4, tot, 1, false),
              "Furrow: range %u of %u cells, Trebago = KRON = C", r, n);
    }
    {
        /* every edge: below lo, at lo, at hi, past hi, an upside-down range */
        static const int64_t p[6] = {1, 2, 4, 5, 3, 0}, l[6] = {2, 2, 2, 2, 4, 0},
                             h[6] = {4, 4, 4, 4, 2, 0}, v[6] = {1000, 20, 300, 4000, 50000, 7};
        int64_t t[1] = {0};
        CHECK(furrow_range_total(p, l, h, v, 6, &t[0]) && t[0] == 327,
              "Furrow: lo and hi are inside, the rest out (FW1)");
        zxapp_arg_t ea[4] = {{"pos", p, 6}, {"lo", l, 6}, {"hi", h, 6}, {"cell", v, 6}};
        CHECK(wave_same(app, "range_total", ea, 4, t, 1, false), "Furrow: range edges");
        /* at INT64_MAX exactly, both ways */
        static const int64_t tp[2] = {1, 1}, top[2] = {INT64_MAX - 5, 5};
        static const int64_t tw[1] = {INT64_MAX};
        zxapp_arg_t ta[4] = {{"pos", tp, 2}, {"lo", tp, 2}, {"hi", tp, 2}, {"cell", top, 2}};
        CHECK(wave_same(app, "range_total", ta, 4, tw, 1, false), "Furrow: a range at INT64_MAX");
        /* from the first, INT64_MAX + 1 passes 64 bits (Trebago refuses);
         * from the last, 1 + -1 first (KRON answers INT64_MAX) */
        static const int64_t op[3] = {1, 1, 1}, over[3] = {INT64_MAX, 1, -1};
        zxapp_arg_t oa[4] = {{"pos", op, 3}, {"lo", op, 3}, {"hi", op, 3}, {"cell", over, 3}};
        CHECK(wave_lp(app, "range_total", oa, 4) == ZXAPP_BOTH,
              "Furrow: a partial sum past 64 bits one way only: BOTH");
        CHECK(!furrow_range_total(op, op, op, over, 3, &t[0]),
              "Furrow: the C refuses it as Trebago does");
        /* outside the range it is never added, so it cannot pass 64 bits */
        static const int64_t far[3] = {9, 9, 9};
        zxapp_arg_t fa[4] = {{"pos", far, 3}, {"lo", op, 3}, {"hi", op, 3}, {"cell", over, 3}};
        static const int64_t zero[1] = {0};
        CHECK(wave_same(app, "range_total", fa, 4, zero, 1, false),
              "Furrow: cells outside their range add nothing");
    }

    /* money_cell (FW2): whole units and hundredths, Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-FURROW_UNITS_MAX, FURROW_UNITS_MAX);
        b[i] = a[i] < 0 ? -wrand(0, 99) : wrand(0, 99);
    }
    a[0] = 0, b[0] = 0;                                 /* nothing */
    a[1] = FURROW_UNITS_MAX, b[1] = FURROW_CENTS_MAX;   /* every max */
    a[2] = -FURROW_UNITS_MAX, b[2] = -FURROW_CENTS_MAX; /* every least */
    a[3] = 12, b[3] = 34;                               /* 12.34 */
    a[4] = -12, b[4] = -34;                             /* -12.34 */
    a[5] = 0, b[5] = -1;                                /* -0.01 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        CHECK(furrow_money_cell(a[i], b[i], &want[i]), "Furrow: money row %u in the C", i);
    CHECK(want[1] == INT64_C(999999999999999999) && want[3] == 1234 && want[4] == -1234 &&
              want[5] == -1,
          "Furrow: money's edges in the C");
    zxapp_arg_t ma[2] = {{"units", a, WAVE_ROWS}, {"cents", b, WAVE_ROWS}};
    CHECK(wave_same(app, "money_cell", ma, 2, want, WAVE_ROWS, false),
          "Furrow: money_cell, Sutra = KRON = C");
    {
        int64_t m = 0;
        /* cents past 99: past Sutra's s9(2), refused there, KRON answers */
        static const int64_t u[2] = {1, 1}, k[2] = {0, 100};
        zxapp_arg_t ka[2] = {{"units", u, 2}, {"cents", k, 2}};
        CHECK(wave_lp(app, "money_cell", ka, 2) == ZXAPP_BOTH,
              "Furrow: 100 cents: Sutra refuses, KRON answers: BOTH");
        /* units max + 1: past s9(16) */
        static const int64_t bu[2] = {1, FURROW_UNITS_MAX + 1}, bk[2] = {0, 0};
        zxapp_arg_t ba[2] = {{"units", bu, 2}, {"cents", bk, 2}};
        CHECK(wave_lp(app, "money_cell", ba, 2) == ZXAPP_BOTH,
              "Furrow: units past 16 digits: Sutra refuses, KRON answers: BOTH");
        CHECK(!furrow_money_cell(1, 100, &m) && !furrow_money_cell(FURROW_UNITS_MAX + 1, 0, &m) &&
                  !furrow_money_cell(-FURROW_UNITS_MAX - 1, 0, &m) &&
                  !furrow_money_cell(0, -100, &m),
              "Furrow: the C refuses the same");
    }

    /* formula_ok (FW3): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-3, FURROW_DEPTH_MAX + 3);
        b[i] = wrand(-3, FURROW_REFS_MAX + 3);
    }
    a[0] = 0, b[0] = 0;                              /* every least */
    a[1] = FURROW_DEPTH_MAX, b[1] = FURROW_REFS_MAX; /* every max */
    a[2] = FURROW_DEPTH_MAX + 1, b[2] = 0;           /* depth max + 1 */
    a[3] = 0, b[3] = FURROW_REFS_MAX + 1;            /* refs max + 1 */
    a[4] = -1, b[4] = 5;                             /* negative */
    a[5] = 5, b[5] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(furrow_formula_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Furrow: formula limits' edges in the C");
    zxapp_arg_t fa[2] = {{"depth", a, WAVE_ROWS}, {"refs", b, WAVE_ROWS}};
    CHECK(wave_same(app, "formula_ok", fa, 2, want, WAVE_ROWS, true),
          "Furrow: formula_ok, Eve = C");

    /* cell_record_ok (FW4): MYRA's digit widths, i4 i7 i5 i1 i18 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-19999, 19999);
        b[i] = wrand(-19999999, 19999999);
        c[i] = wrand(-199999, 199999);
        d[i] = wrand(-19, 19);
        e[i] = wrand(-1999999999, 1999999999) * 1000000000;
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = 9999, b[1] = 9999999, c[1] = 99999, d[1] = 9, e[1] = 999999999999999999;
    a[2] = 10000, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1; /* each one digit past */
    a[3] = 1, b[3] = 10000000, c[3] = 1, d[3] = 1, e[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 100000, d[4] = 1, e[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 10, e[5] = 1;
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = INT64_C(1000000000000000000);
    a[7] = -9999, b[7] = -9999999, c[7] = -99999, d[7] = -9, e[7] = -999999999999999999;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(furrow_cell_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Furrow: the record's edges in the C");
    zxapp_arg_t ca[5] = {{"sheet", a, WAVE_ROWS},
                         {"row", b, WAVE_ROWS},
                         {"col", c, WAVE_ROWS},
                         {"kind", d, WAVE_ROWS},
                         {"value", e, WAVE_ROWS}};
    CHECK(wave_same(app, "cell_record_ok", ca, 5, want, WAVE_ROWS, true),
          "Furrow: cell_record_ok, MYRA = C");

    /* coffer.holdings (FW5): a sheet's money, the exact sum of its money cells */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t amt[32], tot[1];
        uint32_t n = 1u + 2u * r;
        if (n > 32u) n = 32u;
        for (uint32_t i = 0; i < n; i++)
            CHECK(furrow_money_cell(wrand(-1000000, 1000000), wrand(0, 99), &amt[i]),
                  "Furrow: a money cell");
        CHECK(furrow_holdings(amt, n, &tot[0]), "Furrow: sheet %u's money in the C (FW5)", r);
        zxapp_arg_t ta[1] = {{"balance", amt, n}};
        CHECK(wave_same(app, "coffer.holdings", ta, 1, tot, 1, false),
              "Furrow: sheet %u of %u money cells, its total through coffer.holdings", r, n);
    }
}
