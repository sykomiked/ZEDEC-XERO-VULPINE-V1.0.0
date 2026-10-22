/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* footlight_rules.h — Footlight's rules in C, the reference its KRON, C++,
 * Trebago, BASIC and MYRA blocks are checked against (blocks/footlight),
 * with the rule it uses from nearer the core. Pure integer functions, no
 * state: the model (footlight_model.c) and the language test
 * (tests/footlight_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   FL1 SLIDE FIT WIDTH. The width of a 16:9 slide fitted whole inside a
 *       window of winw by winh pixels: min(winw, floor(16 * winh / 9)).
 *       winw and winh 0..FOOTLIGHT_SIDE_MAX, else refused.
 *   FL2 SLIDE FIT HEIGHT. Its height: min(winh, floor(9 * winw / 16)),
 *       the same domain. A window wider than 16:9 fills its height and is
 *       pillarboxed; a taller one fills its width and is letterboxed.
 *   FL3 LETTERBOX BAR. The bar before a fitted side (left of the width,
 *       above the height): floor((span - fit) / 2), the odd pixel after
 *       it. span 0..FOOTLIGHT_SIDE_MAX and fit 0..span, else refused.
 *   FL4 REHEARSAL OVER. A rehearsal's total against the talk's length:
 *       the sum of every slide's rehearsed seconds less the talk's
 *       seconds, above 0 over, below 0 under. 1..FOOTLIGHT_DECK_MAX
 *       slides of 1..FOOTLIGHT_SECS_MAX seconds each, a talk of
 *       1..FOOTLIGHT_TALK_MAX seconds, else refused.
 *   FL5 SLIDE START. Where each slide starts in the rehearsed run, in
 *       seconds from the first: the sum of the seconds of every slide
 *       before it (the first starts at 0), the same slide domain as FL4.
 *   FL6 RUN ORDER. The slide after slide cur (1..count) of count
 *       (1..FOOTLIGHT_DECK_MAX) in a mode: FOOTLIGHT_ADVANCE goes to the
 *       next and ends (0) after the last, FOOTLIGHT_HOLD stays, and
 *       FOOTLIGHT_LOOP goes to the next and back to the first after the
 *       last. Anything else is refused.
 *   FL7 A SLIDE RECORD fits ^ZXFOOTLIGHT (MYRA): the deck's number at most
 *       9 digits, the slide's 4, its rehearsed seconds 4, its layout 1,
 *       its bytes 7 and its stamp (ms since 1970) 15, sign aside.
 *   FL8 A LENGTH SHOWN as h:mm:ss is halloo.hms (halloo_rules.h HR4),
 *       never written again. */
#ifndef FOOTLIGHT_RULES_H
#define FOOTLIGHT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "halloo_rules.h"

#define FOOTLIGHT_REFUSED  (-1)
#define FOOTLIGHT_SIDE_MAX 65535 /* a window side in pixels */
#define FOOTLIGHT_ASPECT_W 16
#define FOOTLIGHT_ASPECT_H 9
#define FOOTLIGHT_DECK_MAX 999   /* slides in one rehearsal or run */
#define FOOTLIGHT_SECS_MAX 3600  /* one slide, an hour */
#define FOOTLIGHT_TALK_MAX 86400 /* one talk, a day */
#define FOOTLIGHT_END      0     /* FL6: no slide after the last */

enum { FOOTLIGHT_ADVANCE = 0, FOOTLIGHT_HOLD = 1, FOOTLIGHT_LOOP = 2 };

static inline bool footlight_side(int64_t v)
{
    return v >= 0 && v <= FOOTLIGHT_SIDE_MAX;
}

/* FL1 */
static inline int64_t footlight_fit_w(int64_t winw, int64_t winh)
{
    if (!footlight_side(winw) || !footlight_side(winh)) return FOOTLIGHT_REFUSED;
    int64_t w = FOOTLIGHT_ASPECT_W * winh / FOOTLIGHT_ASPECT_H;
    return winw < w ? winw : w;
}

/* FL2 */
static inline int64_t footlight_fit_h(int64_t winw, int64_t winh)
{
    if (!footlight_side(winw) || !footlight_side(winh)) return FOOTLIGHT_REFUSED;
    int64_t h = FOOTLIGHT_ASPECT_H * winw / FOOTLIGHT_ASPECT_W;
    return winh < h ? winh : h;
}

/* FL3 */
static inline int64_t footlight_bar(int64_t span, int64_t fit)
{
    if (!footlight_side(span) || fit < 0 || fit > span) return FOOTLIGHT_REFUSED;
    return (span - fit) / 2;
}

static inline bool footlight_secs_ok(const int64_t *secs, uint32_t n)
{
    if (!secs || n < 1 || n > FOOTLIGHT_DECK_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (secs[i] < 1 || secs[i] > FOOTLIGHT_SECS_MAX) return false;
    return true;
}

/* FL4: false when refused; at most 999 * 3600 = 3596400 seconds in all */
static inline bool footlight_rehearsal_over(const int64_t *secs, uint32_t n, int64_t talk,
                                            int64_t *over)
{
    if (!footlight_secs_ok(secs, n) || talk < 1 || talk > FOOTLIGHT_TALK_MAX || !over) return false;
    int64_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += secs[i];
    *over = s - talk;
    return true;
}

/* FL5: start[0..n) */
static inline bool footlight_slide_start(const int64_t *secs, uint32_t n, int64_t *start)
{
    if (!footlight_secs_ok(secs, n) || !start) return false;
    int64_t s = 0;
    for (uint32_t i = 0; i < n; i++) {
        start[i] = s;
        s += secs[i];
    }
    return true;
}

/* FL6 */
static inline int64_t footlight_next(int64_t cur, int64_t count, int64_t mode)
{
    if (count < 1 || count > FOOTLIGHT_DECK_MAX || cur < 1 || cur > count) return FOOTLIGHT_REFUSED;
    if (mode == FOOTLIGHT_HOLD) return cur;
    if (mode == FOOTLIGHT_ADVANCE) return cur < count ? cur + 1 : FOOTLIGHT_END;
    if (mode == FOOTLIGHT_LOOP) return cur < count ? cur + 1 : 1;
    return FOOTLIGHT_REFUSED;
}

/* FL7: |v| below 10^digits */
static inline bool footlight_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

static inline bool footlight_slide_record_ok(int64_t deck, int64_t slide, int64_t secs,
                                             int64_t layout, int64_t bytes, int64_t stamp)
{
    return footlight_digits(deck, INT64_C(1000000000)) && footlight_digits(slide, 10000) &&
           footlight_digits(secs, 10000) && footlight_digits(layout, 10) &&
           footlight_digits(bytes, 10000000) && footlight_digits(stamp, INT64_C(1000000000000000));
}

/* FL8 */
static inline int64_t footlight_hms(int64_t ms)
{
    return halloo_hms(ms);
}

#endif
