/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Verderer's languages, witnessed, row by row against verderer_rules.h,
 * the C the model runs; the parts it composes (pinhole, lifeboat, waymark,
 * weatherglass, fieldguide, assay) resolved and called; a protected place
 * shared through the languages equal to the C's; and the BOTH of sighting
 * 14, named 301 by the observer and 302 by the key. Slots:
 * @num.verderer_recapture_estimate, @num.verderer_share_coarse_coord,
 * @logic.verderer_share_coarse_ok, @logic.verderer_reserve_visit_ok,
 * @num.verderer_sighting_grid_cell, @logic.verderer_species_tree_ok; and,
 * used, @logic.pinhole_photo_record_ok, @logic.lifeboat_restore_fits,
 * @logic.waymark_place_ok, @logic.weatherglass_forecast_ok,
 * @num.fieldguide_species_caution, @num.fieldguide_key_next_lead,
 * @num.fieldguide_match_order_key, @num.assay_score_rank. */
#include "verderer_rules.h"

APP_TEST(verderer, 51, 176)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: six parts, every part entry it calls resolves to its part */
    static const char *const used[8][2] = {{"pinhole.photo_record_ok", "pinhole"},
                                           {"lifeboat.restore_fits", "lifeboat"},
                                           {"waymark.place_ok", "waymark"},
                                           {"weatherglass.forecast_ok", "weatherglass"},
                                           {"fieldguide.species_caution", "fieldguide"},
                                           {"fieldguide.key_next_lead", "fieldguide"},
                                           {"fieldguide.match_order_key", "fieldguide"},
                                           {"assay.score_rank", "assay"}};
    CHECK(app->resolved && app->nuse == 6 && app->composed == 9 && app->n == 51u &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Verderer: composed of 6 parts, 9 own lines and %u of theirs, within %u", app->n - 9u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 8u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Verderer: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* VR1: recapture_estimate, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(1, 5000) : wrand(1, VERDERER_COUNT_MAX);
        b[i] = i % 2u ? wrand(1, 5000) : wrand(1, VERDERER_COUNT_MAX);
        c[i] = wrand(1, a[i] < b[i] ? a[i] : b[i]);
        if (i == 0) a[i] = VERDERER_COUNT_MAX, b[i] = VERDERER_COUNT_MAX, c[i] = 1;
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 1;
        if (i == 2) a[i] = 50, b[i] = 40, c[i] = 7;
        want[i] = verderer_recapture_estimate(a[i], b[i], c[i]);
    }
    zxapp_arg_t ea[3] = {
        {"marked", a, WAVE_ROWS}, {"caught", b, WAVE_ROWS}, {"recaptured", c, WAVE_ROWS}};
    CHECK(want[0] == INT64_C(999998000001) && want[1] == 1 && want[2] == 285 &&
              wave_same(app, "recapture_estimate", ea, 3, want, WAVE_ROWS, false),
          "Verderer: recapture_estimate, Trebago = KRON = C");
    {
        static const int64_t m2[2] = {10, 10}, c2[2] = {5, 5}, r2[2] = {2, 6},
                             mbig[2] = {100, VERDERER_COUNT_MAX + 1}, c60[2] = {60, 60},
                             r20[2] = {20, 20}, rneg[2] = {20, -1}, cbig[2] = {60, 1000000};
        zxapp_arg_t e1[3] = {{"marked", m2, 2}, {"caught", c2, 2}, {"recaptured", r2, 2}};
        zxapp_arg_t e2[3] = {{"marked", mbig, 2}, {"caught", c60, 2}, {"recaptured", r20, 2}};
        zxapp_arg_t e3[3] = {{"marked", c60, 2}, {"caught", c60, 2}, {"recaptured", rneg, 2}};
        zxapp_arg_t e4[3] = {{"marked", c60, 2}, {"caught", cbig, 2}, {"recaptured", r20, 2}};
        CHECK(verderer_recapture_estimate(10, 5, 6) == VERDERER_REFUSED &&
                  verderer_recapture_estimate(10, 5, 0) == VERDERER_REFUSED &&
                  verderer_recapture_estimate(VERDERER_COUNT_MAX + 1, 60, 20) == VERDERER_REFUSED &&
                  wave_lp(app, "recapture_estimate", e1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "recapture_estimate", e2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "recapture_estimate", e3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "recapture_estimate", e4, 3) == ZXAPP_BOTH,
              "Verderer: more recaptured than caught, or a count outside: Trebago refuses, KRON "
              "answers: BOTH");
    }

    /* VR2: share_coarse_coord, KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-VERDERER_LON_MAX, VERDERER_LON_MAX);
        b[i] = i % 2u ? VERDERER_CELL * wrand(1, 2000) : wrand(VERDERER_CELL, VERDERER_CELL_MAX);
        c[i] = wrand(0, 1);
        if (i == 0) a[i] = -1, b[i] = VERDERER_CELL, c[i] = 1;
        if (i == 1) a[i] = -VERDERER_LON_MAX, b[i] = VERDERER_CELL, c[i] = 1;
        if (i == 2) a[i] = VERDERER_LON_MAX, b[i] = VERDERER_CELL_MAX, c[i] = 1;
        if (i == 3) a[i] = 59913900, b[i] = VERDERER_CELL, c[i] = 1;
        if (i == 4) a[i] = 59913900, b[i] = VERDERER_CELL, c[i] = 0;
        want[i] = verderer_coarse(a[i], b[i], c[i]);
    }
    zxapp_arg_t ca[3] = {
        {"coord", a, WAVE_ROWS}, {"cell", b, WAVE_ROWS}, {"protected", c, WAVE_ROWS}};
    CHECK(want[0] == -VERDERER_CELL && want[1] == -VERDERER_LON_MAX &&
              want[2] == VERDERER_LON_MAX && want[3] == 59850000 && want[4] == 59913900 &&
              wave_same(app, "share_coarse_coord", ca, 3, want, WAVE_ROWS, false),
          "Verderer: share_coarse_coord, KRON = Trebago = C");
    {
        static const int64_t co[2] = {59913900, 59913900}, fine[2] = {VERDERER_CELL, 89999},
                             wide[2] = {VERDERER_CELL, VERDERER_CELL_MAX + 1}, one[2] = {1, 1},
                             two[2] = {1, 2}, far[2] = {0, VERDERER_LON_MAX + 1};
        static const int64_t cells[2] = {VERDERER_CELL, VERDERER_CELL};
        zxapp_arg_t s1[3] = {{"coord", co, 2}, {"cell", fine, 2}, {"protected", one, 2}};
        zxapp_arg_t s2[3] = {{"coord", co, 2}, {"cell", wide, 2}, {"protected", one, 2}};
        zxapp_arg_t s3[3] = {{"coord", co, 2}, {"cell", cells, 2}, {"protected", two, 2}};
        zxapp_arg_t s4[3] = {{"coord", far, 2}, {"cell", cells, 2}, {"protected", one, 2}};
        CHECK(verderer_coarse(59913900, 89999, 1) == VERDERER_REFUSED &&
                  wave_lp(app, "share_coarse_coord", s1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "share_coarse_coord", s2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "share_coarse_coord", s3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "share_coarse_coord", s4, 3) == ZXAPP_BOTH,
              "Verderer: a cell finer than 10 km, a flag that is no flag or a point off the "
              "globe: Trebago refuses, KRON answers: BOTH");
    }

    /* VR3: share_coarse_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? 0 : wrand(-3, 3);
        b[i] = i % 4u ? 0 : wrand(-3, 3);
        c[i] = i % 2u ? VERDERER_CELL * wrand(1, 2000) : wrand(0, VERDERER_CELL_MAX);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = VERDERER_CELL;
        if (i == 1) a[i] = 0, b[i] = 0, c[i] = VERDERER_CELL - 1;
        want[i] = T_(verderer_share_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"latoff", a, WAVE_ROWS}, {"lonoff", b, WAVE_ROWS}, {"cell", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "share_coarse_ok", sa, 3, want, WAVE_ROWS, true),
          "Verderer: share_coarse_ok, Eve = KRON = C");
    {
        static const int64_t z[2] = {0, 0}, wide[2] = {VERDERER_CELL, VERDERER_CELL_MAX + 1};
        zxapp_arg_t w1[3] = {{"latoff", z, 2}, {"lonoff", z, 2}, {"cell", wide, 2}};
        CHECK(!verderer_share_ok(0, 0, VERDERER_CELL_MAX + 1) &&
                  wave_lp(app, "share_coarse_ok", w1, 3) == ZXAPP_BOTH,
              "Verderer: a cell past half the globe: Eve no, KRON yes: BOTH");
    }

    /* a protected place shared through the languages, as the model shares
     * it (VR2 then VR3): Oslo's lat and lon, the east-west cell of 59.9
     * degrees north twice 0.09 degree */
    {
        static int64_t pt[2] = {59913900, 10752200}, cl[2], pr[2] = {1, 1}, got[2], off[2],
                       zero1[1] = {0}, zero2[1] = {0}, cw[1];
        cl[0] = VERDERER_CELL;
        cl[1] = verderer_lon_cell(pt[0]);
        got[0] = verderer_coarse(pt[0], cl[0], 1);
        got[1] = verderer_coarse(pt[1], cl[1], 1);
        zxapp_arg_t pa[3] = {{"coord", pt, 2}, {"cell", cl, 2}, {"protected", pr, 2}};
        CHECK(cl[1] == 2 * VERDERER_CELL && got[0] == 59850000 && got[1] == 10620000 &&
                  wave_same(app, "share_coarse_coord", pa, 3, got, 2, false),
              "Verderer: Oslo leaves as 59.85, 10.62 in every language");
        off[0] = got[0] - verderer_coarse(got[0], cl[0], 1);
        off[1] = got[1] - verderer_coarse(got[1], cl[1], 1);
        zero1[0] = off[0];
        zero2[0] = off[1];
        cw[0] = cl[1];
        int64_t yes = ZXAPP_TRUE;
        zxapp_arg_t oa[3] = {{"latoff", zero1, 1}, {"lonoff", zero2, 1}, {"cell", cw, 1}};
        CHECK(off[0] == 0 && off[1] == 0 && verderer_share_ok(off[0], off[1], cl[1]) &&
                  wave_same(app, "share_coarse_ok", oa, 3, &yes, 1, true),
              "Verderer: on its cell's corner, it may leave: Eve = KRON");
        CHECK(verderer_lon_cell(0) == VERDERER_CELL &&
                  verderer_lon_cell(2900000) == VERDERER_CELL &&
                  verderer_lon_cell(3000001) == 2 * VERDERER_CELL &&
                  verderer_lon_cell(-59900000) == 2 * VERDERER_CELL &&
                  verderer_lon_cell(-60000000) == 4 * VERDERER_CELL &&
                  verderer_lon_cell(VERDERER_LAT_MAX) == VERDERER_CELL_MAX &&
                  verderer_lon_cell(VERDERER_LAT_MAX + 1) == VERDERER_REFUSED,
              "Verderer: the east-west cell widens toward the poles, a hemisphere at the pole");
    }

    /* VR4: reserve_visit_ok, Eve = C (one language: unwitnessed) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, 1005);
        b[i] = i % 2u ? wrand(-5, 2000) : wrand(0, VERDERER_DIST_MAX + 5);
        c[i] = i % 5u ? 0 : wrand(0, 2);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0;
        if (i == 1) a[i] = VERDERER_PARTY_MAX, b[i] = VERDERER_DIST_MAX, c[i] = 0;
        if (i == 2) a[i] = 4, b[i] = 50, c[i] = 1;
        want[i] = T_(verderer_reserve_visit_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {
        {"partyroom", a, WAVE_ROWS}, {"distroom", b, WAVE_ROWS}, {"drone", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "reserve_visit_ok", ra, 3, want, WAVE_ROWS, true),
          "Verderer: reserve_visit_ok, Eve = C");

    /* VR5: sighting_grid_cell, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(301, 305);
        b[i] = verderer_cell_of(wrand(59800000, 60100000), wrand(10500000, 11000000));
        if (i == 0) a[i] = 301, b[i] = verderer_cell_of(59913900, 10752200);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = verderer_grid_count(a, b, WAVE_ROWS, i);
    zxapp_arg_t ga[2] = {{"species", a, WAVE_ROWS}, {"cell", b, WAVE_ROWS}};
    CHECK(want[0] >= 1 && wave_same(app, "sighting_grid_cell", ga, 2, want, WAVE_ROWS, false),
          "Verderer: sighting_grid_cell, KRON = C");

    /* VR6: species_tree_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12, 12);
        b[i] = i % 4u ? wrand(1, 999999999) : wrand(0, INT64_C(1200000000));
        c[i] = i % 5u ? wrand(1, 999999999) : wrand(0, INT64_C(1200000000));
        d[i] = i % 6u ? wrand(1, 999999999) : wrand(0, INT64_C(1200000000));
        e[i] = wrand(-1, 11);
        if (i == 0) a[i] = 1, b[i] = 5219, c[i] = 2433406, d[i] = 2433407, e[i] = 1;
        if (i == 1) a[i] = 10, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 0;
        want[i] = T_(verderer_tree_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t ta[5] = {{"kingdom", a, WAVE_ROWS},
                         {"family", b, WAVE_ROWS},
                         {"genus", c, WAVE_ROWS},
                         {"species", d, WAVE_ROWS},
                         {"protected", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "species_tree_ok", ta, 5, want, WAVE_ROWS, true),
          "Verderer: species_tree_ok, MYRA = C");

    /* the parts, called through Verderer as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-INT64_C(200000000), INT64_C(200000000));
        b[i] = wrand(-INT64_C(200000000), INT64_C(200000000));
        if (i == 0) a[i] = VERDERER_LAT_MAX, b[i] = -VERDERER_LON_MAX;
        want[i] = T_(verderer_place_ok(a[i], b[i]));
    }
    zxapp_arg_t wa[2] = {{"lat", a, WAVE_ROWS}, {"lon", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && wave_same(app, "waymark.place_ok", wa, 2, want, WAVE_ROWS, true),
          "Verderer: waymark.place_ok, a sighting's point");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(4000000000));
        b[i] = wrand(0, INT64_C(5000000000));
        c[i] = wrand(0, INT64_C(1000000000));
        want[i] = T_(pinhole_photo_fits(a[i], b[i], c[i]));
    }
    zxapp_arg_t la[3] = {{"need", a, WAVE_ROWS}, {"free", b, WAVE_ROWS}, {"reserve", c, WAVE_ROWS}};
    CHECK(wave_same(app, "lifeboat.restore_fits", la, 3, want, WAVE_ROWS, true),
          "Verderer: lifeboat.restore_fits, photo fits");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, INT64_C(1200000000));
        b[i] = 0;
        c[i] = wrand(1, INT64_C(1200000000000));
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
          "Verderer: pinhole.photo_record_ok, a sighting's photo");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(120000000000));
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(0, 12000);
        d[i] = wrand(0, 120000);
        want[i] = T_(verderer_reading_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t fa[4] = {{"stamp", a, WAVE_ROWS},
                         {"temp", b, WAVE_ROWS},
                         {"wind", c, WAVE_ROWS},
                         {"rain", d, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.forecast_ok", fa, 4, want, WAVE_ROWS, true),
          "Verderer: weatherglass.forecast_ok, the reading of the hour");
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(-1, 0);
        int64_t lv = fieldguide_caution(a, WAVE_ROWS);
        zxapp_arg_t ma[1] = {{"mark", a, WAVE_ROWS}};
        CHECK(lv != FIELDGUIDE_REFUSED &&
                  wave_same(app, "fieldguide.species_caution", ma, 1, &lv, 1, false),
              "Verderer: fieldguide.species_caution, show a caution");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) { /* one row a call: Lingua's BASIC reads one */
        int64_t an = wrand(-1, 1), y = wrand(-FIELDGUIDE_LEAD_MAX, FIELDGUIDE_LEAD_MAX),
                no = wrand(-FIELDGUIDE_LEAD_MAX, FIELDGUIDE_LEAD_MAX);
        if (i == 0) an = 1, y = -302, no = 4; /* the key reaches 302 */
        int64_t w = fieldguide_key_next(an, y, no);
        zxapp_arg_t ka[3] = {{"answer", &an, 1}, {"yes", &y, 1}, {"no", &no, 1}};
        CHECK(w != FIELDGUIDE_REFUSED &&
                  wave_same(app, "fieldguide.key_next_lead", ka, 3, &w, 1, false),
              "Verderer: fieldguide.key_next_lead %lld %lld %lld = %lld, the identification key",
              (long long) an, (long long) y, (long long) no, (long long) w);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FIELDGUIDE_SCORE_MAX);
        b[i] = wrand(0, 1);
        want[i] = fieldguide_match_key(a[i], b[i]);
        c[i] = want[i];
    }
    zxapp_arg_t oa[2] = {{"score", a, WAVE_ROWS}, {"keyed", b, WAVE_ROWS}};
    CHECK(wave_same(app, "fieldguide.match_order_key", oa, 2, want, WAVE_ROWS, false),
          "Verderer: fieldguide.match_order_key, the candidates' keys");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = assay_score_rank(c, WAVE_ROWS, i);
    zxapp_arg_t rk[1] = {{"score", c, WAVE_ROWS}};
    CHECK(wave_same(app, "assay.score_rank", rk, 1, want, WAVE_ROWS, false),
          "Verderer: assay.score_rank, the candidates ranked");

    /* VR7, the BOTH test: sighting 14 is species 301 to the observer and
     * 302 to the key: BOTH, kept until an expert confirms */
    {
        lpres_state_t st = verderer_identity(301, 302);
        CHECK(st == LPRES_STATE_BOTH && (int) st == 3 &&
                  lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Verderer: sighting 14, 301 by the observer and 302 by the key: BOTH");
        CHECK(verderer_identity(301, 301) == LPRES_STATE_TRUE &&
                  verderer_identity(301, 0) == LPRES_STATE_TRUE,
              "Verderer: the key agreeing, or not asked: TRUE");
    }
}
