/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warble_rules.h — Warble's rules in C, the reference its Rust, KRON,
 * Trebago, Eve and MYRA blocks are checked against (blocks/warble), with
 * the rules it uses from nearer the core. Pure integer functions, no state:
 * the model (warble_model.c) and the language test
 * (tests/warble_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   WR1 SHUFFLE (shuffle_at). The track (0..count - 1) played at place
 *       step (0..count - 1) of the shuffle of count tracks from seed: six
 *       rounds of swap-or-not over x, starting at x = step. With
 *       s = seed mod 999983, round r (0..5) takes the key
 *       k = (s + 104729 * r) mod count, the partner p = (k + count - x)
 *       mod count, h = (s + 65537 * r + 31337 * max(x, p)) mod 999983, and
 *       swaps x for p when (h * h mod 999983) is odd. x and p share their
 *       pair's max, so both swap or neither: each round is a permutation,
 *       and the whole is one, every track once. The seed changes the
 *       order itself, not only where it starts. seed 0..999999999,
 *       count 1..999999, step 0..count - 1, else refused.
 *   WR2 PLAYLIST LENGTH (playlist_ms). The exact sum of a playlist's track
 *       lengths in ms, each 0..86400000 (a day; 0 is a length not known),
 *       added from the first; any length outside refuses the sum. A
 *       playlist holds at most WARBLE_PLAYLIST_MAX tracks, so the longest
 *       (100 days) is still inside halloo.hms's lengths.
 *   WR3 VOLUME LIMITS (volume_cap_ok). A listening volume and the owner's
 *       cap, each 0..100 percent (Keystone's volume range, KS1), and the
 *       volume no louder than the cap. Advisory and owner-confirmed: only
 *       the owner moves the cap, and a volume past it is refused, never
 *       turned down silently.
 *   WR4 TRACK INDEX RECORD (track_record_ok) fits ^ZXWARBLE: track (its
 *       place in the library) 0..WARBLE_TRACKS - 1, ms 0..86400000 and
 *       mtime (seconds since 1970) 0..99999999999. MYRA's digits (i4, i8,
 *       i11, sign aside) hold more; Eve's ranges are these.
 *   WR5 LENGTH SHOWN: a playlist's length as h:mm:ss is halloo.hms
 *       (halloo_rules.h HR4), never written again.
 *   WR6 HALL RATE: a watch hall's playing rate (q16, 65536 the normal
 *       speed) is one zxvd's hall keeps, 1..262144, by
 *       kinetoscope.hall_rate_ok (kinetoscope_rules.h KR4), never written
 *       again. */
#ifndef WARBLE_RULES_H
#define WARBLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "halloo_rules.h"
#include "kinetoscope_rules.h"

#define WARBLE_REFUSED      (-1)
#define WARBLE_SEED_MAX     INT64_C(999999999) /* WR1: 9(9) */
#define WARBLE_COUNT_MAX    INT64_C(999999)    /* WR1: tracks a shuffle orders */
#define WARBLE_MIX_PRIME    INT64_C(999983)    /* WR1: the largest prime below 10^6 */
#define WARBLE_KEY_STEP     INT64_C(104729)    /* WR1: the 10000th prime */
#define WARBLE_ROUND_STEP   INT64_C(65537)
#define WARBLE_PAIR_STEP    INT64_C(31337)
#define WARBLE_ROUNDS       6
#define WARBLE_TRACK_MS_MAX INT64_C(86400000)    /* WR2, WR4: a day */
#define WARBLE_PLAYLIST_MAX 100u                 /* WR2: tracks in one playlist */
#define WARBLE_VOLUME_MAX   INT64_C(100)         /* WR3: percent */
#define WARBLE_TRACKS       512u                 /* WR4: the library */
#define WARBLE_MTIME_MAX    INT64_C(99999999999) /* WR4: 11 digits */

/* WR1 */
static inline int64_t warble_shuffle_at(int64_t seed, int64_t count, int64_t step)
{
    if (seed < 0 || seed > WARBLE_SEED_MAX || count < 1 || count > WARBLE_COUNT_MAX || step < 0 ||
        step >= count)
        return WARBLE_REFUSED;
    int64_t s = seed % WARBLE_MIX_PRIME, x = step;
    for (int64_t r = 0; r < WARBLE_ROUNDS; r++) {
        int64_t k = (s + WARBLE_KEY_STEP * r) % count;
        int64_t p = (k + count - x) % count;
        int64_t m = x > p ? x : p;
        int64_t h = (s + WARBLE_ROUND_STEP * r + WARBLE_PAIR_STEP * m) % WARBLE_MIX_PRIME;
        if ((h * h % WARBLE_MIX_PRIME) % 2 == 1) x = p;
    }
    return x;
}

/* WR2: WARBLE_REFUSED when ms is NULL or a length is outside 0..a day */
static inline int64_t warble_playlist_ms(const int64_t *ms, uint32_t n)
{
    int64_t total = 0;
    if (!ms && n) return WARBLE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (ms[i] < 0 || ms[i] > WARBLE_TRACK_MS_MAX) return WARBLE_REFUSED;
        if (total > INT64_MAX - ms[i]) return WARBLE_REFUSED;
        total += ms[i];
    }
    return total;
}

/* WR3 */
static inline bool warble_volume_cap_ok(int64_t volume, int64_t cap)
{
    return volume >= 0 && volume <= WARBLE_VOLUME_MAX && cap >= 0 && cap <= WARBLE_VOLUME_MAX &&
           volume <= cap;
}

/* WR4 */
static inline bool warble_track_record_ok(int64_t track, int64_t ms, int64_t mtime)
{
    return track >= 0 && track < (int64_t) WARBLE_TRACKS && ms >= 0 && ms <= WARBLE_TRACK_MS_MAX &&
           mtime >= 0 && mtime <= WARBLE_MTIME_MAX;
}

/* WR5: h * 10000 + m * 100 + s, or HALLOO_REFUSED */
static inline int64_t warble_length_hms(int64_t ms)
{
    return halloo_hms(ms);
}

/* WR6 */
static inline bool warble_hall_rate_ok(int64_t rate_q16)
{
    return kinetoscope_hall_rate_ok(rate_q16);
}

#endif
