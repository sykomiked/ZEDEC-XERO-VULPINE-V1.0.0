/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Dibber's model against zxvd's real dispatcher (LINK): plot 7 and four
 * beds (made up for the test), a rotation that refuses a repeat, bed 4
 * read at 18 and 41 percent on one day and its planting suggestion held
 * for a re-test, a watering inside the week's budget, one over it and one
 * in a frost; the bed sheet and the supply list summed; the plan saved to
 * the Hoard and read back only on the owner's word (files.write,
 * files.read); where the plot is from place.here. */
#include "dibber_model.h"

static dibber_t DIB, DIB2;

APPS_TEST(dibber)
{
    static char notice[DIBBER_NOTICE_MAX];
    const int64_t day = 20748; /* 2026-10-22 */
    const dibber_reading_t mild = {1792627200, 120, 41, 0}, frost = {1792627200, -15, 41, 0},
                           bad = {1792627200, 1001, 41, 0};

    /* the rules, through the model */
    {
        static const int64_t mean[4] = {120, 150, 90, 200}, cnt[2] = {12, 3},
                             price[2] = {250, 1999}, over[1] = {DIBBER_COUNT_MAX + 1};
        int64_t tot = 0;
        CHECK(dibber_next_task(100, 7, 101) == 107 && dibber_next_task(100, 7, 50) == 100 &&
                  dibber_next_task(100, 0, 50) == SOLSTICE_REFUSED &&
                  dibber_harvest(mean, 4, 100, 100) == 4 &&
                  dibber_harvest(mean, 4, 100, 1000) == DIBBER_REFUSED &&
                  dibber_supply_total(cnt, price, 2, &tot) == ZXM_OK && tot == 8997 &&
                  dibber_supply_total(over, price, 1, &tot) == ZXM_E_ARG && tot == 8997,
              "dibber: the next task, days to harvest and the supply list (%lld)", (long long) tot);
    }

    /* DM1, DM2: refused before anything is sent, nothing kept */
    CHECK(dibber_init(&DIB, &LINK, 0) == ZXM_E_ARG && dibber_add_bed(&DIB, 1, 100) == ZXM_E_ARG,
          "dibber: no plot 0");
    CHECK(dibber_init(&DIB, &LINK, 7) == ZXM_OK, "dibber: plot 7");
    int before = sent;
    CHECK(dibber_add_bed(&DIB, 0, 100) == ZXM_E_ARG && dibber_add_bed(&DIB, 1, 0) == ZXM_E_ARG &&
              dibber_add_bed(&DIB, 10000, 100) == ZXM_E_ARG &&
              dibber_add_bed(&DIB, 1, DIBBER_AREA_MAX + 1) == ZXM_E_ARG &&
              dibber_plant(&DIB, 1, 2026, 3) == ZXM_E_STATE &&
              dibber_soil(&DIB, 4, day, 18) == ZXM_E_STATE &&
              dibber_soil(&DIB, 4, day, 101) == ZXM_E_ARG &&
              dibber_set_budget(&DIB, -1) == ZXM_E_ARG && DIB.nbeds == 0 && DIB.ncrops == 0 &&
              DIB.nsoil == 0 && sent == before,
          "dibber: refused before anything is sent");
    CHECK(dibber_add_bed(&DIB, 1, 200) == ZXM_OK && dibber_add_bed(&DIB, 2, 150) == ZXM_OK &&
              dibber_add_bed(&DIB, 3, 80) == ZXM_OK && dibber_add_bed(&DIB, 4, 120) == ZXM_OK &&
              dibber_add_bed(&DIB, 4, 50) == ZXM_E_STATE && DIB.nbeds == 4 &&
              dibber_find_bed(&DIB, 4)->area == 120,
          "dibber: four beds, bed 4 kept once");

    /* DM2: the rotation, no repeat on a bed */
    CHECK(
        dibber_plant(&DIB, 1, 2025, 3) == ZXM_OK && dibber_plant(&DIB, 1, 2026, 3) == ZXM_E_STATE &&
            dibber_plant(&DIB, 1, 2026, 5) == ZXM_OK &&
            dibber_plant(&DIB, 1, 2026, 6) == ZXM_E_STATE &&
            dibber_plant(&DIB, 2, 2026, 3) == ZXM_OK && dibber_plant(&DIB, 1, 0, 3) == ZXM_E_ARG &&
            dibber_plant(&DIB, 1, 2027, 0) == ZXM_E_ARG && DIB.ncrops == 3,
        "dibber: a family repeated on bed 1 is refused, another bed takes it");

    /* DM3, DM6, the BOTH test: bed 4 at 18 and 41 percent on one day */
    CHECK(dibber_soil(&DIB, 4, day, 18) == ZXM_OK && dibber_soil(&DIB, 4, day, 41) == ZXM_OK &&
              dibber_soil_on(&DIB, 4, day) == LPRES_STATE_BOTH &&
              dibber_soil_on(&DIB, 4, day - 1) == LPRES_STATE_NEITHER,
          "dibber: bed 4 read at 18 and 41 percent on one day: BOTH");
    CHECK(dibber_suggest(&DIB, 4, day, &mild, notice, sizeof notice) == DIBBER_HELD &&
              strstr(notice, "Bed 4: held: the soil reads 18% and 41% on the same day; re-test "
                             "the soil before planting.") != NULL,
          "dibber: the planting suggestion held, a re-test suggested: %s", notice);
    CHECK(dibber_soil(&DIB, 2, day, 18) == ZXM_OK && dibber_soil(&DIB, 3, day, 41) == ZXM_OK &&
              dibber_suggest(&DIB, 2, day, &mild, notice, sizeof notice) == DIBBER_WATER_FIRST &&
              dibber_suggest(&DIB, 1, day, &mild, notice, sizeof notice) == DIBBER_TEST_SOIL &&
              dibber_suggest(&DIB, 3, day, &mild, notice, sizeof notice) == DIBBER_SOW &&
              strstr(notice, "Advice only.") != NULL,
          "dibber: too dry, no reading, and sow: %s", notice);
    CHECK(dibber_suggest(&DIB, 3, day, &frost, notice, sizeof notice) == DIBBER_WAIT_FROST &&
              strstr(notice, "frost warned (-1.5 C, 29.3 F); wait to sow.") != NULL &&
              dibber_suggest(&DIB, 3, day, &bad, notice, sizeof notice) == ZXM_E_ARG &&
              dibber_suggest(&DIB, 9, day, &mild, notice, sizeof notice) == ZXM_E_STATE,
          "dibber: a frost warning, in C and F; a reading that does not fit is refused");

    /* DM4: the week's water, 100 litres */
    CHECK(dibber_set_budget(&DIB, 10000) == ZXM_OK &&
              dibber_log_water(&DIB, 4, day - 1, 9000) == ZXM_OK &&
              dibber_log_water(&DIB, 4, day - 7, 5000) == ZXM_OK &&
              dibber_log_water(&DIB, 4, day, 0) == ZXM_E_ARG &&
              dibber_week_used(&DIB, day) == 9000 && dibber_week_used(&DIB, day - 1) == 14000,
          "dibber: the week is the day and the six before");
    CHECK(dibber_bed_week_cl(&DIB, 4, 5) == 600 &&
              dibber_may_water(&DIB, 4, day, 5, 2, &mild) == DIBBER_MAY_WATER &&
              dibber_may_water(&DIB, 4, day, 10, 2, &mild) == DIBBER_OVER_BUDGET &&
              dibber_may_water(&DIB, 4, day, 5, 2, &frost) == DIBBER_FROST &&
              dibber_may_water(&DIB, 4, day, 41, 2, &mild) == DIBBER_PAST_LIMITS &&
              dibber_may_water(&DIB, 4, day, 5, 15, &mild) == DIBBER_PAST_LIMITS &&
              dibber_may_water(&DIB, 4, day, 5, 2, &bad) == ZXM_E_ARG &&
              dibber_may_water(&DIB, 9, day, 5, 2, &mild) == ZXM_E_STATE,
          "dibber: may water now: within the budget, over it, a frost, past the limits");

    /* DM5: the bed sheet at 10 mm, a column of centilitres */
    {
        int64_t tot = 0;
        CHECK(dibber_sheet_total(&DIB, 1, 4, 10, &tot) == ZXM_OK && tot == 5500 &&
                  dibber_sheet_total(&DIB, 2, 3, 10, &tot) == ZXM_OK && tot == 2300 &&
                  dibber_sheet_total(&DIB, 3, 2, 10, &tot) == ZXM_E_ARG &&
                  dibber_sheet_total(&DIB, 1, 4, DIBBER_DEPTH_MAX + 1, &tot) == ZXM_E_ARG,
              "dibber: the bed sheet's column sums (%lld)", (long long) tot);
    }

    /* DM8: saved only on the owner's word, read back whole */
    before = sent;
    CHECK(dibber_save(&DIB, false) == ZXM_E_STATE && sent == before && DIB.dirty,
          "dibber: not saved without the owner's word, nothing sent");
    CHECK(dibber_save(&DIB, true) == ZXM_OK && !DIB.dirty, "dibber: saved: %s", LINK.err);
    CHECK(dibber_init(&DIB2, &LINK, 1) == ZXM_OK && dibber_load(&DIB2) == ZXM_OK &&
              DIB2.plot == 7 && DIB2.budget == 10000 && DIB2.nbeds == 4 && DIB2.ncrops == 3 &&
              DIB2.nsoil == 4 && DIB2.nwater == 2 &&
              !memcmp(DIB2.bed, DIB.bed, sizeof DIB.bed[0] * 4u) &&
              !memcmp(DIB2.crop, DIB.crop, sizeof DIB.crop[0] * 3u) &&
              !memcmp(DIB2.water, DIB.water, sizeof DIB.water[0] * 2u) &&
              dibber_soil_on(&DIB2, 4, day) == LPRES_STATE_BOTH && !DIB2.dirty,
          "dibber: read back, bed 4 still BOTH: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", DIBBER_FILE);
    zxm_p_str(&LINK, "text", "dibber 1\np 7 10000\nb 1 200\nc 1 2025 3\nc 1 2026 3\n");
    CHECK(zxm_call(&LINK) == ZXM_OK && dibber_load(&DIB2) == ZXM_E_STATE && DIB2.ncrops == 3 &&
              DIB2.nbeds == 4,
          "dibber: a file that repeats a family on a bed leaves what is open as it was");
    CHECK(dibber_save(&DIB, true) == ZXM_OK, "dibber: saved again: %s", LINK.err);

    /* where the plot is */
    zxm_begin(&LINK, "place.set_here");
    zxm_p_i64(&LINK, "lat", 59913900);
    zxm_p_i64(&LINK, "lon", 10752200);
    CHECK(zxm_call(&LINK) == ZXM_OK && dibber_here(&DIB) == ZXM_OK && DIB.here_known &&
              DIB.here_lat == 59913900 && DIB.here_lon == 10752200,
          "dibber: the plot is here, from place.here: %s", LINK.err);
}
