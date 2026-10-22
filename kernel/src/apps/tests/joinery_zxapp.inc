/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Joinery's languages against the C its model runs (joinery_rules.h).
 * Slots: @num.joinery_line_start_offset, @num.joinery_offset_line_index,
 * @num.joinery_utf8_col_offset, @num.joinery_utf8_offset_col,
 * @logic.joinery_edit_buffer_ok, @logic.joinery_undo_record_ok, and how
 * full the buffer is, @num.used_x100. */
#include "joinery_rules.h"

/* one place entry, one call: KRON = Trebago = C, a single answer */
static bool jn_same(zxapp_t *a, const char *entry, const char *vec, const int64_t *v, uint32_t n,
                    const char *one, int64_t x, int64_t want)
{
    int64_t s[1] = {x};
    zxapp_arg_t args[2] = {{vec, v, n}, {one, s, 1}};
    return wave_same(a, entry, args, 2, &want, 1, false);
}

static int32_t jn_lp(zxapp_t *a, const char *entry, const char *vec, const int64_t *v, uint32_t n,
                     const char *one, int64_t x)
{
    int64_t s[1] = {x};
    zxapp_arg_t args[2] = {{vec, v, n}, {one, s, 1}};
    return wave_lp(a, entry, args, 2);
}

APP_TEST(joinery, 13, 121)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    int64_t v[16];

    /* JR1-JR4 in C: the rule's own edges */
    {
        static const int64_t lb[3] = {3, 5, 1}, w[4] = {1, 2, 3, 4}, bad[2] = {3, 0};
        CHECK(joinery_line_start(lb, 3, 0) == 0 && joinery_line_start(lb, 3, 2) == 8 &&
                  joinery_line_start(lb, 3, 3) == 9 &&
                  joinery_line_start(lb, 3, 4) == JOINERY_REFUSED &&
                  joinery_line_start(lb, 3, -1) == JOINERY_REFUSED &&
                  joinery_line_start(bad, 2, 1) == JOINERY_REFUSED &&
                  joinery_line_start(NULL, 0, 0) == 0,
              "Joinery: where a line starts, in C (JR1)");
        CHECK(joinery_offset_line(lb, 3, 0) == 0 && joinery_offset_line(lb, 3, 2) == 0 &&
                  joinery_offset_line(lb, 3, 3) == 1 && joinery_offset_line(lb, 3, 99) == 3 &&
                  joinery_offset_line(lb, 3, -1) == JOINERY_REFUSED &&
                  joinery_offset_line(lb, 3, JOINERY_FILE_MAX + 1) == JOINERY_REFUSED,
              "Joinery: which line holds a byte, in C (JR2)");
        CHECK(joinery_col_offset(w, 4, 0) == 0 && joinery_col_offset(w, 4, 2) == 3 &&
                  joinery_col_offset(w, 4, 4) == 10 &&
                  joinery_col_offset(w, 4, 5) == JOINERY_REFUSED &&
                  joinery_offset_col(w, 4, 0) == 0 && joinery_offset_col(w, 4, 3) == 2 &&
                  joinery_offset_col(w, 4, 10) == 4 &&
                  joinery_offset_col(w, 4, 2) == JOINERY_REFUSED &&
                  joinery_offset_col(w, 4, 11) == JOINERY_REFUSED &&
                  joinery_offset_col(w, 4, -1) == JOINERY_REFUSED,
              "Joinery: a column's offset and back, in C (JR3, JR4)");
    }

    /* line_start_offset (JR1): 64 texts swept, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = 1u + r % 16u;
        for (uint32_t i = 0; i < n; i++) v[i] = wrand(1, r % 5 ? 120 : JOINERY_LINEBYTES_MAX);
        if (r == 0) v[0] = 1;                     /* the least line: its newline alone */
        if (r == 1) v[0] = JOINERY_LINEBYTES_MAX; /* the longest line */
        int64_t line = r == 2 ? 0 : r == 3 ? (int64_t) n : wrand(0, (int64_t) n);
        int64_t s = joinery_line_start(v, n, line);
        CHECK(s >= 0 && jn_same(app, "line_start_offset", "linebytes", v, n, "line", line, s),
              "Joinery: line_start_offset %u, KRON = Trebago = C", r);
    }
    {
        static const int64_t lb[3] = {3, 5, 1}, zero[3] = {3, 0, 1}, over[2] = {3, 65538};
        CHECK(jn_lp(app, "line_start_offset", "linebytes", lb, 3, "line", 4) == ZXAPP_BOTH &&
                  jn_lp(app, "line_start_offset", "linebytes", lb, 3, "line", -1) == ZXAPP_BOTH,
              "Joinery: a line past the text or negative: Trebago refuses, KRON answers: BOTH");
        CHECK(jn_lp(app, "line_start_offset", "linebytes", zero, 3, "line", 1) == ZXAPP_BOTH &&
                  jn_lp(app, "line_start_offset", "linebytes", over, 2, "line", 1) == ZXAPP_BOTH,
              "Joinery: a line of 0 bytes or past 65537: Trebago refuses, KRON answers: BOTH");
    }

    /* offset_line_index (JR2): 64 offsets swept, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = 1u + r % 16u;
        int64_t total = 0;
        for (uint32_t i = 0; i < n; i++) total += v[i] = wrand(1, r % 4 ? 80 : 65537);
        int64_t off = r == 0   ? 0
                      : r == 1 ? total
                      : r == 2 ? JOINERY_FILE_MAX
                      : r == 3 ? v[0] - 1
                               : wrand(0, total + 5);
        int64_t k = joinery_offset_line(v, n, off);
        CHECK(k >= 0 && jn_same(app, "offset_line_index", "linebytes", v, n, "off", off, k),
              "Joinery: offset_line_index %u, KRON = Trebago = C", r);
    }
    {
        static const int64_t lb[3] = {3, 5, 1};
        CHECK(jn_lp(app, "offset_line_index", "linebytes", lb, 3, "off", -1) == ZXAPP_BOTH &&
                  jn_lp(app, "offset_line_index", "linebytes", lb, 3, "off",
                        JOINERY_FILE_MAX + 1) == ZXAPP_BOTH,
              "Joinery: an offset negative or past 16 MiB: Trebago refuses, KRON answers: BOTH");
    }

    /* utf8_col_offset (JR3): 64 lines swept, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = 1u + r % 16u;
        for (uint32_t i = 0; i < n; i++) v[i] = wrand(1, 4);
        int64_t col = r == 0 ? 0 : r == 1 ? (int64_t) n : wrand(0, (int64_t) n);
        int64_t o = joinery_col_offset(v, n, col);
        CHECK(o >= 0 && jn_same(app, "utf8_col_offset", "width", v, n, "col", col, o),
              "Joinery: utf8_col_offset %u, KRON = Trebago = C", r);
    }
    {
        static const int64_t five[2] = {1, 5}, zero[2] = {0, 1}, w[2] = {1, 2};
        CHECK(jn_lp(app, "utf8_col_offset", "width", five, 2, "col", 1) == ZXAPP_BOTH &&
                  jn_lp(app, "utf8_col_offset", "width", zero, 2, "col", 1) == ZXAPP_BOTH,
              "Joinery: a width that is no UTF-8 width: Trebago refuses, KRON answers: BOTH");
        CHECK(jn_lp(app, "utf8_col_offset", "width", w, 2, "col", 3) == ZXAPP_BOTH &&
                  jn_lp(app, "utf8_col_offset", "width", w, 2, "col", -1) == ZXAPP_BOTH,
              "Joinery: a column past the line or negative: Trebago refuses, KRON answers: BOTH");
    }

    /* utf8_offset_col (JR4): 64 boundaries swept, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = 1u + r % 16u;
        int64_t end = 0, at = 0;
        uint32_t pick = (uint32_t) wrand(0, n);
        for (uint32_t i = 0; i < n; i++) {
            v[i] = wrand(1, 4);
            end += v[i];
            if (i + 1u == pick) at = end;
        }
        int64_t off = r == 0 ? 0 : r == 1 ? end : at;
        int64_t k = joinery_offset_col(v, n, off);
        CHECK(k >= 0 && jn_same(app, "utf8_offset_col", "width", v, n, "off", off, k),
              "Joinery: utf8_offset_col %u, KRON = Trebago = C", r);
    }
    {
        static const int64_t w[4] = {1, 2, 3, 4}, zero[2] = {0, 1};
        CHECK(jn_lp(app, "utf8_offset_col", "width", w, 4, "off", 2) == ZXAPP_BOTH &&
                  jn_lp(app, "utf8_offset_col", "width", w, 4, "off", 5) == ZXAPP_BOTH,
              "Joinery: an offset inside a character: Trebago refuses, KRON counts: BOTH");
        CHECK(jn_lp(app, "utf8_offset_col", "width", w, 4, "off", 11) == ZXAPP_BOTH &&
                  jn_lp(app, "utf8_offset_col", "width", w, 4, "off", -1) == ZXAPP_BOTH &&
                  jn_lp(app, "utf8_offset_col", "width", zero, 2, "off", 1) == ZXAPP_BOTH,
              "Joinery: past the line, negative, a width of 0: Trebago refuses, KRON: BOTH");
    }

    /* edit_buffer_ok (JR5): Eve = KRON = C, every edge, the line inside its file */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, JOINERY_FILE_MAX);
        b[i] = wrand(0, a[i] < JOINERY_LINE_MAX ? a[i] : JOINERY_LINE_MAX);
        c[i] = wrand(0, JOINERY_UNDO_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                                   /* every least */
    a[1] = JOINERY_FILE_MAX, b[1] = JOINERY_LINE_MAX, c[1] = 1024;  /* every max */
    a[2] = JOINERY_FILE_MAX + 1, b[2] = 0, c[2] = 0;                /* file max + 1 */
    a[3] = JOINERY_FILE_MAX, b[3] = JOINERY_LINE_MAX + 1, c[3] = 0; /* line max + 1 */
    a[4] = 100, b[4] = 100, c[4] = 1025;                            /* undo max + 1 */
    a[5] = -1, b[5] = 0, c[5] = 0;                                  /* negative */
    a[6] = 100, b[6] = -1, c[6] = 0;
    a[7] = 100, b[7] = 0, c[7] = -1;
    a[8] = 99999999999, b[8] = 99999999999, c[8] = 99999999999; /* far past: no by both */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(joinery_buffer_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Joinery: the buffer's limits' own edges (JR5)");
    zxapp_arg_t ba[3] = {
        {"filebytes", a, WAVE_ROWS}, {"linebytes", b, WAVE_ROWS}, {"undosteps", c, WAVE_ROWS}};
    CHECK(wave_same(app, "edit_buffer_ok", ba, 3, want, WAVE_ROWS, true),
          "Joinery: edit_buffer_ok, Eve = KRON = C");
    {
        /* a line longer than its file: Eve holds each range alone (true), KRON no */
        static const int64_t fb[2] = {10, 10}, ln[2] = {5, 20}, us[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"filebytes", fb, 2}, {"linebytes", ln, 2}, {"undosteps", us, 2}};
        CHECK(wave_lp(app, "edit_buffer_ok", xa, 3) == ZXAPP_BOTH && !joinery_buffer_ok(10, 20, 0),
              "Joinery: a line past its file: Eve says yes, KRON no: BOTH");
    }

    /* undo_record_ok (JR6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, JOINERY_UNDO_MAX);
        b[i] = wrand(0, JOINERY_FILE_MAX);
        c[i] = wrand(0, JOINERY_FILE_MAX);
        d[i] = wrand(0, JOINERY_FILE_MAX);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0;                         /* every least */
    a[1] = 9999, b[1] = 99999999, c[1] = 99999999, d[1] = 99999999; /* every max */
    a[2] = 10000, b[2] = 0, c[2] = 0, d[2] = 0;                     /* step max + 1 */
    a[3] = 1, b[3] = 100000000, c[3] = 0, d[3] = 0;                 /* offset max + 1 */
    a[4] = 1, b[4] = 0, c[4] = 100000000, d[4] = 0;                 /* removed max + 1 */
    a[5] = 1, b[5] = 0, c[5] = 0, d[5] = 100000000;                 /* inserted max + 1 */
    a[6] = -9999, b[6] = -1, c[6] = -1, d[6] = -1;                  /* sign aside: they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(joinery_undo_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE,
          "Joinery: the undo record's own edges (JR6)");
    zxapp_arg_t ua[4] = {{"step", a, WAVE_ROWS},
                         {"offset", b, WAVE_ROWS},
                         {"removed", c, WAVE_ROWS},
                         {"inserted", d, WAVE_ROWS}};
    CHECK(wave_same(app, "undo_record_ok", ua, 4, want, WAVE_ROWS, true),
          "Joinery: undo_record_ok, MYRA = C");

    /* how full the buffer is (JR7), through the core's used_share: Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, JOINERY_FILE_MAX);
        a[i] = JOINERY_FILE_MAX;
        c[i] = JOINERY_FILE_MAX - b[i];
        want[i] = joinery_fill_x100(b[i]);
    }
    zxapp_arg_t fa[2] = {{"total", a, WAVE_ROWS}, {"left", c, WAVE_ROWS}};
    CHECK(joinery_fill_x100(0) == 0 && joinery_fill_x100(JOINERY_FILE_MAX) == 10000 &&
              joinery_fill_x100(JOINERY_FILE_MAX + 1) == JOINERY_REFUSED &&
              joinery_fill_x100(-1) == JOINERY_REFUSED &&
              wave_same(app, "used_share.used_x100", fa, 2, want, WAVE_ROWS, false),
          "Joinery: how full the buffer is through used_share.used_x100, Sutra = KRON = C");
}
