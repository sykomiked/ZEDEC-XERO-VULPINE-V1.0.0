/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Chainman's model against zxvd's real dispatcher (LINK): parcels walked
 * as traverses into the field book in the test's scratch Hoard
 * (files.read, files.write), the area, the closure, a curved boundary, a
 * corner marked where the machine stands (place.here, place.add,
 * place.list), a title filed on the owner's word, and parcels 3 and 4
 * both claiming cell (12, 7): BOTH, and neither record overwritten. */
#include "chainman_model.h"

static chainman_t CM, CM2;
static int64_t CMX[CHAINMAN_VERTS], CMY[CHAINMAN_VERTS];

static bool cm_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static uint32_t cm_claims_on(const chainman_t *c, int64_t cx, int64_t cy)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->nclaims; i++) n += c->claim[i].cx == cx && c->claim[i].cy == cy;
    return n;
}

APPS_TEST(chainman)
{
    int64_t v = 0, dx = 0, dy = 0, perim = 0;
    uint32_t n = 0, added = 0;

    /* nothing before a survey and a load */
    chainman_init(&CM, &LINK, 0);
    CHECK(chainman_load(&CM) == ZXM_E_ARG, "chainman: no survey 0");
    chainman_init(&CM, &LINK, 1);
    int before = sent;
    CHECK(chainman_add_parcel(&CM, 3, UNDERSTORY_CROP, 100000, 60000, 10) == ZXM_E_STATE &&
              chainman_add_leg(&CM, 3, 30000, 0, 30000) == ZXM_E_STATE &&
              chainman_mark(&CM, 3, 0) == ZXM_E_STATE &&
              chainman_title(&CM, 3, 123456, true) == ZXM_E_STATE &&
              chainman_claim_cells(&CM, 3, &added) == ZXM_E_STATE && sent == before,
          "chainman: nothing asked before the field book is read");
    CHECK(chainman_load(&CM) == ZXM_OK && CM.loaded && CM.nparcels == 0 && CM.nlegs == 0,
          "chainman: no field book yet is an empty one: %s", LINK.err);

    /* CM1: parcels, refused before anything is sent */
    before = sent;
    CHECK(chainman_add_parcel(&CM, 0, UNDERSTORY_CROP, 0, 0, 10) == ZXM_E_ARG &&
              chainman_add_parcel(&CM, 3, 9, 0, 0, 10) == ZXM_E_ARG &&
              chainman_add_parcel(&CM, 3, UNDERSTORY_CROP, GNOMON_COORD_MAX + 1, 0, 10) ==
                  ZXM_E_ARG &&
              chainman_add_parcel(&CM, 3, UNDERSTORY_CROP, 0, 0, CHAINMAN_TOL_MAX + 1) ==
                  ZXM_E_ARG &&
              sent == before,
          "chainman: a parcel 0, an unknown kind, a corner past 1 km, a tolerance past 9999");
    CHECK(chainman_add_parcel(&CM, 3, UNDERSTORY_CROP, 100000, 60000, 10) == ZXM_OK &&
              chainman_add_parcel(&CM, 4, UNDERSTORY_PASTURE, 120000, 70000, 10) == ZXM_OK &&
              chainman_add_parcel(&CM, 5, UNDERSTORY_WILD, -50000, -50000, 1) == ZXM_OK &&
              chainman_add_parcel(&CM, 6, UNDERSTORY_GARDEN, 0, 0, 50) == ZXM_OK &&
              CM.nparcels == 4,
          "chainman: four parcels: %s", LINK.err);
    CHECK(chainman_add_parcel(&CM, 3, UNDERSTORY_CROP, 0, 0, 10) == ZXM_E_ARG && CM.nparcels == 4,
          "chainman: a parcel number once");

    /* parcel 3: 30 m a side, its last leg 4 mm east and 10 mm short */
    CHECK(chainman_add_leg(&CM, 3, 30000, 0, 30000) == ZXM_OK &&
              chainman_add_leg(&CM, 3, 0, 30000, 30000) == ZXM_OK &&
              chainman_add_leg(&CM, 3, -30000, 0, 30000) == ZXM_OK &&
              chainman_add_leg(&CM, 3, 4, -29990, 29991) == ZXM_OK,
          "chainman: parcel 3 walked: %s", LINK.err);
    before = sent;
    CHECK(chainman_add_leg(&CM, 3, 4, -29990, 29990) == ZXM_E_ARG &&
              chainman_add_leg(&CM, 9, 10, 0, 10) == ZXM_E_ARG &&
              chainman_add_leg(&CM, 3, 0, 0, 0) == ZXM_E_ARG &&
              chainman_add_leg(&CM, 5, 1100000, 0, 1100000) == ZXM_E_ARG &&
              chainman_add_curve(&CM, 3, 10, 0, 10, 0, 0, 0, 0, 0) == ZXM_E_ARG && CM.nlegs == 4 &&
              sent == before,
          "chainman: a tape shorter than its chord, no parcel, no length, a corner past 1 km");
    /* parcel 4: 40 m by 30 m, closed exactly */
    CHECK(chainman_add_leg(&CM, 4, 40000, 0, 40000) == ZXM_OK &&
              chainman_add_leg(&CM, 4, 0, 30000, 30000) == ZXM_OK &&
              chainman_add_leg(&CM, 4, -40000, 0, 40000) == ZXM_OK &&
              chainman_add_leg(&CM, 4, 0, -30000, 30000) == ZXM_OK,
          "chainman: parcel 4 walked: %s", LINK.err);
    /* parcel 5: 10 m a side at 1 mm per 100 m, a metre short north */
    CHECK(chainman_add_leg(&CM, 5, 10000, 0, 10000) == ZXM_OK &&
              chainman_add_leg(&CM, 5, 0, 10000, 10000) == ZXM_OK &&
              chainman_add_leg(&CM, 5, -10000, 0, 10000) == ZXM_OK &&
              chainman_add_leg(&CM, 5, 0, -9000, 9000) == ZXM_OK,
          "chainman: parcel 5 walked: %s", LINK.err);

    /* CM2, CM3: the outline, the area and the closure */
    CHECK(chainman_outline(&CM, 3, CMX, CMY, CHAINMAN_VERTS, &n) == ZXM_OK && n == 4 &&
              CMX[2] == 130000 && CMY[2] == 90000 && CMX[3] == 100000 && CMY[3] == 90000,
          "chainman: parcel 3's corners, the last leg back to the start");
    CHECK(chainman_area2(&CM, 3, &v) == ZXM_OK && v == INT64_C(1800000000) &&
              chainman_area_m2(&CM, 3, &v) == ZXM_OK && v == 900 &&
              chainman_area_m2(&CM, 4, &v) == ZXM_OK && v == 1200 &&
              chainman_area2(&CM, 9, &v) == ZXM_E_ARG && chainman_area2(&CM, 6, &v) == ZXM_E_STATE,
          "chainman: 900 and 1200 square metres (gnomon.polygon_area2); no area of a start alone");
    CHECK(chainman_closure(&CM, 3, &dx, &dy, &perim) == ZXM_OK && dx == 4 && dy == 10 &&
              perim == 119 && chainman_closes(&CM, 3) == LPRES_STATE_TRUE,
          "chainman: parcel 3 is 10.8 mm out over 119 m, within 11.9 mm: closes");
    CHECK(chainman_closure(&CM, 4, &dx, &dy, &perim) == ZXM_OK && dx == 0 && dy == 0 &&
              perim == 140 && chainman_closes(&CM, 4) == LPRES_STATE_TRUE,
          "chainman: parcel 4 closes exactly");
    CHECK(chainman_closure(&CM, 5, &dx, &dy, &perim) == ZXM_OK && dy == 1000 && perim == 39 &&
              chainman_closes(&CM, 5) == LPRES_STATE_FALSE,
          "chainman: parcel 5 a metre out: does not close");
    CHECK(chainman_closure(&CM, 6, &dx, &dy, &perim) == ZXM_E_STATE &&
              chainman_closes(&CM, 6) == LPRES_STATE_NEITHER &&
              chainman_closes(&CM, 9) == LPRES_STATE_NEITHER,
          "chainman: no legs, no parcel: NEITHER");

    /* a curved boundary flattened (burin.bezier_at) into four chords */
    CHECK(chainman_add_curve(&CM, 6, 20000, 0, 25000, 5000, 5000, 15000, 5000, 4) == ZXM_OK &&
              chainman_add_leg(&CM, 6, 0, 10000, 10000) == ZXM_OK &&
              chainman_add_leg(&CM, 6, -20000, 0, 20000) == ZXM_OK &&
              chainman_add_leg(&CM, 6, 0, -10000, 10000) == ZXM_OK,
          "chainman: parcel 6 with a curved south side: %s", LINK.err);
    CHECK(chainman_outline(&CM, 6, CMX, CMY, CHAINMAN_VERTS, &n) == ZXM_OK && n == 7 &&
              CMX[1] == burin_bezier_at(0, 5000, 15000, 20000, 1, 4) &&
              CMY[2] == burin_bezier_at(0, 5000, 5000, 0, 2, 4) && CMY[2] == 3750 &&
              CMX[4] == 20000 && CMY[4] == 0,
          "chainman: the curve's three inner points, then its end");
    int64_t w = 0;
    CHECK(gnomon_polygon_area2(CMX, CMY, n, &w) && chainman_area2(&CM, 6, &v) == ZXM_OK && v == w &&
              v > 0 && v < INT64_C(400000000),
          "chainman: the curve bows in: less than the 20 by 10 m rectangle (%lld)", (long long) v);
    CHECK(chainman_outline(&CM, 6, CMX, CMY, 3, &n) == ZXM_E_TOOBIG,
          "chainman: an outline past its room is refused");
    CHECK(chainman_path_record_ok(&CM, 3, 1760000000) && chainman_path_record_ok(&CM, 6, 0) &&
              !chainman_path_record_ok(&CM, 3, -1) && !chainman_path_record_ok(&CM, 9, 0),
          "chainman: each outline's path record (burin.drawing_record_ok)");

    /* CM4: a corner marked where the machine stands */
    CHECK(raw_call("place.forget_here", "{}") == ZXM_OK, "chainman: no location");
    before = sent;
    CHECK(chainman_mark(&CM, 3, 0) == ZXM_E_STATE && CM.nmarks == 0 && sent == before + 1,
          "chainman: no location, no mark (%s)", CM.notice);
    CHECK(raw_call("place.set_here", "{\"lat\":51507400,\"lon\":-127800,\"accuracy_m\":3}") ==
              ZXM_OK,
          "chainman: the surveyor's place");
    CHECK(chainman_mark(&CM, 3, 0) == ZXM_OK && CM.nmarks == 1 && CM.mark[0].lat == 51507400 &&
              CM.mark[0].lon == -127800 && CM.nplaces >= 1,
          "chainman: parcel 3's first corner on the map: %s", LINK.err);
    uint32_t places = CM.nplaces;
    before = sent;
    CHECK(chainman_mark(&CM, 3, 0) == ZXM_E_ARG && chainman_mark(&CM, 3, 4) == ZXM_E_ARG &&
              chainman_mark(&CM, 9, 0) == ZXM_E_ARG && sent == before,
          "chainman: a corner once, only the parcel's own corners");
    CHECK(raw_call("place.set_here", "{\"lat\":51507410,\"lon\":-127500,\"accuracy_m\":3}") ==
                  ZXM_OK &&
              chainman_mark(&CM, 3, 1) == ZXM_OK && CM.nplaces == places + 1,
          "chainman: the next corner: %s", LINK.err);

    /* CM5: the title register, on the owner's word */
    CHECK(chainman_title(&CM, 3, 123456, false) == ZXM_E_STATE && CM.parcel[0].title == 0,
          "chainman: no title without the owner's word");
    CHECK(chainman_title(&CM, 3, 0, true) == ZXM_E_ARG &&
              chainman_title(&CM, 3, CHAINMAN_TITLE_MAX + 1, true) == ZXM_E_ARG &&
              chainman_title(&CM, 6, 77, true) == ZXM_OK &&
              chainman_title(&CM, 9, 1, true) == ZXM_E_ARG,
          "chainman: a title 0 or past 9(9) refused");
    CHECK(chainman_title(&CM, 3, 123456, true) == ZXM_OK && CM.parcel[0].title == 123456,
          "chainman: parcel 3's title filed: %s", LINK.err);

    /* CM6, CM7, the BOTH test: parcels 3 and 4 both claim cell (12, 7) */
    CHECK(chainman_claim_cells(&CM, 3, &added) == ZXM_OK && added == 9 &&
              chainman_cell(&CM, 12, 7) == LPRES_STATE_TRUE &&
              chainman_cell(&CM, 10, 6) == LPRES_STATE_TRUE &&
              chainman_cell(&CM, 13, 7) == LPRES_STATE_NEITHER,
          "chainman: parcel 3 claims its nine cells: %s", LINK.err);
    chainman_parcel_t p3 = CM.parcel[0], p4 = CM.parcel[1];
    CHECK(chainman_claim_cells(&CM, 4, &added) == ZXM_OK && added == 12,
          "chainman: parcel 4 claims its twelve: %s", LINK.err);
    lpres_state_t st = chainman_cell(&CM, 12, 7);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3 &&
              chainman_cell(&CM, 12, 8) == LPRES_STATE_BOTH &&
              chainman_cell(&CM, 15, 9) == LPRES_STATE_TRUE &&
              chainman_cell(&CM, 0, 0) == LPRES_STATE_NEITHER,
          "chainman: cell (12, 7) claimed by parcels 3 and 4: BOTH (3), disputed");
    CHECK(cm_claims_on(&CM, 12, 7) == 2 && CM.nclaims == 21 &&
              !memcmp(&p3, &CM.parcel[0], sizeof p3) && !memcmp(&p4, &CM.parcel[1], sizeof p4),
          "chainman: both claims kept, neither parcel's record overwritten");
    CHECK(chainman_claim_cells(&CM, 3, &added) == ZXM_OK && added == 0 && CM.nclaims == 21,
          "chainman: a cell a parcel holds is not claimed again");
    CHECK(lpres_conjoin(chainman_cell(&CM, 12, 7), chainman_closes(&CM, 3)) == LPRES_STATE_BOTH,
          "chainman: a disputed cell joined with a closed parcel stays BOTH");

    /* CM1: another model reads the same field book */
    chainman_init(&CM2, &LINK, 1);
    CHECK(chainman_load(&CM2) == ZXM_OK && CM2.nparcels == 4 && CM2.nlegs == 16 &&
              CM2.nmarks == 2 && CM2.nclaims == 21 && CM2.parcel[0].title == 123456 &&
              chainman_cell(&CM2, 12, 7) == LPRES_STATE_BOTH &&
              chainman_closes(&CM2, 3) == LPRES_STATE_TRUE &&
              chainman_area_m2(&CM2, 4, &v) == ZXM_OK && v == 1200,
          "chainman: the field book read back: %s", LINK.err);
    chainman_init(&CM2, &LINK, 2);
    CHECK(chainman_load(&CM2) == ZXM_E_PROTOCOL && !CM2.loaded,
          "chainman: another survey's field book is refused");

    /* CM1: a line that does not fit is refused on load, what was shown stays */
    CHECK(cm_write(CHAINMAN_BOOK, "S\t1\nP\t3\t7\t0\t0\t0\t10\n") &&
              chainman_load(&CM) == ZXM_E_PROTOCOL && CM.nparcels == 4,
          "chainman: a parcel of kind 7 is refused");
    CHECK(cm_write(CHAINMAN_BOOK, "S\t1\nP\t3\t1\t0\t0\t0\t10\nL\t3\t10\t0\t9\t0\t0\t0\t0\t0\n") &&
              chainman_load(&CM) == ZXM_E_PROTOCOL,
          "chainman: a tape shorter than its chord is refused");
    CHECK(cm_write(CHAINMAN_BOOK, "S\t1\nP\t3\t1\t0\t0\t0\t10\nC\t1\t1\t3\nC\t1\t1\t3\n") &&
              chainman_load(&CM) == ZXM_E_PROTOCOL,
          "chainman: a cell claimed twice by one parcel is refused");
    CHECK(cm_write(CHAINMAN_BOOK, "S\t1\nP\t3\t1\t0\t0\t0\t10") &&
              chainman_load(&CM) == ZXM_E_PROTOCOL,
          "chainman: a line with no end is refused");
    CHECK(cm_write(CHAINMAN_BOOK, "S\t1\nP\t3\t1\t0\t0\t0\t10\nC\t12\t7\t3\nC\t12\t7\t4\n") &&
              chainman_load(&CM) == ZXM_E_PROTOCOL,
          "chainman: a claim by a parcel the book does not hold is refused");

    /* the map tidied: Chainman's places removed, the location forgotten */
    CHECK(raw_call("place.remove", "{\"name\":\"Chainman 1-3-0\"}") == ZXM_OK &&
              raw_call("place.remove", "{\"name\":\"Chainman 1-3-1\"}") == ZXM_OK &&
              chainman_places(&CM) == ZXM_OK && CM.nplaces == places - 1 &&
              raw_call("place.forget_here", "{}") == ZXM_OK,
          "chainman: cleanup");
}
