/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* threshold_rules.h — Threshold's rules in C, the reference its Sutra,
 * KRON, Eve and MYRA blocks are checked against (blocks/threshold). Pure
 * integer functions, no state: the model (threshold_model.c) and the
 * language test (tests/threshold_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   TH1 RIGHTS over a lock: an owner may add, remove or change any lock; a
 *       person only their own (own 1); a guest never has a lock, so no one
 *       gives one a lock (target 3) and a guest changes none. Roles are
 *       1 owner, 2 person, 3 guest; anything else may do nothing.
 *   TH2 SKIP THE LOGIN: exactly one seat, its lock none (0) and auto-enter
 *       on (1). The flags are 0 or 1; any other value does not skip.
 *   TH3 A PASSPHRASE is 12 to 256 bytes long.
 *   TH4 A RECORD of ^ZXSEAT holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/threshold/seat.myra). */
#ifndef THRESHOLD_RULES_H
#define THRESHOLD_RULES_H

#include <stdbool.h>
#include <stdint.h>

enum { THRESHOLD_OWNER = 1, THRESHOLD_PERSON = 2, THRESHOLD_GUEST = 3 };
enum { THRESHOLD_LOCK_NONE = 0, THRESHOLD_LOCK_PASS = 1, THRESHOLD_LOCK_PASSKEY = 2 };

#define THRESHOLD_PASS_MIN 12
#define THRESHOLD_PASS_MAX 256 /* zxvd's SEAT_PASS_MAX (kernel/src/seats/seats.h) */

/* TH1: may a seat of `role` change the lock of a seat of role `target`
 * (own: 1 when that seat is its own)? */
static inline bool threshold_rights(int64_t role, int64_t own, int64_t target)
{
    if (target < THRESHOLD_OWNER || target > THRESHOLD_PERSON || own < 0 || own > 1) return false;
    return role == THRESHOLD_OWNER || (role == THRESHOLD_PERSON && own == 1);
}

/* TH2: is the login skipped? */
static inline bool threshold_skip(int64_t seats, int64_t lock, int64_t auto_on)
{
    return seats == 1 && lock == THRESHOLD_LOCK_NONE && auto_on == 1;
}

/* TH3: may a passphrase be this long? */
static inline bool threshold_phrase_ok(int64_t length)
{
    return length >= THRESHOLD_PASS_MIN && length <= THRESHOLD_PASS_MAX;
}

/* TH4: does x have at most `digits` digits, sign aside? */
static inline bool threshold_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool threshold_seat_fits(int64_t seat, int64_t role, int64_t lock, int64_t color,
                                       int64_t wait)
{
    return threshold_digits_fit(seat, 4) && threshold_digits_fit(role, 1) &&
           threshold_digits_fit(lock, 1) && threshold_digits_fit(color, 10) &&
           threshold_digits_fit(wait, 7);
}

#endif
