/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fib_lattice.h — the Fibonacci lattice ladder (Korkine-Zolotareff-Voronoi
 * geometry of numbers): every Fibonacci dimension F1..F12 = 144 anchored by
 * its optimal ("perfect/extreme") lattice. Two self-dual anchors stand in
 * the whole ladder: F6 = 8 (E8) and F12 = 144 (an extremal even unimodular
 * lattice), the only Fibonacci dimensions up to 144 with n == 0 (mod 8), the
 * necessary condition for an even unimodular (Type II) lattice. 144 is the
 * structural harmonic of E8: 144 = 8*18 = 6*24 (the Leech harmonic) = 12^2,
 * and its shortest vectors have squared length 14, the Mallows-Odlyzko-Sloane
 * bound 2*floor(144/24)+2. The kernel verifies the exact invariants at boot
 * (fib_lattice_selfcheck) and the enumerated kissing numbers (A2=6, A3=12,
 * D5=40, E8=240); the rest of the table is the compiled-in blueprint. */
#ifndef ZXV_FIB_LATTICE_H
#define ZXV_FIB_LATTICE_H

#include <stdint.h>

#define FIB_LATTICE_STEPS 12u /* F1..F12, the ladder up to 144 */

typedef enum {
    FIB_LATTICE_ROOT = 0,     /* A1/A2/A3/D5: classical root lattices */
    FIB_LATTICE_TYPE2 = 1,    /* even unimodular (Type II): E8, L144 */
    FIB_LATTICE_LAMINATED = 2, /* a section of a master lattice (BW16, Leech) */
    FIB_LATTICE_CYCLOTOMIC = 3, /* Craig's cyclotomic construction */
} fib_lattice_type_t;

typedef struct {
    uint32_t fib_step; /* the Fibonacci index n (1..12) */
    uint32_t dim;      /* F_n, the dimension */
    const char *name;  /* the optimal lattice at this dimension */
    uint32_t kissing;  /* tau: the number of minimal vectors */
    uint32_t min_norm; /* squared length of a shortest vector */
    fib_lattice_type_t type;
} fib_lattice_entry_t;

/* The ladder, F1..F12. The kissing numbers of A2/A3/D5/E8 are the exact
 * root-system counts this module verifies; the others are the literature
 * values of the blueprint, embedded as data (not independently computed). */
const fib_lattice_entry_t *fib_lattice_ladder(uint32_t *n);

/* Verify the exact invariants of the ladder: the Fibonacci values, Cassini's
 * identity on every rung, the Pisano period modulo 8 (which admits Type II
 * lattices only at F6 = 8 and F12 = 144), the 144 harmonic (6*24, 8*18, 12^2)
 * and the M-O-S minimum-norm bound 14, and the enumerated kissing numbers of
 * A2 (6), A3 (12), D5 (40) and E8 (240). Returns 0, or the failing step. */
int fib_lattice_selfcheck(void);

#endif /* ZXV_FIB_LATTICE_H */