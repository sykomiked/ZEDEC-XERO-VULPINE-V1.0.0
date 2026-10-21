/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lockbox_rules.h — Lockbox's rules in C, the reference its Sutra, KRON,
 * C++ (WebAssembly) and Eve blocks are checked against (blocks/lockbox).
 * Pure integer functions, no state: the model (lockbox_model.c) and the
 * language test (tests/lockbox_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 * A saved password is described by three counts, never by its bytes:
 * length (bytes of UTF-8), classes (how many of lower case, upper case,
 * digits and symbols it holds) and repeats (how many bytes equal the byte
 * before them).
 *
 *   LR1 STRENGTH, a score 0..100: max(0, 3 * min(length, 20)
 *       + 10 * min(classes, 4) - 5 * repeats). Length counts up to 20
 *       bytes (60 points), classes up to 4 (40 points), and every repeat
 *       takes 5 away. The domain is Sutra's pictures: length and repeats
 *       0..LOCKBOX_COUNT_MAX, classes 0..LOCKBOX_CLASS_MAX; anything else
 *       is refused (LOCKBOX_REFUSED). Advice only: the owner decides.
 *   LR2 DAYS LEFT before a key expires at `expires` (seconds since 1970)
 *       seen at `now`: floor((expires - now) / 86400), never below 0 (0
 *       is due today or past); both 0..LOCKBOX_STAMP_MAX (the last second
 *       of year 9999), else refused.
 *   LR3 A SAVED PASSWORD INSIDE LOCKBOX'S LIMITS: length
 *       LOCKBOX_LEN_MIN..LOCKBOX_LEN_MAX bytes, classes 1..4 and repeats
 *       0..LOCKBOX_LEN_MAX - 1 (a byte can only repeat the one before it). */
#ifndef LOCKBOX_RULES_H
#define LOCKBOX_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define LOCKBOX_REFUSED   (-1)
#define LOCKBOX_COUNT_MAX 9999 /* Sutra's 9(4) */
#define LOCKBOX_CLASS_MAX 9    /* Sutra's 9 */
#define LOCKBOX_LEN_MIN   12
#define LOCKBOX_LEN_MAX   1024
#define LOCKBOX_CLASSES   4 /* lower, upper, digit, symbol */
#define LOCKBOX_STAMP_MAX INT64_C(253402300799)
#define LOCKBOX_DAY       86400

/* LR1 */
static inline int64_t lockbox_strength(int64_t length, int64_t classes, int64_t repeats)
{
    if (length < 0 || length > LOCKBOX_COUNT_MAX || classes < 0 || classes > LOCKBOX_CLASS_MAX ||
        repeats < 0 || repeats > LOCKBOX_COUNT_MAX)
        return LOCKBOX_REFUSED;
    int64_t points = 3 * (length < 20 ? length : 20);
    points += 10 * (classes < LOCKBOX_CLASSES ? classes : LOCKBOX_CLASSES);
    points -= 5 * repeats;
    return points > 0 ? points : 0;
}

/* LR2 */
static inline int64_t lockbox_days_left(int64_t expires, int64_t now)
{
    if (expires < 0 || expires > LOCKBOX_STAMP_MAX || now < 0 || now > LOCKBOX_STAMP_MAX)
        return LOCKBOX_REFUSED;
    return expires > now ? (expires - now) / LOCKBOX_DAY : 0;
}

/* LR3 */
static inline bool lockbox_secret_ok(int64_t length, int64_t classes, int64_t repeats)
{
    return length >= LOCKBOX_LEN_MIN && length <= LOCKBOX_LEN_MAX && classes >= 1 &&
           classes <= LOCKBOX_CLASSES && repeats >= 0 && repeats <= LOCKBOX_LEN_MAX - 1;
}

#endif
