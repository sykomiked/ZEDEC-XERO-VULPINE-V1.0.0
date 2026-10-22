/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chainman_model.h — Chainman, land survey, as a model (zxm.h): parcels
 * walked as traverses of measured legs from a start corner, curved
 * boundaries flattened, each parcel's area, closure and title register
 * line, the grid cells it claims, and its corners kept as places on the
 * map. No drawing, no allocation, integers only. The rules it runs are
 * chainman_rules.h (CH1-CH7).
 *
 * zxvd methods used: files.read, files.write, files.mkdir, place.here,
 * place.add and place.list.
 *
 *   CM1 THE FIELD BOOK is Chainman/fieldbook.txt, one record a line, its
 *       fields tab-separated, a letter first: "S survey"; "P parcel kind
 *       title x0 y0 tol" (kind 1 built, 2 garden, 3 crop, 4 pasture, 5
 *       wild, Understory's; title 0 until one is registered; the start
 *       corner in mm; tol in mm per 100 m); "L parcel de dn len steps c1x
 *       c1y c2x c2y" (a leg's run east and north and its taped length in
 *       mm; steps 0 for a straight leg, else a curve of that many chords
 *       with its two control points relative to the leg's start); "K parcel
 *       corner lat lon" (a corner marked where the machine stood, in
 *       millionths of a degree); "C cx cy parcel" (a grid cell claimed).
 *       Every line is checked on load and before the book is written
 *       whole; a line that does not fit is refused. No book is an empty
 *       one.
 *   CM2 THE OUTLINE. A parcel's corners are its start and the end of each
 *       leg but the last, which returns to the start (its misclosure is
 *       CM3's); a curved leg adds its flattened points (burin.bezier_at,
 *       step 1 .. steps - 1) before its end. Every corner stays inside
 *       gnomon's domain (1 km either way of the survey's origin), every
 *       one fits ^ZXCHAIN (CH5), and the outline's path record fits
 *       burin.drawing_record_ok.
 *   CM3 AREA AND CLOSURE. The area is gnomon.polygon_area2 of the outline,
 *       and the square metres its size halved, floored. The misclosure
 *       east and north and the perimeter are field book columns summed
 *       with furrow.range_total (each leg's parcel its position, the
 *       parcel asked its range); the perimeter in whole metres, floored.
 *       It closes when CH2 holds of CH1's slack: TRUE or FALSE, NEITHER
 *       when it has no legs or a sum is outside CH1's domain.
 *   CM4 CORNERS ON THE MAP. chainman_mark asks place.here; a corner is
 *       marked only where the location is known and waymark.place_ok
 *       holds, kept as the place "Chainman <survey>-<parcel>-<corner>"
 *       (place.add) and in the field book; chainman_places counts
 *       Chainman's places on the map (place.list).
 *   CM5 TITLE REGISTER. chainman_title files a title number for a parcel,
 *       on the owner's confirmed word, when the line holds CH3 and the
 *       parcel as a land entry holds understory.land_entry_ok (and its
 *       meaning, understory_land_ok). Advisory: the title is the land
 *       registry's.
 *   CM6 THE GRID. chainman_claim_cells claims every cell whose centre lies
 *       inside the parcel's outline (CH7), at most CHAINMAN_SCAN cells a
 *       side; a cell a parcel already claims is not claimed again, and a
 *       claim never replaces another's.
 *   CM7 A DISPUTED CELL. chainman_cell is CH6 over every claim: NEITHER
 *       unclaimed, TRUE held by one parcel, BOTH when two parcels or more
 *       claim it; both claims stay side by side, and neither parcel's
 *       record changes. */
#ifndef CHAINMAN_MODEL_H
#define CHAINMAN_MODEL_H

#include "chainman_rules.h"
#include "zxm.h"

#define CHAINMAN_DIR     "Chainman"
#define CHAINMAN_BOOK    "Chainman/fieldbook.txt"
#define CHAINMAN_PARCELS 16u
#define CHAINMAN_LEGS    96u
#define CHAINMAN_CLAIMS  256u
#define CHAINMAN_MARKS   64u
#define CHAINMAN_VERTS   512u             /* corners of one outline (the path record's 3 digits) */
#define CHAINMAN_SCAN    64               /* cells a side scanned for one parcel (CM6) */
#define CHAINMAN_LEN_MAX INT64_C(3000000) /* a leg's taped length, mm */
#define CHAINMAN_NOTICE  200u

typedef struct {
    int64_t parcel, kind, title, x0, y0, tol;
} chainman_parcel_t;

typedef struct {
    int64_t parcel, de, dn, len, steps, c1x, c1y, c2x, c2y;
} chainman_leg_t;

typedef struct {
    int64_t cx, cy, parcel;
} chainman_claim_t;

typedef struct {
    int64_t parcel, corner, lat, lon;
} chainman_mark_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    int64_t survey;
    chainman_parcel_t parcel[CHAINMAN_PARCELS];
    uint32_t nparcels;
    chainman_leg_t leg[CHAINMAN_LEGS];
    uint32_t nlegs;
    chainman_claim_t claim[CHAINMAN_CLAIMS];
    uint32_t nclaims;
    chainman_mark_t mark[CHAINMAN_MARKS];
    uint32_t nmarks;
    uint32_t nplaces; /* Chainman's places on the map (CM4) */
    char notice[CHAINMAN_NOTICE];
} chainman_t;

/* survey 1..999999 */
void chainman_init(chainman_t *c, zxm_link_t *link, int64_t survey);
/* CM1 */
int chainman_load(chainman_t *c);
int chainman_add_parcel(chainman_t *c, int64_t parcel, int64_t kind, int64_t x0, int64_t y0,
                        int64_t tol);
int chainman_add_leg(chainman_t *c, int64_t parcel, int64_t de, int64_t dn, int64_t len);
int chainman_add_curve(chainman_t *c, int64_t parcel, int64_t de, int64_t dn, int64_t len,
                       int64_t c1x, int64_t c1y, int64_t c2x, int64_t c2y, int64_t steps);
/* CM2: the outline's corners into x, y (cap each), *n of them */
int chainman_outline(const chainman_t *c, int64_t parcel, int64_t *x, int64_t *y, uint32_t cap,
                     uint32_t *n);
bool chainman_path_record_ok(const chainman_t *c, int64_t parcel, int64_t mtime);
/* CM3 */
int chainman_area2(const chainman_t *c, int64_t parcel, int64_t *area2);
int chainman_area_m2(const chainman_t *c, int64_t parcel, int64_t *m2);
int chainman_closure(const chainman_t *c, int64_t parcel, int64_t *dx, int64_t *dy, int64_t *perim);
lpres_state_t chainman_closes(const chainman_t *c, int64_t parcel);
/* CM4 */
int chainman_mark(chainman_t *c, int64_t parcel, int64_t corner);
int chainman_places(chainman_t *c);
/* CM5 */
int chainman_title(chainman_t *c, int64_t parcel, int64_t title, bool confirmed);
/* CM6 */
int chainman_claim_cells(chainman_t *c, int64_t parcel, uint32_t *added);
/* CM7 */
lpres_state_t chainman_cell(const chainman_t *c, int64_t cx, int64_t cy);
const chainman_parcel_t *chainman_find(const chainman_t *c, int64_t parcel);

#endif
