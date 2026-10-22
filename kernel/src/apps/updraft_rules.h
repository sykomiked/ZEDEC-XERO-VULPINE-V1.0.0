/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* updraft_rules.h — Updraft's rules in C, the reference its Trebago, KRON,
 * Eve and MYRA blocks are checked against (blocks/updraft). Pure integer
 * functions, no state: the model (updraft_model.c) and the language test
 * (tests/updraft_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   UR1 LINK MARGIN, whole dB: power - loss - rxfloor, the transmit power
 *       in dBm (0..UPDRAFT_POWER_MAX), loss the free-space path loss of
 *       the distance step and the radio band from the integer dB table
 *       below, rxfloor the receiver's floor in dBm
 *       (UPDRAFT_FLOOR_MIN..UPDRAFT_FLOOR_MAX). Below 0 the far end does
 *       not hear the link. Anything outside is refused (UPDRAFT_REFUSED).
 *       The table is 32.44 + 20 log10(km) + 20 log10(MHz) rounded to whole
 *       dB, worked out once, so nothing here is a logarithm: steps 0..7 are
 *       1, 2, 5, 10, 20, 50, 100 and 200 km; at 2 m (146 MHz) they lose
 *       76 82 90 96 102 110 116 122 dB, at 6 m (52 MHz) 10 dB less and at
 *       70 cm (440 MHz) 9 dB more.
 *   UR2 TRANSMIT ALLOWED: the power is at most the band's licensed
 *       maximum, given as its margin headroom = licmax - power: power
 *       0..UPDRAFT_POWER_MAX and headroom 0..UPDRAFT_POWER_MAX, each on its
 *       own range. A negative headroom is power past the licence. Advice
 *       only: the operator keys the transmitter, never Updraft.
 *   UR3 THE LINK MATRIX: one directed link a row, src sending to dst with
 *       its margin (UR1), every station 0..UPDRAFT_STATION_MAX (0 this
 *       one). A row's state is 0 when its own margin is below 0
 *       (down), 2 when some row carries the reverse link (dst to src) with a
 *       margin of 0 or more (two-way), else 1 (one-way).
 *   UR4 A RELAY NODE of ^ZXUPDRAFT(frame, relay) holds integers of at most
 *       the digits its pieces declare, sign aside (blocks/updraft/relay.myra):
 *       frame 9, relay 3, parent 3, hops 2, payload 10 (the Fletcher-32 of
 *       what that relay heard), state 1 (the frame's four-valued state).
 *   UR5 ONE FRAME, MANY RELAYS: each relay's report of a frame is evidence
 *       for what it carries, joined with lpres_kjoin: every relay hearing
 *       one payload is TRUE; two relays hearing two payloads is BOTH, and
 *       the frame is kept, never passed on, until it settles. It settles to
 *       a payload when the frame's own checksum names one a relay heard
 *       (TRUE; FALSE when none heard what the checksum names), or, with no
 *       checksum, when one payload is heard by two relays or more and by
 *       more relays than any other (a third relay). No report is NEITHER.
 *   UR6 COUNTER RATE: sounding.rate's rule, as sounding_model.c works it
 *       (Sounding has no rules header and its rate is static there):
 *       (now - was) * 1000 / dt, truncated; -1 for a counter that went
 *       back, no time between the readings or a product past int64. Used
 *       for frames heard a second from each peer; the composed entry is
 *       Sounding's, never written again. */
#ifndef UPDRAFT_RULES_H
#define UPDRAFT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define UPDRAFT_REFUSED     INT64_MIN
#define UPDRAFT_POWER_MAX   61   /* dBm, 1.26 kW: inside an amateur licence's 1500 W */
#define UPDRAFT_STEPS       8    /* distance steps 0..7 */
#define UPDRAFT_RF          3    /* radio bands: 0 6 m, 1 2 m, 2 70 cm */
#define UPDRAFT_FLOOR_MIN   -150 /* dBm */
#define UPDRAFT_FLOOR_MAX   -60
#define UPDRAFT_STATION_MAX 999 /* a station's number on the link matrix */
#define UPDRAFT_FRAME_MAX   INT64_C(999999999)
#define UPDRAFT_HOPS_MAX    99
#define UPDRAFT_DIGEST_MAX  INT64_C(4294967295) /* a Fletcher-32 */

enum { UPDRAFT_RF_6M = 0, UPDRAFT_RF_2M = 1, UPDRAFT_RF_70CM = 2 };
enum { UPDRAFT_DOWN = 0, UPDRAFT_ONE_WAY = 1, UPDRAFT_TWO_WAY = 2 };

/* UR1: the path loss of a step on a band, whole dB, or UPDRAFT_REFUSED */
static inline int64_t updraft_path_loss(int64_t step, int64_t rf)
{
    static const int64_t at_2m[UPDRAFT_STEPS] = {76, 82, 90, 96, 102, 110, 116, 122};
    static const int64_t band[UPDRAFT_RF] = {-10, 0, 9};
    if (step < 0 || step >= UPDRAFT_STEPS || rf < 0 || rf >= UPDRAFT_RF) return UPDRAFT_REFUSED;
    return at_2m[step] + band[rf];
}

/* UR1 */
static inline int64_t updraft_link_margin(int64_t power, int64_t step, int64_t rf, int64_t rxfloor)
{
    int64_t loss = updraft_path_loss(step, rf);
    if (loss == UPDRAFT_REFUSED || power < 0 || power > UPDRAFT_POWER_MAX ||
        rxfloor < UPDRAFT_FLOOR_MIN || rxfloor > UPDRAFT_FLOOR_MAX)
        return UPDRAFT_REFUSED;
    return power - loss - rxfloor;
}

/* UR2 */
static inline bool updraft_transmit_power_ok(int64_t power, int64_t headroom)
{
    return power >= 0 && power <= UPDRAFT_POWER_MAX && headroom >= 0 &&
           headroom <= UPDRAFT_POWER_MAX;
}

/* UR3: row i's state in the matrix of n directed links */
static inline int64_t updraft_link_state(const int64_t *src, const int64_t *dst,
                                         const int64_t *margin, uint32_t n, uint32_t i)
{
    if (!src || !dst || !margin || i >= n) return UPDRAFT_REFUSED;
    if (margin[i] < 0) return UPDRAFT_DOWN;
    for (uint32_t j = 0; j < n; j++)
        if (src[j] == dst[i] && dst[j] == src[i] && margin[j] >= 0) return UPDRAFT_TWO_WAY;
    return UPDRAFT_ONE_WAY;
}

/* UR4: v fits in `digits` decimal digits, sign aside (MYRA's iN) */
static inline bool updraft_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

static inline bool updraft_relay_node_fits(int64_t frame, int64_t relay, int64_t parent,
                                           int64_t hops, int64_t payload, int64_t state)
{
    return updraft_digits_fit(frame, 9) && updraft_digits_fit(relay, 3) &&
           updraft_digits_fit(parent, 3) && updraft_digits_fit(hops, 2) &&
           updraft_digits_fit(payload, 10) && updraft_digits_fit(state, 1);
}

/* UR5: the frame's state from the payloads n relays heard (digest[0..n)),
 * and the payload it settled to in *settled (-1 while it has not). */
static inline lpres_state_t updraft_frame_join(const int64_t *digest, uint32_t n, bool has_sum,
                                               int64_t sum, int64_t *settled)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    uint32_t best = 0, next = 0;
    int64_t top = -1;
    *settled = -1;
    if (!digest || n == 0) return LPRES_STATE_NEITHER;
    /* the evidence for the first payload heard: TRUE from each relay that
     * heard it, FALSE from each that heard another */
    for (uint32_t i = 0; i < n; i++)
        s = lpres_kjoin(s, digest[i] == digest[0] ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    if (has_sum) {
        for (uint32_t i = 0; i < n; i++)
            if (digest[i] == sum) {
                *settled = sum;
                return LPRES_STATE_TRUE;
            }
        return LPRES_STATE_FALSE;
    }
    if (s == LPRES_STATE_TRUE) {
        *settled = digest[0];
        return s;
    }
    /* BOTH: the payload most relays heard, when it is a clear majority of two or more */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = 0;
        bool seen = false;
        for (uint32_t j = 0; j < i; j++) seen |= digest[j] == digest[i];
        if (seen) continue;
        for (uint32_t j = 0; j < n; j++) c += digest[j] == digest[i];
        if (c > best) {
            next = best;
            best = c;
            top = digest[i];
        } else if (c > next) {
            next = c;
        }
    }
    if (best >= 2 && best > next) {
        *settled = top;
        return LPRES_STATE_TRUE;
    }
    return s;
}

/* UR6 */
static inline int64_t updraft_counter_rate(int64_t now, int64_t was, int64_t dt)
{
    if (dt <= 0 || now < was) return -1;
    if (was < 0 && now > INT64_MAX + was) return -1;
    if (now - was > INT64_MAX / 1000) return -1;
    return (now - was) * 1000 / dt;
}

#endif
