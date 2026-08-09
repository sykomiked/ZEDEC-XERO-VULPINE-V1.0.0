/* mixmat.h — exact rational MIXING MATRICES: the compounding operator, general
 *
 * ORIGIN
 * ------
 * This generalises the mathematics in "The Effective Method of Sonic Chemistry"
 * (M. L. Curzi, 17 Jan 2022). That paper assigns each element a value from its
 * atomic number N and combines elements into compounds. Stripped of the
 * chemistry, the two operations it defines are:
 *
 *   INDEX      E(n) = ((n/φ) · 9/8)²
 *   COMPOUND   C    = ( Π E_i^(P_i/W) )²      with W = Σ P_i
 *
 * The second one is the interesting one, and it is not really about molecules.
 *
 * THE OBSERVATION THAT MAKES IT A MATRIX OPERATOR
 * -----------------------------------------------
 * Take logarithms. Writing L_i = log E_i and w_i = P_i / W:
 *
 *   log C = 2 · Σ w_i L_i          where every w_i ≥ 0 and Σ w_i = 1
 *
 * A product of powers has become a WEIGHTED AVERAGE. The exponents were already
 * constrained to sum to 1 (they are parts of a whole), which is exactly the
 * condition for a row of a ROW-STOCHASTIC MATRIX. So with m compounds over n
 * constituents, M[j][i] = P_ji / W_j and the whole scheme is
 *
 *   L_out = level · M · L_in
 *
 * i.e. **multiply by a stochastic matrix in log space, then scale by the
 * level**. The squaring in the paper is the level: each squaring doubles it,
 * which is why the paper says repeated squaring "establishes a level of harmonic
 * structure" — the levels are 2, 4, 8, 16, … and the mixing is separate from it.
 *
 * WHY THIS IS THE OPERATOR THE SYSTEM NEEDED
 * ------------------------------------------
 * Three properties, all verified in mixmat_selfcheck rather than asserted:
 *
 *   1. CLOSED UNDER COMPOSITION. The product of two row-stochastic matrices is
 *      row-stochastic. So nesting these operators never leaves the class — which
 *      is the requirement for "matrices whose cells are matrices, in closed-loop
 *      systems". The closure is a theorem, not a convention.
 *
 *   2. IT HAS A FIXED POINT. Every row-stochastic matrix has eigenvalue 1 (the
 *      all-ones vector is a right eigenvector) and no eigenvalue larger in
 *      modulus. Iterating a primitive one converges to a rank-1 stationary
 *      projector: the closed loop settles. The second-largest |λ| is the mixing
 *      rate — how fast it settles.
 *
 *   3. IT IS EXACT. The weights are P_i/W — ratios of integer counts, i.e.
 *      RATIONALS. So the entire matrix, its powers and its fixed point can be
 *      held in rat_t with zero rounding. The floating point in the original
 *      spreadsheet is an artefact of the tool, not of the mathematics.
 *
 * The decomposition to hold onto: the dynamics split into a GEOMETRIC SCALE (the
 * level, 2^k, which grows) and a MARKOV MIXING (M^k, which converges). Two
 * independent parts. Cycles within cycles, one climbing and one settling.
 *
 * WHAT THIS MODULE DOES NOT CLAIM
 * -------------------------------
 * Nothing here asserts that E(n) is the physical resonance frequency of element
 * n. E(n) = kn² with k = (9/8)²/φ² is a DEFINED index map — a well-formed way of
 * placing integers on a golden-scaled ladder — and this module implements the
 * combinatorial operator built on top of it. Its correctness is mathematical.
 * Any physical or biological interpretation is a separate question this code
 * does not answer and does not depend on.
 *
 * Freestanding: exact rationals only, no libc, no allocation, no floating point.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV exact-algebra slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_MIXMAT_H
#define ZXV_MIXMAT_H

#include <stdint.h>
#include <stdbool.h>
#include "rational.h"

#define MIXMAT_MAX 13u   /* the system's own arity: 13 logic phases, 13d lattice */

/* A square row-stochastic matrix over exact rationals. `n` is the live order;
 * entries outside [0,n)x[0,n) are ignored. */
typedef struct {
    uint32_t n;
    rat_t    a[MIXMAT_MAX][MIXMAT_MAX];
    bool     valid;
} mixmat_t;

/* A vector in LOG space — the space the operator is linear in. */
typedef struct {
    uint32_t n;
    rat_t    v[MIXMAT_MAX];
    bool     valid;
} mixvec_t;

/* ---- construction -------------------------------------------------------- */
mixmat_t mixmat_identity(uint32_t n);
mixmat_t mixmat_zero(uint32_t n);

/* Build one row from integer COUNTS, exactly as the compounding formula does:
 * w_i = counts[i] / (Σ counts). Returns false if the row is all zeros (no
 * whole to take parts of) or any count is negative. */
bool mixmat_set_row_counts(mixmat_t *m, uint32_t row,
                           const int64_t *counts, uint32_t n);

/* ---- the defining property ----------------------------------------------
 * Every entry ≥ 0 and every row summing to EXACTLY 1. Checked with rat_eq, so
 * there is no epsilon and no "close enough" — a row that sums to 0.9999 is not
 * a mixing row and is refused. */
bool mixmat_is_stochastic(const mixmat_t *m);

/* ---- operations ---------------------------------------------------------- */
mixmat_t mixmat_mul(const mixmat_t *x, const mixmat_t *y);   /* closed */
mixmat_t mixmat_pow(const mixmat_t *x, uint32_t k);
mixvec_t mixmat_apply(const mixmat_t *m, const mixvec_t *v);

/* The full operator: L_out = level · M · L_in. `level` is the paper's squaring
 * count — level 1 means one squaring (scale 2), level 2 means two (scale 4),
 * and so on; scale = 2^level. */
mixvec_t mixmat_compound(const mixmat_t *m, const mixvec_t *v, uint32_t level);

/* ---- the fixed point -----------------------------------------------------
 * The stationary distribution: the unique π with πM = π and Σπ = 1. This is the
 * closed loop's settled state.
 *
 * It is computed by an EXACT LINEAR SOLVE, not by iterating. That distinction
 * matters and an earlier draft of this module got it wrong: M^k approaches its
 * limit asymptotically and NEVER attains it, so "square until two successive
 * powers are equal" can never succeed for a genuinely mixing matrix, and the
 * rational denominators blow past int64 long before the values settle. Solving
 * πM = π gives the exact answer in one elimination with no tolerance, no
 * iteration count, and no overflow from repeated multiplication.
 *
 * Returns false when there is no unique stationary distribution — a permutation
 * matrix, or one with several closed classes. That is the honest answer; the
 * alternative would be to return one of several fixed points arbitrarily. */
bool mixmat_stationary(const mixmat_t *m, mixvec_t *out);

/* Is the matrix a permutation (exactly one 1 per row and per column)? Those are
 * the stochastic matrices that PERMUTE rather than MIX: they are invertible,
 * cycle forever, and have no interior fixed point. Useful to detect, because a
 * closed loop built from them never settles. */
bool mixmat_is_permutation(const mixmat_t *m);

/* Verifies closure, the row-sum invariant under multiplication and powers,
 * the all-ones eigenvector, convergence of a primitive example, and the
 * permutation/mixing distinction. Returns problems found (0 = healthy). */
uint32_t mixmat_selfcheck(void);

#endif /* ZXV_MIXMAT_H */
