/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cutwater_rules.h — Cutwater's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/cutwater), with the
 * rule it uses from nearer the core. Pure integer functions, no state: the
 * model (cutwater_model.c) and the language test (tests/cutwater_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   CR1 A CLIP IS VALID (edit_clip_ok). A clip takes the frames in .. out
 *       - 1 of its source and lies on its track's timeline from place to
 *       place + (out - in). Given as three margins in frames: span = out -
 *       in (in before out) 1..999999999, tail = the source's frames - out
 *       (inside the source) 0..999999999 and room = the next clip's place
 *       on the same track - this clip's end (no overlap; for the last clip
 *       of a track, CUTWATER_FRAME_MAX - its end) 0..999999999. Each
 *       margin against its own range; anything else is no.
 *   CR2 TIMECODE (frame_timecode). A count of frames at fnum / fden frames
 *       a second as h:m:s:f, non-drop-frame at the nominal rate p =
 *       ceil(fnum / fden) (SMPTE ST 12-1: 30 for 30000/1001), written h *
 *       10000000 + m * 100000 + s * 1000 + f, hours not wrapped. frames
 *       0..999999999, fnum and fden 1..999999 and p at most 999, else
 *       refused. Drop-frame timecode is not computed.
 *   CR3 CLIP RECORD (edit_clip_row_ok) fits ^ZXCUTWATER(clip) = track ^
 *       source ^ cutin ^ cutout ^ place: clip at most 2 digits, track and
 *       source 1, the three frames 9, each sign aside (MYRA's widths).
 *   CR4 FRAME AT A POSITION: a playhead in ms on the edit list's timeline
 *       is the frame kinetoscope.frame_at_pos gives at the list's rate
 *       (kinetoscope_rules.h KR3), never written again. */
#ifndef CUTWATER_RULES_H
#define CUTWATER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "kinetoscope_rules.h"
#include "witness_rules.h"

#define CUTWATER_REFUSED   (-1)
#define CUTWATER_FRAME_MAX INT64_C(999999999) /* CR1, CR2: 9 digits of frames */
#define CUTWATER_RATE_MAX  INT64_C(999999)    /* CR2: fnum and fden */
#define CUTWATER_FPS_MAX   INT64_C(999)       /* CR2: three digits of f */

/* CR1 */
static inline bool cutwater_clip_ok(int64_t span, int64_t tail, int64_t room)
{
    return span >= 1 && span <= CUTWATER_FRAME_MAX && tail >= 0 && tail <= CUTWATER_FRAME_MAX &&
           room >= 0 && room <= CUTWATER_FRAME_MAX;
}

/* CR2: the nominal rate, ceil(fnum / fden), or CUTWATER_REFUSED */
static inline int64_t cutwater_fps(int64_t fnum, int64_t fden)
{
    if (fnum < 1 || fnum > CUTWATER_RATE_MAX || fden < 1 || fden > CUTWATER_RATE_MAX)
        return CUTWATER_REFUSED;
    int64_t p = (fnum + fden - 1) / fden;
    return p <= CUTWATER_FPS_MAX ? p : CUTWATER_REFUSED;
}

/* CR2 */
static inline int64_t cutwater_timecode(int64_t frames, int64_t fnum, int64_t fden)
{
    int64_t p = cutwater_fps(fnum, fden);
    if (p < 0 || frames < 0 || frames > CUTWATER_FRAME_MAX) return CUTWATER_REFUSED;
    int64_t t = frames / p, h = t / 3600, m = t % 3600 / 60, s = t % 60;
    return h * 10000000 + m * 100000 + s * 1000 + frames % p;
}

/* CR3 */
static inline bool cutwater_clip_row_ok(int64_t clip, int64_t track, int64_t source, int64_t cutin,
                                        int64_t cutout, int64_t place)
{
    return witness_digits_fit(clip, 2) && witness_digits_fit(track, 1) &&
           witness_digits_fit(source, 1) && witness_digits_fit(cutin, 9) &&
           witness_digits_fit(cutout, 9) && witness_digits_fit(place, 9);
}

/* CR4: the frame a playhead of ms falls in, or KINETOSCOPE_REFUSED */
static inline int64_t cutwater_frame_at_ms(int64_t ms, int64_t fnum, int64_t fden)
{
    return kinetoscope_frame_index(ms, fnum, fden);
}

#endif
