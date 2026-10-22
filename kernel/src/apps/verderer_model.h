/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* verderer_model.h — Verderer, the field survey, as a model (zxm.h):
 * wildlife sightings, trails and habitats logged with their photos and
 * the weather of the hour, kept in the Hoard, put on the device's map and
 * found again. No drawing, no allocation, integers only; the rules it runs
 * are verderer_rules.h (VR1-VR6) and its parts' (Pinhole, Lifeboat,
 * Waymark, Weatherglass, Fieldguide, Assay).
 *
 * zxvd methods used: files.read, files.write, place.here, place.add,
 * place.list and search.query.
 *
 *   VM1 THE SPECIES TREE. verderer_add_species keeps a species under its
 *       kingdom, family and genus (GBIF keys), protected or not, when it
 *       fits the tree (VR6); a species is kept once. A species not in the
 *       tree is treated as protected wherever protection matters (VM6), a
 *       default taken because a place given away cannot be taken back.
 *   VM2 A SIGHTING is a species as the observer named it, how many were
 *       seen (1 .. 999999), where (a point waymark.place_ok accepts, or
 *       here from place.here) and when (seconds since 1970). Sightings are
 *       numbered 1, 2, ... in the order they are logged, at most
 *       VERDERER_SIGHTINGS.
 *   VM3 A PHOTO kept with a sighting: its bytes must fit what may still be
 *       written with the owner's reserve kept free ("photo fits",
 *       lifeboat.restore_fits as Pinhole uses it) and its record must fit
 *       Pinhole's (pinhole.photo_record_ok); else it is refused.
 *   VM4 IDENTIFICATION. The key (fieldguide.key_next_lead, walked in
 *       Fieldguide) reaches a taxon, given here as its last lead (the
 *       taxon negated). What the observer and the key say the sighting is
 *       are joined four-valued (lpres_kjoin): the same species TRUE, two
 *       species BOTH. A BOTH sighting is kept, both names shown, until an
 *       expert confirms it (verderer_confirm, on the owner's word). The
 *       candidates a search finds are ranked as Fieldguide ranks matches
 *       (fieldguide.match_order_key, assay.score_rank).
 *   VM5 THE WEATHER of the hour kept with a sighting, a reading from
 *       Weatherglass that fits its record (weatherglass.forecast_ok) and
 *       its domains (-100.0 .. 100.0 C, wind 0 .. 200.0 m/s, rain 0 ..
 *       9999.9 mm, in tenths).
 *   VM6 SHARE THIS PLACE. verderer_share composes the line a sighting
 *       leaves the device as. A protected (or unknown) species' place is
 *       floored to its 10 km grid cell (VR2) and checked on a corner of it
 *       (VR3) first; any other's is exact. Nothing is sent here: the view
 *       hands the line on.
 *   VM7 THE FILE. VERDERER_FILE in the Hoard holds the tree and the
 *       sightings, one line each, written whole (files.write) only on the
 *       owner's word and read back with files.read; a file that does not
 *       parse leaves what is open as it was.
 *   VM8 THE MAP. verderer_here reads place.here; verderer_mark keeps a
 *       trail point or habitat on the device's map (place.add) on the
 *       owner's word, the point checked first; verderer_places reads them
 *       back (place.list). The map stays on the device, so it is exact.
 *   VM9 SEARCH. verderer_search finds notes and logs in the Hoard by their
 *       words (search.query), at most VERDERER_HITS.
 *
 * Advisory: the population estimate (VR1), the sighting grid (VR5), a
 * protected area's limits (VR4) and the caution say what the numbers say;
 * the owner decides. The caution never says a plant or fungus is safe to
 * eat (Fieldguide's rule, VERDERER_NEVER_SAFE). */
#ifndef VERDERER_MODEL_H
#define VERDERER_MODEL_H

#include "verderer_rules.h"
#include "zxm.h"

#define VERDERER_FILE       "Verderer.sightings"
#define VERDERER_SPECIES    64u
#define VERDERER_SIGHTINGS  64u
#define VERDERER_PLACES     200u /* zxv_place.h ZPL_PLACES */
#define VERDERER_HITS       20u
#define VERDERER_NAME_MAX   63u  /* zxv_place.h ZPL_NAME_MAX */
#define VERDERER_NOTE_MAX   255u /* zxv_place.h ZPL_NOTE_MAX */
#define VERDERER_PATH_MAX   256u /* one Hoard path, with its NUL */
#define VERDERER_QUERY_MAX  256u /* search.query's q */
#define VERDERER_TEXT_MAX   12288u
#define VERDERER_SHARE_MAX  192u
#define VERDERER_NOTICE_MAX 320u
#define VERDERER_TEMP_MAX   1000  /* VM5: tenths of a degree C */
#define VERDERER_WIND_MAX   2000  /* VM5: tenths of a m/s */
#define VERDERER_RAIN_MAX   99999 /* VM5: tenths of a mm */

#define VERDERER_NEVER_SAFE                                                                        \
    "Verderer never says a plant or fungus is safe to eat; do not eat anything on its word."

typedef struct {
    int64_t kingdom, family, genus, species;
    int64_t protected_; /* 1 protected, 0 not */
} verderer_species_t;

typedef struct {
    int64_t n;         /* its number, 1.. */
    int64_t observer;  /* the species the observer named */
    int64_t key;       /* the species the key reached, 0 none */
    int64_t confirmed; /* the species an expert confirmed, 0 none */
    int64_t lat, lon, stamp, count;
    lpres_state_t said;                                          /* VM4 */
    int64_t photo_bytes, photo_width, photo_height, photo_stamp; /* VM3, 0 none */
    int64_t wstamp, temp, wind, rain;                            /* VM5, wstamp 0 none */
} verderer_sighting_t;

typedef struct {
    char name[VERDERER_NAME_MAX + 1];
    int64_t lat, lon;
} verderer_place_t;

typedef struct {
    zxm_link_t *link;
    verderer_species_t sp[VERDERER_SPECIES];
    uint32_t nsp;
    verderer_sighting_t s[VERDERER_SIGHTINGS];
    uint32_t ns;
    bool here_known;
    int64_t here_lat, here_lon;
    verderer_place_t place[VERDERER_PLACES];
    uint32_t nplaces;
    char hit[VERDERER_HITS][VERDERER_PATH_MAX];
    uint32_t nhits;
    bool dirty; /* changed since saved or loaded */
} verderer_t;

void verderer_init(verderer_t *v, zxm_link_t *link);
/* VM1 */
int verderer_add_species(verderer_t *v, int64_t kingdom, int64_t family, int64_t genus,
                         int64_t species, int64_t protected_);
/* 1 protected, 0 not, and 1 for a species not in the tree */
int64_t verderer_protected(const verderer_t *v, int64_t species);
/* VM2: the new sighting's number in *n (may be NULL) */
int verderer_sight(verderer_t *v, int64_t observer, int64_t count, int64_t lat, int64_t lon,
                   int64_t stamp, int64_t *n);
int verderer_sight_here(verderer_t *v, int64_t observer, int64_t count, int64_t stamp, int64_t *n);
verderer_sighting_t *verderer_find(verderer_t *v, int64_t n);
/* VM3 */
int verderer_photo(verderer_t *v, int64_t n, int64_t bytes, int64_t free_bytes, int64_t reserve,
                   int64_t width, int64_t height, int64_t stamp);
/* VM4 */
int verderer_identify(verderer_t *v, int64_t n, int64_t lead);
int verderer_confirm(verderer_t *v, int64_t n, int64_t species, bool owner_confirmed);
/* VM4: rank n candidates (search scores, their taxa) with the taxon the
 * key reached first; rank[i] 1 best */
int verderer_rank(const int64_t *score, const int64_t *taxon, uint32_t n, int64_t reached,
                  int64_t *rank);
/* VM5 */
int verderer_reading(verderer_t *v, int64_t n, int64_t stamp, int64_t temp, int64_t wind,
                     int64_t rain);
/* VM6: the line's length, or a ZXM_E_* code */
int32_t verderer_share(const verderer_t *v, int64_t n, char *out, uint32_t cap);
/* VM7 */
int verderer_save(verderer_t *v, bool owner_confirmed);
int verderer_load(verderer_t *v);
/* VM8 */
int verderer_here(verderer_t *v);
int verderer_mark(verderer_t *v, const char *name, int64_t lat, int64_t lon, const char *note,
                  bool owner_confirmed);
int verderer_places(verderer_t *v);
/* VM9 */
int verderer_search(verderer_t *v, const char *query);
/* advisory: VR1; VR5 for the cell of (lat, lon); VR4 out of the area's
 * limits; the caution over n article marks (FR1) with its notice */
int64_t verderer_estimate(int64_t marked, int64_t caught, int64_t recaptured);
int64_t verderer_grid(const verderer_t *v, int64_t species, int64_t lat, int64_t lon);
bool verderer_visit_ok(int64_t party, int64_t party_max, int64_t distance, int64_t distance_min,
                       int64_t drone);
int64_t verderer_caution(const int64_t *mark, uint32_t n, char *notice, uint32_t cap);

#endif
