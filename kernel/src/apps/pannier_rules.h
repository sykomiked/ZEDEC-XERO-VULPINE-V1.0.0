/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pannier_rules.h — Pannier's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/pannier), beside the
 * C of the entries it composes (rookery_rules.h, trellis_rules.h,
 * pollen_rules.h with ferry_rules.h, caravan_rules.h, cove_rules.h,
 * warren_rules.h). Pure integer functions, no state: the model
 * (pannier_model.c) and the language test (tests/pannier_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   PR1 SEND NOW (parcel_send_now): a parcel goes out now when its peer is
 *       active (peerstate exactly TRELLIS_ACTIVE, 1), it is within size
 *       (room, PANNIER_PARCEL_MAX less its bytes, 0..PANNIER_PARCEL_MAX)
 *       and the attempt about to be made is at most the retry limit
 *       (attempt 1..PANNIER_TRIES). Each field against its own range, so
 *       Eve holds it; KRON reads any room at or above 0 as within size: a
 *       room past PANNIER_PARCEL_MAX (no parcel's bytes give it) is held.
 *   PR2 RETRY LIMITS (parcel_retry_ok): at most PANNIER_TRIES (16) attempts
 *       and a back-off of 0..PANNIER_WAIT_MAX (3600) seconds before one.
 *       The back-off itself is Warren's (warren.backoff, base
 *       PANNIER_WAIT_BASE, most PANNIER_WAIT_MAX): attempt 1 goes at once,
 *       attempt k > 1 waits warren_backoff(k - 1, ...).
 *   PR3 DELIVERY RECEIPTS: a peer's receipts carry a sequence that rises
 *       strictly; Cove's nonce discipline (cove.tunnel_seq_gap) is that
 *       rule, so it is used: the gap seq - last, a reused sequence (at or
 *       below the last) or one past eighteen digits refused, the peer's
 *       last kept as it was.
 *   PR4 ROUTE TIME (route_hours): the hours a parcel is expected to take,
 *       the sum of its hop times in minutes over 60, an exact reduced
 *       fraction, never rounded; 1..PANNIER_HOPS hops of 0..PANNIER_HOP_MAX
 *       minutes each, anything else refused.
 *   PR5 THE QUEUE (parcel_queue_room): a bounded ring of cap slots with
 *       counters head (the next parcel put in) and tail (the oldest still
 *       in); room cap - (head - tail), 0 is backpressure (head - tail >=
 *       cap: nothing more is taken) and the oldest goes first (tail). tail
 *       0..head, cap 1..PANNIER_QUEUE_MAX, head - tail at most cap, else
 *       refused.
 *   PR6 THE PARCEL TREE (parcel_node_ok): a freight of a parcel fits
 *       ^ZXPANNIER(peer, parcel, freight) = size ^ attempt ^ state ^ stamp:
 *       peer 1 digit, parcel 9, freight 6, size 8, attempt 2, state 1 (the
 *       tracking status, caravan_rules.h CR5) and stamp (seconds since
 *       1970) 12, each sign aside.
 *   PR7 DELIVERED OR LOST, four-valued (pannier_delivery, kept in C): a
 *       signed receipt says TRUE, a hop report of loss says FALSE, joined
 *       with lpres_kjoin; both make BOTH, and a BOTH parcel is neither sent
 *       again nor closed until a witness (a second receipt, or the
 *       recipient's word) settles it. */
#ifndef PANNIER_RULES_H
#define PANNIER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "caravan_rules.h"
#include "cove_rules.h"
#include "lpres.h"
#include "pollen_rules.h"
#include "rookery_rules.h"
#include "trellis_rules.h"
#include "warren_rules.h"

#define PANNIER_REFUSED    INT64_MIN
#define PANNIER_PARCEL_MAX POLLEN_FILE_MAX /* PR1: 16 MiB, a file of a Pollen offer */
#define PANNIER_TRIES      16              /* PR2 */
#define PANNIER_WAIT_BASE  30              /* PR2: seconds before the second attempt */
#define PANNIER_WAIT_MAX   3600            /* PR2: the back-off's cap */
#define PANNIER_HOPS       64u             /* PR4 */
#define PANNIER_HOP_MAX    99999           /* PR4: minutes, about 69 days */
#define PANNIER_QUEUE_MAX  INT64_C(999999) /* PR5 */
#define PANNIER_PEER_MAX   9               /* PR6: 1 digit */
#define PANNIER_PARCEL_NO  INT64_C(999999) /* a parcel's number, caravan's ship */

/* what a hop or the recipient says of a parcel (PR7) */
enum { PANNIER_SAID_NONE = 0, PANNIER_SAID_DELIVERED = 1, PANNIER_SAID_LOST = 2 };

/* PR1 */
static inline bool pannier_send_now(int64_t peerstate, int64_t room, int64_t attempt)
{
    return peerstate == TRELLIS_ACTIVE && room >= 0 && room <= PANNIER_PARCEL_MAX && attempt >= 1 &&
           attempt <= PANNIER_TRIES;
}

/* PR2 */
static inline bool pannier_retry_ok(int64_t attempt, int64_t backoff)
{
    return attempt >= 1 && attempt <= PANNIER_TRIES && backoff >= 0 && backoff <= PANNIER_WAIT_MAX;
}

/* PR2: the seconds to wait before attempt k (1..PANNIER_TRIES), or
 * PANNIER_REFUSED */
static inline int64_t pannier_wait(int64_t k)
{
    if (k < 1 || k > PANNIER_TRIES) return PANNIER_REFUSED;
    if (k == 1) return 0;
    int64_t w = warren_backoff(k - 1, PANNIER_TRIES, PANNIER_WAIT_BASE, PANNIER_WAIT_MAX);
    return w == WARREN_REFUSED ? PANNIER_REFUSED : w;
}

static inline int64_t pannier_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
}

/* PR4: the expected hours as num / den, reduced; false when refused */
static inline bool pannier_route_hours(const int64_t *hop, uint32_t n, int64_t *num, int64_t *den)
{
    int64_t s = 0;
    if (!hop || !num || !den || n < 1 || n > PANNIER_HOPS) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (hop[i] < 0 || hop[i] > PANNIER_HOP_MAX) return false;
        s += hop[i]; /* at most 64 * 99999: no overflow */
    }
    int64_t g = s ? pannier_gcd(s, 60) : 60;
    *num = s / g;
    *den = 60 / g;
    return true;
}

/* PR5 */
static inline int64_t pannier_queue_room(int64_t head, int64_t tail, int64_t cap)
{
    if (tail < 0 || head < tail || cap < 1 || cap > PANNIER_QUEUE_MAX || head - tail > cap)
        return PANNIER_REFUSED;
    return cap - (head - tail);
}

/* |v| has at most `digits` decimal digits (MYRA's iN) */
static inline bool pannier_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && i < 18u; i++) top *= 10;
    return v > -top && v < top;
}

/* PR6 */
static inline bool pannier_node_ok(int64_t peer, int64_t parcel, int64_t freight, int64_t size,
                                   int64_t attempt, int64_t state, int64_t stamp)
{
    return pannier_digits(peer, 1) && pannier_digits(parcel, 9) && pannier_digits(freight, 6) &&
           pannier_digits(size, 8) && pannier_digits(attempt, 2) && pannier_digits(state, 1) &&
           pannier_digits(stamp, 12);
}

/* PR7: receipts the signed receipts heard for a parcel, lost 1 when a hop
 * reported it lost, witness what a second witness said (PANNIER_SAID_*):
 * a witness settles it, else the receipts and the hop are joined */
static inline lpres_state_t pannier_delivery(int64_t receipts, int64_t lost, int64_t witness)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    if (witness == PANNIER_SAID_DELIVERED || receipts >= 2) return LPRES_STATE_TRUE;
    if (witness == PANNIER_SAID_LOST) return LPRES_STATE_FALSE;
    if (receipts >= 1) s = lpres_kjoin(s, LPRES_STATE_TRUE);
    if (lost) s = lpres_kjoin(s, LPRES_STATE_FALSE);
    return s;
}

#endif
