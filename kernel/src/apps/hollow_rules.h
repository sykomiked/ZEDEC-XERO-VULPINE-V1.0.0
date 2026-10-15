/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hollow_rules.h — Hollow's rules in C, the reference its KRON, C++
 * (WebAssembly), Sutra, Eve and MYRA blocks are checked against
 * (blocks/hollow). Pure integer functions, no state: the model
 * (hollow_model.c) and the language test (tests/hollow_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. A refused input answers HOLLOW_REFUSED (-1), which no answer is.
 *
 *   HR1 COLUMNS of a grid for n windows, n 0..HOLLOW_TILE_MAX: the least
 *       c with c * c >= n (0 for none). Anything else is refused.
 *   HR2 ROWS of that grid: n over its columns, rounded up (0 for none).
 *   HR3 A CELL along one side: floor(span / parts), span 0..HOLLOW_PX_MAX
 *       pixels, parts 1..HOLLOW_TILE_MAX. Anything else is refused.
 *   HR4 SHOWN: how many pixels of a segment pos .. pos + size - 1 lie on a
 *       screen side 0 .. span - 1: max(0, min(pos + size, span) - max(pos,
 *       0)). pos fits s9(5), size and span 9(5) (Sutra's pictures);
 *       anything else is refused.
 *   HR5 A WINDOW may be: width HOLLOW_MIN_W..HOLLOW_PX_MAX, height
 *       HOLLOW_MIN_H..HOLLOW_PX_MAX, and at least HOLLOW_SEEN_MIN pixels of
 *       it shown across and down (HR4), at most HOLLOW_PX_MAX
 *       (blocks/hollow/window.eve).
 *   HR6 A DOCK RECORD of ^ZXDOCK holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/hollow/dock.myra): place 2, app
 *       3, pinned 1, added 12. */
#ifndef HOLLOW_RULES_H
#define HOLLOW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define HOLLOW_REFUSED  (-1)
#define HOLLOW_TILE_MAX 64    /* windows tiled at once: an 8 by 8 grid */
#define HOLLOW_PX_MAX   16384 /* the widest screen or window side, pixels */
#define HOLLOW_MIN_W    160   /* the narrowest window */
#define HOLLOW_MIN_H    100   /* the shortest window */
#define HOLLOW_SEEN_MIN 32    /* pixels of a window always left on screen */
#define HOLLOW_PIC_MAX  99999 /* Sutra's 9(5) */

/* HR1 */
static inline int64_t hollow_tile_cols(int64_t n)
{
    if (n < 0 || n > HOLLOW_TILE_MAX) return HOLLOW_REFUSED;
    int64_t c = 0;
    while (c * c < n) c++;
    return c;
}

/* HR2 */
static inline int64_t hollow_tile_rows(int64_t n)
{
    int64_t c = hollow_tile_cols(n);
    if (c <= 0) return c;
    return (n + c - 1) / c;
}

/* HR3 */
static inline int64_t hollow_tile_cell(int64_t span, int64_t parts)
{
    if (span < 0 || span > HOLLOW_PX_MAX || parts < 1 || parts > HOLLOW_TILE_MAX)
        return HOLLOW_REFUSED;
    return span / parts;
}

/* HR4 */
static inline int64_t hollow_shown(int64_t pos, int64_t size, int64_t span)
{
    if (pos < -HOLLOW_PIC_MAX || pos > HOLLOW_PIC_MAX || size < 0 || size > HOLLOW_PIC_MAX ||
        span < 0 || span > HOLLOW_PIC_MAX)
        return HOLLOW_REFUSED;
    int64_t lo = pos > 0 ? pos : 0;
    int64_t hi = pos + size < span ? pos + size : span;
    return hi > lo ? hi - lo : 0;
}

/* HR5 */
static inline bool hollow_window_fits(int64_t width, int64_t height, int64_t across, int64_t down)
{
    return width >= HOLLOW_MIN_W && width <= HOLLOW_PX_MAX && height >= HOLLOW_MIN_H &&
           height <= HOLLOW_PX_MAX && across >= HOLLOW_SEEN_MIN && across <= HOLLOW_PX_MAX &&
           down >= HOLLOW_SEEN_MIN && down <= HOLLOW_PX_MAX;
}

/* HR6: does x have at most `digits` digits, sign aside? */
static inline bool hollow_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool hollow_dock_fits(int64_t place, int64_t app, int64_t pinned, int64_t added)
{
    return hollow_digits_fit(place, 2) && hollow_digits_fit(app, 3) &&
           hollow_digits_fit(pinned, 1) && hollow_digits_fit(added, 12);
}

#endif
