/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gleaner's languages against the C its model runs (gleaner_rules.h).
 * Slots: @num.gleaner_holdout, @logic.gleaner_row_match,
 * @logic.gleaner_dataset_ok, @logic.gleaner_row_ok, @logic.gleaner_leave_ok,
 * and Witness's @num.witness_kind_count, used as it is. */
#include "gleaner_rules.h"

APP_TEST(gleaner, 12, 71)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* holdout (GR1): C++ (wasm) = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? (int64_t) i : wrand(0, GLEANER_ROW_MAX);
        b[i] = wrand(0, GLEANER_SHARE_MAX);
    }
    a[0] = 0, b[0] = 0;                               /* no test set */
    a[1] = 0, b[1] = GLEANER_SHARE_MAX;               /* everything in it */
    a[2] = GLEANER_ROW_MAX, b[2] = GLEANER_SHARE_MAX; /* every max */
    a[3] = 4, b[3] = 2000;                            /* the fifth row of 20 % */
    a[4] = 5, b[4] = 2000;                            /* the sixth: training */
    a[5] = GLEANER_ROW_MAX, b[5] = 1;                 /* the last row at the least share */
    a[6] = 9999, b[6] = 1;                            /* 1 in 10000: the 10000th row */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = gleaner_holdout(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 1 && want[2] == 1 && want[3] == 1 && want[4] == 0 &&
              want[6] == 1 && gleaner_holdout(9998, 1) == 0,
          "Gleaner: the C rule's own edges (GR1)");
    {
        /* every row once: over the first n rows, exactly floor(n * share / 10000) are test */
        int64_t t = 0;
        for (int64_t r = 0; r < 1000; r++) t += gleaner_holdout(r, 3333);
        CHECK(t == 333, "Gleaner: 1000 rows at 33.33 %%: 333 test rows (%lld)", (long long) t);
    }
    zxapp_arg_t ha[2] = {{"row", a, WAVE_ROWS}, {"share", b, WAVE_ROWS}};
    CHECK(wave_same(app, "holdout", ha, 2, want, WAVE_ROWS, false),
          "Gleaner: holdout, C++ = KRON = C");
    {
        /* max + 1 and negative: C++ traps (refused), KRON answers */
        static const int64_t r1[2] = {4, GLEANER_ROW_MAX + 1}, s1[2] = {2000, 2000};
        zxapp_arg_t xa[2] = {{"row", r1, 2}, {"share", s1, 2}};
        CHECK(wave_lp(app, "holdout", xa, 2) == ZXAPP_BOTH,
              "Gleaner: a row past 999999999: C++ refuses, KRON answers: BOTH");
        static const int64_t r2[2] = {4, -1};
        zxapp_arg_t na[2] = {{"row", r2, 2}, {"share", s1, 2}};
        CHECK(wave_lp(app, "holdout", na, 2) == ZXAPP_BOTH,
              "Gleaner: a negative row: C++ refuses, KRON answers: BOTH");
        static const int64_t r3[2] = {4, 4}, s3[2] = {2000, GLEANER_SHARE_MAX + 1};
        zxapp_arg_t sa[2] = {{"row", r3, 2}, {"share", s3, 2}};
        CHECK(wave_lp(app, "holdout", sa, 2) == ZXAPP_BOTH && gleaner_holdout(4, 10001) < 0,
              "Gleaner: a share past 100 %%: C++ refuses, KRON answers: BOTH");
    }

    /* row_match (GR2): KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 5); /* label */
        b[i] = wrand(0, 3); /* source */
        c[i] = 20250101 + wrand(0, 2) * 10000 + wrand(0, 1130);
        d[i] = i % 3 ? wrand(0, 5) : -1;          /* lab, any */
        e[i] = i % 4 ? wrand(0, 3) : -1;          /* src, any */
        f[i] = 20250101 + wrand(0, 2) * 10000;    /* from */
        g[i] = f[i] + wrand(0, 2) * 10000 + 1130; /* to */
    }
    a[0] = 1, b[0] = 0, c[0] = 20260101, d[0] = 1, e[0] = -1, f[0] = 20260101, g[0] = 20261231;
    a[1] = 1, b[1] = 0, c[1] = 20261231, d[1] = 1, e[1] = 0, f[1] = 20260101, g[1] = 20261231;
    a[2] = 1, b[2] = 0, c[2] = 20251231, d[2] = 1, e[2] = 0, f[2] = 20260101, g[2] = 20261231;
    a[3] = 1, b[3] = 0, c[3] = 20270101, d[3] = 1, e[3] = 0, f[3] = 20260101, g[3] = 20261231;
    a[4] = 2, b[4] = 0, c[4] = 20260505, d[4] = 1, e[4] = -1, f[4] = 20260101, g[4] = 20261231;
    a[5] = 2, b[5] = 3, c[5] = 20260505, d[5] = -1, e[5] = 2, f[5] = 20260101, g[5] = 20261231;
    a[6] = 0, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = 0, f[6] = 0, g[6] = 0; /* every least */
    a[7] = 5, b[7] = 3, c[7] = 20260505, d[7] = -1, e[7] = -1, f[7] = 20260505, g[7] = 20260504;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(gleaner_row_match(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE,
          "Gleaner: the C filter's own edges (GR2)");
    zxapp_arg_t ma[7] = {{"label", a, WAVE_ROWS}, {"source", b, WAVE_ROWS}, {"date", c, WAVE_ROWS},
                         {"lab", d, WAVE_ROWS},   {"src", e, WAVE_ROWS},    {"from", f, WAVE_ROWS},
                         {"to", g, WAVE_ROWS}};
    CHECK(wave_same(app, "row_match", ma, 7, want, WAVE_ROWS, true),
          "Gleaner: row_match, KRON = C");

    /* dataset_ok (GR3): Eve = C, each range at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, GLEANER_ROWS + 2);
        b[i] = wrand(0, GLEANER_COLS + 2);
        c[i] = wrand(-1, GLEANER_BYTES + 2);
    }
    a[0] = 1, b[0] = 3, c[0] = 1;                                   /* every least */
    a[1] = GLEANER_ROWS, b[1] = GLEANER_COLS, c[1] = GLEANER_BYTES; /* every max */
    a[2] = GLEANER_ROWS + 1, b[2] = 3, c[2] = 1;                    /* rows max + 1 */
    a[3] = 1, b[3] = GLEANER_COLS + 1, c[3] = 1;                    /* cols max + 1 */
    a[4] = 1, b[4] = 3, c[4] = GLEANER_BYTES + 1;                   /* bytes max + 1 */
    a[5] = 0, b[5] = 3, c[5] = 1;                                   /* no rows */
    a[6] = 1, b[6] = 2, c[6] = 1;                                   /* two columns */
    a[7] = -1, b[7] = -1, c[7] = -1;                                /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gleaner_dataset_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Gleaner: the C limits' own edges (GR3)");
    zxapp_arg_t da[3] = {{"rows", a, WAVE_ROWS}, {"cols", b, WAVE_ROWS}, {"bytes", c, WAVE_ROWS}};
    CHECK(wave_same(app, "dataset_ok", da, 3, want, WAVE_ROWS, true),
          "Gleaner: dataset_ok, Eve = C");

    /* row_ok (GR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1100);
        b[i] = wrand(0, 120);
        c[i] = wrand(0, 120);
        d[i] = 20200101 + wrand(0, 80000000);
        e[i] = wrand(0, 11);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;            /* every least */
    a[1] = 999, b[1] = 99, c[1] = 99, d[1] = 99999999, e[1] = 9; /* every max */
    a[2] = 1000, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0;         /* row max + 1 */
    a[3] = 0, b[3] = 100, c[3] = 0, d[3] = 0, e[3] = 0;          /* label max + 1 */
    a[4] = 0, b[4] = 0, c[4] = 100, d[4] = 0, e[4] = 0;          /* source max + 1 */
    a[5] = 0, b[5] = 0, c[5] = 0, d[5] = 100000000, e[5] = 0;    /* date max + 1 */
    a[6] = 0, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = 10;           /* pii max + 1 */
    a[7] = -999, b[7] = -99, c[7] = -99, d[7] = -1, e[7] = -1;   /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(gleaner_row_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Gleaner: the record's own edges (GR4)");
    zxapp_arg_t ra[5] = {{"row", a, WAVE_ROWS},
                         {"label", b, WAVE_ROWS},
                         {"source", c, WAVE_ROWS},
                         {"date", d, WAVE_ROWS},
                         {"pii", e, WAVE_ROWS}};
    CHECK(wave_same(app, "row_ok", ra, 5, want, WAVE_ROWS, true), "Gleaner: row_ok, MYRA = C");

    /* leave_ok (GR5): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(0, 9999) : 0;
        b[i] = wrand(0, 2);
    }
    a[0] = 0, b[0] = 0;    /* nothing personal: it may go */
    a[1] = 1, b[1] = 0;    /* one personal row, no consent */
    a[2] = 1, b[2] = 1;    /* with consent */
    a[3] = 9999, b[3] = 1; /* max */
    a[4] = 9999, b[4] = 2; /* a consent that is no yes */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gleaner_leave_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE,
          "Gleaner: the C rule's own edges (GR5)");
    zxapp_arg_t la[2] = {{"personal", a, WAVE_ROWS}, {"consent", b, WAVE_ROWS}};
    CHECK(wave_same(app, "leave_ok", la, 2, want, WAVE_ROWS, true),
          "Gleaner: leave_ok, Sutra = KRON = C");
    {
        /* max + 1 and negative: past Sutra's 9(4), refused there; KRON answers */
        static const int64_t p1[2] = {1, 10000}, c1[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"personal", p1, 2}, {"consent", c1, 2}};
        CHECK(wave_lp(app, "leave_ok", xa, 2) == ZXAPP_BOTH,
              "Gleaner: a count past 9999: Sutra refuses, KRON answers: BOTH");
        static const int64_t p2[2] = {1, -1};
        zxapp_arg_t na[2] = {{"personal", p2, 2}, {"consent", c1, 2}};
        CHECK(wave_lp(app, "leave_ok", na, 2) == ZXAPP_BOTH && !gleaner_leave_ok(-1, 1),
              "Gleaner: a negative count: Sutra refuses, KRON answers: BOTH");
    }

    /* witness.kind_count, used as it is: the rows of each class, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t k[24], wk[1];
        uint32_t n = 1u + r % 24u;
        for (uint32_t i = 0; i < n; i++) k[i] = r == 0 ? 0 : wrand(0, 7);
        wk[0] = r == 1 ? 63 : wrand(0, 7); /* a class no row has */
        int64_t c1 = witness_kind_count(k, n, wk[0]);
        zxapp_arg_t ka[2] = {{"kind", k, n}, {"want", wk, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &c1, 1, false),
              "Gleaner: rows of class %lld in set %u, KRON = Trebago = C", (long long) wk[0], r);
    }
    {
        /* two classes asked at once: Trebago's scalar refuses, KRON sums them */
        static const int64_t k2[2] = {1, 2}, wants[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"kind", k2, 2}, {"want", wants, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ba, 2) == ZXAPP_BOTH,
              "Gleaner: two classes at once: Trebago refuses, KRON answers: BOTH");
    }
}
