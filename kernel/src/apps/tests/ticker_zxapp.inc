/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ticker's languages against the C its model runs (ticker_rules.h). */
#include "ticker_rules.h"

APP_TEST(ticker, 7, 135)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* caption_least_ms (TK1): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = i % 2 ? wrand(0, 84) : wrand(0, 9999);
    a[0] = 0;    /* nothing to read: a second still */
    a[1] = 16;   /* 960 ms: a second */
    a[2] = 17;   /* 1020 ms: past the second */
    a[3] = 84;   /* two full lines of 42 */
    a[4] = 9999; /* the largest Sutra holds */
    a[5] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = ticker_least_ms(a[i]);
    CHECK(want[0] == 1000 && want[1] == 1000 && want[2] == 1020 && want[3] == 5040 &&
              want[4] == 599940 && ticker_least_ms(-1) == TICKER_REFUSED &&
              ticker_least_ms(10000) == TICKER_REFUSED,
          "Ticker: the C rule's own edges (TK1)");
    zxapp_arg_t la = {"chars", a, WAVE_ROWS};
    CHECK(wave_same(app, "caption_least_ms", &la, 1, want, WAVE_ROWS, false),
          "Ticker: how long a caption stays at least, Sutra = KRON = C");
    {
        static const int64_t neg[2] = {1, -1}, big[2] = {1, 10000};
        zxapp_arg_t na = {"chars", neg, 2}, ba = {"chars", big, 2};
        CHECK(wave_lp(app, "caption_least_ms", &na, 1) == ZXAPP_BOTH,
              "Ticker: a negative count: Sutra refuses, KRON answers: BOTH, held");
        CHECK(wave_lp(app, "caption_least_ms", &ba, 1) == ZXAPP_BOTH,
              "Ticker: a count past 9(4): Sutra refuses, KRON answers: BOTH, held");
    }

    /* caption_pace_ms (TK2): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 10000) : wrand(0, 999999999);
        b[i] = i % 3 ? wrand(1, 84) : wrand(1, 9999);
    }
    a[0] = 0, b[0] = 1;            /* just shown */
    a[1] = 5040, b[1] = 84;        /* exactly 60 ms each */
    a[2] = 5039, b[2] = 84;        /* a millisecond short: 59 */
    a[3] = 999999999, b[3] = 9999; /* both at their largest */
    a[4] = 999999999, b[4] = 1;    /* the largest pace */
    a[5] = 1, b[5] = 9999;         /* floored to 0 */
    a[6] = 1000, b[6] = 16;        /* 62 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = ticker_pace(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 60 && want[2] == 59 && want[3] == 100010 &&
              want[4] == 999999999 && want[5] == 0 && want[6] == 62 &&
              ticker_pace(1000, 0) == TICKER_REFUSED && ticker_pace(-1, 1) == TICKER_REFUSED &&
              ticker_pace(1000000000, 1) == TICKER_REFUSED,
          "Ticker: the C rule's own edges (TK2)");
    zxapp_arg_t pa[2] = {{"shown", a, WAVE_ROWS}, {"chars", b, WAVE_ROWS}};
    CHECK(wave_same(app, "caption_pace_ms", pa, 2, want, WAVE_ROWS, false),
          "Ticker: the pace a caption is read at, Sutra = KRON = C");
    {
        static const int64_t s[2] = {1000, -1}, n[2] = {10, 10};
        zxapp_arg_t sa[2] = {{"shown", s, 2}, {"chars", n, 2}};
        CHECK(wave_lp(app, "caption_pace_ms", sa, 2) == ZXAPP_BOTH,
              "Ticker: a negative time: Sutra refuses, KRON answers: BOTH, held");
        static const int64_t s2[2] = {1000, 1000000000}, n2[2] = {10, 10};
        zxapp_arg_t sb[2] = {{"shown", s2, 2}, {"chars", n2, 2}};
        CHECK(wave_lp(app, "caption_pace_ms", sb, 2) == ZXAPP_BOTH,
              "Ticker: a time past 9(9): Sutra refuses, KRON answers: BOTH, held");
    }

    /* caption_hold_ok (TK3): Eve = KRON = C, each field across its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4 ? wrand(990, 1010) : wrand(0, 999999999);
        b[i] = i % 3 ? wrand(55, 65) : wrand(-1, 100000);
        c[i] = wrand(-1, 3);
        d[i] = i % 2 ? wrand(40, 44) : wrand(-1, 44);
    }
    a[0] = 1000, b[0] = 60, c[0] = 1, d[0] = 1;              /* every least edge: it may go */
    a[1] = 999, b[1] = 60, c[1] = 1, d[1] = 1;               /* a millisecond short of a second */
    a[2] = 1000, b[2] = 59, c[2] = 2, d[2] = 42;             /* read too fast */
    a[3] = 1000, b[3] = 60, c[3] = 2, d[3] = 42;             /* every largest layout edge */
    a[4] = 1000, b[4] = 60, c[4] = 3, d[4] = 42;             /* a third line */
    a[5] = 1000, b[5] = 60, c[5] = 2, d[5] = 43;             /* a line of 43 */
    a[6] = 1000, b[6] = 60, c[6] = 0, d[6] = 1;              /* no line */
    a[7] = 1000, b[7] = 60, c[7] = 1, d[7] = 0;              /* an empty line */
    a[8] = 999999999, b[8] = 999999999, c[8] = 2, d[8] = 42; /* the largest of each */
    a[9] = 1000000000, b[9] = 60, c[9] = 1, d[9] = 1;        /* max + 1 */
    a[10] = 1000, b[10] = 1000000000, c[10] = 1, d[10] = 1;
    a[11] = -1000, b[11] = -60, c[11] = -1, d[11] = -1; /* negatives */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(ticker_hold_ok(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE &&
              want[9] == ZXAPP_FALSE && want[10] == ZXAPP_FALSE && want[11] == ZXAPP_FALSE,
          "Ticker: the C rule's own edges (TK3)");
    zxapp_arg_t ha[4] = {{"shown", a, WAVE_ROWS},
                         {"pace", b, WAVE_ROWS},
                         {"lines", c, WAVE_ROWS},
                         {"longest", d, WAVE_ROWS}};
    CHECK(wave_same(app, "caption_hold_ok", ha, 4, want, WAVE_ROWS, true),
          "Ticker: a caption has stayed long enough, Eve = KRON = C");

    /* caption_record_ok (TK4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 999999999) * 1000000 + wrand(0, 999999) : wrand(-1000, 1000);
        b[i] = wrand(-1000, 1000);
        c[i] = wrand(-1000, 1000);
        d[i] = wrand(-10000, 10000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;
    a[1] = 999999999999999, b[1] = 999, c[1] = 999, d[1] = 9999;
    a[2] = 1000000000000000, b[2] = 999, c[2] = 999, d[2] = 9999;
    a[3] = 999999999999999, b[3] = 1000, c[3] = 999, d[3] = 9999;
    a[4] = 999999999999999, b[4] = 999, c[4] = 1000, d[4] = 9999;
    a[5] = 999999999999999, b[5] = 999, c[5] = 999, d[5] = 10000;
    a[6] = -999999999999999, b[6] = -999, c[6] = -999, d[6] = -9999;
    a[7] = 1792627200000, b[7] = 2, c[7] = 1, d[7] = 84; /* a caption on 2026-10-22 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(ticker_record_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_TRUE,
          "Ticker: the C rule's own edges (TK4)");
    zxapp_arg_t ra[4] = {{"at", a, WAVE_ROWS},
                         {"speaker", b, WAVE_ROWS},
                         {"lang", c, WAVE_ROWS},
                         {"chars", d, WAVE_ROWS}};
    CHECK(wave_same(app, "caption_record_ok", ra, 4, want, WAVE_ROWS, true),
          "Ticker: a saved caption makes a ^ZXTICKER record, MYRA = C");
}
