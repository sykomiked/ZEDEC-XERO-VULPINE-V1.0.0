/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pivot's languages against the C its model runs (pivot_rules.h).
 * Slots: @logic.pivot_dwell_select, @logic.pivot_control_limits_ok,
 * @logic.pivot_control_profile_ok, @logic.pivot_dwell_fires,
 * @num.pivot_scan_item, and, used as they are,
 * @logic.pinhole_camera_ready, @num.hollow_tile_cols and
 * @num.hollow_tile_rows. */
#include "pinhole_rules.h"
#include "pivot_rules.h"

APP_TEST(pivot, 26, 136)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges (PV1-PV5) */
    CHECK(pivot_dwell_select(0, 0, 200) && pivot_dwell_select(8191, 86400000, 5000) &&
              !pivot_dwell_select(-1, 0, 1000) && !pivot_dwell_select(0, -1, 1000) &&
              !pivot_dwell_select(8192, 0, 1000) && !pivot_dwell_select(0, 86400001, 1000) &&
              !pivot_dwell_select(0, 0, 199) && !pivot_dwell_select(0, 0, 5001) &&
              pivot_sample_edge(10, 10, 10, 10, 44, 44) == 0 &&
              pivot_sample_edge(53, 53, 10, 10, 44, 44) == 0 &&
              pivot_sample_edge(31, 32, 10, 10, 44, 44) == 21 &&
              pivot_sample_edge(9, 30, 10, 10, 44, 44) == -1 &&
              pivot_sample_edge(30, 54, 10, 10, 44, 44) == -1 &&
              pivot_control_limits_ok(200, 300, 44, 1, 0) &&
              pivot_control_limits_ok(5000, 10000, 16384, 2, 1) &&
              !pivot_control_limits_ok(1000, 1000, 43, 1, 0) &&
              !pivot_control_limits_ok(1000, 1000, 44, 3, 0) &&
              pivot_profile_record_ok(99, 9, 9999, 99999, 9, 99999) &&
              !pivot_profile_record_ok(100, 1, 1000, 1000, 0, 44) && pivot_dwell_fires(0, 0) &&
              !pivot_dwell_fires(1, 0) && pivot_dwell_fires(1, 1) && !pivot_dwell_fires(1, 2) &&
              !pivot_dwell_fires(2, 1) && !pivot_dwell_fires(-1, 1) &&
              pivot_scan_item(0, 300, 1) == 0 && pivot_scan_item(5999, 1000, 6) == 5 &&
              pivot_scan_item(6000, 1000, 6) == 0 && pivot_scan_item(999999999, 1000, 6) == 3 &&
              pivot_scan_item(1000000000, 1000, 6) == PIVOT_REFUSED &&
              pivot_scan_item(-1, 1000, 6) == PIVOT_REFUSED &&
              pivot_scan_item(0, 299, 6) == PIVOT_REFUSED &&
              pivot_scan_item(0, 10001, 6) == PIVOT_REFUSED &&
              pivot_scan_item(0, 1000, 0) == PIVOT_REFUSED &&
              pivot_scan_item(0, 1000, 65) == PIVOT_REFUSED,
          "Pivot: the C rules' own edges (PV1-PV5)");

    /* dwell_select (PV1): Eve = KRON = C, swept, with every edge inside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5 ? wrand(0, 8191) : wrand(-3, 3);
        b[i] = i % 4 ? wrand(0, 86400000) : wrand(-500, 500);
        c[i] = i % 3 ? wrand(200, 5000) : wrand(150, 5050);
    }
    a[0] = 0, b[0] = 0, c[0] = 200;               /* the least of each */
    a[1] = 8191, b[1] = 86400000, c[1] = 5000;    /* the most of each */
    a[2] = -1, b[2] = 0, c[2] = 1000;             /* a sample one pixel outside */
    a[3] = 0, b[3] = -1, c[3] = 1000;             /* a millisecond short */
    a[4] = 0, b[4] = 0, c[4] = 199;               /* a dwell below the limit */
    a[5] = 0, b[5] = 0, c[5] = 5001;              /* a dwell past it */
    a[6] = -8191, b[6] = -86400000, c[6] = -5000; /* everything negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(pivot_dwell_select(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Pivot: the dwell rule's edges (PV1)");
    zxapp_arg_t da[3] = {{"edge", a, WAVE_ROWS}, {"over", b, WAVE_ROWS}, {"dwell", c, WAVE_ROWS}};
    CHECK(wave_same(app, "dwell_select", da, 3, want, WAVE_ROWS, true),
          "Pivot: dwell to select, Eve = KRON = C");
    {
        static const int64_t e0[2] = {0, 8192}, o0[2] = {0, 0}, w0[2] = {1000, 1000};
        zxapp_arg_t xa[3] = {{"edge", e0, 2}, {"over", o0, 2}, {"dwell", w0, 2}};
        CHECK(wave_lp(app, "dwell_select", xa, 3) == ZXAPP_BOTH,
              "Pivot: an edge margin past any target: Eve refuses, KRON reads it as met: BOTH");
        static const int64_t e1[2] = {3, 3}, o1[2] = {0, 86400001}, w1[2] = {1000, 1000};
        zxapp_arg_t ya[3] = {{"edge", e1, 2}, {"over", o1, 2}, {"dwell", w1, 2}};
        CHECK(wave_lp(app, "dwell_select", ya, 3) == ZXAPP_BOTH,
              "Pivot: a run past a day: Eve refuses, KRON reads it as met: BOTH");
    }

    /* control_limits_ok (PV2): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(150, 5050);
        b[i] = wrand(250, 10050);
        c[i] = i % 2 ? wrand(40, 100) : wrand(16300, 16400);
        d[i] = wrand(0, 3);
        e[i] = wrand(-1, 2);
    }
    a[0] = 200, b[0] = 300, c[0] = 44, d[0] = 1, e[0] = 0;
    a[1] = 5000, b[1] = 10000, c[1] = 16384, d[1] = 2, e[1] = 1;
    a[2] = 199, b[2] = 300, c[2] = 44, d[2] = 1, e[2] = 0;
    a[3] = 200, b[3] = 10001, c[3] = 44, d[3] = 1, e[3] = 0;
    a[4] = 200, b[4] = 300, c[4] = 43, d[4] = 1, e[4] = 0;
    a[5] = 200, b[5] = 300, c[5] = 16385, d[5] = 1, e[5] = 0;
    a[6] = 200, b[6] = 300, c[6] = 44, d[6] = 0, e[6] = 0;
    a[7] = 200, b[7] = 300, c[7] = 44, d[7] = 1, e[7] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(pivot_control_limits_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[5] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Pivot: the limits' edges (PV2)");
    zxapp_arg_t la[5] = {{"dwell", a, WAVE_ROWS},
                         {"step", b, WAVE_ROWS},
                         {"side", c, WAVE_ROWS},
                         {"switches", d, WAVE_ROWS},
                         {"order", e, WAVE_ROWS}};
    CHECK(wave_same(app, "control_limits_ok", la, 5, want, WAVE_ROWS, true),
          "Pivot: a profile inside its limits, Eve = C");

    /* control_profile_ok (PV3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-100, 100);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-10000, 10000);
        d[i] = wrand(-100000, 100000);
        e[i] = wrand(-10, 10);
        f[i] = wrand(-100000, 100000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;
    a[1] = 99, b[1] = 9, c[1] = 9999, d[1] = 99999, e[1] = 9, f[1] = 99999;
    a[2] = 100, b[2] = 9, c[2] = 9999, d[2] = 99999, e[2] = 9, f[2] = 99999;
    a[3] = 99, b[3] = 10, c[3] = 9999, d[3] = 99999, e[3] = 9, f[3] = 99999;
    a[4] = 99, b[4] = 9, c[4] = 10000, d[4] = 99999, e[4] = 9, f[4] = 99999;
    a[5] = 99, b[5] = 9, c[5] = 9999, d[5] = 100000, e[5] = 9, f[5] = 99999;
    a[6] = 99, b[6] = 9, c[6] = 9999, d[6] = 99999, e[6] = 9, f[6] = 100000;
    a[7] = -99, b[7] = -9, c[7] = -9999, d[7] = -99999, e[7] = -9, f[7] = -99999;
    a[8] = 1, b[8] = 2, c[8] = 800, d[8] = 1500, e[8] = 1, f[8] = 88;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(pivot_profile_record_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE &&
              want[7] == ZXAPP_TRUE,
          "Pivot: the record's edges (PV3)");
    zxapp_arg_t pa[6] = {{"profile", a, WAVE_ROWS}, {"switches", b, WAVE_ROWS},
                         {"dwell", c, WAVE_ROWS},   {"step", d, WAVE_ROWS},
                         {"order", e, WAVE_ROWS},   {"side", f, WAVE_ROWS}};
    CHECK(wave_same(app, "control_profile_ok", pa, 6, want, WAVE_ROWS, true),
          "Pivot: a profile makes a ^ZXPIVOT record, MYRA = C");

    /* dwell_fires (PV4): Sutra = KRON = C, every flag pair, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (int64_t) (i >> 1) : wrand(0, 9);
        b[i] = i < 4 ? (int64_t) (i & 1) : wrand(0, 1);
        if (i > 3 && i % 3 == 0) a[i] = 1; /* more held rows */
    }
    a[4] = 9, b[4] = 1; /* the largest Sutra holds: no flag, never yes */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(pivot_dwell_fires(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE,
          "Pivot: a held action never fires on its first select (PV4)");
    zxapp_arg_t fa[2] = {{"held", a, WAVE_ROWS}, {"confirm", b, WAVE_ROWS}};
    CHECK(wave_same(app, "dwell_fires", fa, 2, want, WAVE_ROWS, true),
          "Pivot: a select fires, Sutra = KRON = C");
    {
        static const int64_t h0[2] = {1, 1}, c0[2] = {1, 2};
        zxapp_arg_t xa[2] = {{"held", h0, 2}, {"confirm", c0, 2}};
        CHECK(wave_lp(app, "dwell_fires", xa, 2) == ZXAPP_BOTH,
              "Pivot: a confirm of 2: Sutra reads no, KRON reads yes: BOTH, held");
        static const int64_t h1[2] = {0, -1}, c1[2] = {0, 0};
        zxapp_arg_t ya[2] = {{"held", h1, 2}, {"confirm", c1, 2}};
        CHECK(wave_lp(app, "dwell_fires", ya, 2) == ZXAPP_BOTH,
              "Pivot: a negative held flag: Sutra refuses, KRON answers: BOTH, held");
    }

    /* scan_item (PV5): KRON = Rust (wasm) = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 999999999) : wrand(0, 100000);
        b[i] = wrand(300, 10000);
        c[i] = wrand(1, 64);
    }
    a[0] = 0, b[0] = 300, c[0] = 1;
    a[1] = 999999999, b[1] = 10000, c[1] = 64;
    a[2] = 999999999, b[2] = 300, c[2] = 1;
    a[3] = 299, b[3] = 300, c[3] = 2;
    a[4] = 300, b[4] = 300, c[4] = 2;
    a[5] = 600, b[5] = 300, c[5] = 2;
    a[6] = 639999, b[6] = 10000, c[6] = 64;
    a[7] = 640000, b[7] = 10000, c[7] = 64;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = pivot_scan_item(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 99999 % 64 && want[3] == 0 && want[4] == 1 && want[5] == 0 &&
              want[6] == 63 && want[7] == 0,
          "Pivot: the scan's edges (PV5)");
    zxapp_arg_t sa[3] = {
        {"elapsed", a, WAVE_ROWS}, {"step", b, WAVE_ROWS}, {"items", c, WAVE_ROWS}};
    CHECK(wave_same(app, "scan_item", sa, 3, want, WAVE_ROWS, false),
          "Pivot: the item a scan lights, KRON = Rust = C");
    {
        static const int64_t t0[2] = {1000, 1000}, s0[2] = {1000, 200}, n0[2] = {6, 6};
        zxapp_arg_t xa[3] = {{"elapsed", t0, 2}, {"step", s0, 2}, {"items", n0, 2}};
        CHECK(wave_lp(app, "scan_item", xa, 3) == ZXAPP_BOTH,
              "Pivot: a step below the limit: Rust refuses, KRON answers: BOTH, held");
        static const int64_t t1[2] = {1000, 1000}, s1[2] = {1000, 1000}, n1[2] = {6, 65};
        zxapp_arg_t ya[3] = {{"elapsed", t1, 2}, {"step", s1, 2}, {"items", n1, 2}};
        CHECK(wave_lp(app, "scan_item", ya, 3) == ZXAPP_BOTH,
              "Pivot: 65 items, past the grid: Rust refuses, KRON answers: BOTH, held");
    }

    /* pinhole.camera_ready, used for "gaze only while the camera is ready" (PM2) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 8 ? (i >> 2) & 1 : wrand(0, 1);
        b[i] = i < 8 ? (i >> 1) & 1 : wrand(0, 1);
        c[i] = i < 8 ? i & 1 : wrand(0, 1);
        want[i] = T_(pinhole_camera_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t ca[3] = {
        {"sensor", a, WAVE_ROWS}, {"mode", b, WAVE_ROWS}, {"buffer", c, WAVE_ROWS}};
    CHECK(wave_same(app, "pinhole.camera_ready", ca, 3, want, WAVE_ROWS, true),
          "Pivot: the camera is ready, through pinhole.camera_ready");

    /* hollow.tile_cols and tile_rows, used for the row then column scan (PM5) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = (int64_t) i + 1; /* 1..64 items */
        want[i] = hollow_tile_cols(a[i]);
        b[i] = hollow_tile_rows(a[i]);
    }
    zxapp_arg_t ga[1] = {{"windows", a, WAVE_ROWS}};
    CHECK(wave_same(app, "hollow.tile_cols", ga, 1, want, WAVE_ROWS, false),
          "Pivot: the scan grid's columns, through hollow.tile_cols");
    CHECK(wave_same(app, "hollow.tile_rows", ga, 1, b, WAVE_ROWS, false),
          "Pivot: the scan grid's rows, through hollow.tile_rows");
}
