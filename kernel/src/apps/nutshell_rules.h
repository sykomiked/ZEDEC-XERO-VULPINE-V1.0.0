/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nutshell_rules.h — Nutshell's rules in C, the reference its BASIC, KRON
 * and MYRA blocks are checked against (blocks/nutshell). Pure integer
 * functions, no state: the model (nutshell_model.c) and the language test
 * (tests/nutshell_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs. Days are days since
 * 1970-01-01, 0 .. SOLSTICE_DAY_MAX (9999-12-31).
 *
 * Leitner's boxes: a card lives in one of NUTSHELL_BOX_MAX boxes, and a
 * card in box b is shown again 2^(b - 1) days after it was answered (box 1
 * the next day, box 5 sixteen days on).
 *
 *   NS1 NEXT BOX: answered right (right 1) a card moves up one box, never
 *       past NUTSHELL_BOX_MAX; answered wrong (right 0) it goes back to box
 *       1. box 1 .. NUTSHELL_BOX_MAX and right 0 .. 1, anything else is
 *       NUTSHELL_REFUSED.
 *   NS2 DUE DAY: the day a card answered on `today` is shown again: today +
 *       2^(next box - 1), the next box by NS1. today 0 .. SOLSTICE_DAY_MAX;
 *       a due day past SOLSTICE_DAY_MAX, or any input NS1 refuses, is
 *       NUTSHELL_REFUSED.
 *   NS3 DUE COUNT: how many cards are due on `today`: those whose due day
 *       is on or before it, exact (a card is due from its due day on).
 *   NS4 A DECK RECORD of ^ZXNUTSHELL(deck) = cards ^ made ^ reviewed holds
 *       integers of at most the digits its pieces declare, sign aside: deck
 *       6, cards 4, made 7, reviewed 7 (blocks/nutshell/deck.myra).
 *   NS5 A REVIEW RECORD of ^ZXNUTCARD(deck, card) = box ^ due ^ right ^
 *       wrong: deck 6, card 4, box 1, due 7, right 6, wrong 6
 *       (blocks/nutshell/review.myra). The model's own checks are narrower
 *       (nutshell_card_valid). */
#ifndef NUTSHELL_RULES_H
#define NUTSHELL_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "solstice_rules.h"
#include "witness_rules.h"

#define NUTSHELL_REFUSED (-1)
#define NUTSHELL_BOX_MAX 5 /* boxes 1 .. 5: shown after 1, 2, 4, 8 and 16 days */

/* NS1 */
static inline int64_t nutshell_next_box(int64_t box, int64_t right)
{
    if (box < 1 || box > NUTSHELL_BOX_MAX || right < 0 || right > 1) return NUTSHELL_REFUSED;
    if (!right) return 1;
    return box < NUTSHELL_BOX_MAX ? box + 1 : NUTSHELL_BOX_MAX;
}

/* The days a card in box `box` waits: 2^(box - 1). */
static inline int64_t nutshell_box_days(int64_t box)
{
    if (box < 1 || box > NUTSHELL_BOX_MAX) return NUTSHELL_REFUSED;
    return (int64_t) 1 << (box - 1);
}

/* NS2 */
static inline int64_t nutshell_due_day(int64_t box, int64_t right, int64_t today)
{
    int64_t next = nutshell_next_box(box, right);
    if (next < 0 || today < 0 || today > SOLSTICE_DAY_MAX) return NUTSHELL_REFUSED;
    int64_t due = today + nutshell_box_days(next);
    return due > SOLSTICE_DAY_MAX ? NUTSHELL_REFUSED : due;
}

/* NS3 */
static inline int64_t nutshell_due_count(const int64_t *due, uint32_t n, int64_t today)
{
    int64_t k = 0;
    for (uint32_t i = 0; i < n; i++) k += due[i] <= today;
    return k;
}

/* NS4 */
static inline bool nutshell_deck_fits(int64_t deck, int64_t cards, int64_t made, int64_t reviewed)
{
    return witness_digits_fit(deck, 6) && witness_digits_fit(cards, 4) &&
           witness_digits_fit(made, 7) && witness_digits_fit(reviewed, 7);
}

/* NS5 */
static inline bool nutshell_review_fits(int64_t deck, int64_t card, int64_t box, int64_t due,
                                        int64_t right, int64_t wrong)
{
    return witness_digits_fit(deck, 6) && witness_digits_fit(card, 4) &&
           witness_digits_fit(box, 1) && witness_digits_fit(due, 7) &&
           witness_digits_fit(right, 6) && witness_digits_fit(wrong, 6);
}

#endif
