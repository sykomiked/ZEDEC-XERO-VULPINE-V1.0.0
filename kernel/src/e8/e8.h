/* e8.h — the E8 lattice, built from the icosians. ISOMETRY_LIFT_M8 implemented.
 *
 * WHAT THIS IS, AND WHY IT IS BUILT THIS PARTICULAR WAY
 * ----------------------------------------------------
 * E8 is the densest sphere packing in 8 dimensions. Not "the best we know" —
 * PROVEN optimal (Viazovska, Annals of Mathematics 185, 2017), one of only five
 * dimensions where the packing problem is solved at all (1, 2, 3, 8, 24). Its
 * kissing number 240 is likewise proven and its optimal configuration is unique,
 * which is NOT true in 3 dimensions.
 *
 * There are several ways to construct E8. This module uses the ICOSIAN one,
 * because it is the construction in which φ is the generator rather than a
 * decoration:
 *
 *     φ  →  icosahedron {3,5}  →  600-cell {3,3,5}  →  icosian ring  →  E8
 *
 * Every step is exact and every step was verified before this file was written
 * (see PROVENANCE/GEOMETRY_VERIFIED.md). Concretely: the 120 vertices of the
 * 600-cell, as unit quaternions with coordinates in ℤ[φ], are closed under
 * quaternion multiplication — they are the binary icosahedral group 2I. Their
 * ℤ-span is a rank-8 lattice, and under the Conway-Sloane form that lattice is
 * even, integral and unimodular with theta series 240 / 2160 / 6720. That is
 * E8, and φ is what produced it.
 *
 * The payoff, stated exactly: E8's 240 roots split into TWO shells of 120, each
 * a 600-cell, whose squared radii stand in ratio φ² — an identity in ℤ[φ], not
 * a numerical fit. So this module generates the 240 roots as
 *
 *     {the 120 icosians}  ∪  {the same 120, scaled by 1/φ}
 *
 * in O(240) with no search, because φ−1 = 1/φ is a UNIT of ℤ[φ]. The golden
 * ratio is not applied to the lattice; it is how the second half of the lattice
 * is reached from the first.
 *
 * WHAT "LIFT" MEANS HERE — READ THIS BEFORE USING IT
 * --------------------------------------------------
 * `ISOMETRY_LIFT_M8` has been declared in m5_types.h with no implementation.
 * This provides it, and it is important to be precise about what it does and
 * does not claim.
 *
 * It maps the five integer M5 axis coordinates to a point of E8, injectively,
 * with `e8_project_m5` as an EXACT left inverse. The image is a rank-5
 * sublattice of E8 whose Gram matrix is the top-left 5x5 block of E8's.
 *
 * It is an isometric embedding **of that sublattice**. It does NOT preserve
 * some metric M5 already had — M5 has no canonical metric — it DEFINES the M5
 * metric as the pullback of E8's. That is a real and useful thing (it gives
 * every M5 state a canonical integer norm, and states a canonical integer
 * distance), but it is a definition, not a preservation, and calling it
 * anything else would be overclaiming.
 *
 * The `_tick` variant FAILS CLOSED. A phase_tick_t carries a rational and a
 * complex phase, which are not integers; rather than silently quantise and
 * return a point that is nearly right, it returns false when the state is not
 * exactly representable at the requested scale. A lattice point that is nearly
 * right is in the wrong place.
 *
 * Freestanding: integer only, no libc, no allocation, no floating point.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV exact-geometry slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_E8_H
#define ZXV_E8_H

#include <stdint.h>
#include <stdbool.h>
#include "zphi.h"

#define E8_DIM        8u    /* rank of the lattice                        */
#define E8_ICOSIANS 120u    /* vertices of the 600-cell = |2I|            */
#define E8_ROOTS    240u    /* minimal vectors = the kissing number in 8D */
#define E8_M5_DIM     5u    /* the M5 sublattice the lift lands in        */

/* A unit quaternion of the icosian ring. Coordinates are SCALED BY 2 so that
 * the half-integers in the 600-cell's vertex set stay inside ℤ[φ] — every
 * icosian then has h[0]²+h[1]²+h[2]²+h[3]² == 4 exactly. */
typedef struct {
    zphi_t h[4];
} icos_t;

/* A lattice point, held as its INTEGER COEFFICIENTS over the icosian ℤ-basis
 * rather than as coordinates. That choice is deliberate: coefficients make
 * membership free (every integer vector is a lattice point, by construction),
 * make the lift and its inverse exact and trivial, and make the norm a plain
 * integer quadratic form. Coordinates would make membership a search. */
typedef struct {
    int64_t c[E8_DIM];
    bool    valid;
} e8_pt_t;

/* ---- the icosians / 600-cell -------------------------------------------- */
uint32_t e8_icosian_count(void);            /* 120 */
bool     e8_icosian(uint32_t i, icos_t *out);
icos_t   icos_add(icos_t x, icos_t y);
icos_t   icos_neg(icos_t x);
icos_t   icos_mul(icos_t x, icos_t y);      /* quaternion product (2I is closed) */
icos_t   icos_scale_zphi(icos_t x, zphi_t s);
/* Σ h_i², i.e. 4·N(q) because coordinates carry the ×2 scale. Lives in ℤ[φ]. */
zphi_t   icos_norm4(icos_t q);
bool     icos_eq(icos_t x, icos_t y);

/* ---- the lattice --------------------------------------------------------- */
e8_pt_t  e8_zero(void);
e8_pt_t  e8_from_coeffs(const int64_t c[E8_DIM]);
e8_pt_t  e8_add(e8_pt_t x, e8_pt_t y);
e8_pt_t  e8_neg(e8_pt_t x);

/* Expand coefficients to the quaternion they denote. */
bool     e8_expand(e8_pt_t p, icos_t *out);

/* The Euclidean norm Q(p) = cᵀGc. Even for every lattice point (that is what
 * "even lattice" means); 2 exactly for the 240 roots. Sets *ok=false on
 * overflow or an invalid point. */
int64_t  e8_norm(e8_pt_t p, bool *ok);
bool     e8_is_root(e8_pt_t p);             /* Q == 2 */

/* The Gram matrix, row-major 8x8. Small enough for int8; verified against the
 * basis quaternions at selfcheck time so the table cannot rot silently. */
const int8_t *e8_gram(void);

/* Generate all 240 roots as the two φ-related 600-cell shells. Writes
 * min(max, 240) entries, returns how many exist (240). */
uint32_t e8_roots(icos_t *out, uint32_t max);

/* ---- ISOMETRY_LIFT_M8 and its inverse ------------------------------------
 * Injective; e8_project_m5 is an exact left inverse. See the header note above
 * for exactly what "isometry" claims and does not claim. */
bool e8_lift_m5(const int64_t m5[E8_M5_DIM], e8_pt_t *out);
bool e8_project_m5(e8_pt_t p, int64_t m5_out[E8_M5_DIM]);

/* The induced M5 metric: the top-left 5x5 block of the E8 Gram, row-major.
 * Positive definite (it is a sublattice of a positive definite lattice), so
 * e8_m5_norm is zero only for the zero state. */
const int8_t *e8_m5_gram(void);
int64_t       e8_m5_norm(const int64_t m5[E8_M5_DIM], bool *ok);

/* Exact squared distance between two M5 states under that metric. */
int64_t e8_m5_dist2(const int64_t x[E8_M5_DIM], const int64_t y[E8_M5_DIM],
                    bool *ok);

/* ---- verification --------------------------------------------------------
 * Recomputes the Gram from the basis quaternions, checks unimodularity by
 * exact integer elimination, regenerates the icosians and confirms 2I closure,
 * counts the roots, verifies the φ² shell-radius identity, and round-trips the
 * lift. Returns the number of problems found (0 = healthy). */
uint32_t e8_selfcheck(void);

#endif /* ZXV_E8_H */
