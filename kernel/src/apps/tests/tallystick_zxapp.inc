/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tallystick's languages against the C its model runs (tallystick_rules.h).
 * Slots: @num.tallystick_taxed_line_total, @logic.tallystick_paid_in_full,
 * @num.tallystick_next_doc_number. */
#include "tallystick_rules.h"

APP_TEST(tallystick, 26, 78)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* taxed_line_total (TR1): Sutra = Trebago = C over every size of line */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 1000 : TSTK_QTY_MAX);
        b[i] = wrand(0, i < 32 ? 100000 : TSTK_PRICE_MAX);
        d[i] = wrand(1, TSTK_RATE_MAX);
        c[i] = wrand(0, d[i]);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 1;                         /* nothing */
    a[1] = TSTK_QTY_MAX, b[1] = TSTK_PRICE_MAX, c[1] = 1, d[1] = 1; /* the most, all taxed */
    a[2] = 1, b[2] = 1, c[2] = 1, d[2] = 2;                         /* a half: rounded up */
    a[3] = 1, b[3] = 1, c[3] = 49999, d[3] = 99999;                 /* below a half: down */
    a[4] = 3, b[4] = 1999, c[4] = 2, d[4] = 25;                     /* 8%: 479.76 to 480 */
    a[5] = TSTK_QTY_MAX, b[5] = TSTK_PRICE_MAX, c[5] = 0, d[5] = TSTK_RATE_MAX; /* untaxed */
    a[6] = TSTK_QTY_MAX, b[6] = TSTK_PRICE_MAX, c[6] = 99998, d[6] = TSTK_RATE_MAX;
    a[7] = 2, b[7] = 5, c[7] = 1, d[7] = 4; /* 2.5 to 3 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = tstk_line_total(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != TSTK_REFUSED, "Tallystick: line row %u in range", i);
    }
    CHECK(want[0] == 0 && want[1] == 19999978000002 && want[2] == 2 && want[3] == 1 &&
              want[4] == 6477 && want[5] == 9999989000001 && want[7] == 13,
          "Tallystick: taxed_line_total, the C's own edges");
    zxapp_arg_t la[4] = {{"qty", a, WAVE_ROWS},
                         {"price", b, WAVE_ROWS},
                         {"taxn", c, WAVE_ROWS},
                         {"taxd", d, WAVE_ROWS}};
    CHECK(wave_same(app, "taxed_line_total", la, 4, want, WAVE_ROWS, false),
          "Tallystick: taxed_line_total, Sutra = Trebago = C");
    {
        /* max + 1: a quantity past Sutra's 9(6), refused there; Trebago answers */
        static const int64_t q[2] = {1, TSTK_QTY_MAX + 1}, p[2] = {1, 1}, n[2] = {0, 0},
                             dd[2] = {1, 1};
        zxapp_arg_t xa[4] = {{"qty", q, 2}, {"price", p, 2}, {"taxn", n, 2}, {"taxd", dd, 2}};
        CHECK(tstk_line_total(TSTK_QTY_MAX + 1, 1, 0, 1) == TSTK_REFUSED &&
                  wave_lp(app, "taxed_line_total", xa, 4) == ZXAPP_BOTH,
              "Tallystick: a quantity past 9(6): Sutra refuses, Trebago answers: BOTH");
        /* a price past Sutra's 9(7): BOTH */
        static const int64_t p2[2] = {1, TSTK_PRICE_MAX + 1}, q2[2] = {1, 1};
        zxapp_arg_t ya[4] = {{"qty", q2, 2}, {"price", p2, 2}, {"taxn", n, 2}, {"taxd", dd, 2}};
        CHECK(tstk_line_total(1, TSTK_PRICE_MAX + 1, 0, 1) == TSTK_REFUSED &&
                  wave_lp(app, "taxed_line_total", ya, 4) == ZXAPP_BOTH,
              "Tallystick: a price past 9(7): BOTH");
        /* a rate past 100%: both refuse */
        static const int64_t n3[2] = {0, 2}, d3[2] = {1, 1};
        zxapp_arg_t za[4] = {{"qty", q2, 2}, {"price", q2, 2}, {"taxn", n3, 2}, {"taxd", d3, 2}};
        CHECK(tstk_line_total(1, 1, 2, 1) == TSTK_REFUSED &&
                  wave_lp(app, "taxed_line_total", za, 4) == ZXAPP_NEITHER,
              "Tallystick: a rate past 100%%: both refuse: NEITHER");
        /* a rate over 0: both refuse */
        static const int64_t n4[2] = {0, 0}, d4[2] = {1, 0};
        zxapp_arg_t wa[4] = {{"qty", q2, 2}, {"price", q2, 2}, {"taxn", n4, 2}, {"taxd", d4, 2}};
        CHECK(tstk_line_total(1, 1, 0, 0) == TSTK_REFUSED &&
                  wave_lp(app, "taxed_line_total", wa, 4) == ZXAPP_NEITHER,
              "Tallystick: a rate over 0: both refuse: NEITHER");
        /* a negative quantity: both refuse */
        static const int64_t q5[2] = {1, -1};
        zxapp_arg_t va[4] = {{"qty", q5, 2}, {"price", q2, 2}, {"taxn", n4, 2}, {"taxd", dd, 2}};
        CHECK(tstk_line_total(-1, 1, 0, 1) == TSTK_REFUSED &&
                  wave_lp(app, "taxed_line_total", va, 4) == ZXAPP_NEITHER,
              "Tallystick: a negative quantity: both refuse: NEITHER");
    }

    /* paid_in_full (TR2): Eve = Sutra = C, each field swept past its range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 32 ? wrand(-2, 1000000) : wrand(TSTK_TOTAL_MAX - 3, TSTK_TOTAL_MAX + 3);
        b[i] = i < 32 ? wrand(-1000, 1000) : wrand(TSTK_OVER_MAX - 3, TSTK_OVER_MAX);
        want[i] = T_(tstk_paid_in_full(a[i], b[i]));
    }
    a[0] = 0, b[0] = 0;                          /* nothing owed, nothing paid */
    a[1] = 100, b[1] = -1;                       /* one minor unit short */
    a[2] = TSTK_TOTAL_MAX, b[2] = TSTK_OVER_MAX; /* both at the max */
    a[3] = TSTK_TOTAL_MAX + 1, b[3] = 0;         /* a total no posting carries */
    a[4] = -1, b[4] = 0;                         /* a negative total */
    for (uint32_t i = 0; i < 5; i++) want[i] = T_(tstk_paid_in_full(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE,
          "Tallystick: paid_in_full, the C's own edges");
    zxapp_arg_t pa[2] = {{"total", a, WAVE_ROWS}, {"over", b, WAVE_ROWS}};
    CHECK(wave_same(app, "paid_in_full", pa, 2, want, WAVE_ROWS, true),
          "Tallystick: paid_in_full, Eve = Sutra = C");
    {
        /* max + 1: a margin past 18 digits: Eve says FALSE, Sutra refuses */
        static const int64_t t[2] = {100, 100}, o[2] = {0, TSTK_OVER_MAX + 1};
        zxapp_arg_t xa[2] = {{"total", t, 2}, {"over", o, 2}};
        CHECK(!tstk_paid_in_full(100, TSTK_OVER_MAX + 1) &&
                  wave_lp(app, "paid_in_full", xa, 2) == ZXAPP_BOTH,
              "Tallystick: a margin past 18 digits: Eve FALSE, Sutra refuses: BOTH");
    }

    /* next_doc_number (TR3): Sutra = C, refused past nine digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 32 ? (int64_t) i : wrand(0, TSTK_NUMBER_MAX - 1);
        want[i] = tstk_next_number(a[i]);
        CHECK(want[i] == a[i] + 1, "Tallystick: number row %u", i);
    }
    a[WAVE_ROWS - 1] = TSTK_NUMBER_MAX - 1;
    want[WAVE_ROWS - 1] = TSTK_NUMBER_MAX;
    zxapp_arg_t na[1] = {{"last", a, WAVE_ROWS}};
    CHECK(wave_same(app, "next_doc_number", na, 1, want, WAVE_ROWS, false),
          "Tallystick: next_doc_number, Sutra = C");
    {
        static const int64_t full[2] = {1, TSTK_NUMBER_MAX}, neg[2] = {1, -1};
        zxapp_arg_t xa[1] = {{"last", full, 2}}, ya[1] = {{"last", neg, 2}};
        CHECK(tstk_next_number(TSTK_NUMBER_MAX) == TSTK_REFUSED &&
                  wave_lp(app, "next_doc_number", xa, 1) == ZXAPP_NEITHER,
              "Tallystick: past nine digits the sequence is refused: NEITHER");
        CHECK(tstk_next_number(-1) == TSTK_REFUSED &&
                  wave_lp(app, "next_doc_number", ya, 1) == ZXAPP_NEITHER,
              "Tallystick: a negative last number is refused: NEITHER");
    }
}
