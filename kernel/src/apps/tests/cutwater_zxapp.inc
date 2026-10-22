/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cutwater's languages against the C its model runs (cutwater_rules.h).
 * Slots: @logic.cutwater_edit_clip_ok, @num.cutwater_frame_timecode,
 * @logic.cutwater_edit_clip_row_ok and, used as it is, Kinetoscope's
 * @num.kinetoscope_frame_at_pos. */
#include "cutwater_rules.h"

APP_TEST(cutwater, 12, 56)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        g[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t FM = CUTWATER_FRAME_MAX;

    /* edit_clip_ok (CR1): the C's own edges, then Eve = KRON = C swept */
    CHECK(cutwater_clip_ok(1, 0, 0) && cutwater_clip_ok(FM, FM, FM) && !cutwater_clip_ok(0, 0, 0) &&
              !cutwater_clip_ok(-1, 0, 0) && !cutwater_clip_ok(1, -1, 0) &&
              !cutwater_clip_ok(1, 0, -1) && !cutwater_clip_ok(FM + 1, 0, 0) &&
              !cutwater_clip_ok(1, FM + 1, 0) && !cutwater_clip_ok(1, 0, FM + 1),
          "Cutwater: the C clip rule's own edges (CR1)");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, i < 32 ? 100 : FM);
        b[i] = wrand(-5, i < 32 ? 100 : FM);
        c[i] = wrand(-5, i < 32 ? 100 : FM);
    }
    a[0] = 1, b[0] = 0, c[0] = 0;    /* the least clip, flush with its source and the next */
    a[1] = FM, b[1] = FM, c[1] = FM; /* every max */
    a[2] = 0, b[2] = 5, c[2] = 5;    /* in at out */
    a[3] = -1, b[3] = 5, c[3] = 5;   /* in after out */
    a[4] = 5, b[4] = -1, c[4] = 5;   /* out past the source */
    a[5] = 5, b[5] = 5, c[5] = -1;   /* over the next clip */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(cutwater_clip_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Cutwater: the swept clip rows' edges, by hand");
    {
        zxapp_arg_t ca[3] = {
            {"span", a, WAVE_ROWS}, {"tail", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
        CHECK(wave_same(app, "edit_clip_ok", ca, 3, want, WAVE_ROWS, true),
              "Cutwater: edit_clip_ok, Eve = KRON = C");
    }
    {
        /* a margin past 999999999 frames: Eve refuses it, KRON reads it as fitting */
        static const int64_t s2[2] = {5, 5}, t2[2] = {0, 0}, r2[2] = {0, FM + 1},
                             sb[2] = {5, FM + 1}, tb[2] = {0, FM + 1};
        zxapp_arg_t xa[3] = {{"span", s2, 2}, {"tail", t2, 2}, {"room", r2, 2}};
        CHECK(wave_lp(app, "edit_clip_ok", xa, 3) == ZXAPP_BOTH,
              "Cutwater: room past 999999999: Eve refuses, KRON answers: BOTH");
        zxapp_arg_t ya[3] = {{"span", sb, 2}, {"tail", t2, 2}, {"room", t2, 2}};
        CHECK(wave_lp(app, "edit_clip_ok", ya, 3) == ZXAPP_BOTH,
              "Cutwater: a span past 999999999: Eve refuses, KRON answers: BOTH");
        zxapp_arg_t za[3] = {{"span", s2, 2}, {"tail", tb, 2}, {"room", t2, 2}};
        CHECK(wave_lp(app, "edit_clip_ok", za, 3) == ZXAPP_BOTH,
              "Cutwater: a tail past 999999999: Eve refuses, KRON answers: BOTH");
    }

    /* frame_timecode (CR2): the C's own edges, then Trebago = KRON = C at six rates */
    CHECK(cutwater_timecode(0, 30, 1) == 0 && cutwater_timecode(1799, 30, 1) == 59029 &&
              cutwater_timecode(108000, 30000, 1001) == 10000000 &&
              cutwater_timecode(FM, 1, 1) == INT64_C(2777774639000) &&
              cutwater_timecode(FM, 999, 1) == 2780321000 &&
              cutwater_timecode(-1, 30, 1) == CUTWATER_REFUSED &&
              cutwater_timecode(FM + 1, 30, 1) == CUTWATER_REFUSED &&
              cutwater_timecode(1, 0, 1) == CUTWATER_REFUSED &&
              cutwater_timecode(1, 30, 0) == CUTWATER_REFUSED &&
              cutwater_timecode(1, 1000, 1) == CUTWATER_REFUSED &&
              cutwater_timecode(1, 999001, 1000) == CUTWATER_REFUSED &&
              cutwater_timecode(1, 999000, 1000) == 1 &&
              cutwater_timecode(1, CUTWATER_RATE_MAX + 1, CUTWATER_RATE_MAX) == CUTWATER_REFUSED &&
              cutwater_fps(30000, 1001) == 30 && cutwater_fps(24000, 1001) == 24 &&
              cutwater_fps(25, 1) == 25 && cutwater_fps(1, CUTWATER_RATE_MAX) == 1,
          "Cutwater: the C timecode's own edges (CR2)");
    {
        static const int64_t rn[6] = {30, 30000, 25, 24000, 60000, 120},
                             rd[6] = {1, 1001, 1, 1001, 1001, 1};
        for (uint32_t r = 0; r < 6; r++) {
            for (uint32_t i = 0; i < WAVE_ROWS; i++) {
                a[i] = wrand(0, i < 32 ? 400000 : FM);
                b[i] = rn[r];
                c[i] = rd[r];
            }
            a[0] = 0, a[1] = FM, a[2] = rn[r] / rd[r];
            for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cutwater_timecode(a[i], b[i], c[i]);
            zxapp_arg_t ta[3] = {
                {"frames", a, WAVE_ROWS}, {"fnum", b, WAVE_ROWS}, {"fden", c, WAVE_ROWS}};
            CHECK(wave_same(app, "frame_timecode", ta, 3, want, WAVE_ROWS, false),
                  "Cutwater: frame_timecode at %lld/%lld, Trebago = KRON = C", (long long) rn[r],
                  (long long) rd[r]);
        }
        /* the rates themselves swept, every frame count inside the domain */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            c[i] = wrand(1, 2000);
            b[i] = wrand(1, 999 * c[i] < CUTWATER_RATE_MAX ? 999 * c[i] : CUTWATER_RATE_MAX);
            a[i] = wrand(0, FM);
            want[i] = cutwater_timecode(a[i], b[i], c[i]);
        }
        zxapp_arg_t ta[3] = {
            {"frames", a, WAVE_ROWS}, {"fnum", b, WAVE_ROWS}, {"fden", c, WAVE_ROWS}};
        CHECK(wave_same(app, "frame_timecode", ta, 3, want, WAVE_ROWS, false),
              "Cutwater: frame_timecode at swept rates, Trebago = KRON = C");
    }
    {
        /* outside the domain Trebago refuses, KRON answers: held */
        static const int64_t fr[2] = {1, -1}, n30[2] = {30, 30}, d1[2] = {1, 1},
                             fb[2] = {1, FM + 1}, nb[2] = {30, 1000},
                             db[2] = {1, CUTWATER_RATE_MAX + 1};
        zxapp_arg_t xa[3] = {{"frames", fr, 2}, {"fnum", n30, 2}, {"fden", d1, 2}};
        CHECK(wave_lp(app, "frame_timecode", xa, 3) == ZXAPP_BOTH,
              "Cutwater: a negative frame count: Trebago refuses, KRON answers: BOTH");
        zxapp_arg_t ya[3] = {{"frames", fb, 2}, {"fnum", n30, 2}, {"fden", d1, 2}};
        CHECK(wave_lp(app, "frame_timecode", ya, 3) == ZXAPP_BOTH,
              "Cutwater: past 999999999 frames: Trebago refuses, KRON answers: BOTH");
        static const int64_t one[2] = {1, 1};
        zxapp_arg_t za[3] = {{"frames", one, 2}, {"fnum", nb, 2}, {"fden", d1, 2}};
        CHECK(wave_lp(app, "frame_timecode", za, 3) == ZXAPP_BOTH,
              "Cutwater: a nominal rate past 999: Trebago refuses, KRON answers: BOTH");
        zxapp_arg_t wa[3] = {{"frames", one, 2}, {"fnum", n30, 2}, {"fden", db, 2}};
        CHECK(wave_lp(app, "frame_timecode", wa, 3) == ZXAPP_BOTH,
              "Cutwater: fden past 999999: Trebago refuses, KRON answers: BOTH");
    }

    /* edit_clip_row_ok (CR3): MYRA's widths = C, swept and at every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-199, 199);
        b[i] = wrand(-19, 19);
        c[i] = wrand(-19, 19);
        d[i] = wrand(-1999999999, 1999999999);
        e[i] = wrand(-1999999999, 1999999999);
        g[i] = wrand(-1999999999, 1999999999);
    }
    a[0] = 0, b[0] = 1, c[0] = 1, d[0] = 0, e[0] = 1, g[0] = 0;
    a[1] = 99, b[1] = 9, c[1] = 9, d[1] = FM, e[1] = FM, g[1] = FM;
    a[2] = 100, b[2] = 1, c[2] = 1, d[2] = 0, e[2] = 1, g[2] = 0;
    a[3] = 1, b[3] = 10, c[3] = 1, d[3] = 0, e[3] = 1, g[3] = 0;
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = 0, e[4] = 1, g[4] = FM + 1;
    a[5] = -99, b[5] = -9, c[5] = -9, d[5] = -FM, e[5] = -FM, g[5] = -FM;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(cutwater_clip_row_ok(a[i], b[i], c[i], d[i], e[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE,
          "Cutwater: the C clip record's own edges (CR3)");
    {
        zxapp_arg_t ma[6] = {{"clip", a, WAVE_ROWS},   {"track", b, WAVE_ROWS},
                             {"source", c, WAVE_ROWS}, {"cutin", d, WAVE_ROWS},
                             {"cutout", e, WAVE_ROWS}, {"place", g, WAVE_ROWS}};
        CHECK(wave_same(app, "edit_clip_row_ok", ma, 6, want, WAVE_ROWS, true),
              "Cutwater: edit_clip_row_ok, MYRA = C");
    }

    /* kinetoscope.frame_at_pos (CR4): a playhead's frame, whole frames at 25 a second */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 40 * wrand(0, 8999999);
        b[i] = 25;
        c[i] = 1;
        want[i] = cutwater_frame_at_ms(a[i], 25, 1);
    }
    a[0] = 0, want[0] = 0;
    CHECK(cutwater_frame_at_ms(1001, 30000, 1001) == 30 && cutwater_frame_at_ms(-1, 30, 1) < 0,
          "Cutwater: a playhead's frame by hand (CR4)");
    {
        zxapp_arg_t pa[3] = {{"pos", a, WAVE_ROWS}, {"fnum", b, WAVE_ROWS}, {"fden", c, WAVE_ROWS}};
        CHECK(wave_same(app, "kinetoscope.frame_at_pos", pa, 3, want, WAVE_ROWS, false),
              "Cutwater: a playhead through kinetoscope.frame_at_pos, Trebago = C");
    }
}
