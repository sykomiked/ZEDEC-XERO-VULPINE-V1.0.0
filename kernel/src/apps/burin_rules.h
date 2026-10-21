/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burin_rules.h — Burin's rules in C, the reference its C++, KRON, Trebago,
 * Eve and MYRA blocks are checked against (blocks/burin). Pure integer
 * functions, no state: the model (burin_model.c) and the language test
 * (tests/burin_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Lengths are hundredths of a point
 * (a point is 1/72 inch), so an A4 page is 59500 by 84200.
 *
 *   BR1 A POINT ON A CUBIC CURVE. One coordinate of the cubic Bezier
 *       p0 p1 p2 p3 at t = step / steps, by de Casteljau in fixed point:
 *       with u = steps - step, three rounds of a * u + b * step (each
 *       round keeps its scale, nothing is divided) give the point times
 *       steps^3, which is then floored toward minus infinity. Control
 *       points -BURIN_COORD_MAX .. BURIN_COORD_MAX, steps 1 ..
 *       BURIN_STEPS_MAX, step 0 .. steps; anything else BURIN_REFUSED,
 *       which no point can be (a point stays inside its control points).
 *   BR2 HATCH LINES AT A SPACING. Lines at gap, 2 * gap, ... strictly
 *       inside a span, so neither edge is cut twice: floor((span - 1) /
 *       gap), none for a span of 0. span 0 .. BURIN_SPAN_MAX, gap 1 ..
 *       BURIN_SPAN_MAX; anything else -1.
 *   BR3 HATCH SPACING FOR A COUNT. Lines lines set evenly across a span,
 *       both edges uncut: line k (1 .. lines) at k * span / (lines + 1),
 *       the spacing span / (lines + 1) exact (a fraction, never rounded).
 *       span 0 .. BURIN_SPAN_MAX, lines 1 .. BURIN_HATCH_MAX. BR2 at that
 *       spacing gives back lines when it is whole.
 *   BR4 PATH LIMITS. A path holds 2 .. BURIN_POINTS_MAX points, on a page
 *       1 .. BURIN_PAGE_MAX on each side, cut with a stroke 1 ..
 *       BURIN_STROKE_MAX wide.
 *   BR5 PATH RECORD. A path's record fits ^ZXBURIN: path number at most 9
 *       digits, points 3, stroke 5, mtime (Unix seconds) 12, sign aside. */
#ifndef BURIN_RULES_H
#define BURIN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define BURIN_COORD_MAX  INT64_C(1000000)
#define BURIN_STEPS_MAX  INT64_C(1024)
#define BURIN_REFUSED    (-BURIN_COORD_MAX - 1)
#define BURIN_SPAN_MAX   INT64_C(9999999)
#define BURIN_HATCH_MAX  INT64_C(9999)
#define BURIN_SEGS       256u                           /* segments in one path */
#define BURIN_POINTS_MAX (1 + 3 * (int64_t) BURIN_SEGS) /* a start and 256 cubics */
#define BURIN_PAGE_MAX   INT64_C(1000000)
#define BURIN_STROKE_MAX INT64_C(10000)

/* BR1 */
static inline int64_t burin_bezier_at(int64_t p0, int64_t p1, int64_t p2, int64_t p3, int64_t step,
                                      int64_t steps)
{
    const int64_t p[4] = {p0, p1, p2, p3};
    for (uint32_t i = 0; i < 4; i++)
        if (p[i] < -BURIN_COORD_MAX || p[i] > BURIN_COORD_MAX) return BURIN_REFUSED;
    if (steps < 1 || steps > BURIN_STEPS_MAX || step < 0 || step > steps) return BURIN_REFUSED;
    int64_t u = steps - step, q[4] = {p0, p1, p2, p3};
    for (uint32_t round = 0; round < 3; round++)
        for (uint32_t i = 0; i + 1 < 4 - round; i++) q[i] = q[i] * u + q[i + 1] * step;
    int64_t n3 = steps * steps * steps, f = q[0] / n3;
    if (q[0] % n3 != 0 && q[0] < 0) f -= 1;
    return f;
}

/* BR2 */
static inline int64_t burin_hatch_lines(int64_t span, int64_t gap)
{
    if (span < 0 || span > BURIN_SPAN_MAX || gap < 1 || gap > BURIN_SPAN_MAX) return -1;
    return span ? (span - 1) / gap : 0;
}

/* BR3: the spacing as num / den, not reduced (den = lines + 1). */
static inline bool burin_hatch_gap(int64_t span, int64_t lines, int64_t *num, int64_t *den)
{
    if (span < 0 || span > BURIN_SPAN_MAX || lines < 1 || lines > BURIN_HATCH_MAX) return false;
    *num = span;
    *den = lines + 1;
    return true;
}

/* BR4 */
static inline bool burin_path_limits_ok(int64_t points, int64_t width, int64_t height,
                                        int64_t stroke)
{
    return points >= 2 && points <= BURIN_POINTS_MAX && width >= 1 && width <= BURIN_PAGE_MAX &&
           height >= 1 && height <= BURIN_PAGE_MAX && stroke >= 1 && stroke <= BURIN_STROKE_MAX;
}

/* BR5: |v| has fewer than `digits` + 1 decimal digits (MYRA's iN) */
static inline bool burin_fits_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

static inline bool burin_record_ok(int64_t path, int64_t points, int64_t stroke, int64_t mtime)
{
    return burin_fits_digits(path, 9) && burin_fits_digits(points, 3) &&
           burin_fits_digits(stroke, 5) && burin_fits_digits(mtime, 12);
}

#endif
