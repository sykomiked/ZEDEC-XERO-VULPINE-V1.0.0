/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glimpse_rules.h — Glimpse's rules in C, the reference its KRON and
 * Trebago blocks are checked against (blocks/glimpse), and the C of the
 * rules it uses from Hollow and Lifeboat. Pure integer functions, no
 * state: the model (glimpse_model.c) and the language test
 * (tests/glimpse_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   GR1 CAPTURE ORIGIN (capture_origin). Where a capture region starts
 *       along one side of the screen, clamped to it: min(max(pos, 0),
 *       span), so it is never negative and never past the side. pos is
 *       -99999..99999 pixels (HOLLOW_PIC_MAX, Sutra's picture in
 *       hollow.shown) and span, the screen's side, 1..16384
 *       (HOLLOW_PX_MAX); anything else is refused (GLIMPSE_REFUSED).
 *   GR2 CAPTURE EXTENT (hollow.shown, used). How many pixels of the region
 *       along that side lie on the screen: max(0, min(pos + size, span) -
 *       max(pos, 0)), never negative. With GR1 the region x, y, w, h is
 *       clamped inside width and height.
 *   GR3 RECORDING SIZE (recording_bytes). The exact raw size of a
 *       recording: w * h * bpp * secs * fps bytes (w, h 1..16384 pixels,
 *       bpp 1..8 bytes per pixel, secs 1..7200, two hours, and fps
 *       1..60), when it is at most room (0..999999999999999999 bytes, the
 *       room sys.disk reports less the owner's floor); -1 when it is past
 *       the room. Any input outside its range is refused (GLIMPSE_REFUSED,
 *       the same -1: the model tells the two apart before asking).
 *   GR4 FIRST FREE NUMBER (first_free_n). The smallest N of 1 or more that
 *       is not among the used numbers (1..GLIMPSE_USED_MAX of them, in any
 *       order, repeats allowed); a number below 1 holds no place, so a
 *       list of one 0 is an empty folder and answers 1.
 *   GR5 A SCREENSHOT FITS (lifeboat.restore_fits, used): its bytes, with
 *       the owner's floor kept free, at most what may still be written
 *       (sys.disk avail). */
#ifndef GLIMPSE_RULES_H
#define GLIMPSE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "hollow_rules.h"
#include "lifeboat_rules.h"

#define GLIMPSE_REFUSED  (-1)
#define GLIMPSE_SIDE_MAX HOLLOW_PX_MAX  /* the widest screen side, pixels */
#define GLIMPSE_POS_MAX  HOLLOW_PIC_MAX /* a region's corner, either way */
#define GLIMPSE_BPP_MAX  8              /* bytes per pixel */
#define GLIMPSE_SECS_MAX 7200           /* two hours */
#define GLIMPSE_FPS_MAX  60
#define GLIMPSE_ROOM_MAX LIFEBOAT_BYTES_MAX
#define GLIMPSE_USED_MAX 512u /* numbers read from one listing */

/* GR1 */
static inline int64_t glimpse_capture_origin(int64_t pos, int64_t span)
{
    if (pos < -GLIMPSE_POS_MAX || pos > GLIMPSE_POS_MAX || span < 1 || span > GLIMPSE_SIDE_MAX)
        return GLIMPSE_REFUSED;
    int64_t o = pos > 0 ? pos : 0;
    return o < span ? o : span;
}

/* GR2 */
static inline int64_t glimpse_capture_extent(int64_t pos, int64_t size, int64_t span)
{
    return hollow_shown(pos, size, span);
}

/* GR3 */
static inline int64_t glimpse_recording_bytes(int64_t w, int64_t h, int64_t bpp, int64_t secs,
                                              int64_t fps, int64_t room)
{
    if (w < 1 || w > GLIMPSE_SIDE_MAX || h < 1 || h > GLIMPSE_SIDE_MAX || bpp < 1 ||
        bpp > GLIMPSE_BPP_MAX || secs < 1 || secs > GLIMPSE_SECS_MAX || fps < 1 ||
        fps > GLIMPSE_FPS_MAX || room < 0 || room > GLIMPSE_ROOM_MAX)
        return GLIMPSE_REFUSED;
    int64_t raw = w * h * bpp * secs * fps; /* at most 16384^2 * 8 * 432000, about 9.3e14 */
    return raw <= room ? raw : GLIMPSE_REFUSED;
}

/* GR4: the first free N, or GLIMPSE_REFUSED for no list or one too long */
static inline int64_t glimpse_first_free(const int64_t *used, uint32_t n)
{
    if (!used || n < 1u || n > GLIMPSE_USED_MAX) return GLIMPSE_REFUSED;
    for (int64_t c = 1; c <= (int64_t) n + 1; c++) {
        bool taken = false;
        for (uint32_t i = 0; i < n && !taken; i++) taken = used[i] == c;
        if (!taken) return c;
    }
    return GLIMPSE_REFUSED; /* never reached: n numbers cannot fill 1..n+1 */
}

/* GR5: need bytes, avail what may still be written, floor the bytes kept free */
static inline bool glimpse_shot_fits(int64_t need, int64_t avail, int64_t floor)
{
    return lifeboat_restore_fits(need, avail, floor);
}

#endif
