/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kinetoscope_rules.h — Kinetoscope's rules in C, the reference its KRON,
 * C++, Trebago, Eve and MYRA blocks are checked against
 * (blocks/kinetoscope), with the rules it uses from nearer the core. Pure
 * integer functions, no state: the model (kinetoscope_model.c) and the
 * language test (tests/kinetoscope_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   KR1 FIT WIDTH (video_fit_w). The width of a video of vidw by vidh
 *       pixels fitted whole inside a window of winw by winh, its aspect
 *       kept: min(winw, floor(winh * vidw / vidh)). vidw and vidh
 *       1..65535, winw and winh 0..65535, else refused.
 *   KR2 FIT HEIGHT (video_fit_h). Its height: min(winh, floor(winw * vidh
 *       / vidw)), the same domain. One of the two always fills its side
 *       of the window; the other leaves bars, half before the picture
 *       (the odd pixel after it).
 *   KR3 FRAME AT A POSITION (frame_at_pos). The frame a position of pos ms
 *       falls in, in a video of fnum / fden frames a second: pos * fnum /
 *       (1000 * fden) as an exact reduced rational (num, den); the frame
 *       shown is its floor. pos 0..359999999 (under 100 hours), fnum and
 *       fden 1..999999 each, else refused.
 *   KR4 HALL RATE (hall_rate_ok). A watch hall's playing rate, q16
 *       (65536 the normal speed): 1..262144, up to four times, the range
 *       zxvd's hall keeps (zxvd_pspace.c). Outside is refused, never sent.
 *   KR5 RESUME RECORD (resume_row_ok) fits ^ZXKINETOSCOPE(video) = pos
 *       ^ length ^ stamp: video at most 4 digits, pos and length 9, stamp
 *       (seconds since 1970) 11, each sign aside (MYRA's widths).
 *   KR6 SEEK POINTS: the segments of a plan that start on a keyframe,
 *       counted with witness.kind_count (witness_rules.h WR2), never
 *       written again.
 *   KR7 POSITION SHOWN: a position or a length as h:mm:ss is halloo.hms
 *       (halloo_rules.h HR4), never written again. */
#ifndef KINETOSCOPE_RULES_H
#define KINETOSCOPE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "halloo_rules.h"
#include "witness_rules.h"

#define KINETOSCOPE_REFUSED   (-1)
#define KINETOSCOPE_SIDE_MAX  INT64_C(65535)
#define KINETOSCOPE_POS_MAX   INT64_C(359999999)   /* KR3: under 100 hours, in ms */
#define KINETOSCOPE_FRATE_MAX INT64_C(999999)      /* KR3: fnum and fden */
#define KINETOSCOPE_RATE_UNIT INT64_C(65536)       /* KR4: normal speed */
#define KINETOSCOPE_RATE_MAX  INT64_C(262144)      /* KR4: four times */
#define KINETOSCOPE_VIDEO_MAX INT64_C(9999)        /* KR5: i4 */
#define KINETOSCOPE_MS_MAX    INT64_C(999999999)   /* KR5: i9 */
#define KINETOSCOPE_STAMP_MAX INT64_C(99999999999) /* KR5: i11 */

static inline bool kinetoscope_fit_domain(int64_t vidw, int64_t vidh, int64_t winw, int64_t winh)
{
    return vidw >= 1 && vidw <= KINETOSCOPE_SIDE_MAX && vidh >= 1 && vidh <= KINETOSCOPE_SIDE_MAX &&
           winw >= 0 && winw <= KINETOSCOPE_SIDE_MAX && winh >= 0 && winh <= KINETOSCOPE_SIDE_MAX;
}

/* KR1 */
static inline int64_t kinetoscope_fit_w(int64_t vidw, int64_t vidh, int64_t winw, int64_t winh)
{
    if (!kinetoscope_fit_domain(vidw, vidh, winw, winh)) return KINETOSCOPE_REFUSED;
    int64_t w = winh * vidw / vidh;
    return w < winw ? w : winw;
}

/* KR2 */
static inline int64_t kinetoscope_fit_h(int64_t vidw, int64_t vidh, int64_t winw, int64_t winh)
{
    if (!kinetoscope_fit_domain(vidw, vidh, winw, winh)) return KINETOSCOPE_REFUSED;
    int64_t h = winw * vidh / vidw;
    return h < winh ? h : winh;
}

static inline int64_t kinetoscope_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* KR3: false when refused; else *num / *den reduced, den above 0 */
static inline bool kinetoscope_frame_at(int64_t pos, int64_t fnum, int64_t fden, int64_t *num,
                                        int64_t *den)
{
    if (!num || !den || pos < 0 || pos > KINETOSCOPE_POS_MAX || fnum < 1 ||
        fnum > KINETOSCOPE_FRATE_MAX || fden < 1 || fden > KINETOSCOPE_FRATE_MAX)
        return false;
    int64_t n = pos * fnum, d = 1000 * fden, g = kinetoscope_gcd(n, d);
    *num = n / g;
    *den = d / g;
    return true;
}

/* KR3: the frame shown (the floor), or KINETOSCOPE_REFUSED */
static inline int64_t kinetoscope_frame_index(int64_t pos, int64_t fnum, int64_t fden)
{
    int64_t n = 0, d = 1;
    if (!kinetoscope_frame_at(pos, fnum, fden, &n, &d)) return KINETOSCOPE_REFUSED;
    return n / d;
}

/* KR4 */
static inline bool kinetoscope_hall_rate_ok(int64_t rate)
{
    return rate >= 1 && rate <= KINETOSCOPE_RATE_MAX;
}

/* KR5 */
static inline bool kinetoscope_resume_row_ok(int64_t video, int64_t pos, int64_t length,
                                             int64_t stamp)
{
    return witness_digits_fit(video, 4) && witness_digits_fit(pos, 9) &&
           witness_digits_fit(length, 9) && witness_digits_fit(stamp, 11);
}

/* KR6: how many of n segments start on a keyframe (key 1, else 0) */
static inline int64_t kinetoscope_seek_points(const int64_t *key, uint32_t n)
{
    return witness_kind_count(key, n, 1);
}

/* KR7: h * 10000 + m * 100 + s, or HALLOO_REFUSED */
static inline int64_t kinetoscope_hms(int64_t ms)
{
    return halloo_hms(ms);
}

#endif
