/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pinhole's languages against the C its model runs (pinhole_rules.h).
 * Slots: @logic.pinhole_camera_ready, @num.pinhole_camera_ready_n,
 * @logic.pinhole_exposure_ok, @logic.pinhole_photo_record_ok, and, used as
 * they are, @logic.lifeboat_restore_fits, @num.furrow_range_total and
 * @logic.gatehouse_use_ok. */
#include "pinhole_rules.h"

/* one camera per call: sensor mode buffer */
static bool ph_row(zxapp_t *app, int64_t s, int64_t m, int64_t b, int64_t want)
{
    int64_t sa[1] = {s}, ma[1] = {m}, ba[1] = {b}, w[1] = {want};
    zxapp_arg_t a[3] = {{"sensor", sa, 1}, {"mode", ma, 1}, {"buffer", ba, 1}};
    if (wave_same(app, "camera_ready_n", a, 3, w, 1, false)) return true;
    printf("    camera_ready_n sensor %lld mode %lld buffer %lld\n", (long long) s, (long long) m,
           (long long) b);
    return false;
}

static int32_t ph_one(zxapp_t *app, int64_t s, int64_t m, int64_t b)
{
    int64_t sa[1] = {s}, ma[1] = {m}, ba[1] = {b};
    zxapp_arg_t a[3] = {{"sensor", sa, 1}, {"mode", ma, 1}, {"buffer", ba, 1}};
    return wave_lp(app, "camera_ready_n", a, 3);
}

APP_TEST(pinhole, 28, 58)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges (PH1-PH7) */
    CHECK(pinhole_camera_ready(1, 1, 1) && !pinhole_camera_ready(0, 1, 1) &&
              !pinhole_camera_ready(1, 2, 1) && !pinhole_camera_ready(1, 1, -1) &&
              pinhole_camera_ready_n(1, 1, 1) == 1 && pinhole_camera_ready_n(1, 0, 1) == 0 &&
              pinhole_camera_ready_n(2, 1, 1) == PINHOLE_REFUSED &&
              pinhole_camera_ready_n(1, 1, -1) == PINHOLE_REFUSED &&
              pinhole_exposure_ok(25, 31, -9) && pinhole_exposure_ok(25600, 30000000, 9) &&
              !pinhole_exposure_ok(24, 8000, 0) && !pinhole_exposure_ok(25601, 8000, 0) &&
              !pinhole_exposure_ok(100, 30, 0) && !pinhole_exposure_ok(100, 30000001, 0) &&
              !pinhole_exposure_ok(100, 8000, -10) && !pinhole_exposure_ok(100, 8000, 10) &&
              pinhole_photo_fits(3000000, 4000000, 1000000) &&
              !pinhole_photo_fits(3000001, 4000000, 1000000) && !pinhole_photo_fits(-1, 10, 0) &&
              pinhole_place_ok(1, 1, 0) && !pinhole_place_ok(1, 1, 1) &&
              !pinhole_place_ok(1, 2, 0) && pinhole_record_ok(1, 0, 1, 4032, 3024, 1, 0) &&
              !pinhole_record_ok(1000000000, 0, 1, 1, 1, 0, 0) &&
              !pinhole_record_ok(1, 10000, 1, 1, 1, 0, 0),
          "Pinhole: the C rules' own edges (PH1-PH7)");

    /* camera_ready (PH1): Eve = KRON = C, every flag pattern, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 8 ? (i >> 2) & 1 : wrand(0, 1);
        b[i] = i < 8 ? (i >> 1) & 1 : wrand(0, 1);
        c[i] = i < 8 ? i & 1 : wrand(0, 1);
        if (i > 7 && i % 3 == 0) a[i] = b[i] = c[i] = 1; /* more ready rows */
        if (i > 7 && i % 7 == 0) a[i] = -1;              /* below a flag: no on both sides */
        want[i] = T_(pinhole_camera_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {
        {"sensor", a, WAVE_ROWS}, {"mode", b, WAVE_ROWS}, {"buffer", c, WAVE_ROWS}};
    CHECK(want[7] == ZXAPP_TRUE && want[6] == ZXAPP_FALSE && want[0] == ZXAPP_FALSE &&
              wave_same(app, "camera_ready", ra, 3, want, WAVE_ROWS, true),
          "Pinhole: camera_ready, Eve = KRON = C");
    {
        /* a flag of 2 or 3: Eve's record refuses it, KRON reads it as yes */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2}, three[2] = {1, 3};
        zxapp_arg_t xa[3] = {{"sensor", two, 2}, {"mode", one, 2}, {"buffer", one, 2}};
        CHECK(wave_lp(app, "camera_ready", xa, 3) == ZXAPP_BOTH,
              "Pinhole: sensor 2: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t ya[3] = {{"sensor", one, 2}, {"mode", one, 2}, {"buffer", three, 2}};
        CHECK(wave_lp(app, "camera_ready", ya, 3) == ZXAPP_BOTH,
              "Pinhole: buffer 3: Eve FALSE, KRON TRUE: BOTH");
    }

    /* camera_ready_n (PH2): Forth (Cairndrake) = KRON = C, one row per call, 64 calls */
    bool rn = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t s = r < 8 ? (r >> 2) & 1 : wrand(0, 1), m = r < 8 ? (r >> 1) & 1 : wrand(0, 1),
                k = r < 8 ? r & 1 : wrand(0, 1);
        rn = ph_row(app, s, m, k, pinhole_camera_ready_n(s, m, k)) && rn;
    }
    CHECK(rn, "Pinhole: camera_ready_n, Forth = KRON = C, 64 rows");
    CHECK(ph_one(app, 2, 1, 1) == ZXAPP_BOTH && ph_one(app, 1, -1, 1) == ZXAPP_BOTH &&
              ph_one(app, 1, 1, 9) == ZXAPP_BOTH,
          "Pinhole: camera_ready_n past its domain: Forth refuses, KRON answers: BOTH");

    /* exposure_ok (PH3): Eve = KRON = C, each range at its edges, then swept around them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 30000);
        b[i] = i % 2 ? wrand(0, 40000000) : wrand(1, 1000000);
        c[i] = wrand(-12, 12);
    }
    a[0] = 25, b[0] = 31, c[0] = -9;
    a[1] = 25600, b[1] = 30000000, c[1] = 9;
    a[2] = 24, b[2] = 8000, c[2] = 0;
    a[3] = 25601, b[3] = 8000, c[3] = 0;
    a[4] = 100, b[4] = 30, c[4] = 0;
    a[5] = 100, b[5] = 30000001, c[5] = 0;
    a[6] = 100, b[6] = 8000, c[6] = -10;
    a[7] = 100, b[7] = 8000, c[7] = 10;
    a[8] = -100, b[8] = -8000, c[8] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(pinhole_exposure_ok(a[i], b[i], c[i]));
    zxapp_arg_t ea[3] = {{"iso", a, WAVE_ROWS}, {"shutter", b, WAVE_ROWS}, {"comp", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              wave_same(app, "exposure_ok", ea, 3, want, WAVE_ROWS, true),
          "Pinhole: exposure_ok, Eve = KRON = C");

    /* photo_record_ok (PH7): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(-1200000000000, 1200000000000);
        d[i] = wrand(-120000, 120000);
        e[i] = wrand(-120000, 120000);
        f[i] = wrand(-12, 12);
        g[i] = wrand(-1200000000000000, 1200000000000000);
    }
    a[0] = 1, b[0] = 0, c[0] = 2400000, d[0] = 4032, e[0] = 3024, f[0] = 1, g[0] = 1792627200000;
    a[1] = 999999999, b[1] = 9999, c[1] = 999999999999, d[1] = 99999, e[1] = 99999, f[1] = 9,
    g[1] = 999999999999999;
    a[2] = 1000000000, b[2] = 0, c[2] = 1, d[2] = 1, e[2] = 1, f[2] = 0, g[2] = 1;
    a[3] = 1, b[3] = 10000, c[3] = 1, d[3] = 1, e[3] = 1, f[3] = 0, g[3] = 1;
    a[4] = 1, b[4] = 0, c[4] = 1000000000000, d[4] = 1, e[4] = 1, f[4] = 0, g[4] = 1;
    a[5] = 1, b[5] = 0, c[5] = 1, d[5] = 100000, e[5] = 1, f[5] = 0, g[5] = 1;
    a[6] = 1, b[6] = 0, c[6] = 1, d[6] = 1, e[6] = 1, f[6] = 0, g[6] = 1000000000000000;
    a[7] = -999999999, b[7] = -9999, c[7] = 0, d[7] = -1, e[7] = 0, f[7] = -9, g[7] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(pinhole_record_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Pinhole: the C record's own edges (PH7)");
    zxapp_arg_t ka[7] = {{"photo", a, WAVE_ROWS},  {"album", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},  {"width", d, WAVE_ROWS},
                         {"height", e, WAVE_ROWS}, {"placed", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "photo_record_ok", ka, 7, want, WAVE_ROWS, true),
          "Pinhole: a photo makes a ^ZXPINHOLE record, MYRA = C");

    /* lifeboat.restore_fits, used for "photo fits" (PH4): Sutra = KRON = the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 20000000);   /* the photo's bytes */
        c[i] = wrand(0, 1000000000); /* the floor */
        b[i] = i % 2 ? a[i] + c[i] + wrand(-1000, 1000) : wrand(0, INT64_C(1) << 40); /* avail */
        if (b[i] < 0) b[i] = 0;
    }
    a[0] = 0, b[0] = 0, c[0] = 0;
    a[1] = 3000000, b[1] = 4000000, c[1] = 1000000; /* exactly fits */
    a[2] = 3000001, b[2] = 4000000, c[2] = 1000000; /* one byte short */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(pinhole_photo_fits(a[i], b[i], c[i]));
    zxapp_arg_t fa[3] = {{"need", a, WAVE_ROWS}, {"free", b, WAVE_ROWS}, {"reserve", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "lifeboat.restore_fits", fa, 3, want, WAVE_ROWS, true),
          "Pinhole: photo fits, through lifeboat.restore_fits");
    {
        static const int64_t nn[2] = {1, -1}, fr[2] = {10, 10}, rs[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"need", nn, 2}, {"free", fr, 2}, {"reserve", rs, 2}};
        CHECK(wave_lp(app, "lifeboat.restore_fits", xa, 3) == ZXAPP_BOTH,
              "Pinhole: a negative photo size: Sutra refuses, KRON answers: BOTH");
    }

    /* furrow.range_total, used for bytes per album (PH5): Trebago = KRON = the model's C */
    {
        static int64_t al[WAVE_ROWS], by[WAVE_ROWS], w1[WAVE_ROWS], tot[1];
        bool same = true;
        for (int64_t want_album = 0; want_album < 4; want_album++) {
            for (uint32_t i = 0; i < WAVE_ROWS; i++) {
                al[i] = wrand(0, 3);
                by[i] = wrand(1, 20000000);
                w1[i] = want_album;
            }
            int64_t total = 0;
            CHECK(pinhole_album_total(al, by, WAVE_ROWS, want_album, &total) == 0,
                  "Pinhole: the C album total");
            tot[0] = total;
            zxapp_arg_t aa[4] = {{"pos", al, WAVE_ROWS},
                                 {"lo", w1, WAVE_ROWS},
                                 {"hi", w1, WAVE_ROWS},
                                 {"cell", by, WAVE_ROWS}};
            same = wave_same(app, "furrow.range_total", aa, 4, tot, 1, false) && same;
        }
        CHECK(same, "Pinhole: bytes per album, through furrow.range_total");
        int64_t t = 0;
        static const int64_t al2[3] = {1, 1, 2}, by2[3] = {INT64_MAX, 1, 5};
        CHECK(pinhole_album_total(al2, by2, 3, 1, &t) == PINHOLE_REFUSED &&
                  pinhole_album_total(al2, by2, 3, 2, &t) == 0 && t == 5 &&
                  pinhole_album_total(al2, by2, 3, 2, NULL) == PINHOLE_REFUSED &&
                  pinhole_album_total(al2, by2, PINHOLE_ROWS_MAX + 1, 2, &t) == PINHOLE_REFUSED,
              "Pinhole: the C album total refuses past 64 bits");
        static const int64_t p1[3] = {1, 1, 1}, c1[3] = {INT64_MAX, 1, -1};
        zxapp_arg_t xa[4] = {{"pos", p1, 3}, {"lo", p1, 3}, {"hi", p1, 3}, {"cell", c1, 3}};
        CHECK(wave_lp(app, "furrow.range_total", xa, 4) == ZXAPP_BOTH,
              "Pinhole: a sum past 64 bits one way only: Trebago refuses, KRON answers: BOTH");
    }

    /* gatehouse.use_ok, used for the place tag (PH6): Eve = KRON = the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 1; /* Pinhole needs location */
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
        want[i] = T_(pinhole_place_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t pa[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", pa, 3, want, WAVE_ROWS, true),
          "Pinhole: the place tag, through gatehouse.use_ok");
    {
        static const int64_t one[2] = {1, 1}, gr[2] = {1, 2}, rv[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"asked", one, 2}, {"granted", gr, 2}, {"revoked", rv, 2}};
        CHECK(wave_lp(app, "gatehouse.use_ok", xa, 3) == ZXAPP_BOTH,
              "Pinhole: a grant of 2: Eve FALSE, KRON TRUE: BOTH");
    }
}
