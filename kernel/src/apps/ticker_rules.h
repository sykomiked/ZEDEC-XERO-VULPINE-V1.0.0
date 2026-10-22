/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ticker_rules.h — Ticker's rules in C, the reference its Sutra, KRON, Eve
 * and MYRA blocks are checked against (blocks/ticker). Pure integer
 * functions, no state: the model (ticker_model.c) and the language test
 * (tests/ticker_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   TK1 LEAST TIME: a caption of `chars` characters stays at least
 *       max(1000, 60 * chars) milliseconds; chars 0..9999, anything else
 *       refused (-1).
 *   TK2 PACE: the milliseconds each character of a caption has had on
 *       screen, floor(shown / chars); shown 0..999999999 ms, chars
 *       1..9999, anything else refused (-1).
 *   TK3 LONG ENOUGH: a caption may leave the screen when it has been
 *       shown 1000..999999999 ms at a pace of 60..999999999 ms per
 *       character (TK2), and it is laid out in 1..2 lines whose longest
 *       holds 1..42 characters. Each field against its own range, so Eve
 *       holds it as KRON does. pace >= 60 is shown >= 60 * chars exactly,
 *       since 60 is whole: floor(s / c) >= 60 when and only when s >= 60c.
 *   TK4 A RECORD of ^ZXTICKER holds integers of at most the digits its
 *       pieces declare, sign aside: at 15 (ms since 1970), speaker 3,
 *       lang 3 (the source language's place in xlate.languages, 0 not
 *       known), chars 4 (blocks/ticker/caption.myra). */
#ifndef TICKER_RULES_H
#define TICKER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TICKER_LEAST_MS    1000      /* TK1: never shorter than a second */
#define TICKER_MS_PER_CHAR 60        /* TK1, TK3 */
#define TICKER_LINE_CHARS  42        /* TK3: characters on one line */
#define TICKER_LINES       2         /* TK3: lines on one caption */
#define TICKER_CHARS_MAX   9999      /* TK1, TK2: Sutra's 9(4) */
#define TICKER_SHOWN_MAX   999999999 /* TK2, TK3: Sutra's 9(9) */
#define TICKER_PACE_MAX    999999999 /* TK3 */
#define TICKER_REFUSED     (-1)

/* TK1: how long a caption of `chars` characters stays at least */
static inline int64_t ticker_least_ms(int64_t chars)
{
    if (chars < 0 || chars > TICKER_CHARS_MAX) return TICKER_REFUSED;
    int64_t ms = chars * TICKER_MS_PER_CHAR;
    return ms < TICKER_LEAST_MS ? TICKER_LEAST_MS : ms;
}

/* TK2: milliseconds per character so far */
static inline int64_t ticker_pace(int64_t shown, int64_t chars)
{
    if (shown < 0 || shown > TICKER_SHOWN_MAX || chars < 1 || chars > TICKER_CHARS_MAX)
        return TICKER_REFUSED;
    return shown / chars;
}

/* TK3: has a caption stayed long enough, and does it fit? */
static inline bool ticker_hold_ok(int64_t shown, int64_t pace, int64_t lines, int64_t longest)
{
    return shown >= TICKER_LEAST_MS && shown <= TICKER_SHOWN_MAX && pace >= TICKER_MS_PER_CHAR &&
           pace <= TICKER_PACE_MAX && lines >= 1 && lines <= TICKER_LINES && longest >= 1 &&
           longest <= TICKER_LINE_CHARS;
}

/* TK4: does x have at most `digits` digits, sign aside? */
static inline bool ticker_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool ticker_record_fits(int64_t at, int64_t speaker, int64_t lang, int64_t chars)
{
    return ticker_digits_fit(at, 15) && ticker_digits_fit(speaker, 3) &&
           ticker_digits_fit(lang, 3) && ticker_digits_fit(chars, 4);
}

#endif
