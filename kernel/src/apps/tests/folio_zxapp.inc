/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Folio's languages against the C its model runs (folio_rules.h).
 * Slots: @num.folio_page_lines, @num.folio_page_count,
 * @num.folio_scaled_amount, @logic.folio_page_setup_ok,
 * @logic.folio_outline_ok, and, used as they are, @num.flint_cells and
 * @num.coffer_holdings. */
#include "folio_rules.h"

APP_TEST(folio, 17, 39)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        g[WAVE_ROWS], h[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t TW = FOLIO_TWIPS_MAX;

    /* page_lines (FR1): pages and margins swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TW);
        b[i] = wrand(0, a[i] / 2);
        c[i] = wrand(0, a[i] - b[i]);
        d[i] = i % 2 ? wrand(160, 1440) : wrand(1, TW);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 1;             /* no page at all */
    a[1] = TW, b[1] = 0, c[1] = 0, d[1] = 1;            /* the most lines */
    a[2] = 15840, b[2] = 1440, c[2] = 1440, d[2] = 240; /* Letter, 12 points */
    a[3] = 2880, b[3] = 1440, c[3] = 1440, d[3] = 240;  /* margins fill the page */
    a[4] = 16838, b[4] = 1440, c[4] = 1440, d[4] = 280; /* A4, 14 points */
    a[5] = TW, b[5] = TW, c[5] = 0, d[5] = TW;          /* one margin is the page */
    a[6] = 1000, b[6] = 0, c[6] = 0, d[6] = 1001;       /* a leading taller than the page */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = folio_page_lines(a[i], b[i], c[i], d[i]);
    CHECK(want[0] == 0 && want[1] == TW && want[2] == 54 && want[3] == 0 && want[4] == 49 &&
              want[5] == 0 && want[6] == 0 && folio_page_lines(-1, 0, 0, 1) == FOLIO_REFUSED &&
              folio_page_lines(TW + 1, 0, 0, 1) == FOLIO_REFUSED &&
              folio_page_lines(100, 0, 0, 0) == FOLIO_REFUSED &&
              folio_page_lines(100, 60, 41, 1) == FOLIO_REFUSED,
          "Folio: the C lines per page's own edges (FR1)");
    zxapp_arg_t la[4] = {{"height", a, WAVE_ROWS},
                         {"top", b, WAVE_ROWS},
                         {"bottom", c, WAVE_ROWS},
                         {"leading", d, WAVE_ROWS}};
    CHECK(wave_same(app, "page_lines", la, 4, want, WAVE_ROWS, false),
          "Folio: page_lines, KRON = C++ = C");
    {
        /* margins past the page: the C++ refuses, KRON answers a negative count */
        static const int64_t ht[2] = {15840, 15840}, tp[2] = {1440, 9000}, bt[2] = {1440, 9000},
                             ld[2] = {240, 240};
        zxapp_arg_t xa[4] = {
            {"height", ht, 2}, {"top", tp, 2}, {"bottom", bt, 2}, {"leading", ld, 2}};
        CHECK(wave_lp(app, "page_lines", xa, 4) == ZXAPP_BOTH,
              "Folio: margins past the page: C++ refuses, KRON answers: BOTH");
        static const int64_t tall[2] = {15840, TW + 1}, z[2] = {0, 0};
        zxapp_arg_t ta[4] = {
            {"height", tall, 2}, {"top", z, 2}, {"bottom", z, 2}, {"leading", ld, 2}};
        CHECK(wave_lp(app, "page_lines", ta, 4) == ZXAPP_BOTH,
              "Folio: a page past 22 inches: C++ refuses, KRON answers: BOTH");
    }

    /* page_count (FR2): line counts swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 100000) : wrand(0, FOLIO_LINES_MAX);
        b[i] = i % 3 ? wrand(1, 200) : wrand(1, FOLIO_PER_MAX);
    }
    a[0] = 0, b[0] = 54;
    a[1] = 1, b[1] = 54;
    a[2] = 54, b[2] = 54;
    a[3] = 55, b[3] = 54;
    a[4] = FOLIO_LINES_MAX, b[4] = 1;
    a[5] = FOLIO_LINES_MAX, b[5] = FOLIO_PER_MAX;
    a[6] = 0, b[6] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = folio_page_count(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 1 && want[2] == 1 && want[3] == 2 &&
              want[4] == FOLIO_LINES_MAX && want[5] == 100011 && want[6] == 0 &&
              folio_page_count(-1, 1) == FOLIO_REFUSED && folio_page_count(1, 0) == FOLIO_REFUSED &&
              folio_page_count(1, FOLIO_PER_MAX + 1) == FOLIO_REFUSED &&
              folio_page_count(FOLIO_LINES_MAX + 1, 1) == FOLIO_REFUSED,
          "Folio: the C page count's own edges (FR2)");
    zxapp_arg_t pa[2] = {{"lines", a, WAVE_ROWS}, {"perpage", b, WAVE_ROWS}};
    CHECK(wave_same(app, "page_count", pa, 2, want, WAVE_ROWS, false),
          "Folio: page_count, KRON = C++ = C");
    {
        static const int64_t neg[2] = {-5, 10}, per[2] = {3, 3};
        zxapp_arg_t xa[2] = {{"lines", neg, 2}, {"perpage", per, 2}};
        CHECK(wave_lp(app, "page_count", xa, 2) == ZXAPP_BOTH,
              "Folio: a negative count of lines: C++ refuses, KRON answers: BOTH");
        static const int64_t ln[2] = {10, 10}, big[2] = {3, FOLIO_PER_MAX + 1};
        zxapp_arg_t ba[2] = {{"lines", ln, 2}, {"perpage", big, 2}};
        CHECK(wave_lp(app, "page_count", ba, 2) == ZXAPP_BOTH,
              "Folio: a page of 10000 lines: C++ refuses, KRON answers: BOTH");
    }

    /* scaled_amount (FR3): table lines swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 100000) : wrand(0, FOLIO_QTY_MAX);
        b[i] = i % 3 ? wrand(0, 1000000) : wrand(0, FOLIO_PRICE_MAX);
    }
    a[0] = 0, b[0] = 0;
    a[1] = 1000, b[1] = 2500; /* one unit */
    a[2] = 1500, b[2] = 1999; /* 2998.5: half up */
    a[3] = 1, b[3] = 499;     /* 0.499: down */
    a[4] = 1, b[4] = 500;     /* 0.5: up */
    a[5] = FOLIO_QTY_MAX, b[5] = FOLIO_PRICE_MAX;
    a[6] = 333, b[6] = 3;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = folio_scaled_amount(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 2500 && want[2] == 2999 && want[3] == 0 && want[4] == 1 &&
              want[5] == INT64_C(999999998000000) && want[6] == 1 &&
              folio_scaled_amount(-1, 1) == FOLIO_REFUSED &&
              folio_scaled_amount(1, -1) == FOLIO_REFUSED &&
              folio_scaled_amount(FOLIO_QTY_MAX + 1, 1) == FOLIO_REFUSED &&
              folio_scaled_amount(1, FOLIO_PRICE_MAX + 1) == FOLIO_REFUSED,
          "Folio: the C amount's own edges (FR3)");
    zxapp_arg_t sa[2] = {{"qty", a, WAVE_ROWS}, {"price", b, WAVE_ROWS}};
    CHECK(wave_same(app, "scaled_amount", sa, 2, want, WAVE_ROWS, false),
          "Folio: scaled_amount, Sutra = C++ = C");
    {
        static const int64_t q[2] = {1000, FOLIO_QTY_MAX + 1}, p1[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"qty", q, 2}, {"price", p1, 2}};
        CHECK(wave_lp(app, "scaled_amount", xa, 2) == ZXAPP_BOTH,
              "Folio: a quantity past 9(9): Sutra refuses, C++ answers: BOTH");
        static const int64_t q1[2] = {1000, 1000}, pb[2] = {1, FOLIO_PRICE_MAX + 1};
        zxapp_arg_t pa2[2] = {{"qty", q1, 2}, {"price", pb, 2}};
        CHECK(wave_lp(app, "scaled_amount", pa2, 2) == ZXAPP_BOTH,
              "Folio: a price past 9(9): Sutra refuses, C++ answers: BOTH");
        static const int64_t qn[2] = {1000, -1};
        zxapp_arg_t na[2] = {{"qty", qn, 2}, {"price", p1, 2}};
        CHECK(wave_lp(app, "scaled_amount", na, 2) != ZXAPP_BOTH,
              "Folio: a negative quantity: Sutra and C++ both refuse, no disagreement");
    }

    /* page_setup_ok (FR6): Eve's ranges at and just past every edge, then swept */
    {
        static const int64_t ew[10] = {12240, 2879,  2880,  31680, 31681,
                                       12240, 12240, 12240, 12240, 12240},
                             eh[10] = {15840, 15840, 2880,  31680, 15840,
                                       15840, 15840, 15840, 15840, 15840},
                             el[10] = {1440, 1440, 0, 4320, 1440, 4321, 1440, 1440, 1440, -1},
                             er[10] = {1440, 1440, 0, 4320, 1440, 1440, 1440, 1440, 1440, 1440},
                             et[10] = {1440, 1440, 0, 4320, 1440, 1440, 1440, 1440, 1440, 1440},
                             eb[10] = {1440, 1440, 0, 4320, 1440, 1440, 1440, 1440, 1440, 1440},
                             eld[10] = {240, 240, 160, 1440, 240, 240, 159, 1441, 240, 240};
        int64_t ewant[10];
        for (uint32_t i = 0; i < 10; i++)
            ewant[i] = T_(folio_page_setup_ok(ew[i], eh[i], el[i], er[i], et[i], eb[i], eld[i]));
        zxapp_arg_t ea[7] = {{"width", ew, 10},   {"height", eh, 10}, {"left", el, 10},
                             {"right", er, 10},   {"top", et, 10},    {"bottom", eb, 10},
                             {"leading", eld, 10}};
        CHECK(ewant[0] == ZXAPP_TRUE && ewant[1] == ZXAPP_FALSE && ewant[2] == ZXAPP_TRUE &&
                  ewant[3] == ZXAPP_TRUE && ewant[4] == ZXAPP_FALSE && ewant[5] == ZXAPP_FALSE &&
                  ewant[6] == ZXAPP_FALSE && ewant[7] == ZXAPP_FALSE && ewant[8] == ZXAPP_TRUE &&
                  ewant[9] == ZXAPP_FALSE,
              "Folio: the C page limits' own edges (FR6)");
        CHECK(wave_same(app, "page_setup_ok", ea, 7, ewant, 10, true),
              "Folio: page_setup_ok edges (Eve = C)");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(2000, 33000);
        b[i] = wrand(2000, 33000);
        c[i] = wrand(-100, 5000);
        d[i] = wrand(-100, 5000);
        e[i] = wrand(-100, 5000);
        g[i] = wrand(-100, 5000);
        h[i] = wrand(100, 1600);
        want[i] = T_(folio_page_setup_ok(a[i], b[i], c[i], d[i], e[i], g[i], h[i]));
    }
    zxapp_arg_t ua[7] = {{"width", a, WAVE_ROWS},  {"height", b, WAVE_ROWS},
                         {"left", c, WAVE_ROWS},   {"right", d, WAVE_ROWS},
                         {"top", e, WAVE_ROWS},    {"bottom", g, WAVE_ROWS},
                         {"leading", h, WAVE_ROWS}};
    CHECK(wave_same(app, "page_setup_ok", ua, 7, want, WAVE_ROWS, true),
          "Folio: page_setup_ok sweep (Eve = C)");

    /* outline_ok (FR7): MYRA's digit widths, i9 i4 i1 i6 i9 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1999999999, 1999999999);
        b[i] = wrand(-19999, 19999);
        c[i] = wrand(-19, 19);
        d[i] = wrand(-1999999, 1999999);
        e[i] = wrand(-1999999999, 1999999999);
        want[i] = T_(folio_outline_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t oa[5] = {{"doc", a, WAVE_ROWS},
                         {"entry", b, WAVE_ROWS},
                         {"level", c, WAVE_ROWS},
                         {"page", d, WAVE_ROWS},
                         {"line", e, WAVE_ROWS}};
    CHECK(wave_same(app, "outline_ok", oa, 5, want, WAVE_ROWS, true),
          "Folio: outline_ok sweep (MYRA = C)");
    {
        static const int64_t od[6] = {0, 999999999, 1000000000, -999999999, 1, 1},
                             oe[6] = {0, 9999, 1, -9999, 10000, 1}, ol[6] = {0, 9, 1, -9, 1, 10},
                             op[6] = {0, 999999, 1, -999999, 1, 1},
                             on[6] = {0, 999999999, 1, -999999999, 1, 1};
        int64_t ow[6];
        for (uint32_t i = 0; i < 6; i++)
            ow[i] = T_(folio_outline_ok(od[i], oe[i], ol[i], op[i], on[i]));
        zxapp_arg_t xa[5] = {
            {"doc", od, 6}, {"entry", oe, 6}, {"level", ol, 6}, {"page", op, 6}, {"line", on, 6}};
        CHECK(ow[0] == ZXAPP_TRUE && ow[1] == ZXAPP_TRUE && ow[2] == ZXAPP_FALSE &&
                  ow[3] == ZXAPP_TRUE && ow[4] == ZXAPP_FALSE && ow[5] == ZXAPP_FALSE &&
                  wave_same(app, "outline_ok", xa, 5, ow, 6, true),
              "Folio: outline_ok at 0, the widest values either side, and one digit past");
    }

    /* flint.cells (FR5): the columns across a page's text width, one cell a row */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i ? wrand(0, TW) : 9360; /* Letter less two inches: 78 columns */
        b[i] = FOLIO_CHAR_TWIPS;
        want[i] = folio_cells(a[i], b[i]);
    }
    zxapp_arg_t ca[2] = {{"pixels", a, WAVE_ROWS}, {"cell", b, WAVE_ROWS}};
    CHECK(want[0] == 78 && wave_same(app, "flint.cells", ca, 2, want, WAVE_ROWS, false),
          "Folio: columns across through flint.cells, KRON = C++ = C");

    /* coffer.holdings (FR4): a table's total, the exact sum of its amounts */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t amt[32], tot[1];
        uint32_t n = 1u + 2u * r;
        if (n > 32u) n = 32u;
        for (uint32_t i = 0; i < n; i++)
            amt[i] = folio_scaled_amount(wrand(0, r ? 100000 : FOLIO_QTY_MAX),
                                         wrand(0, r ? 1000000 : FOLIO_PRICE_MAX));
        CHECK(folio_table_total(amt, n, &tot[0]), "Folio: table %u's total in the C (FR4)", r);
        zxapp_arg_t ta[1] = {{"balance", amt, n}};
        CHECK(wave_same(app, "coffer.holdings", ta, 1, tot, 1, false),
              "Folio: table %u of %u lines, its total through coffer.holdings", r, n);
    }
    {
        int64_t over[2] = {INT64_MAX, 1}, t = 0;
        CHECK(!folio_table_total(over, 2, &t) && !folio_table_total(NULL, 0, &t),
              "Folio: a total past 64 bits is refused, never wrapped (FR4)");
    }
}
