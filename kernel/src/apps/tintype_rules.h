/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tintype_rules.h — Tintype's rules in C, the reference its Trebago, KRON,
 * Eve, Sutra and MYRA blocks (blocks/tintype) are checked against, beside
 * the C of the entries it composes from Glint, Gesso, Pinhole, Glimpse and
 * Lifeboat (their own rules headers). Pure integer functions, no state:
 * the model (tintype_model.c) and the language test (tests/tintype_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   TR1 EXPOSURE VALUE (exposure_ev_thirds). The exposure value at ISO
 *       100 in thirds of a stop: av + tv - sv. Each is the place of its
 *       input on an integer log2 table of nominal third stops, the largest
 *       entry at most the input: av the aperture's (tenths of an f-number,
 *       f/1 10 .. f/32 320; f/1 is 0, f/2 6, f/16 24), tv the shutter's
 *       (microseconds, 1/32000 s 31 .. 30 s 30000000; 1 s is 0, each third
 *       faster one more, so 1/125 s is 21 and 30 s is -15) and sv the
 *       ISO's (25 .. 25600; ISO 100 is 0, 400 is 6). An input outside its
 *       table is refused (TINTYPE_EV_REFUSED). f/16 at 1/125 s and ISO 100
 *       is 45, EV 15.
 *   TR2 POSSIBLY ALTERED (photo_decode_ok). A photo decodes to what its
 *       record says when the recorded width and height less the decoded
 *       ones are 0, the recorded time less the decoded time (seconds) is
 *       within the camera clock's second either way, and the content id
 *       the file hashes to is the recorded one (cidsame exactly 1); else it
 *       is possibly altered. Four-valued (tintype_integrity): the record
 *       asserts each field (TRUE) and the decode confirms it (TRUE),
 *       contradicts it (FALSE) or cannot read it (NEITHER, a time where the
 *       file holds none); the two are joined field by field (lpres_kjoin)
 *       and the fields conjoined (lpres_conjoin), so a photo recorded 4000
 *       by 3000 that decodes to 3990 by 3000 is BOTH: possibly altered,
 *       kept with both claims until the owner decides.
 *   TR3 RIGHTS REGISTER (rights_record_ok). A row is a photo
 *       1..999999999, an owner 1..999999999, a licence code 0..7
 *       (TINTYPE_LICENCE_*) and a release flag 0 or 1.
 *   TR4 STORAGE LIMIT (album_storage_ok). An album holds at most the bytes
 *       its owner declared: room = declared - used - adding is 0 or more
 *       (and at most 15 digits), declared 1..TINTYPE_BYTES_MAX, each field
 *       on its own range.
 *   TR5 DEVELOP CURVE (develop_curve_px). A tone curve through five knots,
 *       the outputs at levels 0, 64, 128, 192 and 255: a channel value px
 *       in segment s = min(3, floor(px / 64)) takes lo + floor((hi - lo) *
 *       (px - 64 * s) / w), lo and hi the knots at either end and w 64 (63
 *       for the last segment), the floor toward minus infinity. px and
 *       every knot 0..255, anything else refused (TINTYPE_REFUSED).
 *   TR6 ALBUM TREE (album_node_ok). ^ZXTINTYPE(parent, album) =
 *       declared ^ used ^ photos: parent and album at most 4 digits,
 *       declared and used 15, photos 5, sign aside. That an album's parent
 *       exists, and is not the album itself, is the model's.
 *
 * Composed, the C beside the entry (the part's rules header):
 *   glint.item_ok           ^ZXGLINT(folder, seq) = kind ^ size: folder
 *                           and seq 9 digits, kind 3, size 18 (Glint keeps
 *                           no rules header, so tintype_item_fits is its
 *                           record's C)
 *   gesso.pixel_bright, gesso.pixel_contrast, gesso.pixel_range_count
 *   pinhole.camera_ready, pinhole.exposure_ok, pinhole.photo_record_ok
 *   glimpse.capture_origin, glimpse.recording_bytes, glimpse.first_free_n
 *   lifeboat.restore_fits   "photo fits" (Pinhole's PH4, Lifeboat's rule) */
#ifndef TINTYPE_RULES_H
#define TINTYPE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "gesso_rules.h"
#include "glimpse_rules.h"
#include "lifeboat_rules.h"
#include "lpres.h"
#include "pinhole_rules.h"

#define TINTYPE_REFUSED     (-1)
#define TINTYPE_EV_REFUSED  (-1000) /* TR1: outside -39..105, never an EV */
#define TINTYPE_AP_MIN      10      /* TR1: f/1, tenths */
#define TINTYPE_AP_MAX      320     /* TR1: f/32 */
#define TINTYPE_SH_MIN      PINHOLE_SHUTTER_MIN
#define TINTYPE_SH_MAX      PINHOLE_SHUTTER_MAX
#define TINTYPE_ISO_MIN     PINHOLE_ISO_MIN
#define TINTYPE_ISO_MAX     PINHOLE_ISO_MAX
#define TINTYPE_TV_AT_1S    45 /* TR1: 1 s's place on the shutter table */
#define TINTYPE_SV_AT_100   6  /* TR1: ISO 100's place on the ISO table */
#define TINTYPE_CLOCK_SLACK 1  /* TR2: seconds either way */
#define TINTYPE_ID_MAX      999999999
#define TINTYPE_LICENCE_MAX 7
#define TINTYPE_BYTES_MAX   INT64_C(999999999999999) /* TR4: 15 digits */
#define TINTYPE_KNOTS       5u
#define TINTYPE_KNOT_STEP   64
#define TINTYPE_ALBUM_MAX   9999 /* TR6: 4 digits */

/* what a catalogued item is, its kind in glint.item_ok */
enum { TINTYPE_KIND_PHOTO = 1, TINTYPE_KIND_SCREEN = 2 };

/* TR3: licence codes */
enum {
    TINTYPE_LICENCE_RESERVED = 0, /* all rights reserved */
    TINTYPE_LICENCE_PUBLIC = 1,   /* a public domain dedication */
    TINTYPE_LICENCE_BY = 2,       /* attribution */
    TINTYPE_LICENCE_BY_SA = 3,    /* attribution, share alike */
    TINTYPE_LICENCE_BY_ND = 4,    /* attribution, no derivatives */
    TINTYPE_LICENCE_BY_NC = 5,    /* attribution, non-commercial */
    TINTYPE_LICENCE_NC_SA = 6,    /* non-commercial, share alike */
    TINTYPE_LICENCE_NC_ND = 7,    /* non-commercial, no derivatives */
};

/* TR1: the place of x on an ascending table: the entries at most x, less 1 */
static inline int64_t tintype_place(const int64_t *t, uint32_t n, int64_t x)
{
    int64_t c = 0;
    for (uint32_t i = 0; i < n; i++) c += t[i] <= x;
    return c - 1;
}

/* TR1 */
static inline int64_t tintype_ev_thirds(int64_t aperture, int64_t shutter, int64_t iso)
{
    static const int64_t ap[31] = {10,  11,  12,  14,  16,  18,  20,  22,  25, 28,  32,
                                   35,  40,  45,  50,  56,  63,  71,  80,  90, 100, 110,
                                   130, 140, 160, 180, 200, 220, 250, 290, 320};
    static const int64_t sh[61] = {
        31,      39,       50,       62,       78,       100,      125,     156,     200,
        250,     312,      400,      500,      625,      800,      1000,    1250,    1562,
        2000,    2500,     3125,     4000,     5000,     6250,     8000,    10000,   12500,
        16666,   20000,    25000,    33333,    40000,    50000,    66666,   76923,   100000,
        125000,  166666,   200000,   250000,   333333,   400000,   500000,  625000,  769230,
        1000000, 1300000,  1600000,  2000000,  2500000,  3200000,  4000000, 5000000, 6000000,
        8000000, 10000000, 13000000, 15000000, 20000000, 25000000, 30000000};
    static const int64_t is[31] = {25,   32,   40,    50,    64,    80,    100,  125,
                                   160,  200,  250,   320,   400,   500,   640,  800,
                                   1000, 1250, 1600,  2000,  2500,  3200,  4000, 5000,
                                   6400, 8000, 10000, 12800, 16000, 20000, 25600};
    if (aperture < TINTYPE_AP_MIN || aperture > TINTYPE_AP_MAX || shutter < TINTYPE_SH_MIN ||
        shutter > TINTYPE_SH_MAX || iso < TINTYPE_ISO_MIN || iso > TINTYPE_ISO_MAX)
        return TINTYPE_EV_REFUSED;
    int64_t av = tintype_place(ap, 31u, aperture);
    int64_t tv = TINTYPE_TV_AT_1S - tintype_place(sh, 61u, shutter);
    int64_t sv = tintype_place(is, 31u, iso) - TINTYPE_SV_AT_100;
    return av + tv - sv;
}

/* TR2 */
static inline bool tintype_decode_match(int64_t wgap, int64_t hgap, int64_t tgap, int64_t cidsame)
{
    return wgap == 0 && hgap == 0 && tgap >= -TINTYPE_CLOCK_SLACK && tgap <= TINTYPE_CLOCK_SLACK &&
           cidsame == 1;
}

/* TR2: one field, the record's claim joined with the decode's */
static inline lpres_state_t tintype_field(bool known, bool same)
{
    lpres_state_t decoded = !known ? LPRES_STATE_NEITHER
                            : same ? LPRES_STATE_TRUE
                                   : LPRES_STATE_FALSE;
    return lpres_kjoin(LPRES_STATE_TRUE, decoded);
}

/* TR2, four-valued: TRUE as recorded, BOTH possibly altered */
static inline lpres_state_t tintype_integrity(int64_t wgap, int64_t hgap, int64_t tgap, bool tknown,
                                              int64_t cidsame)
{
    lpres_state_t s = tintype_field(true, wgap == 0);
    s = lpres_conjoin(s, tintype_field(true, hgap == 0));
    s = lpres_conjoin(
        s, tintype_field(tknown, tgap >= -TINTYPE_CLOCK_SLACK && tgap <= TINTYPE_CLOCK_SLACK));
    return lpres_conjoin(s, tintype_field(true, cidsame == 1));
}

/* TR3 */
static inline bool tintype_rights_ok(int64_t photo, int64_t owner, int64_t licence, int64_t release)
{
    return photo >= 1 && photo <= TINTYPE_ID_MAX && owner >= 1 && owner <= TINTYPE_ID_MAX &&
           licence >= 0 && licence <= TINTYPE_LICENCE_MAX && (release == 0 || release == 1);
}

/* TR4: the room an album keeps after adding, or TINTYPE_REFUSED for an
 * input outside 0..TINTYPE_BYTES_MAX (declared 1..); below 0 is over */
static inline int64_t tintype_album_room(int64_t declared, int64_t used, int64_t adding)
{
    if (declared < 1 || declared > TINTYPE_BYTES_MAX || used < 0 || used > TINTYPE_BYTES_MAX ||
        adding < 0 || adding > TINTYPE_BYTES_MAX)
        return TINTYPE_REFUSED;
    return declared - used - adding;
}

/* TR4 */
static inline bool tintype_storage_ok(int64_t room, int64_t declared)
{
    return room >= 0 && room <= TINTYPE_BYTES_MAX && declared >= 1 && declared <= TINTYPE_BYTES_MAX;
}

/* TR5 */
static inline int64_t tintype_curve_px(int64_t px, const int64_t *curve)
{
    if (!curve || px < 0 || px > GESSO_PX_MAX) return TINTYPE_REFUSED;
    for (uint32_t k = 0; k < TINTYPE_KNOTS; k++)
        if (curve[k] < 0 || curve[k] > GESSO_PX_MAX) return TINTYPE_REFUSED;
    int64_t s = px / TINTYPE_KNOT_STEP;
    if (s > 3) s = 3;
    int64_t lo = curve[s], hi = curve[s + 1],
            w = s == 3 ? TINTYPE_KNOT_STEP - 1 : TINTYPE_KNOT_STEP;
    int64_t p = (hi - lo) * (px - TINTYPE_KNOT_STEP * s);
    int64_t q = p / w;
    if (p % w != 0 && p < 0) q -= 1;
    return lo + q;
}

/* TR6 */
static inline bool tintype_album_node_ok(int64_t parent, int64_t album, int64_t declared,
                                         int64_t used, int64_t photos)
{
    return pinhole_digits(parent, 4) && pinhole_digits(album, 4) && pinhole_digits(declared, 15) &&
           pinhole_digits(used, 15) && pinhole_digits(photos, 5);
}

/* glint.item_ok's record, ^ZXGLINT(folder, seq) = kind ^ size */
static inline bool tintype_item_fits(int64_t folder, int64_t seq, int64_t kind, int64_t size)
{
    return pinhole_digits(folder, 9) && pinhole_digits(seq, 9) && pinhole_digits(kind, 3) &&
           pinhole_digits(size, 18);
}

#endif
