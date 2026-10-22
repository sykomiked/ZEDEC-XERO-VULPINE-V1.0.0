/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Verderer's model against zxvd's real dispatcher (LINK): a species tree
 * and fourteen sightings (made up for the test), sighting 14 held BOTH
 * between the observer and the key until an expert confirms on the
 * owner's word; photos and the weather kept with a sighting; a protected
 * species' place shared on its 10 km cell and another's exact; the log
 * saved to the Hoard and read back only on the owner's word (files.write,
 * files.read); a habitat put on the map (place.here, place.add,
 * place.list) and the log found by search.query. */
#include "verderer_model.h"

static verderer_t VER, VER2;

/* the notice says nothing is safe: "safe" only inside VERDERER_NEVER_SAFE */
static bool ver_never_safe(const char *notice)
{
    const char *tail = strstr(notice, VERDERER_NEVER_SAFE);
    if (!tail || strlen(tail) != strlen(VERDERER_NEVER_SAFE)) return false;
    const char *s = strstr(notice, "safe");
    return s && s > tail;
}

APPS_TEST(verderer)
{
    static char line[VERDERER_SHARE_MAX], notice[VERDERER_NOTICE_MAX];
    int64_t n = 0;

    /* the rules */
    {
        int64_t score[3] = {5, 9, 7}, taxon[3] = {301, 302, 303}, rank[3] = {0, 0, 0};
        CHECK(verderer_estimate(100, 60, 20) == 300 && verderer_estimate(50, 40, 7) == 285 &&
                  verderer_estimate(100, 60, 0) == VERDERER_REFUSED &&
                  verderer_estimate(10, 5, 6) == VERDERER_REFUSED &&
                  verderer_coarse(-1, VERDERER_CELL, 1) == -VERDERER_CELL &&
                  verderer_coarse(-1, VERDERER_CELL, 0) == -1 &&
                  verderer_visit_ok(6, 8, 150, 100, 0) && !verderer_visit_ok(9, 8, 150, 100, 0) &&
                  !verderer_visit_ok(6, 8, 99, 100, 0) && !verderer_visit_ok(6, 8, 150, 100, 1) &&
                  verderer_rank(score, taxon, 3, 0, rank) == ZXM_OK && rank[0] == 3 &&
                  rank[1] == 1 && rank[2] == 2 &&
                  verderer_rank(score, taxon, 3, 303, rank) == ZXM_OK && rank[2] == 1 &&
                  rank[1] == 2,
              "verderer: the estimate, the coarse coordinate, a visit's limits, the ranking");
        static const int64_t marks[3][2] = {{0, 0}, {-1, 0}, {1, 0}};
        bool ok = true;
        for (uint32_t i = 0; i < 3u; i++)
            ok = ok && verderer_caution(marks[i], 2, notice, sizeof notice) == (int64_t) i &&
                 ver_never_safe(notice);
        CHECK(ok &&
                  verderer_caution(NULL, 0, notice, sizeof notice) == FIELDGUIDE_CAUTION_UNKNOWN &&
                  ver_never_safe(notice),
              "verderer: every caution ends with the warning and none says safe: %s", notice);
    }

    /* VM1, VM2: refused before anything is sent, nothing kept */
    verderer_init(&VER, &LINK);
    int before = sent;
    CHECK(verderer_add_species(&VER, 1, 5219, 2433406, 0, 1) == ZXM_E_ARG &&
              verderer_add_species(&VER, 10, 5219, 2433406, 301, 1) == ZXM_E_ARG &&
              verderer_add_species(&VER, 1, 5219, 2433406, 301, 2) == ZXM_E_ARG &&
              verderer_sight(&VER, 0, 1, 0, 0, 1, NULL) == ZXM_E_ARG &&
              verderer_sight(&VER, 301, 0, 0, 0, 1, NULL) == ZXM_E_ARG &&
              verderer_sight(&VER, 301, 1, VERDERER_LAT_MAX + 1, 0, 1, NULL) == ZXM_E_ARG &&
              verderer_identify(&VER, 1, -302) == ZXM_E_STATE &&
              verderer_share(&VER, 1, line, sizeof line) == ZXM_E_STATE &&
              verderer_mark(&VER, "", 0, 0, NULL, true) == ZXM_E_ARG &&
              verderer_search(&VER, "  ") == ZXM_E_ARG && VER.nsp == 0 && VER.ns == 0 &&
              sent == before,
          "verderer: refused before anything is sent");

    /* the tree: 301 protected, 302 and 303 not */
    CHECK(verderer_add_species(&VER, 1, 5219, 2433406, 301, 1) == ZXM_OK &&
              verderer_add_species(&VER, 1, 5219, 2433406, 302, 0) == ZXM_OK &&
              verderer_add_species(&VER, 6, 2650, 2650100, 303, 0) == ZXM_OK &&
              verderer_add_species(&VER, 1, 5219, 2433406, 302, 0) == ZXM_E_STATE && VER.nsp == 3 &&
              verderer_protected(&VER, 301) == 1 && verderer_protected(&VER, 302) == 0 &&
              verderer_protected(&VER, 999) == 1,
          "verderer: the species tree, an unknown species treated as protected");

    /* fourteen sightings; 14 is 301 to the observer */
    bool all = true;
    for (int64_t i = 1; i <= 13; i++)
        all = all &&
              verderer_sight(&VER, i % 2 ? 302 : 303, i, 59913900 + i, 10752200 - i,
                             1792627200 + i * 60, &n) == ZXM_OK &&
              n == i;
    CHECK(all && verderer_sight(&VER, 301, 2, 59913900, 10752200, 1792628100, &n) == ZXM_OK &&
              n == 14 && VER.ns == 14 && verderer_find(&VER, 14)->said == LPRES_STATE_TRUE,
          "verderer: fourteen sightings numbered in order");

    /* VM4, the BOTH test: the key reaches 302 for sighting 14 */
    {
        verderer_sighting_t *s = verderer_find(&VER, 14);
        CHECK(verderer_identify(&VER, 14, 302) == ZXM_E_ARG &&
                  verderer_identify(&VER, 14, -302) == ZXM_OK && s->said == LPRES_STATE_BOTH &&
                  (int) s->said == 3 && s->observer == 301 && s->key == 302 && VER.ns == 14,
              "verderer: sighting 14, 301 by the observer and 302 by the key: BOTH, kept");
        int32_t len = verderer_share(&VER, 14, line, sizeof line);
        CHECK(len > 0 &&
                  strstr(line, "Sighting 14: species 301 (unconfirmed: the observer says "
                               "301, the key says 302)") != NULL &&
                  strstr(line, "59.850000,10.620000 (10 km grid") != NULL,
              "verderer: shown with both names, on its 10 km cell: %s", line);
        before = sent;
        CHECK(verderer_confirm(&VER, 14, 302, false) == ZXM_E_STATE &&
                  s->said == LPRES_STATE_BOTH && s->confirmed == 0 && sent == before,
              "verderer: no expert's word without the owner's: still BOTH, nothing sent");
        CHECK(verderer_identify(&VER, 1, -302) == ZXM_OK &&
                  verderer_find(&VER, 1)->said == LPRES_STATE_TRUE,
              "verderer: the key agreeing with the observer: TRUE");
    }

    /* VM3, VM5: a photo that fits, one that does not, and the weather */
    {
        verderer_sighting_t *s = verderer_find(&VER, 3);
        CHECK(verderer_photo(&VER, 3, 3000000, 4000000, 1000000, 4032, 3024, 1792627380000) ==
                      ZXM_OK &&
                  s->photo_bytes == 3000000 &&
                  verderer_photo(&VER, 4, 3000001, 4000000, 1000000, 4032, 3024, 1) ==
                      ZXM_E_STATE &&
                  verderer_photo(&VER, 4, 5, 4000000, 0, 100000, 3024, 1) == ZXM_E_ARG &&
                  verderer_find(&VER, 4)->photo_bytes == 0 &&
                  verderer_photo(&VER, 99, 5, 10, 0, 1, 1, 1) == ZXM_E_STATE,
              "verderer: photo fits (Lifeboat's rule) and its record fits (Pinhole's)");
        CHECK(verderer_reading(&VER, 3, 1792627200, -35, 41, 2) == ZXM_OK && s->temp == -35 &&
                  verderer_reading(&VER, 4, 1792627200, 1001, 41, 2) == ZXM_E_ARG &&
                  verderer_reading(&VER, 4, INT64_C(100000000000), 0, 0, 0) == ZXM_E_ARG &&
                  verderer_find(&VER, 4)->wstamp == 0,
              "verderer: the reading of the hour, Weatherglass's record");
    }

    /* VM6: an unprotected species' place leaves exact */
    CHECK(verderer_share(&VER, 1, line, sizeof line) > 0 &&
              strstr(line, "Sighting 1: species 302, 1 seen at 59.913901,10.752199") != NULL &&
              strstr(line, "grid") == NULL,
          "verderer: 302 is not protected: exact: %s", line);

    /* VR5: the grid; 302 seven times in its cell */
    CHECK(verderer_grid(&VER, 302, 59913900, 10752200) == 7 &&
              verderer_grid(&VER, 303, 59913900, 10752200) == 6 &&
              verderer_grid(&VER, 301, 59913900, 10752200) == 1 &&
              verderer_grid(&VER, 302, 0, 0) == 0,
          "verderer: the sighting grid, species by cell");

    /* VM7: saved only on the owner's word, read back whole */
    before = sent;
    CHECK(verderer_save(&VER, false) == ZXM_E_STATE && sent == before && VER.dirty,
          "verderer: not saved without the owner's word, nothing sent");
    CHECK(verderer_save(&VER, true) == ZXM_OK && !VER.dirty, "verderer: saved: %s", LINK.err);
    verderer_init(&VER2, &LINK);
    CHECK(verderer_load(&VER2) == ZXM_OK && VER2.nsp == 3 && VER2.ns == 14 &&
              !memcmp(VER2.s, VER.s, sizeof VER.s[0] * 14u) &&
              !memcmp(VER2.sp, VER.sp, sizeof VER.sp[0] * 3u) &&
              verderer_find(&VER2, 14)->said == LPRES_STATE_BOTH,
          "verderer: read back, sighting 14 still BOTH: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", VERDERER_FILE);
    zxm_p_str(&LINK, "text", "verderer 1\ns 1 301 0 0 95000000 0 1 1 1 0 0 0 0 0 0 0 0\n");
    CHECK(zxm_call(&LINK) == ZXM_OK && verderer_load(&VER2) == ZXM_E_STATE && VER2.ns == 14,
          "verderer: a file that does not parse leaves what is open as it was");
    CHECK(verderer_save(&VER, true) == ZXM_OK, "verderer: saved again: %s", LINK.err);

    /* VM4: the expert confirms, on the owner's word */
    CHECK(verderer_confirm(&VER, 14, 302, true) == ZXM_OK &&
              verderer_find(&VER, 14)->said == LPRES_STATE_TRUE &&
              verderer_identify(&VER, 14, -303) == ZXM_E_STATE &&
              verderer_share(&VER, 14, line, sizeof line) > 0 &&
              strstr(line, "species 302") != NULL && strstr(line, "unconfirmed") == NULL &&
              strstr(line, "grid") == NULL,
          "verderer: confirmed 302, not protected: exact: %s", line);

    /* VM8: the map */
    zxm_begin(&LINK, "place.set_here");
    zxm_p_i64(&LINK, "lat", 59913900);
    zxm_p_i64(&LINK, "lon", 10752200);
    CHECK(zxm_call(&LINK) == ZXM_OK && verderer_here(&VER) == ZXM_OK && VER.here_known &&
              VER.here_lat == 59913900 && VER.here_lon == 10752200,
          "verderer: here, from place.here: %s", LINK.err);
    CHECK(verderer_sight_here(&VER, 303, 1, 1792629000, &n) == ZXM_OK && n == 15 &&
              verderer_find(&VER, 15)->lat == 59913900,
          "verderer: a sighting here");
    before = sent;
    uint32_t places = VER.nplaces;
    CHECK(verderer_mark(&VER, "Verderer alder carr", 59920000, 10760000, "otter holt", false) ==
                  ZXM_E_STATE &&
              sent == before && VER.nplaces == places,
          "verderer: no map place without the owner's word, nothing sent");
    bool found = false;
    CHECK(verderer_mark(&VER, "Verderer alder carr", 59920000, 10760000, "otter holt", true) ==
              ZXM_OK,
          "verderer: a habitat on the map: %s", LINK.err);
    for (uint32_t i = 0; i < VER.nplaces; i++)
        found = found || (!strcmp(VER.place[i].name, "Verderer alder carr") &&
                          VER.place[i].lat == 59920000 && VER.place[i].lon == 10760000);
    CHECK(found && verderer_places(&VER) == ZXM_OK, "verderer: the habitat listed exact");

    /* VM9: the log found by its words */
    found = false;
    CHECK(verderer_search(&VER, "Verderer") == ZXM_OK, "verderer: search: %s", LINK.err);
    for (uint32_t i = 0; i < VER.nhits; i++) found = found || !strcmp(VER.hit[i], VERDERER_FILE);
    CHECK(found, "verderer: the log is found (%u hits)", VER.nhits);
}
