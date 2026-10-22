/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tintype's languages, witnessed, row by row against tintype_rules.h, the
 * C the model runs; the parts it composes (glint, gesso, pinhole, lifeboat,
 * glimpse) resolved and called through it; and the BOTH test: a photo
 * recorded 4000 by 3000 that decodes to 3990 by 3000 is possibly altered.
 * Slots: @num.tintype_exposure_ev_thirds, @logic.tintype_photo_decode_ok,
 * @logic.tintype_rights_record_ok, @logic.tintype_album_storage_ok,
 * @num.tintype_develop_curve_px, @logic.tintype_album_node_ok; and,
 * used, @logic.glint_item_ok, @num.gesso_pixel_bright,
 * @num.gesso_pixel_contrast, @num.gesso_pixel_range_count,
 * @logic.pinhole_camera_ready, @logic.pinhole_exposure_ok,
 * @logic.pinhole_photo_record_ok, @logic.lifeboat_restore_fits,
 * @num.glimpse_capture_origin, @num.glimpse_recording_bytes,
 * @num.glimpse_first_free_n. */
#include "tintype_rules.h"

APP_TEST(tintype, 43, 167)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[11][2] = {{"glint.item_ok", "glint"},
                                            {"gesso.pixel_bright", "gesso"},
                                            {"gesso.pixel_contrast", "gesso"},
                                            {"gesso.pixel_range_count", "gesso"},
                                            {"pinhole.camera_ready", "pinhole"},
                                            {"pinhole.exposure_ok", "pinhole"},
                                            {"pinhole.photo_record_ok", "pinhole"},
                                            {"lifeboat.restore_fits", "lifeboat"},
                                            {"glimpse.capture_origin", "glimpse"},
                                            {"glimpse.recording_bytes", "glimpse"},
                                            {"glimpse.first_free_n", "glimpse"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 8 && app->n == 43u &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Tintype: composed of 5 parts, 8 own lines and %u of theirs, within %u", app->n - 8u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 11u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Tintype: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* TR1: exposure_ev_thirds, Trebago = KRON = C, swept over the tables */
    CHECK(tintype_ev_thirds(160, 8000, 100) == 45 && tintype_ev_thirds(10, 31, 25) == 51 &&
              tintype_ev_thirds(320, 30000000, 25600) == -9 &&
              tintype_ev_thirds(20, 1000000, 100) == 6 && tintype_ev_thirds(160, 7999, 99) == 47 &&
              tintype_ev_thirds(9, 8000, 100) == TINTYPE_EV_REFUSED &&
              tintype_ev_thirds(321, 8000, 100) == TINTYPE_EV_REFUSED &&
              tintype_ev_thirds(160, 30, 100) == TINTYPE_EV_REFUSED &&
              tintype_ev_thirds(160, 30000001, 100) == TINTYPE_EV_REFUSED &&
              tintype_ev_thirds(160, 8000, 24) == TINTYPE_EV_REFUSED &&
              tintype_ev_thirds(160, 8000, 25601) == TINTYPE_EV_REFUSED,
          "Tintype: the exposure value's own edges: sunny 16 is EV 15");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(TINTYPE_AP_MIN, TINTYPE_AP_MAX);
        b[i] = i % 2u ? wrand(TINTYPE_SH_MIN, 1000000) : wrand(TINTYPE_SH_MIN, TINTYPE_SH_MAX);
        c[i] = wrand(TINTYPE_ISO_MIN, TINTYPE_ISO_MAX);
        if (i == 0) a[i] = 160, b[i] = 8000, c[i] = 100;
        if (i == 1) a[i] = TINTYPE_AP_MIN, b[i] = TINTYPE_SH_MIN, c[i] = TINTYPE_ISO_MIN;
        if (i == 2) a[i] = TINTYPE_AP_MAX, b[i] = TINTYPE_SH_MAX, c[i] = TINTYPE_ISO_MAX;
        if (i == 3) a[i] = 279, b[i] = 999999, c[i] = 6399; /* each just below a table entry */
        want[i] = tintype_ev_thirds(a[i], b[i], c[i]);
    }
    zxapp_arg_t ea[3] = {
        {"aperture", a, WAVE_ROWS}, {"shutter", b, WAVE_ROWS}, {"iso", c, WAVE_ROWS}};
    CHECK(want[0] == 45 && want[1] == 51 && want[2] == -9 && want[3] == 12 &&
              wave_same(app, "exposure_ev_thirds", ea, 3, want, WAVE_ROWS, false),
          "Tintype: exposure_ev_thirds, Trebago = KRON = C");
    {
        static const int64_t ap2[2] = {160, 9}, ap1[2] = {160, 160}, sh2[2] = {8000, 30000001},
                             sh1[2] = {8000, 8000}, is2[2] = {100, 24}, is1[2] = {100, 100};
        zxapp_arg_t x1[3] = {{"aperture", ap2, 2}, {"shutter", sh1, 2}, {"iso", is1, 2}};
        zxapp_arg_t x2[3] = {{"aperture", ap1, 2}, {"shutter", sh2, 2}, {"iso", is1, 2}};
        zxapp_arg_t x3[3] = {{"aperture", ap1, 2}, {"shutter", sh1, 2}, {"iso", is2, 2}};
        CHECK(wave_lp(app, "exposure_ev_thirds", x1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "exposure_ev_thirds", x2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "exposure_ev_thirds", x3, 3) == ZXAPP_BOTH,
              "Tintype: an aperture, shutter or ISO off its table: Trebago refuses, KRON counts: "
              "BOTH");
    }

    /* TR2: photo_decode_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? 0 : wrand(-10, 10);
        b[i] = i % 5u ? 0 : wrand(-10, 10);
        c[i] = i % 3u ? wrand(-1, 1) : wrand(-3, 3);
        d[i] = i % 6u ? 1 : wrand(0, 1);
        want[i] = T_(tintype_decode_match(a[i], b[i], c[i], d[i]));
    }
    a[0] = 0, b[0] = 0, c[0] = 1, d[0] = 1, want[0] = ZXAPP_TRUE;
    a[1] = 10, b[1] = 0, c[1] = 0, d[1] = 1,
    want[1] = ZXAPP_FALSE; /* 4000 recorded, 3990 decoded */
    zxapp_arg_t ma[4] = {{"wgap", a, WAVE_ROWS},
                         {"hgap", b, WAVE_ROWS},
                         {"tgap", c, WAVE_ROWS},
                         {"cidsame", d, WAVE_ROWS}};
    CHECK(T_(tintype_decode_match(10, 0, 0, 1)) == ZXAPP_FALSE &&
              wave_same(app, "photo_decode_ok", ma, 4, want, WAVE_ROWS, true),
          "Tintype: photo_decode_ok, Eve = KRON = C");
    {
        static const int64_t z[2] = {0, 0}, cid2[2] = {1, 2};
        zxapp_arg_t x1[4] = {{"wgap", z, 2}, {"hgap", z, 2}, {"tgap", z, 2}, {"cidsame", cid2, 2}};
        CHECK(!tintype_decode_match(0, 0, 0, 2) &&
                  wave_lp(app, "photo_decode_ok", x1, 4) == ZXAPP_BOTH,
              "Tintype: a content-id flag of 2: Eve no, KRON yes: BOTH");
    }

    /* TR2, the BOTH test: recorded 4000 x 3000, decoded 3990 x 3000 */
    {
        lpres_state_t st = tintype_integrity(4000 - 3990, 3000 - 3000, 0, true, 1);
        CHECK(st == LPRES_STATE_BOTH && (int) st == 3 &&
                  lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Tintype: recorded 4000 x 3000, decoded 3990 x 3000: BOTH, possibly altered");
        CHECK(tintype_integrity(0, 0, 0, true, 1) == LPRES_STATE_TRUE &&
                  tintype_integrity(0, 0, 3600, false, 1) == LPRES_STATE_TRUE &&
                  tintype_integrity(0, 0, 2, true, 1) == LPRES_STATE_BOTH &&
                  tintype_integrity(0, 0, 0, true, 0) == LPRES_STATE_BOTH,
              "Tintype: as recorded TRUE, a time the file lacks is no contradiction, a moved "
              "clock or a new content id is BOTH");
    }

    /* TR3: rights_record_ok, Sutra = C (one language: unwitnessed) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(1, TINTYPE_ID_MAX) : wrand(0, 2);
        b[i] = i % 4u ? wrand(1, TINTYPE_ID_MAX) : wrand(0, 2);
        c[i] = wrand(0, 99);
        d[i] = wrand(0, 9);
        if (i % 2u) c[i] = wrand(0, TINTYPE_LICENCE_MAX), d[i] = wrand(0, 1);
        want[i] = T_(tintype_rights_ok(a[i], b[i], c[i], d[i]));
    }
    a[0] = TINTYPE_ID_MAX, b[0] = TINTYPE_ID_MAX, c[0] = TINTYPE_LICENCE_MAX, d[0] = 1;
    want[0] = ZXAPP_TRUE;
    a[1] = 1, b[1] = 1, c[1] = TINTYPE_LICENCE_MAX + 1, d[1] = 0, want[1] = ZXAPP_FALSE;
    zxapp_arg_t ra[4] = {{"photo", a, WAVE_ROWS},
                         {"owner", b, WAVE_ROWS},
                         {"licence", c, WAVE_ROWS},
                         {"release", d, WAVE_ROWS}};
    CHECK(wave_same(app, "rights_record_ok", ra, 4, want, WAVE_ROWS, true),
          "Tintype: rights_record_ok, Sutra = C");

    /* TR4: album_storage_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 4u ? wrand(1, TINTYPE_BYTES_MAX) : wrand(0, 2);
        c[i] = wrand(0, b[i] > 0 ? b[i] : 1);
        a[i] = i % 3u ? tintype_album_room(b[i] > 0 ? b[i] : 1, c[i], wrand(0, 4096))
                      : wrand(-TINTYPE_BYTES_MAX, TINTYPE_BYTES_MAX + 5);
        want[i] = T_(tintype_storage_ok(a[i], b[i]));
    }
    a[0] = 0, b[0] = TINTYPE_BYTES_MAX, want[0] = ZXAPP_TRUE;
    a[1] = -1, b[1] = 4096, want[1] = ZXAPP_FALSE;
    zxapp_arg_t sa[2] = {{"room", a, WAVE_ROWS}, {"declared", b, WAVE_ROWS}};
    CHECK(tintype_album_room(1000, 400, 600) == 0 && tintype_album_room(1000, 400, 601) == -1 &&
              tintype_album_room(0, 0, 0) == TINTYPE_REFUSED &&
              wave_same(app, "album_storage_ok", sa, 2, want, WAVE_ROWS, true),
          "Tintype: album_storage_ok, Eve = C");

    /* TR5: develop_curve_px, KRON = C, over 64 values and one curve a call */
    for (uint32_t r = 0; r < 8u; r++) {
        int64_t curve[TINTYPE_KNOTS];
        for (uint32_t k = 0; k < TINTYPE_KNOTS; k++) curve[k] = wrand(0, GESSO_PX_MAX);
        if (r == 0) curve[0] = 0, curve[1] = 64, curve[2] = 128, curve[3] = 192, curve[4] = 255;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = i < 6u ? (int64_t) (i * 51u) : wrand(0, GESSO_PX_MAX);
            want[i] = tintype_curve_px(a[i], curve);
        }
        zxapp_arg_t ca[2] = {{"px", a, WAVE_ROWS}, {"curve", curve, TINTYPE_KNOTS}};
        CHECK((r != 0 || (want[0] == 0 && want[5] == 255 && want[2] == 102)) &&
                  wave_same(app, "develop_curve_px", ca, 2, want, WAVE_ROWS, false),
              "Tintype: develop_curve_px, curve %u, KRON = C", r);
    }
    {
        static const int64_t dn[TINTYPE_KNOTS] = {255, 200, 128, 60, 0},
                             bad[TINTYPE_KNOTS] = {0, 1, 2, 3, 256};
        CHECK(tintype_curve_px(100, dn) == 159 && tintype_curve_px(256, dn) == TINTYPE_REFUSED &&
                  tintype_curve_px(-1, dn) == TINTYPE_REFUSED &&
                  tintype_curve_px(3, bad) == TINTYPE_REFUSED,
              "Tintype: a falling curve floors toward minus infinity; a value or knot past 255 is "
              "refused");
    }

    /* TR6: album_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(0, 9999) : wrand(-10500, 10500);
        b[i] = i % 5u ? wrand(1, 9999) : wrand(-10500, 10500);
        c[i] = i % 6u ? wrand(1, TINTYPE_BYTES_MAX) : wrand(0, INT64_C(1200000000000000));
        d[i] = i % 7u ? wrand(0, TINTYPE_BYTES_MAX) : wrand(0, INT64_C(1200000000000000));
        e[i] = i % 3u ? wrand(0, 99999) : wrand(-120000, 120000);
        want[i] = T_(tintype_album_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    a[0] = 0, b[0] = 1, c[0] = 1, d[0] = 0, e[0] = 0, want[0] = ZXAPP_TRUE;
    a[1] = 10000, b[1] = 1, c[1] = 1, d[1] = 0, e[1] = 0, want[1] = ZXAPP_FALSE;
    zxapp_arg_t na[5] = {{"parent", a, WAVE_ROWS},
                         {"album", b, WAVE_ROWS},
                         {"declared", c, WAVE_ROWS},
                         {"used", d, WAVE_ROWS},
                         {"photos", e, WAVE_ROWS}};
    CHECK(wave_same(app, "album_node_ok", na, 5, want, WAVE_ROWS, true),
          "Tintype: album_node_ok, MYRA = C");

    /* the parts, called through Tintype as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, TINTYPE_ALBUM_MAX);
        b[i] = i % 3u ? wrand(1, 99999) : wrand(0, INT64_C(1200000000));
        c[i] = i % 4u ? wrand(TINTYPE_KIND_PHOTO, TINTYPE_KIND_SCREEN) : wrand(-1200, 1200);
        d[i] = wrand(0, 2097152);
        want[i] = T_(tintype_item_fits(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ia[4] = {{"folder", a, WAVE_ROWS},
                         {"seq", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"size", d, WAVE_ROWS}};
    CHECK(wave_same(app, "glint.item_ok", ia, 4, want, WAVE_ROWS, true),
          "Tintype: glint.item_ok, each catalogued photo as a media item");

    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_PX_MAX);
        b[i] = wrand(-GESSO_PX_MAX, GESSO_PX_MAX);
        c[i] = wrand(0, GESSO_GAIN_MAX);
        want[i] = gesso_bright_px(a[i], b[i]);
        e[i] = gesso_contrast_px(a[i], c[i]);
    }
    zxapp_arg_t ba[2] = {{"px", a, WAVE_ROWS}, {"delta", b, WAVE_ROWS}};
    zxapp_arg_t ga[2] = {{"px", a, WAVE_ROWS}, {"gain", c, WAVE_ROWS}};
    CHECK(wave_same(app, "gesso.pixel_bright", ba, 2, want, WAVE_ROWS, false) &&
              wave_same(app, "gesso.pixel_contrast", ga, 2, e, WAVE_ROWS, false),
          "Tintype: gesso.pixel_bright and gesso.pixel_contrast, the develop step, KRON = C++ = C");
    {
        static const int64_t p2[2] = {10, 256}, d2[2] = {0, 0}, n2[2] = {10, -1},
                             g2[2] = {10000, 10000};
        zxapp_arg_t x1[2] = {{"px", p2, 2}, {"delta", d2, 2}};
        zxapp_arg_t x2[2] = {{"px", n2, 2}, {"gain", g2, 2}};
        CHECK(wave_lp(app, "gesso.pixel_bright", x1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "gesso.pixel_contrast", x2, 2) == ZXAPP_BOTH,
              "Tintype: a channel value past 0..255: C++ refuses, KRON answers: BOTH");
    }
    {
        /* the histogram: each bin of 32 levels over one developed row */
        int64_t row[64], lo[1], hi[1], count, total = 0;
        static const int64_t curve[TINTYPE_KNOTS] = {0, 80, 140, 200, 255};
        for (uint32_t i = 0; i < 64u; i++)
            row[i] = gesso_bright_px(tintype_curve_px(wrand(0, GESSO_PX_MAX), curve), 10);
        bool same = true;
        for (uint32_t bin = 0; bin < 8u; bin++) {
            lo[0] = (int64_t) bin * 32, hi[0] = lo[0] + 31;
            count = gesso_range_count(row, 64, lo[0], hi[0]);
            total += count;
            zxapp_arg_t ha[3] = {{"px", row, 64}, {"lo", lo, 1}, {"hi", hi, 1}};
            same = wave_same(app, "gesso.pixel_range_count", ha, 3, &count, 1, false) && same;
        }
        static const int64_t px[2] = {1, 2}, l2[2] = {1, 1}, h2[2] = {2, 2};
        zxapp_arg_t xa[3] = {{"px", px, 2}, {"lo", l2, 2}, {"hi", h2, 2}};
        CHECK(same && total == 64 && wave_lp(app, "gesso.pixel_range_count", xa, 3) == ZXAPP_BOTH,
              "Tintype: gesso.pixel_range_count, the histogram's 8 bins hold all 64; two lo: BOTH");
    }

    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = i % 2u ? 1 : wrand(0, 1);
        want[i] = T_(pinhole_camera_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t ka[3] = {
        {"sensor", a, WAVE_ROWS}, {"mode", b, WAVE_ROWS}, {"buffer", c, WAVE_ROWS}};
    {
        static const int64_t s2[2] = {1, 2}, o2[2] = {1, 1};
        zxapp_arg_t x1[3] = {{"sensor", s2, 2}, {"mode", o2, 2}, {"buffer", o2, 2}};
        CHECK(wave_same(app, "pinhole.camera_ready", ka, 3, want, WAVE_ROWS, true) &&
                  wave_lp(app, "pinhole.camera_ready", x1, 3) == ZXAPP_BOTH,
              "Tintype: pinhole.camera_ready, Eve = KRON = C; a flag of 2: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 30000);
        b[i] = i % 2u ? wrand(0, 100000) : wrand(0, 40000000);
        c[i] = wrand(-12, 12);
        want[i] = T_(pinhole_exposure_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t xa[3] = {{"iso", a, WAVE_ROWS}, {"shutter", b, WAVE_ROWS}, {"comp", c, WAVE_ROWS}};
    CHECK(wave_same(app, "pinhole.exposure_ok", xa, 3, want, WAVE_ROWS, true),
          "Tintype: pinhole.exposure_ok, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, INT64_C(1200000000));
        b[i] = wrand(0, 12000);
        c[i] = wrand(1, 2097152);
        d[i] = wrand(1, 120000);
        e[i] = wrand(1, 120000);
        f[i] = 0;
        g[i] = wrand(0, INT64_C(1900000000000000));
        want[i] = T_(pinhole_record_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t pa[7] = {{"photo", a, WAVE_ROWS},  {"album", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},  {"width", d, WAVE_ROWS},
                         {"height", e, WAVE_ROWS}, {"placed", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "pinhole.photo_record_ok", pa, 7, want, WAVE_ROWS, true),
          "Tintype: pinhole.photo_record_ok, a kept photo's record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 2097152);
        b[i] = wrand(0, INT64_C(200000000));
        c[i] = wrand(0, INT64_C(100000000));
        want[i] = T_(lifeboat_restore_fits(a[i], b[i], c[i]));
    }
    zxapp_arg_t la[3] = {{"need", a, WAVE_ROWS}, {"free", b, WAVE_ROWS}, {"reserve", c, WAVE_ROWS}};
    {
        static const int64_t n2[2] = {1, -1}, f2[2] = {10, 10}, r2[2] = {1, 1};
        zxapp_arg_t x1[3] = {{"need", n2, 2}, {"free", f2, 2}, {"reserve", r2, 2}};
        CHECK(wave_same(app, "lifeboat.restore_fits", la, 3, want, WAVE_ROWS, true) &&
                  wave_lp(app, "lifeboat.restore_fits", x1, 3) == ZXAPP_BOTH,
              "Tintype: lifeboat.restore_fits, photo fits; a negative need: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, GLIMPSE_SIDE_MAX);
        a[i] = wrand(-b[i], 2 * b[i]);
        want[i] = glimpse_capture_origin(a[i], b[i]);
    }
    zxapp_arg_t oa[2] = {{"pos", a, WAVE_ROWS}, {"span", b, WAVE_ROWS}};
    {
        static const int64_t p2[2] = {10, 10}, s2[2] = {1920, 0};
        zxapp_arg_t x1[2] = {{"pos", p2, 2}, {"span", s2, 2}};
        CHECK(wave_same(app, "glimpse.capture_origin", oa, 2, want, WAVE_ROWS, false) &&
                  wave_lp(app, "glimpse.capture_origin", x1, 2) == ZXAPP_BOTH,
              "Tintype: glimpse.capture_origin, the grab's corner; a screen of no side: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 3840);
        b[i] = wrand(1, 2160);
        c[i] = wrand(1, 4);
        d[i] = wrand(1, 600);
        e[i] = wrand(1, 60);
        int64_t raw = a[i] * b[i] * c[i] * d[i] * e[i];
        f[i] = i % 2u ? raw : wrand(0, 2 * raw);
        want[i] = glimpse_recording_bytes(a[i], b[i], c[i], d[i], e[i], f[i]);
    }
    zxapp_arg_t rb[6] = {{"w", a, WAVE_ROWS},    {"h", b, WAVE_ROWS},   {"bpp", c, WAVE_ROWS},
                         {"secs", d, WAVE_ROWS}, {"fps", e, WAVE_ROWS}, {"room", f, WAVE_ROWS}};
    CHECK(wave_same(app, "glimpse.recording_bytes", rb, 6, want, WAVE_ROWS, false),
          "Tintype: glimpse.recording_bytes, a screen recording against the room");
    {
        static int64_t nums[16];
        bool same = true;
        for (uint32_t r = 0; r < 8u; r++) {
            uint32_t n = 1u + r * 2u;
            for (uint32_t i = 0; i < n; i++) nums[i] = wrand(0, (int64_t) n + 2);
            int64_t one = glimpse_first_free(nums, n);
            zxapp_arg_t ua[1] = {{"used", nums, n}};
            same =
                one >= 1 && wave_same(app, "glimpse.first_free_n", ua, 1, &one, 1, false) && same;
        }
        CHECK(same, "Tintype: glimpse.first_free_n, the next photo number in an album");
    }
}
