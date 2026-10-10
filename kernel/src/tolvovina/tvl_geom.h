/* tvl_geom.h — TOL VOVINA UPAAH LOT: the ORIENTATION BASIS.
 *
 * WHAT THIS IS
 * ------------
 * This is the file that makes "the geometry engine orients the graphics"
 * literally true rather than thematic. A root system is a set of vectors closed
 * under reflection — that is, a canonical ORIENTATION FRAME — and this module
 * DERIVES that frame from the E8/icosian construction already in the tree
 * (kernel/src/e8) instead of tabling a set of decimal direction vectors.
 *
 * Concretely: the 120 icosians are the vertices of the 600-cell, held as unit
 * quaternions with coordinates in ℤ[φ] and scaled by 2 (e8.h:78-83). Classify
 * them by their REAL part and the imaginary parts fall into exactly the three
 * icosahedral axis systems of 3-space:
 *
 *   real part  φ−1  ->  12 directions,  6 five-fold axes   |v|² = 2 + φ
 *                       these are the 12 vertices of the icosahedron, i.e. the
 *                       permutations of (0, ±1, ±φ) — derived, not tabled.
 *   real part   1   ->  20 directions, 10 three-fold axes  |v|² = 3
 *                       the 20 vertices of the dual dodecahedron.
 *   real part   0   ->  30 directions, 15 two-fold axes    |v|² = 4
 *                       the 30 vertices of the icosidodecahedron / edge midpoints.
 *
 * 6 + 10 + 15 = 31 axes, and 1 + 6·4 + 10·2 + 15·1 = 60 = |I|, the order of the
 * icosahedral rotation group. Those two counts are checked, not asserted.
 *
 * EXACTNESS, AND WHY IT IS NOT OPTIONAL
 * -------------------------------------
 * Every direction here lives in ℤ[φ]: two machine integers per coordinate, no
 * rounding anywhere. That matters for three separate reasons, all of them the
 * owner's requirements rather than taste:
 *   - the AI REPLAYS runs, so orientation must be bit-identical run to run;
 *   - dimfold requires losslessness, and a drifting frame is not lossless;
 *   - a rotated direction must still BE a frame direction. With floats it is
 *     "close to" one, which is not a question with an answer.
 * `tvl_orient` therefore rotates by the symmetry group EXACTLY and reversibly:
 * rotate by q, rotate back by tvl_rot_inverse(q), and the ℤ[φ] pair returned is
 * identical, not approximately equal.
 *
 * WHERE THE EXACTNESS STOPS — READ THIS BEFORE PROJECTING
 * ------------------------------------------------------
 * A screen pixel is an integer, so somewhere the ring has to be left. ℤ[φ] has
 * no division and no ordering (zphi.h:80-102), so it cannot express a
 * perspective divide. The split is at the projection boundary and NOWHERE else:
 * model-space directions, the axis frames and every rotation stay in exact
 * ℤ[φ]; `tvl_project` converts ONCE per direction into Q32.32 and does the
 * divide there. That conversion is integer, deterministic and bit-identical
 * across runs and architectures. `zphi_to_double` never appears in this module.
 *
 * NO FIXED VALUES. `tvl_project` takes the REAL viewport width and height as
 * parameters and derives the centre, the sphere radius and the focal length
 * from them. There is no resolution table here — that is the known hc_project()
 * defect and it is deliberately not repeated.
 *
 * TRI-SPACE DEPTH. `tvl_screen_t.depth` is the signed parallax the visual
 * language needs: > 0 protrudes toward the viewer (S+, available), < 0 recedes
 * (S−, withdrawable), == 0 is the screen plane (S0, held). It is in the same
 * pixel units as x/y and is intended to drive holo_shade()'s depth_z. Note this
 * is complementary-CHANNEL depth on one ordinary framebuffer — it is NOT the
 * Virtual Boy's per-eye dual-LED stereo, and the two must not be conflated.
 *
 * Freestanding: integer only, no libc, no libgcc, no floating point, no SIMD.
 * Every divide in the implementation is a plain-C restoring loop with
 * constant-count shifts, so nothing here lowers to __aeabi_ldivmod/__divti3.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV exact-geometry slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TVL_GEOM_H
#define ZXV_TVL_GEOM_H

#include <stdint.h>
#include <stdbool.h>
#include "zphi.h"
#include "e8.h"

/* ---- the frame ----------------------------------------------------------- */

/* An exact 3D direction. Coordinates carry the SAME ×2 scale as icos_t, because
 * that is what keeps the icosahedral rotation matrices — whose entries live in
 * ½·ℤ[φ] — inside the ring. Magnitude is meaningless for a direction; what is
 * load-bearing is that every coordinate is exact. */
typedef struct {
    zphi_t v[3];
} tvl_dir_t;

typedef enum {
    TVL_AXIS_5FOLD = 0,   /* icosahedron vertices   — 12 dirs,  6 axes */
    TVL_AXIS_3FOLD = 1,   /* dodecahedron vertices  — 20 dirs, 10 axes */
    TVL_AXIS_2FOLD = 2,   /* icosidodecahedron      — 30 dirs, 15 axes */
    TVL_AXIS_CLASSES = 3
} tvl_axis_class_t;

#define TVL_DIRS_5FOLD  12u
#define TVL_DIRS_3FOLD  20u
#define TVL_DIRS_2FOLD  30u
#define TVL_DIRS_MAX    30u

/* How many directions / how many axes this class actually yielded. Both are
 * COUNTED from the derivation, so if e8.c ever changes these change with it
 * rather than lying. */
uint32_t tvl_frame_size(tvl_axis_class_t cls);
uint32_t tvl_axis_count(tvl_axis_class_t cls);   /* = size/2, antipodes paired */

/* The i-th direction of the class, in derivation order (which is icosian index
 * order, hence deterministic). */
bool tvl_frame_dir(tvl_axis_class_t cls, uint32_t i, tvl_dir_t *out);

/* Exact squared length, in ℤ[φ]. Constant across a class — that is proved in
 * tvl_geom_selfcheck, not assumed. */
zphi_t tvl_dir_norm2(tvl_dir_t d);

bool      tvl_dir_eq(tvl_dir_t a, tvl_dir_t b);
tvl_dir_t tvl_dir_neg(tvl_dir_t d);
tvl_dir_t tvl_dir_make(zphi_t x, zphi_t y, zphi_t z);

/* Exact membership: is d one of this class's directions? Writes the index.
 * This is a real equality test, not a nearest-match — which is the entire
 * reason the coordinates are kept in ℤ[φ]. */
bool tvl_frame_index(tvl_dir_t d, tvl_axis_class_t cls, uint32_t *idx_out);

/* ---- orientation (the symmetry group acting on directions) ---------------- */

/* The rotation group is the 120 icosians acting by q·v·q̄. |2I| = 120 double
 * covers the 60 rotations, so each rotation appears twice (q and −q) — that is
 * the double cover, not a duplication bug. */
uint32_t tvl_rot_count(void);                    /* 120 */
bool     tvl_rot(uint32_t i, icos_t *out);

/* Quaternion conjugate. For a unit quaternion this IS the inverse rotation, and
 * 2I is closed under it, so the inverse of a group element is a group element.
 * e8.h exposes no conjugate, so it is implemented here (see honest limits). */
icos_t tvl_rot_inverse(icos_t q);

/* Composition. Delegates to icos_mul, which is correct for ring elements. */
icos_t tvl_rot_compose(icos_t a, icos_t b);

/* Rotate a direction, EXACTLY. Returns false rather than a rounded answer if
 * the result is not representable in ℤ[φ] at this scale — the same fail-closed
 * discipline icos_mul uses.
 *
 * WHEN IT IS EXACT, PRECISELY. The icosahedral rotation matrices have entries in
 * ½·ℤ[φ], so a general direction rotates to a half-integer and is refused. Two
 * cases are always exact, and between them they cover everything a renderer
 * needs:
 *   - every frame direction (the image is another frame direction), and
 *   - every direction whose three coordinates are all EVEN in ℤ[φ], because the
 *     factor of 2 absorbs the ½ in the matrix.
 * So the rule for scene geometry is simply: hold model-space directions at 2×,
 * via tvl_dir_even(), and rotation never fails. Measured: a general direction
 * (1,0,0) is accepted by 24 of the 120 group elements; the same direction at 2×
 * is accepted by all 120. */
bool tvl_orient(icos_t q, tvl_dir_t d, tvl_dir_t *out);

/* Double every coordinate — the cheap, exact way to make an arbitrary direction
 * safe for tvl_orient. Doubling changes length, not direction, and tvl_project
 * normalises, so nothing downstream notices. */
tvl_dir_t tvl_dir_even(tvl_dir_t d);

/* ---- projection ---------------------------------------------------------- */

/* Sanity rails on the viewport, not a resolution table: below 2 pixels there is
 * no centre to speak of, and above 65535 the Q32.32 focal length would overflow
 * int64. Any real display size falls between. */
#define TVL_VIEWPORT_MIN  2u
#define TVL_VIEWPORT_MAX  65535u

typedef struct {
    int32_t x;        /* pixel column, origin top-left, clamped to [0, vw-1]   */
    int32_t y;        /* pixel row,    origin top-left, clamped to [0, vh-1]   */
    int32_t depth;    /* signed parallax in pixels: >0 S+, <0 S−, ==0 S0       */
    int32_t scale;    /* the sphere radius used, in pixels — derived from vw/vh */
    bool    valid;
} tvl_screen_t;

/* Perspective projection onto a viewport of the given REAL pixel size.
 *
 * The camera sits at φ² sphere-radii from the origin. That is not a tuning
 * constant pulled out of the air: φ² = φ + 1 is the generating law of this
 * whole geometry, it is computed from TVL_PHI_Q32 rather than tabled, and it
 * places the eye far enough that every direction on the sphere is in front of
 * it (the denominator is provably ≥ ρ·φ > 0, so there is no clipping case).
 * The focal length is then whatever makes the sphere's equator land exactly on
 * the inscribed circle of the viewport passed in. */
bool tvl_project(tvl_dir_t d, uint32_t vw, uint32_t vh, tvl_screen_t *out);

/* Tri-space polarity of a projected point: +1 protrudes (S+), −1 recedes (S−),
 * 0 is the screen plane (S0). Kept dependency-free on purpose so the renderer,
 * not this module, decides how to reach trispace.h / holo_shade. */
int tvl_depth_polarity(const tvl_screen_t *s);

/* ---- verification --------------------------------------------------------
 * PROVES the geometry rather than asserting it: rebuilds the frames from
 * e8_icosian(), checks the class sizes and axis counts, checks that every
 * direction in a class has the IDENTICAL exact norm, independently
 * characterises the 12 five-fold directions as the signed cyclic permutations
 * of (0, 1, φ), sweeps all 120 group elements over all 62 directions checking
 * that norm is preserved exactly and the image is still a frame direction,
 * round-trips every rotation through its inverse for bit-identical recovery,
 * confirms orbit-stabiliser (|orbit| · |stabiliser| = 120) for each class,
 * checks that an arbitrary even-scaled direction rotates exactly under all 120,
 * proves the fixed-point φ constant satisfies φ² = φ + 1 in its own arithmetic,
 * and projects into a range of viewport sizes checking bounds, viewport
 * dependence and depth-polarity symmetry.
 *
 * Returns the number of problems found — 0 = healthy — matching zphi_selfcheck
 * and e8_selfcheck, the two modules this one is built on. */
uint32_t tvl_geom_selfcheck(void);

#endif /* ZXV_TVL_GEOM_H */
