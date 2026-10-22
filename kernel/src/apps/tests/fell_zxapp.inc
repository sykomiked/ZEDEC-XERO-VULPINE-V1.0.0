/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fell's languages, witnessed, row by row against fell_rules.h, the C the
 * model runs; the parts it composes (waymark, aerie, watchfire, commons)
 * resolved and called against their own rules; and the BOTH test: a
 * sighting in sector 4 at 14:10 and another in sector 9 at 14:12 make the
 * search's lead BOTH (state 3), and neither sector is dropped.
 * Slots: @num.fell_sector_priority, @logic.fell_crew_shift_safe,
 * @num.fell_search_grid_sector, @logic.fell_sector_unsearched,
 * @num.fell_sightings_between, @num.fell_callout_claim,
 * @logic.fell_casualty_record_ok; and, used, @logic.waymark_place_ok,
 * @num.aerie_frame_tag, @logic.aerie_band_fits,
 * @logic.watchfire_place_share_ok, @logic.watchfire_medical_id_ok,
 * @logic.commons_room_post_ok. */
#include "aerie_rules.h"
#include "commons_rules.h"
#include "fell_rules.h"
#include "watchfire_rules.h"

/* one frame's parts: Aerie's Forth reads one frame from its stack per call */
static bool fe_tag_row(zxapp_t *app, int64_t sync, int64_t shell, int64_t trunk)
{
    int64_t sa[1] = {sync}, ea[1] = {shell}, ta[1] = {trunk};
    int64_t w[1] = {aerie_frame_tag(sync, shell, trunk)};
    zxapp_arg_t a[3] = {{"sync", sa, 1}, {"shell", ea, 1}, {"trunk", ta, 1}};
    return wave_same(app, "aerie.frame_tag", a, 3, w, 1, false);
}

APP_TEST(fell, 35, 253)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[6][2] = {{"waymark.place_ok", "waymark"},
                                           {"aerie.frame_tag", "aerie"},
                                           {"aerie.band_fits", "aerie"},
                                           {"watchfire.place_share_ok", "watchfire"},
                                           {"watchfire.medical_id_ok", "watchfire"},
                                           {"commons.room_post_ok", "commons"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 9 && app->n == 35,
          "Fell: composed of 4 parts, 9 own lines and 26 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 6u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Fell: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 5 && !org[0].from, "Fell: itself and four parts");

    /* FR1: sector_priority, Trebago = KRON = C, 64 sectors of one search */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = i % 5u ? wrand(0, FELL_X100) : wrand(0, 3);
            b[i] = i % 3u ? wrand(0, FELL_X100) : (i % 2u ? FELL_X100 : 0);
        }
        a[0] = 5000, b[0] = 0; /* something is left to find */
        int64_t sum = 0;
        CHECK(fell_sector_priority(a, b, WAVE_ROWS, want), "Fell: FR1 answers");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) sum += want[i];
        zxapp_arg_t pa[2] = {{"poa", a, WAVE_ROWS}, {"cov", b, WAVE_ROWS}};
        CHECK(sum == FELL_X100 && wave_same(app, "sector_priority", pa, 2, want, WAVE_ROWS, false),
              "Fell: sector_priority, Trebago = KRON = C, summing to %lld", (long long) sum);
        /* three sectors: 50%, 30% half searched, 20% */
        int64_t p3[3] = {5000, 3000, 2000}, c3[3] = {0, 5000, 0}, w3[3] = {0, 0, 0};
        zxapp_arg_t qa[2] = {{"poa", p3, 3}, {"cov", c3, 3}};
        CHECK(fell_sector_priority(p3, c3, 3, w3) && w3[0] == 5882 && w3[1] == 1765 &&
                  w3[2] == 2353 && wave_same(app, "sector_priority", qa, 2, w3, 3, false),
              "Fell: sector_priority 5882, 1765, 2353");
        /* a poa or coverage past 10000, or one below 0: Trebago refuses, KRON answers */
        static const int64_t odd[4][2] = {{5000, 10001}, {10001, 0}, {-1, 0}, {5000, -1}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t p2[2] = {5000, odd[k][0]}, c2[2] = {0, odd[k][1]}, w2[2];
            zxapp_arg_t xa[2] = {{"poa", p2, 2}, {"cov", c2, 2}};
            CHECK(!fell_sector_priority(p2, c2, 2, w2) &&
                      wave_lp(app, "sector_priority", xa, 2) == ZXAPP_BOTH,
                  "Fell: sector_priority edge %u: Trebago refuses, KRON answers: BOTH", k);
        }
        /* nothing left that is likely: refused by both, C alike */
        int64_t p2[2] = {5000, 0}, c2[2] = {FELL_X100, 0}, w2[2];
        zxapp_arg_t za[2] = {{"poa", p2, 2}, {"cov", c2, 2}};
        zxapp_num_t o[WAVE_ROWS];
        uint32_t n = 0;
        int32_t lp = -1;
        int rc = zxapp_call_witnessed(app, &H, "sector_priority", za, 2, o, WAVE_ROWS, &n, &lp);
        CHECK(!fell_sector_priority(p2, c2, 2, w2) && rc != ZXAPP_OK,
              "Fell: sector_priority refused by both when every sector is searched through");
    }

    /* FR2: crew_shift_safe, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 800);
        b[i] = wrand(0, 80);
        if (i == 0) a[i] = 0, b[i] = 0;
        if (i == 1) a[i] = FELL_SHIFT_MAX, b[i] = FELL_CHECKIN_MAX;
        if (i == 2) a[i] = FELL_SHIFT_MAX + 1, b[i] = 0;
        if (i == 3) a[i] = 300, b[i] = FELL_CHECKIN_MAX + 1;
        want[i] = T_(fell_crew_safe(a[i], b[i]));
    }
    zxapp_arg_t ca[2] = {{"worked", a, WAVE_ROWS}, {"since", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "crew_shift_safe", ca, 2, want, WAVE_ROWS, true),
          "Fell: crew_shift_safe, Eve = KRON = C");
    {
        /* a minute count below 0 (a clock gone back): Eve refuses, KRON reads it as safe */
        static const int64_t odd[2][2] = {{-1, 0}, {100, -1}};
        for (uint32_t k = 0; k < 2u; k++) {
            int64_t w2[2] = {100, odd[k][0]}, s2[2] = {0, odd[k][1]};
            zxapp_arg_t xa[2] = {{"worked", w2, 2}, {"since", s2, 2}};
            CHECK(!fell_crew_safe(odd[k][0], odd[k][1]) &&
                      wave_lp(app, "crew_shift_safe", xa, 2) == ZXAPP_BOTH,
                  "Fell: crew_shift_safe edge %u: Eve refuses, KRON safe: BOTH, raised", k);
        }
    }

    /* FR3: search_grid_sector, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = wrand(FELL_CELL_MIN, 5000);
        d[i] = wrand(1, FELL_COLS_MAX);
        a[i] = wrand(0, c[i] * d[i] - 1); /* inside the grid's columns */
        b[i] = wrand(0, 20 * c[i]);
        if (i == 0) a[i] = 0, b[i] = 0;
        if (i == 1) a[i] = c[i] * d[i] - 1, b[i] = c[i] - 1; /* the last column of row 0 */
        want[i] = fell_grid_sector(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t ga[4] = {{"east", a, WAVE_ROWS},
                         {"north", b, WAVE_ROWS},
                         {"cell", c, WAVE_ROWS},
                         {"cols", d, WAVE_ROWS}};
    CHECK(want[0] == 1 && want[1] == d[1] &&
              wave_same(app, "search_grid_sector", ga, 4, want, WAVE_ROWS, false),
          "Fell: search_grid_sector, KRON = C");
    {
        /* a point past the last column, or west of the corner: refused by KRON and C */
        int64_t e2[2] = {0, 3000}, n2[2] = {0, 0}, c2[2] = {1000, 1000}, k2[2] = {3, 3};
        zxapp_arg_t xa[4] = {{"east", e2, 2}, {"north", n2, 2}, {"cell", c2, 2}, {"cols", k2, 2}};
        zxapp_num_t o[WAVE_ROWS];
        uint32_t n = 0;
        int32_t lp = -1;
        int rc = zxapp_call_witnessed(app, &H, "search_grid_sector", xa, 4, o, WAVE_ROWS, &n, &lp);
        e2[1] = -1;
        int rc2 = zxapp_call_witnessed(app, &H, "search_grid_sector", xa, 4, o, WAVE_ROWS, &n, &lp);
        CHECK(fell_grid_sector(3000, 0, 1000, 3) == FELL_REFUSED &&
                  fell_grid_sector(-1, 0, 1000, 3) == FELL_REFUSED && rc != ZXAPP_OK &&
                  rc2 != ZXAPP_OK,
              "Fell: search_grid_sector refuses a point past the grid or west of it");
    }

    /* FR4: sector_unsearched, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FELL_X100);
        b[i] = 5000;
        if (i == 0) a[i] = 4999;
        if (i == 1) a[i] = 5000;
        want[i] = T_(fell_unsearched(a[i], b[i]));
    }
    zxapp_arg_t ua[2] = {{"cov", a, WAVE_ROWS}, {"bar", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "sector_unsearched", ua, 2, want, WAVE_ROWS, true),
          "Fell: sector_unsearched, KRON = C");

    /* FR5: sightings_between, KRON = C, one count over 64 sightings */
    {
        int64_t from = 51000, to = 51900, n1;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(50000, 53000);
            b[i] = from, c[i] = to;
        }
        a[0] = from, a[1] = to, a[2] = from - 1, a[3] = to + 1;
        n1 = fell_sightings_between(a, WAVE_ROWS, from, to);
        zxapp_arg_t sa[3] = {{"stamp", a, WAVE_ROWS}, {"from", b, WAVE_ROWS}, {"to", c, WAVE_ROWS}};
        CHECK(n1 >= 2 && wave_same(app, "sightings_between", sa, 3, &n1, 1, false),
              "Fell: sightings_between, %lld in the window", (long long) n1);
    }

    /* FR6: callout_claim, Sutra = C, and its overflow refused */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 48);
        b[i] = wrand(0, 9000);
        c[i] = wrand(0, 600);
        d[i] = wrand(0, 90);
        if (i == 0) a[i] = FELL_HOURS_MAX, b[i] = FELL_RATE_MAX, c[i] = 0, d[i] = 0;
        if (i == 1) a[i] = 0, b[i] = 0, c[i] = FELL_MILES_MAX, d[i] = FELL_MILES_MAX;
        want[i] = fell_callout_claim(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t la[4] = {{"hours", a, WAVE_ROWS},
                         {"rate", b, WAVE_ROWS},
                         {"miles", c, WAVE_ROWS},
                         {"permile", d, WAVE_ROWS}};
    CHECK(want[0] == 998999001 && want[1] == 99980001 &&
              wave_same(app, "callout_claim", la, 4, want, WAVE_ROWS, false),
          "Fell: callout_claim, Sutra = C");
    {
        /* past 9(9), a negative figure, or a rate past 9(6): refused by Sutra and C */
        static const int64_t bad[3][4] = {
            {999, 999999, 9999, 9999}, {-1, 100, 0, 0}, {1, 1000000, 0, 0}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t h1[1] = {bad[k][0]}, r1[1] = {bad[k][1]}, m1[1] = {bad[k][2]},
                    q1[1] = {bad[k][3]};
            zxapp_arg_t xa[4] = {
                {"hours", h1, 1}, {"rate", r1, 1}, {"miles", m1, 1}, {"permile", q1, 1}};
            zxapp_num_t o[WAVE_ROWS];
            uint32_t n = 0;
            int32_t lp = -1;
            int rc = zxapp_call_witnessed(app, &H, "callout_claim", xa, 4, o, WAVE_ROWS, &n, &lp);
            CHECK(fell_callout_claim(bad[k][0], bad[k][1], bad[k][2], bad[k][3]) == FELL_REFUSED &&
                      rc != ZXAPP_OK,
                  "Fell: callout_claim edge %u refused by Sutra and C", k);
        }
    }

    /* FR7: casualty_record_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-12000, 12000);
        d[i] = wrand(-12, 12);
        e[i] = wrand(0, 1700000000000LL);
        if (i == 0) a[i] = 999999, b[i] = 999, c[i] = 9999, d[i] = 9, e[i] = 9999999999999LL;
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 4, d[i] = 10, e[i] = 0;
        if (i == 2) a[i] = 1, b[i] = 1, c[i] = 4, d[i] = 2, e[i] = 10000000000000LL;
        want[i] = T_(fell_casualty_fits(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t ra[5] = {{"incident", a, WAVE_ROWS},
                         {"casualty", b, WAVE_ROWS},
                         {"sector", c, WAVE_ROWS},
                         {"triage", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "casualty_record_ok", ra, 5, want, WAVE_ROWS, true),
          "Fell: casualty_record_ok, MYRA = C");

    /* the BOTH test (FR8): a sighting in sector 4 at 14:10 and another in sector 9 at 14:12.
     * Each says TRUE of its own sector and FALSE of the other; joined (lpres_kjoin) both
     * sectors are BOTH, the lead over the sectors (lpres_disjoin) is BOTH, state 3, and both
     * sectors stay leads: neither is dropped. KRON counts both in the window. */
    {
        int64_t seen[2] = {4, 9}, when[2] = {51000, 51120}; /* 14:10 and 14:12, seconds */
        lpres_state_t lead = LPRES_STATE_NEITHER;
        uint32_t leads = 0;
        bool four = false, nine = false;
        for (int64_t s = 1; s <= 12; s++) {
            lpres_state_t st = fell_sector_state(seen, 2, s);
            lead = s == 1 ? st : lpres_disjoin(lead, st);
            if (fell_is_lead(st)) leads++;
            if (s == 4) four = st == LPRES_STATE_BOTH && fell_is_lead(st);
            if (s == 9) nine = st == LPRES_STATE_BOTH && fell_is_lead(st);
        }
        int64_t f2[2] = {51120 - FELL_SIGHT_WINDOW, 51120 - FELL_SIGHT_WINDOW},
                t2[2] = {51120, 51120}, n2 = 2;
        zxapp_arg_t sa[3] = {{"stamp", when, 2}, {"from", f2, 2}, {"to", t2, 2}};
        CHECK((int) lead == 3 && lead == LPRES_STATE_BOTH && four && nine && leads == 2u &&
                  fell_sightings_between(when, 2, f2[0], t2[0]) == 2 &&
                  wave_same(app, "sightings_between", sa, 3, &n2, 1, false),
              "Fell: sector 4 at 14:10 and sector 9 at 14:12: BOTH (state 3), both kept");
        int64_t one[1] = {4};
        CHECK(fell_sector_state(one, 1, 4) == LPRES_STATE_TRUE &&
                  fell_sector_state(one, 1, 9) == LPRES_STATE_FALSE &&
                  fell_sector_state(one, 0, 4) == LPRES_STATE_NEITHER,
              "Fell: one sighting is TRUE where it was seen, FALSE elsewhere; none is NEITHER");
    }

    /* the parts, called through Fell as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-95000000, 95000000), b[i] = wrand(-185000000, 185000000);
        want[i] = T_(a[i] >= -90000000 && a[i] <= 90000000 && b[i] >= -180000000 &&
                     b[i] <= 180000000); /* waymark_model.c good_point */
    }
    zxapp_arg_t wa[2] = {{"lat", a, WAVE_ROWS}, {"lon", b, WAVE_ROWS}};
    CHECK(wave_same(app, "waymark.place_ok", wa, 2, want, WAVE_ROWS, true),
          "Fell: waymark.place_ok, a place on the globe");
    {
        bool all = true;
        for (uint32_t i = 0; i < 8u; i++)
            all = fe_tag_row(app, wrand(0, 1), wrand(0, 9), wrand(0, 9)) && all;
        int64_t sa[1] = {2}, ea[1] = {0}, ta[1] = {0};
        zxapp_arg_t xa[3] = {{"sync", sa, 1}, {"shell", ea, 1}, {"trunk", ta, 1}};
        CHECK(all && wave_lp(app, "aerie.frame_tag", xa, 3) == ZXAPP_BOTH,
              "Fell: aerie.frame_tag, a line frame's tag; sync 2 held");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        /* inside the band, or past the largest payload: Eve's ranges and KRON's relation
         * agree on both (a message too big for a frame of its range is held, tested below) */
        a[i] = i % 4u ? wrand(0, AERIE_PAYLOAD_MAX) : wrand(AERIE_PAYLOAD_MAX + 1, 2100);
        b[i] = i % 4u ? wrand(a[i] + AERIE_OVERHEAD, AERIE_FRAME_MAX)
                      : wrand(AERIE_FRAME_MIN, AERIE_FRAME_MAX);
        want[i] = T_(aerie_band_fits(a[i], b[i]));
    }
    zxapp_arg_t ba[2] = {{"bytes", a, WAVE_ROWS}, {"frame", b, WAVE_ROWS}};
    CHECK(wave_same(app, "aerie.band_fits", ba, 2, want, WAVE_ROWS, true),
          "Fell: aerie.band_fits, a radio message fits its band");
    {
        int64_t y2[2] = {600, 600}, f2[2] = {560, 2096};
        zxapp_arg_t xa[2] = {{"bytes", y2, 2}, {"frame", f2, 2}};
        CHECK(
            !aerie_band_fits(600, 560) && wave_lp(app, "aerie.band_fits", xa, 2) == ZXAPP_BOTH,
            "Fell: 600 bytes on a 560-byte frame: Eve's ranges pass, KRON's relation fails: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? 1 : wrand(0, 1), b[i] = i % 3u ? 1 : wrand(0, 1),
        c[i] = i % 5u ? 1 : wrand(0, 1), d[i] = i % 7u ? 1 : wrand(0, 1);
        want[i] = T_(watchfire_place_share_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t pa[4] = {{"started", a, WAVE_ROWS},
                         {"optin", b, WAVE_ROWS},
                         {"listed", c, WAVE_ROWS},
                         {"placeok", d, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.place_share_ok", pa, 4, want, WAVE_ROWS, true),
          "Fell: watchfire.place_share_ok, where the command post is shared");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 12), b[i] = wrand(0, 1200), c[i] = wrand(0, 1700000000000LL);
        want[i] = T_(watchfire_medical_fits(a[i], b[i], c[i]));
    }
    zxapp_arg_t ma[3] = {{"field", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.medical_id_ok", ma, 3, want, WAVE_ROWS, true),
          "Fell: watchfire.medical_id_ok, a casualty's medical note");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? 1 : wrand(0, 1), b[i] = i % 4u ? 1 : wrand(0, 1),
        c[i] = i % 5u ? 1 : wrand(0, 1);
        want[i] = T_(commons_room_post_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t oa[3] = {
        {"member", a, WAVE_ROWS}, {"joined", b, WAVE_ROWS}, {"enabled", c, WAVE_ROWS}};
    CHECK(wave_same(app, "commons.room_post_ok", oa, 3, want, WAVE_ROWS, true),
          "Fell: commons.room_post_ok, may the team room be posted in");
}
