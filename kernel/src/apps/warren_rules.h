/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warren_rules.h — Warren's rules in C, the reference its Sutra, KRON, Eve,
 * Hatchling (BASIC) and MYRA blocks are checked against (blocks/warren).
 * Pure integer functions, no state: the model (warren_model.c) and the
 * language test (tests/warren_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   WR1 A FLAG is 0 (no) or 1 (yes). Anything else is no flag, and a rule
 *       never reads it as yes.
 *   WR2 A SWITCH MAY CHANGE NOW (change_ok) when the session is the
 *       owner's (owner 1) and, when the change turns a regulated engine on
 *       (regulated 1, enable 1), the owner approved it (approved 1).
 *       Turning any engine off, or a plain one on, needs only the owner's
 *       session. A flag that is no flag: not allowed.
 *   WR3 THE OWNER'S APPROVAL (approval_ok), the half of WR2 a regulated
 *       engine turned on must pass: an owner session (1) and the owner's
 *       approval (1), both exactly.
 *   WR4 A RESTART'S BACK-OFF (backoff): the wait before attempt k of
 *       `tries` is min(most, base * 2^(k - 1)) seconds, k = 1..tries;
 *       tries 1..32, base 1..3600 and most 1..86400 seconds, anything
 *       else refused.
 *   WR5 A SERVICE RECORD of ^ZXWARREN holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/warren/service.myra):
 *       engine 2, enabled 1, regulated 1, restarts 3, changed 15 (ms since
 *       1970). */
#ifndef WARREN_RULES_H
#define WARREN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define WARREN_TRIES_MAX 32
#define WARREN_BASE_MAX  3600
#define WARREN_MOST_MAX  86400
#define WARREN_REFUSED   (-1)

/* WR1 */
static inline bool warren_flag(int64_t f)
{
    return f == 0 || f == 1;
}

/* WR2 */
static inline bool warren_change_ok(int64_t owner, int64_t regulated, int64_t approved,
                                    int64_t enable)
{
    if (!warren_flag(owner) || !warren_flag(regulated) || !warren_flag(approved) ||
        !warren_flag(enable))
        return false;
    return owner == 1 && (regulated == 0 || enable == 0 || approved == 1);
}

/* WR3 */
static inline bool warren_approval_ok(int64_t owner, int64_t approved)
{
    return owner == 1 && approved == 1;
}

/* WR4: the wait before attempt k (1-based); WARREN_REFUSED outside the
 * domain (k past tries included). */
static inline int64_t warren_backoff(int64_t k, int64_t tries, int64_t base, int64_t most)
{
    if (tries < 1 || tries > WARREN_TRIES_MAX || base < 1 || base > WARREN_BASE_MAX || most < 1 ||
        most > WARREN_MOST_MAX || k < 1 || k > tries)
        return WARREN_REFUSED;
    int64_t w = base; /* base * 2^31 fits int64: no cap needed before most */
    for (int64_t i = 1; i < k && w < most; i++) w *= 2;
    return w < most ? w : most;
}

/* WR5: does x have at most `digits` digits, sign aside? */
static inline bool warren_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool warren_record_fits(int64_t engine, int64_t enabled, int64_t regulated,
                                      int64_t restarts, int64_t changed)
{
    return warren_digits_fit(engine, 2) && warren_digits_fit(enabled, 1) &&
           warren_digits_fit(regulated, 1) && warren_digits_fit(restarts, 3) &&
           warren_digits_fit(changed, 15);
}

/* banker.share (share_of_whole), as the model works it out for each
 * consumer's energy: floor(10000 * part / whole), a whole of 0 counted as
 * 1; WARREN_REFUSED for a negative part, a part past the whole, or a
 * product past 64 bits. */
static inline int64_t warren_share_x100(int64_t part, int64_t whole)
{
    if (whole < 1) whole = 1;
    if (part < 0 || part > whole || part > INT64_MAX / 10000) return WARREN_REFUSED;
    return part * 10000 / whole;
}

#endif
