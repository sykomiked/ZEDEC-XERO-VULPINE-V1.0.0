/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Broadsheet's languages against the C its model runs (broadsheet_rules.h).
 * Slots: @logic.broadsheet_well_formed, @num.broadsheet_form_fault,
 * @num.broadsheet_fit_zoom, @logic.broadsheet_note_ok. */
#include "broadsheet_rules.h"

APP_TEST(broadsheet, 8, 42)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t FM = BROADSHEET_FILE_MAX;

    /* well_formed and form_fault (BR1, BR2): one offset a row, the header a
     * flag, the offset and room inside zxvd's largest file or below 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5 ? 1 : 0;
        b[i] = i % 7 ? wrand(0, i % 2 ? 100000 : FM - 1) : wrand(-1000, -1);
        c[i] = i % 3 ? wrand(1, i % 2 ? 100000 : FM) : wrand(-1000, 0);
    }
    a[0] = 1, b[0] = 0, c[0] = 1;      /* the first byte of a one-byte file */
    a[1] = 1, b[1] = FM - 1, c[1] = 1; /* the last byte of the largest file */
    a[2] = 1, b[2] = 0, c[2] = FM;     /* the first byte of the largest file */
    a[3] = 1, b[3] = 5000, c[3] = 0;   /* the offset is the size: outside */
    a[4] = 1, b[4] = 5001, c[4] = -1;  /* past the size */
    a[5] = 1, b[5] = -1, c[5] = 5001;  /* below 0 */
    a[6] = 0, b[6] = 0, c[6] = 5000;   /* no header */
    a[7] = 0, b[7] = -1, c[7] = 0;     /* no header first, whatever else */
    a[8] = 1, b[8] = 4999, c[8] = 1;   /* the last byte of a 5000-byte file */
    a[9] = 1, b[9] = 0, c[9] = 0;      /* an empty file has no byte to name */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(broadsheet_well_formed(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE &&
              want[9] == ZXAPP_FALSE && !broadsheet_well_formed(1, FM, 1) &&
              !broadsheet_well_formed(1, 0, FM + 1) && !broadsheet_well_formed(2, 0, 1),
          "Broadsheet: the C well formed's own edges (BR1)");
    zxapp_arg_t wa[3] = {{"header", a, WAVE_ROWS}, {"off", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
    CHECK(wave_same(app, "well_formed", wa, 3, want, WAVE_ROWS, true),
          "Broadsheet: well_formed, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = broadsheet_form_fault(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 0 && want[2] == 0 && want[3] == 2 && want[4] == 2 &&
              want[5] == 2 && want[6] == 1 && want[7] == 1 && want[8] == 0 && want[9] == 2 &&
              broadsheet_form_fault(2, 0, 1) == BROADSHEET_REFUSED &&
              broadsheet_form_fault(-1, 0, 1) == BROADSHEET_REFUSED &&
              broadsheet_form_fault(1, FM, 1) == BROADSHEET_REFUSED &&
              broadsheet_form_fault(1, 0, FM + 1) == BROADSHEET_REFUSED,
          "Broadsheet: the C form fault's own edges (BR2)");
    CHECK(wave_same(app, "form_fault", wa, 3, want, WAVE_ROWS, false),
          "Broadsheet: form_fault, Rust = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        CHECK((broadsheet_form_fault(a[i], b[i], c[i]) == 0) ==
                  (broadsheet_well_formed(a[i], b[i], c[i]) == 1),
              "Broadsheet: row %u, well formed exactly when no fault", i);
    {
        static const int64_t h2[2] = {1, 2}, o0[2] = {0, 0}, r10[2] = {10, 10}, h1[2] = {1, 1},
                             obig[2] = {0, BROADSHEET_FILE_MAX}, r1[2] = {10, 1},
                             rbig[2] = {10, BROADSHEET_FILE_MAX + 1}, hn[2] = {1, -1};
        zxapp_arg_t x1[3] = {{"header", h2, 2}, {"off", o0, 2}, {"room", r10, 2}};
        zxapp_arg_t x2[3] = {{"header", h1, 2}, {"off", obig, 2}, {"room", r1, 2}};
        zxapp_arg_t x3[3] = {{"header", h1, 2}, {"off", o0, 2}, {"room", rbig, 2}};
        zxapp_arg_t x4[3] = {{"header", hn, 2}, {"off", o0, 2}, {"room", r10, 2}};
        CHECK(wave_lp(app, "well_formed", x1, 3) == ZXAPP_BOTH,
              "Broadsheet: a header of 2: Eve says no flag, KRON reads it present: BOTH");
        CHECK(wave_lp(app, "well_formed", x2, 3) == ZXAPP_BOTH,
              "Broadsheet: an offset past 256 MiB: Eve refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "form_fault", x1, 3) == ZXAPP_BOTH,
              "Broadsheet: a header of 2: Rust traps, KRON answers: BOTH");
        CHECK(wave_lp(app, "form_fault", x3, 3) == ZXAPP_BOTH,
              "Broadsheet: a room past 256 MiB: Rust traps, KRON answers: BOTH");
        CHECK(wave_lp(app, "form_fault", x4, 3) == ZXAPP_BOTH,
              "Broadsheet: a header of -1: Rust traps, KRON reads no header: BOTH");
    }

    /* BR1 over a document: every offset inside, at least one */
    {
        static const int64_t ok3[3] = {9, 400, 4999}, out3[3] = {9, 400, 5000};
        CHECK(broadsheet_all_inside(1, ok3, 3, 5000) == 1 &&
                  broadsheet_all_inside(1, out3, 3, 5000) == 0 &&
                  broadsheet_all_inside(0, ok3, 3, 5000) == 0 &&
                  broadsheet_all_inside(1, ok3, 0, 5000) == 0 &&
                  broadsheet_all_inside(1, NULL, 3, 5000) == 0 &&
                  broadsheet_all_inside(1, ok3, 3, -1) == BROADSHEET_REFUSED &&
                  broadsheet_all_inside(1, ok3, 3, BROADSHEET_FILE_MAX + 1) == BROADSHEET_REFUSED,
              "Broadsheet: a document is well formed when every offset is (BR1)");
    }

    /* fit_zoom (BR3): pages and views swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i % 2 ? 4000 : BROADSHEET_SIDE_MAX);
        b[i] = wrand(1, i % 2 ? 4000 : BROADSHEET_SIDE_MAX);
        c[i] = wrand(0, i % 3 ? 4000 : BROADSHEET_SIDE_MAX);
        d[i] = wrand(0, i % 3 ? 4000 : BROADSHEET_SIDE_MAX);
    }
    a[0] = 816, b[0] = 1056, c[0] = 816, d[0] = 1056;   /* Letter at its own size */
    a[1] = 816, b[1] = 1056, c[1] = 1600, d[1] = 900;   /* a wide view: the height wins */
    a[2] = 1, b[2] = 1, c[2] = 99999, d[2] = 99999;     /* the most */
    a[3] = 99999, b[3] = 99999, c[3] = 0, d[3] = 99999; /* no view across */
    a[4] = 816, b[4] = 1056, c[4] = 408, d[4] = 1056;   /* half across */
    a[5] = 3, b[5] = 7, c[5] = 2, d[5] = 5;             /* 6666.6: down */
    a[6] = 99999, b[6] = 99999, c[6] = 99999, d[6] = 99999;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = broadsheet_fit_zoom(a[i], b[i], c[i], d[i]);
    CHECK(want[0] == 10000 && want[1] == 8522 && want[2] == 999990000 && want[3] == 0 &&
              want[4] == 5000 && want[5] == 6666 && want[6] == 10000 &&
              broadsheet_fit_zoom(0, 1, 1, 1) == BROADSHEET_REFUSED &&
              broadsheet_fit_zoom(1, 1, -1, 1) == BROADSHEET_REFUSED &&
              broadsheet_fit_zoom(100000, 1, 1, 1) == BROADSHEET_REFUSED &&
              broadsheet_fit_zoom(1, 1, 1, 100000) == BROADSHEET_REFUSED,
          "Broadsheet: the C zoom to fit's own edges (BR3)");
    zxapp_arg_t fa[4] = {{"pagew", a, WAVE_ROWS},
                         {"pageh", b, WAVE_ROWS},
                         {"vieww", c, WAVE_ROWS},
                         {"viewh", d, WAVE_ROWS}};
    CHECK(wave_same(app, "fit_zoom", fa, 4, want, WAVE_ROWS, false),
          "Broadsheet: fit_zoom, KRON = Sutra = C");
    {
        static const int64_t pw[2] = {816, 816}, ph[2] = {1056, 1056}, vw[2] = {816, 100000},
                             vh[2] = {1056, 1056}, pbig[2] = {816, 100000}, vn[2] = {816, -1};
        zxapp_arg_t x1[4] = {
            {"pagew", pw, 2}, {"pageh", ph, 2}, {"vieww", vw, 2}, {"viewh", vh, 2}};
        zxapp_arg_t x2[4] = {
            {"pagew", pbig, 2}, {"pageh", ph, 2}, {"vieww", pw, 2}, {"viewh", vh, 2}};
        zxapp_arg_t x3[4] = {
            {"pagew", pw, 2}, {"pageh", ph, 2}, {"vieww", vn, 2}, {"viewh", vh, 2}};
        CHECK(wave_lp(app, "fit_zoom", x1, 4) == ZXAPP_BOTH,
              "Broadsheet: a view past 9(5): Sutra refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "fit_zoom", x2, 4) == ZXAPP_BOTH,
              "Broadsheet: a page past 9(5): Sutra refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "fit_zoom", x3, 4) == ZXAPP_BOTH,
              "Broadsheet: a view below 0: Sutra refuses, KRON answers: BOTH");
    }

    /* note_ok (BR4): each field inside its range or past MYRA's digits, where
     * MYRA and Eve agree */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        bool past = i % 4 == 3;
        uint32_t which = (uint32_t) wrand(0, 5);
        a[i] = past && which == 0 ? wrand(10000, 99999) : wrand(1, 9999);
        b[i] = past && which == 1 ? wrand(1000000, 9999999) : wrand(1, 999999);
        c[i] = past && which == 2 ? wrand(100000, 999999) : wrand(0, 99999);
        d[i] = past && which == 3 ? wrand(100000, 999999) : wrand(0, 99999);
        e[i] = past && which == 4 ? wrand(10, 99) : wrand(1, 3);
        f[i] = past && which == 5 ? wrand(INT64_C(1000000000000000), INT64_C(9999999999999999))
                                  : wrand(0, BROADSHEET_STAMP_MAX);
        want[i] = T_(broadsheet_note_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"note", a, WAVE_ROWS}, {"page", b, WAVE_ROWS}, {"x", c, WAVE_ROWS},
                         {"y", d, WAVE_ROWS},    {"kind", e, WAVE_ROWS}, {"stamp", f, WAVE_ROWS}};
    CHECK(wave_same(app, "note_ok", na, 6, want, WAVE_ROWS, true),
          "Broadsheet: note_ok sweep, MYRA = Eve = C");
    {
        static const int64_t en[8] = {1, 9999, 10000, 1, 1, 1, 1, 1},
                             ep[8] = {1, 999999, 1, 1000000, 1, 1, 1, 1},
                             ex[8] = {0, 99999, 0, 0, 100000, 0, 0, 0},
                             ey[8] = {0, 99999, 0, 0, 0, 0, 0, 0},
                             ek[8] = {1, 3, 1, 1, 1, 10, 2, 1};
        static const int64_t es[8] = {
            0, BROADSHEET_STAMP_MAX, 0, 0, 0, 0, INT64_C(1700000000000), BROADSHEET_STAMP_MAX + 1};
        int64_t ew[8];
        for (uint32_t i = 0; i < 8; i++)
            ew[i] = T_(broadsheet_note_ok(en[i], ep[i], ex[i], ey[i], ek[i], es[i]));
        zxapp_arg_t xa[6] = {{"note", en, 8}, {"page", ep, 8}, {"x", ex, 8},
                             {"y", ey, 8},    {"kind", ek, 8}, {"stamp", es, 8}};
        CHECK(ew[0] == ZXAPP_TRUE && ew[1] == ZXAPP_TRUE && ew[2] == ZXAPP_FALSE &&
                  ew[3] == ZXAPP_FALSE && ew[4] == ZXAPP_FALSE && ew[5] == ZXAPP_FALSE &&
                  ew[6] == ZXAPP_TRUE && ew[7] == ZXAPP_FALSE &&
                  wave_same(app, "note_ok", xa, 6, ew, 8, true),
              "Broadsheet: note_ok at its edges and one digit past each");
        static const int64_t n1[2] = {1, 1}, n0[2] = {1, 0}, z[2] = {0, 0}, k4[2] = {1, 4},
                             kn[2] = {1, -1};
        zxapp_arg_t x1[6] = {{"note", n1, 2}, {"page", n1, 2}, {"x", z, 2},
                             {"y", z, 2},     {"kind", k4, 2}, {"stamp", z, 2}};
        zxapp_arg_t x2[6] = {{"note", n0, 2}, {"page", n1, 2}, {"x", z, 2},
                             {"y", z, 2},     {"kind", n1, 2}, {"stamp", z, 2}};
        zxapp_arg_t x3[6] = {{"note", n1, 2}, {"page", n1, 2}, {"x", z, 2},
                             {"y", z, 2},     {"kind", kn, 2}, {"stamp", z, 2}};
        CHECK(wave_lp(app, "note_ok", x1, 6) == ZXAPP_BOTH,
              "Broadsheet: kind 4: MYRA's one digit holds it, Eve knows no such kind: BOTH");
        CHECK(wave_lp(app, "note_ok", x2, 6) == ZXAPP_BOTH,
              "Broadsheet: note 0: MYRA holds it, Eve numbers from 1: BOTH");
        CHECK(wave_lp(app, "note_ok", x3, 6) == ZXAPP_BOTH,
              "Broadsheet: kind -1: MYRA holds it (sign aside), Eve refuses: BOTH");
    }
}
