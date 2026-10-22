/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trellis_rules.h — Trellis's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/trellis). Pure
 * integer functions, no state: the model (trellis_model.c) and the
 * language test (tests/trellis_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   TL1 REVOKE ALLOWED: a device of the owner's mesh may be revoked from
 *       this device when this is an admin session (sessionadmin exactly 1),
 *       the device is not this one (isself exactly 0), it is still active
 *       (isactive exactly 1) and at least one active admin is left once it
 *       is gone (adminsleft, the active admins less the device itself when
 *       it is one, 1..TRELLIS_DEVICES). devmesh.h's own rule (dm_revoke):
 *       only an admin signs a roster, and never one without an admin; that
 *       a device does not revoke itself is Trellis's, stricter. Advice
 *       only: a revocation can never be undone, so the owner confirms it
 *       twice (trellis_model.h TM4).
 *   TL2 INVITE MINUTES LEFT: the whole minutes, rounded up, an invitation
 *       issued at `issued` with a life of `ttl` still has at `now` (ms):
 *       ceil(max(0, issued + ttl - now) / 60000); 0 is lapsed, 10 a fresh
 *       one (devmesh.h DM_INVITE_TTL_MS, ten minutes). issued and now are
 *       0..TRELLIS_MS_MAX, ttl 0..TRELLIS_INVITE_TTL_MS; anything else is
 *       refused (TRELLIS_REFUSED).
 *   TL3 PEERS IN STATE: how many peers of a vector of states are in the
 *       state want (given once per row, the same on every row): states
 *       TRELLIS_ACTIVE 1, TRELLIS_REVOKED 2, TRELLIS_WAITING 3. A state or
 *       a want outside 1..3, no rows or more than TRELLIS_ROWS_MAX refuse
 *       the count.
 *   TL4 A PEER RECORD of ^ZXTRELLIS holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/trellis/peer.myra): peer,
 *       state, role, admin and held 1 each, since (the roster version the
 *       row has held since) 18.
 *   TL5 A PHONE MAY POST (move money, change the network) when the action
 *       asks for it, its pairing allows it and it is not revoked:
 *       gatehouse.use_ok (@logic.gatehouse_use_ok), used as it is
 *       (gatehouse_rules.h GR3). Advice only: zxvd decides (PH_POST). */
#ifndef TRELLIS_RULES_H
#define TRELLIS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TRELLIS_DEVICES       8u              /* devmesh.h DM_MAX_DEVICES */
#define TRELLIS_INVITE_TTL_MS INT64_C(600000) /* devmesh.h DM_INVITE_TTL_MS */
#define TRELLIS_MINUTE_MS     INT64_C(60000)
#define TRELLIS_MS_MAX        INT64_C(253402300799999) /* the last ms of the year 9999 */
#define TRELLIS_ROWS_MAX      4096u
#define TRELLIS_REFUSED       INT64_MIN

/* a peer's state (TL3) */
enum { TRELLIS_ACTIVE = 1, TRELLIS_REVOKED = 2, TRELLIS_WAITING = 3 };

/* TL1 */
static inline bool trellis_revoke_allowed(int64_t sessionadmin, int64_t isself, int64_t isactive,
                                          int64_t adminsleft)
{
    return sessionadmin == 1 && isself == 0 && isactive == 1 && adminsleft >= 1 &&
           adminsleft <= (int64_t) TRELLIS_DEVICES;
}

/* TL2: whole minutes left, rounded up, or TRELLIS_REFUSED */
static inline int64_t trellis_invite_minutes_left(int64_t issued, int64_t ttl, int64_t now)
{
    if (issued < 0 || issued > TRELLIS_MS_MAX || now < 0 || now > TRELLIS_MS_MAX || ttl < 0 ||
        ttl > TRELLIS_INVITE_TTL_MS)
        return TRELLIS_REFUSED;
    int64_t left = issued + ttl - now; /* every term is far inside int64 */
    if (left <= 0) return 0;
    return (left + TRELLIS_MINUTE_MS - 1) / TRELLIS_MINUTE_MS;
}

static inline bool trellis_state_ok(int64_t s)
{
    return s >= TRELLIS_ACTIVE && s <= TRELLIS_WAITING;
}

/* TL3: how many of state[0..n) equal want[i], or TRELLIS_REFUSED */
static inline int64_t trellis_peers_in_state(const int64_t *state, const int64_t *want, uint32_t n)
{
    int64_t c = 0;
    if (!state || !want || n == 0 || n > TRELLIS_ROWS_MAX) return TRELLIS_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!trellis_state_ok(state[i]) || !trellis_state_ok(want[i])) return TRELLIS_REFUSED;
        c += state[i] == want[i];
    }
    return c;
}

/* TL4: v fits in `digits` decimal digits, sign aside (MYRA's iN) */
static inline bool trellis_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

static inline bool trellis_peer_record_fits(int64_t peer, int64_t state, int64_t role,
                                            int64_t admin, int64_t held, int64_t since)
{
    return trellis_digits_fit(peer, 1) && trellis_digits_fit(state, 1) &&
           trellis_digits_fit(role, 1) && trellis_digits_fit(admin, 1) &&
           trellis_digits_fit(held, 1) && trellis_digits_fit(since, 18);
}

#endif
