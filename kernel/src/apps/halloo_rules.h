/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* halloo_rules.h — Halloo's rules in C, the reference its KRON, Rust,
 * Eve, Sutra and MYRA blocks are checked against (blocks/halloo). Pure
 * integer functions, no state: the model (halloo_model.c) and the
 * language test (tests/halloo_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   HR1 QUIET MINUTES LEFT. Quiet hours run from minute `opens` of the day
 *       up to minute `closes` (opens included, closes not), crossing
 *       midnight when opens is after closes, the whole day when they are
 *       equal. At minute `now` the answer is the minutes until they close
 *       (1..1440) inside them, 0 outside. Every minute is 0..1439, else
 *       refused (HALLOO_REFUSED).
 *   HR2 MAY THIS CALL RING NOW. The caller is a known contact (known 1),
 *       not blocked (blocked 0), and no quiet hours are running (quiet 0,
 *       HR1's answer). Anything else, a flag that is no flag included, is
 *       no.
 *   HR3 RING TIME. How long a call may ring before Halloo gives up:
 *       HALLOO_RING_MIN..HALLOO_RING_MAX ms. zxvd's own ringing ends at
 *       30 s (call_session.c, ring_ms), so no longer.
 *   HR4 CALL LENGTH. ms as h * 10000 + m * 100 + s, whole seconds, the
 *       part second dropped; ms 0..HALLOO_MS_MAX (Sutra's 9(10)), else
 *       refused.
 *   HR5 A CALL RECORD. seq 1..HALLOO_SEQ_MAX, peer 1..HALLOO_PEER_MAX,
 *       dir 0 in or 1 out, media 0 audio or 1 audio and video, reason one
 *       of zxvd's end reasons (HALLOO_END_NONE..HALLOO_END_CANCELLED), ms
 *       0..HALLOO_MS_MAX and stamp 0..HALLOO_STAMP_MAX ms since 1970. */
#ifndef HALLOO_RULES_H
#define HALLOO_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define HALLOO_REFUSED   (-1)
#define HALLOO_DAY_MIN   1440
#define HALLOO_RING_MIN  5000
#define HALLOO_RING_MAX  30000 /* zxvd's ring_ms */
#define HALLOO_MS_MAX    9999999999LL
#define HALLOO_SEQ_MAX   999999999
#define HALLOO_PEER_MAX  4294967295LL
#define HALLOO_STAMP_MAX 9999999999999LL

/* zxvd's end reasons (call.status end_reason), in its order */
enum {
    HALLOO_END_NONE = 0,
    HALLOO_END_LOCAL = 1,
    HALLOO_END_REMOTE = 2,
    HALLOO_END_REJECTED = 3,
    HALLOO_END_NO_ANSWER = 4,
    HALLOO_END_NO_RESPONSE = 5,
    HALLOO_END_ICE_FAILED = 6,
    HALLOO_END_TIMEOUT = 7,
    HALLOO_END_BUSY = 8,
    HALLOO_END_INCOMPATIBLE = 9,
    HALLOO_END_CANCELLED = 10,
};

static inline bool halloo_minute(int64_t m)
{
    return m >= 0 && m < HALLOO_DAY_MIN;
}

/* HR1 */
static inline int64_t halloo_quiet_left(int64_t opens, int64_t closes, int64_t now)
{
    if (!halloo_minute(opens) || !halloo_minute(closes) || !halloo_minute(now))
        return HALLOO_REFUSED;
    int64_t span = HALLOO_DAY_MIN - (opens - closes + HALLOO_DAY_MIN) % HALLOO_DAY_MIN;
    int64_t past = (now - opens + HALLOO_DAY_MIN) % HALLOO_DAY_MIN;
    return past < span ? span - past : 0;
}

/* HR2 */
static inline bool halloo_ring_ok(int64_t known, int64_t blocked, int64_t quiet)
{
    return known == 1 && blocked == 0 && quiet == 0;
}

/* HR3 */
static inline bool halloo_ring_wait_ok(int64_t ring_ms)
{
    return ring_ms >= HALLOO_RING_MIN && ring_ms <= HALLOO_RING_MAX;
}

/* HR4 */
static inline int64_t halloo_hms(int64_t ms)
{
    if (ms < 0 || ms > HALLOO_MS_MAX) return HALLOO_REFUSED;
    int64_t s = ms / 1000;
    return s / 3600 * 10000 + s % 3600 / 60 * 100 + s % 60;
}

/* HR5 */
static inline bool halloo_call_ok(int64_t seq, int64_t peer, int64_t dir, int64_t media,
                                  int64_t reason, int64_t ms, int64_t stamp)
{
    return seq >= 1 && seq <= HALLOO_SEQ_MAX && peer >= 1 && peer <= HALLOO_PEER_MAX &&
           (dir == 0 || dir == 1) && (media == 0 || media == 1) && reason >= HALLOO_END_NONE &&
           reason <= HALLOO_END_CANCELLED && ms >= 0 && ms <= HALLOO_MS_MAX && stamp >= 0 &&
           stamp <= HALLOO_STAMP_MAX;
}

#endif
