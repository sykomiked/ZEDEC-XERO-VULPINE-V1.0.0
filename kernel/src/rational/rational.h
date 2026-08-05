/* rational.h — WyvernEye exact rational arithmetic
 *
 * THE DIFFERENTIATOR, STATED PRECISELY
 * ------------------------------------
 * A conventional spreadsheet stores 0.1 as a binary float, which cannot
 * represent one tenth. 0.1 + 0.2 therefore does not equal 0.3, and a
 * column of a thousand currency values drifts. This module removes that
 * failure mode at the root: every value is an EXACT ratio of two
 * integers, so 1/10 is one tenth, period.
 *
 * What we may claim: exact arithmetic over the rationals — addition,
 * subtraction, multiplication and division are exact, and equality is
 * decidable.
 *
 * What we may NOT claim: exactness over the reals. sqrt(2), pi and
 * 1/3-as-a-decimal are irrational or non-terminating and no rational
 * type represents them exactly. Those operations must either stay
 * symbolic or return an explicitly-flagged approximation. This module
 * refuses to pretend otherwise.
 *
 * Overflow is DETECTED, never silent: every operation checks a 128-bit
 * intermediate and marks the result invalid rather than wrapping. A
 * spreadsheet that silently wraps is worse than one that says "too big".
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV WyvernEye slice)
 * License: SEL-3.3
 */
#ifndef ZXV_RATIONAL_H
#define ZXV_RATIONAL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t num;      /* numerator; carries the sign */
    int64_t den;      /* denominator; ALWAYS > 0 when valid */
    bool    valid;    /* false = overflow or division by zero */
} rat_t;

/* ---- construction ---- */
rat_t rat_zero(void);
rat_t rat_from_int(int64_t v);
rat_t rat_make(int64_t num, int64_t den);        /* normalizes + reduces */

/* Parse an exact decimal literal: "0.1" -> 1/10, "-12.75" -> -51/4.
 * Also accepts "3/4" ratio form. Returns invalid on malformed input. */
rat_t rat_from_string(const char *s);

/* ---- arithmetic (all exact; set valid=false on overflow / div-by-0) ---- */
rat_t rat_add(rat_t a, rat_t b);
rat_t rat_sub(rat_t a, rat_t b);
rat_t rat_mul(rat_t a, rat_t b);
rat_t rat_div(rat_t a, rat_t b);
rat_t rat_neg(rat_t a);
rat_t rat_abs(rat_t a);

/* ---- comparison (exact, no epsilon needed) ---- */
bool  rat_eq(rat_t a, rat_t b);
/* Total order: -1 / 0 / +1. Invalid values sort AFTER every valid value
 * and compare equal to each other (rat_eq is false for any invalid). */
int   rat_cmp(rat_t a, rat_t b);
bool  rat_is_zero(rat_t a);
bool  rat_is_int(rat_t a);

/* ---- rendering ----
 * rat_to_string writes an EXACT representation: a terminating decimal
 * when one exists (den's only prime factors are 2 and 5), otherwise the
 * reduced "n/d" form. It never silently rounds.
 * Return semantics (both renderers, snprintf-style): the length the full
 * output requires, excluding the NUL. If it exceeds max-1 the text is
 * truncated but still NUL-terminated, and the return value tells you. */
uint32_t rat_to_string(rat_t a, char *out, uint32_t max);

/* Render rounded to `places` decimals for DISPLAY only, using
 * round-half-away-from-zero. `places` is capped at 18 — the emitted width
 * always equals min(places, 18). `exact` reports whether the rendering
 * was lossless — the UI should surface that, not hide it. */
uint32_t rat_to_fixed(rat_t a, uint32_t places, char *out, uint32_t max,
                      bool *exact);

/* Money helper: split `amount` into `parts` EQUAL shares of exactly
 * amount/parts each; rational arithmetic makes the shares sum back to
 * amount with zero residue. NOTE: shares are equal rationals, NOT
 * minor-unit (cent) allocations — quantizing to a currency's minor unit
 * is presentation-layer work (see rat_to_fixed and its `exact` flag).
 * Returns false if parts == 0, amount is invalid, or the share would
 * overflow (in which case out[] is untouched). */
bool rat_split(rat_t amount, uint32_t parts, rat_t *out);

#endif /* ZXV_RATIONAL_H */
