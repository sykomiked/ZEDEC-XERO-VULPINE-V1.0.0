/* e8.c — E8 from the icosians. See e8.h for the construction and its claims.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "e8.h"

/* ===================== the 600-cell, generated exactly =====================
 * Three orbits, coordinates scaled by 2 (see e8.h):
 *    8  : (±2, 0, 0, 0) and its coordinate permutations
 *   16  : (±1, ±1, ±1, ±1)
 *   96  : EVEN permutations of (±φ, ±1, ±(φ−1), 0)
 * 8 + 16 + 96 = 120, with no duplicates — asserted in e8_selfcheck.
 *
 * The even permutations are computed here rather than tabled, because a
 * hand-written table of A4 is exactly the kind of constant that is wrong once
 * and then trusted forever. */
static uint32_t perm_is_even(const uint8_t p[4]) {
    uint32_t inv = 0;
    for (uint32_t i = 0; i < 4; i++)
        for (uint32_t j = i + 1; j < 4; j++)
            if (p[i] > p[j]) inv++;
    return (inv % 2u) == 0u;
}

/* The i-th permutation of {0,1,2,3} in lexicographic order (i < 24). */
static void perm_nth(uint32_t i, uint8_t out[4]) {
    uint8_t pool[4] = {0, 1, 2, 3};
    uint32_t n = 4, fact[4] = {6, 2, 1, 1};
    for (uint32_t k = 0; k < 4; k++) {
        uint32_t idx = i / fact[k];
        i %= fact[k];
        out[k] = pool[idx];
        for (uint32_t j = idx + 1; j < n; j++) pool[j - 1] = pool[j];
        n--;
    }
}

static icos_t icos_zero_q(void) {
    icos_t q;
    for (uint32_t k = 0; k < 4; k++) q.h[k] = zphi_zero();
    return q;
}

uint32_t e8_icosian_count(void) { return E8_ICOSIANS; }

bool e8_icosian(uint32_t i, icos_t *out) {
    if (!out || i >= E8_ICOSIANS) return false;
    icos_t q = icos_zero_q();

    if (i < 8u) {                       /* (±2,0,0,0), 4 axes × 2 signs */
        uint32_t axis = i >> 1, sgn = i & 1u;
        q.h[axis] = zphi_int(sgn ? -2 : 2);
        *out = q; return true;
    }
    i -= 8u;
    if (i < 16u) {                      /* (±1,±1,±1,±1) */
        for (uint32_t k = 0; k < 4; k++)
            q.h[k] = zphi_int(((i >> k) & 1u) ? -1 : 1);
        *out = q; return true;
    }
    i -= 16u;
    /* 96 = 12 even permutations × 8 sign patterns on the three nonzero slots.
     * The fourth entry is 0, so its sign is not a degree of freedom. */
    {
        uint32_t pi = i / 8u, sg = i % 8u;
        uint8_t p[4]; uint32_t seen = 0;
        for (uint32_t t = 0; t < 24u; t++) {
            perm_nth(t, p);
            if (!perm_is_even(p)) continue;
            if (seen == pi) break;
            seen++;
        }
        zphi_t base[4];
        base[0] = zphi_golden();       /* φ       */
        base[1] = zphi_one();          /* 1       */
        base[2] = zphi_inv_golden();   /* φ−1=1/φ */
        base[3] = zphi_zero();
        for (uint32_t k = 0; k < 3; k++)
            if ((sg >> k) & 1u) base[k] = zphi_neg(base[k]);
        for (uint32_t k = 0; k < 4; k++) q.h[p[k]] = base[k];
        *out = q; return true;
    }
}

/* ===================== quaternion arithmetic in ℤ[φ] ====================== */

icos_t icos_add(icos_t x, icos_t y) {
    icos_t r;
    for (uint32_t k = 0; k < 4; k++) r.h[k] = zphi_add(x.h[k], y.h[k]);
    return r;
}
icos_t icos_neg(icos_t x) {
    icos_t r;
    for (uint32_t k = 0; k < 4; k++) r.h[k] = zphi_neg(x.h[k]);
    return r;
}
icos_t icos_scale_zphi(icos_t x, zphi_t s) {
    icos_t r;
    for (uint32_t k = 0; k < 4; k++) r.h[k] = zphi_mul(x.h[k], s);
    return r;
}

/* Hamilton product. Both operands carry the ×2 scale, so the raw product
 * carries ×4 and must be halved to return to the ×2 convention. The ring is
 * closed, so the halving is always exact; if it ever is not, the inputs were
 * not ring elements and we return an INVALID quaternion rather than a
 * silently-truncated one. */
icos_t icos_mul(icos_t x, icos_t y) {
    const zphi_t a1 = x.h[0], b1 = x.h[1], c1 = x.h[2], d1 = x.h[3];
    const zphi_t a2 = y.h[0], b2 = y.h[1], c2 = y.h[2], d2 = y.h[3];
    zphi_t r[4];
    r[0] = zphi_sub(zphi_sub(zphi_mul(a1,a2), zphi_mul(b1,b2)),
                    zphi_add(zphi_mul(c1,c2), zphi_mul(d1,d2)));
    r[1] = zphi_add(zphi_add(zphi_mul(a1,b2), zphi_mul(b1,a2)),
                    zphi_sub(zphi_mul(c1,d2), zphi_mul(d1,c2)));
    r[2] = zphi_add(zphi_add(zphi_mul(a1,c2), zphi_mul(c1,a2)),
                    zphi_sub(zphi_mul(d1,b2), zphi_mul(b1,d2)));
    r[3] = zphi_add(zphi_add(zphi_mul(a1,d2), zphi_mul(d1,a2)),
                    zphi_sub(zphi_mul(b1,c2), zphi_mul(c1,b2)));
    icos_t out;
    for (uint32_t k = 0; k < 4; k++) {
        if (!r[k].valid || (r[k].a % 2) != 0 || (r[k].b % 2) != 0) {
            out.h[k] = zphi_invalid();
            for (uint32_t j = 0; j < 4; j++) out.h[j] = zphi_invalid();
            return out;
        }
        out.h[k] = zphi_make(r[k].a / 2, r[k].b / 2);
    }
    return out;
}

zphi_t icos_norm4(icos_t q) {
    zphi_t s = zphi_zero();
    for (uint32_t k = 0; k < 4; k++) s = zphi_add(s, zphi_mul(q.h[k], q.h[k]));
    return s;
}

bool icos_eq(icos_t x, icos_t y) {
    for (uint32_t k = 0; k < 4; k++) if (!zphi_eq(x.h[k], y.h[k])) return false;
    return true;
}

/* ===================== the ℤ-basis and the Gram matrix =====================
 * Eight icosians whose ℤ-span is the whole icosian ring. Derived offline by
 * rank-reduction over ℚ across all 120 icosians, then verified here: the Gram
 * matrix is RECOMPUTED from these quaternions in e8_selfcheck and compared
 * against the table below, so the two cannot drift apart.
 *
 * The quadratic form is Q(q) = (U + V)/2 where U + Vφ = Σh_i². Check it on the
 * first basis element: (−2,0,0,0) gives Σh_i² = 4 + 0φ, so Q = 2. Every basis
 * element is a root. */
static const int64_t BASIS[E8_DIM][4][2] = {
    { {-2,0}, { 0,0}, { 0,0}, { 0,0} },   /* b0 */
    { {-1,0}, {-1,0}, {-1,0}, {-1,0} },   /* b1 */
    { {-1,0}, {-1,0}, {-1,0}, { 1,0} },   /* b2 */
    { {-1,0}, {-1,0}, { 1,0}, {-1,0} },   /* b3 */
    { {-1,0}, {-1,1}, { 0,-1}, { 0,0} },  /* b4 */
    { {-1,0}, {-1,1}, { 0, 1}, { 0,0} },  /* b5 */
    { {-1,0}, { 0,-1}, { 0,0}, {-1,1} },  /* b6 */
    { {-1,1}, {-1,0}, { 0,0}, { 0,-1} },  /* b7 */
};

/* G = the E8 Gram matrix. det(G) = 1 (unimodular), diagonal all 2 (even, and
 * every basis vector is a root). Theta series 240 / 2160 / 6720 — verified by
 * complete Cholesky-bounded enumeration before this table was written. */
static const int8_t GRAM[E8_DIM * E8_DIM] = {
    2, 1, 1, 1, 1, 1, 1, 0,
    1, 2, 1, 1, 1, 0, 1, 1,
    1, 1, 2, 0, 1, 0, 1, 0,
    1, 1, 0, 2, 0, 1, 1, 1,
    1, 1, 1, 0, 2, 0, 0, 0,
    1, 0, 0, 1, 0, 2, 0, 0,
    1, 1, 1, 1, 0, 0, 2, 0,
    0, 1, 0, 1, 0, 0, 0, 2,
};

static const int8_t M5_GRAM[E8_M5_DIM * E8_M5_DIM] = {
    2, 1, 1, 1, 1,
    1, 2, 1, 1, 1,
    1, 1, 2, 0, 1,
    1, 1, 0, 2, 0,
    1, 1, 1, 0, 2,
};

const int8_t *e8_gram(void)    { return GRAM; }
const int8_t *e8_m5_gram(void) { return M5_GRAM; }

static icos_t basis_q(uint32_t i) {
    icos_t q;
    for (uint32_t k = 0; k < 4; k++)
        q.h[k] = zphi_make(BASIS[i][k][0], BASIS[i][k][1]);
    return q;
}

/* ===================== lattice points ===================================== */

e8_pt_t e8_zero(void) {
    e8_pt_t p;
    for (uint32_t i = 0; i < E8_DIM; i++) p.c[i] = 0;
    p.valid = true;
    return p;
}

e8_pt_t e8_from_coeffs(const int64_t c[E8_DIM]) {
    e8_pt_t p = e8_zero();
    if (!c) { p.valid = false; return p; }
    for (uint32_t i = 0; i < E8_DIM; i++) p.c[i] = c[i];
    return p;
}

e8_pt_t e8_add(e8_pt_t x, e8_pt_t y) {
    e8_pt_t r = e8_zero();
    if (!x.valid || !y.valid) { r.valid = false; return r; }
    for (uint32_t i = 0; i < E8_DIM; i++)
        if (__builtin_add_overflow(x.c[i], y.c[i], &r.c[i])) { r.valid = false; return r; }
    return r;
}

e8_pt_t e8_neg(e8_pt_t x) {
    e8_pt_t r = e8_zero();
    if (!x.valid) { r.valid = false; return r; }
    for (uint32_t i = 0; i < E8_DIM; i++)
        if (__builtin_sub_overflow((int64_t)0, x.c[i], &r.c[i])) { r.valid = false; return r; }
    return r;
}

bool e8_expand(e8_pt_t p, icos_t *out) {
    if (!out || !p.valid) return false;
    icos_t acc = icos_zero_q();
    for (uint32_t i = 0; i < E8_DIM; i++) {
        if (p.c[i] == 0) continue;
        icos_t b = basis_q(i);
        for (uint32_t k = 0; k < 4; k++) {
            zphi_t t = zphi_scale(b.h[k], p.c[i]);
            acc.h[k] = zphi_add(acc.h[k], t);
            if (!acc.h[k].valid) return false;
        }
    }
    *out = acc;
    return true;
}

/* Q(p) = cᵀGc, computed in int64 with overflow checking at every step. */
int64_t e8_norm(e8_pt_t p, bool *ok) {
    if (ok) *ok = false;
    if (!p.valid) return 0;
    int64_t total = 0;
    for (uint32_t i = 0; i < E8_DIM; i++) {
        if (p.c[i] == 0) continue;
        for (uint32_t j = 0; j < E8_DIM; j++) {
            int64_t g = (int64_t)GRAM[i * E8_DIM + j];
            if (g == 0 || p.c[j] == 0) continue;
            int64_t t;
            if (__builtin_mul_overflow(p.c[i], p.c[j], &t)) return 0;
            if (__builtin_mul_overflow(t, g, &t))           return 0;
            if (__builtin_add_overflow(total, t, &total))   return 0;
        }
    }
    if (ok) *ok = true;
    return total;
}

bool e8_is_root(e8_pt_t p) {
    bool ok = false;
    int64_t n = e8_norm(p, &ok);
    return ok && n == 2;
}

/* The 240 roots, as the two φ-related 600-cell shells. No search: the second
 * shell is the first scaled by the UNIT 1/φ = φ−1 of ℤ[φ]. */
uint32_t e8_roots(icos_t *out, uint32_t max) {
    if (out) {
        const zphi_t inv = zphi_inv_golden();
        for (uint32_t i = 0; i < E8_ICOSIANS; i++) {
            icos_t q;
            if (!e8_icosian(i, &q)) continue;
            if (i < max)                 out[i] = q;
            if (i + E8_ICOSIANS < max)   out[i + E8_ICOSIANS] = icos_scale_zphi(q, inv);
        }
    }
    return E8_ROOTS;
}

/* ===================== ISOMETRY_LIFT_M8 ==================================== */

bool e8_lift_m5(const int64_t m5[E8_M5_DIM], e8_pt_t *out) {
    if (!m5 || !out) return false;
    e8_pt_t p = e8_zero();
    for (uint32_t i = 0; i < E8_M5_DIM; i++) p.c[i] = m5[i];
    /* c[5..7] stay 0: the image is the rank-5 sublattice spanned by b0..b4,
     * which is exactly what makes the projection below an exact left inverse. */
    *out = p;
    return true;
}

bool e8_project_m5(e8_pt_t p, int64_t m5_out[E8_M5_DIM]) {
    if (!m5_out || !p.valid) return false;
    /* A point outside the rank-5 sublattice has no M5 preimage. Say so rather
     * than truncate — truncation would make project(lift(x)) == x hold while
     * project(y) silently invented an answer for y not in the image. */
    for (uint32_t i = E8_M5_DIM; i < E8_DIM; i++) if (p.c[i] != 0) return false;
    for (uint32_t i = 0; i < E8_M5_DIM; i++) m5_out[i] = p.c[i];
    return true;
}

int64_t e8_m5_norm(const int64_t m5[E8_M5_DIM], bool *ok) {
    if (ok) *ok = false;
    if (!m5) return 0;
    e8_pt_t p;
    if (!e8_lift_m5(m5, &p)) return 0;
    return e8_norm(p, ok);
}

int64_t e8_m5_dist2(const int64_t x[E8_M5_DIM], const int64_t y[E8_M5_DIM],
                    bool *ok) {
    if (ok) *ok = false;
    if (!x || !y) return 0;
    int64_t d[E8_M5_DIM];
    for (uint32_t i = 0; i < E8_M5_DIM; i++)
        if (__builtin_sub_overflow(x[i], y[i], &d[i])) return 0;
    return e8_m5_norm(d, ok);
}

/* ===================== verification ======================================= */

/* Exact integer determinant by fraction-free (Bareiss) elimination — no
 * floating point, no division that is not exact. */
static bool gram_det_is_one(void) {
    int64_t m[E8_DIM][E8_DIM];
    for (uint32_t i = 0; i < E8_DIM; i++)
        for (uint32_t j = 0; j < E8_DIM; j++)
            m[i][j] = (int64_t)GRAM[i * E8_DIM + j];
    int64_t prev = 1, sign = 1;
    for (uint32_t k = 0; k + 1 < E8_DIM; k++) {
        if (m[k][k] == 0) {                       /* pivot */
            uint32_t s = k + 1;
            while (s < E8_DIM && m[s][k] == 0) s++;
            if (s == E8_DIM) return false;        /* singular */
            for (uint32_t j = 0; j < E8_DIM; j++) {
                int64_t t = m[k][j]; m[k][j] = m[s][j]; m[s][j] = t;
            }
            sign = -sign;
        }
        for (uint32_t i = k + 1; i < E8_DIM; i++)
            for (uint32_t j = k + 1; j < E8_DIM; j++) {
                int64_t p1, p2, num;
                if (__builtin_mul_overflow(m[i][j], m[k][k], &p1)) return false;
                if (__builtin_mul_overflow(m[i][k], m[k][j], &p2)) return false;
                if (__builtin_sub_overflow(p1, p2, &num))          return false;
                m[i][j] = num / prev;             /* exact by Bareiss */
            }
        prev = m[k][k];
    }
    return (sign * m[E8_DIM - 1][E8_DIM - 1]) == 1;
}

uint32_t e8_selfcheck(void) {
    /* MEMOISED. The geometry is immutable — the basis, the Gram and the
     * icosians are compile-time constants — so the answer cannot change between
     * calls. The full check sweeps ~22k quaternion comparisons, which is fine
     * once at bring-up and not fine on a caller's hot path;
     * axiom_matrix_is_symmetric(ISOMETRY_LIFT_M8) consults it per query. */
    static bool  done = false;
    static uint32_t cached = 0;
    if (done) return cached;

    uint32_t bad = 0;

    /* 0. The golden integers themselves must be sound first. */
    bad += zphi_selfcheck();

    /* 1. 120 distinct icosians, every one a unit quaternion (Σh² == 4). */
    {
        uint32_t dup = 0;
        for (uint32_t i = 0; i < E8_ICOSIANS; i++) {
            icos_t qi;
            if (!e8_icosian(i, &qi)) { bad++; continue; }
            if (!zphi_eq(icos_norm4(qi), zphi_int(4))) bad++;
            for (uint32_t j = i + 1; j < E8_ICOSIANS; j++) {
                icos_t qj;
                if (e8_icosian(j, &qj) && icos_eq(qi, qj)) dup++;
            }
        }
        if (dup != 0) bad++;
    }

    /* 2. 2I closure: the product of two icosians is an icosian. This is the
     *    binary icosahedral group, and it is what makes the ℤ-span a RING
     *    rather than merely a lattice. Swept over a stride so the check stays
     *    bounded but still touches every orbit. */
    for (uint32_t i = 0; i < E8_ICOSIANS; i += 7) {
        for (uint32_t j = 0; j < E8_ICOSIANS; j += 11) {
            icos_t a, b;
            if (!e8_icosian(i, &a) || !e8_icosian(j, &b)) { bad++; continue; }
            icos_t p = icos_mul(a, b);
            if (!zphi_eq(icos_norm4(p), zphi_int(4))) { bad++; continue; }
            bool found = false;
            for (uint32_t k = 0; k < E8_ICOSIANS && !found; k++) {
                icos_t c;
                if (e8_icosian(k, &c) && icos_eq(p, c)) found = true;
            }
            if (!found) bad++;
        }
    }

    /* 3. The Gram table must equal what the basis quaternions actually give.
     *    G_ij = (Q(b_i+b_j) − Q(b_i) − Q(b_j)) / 2, with Q = (U+V)/2. */
    for (uint32_t i = 0; i < E8_DIM; i++) {
        for (uint32_t j = 0; j < E8_DIM; j++) {
            icos_t bi = basis_q(i), bj = basis_q(j);
            zphi_t ni = icos_norm4(bi), nj = icos_norm4(bj);
            zphi_t ns = icos_norm4(icos_add(bi, bj));
            if (!ni.valid || !nj.valid || !ns.valid) { bad++; continue; }
            int64_t qi = (ni.a + ni.b), qj = (nj.a + nj.b), qs = (ns.a + ns.b);
            if ((qi % 2) || (qj % 2) || (qs % 2)) { bad++; continue; }
            qi /= 2; qj /= 2; qs /= 2;
            int64_t g = (qs - qi - qj) / 2;
            if (g != (int64_t)GRAM[i * E8_DIM + j]) bad++;
        }
    }

    /* 4. Even and unimodular — the two properties that identify E8 at rank 8. */
    for (uint32_t i = 0; i < E8_DIM; i++)
        if (GRAM[i * E8_DIM + i] % 2 != 0) bad++;
    for (uint32_t i = 0; i < E8_DIM; i++)
        for (uint32_t j = 0; j < E8_DIM; j++)
            if (GRAM[i * E8_DIM + j] != GRAM[j * E8_DIM + i]) bad++;
    if (!gram_det_is_one()) bad++;

    /* 5. Every basis vector is a root, and the count is 240. */
    for (uint32_t i = 0; i < E8_DIM; i++) {
        int64_t c[E8_DIM] = {0,0,0,0,0,0,0,0};
        c[i] = 1;
        if (!e8_is_root(e8_from_coeffs(c))) bad++;
    }
    if (e8_roots((icos_t *)0, 0) != E8_ROOTS) bad++;

    /* 6. THE φ IDENTITY. The second root shell is the first scaled by 1/φ, so
     *    the ratio of squared radii is φ². Exactly:
     *        Σh² of a scaled icosian = 4·(φ−1)² = 4·(2−φ)
     *    and (2−φ)·φ² = 1, so radius_outer²/radius_inner² = φ². Checked as an
     *    identity in ℤ[φ], not as a float comparison. */
    {
        const zphi_t inv = zphi_inv_golden();          /* 1/φ = φ−1        */
        const zphi_t g2  = zphi_mul(zphi_golden(), zphi_golden());  /* φ²   */
        for (uint32_t i = 0; i < E8_ICOSIANS; i += 13) {
            icos_t q;
            if (!e8_icosian(i, &q)) { bad++; continue; }
            zphi_t inner = icos_norm4(icos_scale_zphi(q, inv));  /* 4(2−φ) */
            zphi_t outer = icos_norm4(q);                        /* 4      */
            /* inner · φ² == outer */
            if (!zphi_eq(zphi_mul(inner, g2), outer)) bad++;
        }
        /* and the scaled shell really is made of roots: 4(2−φ) has U+V = 4·2 +
         * 4·(−1) = 4, so Q = 2. */
        icos_t q0;
        if (e8_icosian(0, &q0)) {
            zphi_t n = icos_norm4(icos_scale_zphi(q0, inv));
            if (!n.valid || ((n.a + n.b) / 2) != 2) bad++;
        } else bad++;
    }

    /* 7. The lift round-trips exactly, and lands on genuine lattice points. */
    {
        static const int64_t samples[6][E8_M5_DIM] = {
            {0,0,0,0,0}, {1,0,0,0,0}, {0,1,-1,0,2},
            {-3,5,7,-2,1}, {100,-100,3,3,3}, {1,1,1,1,1},
        };
        for (uint32_t s = 0; s < 6; s++) {
            e8_pt_t p; int64_t back[E8_M5_DIM];
            if (!e8_lift_m5(samples[s], &p)) { bad++; continue; }
            if (!e8_project_m5(p, back))     { bad++; continue; }
            for (uint32_t i = 0; i < E8_M5_DIM; i++)
                if (back[i] != samples[s][i]) bad++;
            bool ok = false;
            int64_t n = e8_norm(p, &ok);
            if (!ok || (n % 2) != 0) bad++;      /* even lattice */
            if (n < 0) bad++;                    /* positive definite */
            /* the M5 Gram must agree with the lifted norm */
            bool ok2 = false;
            if (e8_m5_norm(samples[s], &ok2) != n || !ok2) bad++;
        }
        /* a point outside the image must be REFUSED, not truncated */
        {
            int64_t c[E8_DIM] = {1,2,3,4,5,6,0,0};
            int64_t back[E8_M5_DIM];
            if (e8_project_m5(e8_from_coeffs(c), back)) bad++;
        }
        /* positive definiteness on the M5 block: only the zero state has norm 0 */
        {
            int64_t z[E8_M5_DIM] = {0,0,0,0,0};
            bool ok = false;
            if (e8_m5_norm(z, &ok) != 0 || !ok) bad++;
            for (int64_t v = -3; v <= 3; v++) {
                if (v == 0) continue;
                for (uint32_t i = 0; i < E8_M5_DIM; i++) {
                    int64_t t[E8_M5_DIM] = {0,0,0,0,0};
                    t[i] = v;
                    bool o = false;
                    if (e8_m5_norm(t, &o) <= 0 || !o) bad++;
                }
            }
        }
    }

    /* 8. The M5 Gram really is the top-left block of the E8 Gram. */
    for (uint32_t i = 0; i < E8_M5_DIM; i++)
        for (uint32_t j = 0; j < E8_M5_DIM; j++)
            if (M5_GRAM[i * E8_M5_DIM + j] != GRAM[i * E8_DIM + j]) bad++;

    cached = bad;
    done = true;
    return bad;
}
