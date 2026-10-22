/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* relief_rules.h — Relief's rules in C, the reference its C++, KRON,
 * Trebago, Eve and MYRA blocks are checked against (blocks/relief). Pure
 * integer functions, no state: the model (relief_model.c) and the language
 * test (tests/relief_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs. Lengths are hundredths of a
 * millimetre (a coordinate of 100000 is one metre); a turn is given by its
 * cosine and sine in 4096ths (RELIEF_ONE is 1).
 *
 *   RR1 SCREEN X. A vertex x, z turned about the vertical axis by cos,
 *       sin (xr = x * cos + z * sin, zr = z * cos - x * sin, both in
 *       4096ths), scaled by scale percent and seen in perspective from a
 *       viewer focal units in front of the screen plane: floor(scale *
 *       focal * xr / (100 * (4096 * focal + zr))), floored toward minus
 *       infinity. x, z -RELIEF_COORD_MAX .. RELIEF_COORD_MAX, cos and sin
 *       -RELIEF_ONE .. RELIEF_ONE, scale 1 .. RELIEF_SCALE_MAX, focal 1 ..
 *       RELIEF_FOCAL_MAX, and the vertex in front of the viewer (4096 *
 *       focal + zr at least 1); anything else is refused.
 *   RR2 SCREEN Y. The same turn and view for the height y, which a turn
 *       about the vertical axis keeps: floor(scale * focal * 4096 * y /
 *       (100 * (4096 * focal + zr))); y in the coordinate range, the rest
 *       as RR1. Screen x and y are from the screen's centre, y up.
 *   RR3 DEPTH LAYER OFFSET. The depth effect: a layer at depth (0 the
 *       nearest .. far the farthest) moves by floor(shift * (far - depth) /
 *       far) when the nearest layer moves by shift pixels, so the farthest
 *       stays still. shift -RELIEF_SHIFT_MAX .. RELIEF_SHIFT_MAX, far 1 ..
 *       RELIEF_FAR_MAX, depth 0 .. far; anything else is refused.
 *   RR4 MESH VOLUME. The volume a closed triangle mesh encloses, by signed
 *       tetrahedra: each face a, b, c (counter-clockwise seen from outside)
 *       adds orient * det(a, b, c) / 6, the tetrahedron it makes with the
 *       origin, exact (a reduced fraction, never rounded), in cubic
 *       hundredths of a millimetre. The corners come three to a face (x, y
 *       and z, one row a corner) and orient one to a face: 1 as wound, -1
 *       wound the other way, 0 left out. Every coordinate in the coordinate
 *       range, every orient -1 .. 1, three corners a face and at most
 *       RELIEF_FACES_MAX faces; anything else is refused.
 *   RR5 MESH LIMITS. A mesh holds 4 .. RELIEF_VERTS_MAX vertices and 4 ..
 *       RELIEF_FACES_MAX faces (a tetrahedron is the least closed mesh; a
 *       closed mesh of V vertices has 2V - 4 faces), each held to its own
 *       range.
 *   RR6 MESH RECORD. A mesh's record fits ^ZXRELIEF: mesh number at most
 *       9 digits, vertices 3, faces 4, mtime (Unix seconds) 12, sign
 *       aside. */
#ifndef RELIEF_RULES_H
#define RELIEF_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define RELIEF_COORD_MAX INT64_C(100000)
#define RELIEF_ONE       INT64_C(4096)
#define RELIEF_SCALE_MAX INT64_C(1000)
#define RELIEF_FOCAL_MAX INT64_C(1000000)
#define RELIEF_SHIFT_MAX INT64_C(100000)
#define RELIEF_FAR_MAX   INT64_C(1000000)
#define RELIEF_VERTS_MAX INT64_C(512)
#define RELIEF_FACES_MAX (2 * RELIEF_VERTS_MAX - 4)

static inline bool relief_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* n / d floored toward minus infinity; d > 0 */
static inline int64_t relief_floor_div(int64_t n, int64_t d)
{
    int64_t q = n / d;
    if (n % d != 0 && n < 0) q -= 1;
    return q;
}

/* The shared part of RR1 and RR2: the view's inputs are in range and the
 * turned vertex is in front of the viewer; *depth is 4096 * focal + zr. */
static inline bool relief_view_ok(int64_t x, int64_t z, int64_t cos, int64_t sin, int64_t scale,
                                  int64_t focal, int64_t *depth)
{
    if (!relief_in(x, -RELIEF_COORD_MAX, RELIEF_COORD_MAX) ||
        !relief_in(z, -RELIEF_COORD_MAX, RELIEF_COORD_MAX) ||
        !relief_in(cos, -RELIEF_ONE, RELIEF_ONE) || !relief_in(sin, -RELIEF_ONE, RELIEF_ONE) ||
        !relief_in(scale, 1, RELIEF_SCALE_MAX) || !relief_in(focal, 1, RELIEF_FOCAL_MAX))
        return false;
    *depth = RELIEF_ONE * focal + z * cos - x * sin;
    return *depth >= 1;
}

/* RR1 */
static inline bool relief_screen_x(int64_t x, int64_t z, int64_t cos, int64_t sin, int64_t scale,
                                   int64_t focal, int64_t *out)
{
    int64_t depth = 0;
    if (!relief_view_ok(x, z, cos, sin, scale, focal, &depth)) return false;
    *out = relief_floor_div(scale * focal * (x * cos + z * sin), 100 * depth);
    return true;
}

/* RR2 */
static inline bool relief_screen_y(int64_t x, int64_t y, int64_t z, int64_t cos, int64_t sin,
                                   int64_t scale, int64_t focal, int64_t *out)
{
    int64_t depth = 0;
    if (!relief_in(y, -RELIEF_COORD_MAX, RELIEF_COORD_MAX) ||
        !relief_view_ok(x, z, cos, sin, scale, focal, &depth))
        return false;
    *out = relief_floor_div(scale * focal * RELIEF_ONE * y, 100 * depth);
    return true;
}

/* RR3 */
static inline bool relief_depth_offset(int64_t shift, int64_t depth, int64_t far, int64_t *out)
{
    if (!relief_in(shift, -RELIEF_SHIFT_MAX, RELIEF_SHIFT_MAX) ||
        !relief_in(far, 1, RELIEF_FAR_MAX) || !relief_in(depth, 0, far))
        return false;
    *out = relief_floor_div(shift * (far - depth), far);
    return true;
}

/* RR4: one face's det(a, b, c), six times its tetrahedron's volume */
static inline int64_t relief_face_det(int64_t ax, int64_t ay, int64_t az, int64_t bx, int64_t by,
                                      int64_t bz, int64_t cx, int64_t cy, int64_t cz)
{
    return ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx);
}

/* RR4: x, y, z are the faces' corners, three rows a face (corners rows),
 * orient one row a face (faces rows); the volume as num / den, reduced,
 * den > 0. */
static inline bool relief_mesh_volume(const int64_t *x, const int64_t *y, const int64_t *z,
                                      uint32_t corners, const int64_t *orient, uint32_t faces,
                                      int64_t *num, int64_t *den)
{
    if ((int64_t) faces > RELIEF_FACES_MAX || corners != 3u * faces) return false;
    for (uint32_t i = 0; i < corners; i++)
        if (!relief_in(x[i], -RELIEF_COORD_MAX, RELIEF_COORD_MAX) ||
            !relief_in(y[i], -RELIEF_COORD_MAX, RELIEF_COORD_MAX) ||
            !relief_in(z[i], -RELIEF_COORD_MAX, RELIEF_COORD_MAX))
            return false;
    int64_t s = 0;
    for (uint32_t j = 0; j < faces; j++) {
        if (!relief_in(orient[j], -1, 1)) return false;
        uint32_t a = 3u * j, b = a + 1u, c = a + 2u;
        s += orient[j] * relief_face_det(x[a], y[a], z[a], x[b], y[b], z[b], x[c], y[c], z[c]);
    }
    int64_t d = 6, g = s < 0 ? -s : s, h = d;
    while (h) {
        int64_t t = g % h;
        g = h;
        h = t;
    }
    *num = s / g;
    *den = d / g;
    return true;
}

/* RR5 */
static inline bool relief_mesh_limits_ok(int64_t vertices, int64_t faces)
{
    return relief_in(vertices, 4, RELIEF_VERTS_MAX) && relief_in(faces, 4, RELIEF_FACES_MAX);
}

/* RR6: |v| has fewer than `digits` + 1 decimal digits (MYRA's iN) */
static inline bool relief_fits_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

static inline bool relief_mesh_record_ok(int64_t mesh, int64_t vertices, int64_t faces,
                                         int64_t mtime)
{
    return relief_fits_digits(mesh, 9) && relief_fits_digits(vertices, 3) &&
           relief_fits_digits(faces, 4) && relief_fits_digits(mtime, 12);
}

#endif
