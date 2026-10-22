/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gnomon_rules.h — Gnomon's rules in C, the reference its KRON, Trebago,
 * C++ and MYRA blocks are checked against (blocks/gnomon). Pure integer
 * functions, no state: the model (gnomon_model.c) and the language test
 * (tests/gnomon_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Coordinates are integers in any one
 * unit (a grid square, a millimetre), y up.
 *   GR1 POLYGON AREA, TWICE. The shoelace sum of a polygon's vertices in
 *       order, the last joining the first: the sum over every side of
 *       x(i) * y(i + 1) - x(i + 1) * y(i), exact. Counter-clockwise is
 *       positive, clockwise negative; for a polygon that crosses itself it
 *       is the sum of each part's area times how often the outline winds
 *       round it. 3 .. GNOMON_VERTS_MAX vertices, every coordinate inside
 *       -GNOMON_COORD_MAX .. GNOMON_COORD_MAX; anything else is refused.
 *   GR2 POLYGON AREA. GR1 halved, an exact reduced fraction (num / den,
 *       den 1 or 2), never rounded; the same domain.
 *   GR3 TRIANGLE AREA, TWICE. GR1 for three corners a, b, c, written as
 *       the cross product of the sides a to b and a to c: (bx - ax) * (cy -
 *       ay) - (by - ay) * (cx - ax): positive for a left turn, negative for
 *       a right turn, 0 on one line. Every coordinate in range.
 *   GR4 SQUARED SIDE LENGTH. The squared distance from a to b, (bx - ax)^2
 *       + (by - ay)^2, exact, no root taken. Every coordinate in range.
 *   GR5 FIGURE RECORD. A saved figure's record fits ^ZXGNOMON: figure
 *       number at most 9 digits, vertices 4, kind 1, mtime (Unix seconds)
 *       12, sign aside.
 * The widest answer is GR1 at the limits: 1024 sides of at most 2 * 10^12
 * each, 2^51 or so, well inside int64; nothing here can overflow. */
#ifndef GNOMON_RULES_H
#define GNOMON_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "../rational/rational.h"

#define GNOMON_COORD_MAX INT64_C(1000000)
#define GNOMON_VERTS_MAX 1024u

static inline bool gnomon_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

static inline bool gnomon_coord_ok(int64_t v)
{
    return gnomon_in(v, -GNOMON_COORD_MAX, GNOMON_COORD_MAX);
}

/* GR1: x, y are the vertices in order, n of them */
static inline bool gnomon_polygon_area2(const int64_t *x, const int64_t *y, uint32_t n,
                                        int64_t *out)
{
    if (!x || !y || n < 3u || n > GNOMON_VERTS_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!gnomon_coord_ok(x[i]) || !gnomon_coord_ok(y[i])) return false;
    int64_t s = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = i + 1u == n ? 0u : i + 1u;
        s += x[i] * y[j] - x[j] * y[i];
    }
    *out = s;
    return true;
}

/* GR2: reduced by kernel/src/rational (rat_make), den 1 or 2 */
static inline bool gnomon_polygon_area(const int64_t *x, const int64_t *y, uint32_t n, int64_t *num,
                                       int64_t *den)
{
    int64_t twice = 0;
    if (!gnomon_polygon_area2(x, y, n, &twice)) return false;
    rat_t a = rat_make(twice, 2);
    if (!a.valid) return false;
    *num = a.num;
    *den = a.den;
    return true;
}

/* GR3 */
static inline bool gnomon_triangle_area2(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t cx,
                                         int64_t cy, int64_t *out)
{
    if (!gnomon_coord_ok(ax) || !gnomon_coord_ok(ay) || !gnomon_coord_ok(bx) ||
        !gnomon_coord_ok(by) || !gnomon_coord_ok(cx) || !gnomon_coord_ok(cy))
        return false;
    *out = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    return true;
}

/* GR4 */
static inline bool gnomon_side_len_sq(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t *out)
{
    if (!gnomon_coord_ok(ax) || !gnomon_coord_ok(ay) || !gnomon_coord_ok(bx) ||
        !gnomon_coord_ok(by))
        return false;
    int64_t dx = bx - ax, dy = by - ay;
    *out = dx * dx + dy * dy;
    return true;
}

/* GR5: |v| has fewer than `digits` + 1 decimal digits (MYRA's iN) */
static inline bool gnomon_fits_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

static inline bool gnomon_figure_record_ok(int64_t figure, int64_t vertices, int64_t kind,
                                           int64_t mtime)
{
    return gnomon_fits_digits(figure, 9) && gnomon_fits_digits(vertices, 4) &&
           gnomon_fits_digits(kind, 1) && gnomon_fits_digits(mtime, 12);
}

#endif
