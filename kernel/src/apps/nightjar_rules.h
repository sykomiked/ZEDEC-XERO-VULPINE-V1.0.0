/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nightjar_rules.h — Nightjar's rules in C, the reference its Eve, KRON
 * and MYRA blocks are checked against (blocks/nightjar). Pure integer
 * functions, no state: the model (nightjar_model.c) and the language test
 * (tests/nightjar_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   NR1 CUT IN: an alert (kind 1) or a change of focus (kind 2) cuts in on
 *       what is being said; page reading (kind 3) waits its turn. Any
 *       other kind is no kind and never cuts in (Eve holds it to 1..2;
 *       KRON reads every kind below 3 as cutting in, so 0 and below are
 *       held).
 *   NR2 VOICE: a rate of 80..450 words a minute and a pitch of 50..200
 *       (hundredths of the voice's own), each against its own range.
 *   NR3 ELEMENT RECORD: an element fits ^ZXNIGHTJAR(element) = role ^
 *       namelen ^ row ^ column ^ state, each an integer of at most the
 *       digits its piece declares (3; 1, 2, 4, 4, 1), sign aside.
 *   NR4 READING ORDER: by row, then column. An element's place is 1 + the
 *       number of elements strictly before it, so two at one spot share a
 *       place. Row and column 0..999999 (the KRON block orders by the key
 *       row * 1000000 + column, which is exact on that range). */
#ifndef NIGHTJAR_RULES_H
#define NIGHTJAR_RULES_H

#include <stdbool.h>
#include <stdint.h>

enum { NIGHTJAR_ALERT = 1, NIGHTJAR_FOCUS = 2, NIGHTJAR_PAGE = 3 };

#define NIGHTJAR_WPM_MIN   80
#define NIGHTJAR_WPM_MAX   450
#define NIGHTJAR_PITCH_MIN 50
#define NIGHTJAR_PITCH_MAX 200
#define NIGHTJAR_POS_MAX   INT64_C(999999) /* NR4: a row or column */

/* NR1 */
static inline bool nightjar_cuts_in(int64_t kind)
{
    return kind == NIGHTJAR_ALERT || kind == NIGHTJAR_FOCUS;
}

/* NR2 */
static inline bool nightjar_voice_ok(int64_t wpm, int64_t pitch)
{
    return wpm >= NIGHTJAR_WPM_MIN && wpm <= NIGHTJAR_WPM_MAX && pitch >= NIGHTJAR_PITCH_MIN &&
           pitch <= NIGHTJAR_PITCH_MAX;
}

/* NR3: does x have at most `digits` digits, sign aside? */
static inline bool nightjar_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool nightjar_element_fits(int64_t element, int64_t role, int64_t namelen,
                                         int64_t row, int64_t column, int64_t state)
{
    return nightjar_digits_fit(element, 3) && nightjar_digits_fit(role, 1) &&
           nightjar_digits_fit(namelen, 2) && nightjar_digits_fit(row, 4) &&
           nightjar_digits_fit(column, 4) && nightjar_digits_fit(state, 1);
}

/* NR4: is (row a, column b) strictly before (row c, column d)? */
static inline bool nightjar_before(int64_t a, int64_t b, int64_t c, int64_t d)
{
    return a < c || (a == c && b < d);
}

/* NR4: the place of element i of n (row and column vectors); 0 when i is
 * not one of them. */
static inline int64_t nightjar_reading_place(const int64_t *row, const int64_t *column, uint32_t n,
                                             uint32_t i)
{
    if (!row || !column || i >= n) return 0;
    int64_t place = 1;
    for (uint32_t j = 0; j < n; j++) place += nightjar_before(row[j], column[j], row[i], column[i]);
    return place;
}

#endif
