/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* witness_rules.h — Witness's rules in C, the reference its KRON, Sutra,
 * Trebago and MYRA blocks are checked against (blocks/witness). Pure
 * integer functions, no state: the model (witness_model.c) and the
 * language test (tests/witness_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   WR1 A LINK: an entry of the record is linked to the one before it when
 *       its prev index equals that entry's index (last) and its own index
 *       is past it. The first entry's last is 0, the record's start. A
 *       chain is intact when every entry is linked (chain_ok).
 *   WR2 A COUNT BY KIND: how many entries of a kind vector equal `want`
 *       (kind_count), exact.
 *   WR3 A RECORD of ^ZXWIT holds integers of at most the digits its pieces
 *       declare, sign aside (blocks/witness/entry.myra): seq and prev 18,
 *       kind 1, stamp 15 (ms since 1970 until the year 33658). */
#ifndef WITNESS_RULES_H
#define WITNESS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define WITNESS_SEQ_MAX   INT64_C(999999999999999999) /* 18 digits: Sutra 9(18), MYRA i18 */
#define WITNESS_STAMP_MAX INT64_C(999999999999999)    /* 15 digits of ms */

/* WR1: is this entry linked to the one before it? */
static inline bool witness_linked(int64_t index, int64_t prev, int64_t last)
{
    return prev == last && index > last;
}

/* WR2: how many of the n kinds are `want` (0 when kind is NULL). */
static inline int64_t witness_kind_count(const int64_t *kind, uint32_t n, int64_t want)
{
    int64_t c = 0;
    if (!kind) return 0;
    for (uint32_t i = 0; i < n; i++) c += kind[i] == want;
    return c;
}

/* WR3: does x have at most `digits` digits, sign aside? */
static inline bool witness_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool witness_record_fits(int64_t seq, int64_t prev, int64_t kind, int64_t stamp)
{
    return witness_digits_fit(seq, 18) && witness_digits_fit(prev, 18) &&
           witness_digits_fit(kind, 1) && witness_digits_fit(stamp, 15);
}

#endif
