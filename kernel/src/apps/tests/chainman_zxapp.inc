/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Chainman's languages, witnessed, row by row against chainman_rules.h,
 * the C the model runs; the parts it composes (gnomon, furrow, burin,
 * waymark, understory) resolved and called; and the BOTH of a disputed
 * cell. Slots: @num.chainman_closure_slack, @logic.chainman_traverse_closes,
 * @logic.chainman_title_record_ok, @num.chainman_cell_rival_claims,
 * @logic.chainman_corner_node_ok; and, used, @num.gnomon_polygon_area2,
 * @num.furrow_range_total, @num.burin_bezier_at,
 * @logic.burin_drawing_record_ok, @logic.waymark_place_ok,
 * @logic.understory_land_entry_ok. */
#include "chainman_rules.h"

APP_TEST(chainman, 39, 154)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[6][2] = {
        {"gnomon.polygon_area2", "gnomon"}, {"furrow.range_total", "furrow"},
        {"burin.bezier_at", "burin"},       {"burin.drawing_record_ok", "burin"},
        {"waymark.place_ok", "waymark"},    {"understory.land_entry_ok", "understory"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 7 && app->n == 39,
          "Chainman: composed of 5 parts, 7 own lines and 32 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 6u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Chainman: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 6 && !org[0].from, "Chainman: itself and five parts");

    /* CH1: closure_slack, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-CHAINMAN_MISS_MAX, CHAINMAN_MISS_MAX);
        b[i] = wrand(-CHAINMAN_MISS_MAX, CHAINMAN_MISS_MAX);
        c[i] = wrand(1, CHAINMAN_PERIM_MAX);
        d[i] = wrand(0, CHAINMAN_TOL_MAX);
        if (i % 3u == 1u) a[i] = wrand(-200, 200), b[i] = wrand(-200, 200); /* a near close */
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 1, d[i] = 0;                 /* zero */
        if (i == 1)                                                         /* max */
            a[i] = CHAINMAN_MISS_MAX, b[i] = -CHAINMAN_MISS_MAX, c[i] = CHAINMAN_PERIM_MAX,
            d[i] = CHAINMAN_TOL_MAX;
        if (i == 2) a[i] = 30, b[i] = 40, c[i] = 100, d[i] = 1;   /* 50 mm out, 1 mm allowed */
        if (i == 3) a[i] = 60, b[i] = 80, c[i] = 1000, d[i] = 10; /* 100 mm, exactly allowed */
        CHECK(chainman_closure_slack(a[i], b[i], c[i], d[i], &want[i]),
              "Chainman: CH1 row %u answers", i);
    }
    CHECK(want[0] == 0 && want[2] == -24990000 && want[3] == 0 && want[1] > 0,
          "Chainman: CH1 at its edges");
    zxapp_arg_t sa[4] = {
        {"dx", a, WAVE_ROWS}, {"dy", b, WAVE_ROWS}, {"perim", c, WAVE_ROWS}, {"tol", d, WAVE_ROWS}};
    CHECK(wave_same(app, "closure_slack", sa, 4, want, WAVE_ROWS, false),
          "Chainman: closure_slack, Trebago = KRON = C");
    {
        /* max + 1 and negatives: Trebago refuses, KRON answers: BOTH */
        static const int64_t bad[5][4] = {{CHAINMAN_MISS_MAX + 1, 0, 100, 10},
                                          {0, -CHAINMAN_MISS_MAX - 1, 100, 10},
                                          {0, 0, 0, 10},
                                          {0, 0, CHAINMAN_PERIM_MAX + 1, 10},
                                          {0, 0, 100, -1}};
        for (uint32_t i = 0; i < 5u; i++) {
            int64_t x[2] = {0, bad[i][0]}, y[2] = {0, bad[i][1]}, p[2] = {100, bad[i][2]},
                    t[2] = {10, bad[i][3]}, w = 0;
            zxapp_arg_t xa[4] = {{"dx", x, 2}, {"dy", y, 2}, {"perim", p, 2}, {"tol", t, 2}};
            CHECK(!chainman_closure_slack(bad[i][0], bad[i][1], bad[i][2], bad[i][3], &w) &&
                      wave_lp(app, "closure_slack", xa, 4) == ZXAPP_BOTH,
                  "Chainman: closure_slack outside its domain (%u): BOTH", i);
        }
    }

    /* CH2: traverse_closes, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-1000000, 1000000) : wrand(-CHAINMAN_SLACK_MAX, CHAINMAN_SLACK_MAX);
        if (i < 4u) a[i] = (int64_t[]){0, -1, 1, CHAINMAN_SLACK_MAX}[i];
        want[i] = T_(chainman_traverse_closes(a[i]));
    }
    zxapp_arg_t ca[1] = {{"slack", a, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[3] == ZXAPP_TRUE &&
              wave_same(app, "traverse_closes", ca, 1, want, WAVE_ROWS, true),
          "Chainman: traverse_closes, Eve = KRON = C");
    {
        static const int64_t big[2] = {5, CHAINMAN_SLACK_MAX + 1};
        zxapp_arg_t xa[1] = {{"slack", big, 2}};
        CHECK(!chainman_traverse_closes(big[1]) &&
                  wave_lp(app, "traverse_closes", xa, 1) == ZXAPP_BOTH,
              "Chainman: a slack past 18 digits: Eve refuses, KRON answers: BOTH");
    }
    /* CH1 then CH2, as the model asks them: the 100 mm closure closes */
    {
        int64_t s = 0;
        CHECK(chainman_closure_slack(60, 80, 1000, 10, &s) && chainman_traverse_closes(s) &&
                  chainman_closure_slack(61, 80, 1000, 10, &s) && !chainman_traverse_closes(s),
              "Chainman: 100 mm out over 1 km at 10 mm per 100 m closes, 1 mm more does not");
    }

    /* CH3: title_record_ok, Sutra = C inside the pictures */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, CHAINMAN_TITLE_MAX);
        b[i] = wrand(0, CHAINMAN_PARCEL_MAX);
        c[i] = wrand(0, CHAINMAN_AREA_MAX);
        if (i % 4u == 1u) a[i] = 0;
        if (i % 4u == 2u) c[i] = 0;
        if (i == 0) a[i] = CHAINMAN_TITLE_MAX, b[i] = CHAINMAN_PARCEL_MAX, c[i] = CHAINMAN_AREA_MAX;
        if (i == 3) a[i] = 1, b[i] = 1, c[i] = 1;
        want[i] = T_(chainman_title_record_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ta[3] = {{"title", a, WAVE_ROWS}, {"parcel", b, WAVE_ROWS}, {"area", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[3] == ZXAPP_TRUE &&
              wave_same(app, "title_record_ok", ta, 3, want, WAVE_ROWS, true),
          "Chainman: title_record_ok, Sutra = C");
    {
        /* past a picture or negative: refused outright, never an answer */
        static const int64_t bad[4][3] = {{CHAINMAN_TITLE_MAX + 1, 1, 1},
                                          {1, CHAINMAN_PARCEL_MAX + 1, 1},
                                          {1, 1, CHAINMAN_AREA_MAX + 1},
                                          {-1, 1, 1}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t t[2] = {1, bad[i][0]}, p[2] = {1, bad[i][1]}, r[2] = {1, bad[i][2]};
            zxapp_arg_t xa[3] = {{"title", t, 2}, {"parcel", p, 2}, {"area", r, 2}};
            CHECK(!chainman_title_fits(bad[i][0], bad[i][1], bad[i][2]) &&
                      wave_lp(app, "title_record_ok", xa, 3) == ZXAPP_NEITHER,
                  "Chainman: a title line past its pictures (%u): refused", i);
        }
    }

    /* CH4: cell_rival_claims, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(10, 13), b[i] = wrand(6, 8), c[i] = wrand(1, 5);
    a[0] = 12, b[0] = 7, c[0] = 3, a[1] = 12, b[1] = 7, c[1] = 4;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = chainman_cell_rivals(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {{"cx", a, WAVE_ROWS}, {"cy", b, WAVE_ROWS}, {"parcel", c, WAVE_ROWS}};
    CHECK(want[0] >= 1 && want[1] >= 1 &&
              wave_same(app, "cell_rival_claims", ga, 3, want, WAVE_ROWS, false),
          "Chainman: cell_rival_claims, KRON = C");
    {
        static const int64_t x[3] = {1, 1, 2}, y[3] = {1, 1, 1}, p[3] = {5, 5, 6}, none[3] = {0};
        zxapp_arg_t xa[3] = {{"cx", x, 3}, {"cy", y, 3}, {"parcel", p, 3}};
        CHECK(wave_same(app, "cell_rival_claims", xa, 3, none, 3, false),
              "Chainman: one parcel twice on a cell, another alone: no rivals");
    }

    /* CH5: corner_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(-12000, 12000);
        d[i] = wrand(-12000000, 12000000);
        e[i] = wrand(-12000000, 12000000);
        if (i == 0) a[i] = 999999, b[i] = 999999, c[i] = 9999, d[i] = 9999999, e[i] = -9999999;
        if (i == 1) a[i] = 1, b[i] = 3, c[i] = 10000, d[i] = 0, e[i] = 0; /* max + 1 */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0;     /* zero */
        want[i] = T_(chainman_corner_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"survey", a, WAVE_ROWS},
                         {"parcel", b, WAVE_ROWS},
                         {"corner", c, WAVE_ROWS},
                         {"x", d, WAVE_ROWS},
                         {"y", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              wave_same(app, "corner_node_ok", na, 5, want, WAVE_ROWS, true),
          "Chainman: corner_node_ok, MYRA = C");

    /* the parts, called through Chainman as it exports them */
    {
        /* a 40 m by 30 m parcel, counter-clockwise: 1200 square metres, twice in mm^2 */
        static const int64_t x[4] = {100000, 140000, 140000, 100000},
                             y[4] = {60000, 60000, 90000, 90000};
        int64_t w = 0;
        CHECK(gnomon_polygon_area2(x, y, 4, &w) && w == INT64_C(2400000000),
              "Chainman: the C shoelace of a 40 by 30 m parcel");
        zxapp_arg_t pa[2] = {{"x", x, 4}, {"y", y, 4}};
        CHECK(wave_same(app, "gnomon.polygon_area2", pa, 2, &w, 1, false),
              "Chainman: gnomon.polygon_area2, KRON = Trebago = C");
        static const int64_t fx[3] = {0, GNOMON_COORD_MAX + 1, 0}, fy[3] = {0, 0, 1};
        zxapp_arg_t fa[2] = {{"x", fx, 3}, {"y", fy, 3}};
        CHECK(wave_lp(app, "gnomon.polygon_area2", fa, 2) == ZXAPP_BOTH,
              "Chainman: a corner past 1 km: Trebago refuses, KRON answers: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 4); /* each leg's parcel */
        b[i] = 3;           /* parcel 3's column */
        c[i] = wrand(-500000, 500000);
    }
    CHECK(furrow_range_total(a, b, b, c, WAVE_ROWS, &want[0]),
          "Chainman: parcel 3's run east adds up");
    zxapp_arg_t ra[4] = {
        {"pos", a, WAVE_ROWS}, {"lo", b, WAVE_ROWS}, {"hi", b, WAVE_ROWS}, {"cell", c, WAVE_ROWS}};
    CHECK(wave_same(app, "furrow.range_total", ra, 4, want, 1, false),
          "Chainman: furrow.range_total, a field book column, Trebago = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
        b[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
        c[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
        d[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
        e[i] = wrand(2, CHAINMAN_STEPS_MAX);
        want[i] = wrand(0, e[i]);
    }
    {
        static int64_t w[WAVE_ROWS];
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            w[i] = burin_bezier_at(a[i], b[i], c[i], d[i], want[i], e[i]);
        zxapp_arg_t ba[6] = {{"pa", a, WAVE_ROWS},      {"pb", b, WAVE_ROWS},
                             {"pc", c, WAVE_ROWS},      {"pd", d, WAVE_ROWS},
                             {"step", want, WAVE_ROWS}, {"steps", e, WAVE_ROWS}};
        CHECK(wave_same(app, "burin.bezier_at", ba, 6, w, WAVE_ROWS, false),
              "Chainman: burin.bezier_at flattens a curved boundary, wasm = KRON = C");
    }
    {
        static const int64_t path[3] = {3, 999999999, 1000000000}, pts[3] = {40, 511, 1},
                             stroke[3] = {CHAINMAN_STROKE, CHAINMAN_STROKE, 1},
                             mt[3] = {1760000000, 0, 1};
        int64_t w[3];
        for (uint32_t i = 0; i < 3u; i++)
            w[i] = T_(burin_record_ok(path[i], pts[i], stroke[i], mt[i]));
        zxapp_arg_t da[4] = {
            {"path", path, 3}, {"points", pts, 3}, {"stroke", stroke, 3}, {"mtime", mt, 3}};
        CHECK(w[0] == ZXAPP_TRUE && w[2] == ZXAPP_FALSE &&
                  wave_same(app, "burin.drawing_record_ok", da, 4, w, 3, true),
              "Chainman: burin.drawing_record_ok, an outline's path record");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-CHAINMAN_LAT_MAX - 10, CHAINMAN_LAT_MAX + 10);
        b[i] = wrand(-CHAINMAN_LON_MAX - 10, CHAINMAN_LON_MAX + 10);
        if (i < 4u) {
            a[i] = (int64_t[]){CHAINMAN_LAT_MAX, -CHAINMAN_LAT_MAX, CHAINMAN_LAT_MAX + 1, 0}[i];
            b[i] = (int64_t[]){CHAINMAN_LON_MAX, -CHAINMAN_LON_MAX, 0, -CHAINMAN_LON_MAX - 1}[i];
        }
        want[i] = T_(chainman_place_ok(a[i], b[i]));
    }
    zxapp_arg_t wa[2] = {{"lat", a, WAVE_ROWS}, {"lon", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[3] == ZXAPP_FALSE &&
              wave_same(app, "waymark.place_ok", wa, 2, want, WAVE_ROWS, true),
          "Chainman: waymark.place_ok on every corner, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100000, 1100000);
        b[i] = wrand(-12, 12);
        c[i] = i % 2u ? wrand(0, 999999) : wrand(-UNDERSTORY_AREA_MAX - 9, UNDERSTORY_AREA_MAX + 9);
        if (i == 0) a[i] = 3, b[i] = UNDERSTORY_CROP, c[i] = 2400;
        if (i == 1) a[i] = 1000000, b[i] = 1, c[i] = 5;
        want[i] = T_(understory_land_entry_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t la[3] = {{"parcel", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS}, {"area", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "understory.land_entry_ok", la, 3, want, WAVE_ROWS, true),
          "Chainman: understory.land_entry_ok, a parcel as a land entry");

    /* CH6, the BOTH test: parcels 3 and 4 both claim cell (12, 7): BOTH,
     * disputed; both claims are kept and neither parcel's row changes */
    {
        static const int64_t x[3] = {12, 12, 11}, y[3] = {7, 7, 7}, p[3] = {3, 4, 3};
        int64_t rivals = chainman_cell_rivals(x, y, p, 3, 0) + chainman_cell_rivals(x, y, p, 3, 1);
        lpres_state_t st = chainman_cell_state(2, rivals);
        CHECK(st == LPRES_STATE_BOTH && (int) st == 3 && p[0] == 3 && p[1] == 4,
              "Chainman: cell (12, 7) claimed by parcels 3 and 4: BOTH (3), both kept");
        CHECK(chainman_cell_state(1, chainman_cell_rivals(x, y, p, 3, 2)) == LPRES_STATE_TRUE &&
                  chainman_cell_state(0, 0) == LPRES_STATE_NEITHER,
              "Chainman: cell (11, 7) held by parcel 3 alone: TRUE; an unclaimed cell NEITHER");
        CHECK(lpres_conjoin(st, LPRES_STATE_TRUE) == LPRES_STATE_BOTH,
              "Chainman: a disputed cell joined with a parcel that closes: still BOTH");
        int64_t w[3] = {1, 1, 0};
        zxapp_arg_t xa[3] = {{"cx", x, 3}, {"cy", y, 3}, {"parcel", p, 3}};
        CHECK(wave_same(app, "cell_rival_claims", xa, 3, w, 3, false),
              "Chainman: KRON finds the rival claim on (12, 7) for each parcel");
        CHECK(chainman_cell_of(125000) == 12 && chainman_cell_of(75000) == 7 &&
                  chainman_cell_of(-1) == -1 && chainman_cell_of(0) == 0,
              "Chainman: cells floor toward minus infinity");
    }
}
