/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Typecase's languages against the C its model runs (typecase_rules.h).
 * Slots: @num.typecase_font_ok, @num.typecase_coverage_x100. */
#include "typecase_rules.h"

APP_TEST(typecase, 4, 113)
{
    static int64_t off[TYPECASE_TABLES_MAX + 8], len[TYPECASE_TABLES_MAX + 8];
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* font_ok (TR1): 64 table directories, KRON = Trebago = C, with every edge */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = r == 0   ? 1u
                     : r == 1 ? TYPECASE_TABLES_MAX
                     : r == 2 ? TYPECASE_TABLES_MAX + 1u
                              : (uint32_t) wrand(1, 40);
        int64_t sz[1];
        sz[0] = r == 3   ? TYPECASE_FILE_MAX
                : r == 4 ? TYPECASE_FILE_MAX + 1
                : r == 5 ? 0
                         : wrand(12, r % 5 ? 200000 : TYPECASE_FILE_MAX);
        for (uint32_t i = 0; i < n; i++) {
            off[i] = wrand(0, sz[0]);
            len[i] = wrand(0, sz[0] - off[i]);
        }
        if (r == 6) off[0] = 0, len[0] = sz[0];         /* one table, the whole file */
        if (r == 7) off[n - 1] = sz[0], len[n - 1] = 0; /* empty, at the very end */
        if (r % 4 == 1 && r > 4) {                      /* one table a byte past the end */
            uint32_t k = (uint32_t) wrand(0, n - 1);
            off[k] = wrand(0, sz[0]);
            len[k] = sz[0] - off[k] + 1;
        }
        if (r % 9 == 2 && r > 4) off[0] = sz[0] + wrand(1, 1000), len[0] = 0; /* past, empty */
        int64_t w = typecase_font_ok(off, len, n, sz[0]);
        zxapp_arg_t fa[3] = {{"off", off, n}, {"len", len, n}, {"size", sz, 1}};
        CHECK(w >= 0 && wave_same(app, "font_ok", fa, 3, &w, 1, false),
              "Typecase: font_ok %u (%u tables, %lld bytes: %lld), KRON = Trebago = C", r, n,
              (long long) sz[0], (long long) w);
    }
    {
        /* the C rule's own edges */
        static const int64_t o2[2] = {12, 140}, l2[2] = {128, 60}, l2p[2] = {128, 61};
        static const int64_t neg[2] = {-1, 0}, zero[2] = {0, 0};
        CHECK(typecase_font_ok(o2, l2, 2, 200) == 1 && typecase_font_ok(o2, l2, 2, 199) == 0 &&
                  typecase_font_ok(o2, l2p, 2, 200) == 0 && typecase_font_ok(o2, l2, 0, 200) == 0 &&
                  typecase_font_ok(zero, zero, 2, 0) == 1 &&
                  typecase_font_ok(o2, l2, 2, TYPECASE_FILE_MAX) == 1 &&
                  typecase_font_ok(o2, l2, 2, TYPECASE_FILE_MAX + 1) == 0 &&
                  typecase_font_ok(o2, l2, TYPECASE_TABLES_MAX + 1, 200) == 0 &&
                  typecase_font_ok(neg, zero, 2, 200) == TYPECASE_REFUSED &&
                  typecase_font_ok(zero, neg, 2, 200) == TYPECASE_REFUSED &&
                  typecase_font_ok(o2, l2, 2, -1) == TYPECASE_REFUSED &&
                  typecase_font_ok(NULL, l2, 2, 200) == 0,
              "Typecase: the C rule's own edges (TR1)");
        /* a negative offset, length or size: Trebago refuses, KRON reads only the ends */
        static const int64_t no[2] = {-4, 100}, nl[2] = {10, 50}, s150[1] = {150};
        static const int64_t po[2] = {0, 100}, ml[2] = {-1, 50}, sneg[1] = {-1};
        zxapp_arg_t x1[3] = {{"off", no, 2}, {"len", nl, 2}, {"size", s150, 1}};
        zxapp_arg_t x2[3] = {{"off", po, 2}, {"len", ml, 2}, {"size", s150, 1}};
        zxapp_arg_t x3[3] = {{"off", zero, 2}, {"len", zero, 2}, {"size", sneg, 1}};
        CHECK(wave_lp(app, "font_ok", x1, 3) == ZXAPP_BOTH,
              "Typecase: a negative offset: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "font_ok", x2, 3) == ZXAPP_BOTH,
              "Typecase: a negative length: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "font_ok", x3, 3) == ZXAPP_BOTH,
              "Typecase: a negative size: Trebago refuses, KRON answers: BOTH");
    }

    /* coverage_x100 (TR2): one row per script, KRON = Trebago = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, i < 32 ? 1000 : TYPECASE_CP_MAX);
        a[i] = wrand(0, b[i]);
    }
    a[0] = 0, b[0] = 1;                             /* nothing of the least script */
    a[1] = 1, b[1] = 1;                             /* all of it */
    a[2] = 1, b[2] = 3;                             /* 33.33.. floored */
    a[3] = 2, b[3] = 3;                             /* 66.66.. floored, never rounded up */
    a[4] = TYPECASE_CP_MAX, b[4] = TYPECASE_CP_MAX; /* every code point there is */
    a[5] = 1, b[5] = TYPECASE_CP_MAX;               /* the least of the most: 0 */
    a[6] = TYPECASE_CP_MAX - 1, b[6] = TYPECASE_CP_MAX;
    a[7] = 449, b[7] = 450; /* Latin, one letter short */
    a[8] = 0, b[8] = 20992; /* no Han */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = typecase_coverage_x100(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 10000 && want[2] == 3333 && want[3] == 6666 &&
              want[4] == 10000 && want[5] == 0 && want[6] == 9999 && want[7] == 9977 &&
              want[8] == 0,
          "Typecase: the C rule's own edges (TR2)");
    CHECK(typecase_coverage_x100(1, 0) == TYPECASE_REFUSED &&
              typecase_coverage_x100(2, 1) == TYPECASE_REFUSED &&
              typecase_coverage_x100(-1, 5) == TYPECASE_REFUSED &&
              typecase_coverage_x100(0, -1) == TYPECASE_REFUSED &&
              typecase_coverage_x100(1, TYPECASE_CP_MAX + 1) == TYPECASE_REFUSED,
          "Typecase: the C refuses a total of 0, covered past total, a negative, past 0x110000");
    zxapp_arg_t ca[2] = {{"covered", a, WAVE_ROWS}, {"total", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coverage_x100", ca, 2, want, WAVE_ROWS, false),
          "Typecase: coverage_x100, KRON = Trebago = C");
    {
        static const int64_t c[3][2] = {{1, 5}, {1, -1}, {1, 1}};
        static const int64_t t[3][2] = {{1, 4}, {1, 4}, {1, TYPECASE_CP_MAX + 1}};
        static const char *const why[3] = {"covered past its total", "a negative covered",
                                           "a total past 0x110000"};
        for (uint32_t e = 0; e < 3; e++) {
            zxapp_arg_t xa[2] = {{"covered", c[e], 2}, {"total", t[e], 2}};
            CHECK(wave_lp(app, "coverage_x100", xa, 2) == ZXAPP_BOTH,
                  "Typecase: coverage_x100 with %s: Trebago refuses, KRON answers: BOTH", why[e]);
        }
    }
}
