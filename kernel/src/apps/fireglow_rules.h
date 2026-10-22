/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fireglow_rules.h — Fireglow's rules in C, the reference its Trebago,
 * KRON, Eve and MYRA blocks are checked against (blocks/fireglow), with the
 * rules it uses from nearer the core. Pure integer functions, no state:
 * the model (fireglow_model.c) and the language test
 * (tests/fireglow_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   FR1 TIME LEFT AT A SPEED (speed_left_ms). What is left of an episode
 *       or book of length ms heard to pos ms, played at a speed rate (a
 *       q16 multiple: 65536 is the normal speed), in ms of the listener's
 *       time: (length - pos) * 65536 / rate, an exact reduced rational,
 *       never rounded (Trebago has no floor; the model shows its floor).
 *       length 0..FIREGLOW_LEN_MAX (under 100 hours), pos 0..length and
 *       rate FIREGLOW_RATE_MIN..FIREGLOW_RATE_MAX (FR4's limits), else
 *       refused.
 *   FR2 LISTENING SHARE PER SHOW: the Banker's share (banker.share, the
 *       concept share_of_whole), never written again: each show's minutes
 *       listened as hundredths of a percent of the minutes listened in
 *       all, floor(10000 * minutes / max(1, total)); a negative count, or
 *       one past the total, is refused. This is the C the model runs; the
 *       languages are the Banker's KRON and C++ (WebAssembly).
 *   FR3 CHAPTER AT A POSITION (chapter_at_pos). The chapter playing at
 *       pos ms: the number of chapter starts at or before pos, so 1 is
 *       the first chapter and 0 is before it. The starts are ascending
 *       (the model refuses a list that is not, FM3), at most
 *       FIREGLOW_CHAPTERS of them.
 *   FR4 SPEED LIMITS (play_speed_ok). A playing speed rate (q16) from
 *       half the normal speed to three times it:
 *       FIREGLOW_RATE_MIN..FIREGLOW_RATE_MAX. Anything else is refused,
 *       never brought inside silently.
 *   FR5 EPISODE RECORD (episode_row_ok) fits ^ZXFIREGLOW: show (its
 *       place among the shows) 0..FIREGLOW_SHOWS - 1, episode (its place
 *       among the episodes) 0..FIREGLOW_EPISODES - 1, pos and length
 *       0..FIREGLOW_LEN_MAX (length 0 is not known yet) and heard (ms
 *       listened in all) 0..FIREGLOW_HEARD_MAX. MYRA's digits (i2, i3,
 *       i9, i9, i10, sign aside) hold more; Eve's ranges are these. That
 *       pos is no later than a known length relates two fields: the
 *       model checks it (FM4), neither language can say it.
 *   FR6 TIME SHOWN: a time left as h:mm:ss is halloo.hms (halloo_rules.h
 *       HR4), never written again. */
#ifndef FIREGLOW_RULES_H
#define FIREGLOW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "halloo_rules.h"

#define FIREGLOW_REFUSED   (-1)
#define FIREGLOW_RATE_UNIT INT64_C(65536)      /* FR1: the normal speed, q16 */
#define FIREGLOW_RATE_MIN  INT64_C(32768)      /* FR4: half speed */
#define FIREGLOW_RATE_MAX  INT64_C(196608)     /* FR4: three times */
#define FIREGLOW_LEN_MAX   INT64_C(359999999)  /* FR1, FR5: under 100 hours, MYRA's i9 */
#define FIREGLOW_HEARD_MAX INT64_C(9999999999) /* FR5: MYRA's i10 */
#define FIREGLOW_SHOWS     32u                 /* FR5: shows and books */
#define FIREGLOW_EPISODES  128u                /* FR5: episodes in all */
#define FIREGLOW_CHAPTERS  64u                 /* FR3: chapters of one episode */
#define FIREGLOW_MINUTE_MS INT64_C(60000)

static inline int64_t fireglow_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

/* FR1: false when refused; else *num / *den reduced, *den at least 1 */
static inline bool fireglow_speed_left(int64_t length, int64_t pos, int64_t rate, int64_t *num,
                                       int64_t *den)
{
    if (!num || !den || length < 0 || length > FIREGLOW_LEN_MAX || pos < 0 || pos > length ||
        rate < FIREGLOW_RATE_MIN || rate > FIREGLOW_RATE_MAX)
        return false;
    int64_t n = (length - pos) * FIREGLOW_RATE_UNIT, g = fireglow_gcd(n, rate);
    *num = n / g;
    *den = rate / g;
    return true;
}

/* FR1, shown: the floor of the time left in ms, or FIREGLOW_REFUSED */
static inline int64_t fireglow_speed_left_floor(int64_t length, int64_t pos, int64_t rate)
{
    int64_t num = 0, den = 1;
    if (!fireglow_speed_left(length, pos, rate, &num, &den)) return FIREGLOW_REFUSED;
    return num / den;
}

/* FR2: banker.share's rule, or FIREGLOW_REFUSED */
static inline int64_t fireglow_listen_share(int64_t minutes, int64_t total)
{
    int64_t t = total < 1 ? 1 : total;
    if (minutes < 0 || minutes > t || t > INT64_MAX / 10000) return FIREGLOW_REFUSED;
    return minutes * 10000 / t;
}

/* FR3: the chapter at pos, or FIREGLOW_REFUSED for no list */
static inline int64_t fireglow_chapter_at_pos(const int64_t *starts, uint32_t n, int64_t pos)
{
    int64_t c = 0;
    if (!starts && n) return FIREGLOW_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (starts[i] <= pos) c++;
    return c;
}

/* FR4 */
static inline bool fireglow_play_speed_ok(int64_t rate)
{
    return rate >= FIREGLOW_RATE_MIN && rate <= FIREGLOW_RATE_MAX;
}

/* FR5 */
static inline bool fireglow_episode_row_ok(int64_t show, int64_t episode, int64_t pos,
                                           int64_t length, int64_t heard)
{
    return show >= 0 && show < (int64_t) FIREGLOW_SHOWS && episode >= 0 &&
           episode < (int64_t) FIREGLOW_EPISODES && pos >= 0 && pos <= FIREGLOW_LEN_MAX &&
           length >= 0 && length <= FIREGLOW_LEN_MAX && heard >= 0 && heard <= FIREGLOW_HEARD_MAX;
}

/* FR6: h * 10000 + m * 100 + s, or HALLOO_REFUSED */
static inline int64_t fireglow_left_hms(int64_t ms)
{
    return halloo_hms(ms);
}

#endif
