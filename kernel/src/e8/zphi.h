/* zphi.h — ℤ[φ], the golden integers: exact arithmetic in a + bφ
 *
 * WHY THIS TYPE EXISTS
 * --------------------
 * φ is irrational, so every φ-bearing structure in this kernel has until now
 * been stored as a rounded double or a Q32.32 fixed-point approximation. That is
 * fine for rendering and fatal for algebra: a lattice whose points are only
 * approximately where they belong is not a lattice, and "is this point in the
 * lattice?" stops being a question with an answer.
 *
 * The fix is the standard one. φ satisfies x² = x + 1, so the ring ℤ[φ] — all
 * numbers a + bφ with a, b integers — is CLOSED under +, − and ×, and every
 * element is represented EXACTLY by two machine integers. No rounding exists
 * anywhere in this file. The closure is the whole point:
 *
 *     (a + bφ) + (c + dφ) = (a+c) + (b+d)φ
 *     (a + bφ)(c + dφ)    = (ac + bd) + (ad + bc + bd)φ        [uses φ² = φ+1]
 *
 * So the owner's rule — φ is the generating law, not one pattern among many —
 * becomes a data type. `zphi_mul` IS x² = x + 1; the identity is not applied on
 * top of the arithmetic, it is what makes the arithmetic close.
 *
 * THE TWO ROOTS, AND WHY BOTH ARE HERE
 * ------------------------------------
 * x² = x + 1 has two roots: φ = 1.618... and ψ = 1 − φ = −0.618... The map
 * φ ↦ ψ is the field conjugation, `zphi_conj`. It is not decoration — the NORM
 *
 *     N(a + bφ) = (a + bφ)(a + bψ) = a² + ab − b²
 *
 * is an ordinary INTEGER, and it is what lets an irrational-coordinate lattice
 * be tested with integer comparisons. Every exactness result downstream rests on
 * that one fact. (Both roots, and their interaction, are exactly the owner's
 * "first binary choice" — and N is what the interaction produces.)
 *
 * OVERFLOW IS REPORTED, NEVER WRAPPED
 * -----------------------------------
 * Following `rat_t` (src/rational/rational.h) rather than `rational_t` (which
 * wraps silently): every operation checks, and a result that would not fit
 * carries valid=false and propagates. A wrapped coordinate is a point somewhere
 * else in the lattice, which would be an undetectable wrong answer rather than a
 * loud one.
 *
 * Freestanding: integer only, no libc, no allocation, no floating point.
 * `zphi_to_double` is the one exception and is DIAGNOSTIC ONLY — see its note.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV exact-geometry slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ZPHI_H
#define ZXV_ZPHI_H

#include <stdint.h>
#include <stdbool.h>

/* a + b*phi, exact. valid=false once an operation has overflowed. */
typedef struct {
    int64_t a;
    int64_t b;
    bool    valid;
} zphi_t;

/* ---- construction --------------------------------------------------------
 * NOTE ON NAMING: this module deliberately avoids the identifiers `phi` and
 * `PHI`. In this kernel those already mean the M5 imaginary/externality axis
 * (edp_risk.h, triple_ledger.h, ministry.c — 82 use sites), and they ALSO mean
 * Euler's totient, luminous flux and the 13th cyclotomic polynomial in various
 * comments. Everything here is prefixed `zphi_`/`ZPHI_` so no golden-ratio
 * quantity can ever be mistaken for an externality phase. */
zphi_t zphi_make(int64_t a, int64_t b);   /* a + b*phi          */
zphi_t zphi_int(int64_t n);               /* n + 0*phi          */
zphi_t zphi_zero(void);                   /* 0                  */
zphi_t zphi_one(void);                    /* 1                  */
zphi_t zphi_golden(void);                 /* phi      = 0 + 1*phi   */
zphi_t zphi_inv_golden(void);             /* 1/phi    = -1 + 1*phi  */
zphi_t zphi_invalid(void);

/* ---- arithmetic (exact; overflow sets valid=false and propagates) -------- */
zphi_t zphi_add(zphi_t x, zphi_t y);
zphi_t zphi_sub(zphi_t x, zphi_t y);
zphi_t zphi_mul(zphi_t x, zphi_t y);      /* uses phi^2 = phi + 1 */
zphi_t zphi_neg(zphi_t x);
zphi_t zphi_scale(zphi_t x, int64_t k);

/* Field conjugation phi -> psi = 1 - phi.  conj(a + b*phi) = (a+b) - b*phi.
 * This is the OTHER root of the generating rule, not an unrelated operation. */
zphi_t zphi_conj(zphi_t x);

/* Norm N(x) = x * conj(x) = a^2 + ab - b^2. ALWAYS an ordinary integer — that
 * is what makes exact tests on an irrational-coordinate lattice possible.
 * Sets *ok=false on overflow (and returns 0). */
int64_t zphi_norm(zphi_t x, bool *ok);

/* Trace Tr(x) = x + conj(x) = 2a + b. Also an ordinary integer. */
int64_t zphi_trace(zphi_t x, bool *ok);

/* ---- comparison ---------------------------------------------------------- */
bool zphi_eq(zphi_t x, zphi_t y);         /* false if either is invalid */
bool zphi_is_zero(zphi_t x);
bool zphi_is_int(zphi_t x);               /* b == 0 */

/* ---- diagnostics ---------------------------------------------------------
 * DOUBLE CONVERSION IS FOR PRINTING AND TESTS ONLY. Never make a decision on
 * this value: the exact predicates above are the whole reason this type exists,
 * and comparing two converted doubles reintroduces precisely the rounding this
 * module was written to eliminate. Declared in hosted builds only: kernel
 * images (-ffreestanding) are integer-only. */
#if __STDC_HOSTED__
double zphi_to_double(zphi_t x);
#endif

/* Internal consistency: closure, the two roots, conjugation, norm
 * multiplicativity, and overflow reporting. Returns the number of problems
 * found (0 = healthy). */
uint32_t zphi_selfcheck(void);

#endif /* ZXV_ZPHI_H */
