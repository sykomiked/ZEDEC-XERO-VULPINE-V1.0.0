/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pinhole_rules.h — Pinhole's rules in C, the reference its Eve, Forth
 * (twin Cairndrake), KRON and MYRA blocks are checked against
 * (blocks/pinhole), and the C of the rules it uses from Lifeboat,
 * Furrow and Gatehouse. Pure integer functions, no state: the model
 * (pinhole_model.c) and the language test (tests/pinhole_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. (Homestead's total_by_key says the same as PH5, but Homestead is
 * built after this layer, so Furrow's range_total is used: an album asked
 * about as the range album..album.)
 *
 *   PH1 CAMERA READY (camera_ready). sensor (a sensor is present), mode
 *       (the mode asked for is supported) and buffer (the frame fits the
 *       capture buffer) are flags; the camera is ready when each is
 *       exactly 1. A flag that is no flag (not 0 or 1) is never read as
 *       yes: not ready.
 *   PH2 THE SAME AS A NUMBER (camera_ready_n): 1 ready, 0 not, and
 *       PINHOLE_REFUSED for a flag that is no flag.
 *   PH3 EXPOSURE LIMITS (exposure_ok). iso 25..25600, shutter (how long
 *       the exposure lasts, in microseconds) 31..30000000 (1/32000 s to
 *       30 s) and comp (exposure compensation, in thirds of a stop)
 *       -9..9, each held to its own range with nothing relating them.
 *   PH4 A PHOTO FITS (lifeboat.restore_fits, used): the photo's bytes,
 *       with the owner's floor kept free, at most what the owner may
 *       still write (sys.disk avail).
 *   PH5 BYTES PER ALBUM (furrow.range_total, used): the exact sum of the
 *       bytes of the photos whose album is the one asked for (each photo's
 *       album as its position, the range want..want), added from the
 *       first; a partial sum past 64 bits is refused. At most
 *       PINHOLE_ROWS_MAX photos at once.
 *   PH6 A PLACE TAG (gatehouse.use_ok, used): asked (Pinhole needs
 *       "location": 1), granted (place.ask answered allowed) and not
 *       revoked (the owner did not say never). Anything else: no tag.
 *   PH7 A PHOTO'S RECORD (photo_record_ok) fits ^ZXPINHOLE: photo at most
 *       9 digits, album 4, bytes 12, width and height 5 each, placed 1
 *       and stamp (ms since 1970) 15, sign aside. */
#ifndef PINHOLE_RULES_H
#define PINHOLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "gatehouse_rules.h"
#include "furrow_rules.h"
#include "lifeboat_rules.h"

#define PINHOLE_REFUSED     (-1)
#define PINHOLE_ISO_MIN     25
#define PINHOLE_ISO_MAX     25600
#define PINHOLE_SHUTTER_MIN 31       /* microseconds: 1/32000 s */
#define PINHOLE_SHUTTER_MAX 30000000 /* 30 s */
#define PINHOLE_COMP_MAX    9        /* thirds of a stop, either way */
#define PINHOLE_ROWS_MAX    64u      /* PH5: photos summed at once */

static inline bool pinhole_flag(int64_t f)
{
    return f == 0 || f == 1;
}

/* PH1 */
static inline bool pinhole_camera_ready(int64_t sensor, int64_t mode, int64_t buffer)
{
    return sensor == 1 && mode == 1 && buffer == 1;
}

/* PH2 */
static inline int64_t pinhole_camera_ready_n(int64_t sensor, int64_t mode, int64_t buffer)
{
    if (!pinhole_flag(sensor) || !pinhole_flag(mode) || !pinhole_flag(buffer))
        return PINHOLE_REFUSED;
    return pinhole_camera_ready(sensor, mode, buffer) ? 1 : 0;
}

/* PH3 */
static inline bool pinhole_exposure_ok(int64_t iso, int64_t shutter, int64_t comp)
{
    return iso >= PINHOLE_ISO_MIN && iso <= PINHOLE_ISO_MAX && shutter >= PINHOLE_SHUTTER_MIN &&
           shutter <= PINHOLE_SHUTTER_MAX && comp >= -PINHOLE_COMP_MAX && comp <= PINHOLE_COMP_MAX;
}

/* PH4: need bytes, avail what may still be written, floor the bytes kept free */
static inline bool pinhole_photo_fits(int64_t need, int64_t avail, int64_t floor)
{
    return lifeboat_restore_fits(need, avail, floor);
}

/* PH5: 0 and the total in *total, or PINHOLE_REFUSED past 64 bits */
static inline int pinhole_album_total(const int64_t *album, const int64_t *bytes, uint32_t n,
                                      int64_t want, int64_t *total)
{
    static int64_t range[PINHOLE_ROWS_MAX];
    if (!total || n > PINHOLE_ROWS_MAX) return PINHOLE_REFUSED;
    for (uint32_t i = 0; i < n; i++) range[i] = want;
    return furrow_range_total(album, range, range, bytes, n, total) ? 0 : PINHOLE_REFUSED;
}

/* PH6 */
static inline bool pinhole_place_ok(int64_t asked, int64_t granted, int64_t revoked)
{
    return gatehouse_use_ok(asked, granted, revoked);
}

/* |v| has at most `digits` decimal digits (MYRA's iN) */
static inline bool pinhole_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

/* PH7 */
static inline bool pinhole_record_ok(int64_t photo, int64_t album, int64_t bytes, int64_t width,
                                     int64_t height, int64_t placed, int64_t stamp)
{
    return pinhole_digits(photo, 9) && pinhole_digits(album, 4) && pinhole_digits(bytes, 12) &&
           pinhole_digits(width, 5) && pinhole_digits(height, 5) && pinhole_digits(placed, 1) &&
           pinhole_digits(stamp, 15);
}

#endif
