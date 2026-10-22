/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Glimpse's languages against the C its model runs (glimpse_rules.h).
 * Slots: @num.glimpse_capture_origin, @num.glimpse_recording_bytes,
 * @num.glimpse_first_free_n, and, used as they are, @num.hollow_shown and
 * @logic.lifeboat_restore_fits. */
#include "glimpse_rules.h"

/* first_free_n over one used list: KRON answers one number for it */
static bool glm_free(zxapp_t *app, const int64_t *used, uint32_t n)
{
    int64_t want[1] = {glimpse_first_free(used, n)};
    zxapp_arg_t a[1] = {{"used", used, n}};
    if (want[0] >= 1 && wave_same(app, "first_free_n", a, 1, want, 1, false)) return true;
    printf("    first_free_n over %u numbers, want %lld\n", n, (long long) want[0]);
    return false;
}

APP_TEST(glimpse, 25, 111)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges (GR1-GR5) */
    CHECK(glimpse_capture_origin(-5, 1920) == 0 && glimpse_capture_origin(0, 1920) == 0 &&
              glimpse_capture_origin(1920, 1920) == 1920 &&
              glimpse_capture_origin(3000, 1920) == 1920 &&
              glimpse_capture_origin(-99999, 1) == 0 &&
              glimpse_capture_origin(99999, 16384) == 16384 &&
              glimpse_capture_origin(-100000, 1920) == GLIMPSE_REFUSED &&
              glimpse_capture_origin(100000, 1920) == GLIMPSE_REFUSED &&
              glimpse_capture_origin(10, 0) == GLIMPSE_REFUSED &&
              glimpse_capture_origin(10, 16385) == GLIMPSE_REFUSED,
          "Glimpse: capture_origin's own edges (GR1)");
    CHECK(glimpse_capture_extent(-100, 300, 1920) == 200 &&
              glimpse_capture_extent(1900, 800, 1920) == 20 &&
              glimpse_capture_extent(2000, 10, 1920) == 0 &&
              glimpse_capture_extent(0, -1, 1920) == HOLLOW_REFUSED,
          "Glimpse: the extent through hollow.shown, never negative (GR2)");
    CHECK(glimpse_recording_bytes(1920, 1080, 4, 10, 30, 2488320000) == 2488320000 &&
              glimpse_recording_bytes(1920, 1080, 4, 10, 30, 2488319999) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 1, 1, 0) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 1, 1, 1) == 1 &&
              glimpse_recording_bytes(16384, 16384, 8, 7200, 60, GLIMPSE_ROOM_MAX) ==
                  INT64_C(927712935936000) &&
              glimpse_recording_bytes(0, 1, 1, 1, 1, 9) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(16385, 1, 1, 1, 1, 9) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 9, 1, 1, 9) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 7201, 1, 999999) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 1, 61, 999) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 1, 1, -1) == GLIMPSE_REFUSED &&
              glimpse_recording_bytes(1, 1, 1, 1, 1, GLIMPSE_ROOM_MAX + 1) == GLIMPSE_REFUSED,
          "Glimpse: recording_bytes' own edges (GR3)");
    {
        static const int64_t u1[4] = {1, 3, 2, 6}, u2[1] = {0}, u3[5] = {2, 2, -1, 1, 1};
        CHECK(glimpse_first_free(u1, 4) == 4 && glimpse_first_free(u2, 1) == 1 &&
                  glimpse_first_free(u3, 5) == 3 &&
                  glimpse_first_free(NULL, 1) == GLIMPSE_REFUSED &&
                  glimpse_first_free(u1, 0) == GLIMPSE_REFUSED &&
                  glimpse_first_free(u1, GLIMPSE_USED_MAX + 1u) == GLIMPSE_REFUSED,
              "Glimpse: first_free_n's own edges (GR4)");
    }
    CHECK(glimpse_shot_fits(3000000, 4000000, 1000000) &&
              !glimpse_shot_fits(3000001, 4000000, 1000000) && !glimpse_shot_fits(-1, 10, 0),
          "Glimpse: a screenshot fits, through lifeboat.restore_fits (GR5)");

    /* capture_origin (GR1): KRON = Trebago = C, the edges, then swept around the screen */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, 16384);
        a[i] = i % 3 ? wrand(-99999, 99999) : wrand(-b[i], 2 * b[i]);
    }
    a[0] = -1, b[0] = 1920;
    a[1] = 0, b[1] = 1920;
    a[2] = 1919, b[2] = 1920;
    a[3] = 1920, b[3] = 1920;
    a[4] = 1921, b[4] = 1920;
    a[5] = -99999, b[5] = 1;
    a[6] = 99999, b[6] = 16384;
    a[7] = 5, b[7] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = glimpse_capture_origin(a[i], b[i]);
    zxapp_arg_t oa[2] = {{"pos", a, WAVE_ROWS}, {"span", b, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[3] == 1920 && want[4] == 1920 && want[6] == 16384 && want[7] == 1 &&
              wave_same(app, "capture_origin", oa, 2, want, WAVE_ROWS, false),
          "Glimpse: capture_origin, KRON = Trebago = C");
    {
        /* a screen side of 0 or past 16384, or a corner past 99999: Trebago refuses, KRON clamps */
        static const int64_t p1[2] = {10, 10}, s0[2] = {1920, 0}, sbig[2] = {1920, 16385},
                             pbig[2] = {10, 100000}, pneg[2] = {10, -100000}, s1[2] = {1920, 1920};
        zxapp_arg_t x1[2] = {{"pos", p1, 2}, {"span", s0, 2}};
        zxapp_arg_t x2[2] = {{"pos", p1, 2}, {"span", sbig, 2}};
        zxapp_arg_t x3[2] = {{"pos", pbig, 2}, {"span", s1, 2}};
        zxapp_arg_t x4[2] = {{"pos", pneg, 2}, {"span", s1, 2}};
        CHECK(wave_lp(app, "capture_origin", x1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "capture_origin", x2, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "capture_origin", x3, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "capture_origin", x4, 2) == ZXAPP_BOTH,
              "Glimpse: capture_origin past its domain: Trebago refuses, KRON clamps: BOTH");
    }

    /* recording_bytes (GR3): Trebago = KRON = C, fitting and one byte past the room */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 16384);
        b[i] = wrand(1, 16384);
        c[i] = wrand(1, 8);
        d[i] = wrand(1, 7200);
        e[i] = wrand(1, 60);
        int64_t raw = a[i] * b[i] * c[i] * d[i] * e[i];
        f[i] = i % 4 == 0 ? raw : i % 4 == 1 ? raw - 1 : wrand(0, 2 * raw);
    }
    a[0] = 1920, b[0] = 1080, c[0] = 4, d[0] = 10, e[0] = 30, f[0] = 2488320000;
    a[1] = 1920, b[1] = 1080, c[1] = 4, d[1] = 10, e[1] = 30, f[1] = 2488319999;
    a[2] = 1, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1, f[2] = 0;
    a[3] = 16384, b[3] = 16384, c[3] = 8, d[3] = 7200, e[3] = 60, f[3] = GLIMPSE_ROOM_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = glimpse_recording_bytes(a[i], b[i], c[i], d[i], e[i], f[i]);
    zxapp_arg_t ra[6] = {{"w", a, WAVE_ROWS},    {"h", b, WAVE_ROWS},   {"bpp", c, WAVE_ROWS},
                         {"secs", d, WAVE_ROWS}, {"fps", e, WAVE_ROWS}, {"room", f, WAVE_ROWS}};
    CHECK(want[0] == 2488320000 && want[1] == GLIMPSE_REFUSED && want[2] == GLIMPSE_REFUSED &&
              want[3] == INT64_C(927712935936000) && want[4] > 0 && want[5] == GLIMPSE_REFUSED &&
              wave_same(app, "recording_bytes", ra, 6, want, WAVE_ROWS, false),
          "Glimpse: recording_bytes, Trebago = KRON = C");
    {
        /* past 60 fps, past 7200 s, no width: Trebago refuses, KRON multiplies: BOTH */
        static const int64_t w2[2] = {1920, 1920}, h2[2] = {1080, 1080}, p2[2] = {4, 4},
                             s2[2] = {10, 10}, s7201[2] = {10, 7201}, f2[2] = {30, 30},
                             f61[2] = {30, 61}, w0[2] = {1920, 0},
                             r2[2] = {999999999, 999999999999};
        zxapp_arg_t x1[6] = {{"w", w2, 2},    {"h", h2, 2},    {"bpp", p2, 2},
                             {"secs", s2, 2}, {"fps", f61, 2}, {"room", r2, 2}};
        zxapp_arg_t x2[6] = {{"w", w2, 2},       {"h", h2, 2},   {"bpp", p2, 2},
                             {"secs", s7201, 2}, {"fps", f2, 2}, {"room", r2, 2}};
        zxapp_arg_t x3[6] = {{"w", w0, 2},    {"h", h2, 2},   {"bpp", p2, 2},
                             {"secs", s2, 2}, {"fps", f2, 2}, {"room", r2, 2}};
        CHECK(wave_lp(app, "recording_bytes", x1, 6) == ZXAPP_BOTH &&
                  wave_lp(app, "recording_bytes", x2, 6) == ZXAPP_BOTH &&
                  wave_lp(app, "recording_bytes", x3, 6) == ZXAPP_BOTH,
              "Glimpse: recording_bytes past its limits: Trebago refuses, KRON answers: BOTH");
    }

    /* first_free_n (GR4): KRON = C over 64 used lists, gaps, repeats and noise */
    {
        static int64_t used[GLIMPSE_USED_MAX];
        bool same = true;
        for (uint32_t r = 0; r < WAVE_ROWS; r++) {
            uint32_t n = (uint32_t) wrand(1, r < 8 ? 8 : 200);
            for (uint32_t i = 0; i < n; i++)
                used[i] = r % 4 == 0 ? (int64_t) i + 1 : wrand(-3, (int64_t) n + 2);
            same = glm_free(app, used, n) && same;
        }
        for (uint32_t i = 0; i < GLIMPSE_USED_MAX; i++) used[i] = GLIMPSE_USED_MAX - i;
        same = glm_free(app, used, GLIMPSE_USED_MAX) && same; /* all taken: the next */
        used[100] = 0;
        same = glm_free(app, used, GLIMPSE_USED_MAX) && same; /* one gap */
        CHECK(same, "Glimpse: first_free_n, KRON = C, 66 lists");
    }

    /* hollow.shown, used for the region's extent (GR2): Sutra = KRON = the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = wrand(1, 16384);
        a[i] = wrand(-c[i], 2 * c[i]);
        b[i] = wrand(0, 2 * c[i]);
        want[i] = glimpse_capture_extent(a[i], b[i], c[i]);
    }
    zxapp_arg_t sa[3] = {{"pos", a, WAVE_ROWS}, {"size", b, WAVE_ROWS}, {"span", c, WAVE_ROWS}};
    CHECK(wave_same(app, "hollow.shown", sa, 3, want, WAVE_ROWS, false),
          "Glimpse: the region's extent, through hollow.shown");
    {
        static const int64_t p0[2] = {0, 0}, sz[2] = {800, -1}, sp[2] = {1920, 1920};
        zxapp_arg_t xa[3] = {{"pos", p0, 2}, {"size", sz, 2}, {"span", sp, 2}};
        CHECK(wave_lp(app, "hollow.shown", xa, 3) == ZXAPP_BOTH,
              "Glimpse: a negative size: Sutra refuses, KRON answers: BOTH");
    }

    /* lifeboat.restore_fits, used for "a screenshot fits" (GR5): Sutra = KRON = the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 8388608);
        c[i] = wrand(0, 1000000000);
        b[i] = i % 2 ? a[i] + c[i] + wrand(-1000, 1000) : wrand(0, INT64_C(1) << 40);
        if (b[i] < 0) b[i] = 0;
    }
    a[0] = 0, b[0] = 0, c[0] = 0;
    a[1] = 3000000, b[1] = 4000000, c[1] = 1000000;
    a[2] = 3000001, b[2] = 4000000, c[2] = 1000000;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(glimpse_shot_fits(a[i], b[i], c[i]));
    zxapp_arg_t fa[3] = {{"need", a, WAVE_ROWS}, {"free", b, WAVE_ROWS}, {"reserve", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "lifeboat.restore_fits", fa, 3, want, WAVE_ROWS, true),
          "Glimpse: a screenshot fits, through lifeboat.restore_fits");
    {
        static const int64_t nn[2] = {1, -1}, fr[2] = {10, 10}, rs[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"need", nn, 2}, {"free", fr, 2}, {"reserve", rs, 2}};
        CHECK(wave_lp(app, "lifeboat.restore_fits", xa, 3) == ZXAPP_BOTH,
              "Glimpse: a negative size: Sutra refuses, KRON answers: BOTH");
    }
}
