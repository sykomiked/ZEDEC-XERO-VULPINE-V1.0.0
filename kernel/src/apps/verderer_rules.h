/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* verderer_rules.h — Verderer's rules in C, the reference its Trebago,
 * KRON, Eve and MYRA blocks are checked against (blocks/verderer). Pure
 * integer functions, no state: the model (verderer_model.c) and the
 * language test (tests/verderer_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. Places are in
 * millionths of a degree (zxv_place.h).
 *
 *   VR1 THE POPULATION ESTIMATE (recapture_estimate), by mark and
 *       recapture: marked animals released, caught on a second visit,
 *       recaptured among those caught; floor(marked * caught /
 *       recaptured). marked and caught 1 .. 999999, recaptured 1 .. the
 *       least of them; none recaptured gives no estimate, and more
 *       recaptured than marked or caught is no survey: refused.
 *   VR2 SHARE THIS PLACE, THE COORDINATE (share_coarse_coord): a protected
 *       species' coordinate leaves the device floored to its grid cell,
 *       cell * floor(coord / cell); any other leaves as it is. coord
 *       -180000000 .. 180000000, cell 90000 .. 180000000 (the 10 km grid
 *       or coarser), protected 0 or 1; else refused. The 10 km grid: a
 *       cell is VERDERER_CELL (0.09 degree, 10.02 km) north to south, and
 *       east to west VERDERER_CELL times the band's multiple of the
 *       latitude of the cell's edge nearer the pole, the least divisor of
 *       2000 that keeps the cell at least 10 km wide at that latitude
 *       (verderer_lon_cell, a default taken from 111.32 km a degree at the
 *       equator); so the cells tile -180 .. 180 degrees exactly, and near
 *       a pole a cell is a whole hemisphere's width.
 *   VR3 SHARE THIS PLACE, MAY IT LEAVE (share_coarse_ok): a protected
 *       species' place leaves the device only on a corner of its cell:
 *       latoff and lonoff (the place less its VR2 coordinate) 0 each, and
 *       cell (the east-west cell used) 90000 .. 180000000. Each field
 *       against its own range, so Eve holds it; a cell past half the globe
 *       is no grid cell (Eve no, KRON yes: held).
 *   VR4 A PROTECTED AREA'S LIMITS (reserve_visit_ok), advisory: the room
 *       left under the area's largest party (partyroom 0 .. 999), the
 *       distance kept from a nest, den or roost past the area's least
 *       (distroom 0 .. 1000000 m) and drones flown (drone 0). The area's
 *       own limits are the owner's; verderer_visit_rooms works the rooms
 *       out of them.
 *   VR5 THE SIGHTING GRID (sighting_grid_cell), species by cell: one
 *       sighting a row, row i's count of rows with its species and its
 *       cell, itself included. A cell is numbered by verderer_cell_of.
 *   VR6 THE SPECIES TREE (species_tree_ok) fits ^ZXVERDERER(kingdom,
 *       family, genus, species) = protected: kingdom at most 1 digit,
 *       family, genus and species 9 each (GBIF keys), protected 1, sign
 *       aside.
 *   VR7 WHAT A SIGHTING IS (verderer_identity, kept in C): the observer's
 *       word and the identification key's, joined four-valued
 *       (lpres_kjoin): one species named by both TRUE, two species BOTH,
 *       kept with both names until an expert confirms.
 *
 * The parts' rules, each the C witness of a composed entry: a point on
 * the globe (waymark.place_ok, verderer_place_ok), a reading's record
 * (weatherglass.forecast_ok, verderer_reading_ok: ^ZXWEATHER's 11, 4, 4
 * and 5 digits), and Pinhole's, Lifeboat's, Fieldguide's and Assay's own
 * headers. Anything outside is refused (VERDERER_REFUSED), never rounded
 * or wrapped. */
#ifndef VERDERER_RULES_H
#define VERDERER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "assay_rules.h"
#include "fieldguide_rules.h"
#include "lpres.h"
#include "pinhole_rules.h"

#define VERDERER_REFUSED     INT64_MIN
#define VERDERER_COUNT_MAX   INT64_C(999999)    /* VR1 */
#define VERDERER_CELL        INT64_C(90000)     /* VR2: 0.09 degree, 10.02 km */
#define VERDERER_CELL_MAX    INT64_C(180000000) /* VR2, VR3: half the globe */
#define VERDERER_LAT_MAX     INT64_C(90000000)
#define VERDERER_LON_MAX     INT64_C(180000000)
#define VERDERER_PARTY_MAX   INT64_C(999)       /* VR4 */
#define VERDERER_DIST_MAX    INT64_C(1000000)   /* VR4: metres */
#define VERDERER_KEY_MAX     INT64_C(999999999) /* VR6: a GBIF key's 9 digits */
#define VERDERER_KINGDOM_MAX 9                  /* VR6: 1 digit */

/* VR1 */
static inline int64_t verderer_recapture_estimate(int64_t marked, int64_t caught,
                                                  int64_t recaptured)
{
    if (marked < 1 || marked > VERDERER_COUNT_MAX || caught < 1 || caught > VERDERER_COUNT_MAX)
        return VERDERER_REFUSED;
    if (recaptured < 1 || recaptured > marked || recaptured > caught) return VERDERER_REFUSED;
    return marked * caught / recaptured; /* both positive: truncation is the floor */
}

/* floor(a / b), b > 0 */
static inline int64_t verderer_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

/* VR2 */
static inline int64_t verderer_coarse(int64_t coord, int64_t cell, int64_t protected_)
{
    if (coord < -VERDERER_LON_MAX || coord > VERDERER_LON_MAX || cell < VERDERER_CELL ||
        cell > VERDERER_CELL_MAX || protected_ < 0 || protected_ > 1)
        return VERDERER_REFUSED;
    return protected_ ? cell * verderer_floor_div(coord, cell) : coord;
}

/* VR2: the east-west cell at a latitude, or VERDERER_REFUSED off the globe.
 * The bands, by whole degrees of the cell's edge nearer the pole (rounded
 * up), and the multiple of VERDERER_CELL each takes. */
static inline int64_t verderer_lon_cell(int64_t lat)
{
    static const int64_t upto[11] = {3, 60, 75, 78, 82, 84, 86, 87, 88, 89, 90};
    static const int64_t times[11] = {1, 2, 4, 5, 8, 10, 16, 20, 40, 80, 2000};
    if (lat < -VERDERER_LAT_MAX || lat > VERDERER_LAT_MAX) return VERDERER_REFUSED;
    int64_t south = VERDERER_CELL * verderer_floor_div(lat, VERDERER_CELL);
    int64_t north = south + VERDERER_CELL;
    int64_t edge = south < 0 ? -south : south;
    int64_t n = north < 0 ? -north : north;
    if (n > edge) edge = n;
    if (edge > VERDERER_LAT_MAX) edge = VERDERER_LAT_MAX;
    int64_t deg = (edge + 999999) / 1000000;
    for (uint32_t i = 0; i < 11u; i++)
        if (deg <= upto[i]) return VERDERER_CELL * times[i];
    return VERDERER_CELL_MAX;
}

/* VR3 */
static inline bool verderer_share_ok(int64_t latoff, int64_t lonoff, int64_t cell)
{
    return latoff == 0 && lonoff == 0 && cell >= VERDERER_CELL && cell <= VERDERER_CELL_MAX;
}

/* VR4 */
static inline bool verderer_reserve_visit_ok(int64_t partyroom, int64_t distroom, int64_t drone)
{
    return partyroom >= 0 && partyroom <= VERDERER_PARTY_MAX && distroom >= 0 &&
           distroom <= VERDERER_DIST_MAX && drone == 0;
}

/* VR4: the rooms out of the area's limits; false when a value is no count */
static inline bool verderer_visit_rooms(int64_t party, int64_t party_max, int64_t distance,
                                        int64_t distance_min, int64_t room[2])
{
    if (!room || party < 1 || party_max < 1 || party_max > VERDERER_PARTY_MAX || distance < 0 ||
        distance > VERDERER_DIST_MAX || distance_min < 0 || distance_min > VERDERER_DIST_MAX)
        return false;
    room[0] = party_max - party;
    room[1] = distance - distance_min;
    return true;
}

/* VR5: the number of the cell a point lies in, or VERDERER_REFUSED */
static inline int64_t verderer_cell_of(int64_t lat, int64_t lon)
{
    int64_t w = verderer_lon_cell(lat);
    if (w == VERDERER_REFUSED || lon < -VERDERER_LON_MAX || lon > VERDERER_LON_MAX)
        return VERDERER_REFUSED;
    int64_t row = verderer_floor_div(lat, VERDERER_CELL) + 1000;           /* 0 .. 2000 */
    int64_t col = verderer_floor_div(lon, w) * (w / VERDERER_CELL) + 2000; /* 0 .. 4000 */
    return row * 4001 + col;
}

/* VR5 */
static inline int64_t verderer_grid_count(const int64_t *species, const int64_t *cell, uint32_t n,
                                          uint32_t i)
{
    int64_t c = 0;
    if (!species || !cell || i >= n) return VERDERER_REFUSED;
    for (uint32_t j = 0; j < n; j++) c += species[j] == species[i] && cell[j] == cell[i];
    return c;
}

/* |v| has at most `digits` decimal digits (MYRA's iN) */
static inline bool verderer_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

/* VR6 */
static inline bool verderer_tree_ok(int64_t kingdom, int64_t family, int64_t genus, int64_t species,
                                    int64_t protected_)
{
    return verderer_digits(kingdom, 1) && verderer_digits(family, 9) && verderer_digits(genus, 9) &&
           verderer_digits(species, 9) && verderer_digits(protected_, 1);
}

/* VR7: what a sighting is, joined from the observer's word (TRUE: it is the
 * species named) and the key's (the same species TRUE, another FALSE; 0
 * not asked): BOTH when they name two species, kept until an expert
 * confirms */
static inline lpres_state_t verderer_identity(int64_t observer, int64_t key)
{
    lpres_state_t s = LPRES_STATE_TRUE;
    if (key) s = lpres_kjoin(s, key == observer ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    return s;
}

/* waymark.place_ok */
static inline bool verderer_place_ok(int64_t lat, int64_t lon)
{
    return lat >= -VERDERER_LAT_MAX && lat <= VERDERER_LAT_MAX && lon >= -VERDERER_LON_MAX &&
           lon <= VERDERER_LON_MAX;
}

/* weatherglass.forecast_ok: ^ZXWEATHER(stamp) = temp ^ wind ^ rain */
static inline bool verderer_reading_ok(int64_t stamp, int64_t temp, int64_t wind, int64_t rain)
{
    return verderer_digits(stamp, 11) && verderer_digits(temp, 4) && verderer_digits(wind, 4) &&
           verderer_digits(rain, 5);
}

#endif
