/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tideturn_rules.h — Tideturn's rules in C, the reference its Sutra, KRON,
 * Eve and MYRA blocks are checked against (blocks/tideturn). Pure integer
 * functions, no state: the model (tideturn_model.c) and the language test
 * (tests/tideturn_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   TR1 VERSION ORDER: two versions major.minor.patch, each part 0..999
 *       (zxvd's update manifest, zx_upcheck.h), compared part by part from
 *       the major: 1 when a is newer than b, 0 when they are the same, -1
 *       when a is older. A part outside 0..999 is no version: refused
 *       (TIDETURN_REFUSED).
 *   TR2 DUAL SIGNATURE: an update is dual-signed when both signatures were
 *       checked and verified: each verify result is exactly 1. Anything
 *       else (0 failed, any other value no result at all) is not.
 *   TR3 A RECORD of ^ZXTIDE holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/tideturn/record.myra): seq 9,
 *       major, minor and patch 3 each, signed 1, outcome 1, stamp 15 (ms
 *       since 1970). */
#ifndef TIDETURN_RULES_H
#define TIDETURN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "witness_rules.h"

#define TIDETURN_PART_MAX  INT64_C(999)             /* one part of a version: Sutra 9(3) */
#define TIDETURN_SEQ_MAX   INT64_C(999999999)       /* 9 digits: MYRA i9 */
#define TIDETURN_STAMP_MAX INT64_C(999999999999999) /* 15 digits of ms */
#define TIDETURN_REFUSED   INT64_MIN

static inline bool tideturn_part_ok(int64_t p)
{
    return p >= 0 && p <= TIDETURN_PART_MAX;
}

/* TR1: -1, 0 or 1 as a is older, the same or newer than b. */
static inline int64_t tideturn_version_order(int64_t amajor, int64_t aminor, int64_t apatch,
                                             int64_t bmajor, int64_t bminor, int64_t bpatch)
{
    if (!tideturn_part_ok(amajor) || !tideturn_part_ok(aminor) || !tideturn_part_ok(apatch) ||
        !tideturn_part_ok(bmajor) || !tideturn_part_ok(bminor) || !tideturn_part_ok(bpatch))
        return TIDETURN_REFUSED;
    if (amajor != bmajor) return amajor > bmajor ? 1 : -1;
    if (aminor != bminor) return aminor > bminor ? 1 : -1;
    if (apatch != bpatch) return apatch > bpatch ? 1 : -1;
    return 0;
}

/* TR2: both verify results are exactly 1. */
static inline bool tideturn_dual_signed(int64_t firstsig, int64_t secondsig)
{
    return firstsig == 1 && secondsig == 1;
}

/* TR3: does the record fit ^ZXTIDE? */
static inline bool tideturn_record_fits(int64_t seq, int64_t major, int64_t minor, int64_t patch,
                                        int64_t sig, int64_t outcome, int64_t stamp)
{
    return witness_digits_fit(seq, 9) && witness_digits_fit(major, 3) &&
           witness_digits_fit(minor, 3) && witness_digits_fit(patch, 3) &&
           witness_digits_fit(sig, 1) && witness_digits_fit(outcome, 1) &&
           witness_digits_fit(stamp, 15);
}

#endif
