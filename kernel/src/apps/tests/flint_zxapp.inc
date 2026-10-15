/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Flint (slot 13): cells (@num.flint_cells, KRON and C++ witnessed),
 * scroll_ok (@logic.flint_scroll_ok, Eve), history_ok
 * (@logic.flint_history_ok, MYRA) and the block it uses,
 * used_share.used_x100 (@num.used_x100), each row against the rule as
 * flint_model.c runs it (written out here, since this file cannot reach the
 * model; tests/flint_apps.inc checks the model). */

static int64_t fl_cells(int64_t pixels, int64_t cell) /* flint_cells */
{
    if (pixels < 0 || pixels > 65535 || cell < 1 || cell > 256) return -1;
    return pixels / cell;
}

static int64_t fl_scroll_ok(int64_t lines, int64_t bytes) /* flint_scroll_ok */
{
    return T_(lines >= 24 && lines <= 10000 && bytes >= 4096 && bytes <= 1048576);
}

static bool fl_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

static int64_t fl_history_ok(int64_t seq, int64_t stamp, int64_t status, int64_t bytes)
{
    return T_(fl_digits(seq, 1000000000) && fl_digits(stamp, INT64_C(1000000000000)) &&
              fl_digits(status, 1000) && fl_digits(bytes, 10000));
}

APP_TEST(flint, 6, 13)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* cells: window widths and heights swept over every cell size */
    for (uint32_t r = 0; r < 4; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 65535);
            b[i] = r == 0 ? wrand(1, 256) : r == 1 ? 8 : r == 2 ? 16 : wrand(1, 4);
            want[i] = fl_cells(a[i], b[i]);
        }
        zxapp_arg_t ca[2] = {{"pixels", a, WAVE_ROWS}, {"cell", b, WAVE_ROWS}};
        CHECK(wave_same(app, "cells", ca, 2, want, WAVE_ROWS, false),
              "Flint: cells sweep %u, KRON = C++ = C", r);
    }
    /* the edges: 0, a part cell, one whole cell, the largest span and cell */
    {
        static const int64_t ep[8] = {0, 7, 8, 1919, 1920, 65535, 65535, 255};
        static const int64_t ec[8] = {8, 8, 8, 8, 8, 1, 256, 256};
        static const int64_t ew[8] = {0, 0, 1, 239, 240, 65535, 255, 0};
        for (uint32_t i = 0; i < 8; i++) {
            a[i] = ep[i];
            b[i] = ec[i];
            CHECK(fl_cells(ep[i], ec[i]) == ew[i], "Flint: the C rule at edge %u", i);
        }
        zxapp_arg_t ea[2] = {{"pixels", a, 8}, {"cell", b, 8}};
        CHECK(wave_same(app, "cells", ea, 2, ew, 8, false), "Flint: cells at its edges");
    }
    /* past the domain: KRON still divides, the C++ refuses (-1): BOTH */
    {
        int64_t p1[2] = {800, 65536}, c1[2] = {8, 8};
        zxapp_arg_t x1[2] = {{"pixels", p1, 2}, {"cell", c1, 2}};
        CHECK(wave_lp(app, "cells", x1, 2) == ZXAPP_BOTH,
              "Flint: a span past 65535: KRON answers, C++ refuses: BOTH");
        int64_t p2[2] = {800, 800}, c2[2] = {8, 257};
        zxapp_arg_t x2[2] = {{"pixels", p2, 2}, {"cell", c2, 2}};
        CHECK(wave_lp(app, "cells", x2, 2) == ZXAPP_BOTH, "Flint: a cell past 256: BOTH");
        int64_t p3[2] = {800, -9}, c3[2] = {8, 8};
        zxapp_arg_t x3[2] = {{"pixels", p3, 2}, {"cell", c3, 2}};
        CHECK(wave_lp(app, "cells", x3, 2) == ZXAPP_BOTH,
              "Flint: a negative span: KRON floors to -2, C++ refuses: BOTH");
        int64_t p4[2] = {800, 800}, c4[2] = {8, 0};
        zxapp_arg_t x4[2] = {{"pixels", p4, 2}, {"cell", c4, 2}};
        CHECK(wave_lp(app, "cells", x4, 2) != ZXAPP_TRUE, "Flint: a cell of 0 is never TRUE");
    }

    /* scroll_ok: Eve's ranges, at and just past every edge, then swept */
    {
        static const int64_t el[10] = {23, 24, 10000, 10001, 500, 500, 500, 500, 0, -24};
        static const int64_t eb[10] = {65536, 4096,    1048576, 65536, 4095,
                                       4096,  1048576, 1048577, 4096,  4096};
        for (uint32_t i = 0; i < 10; i++) {
            a[i] = el[i];
            b[i] = eb[i];
            want[i] = fl_scroll_ok(el[i], eb[i]);
        }
        zxapp_arg_t sa[2] = {{"lines", a, 10}, {"bytes", b, 10}};
        CHECK(wave_same(app, "scroll_ok", sa, 2, want, 10, true), "Flint: scroll_ok edges (Eve)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-10, 10100);
            b[i] = wrand(0, 1100000);
            want[i] = fl_scroll_ok(a[i], b[i]);
        }
        zxapp_arg_t wa[2] = {{"lines", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}};
        CHECK(wave_same(app, "scroll_ok", wa, 2, want, WAVE_ROWS, true),
              "Flint: scroll_ok sweep (Eve = C)");
    }

    /* history_ok: MYRA's digit widths, i9 i12 i3 i4 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 1999999999);
            b[i] = wrand(0, INT64_C(1999999999999));
            c[i] = wrand(-1999, 1999);
            d[i] = wrand(0, 19999);
            want[i] = fl_history_ok(a[i], b[i], c[i], d[i]);
        }
        zxapp_arg_t ha[4] = {{"seq", a, WAVE_ROWS},
                             {"stamp", b, WAVE_ROWS},
                             {"status", c, WAVE_ROWS},
                             {"bytes", d, WAVE_ROWS}};
        CHECK(wave_same(app, "history_ok", ha, 4, want, WAVE_ROWS, true),
              "Flint: history_ok sweep (MYRA = C)");
        int64_t es[6] = {0, 999999999, 1000000000, 1, 1, 1},
                et[6] = {0, INT64_C(999999999999), 1, INT64_C(1000000000000), 1, 1},
                eu[6] = {0, 999, 0, 0, 1000, 127}, eb[6] = {0, 9999, 1, 1, 1, 10000},
                ew[6] = {ZXAPP_TRUE,  ZXAPP_TRUE,  ZXAPP_FALSE,
                         ZXAPP_FALSE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t xa[4] = {{"seq", es, 6}, {"stamp", et, 6}, {"status", eu, 6}, {"bytes", eb, 6}};
        CHECK(wave_same(app, "history_ok", xa, 4, ew, 6, true),
              "Flint: history_ok at 0, the widest values, and one digit past each");
    }

    /* used_share.used_x100 (df and free), through the block Flint uses */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(1, i < 8 ? 3 : INT64_C(1) << 40);
            b[i] = wrand(0, a[i]);
            want[i] = (a[i] - b[i]) * 10000 / a[i];
        }
        zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
        CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
              "Flint: used_share.used_x100 sweep, Sutra = KRON = C");
    }
}
