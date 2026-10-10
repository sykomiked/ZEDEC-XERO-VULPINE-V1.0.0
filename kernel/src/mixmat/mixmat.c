/* mixmat.c — exact rational mixing matrices. See mixmat.h for the derivation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mixmat.h"

static inline bool dim_ok(uint32_t n) { return n > 0 && n <= MIXMAT_MAX; }

mixmat_t mixmat_zero(uint32_t n) {
    mixmat_t m;
    m.n = dim_ok(n) ? n : 0;
    m.valid = dim_ok(n);
    for (uint32_t i = 0; i < MIXMAT_MAX; i++)
        for (uint32_t j = 0; j < MIXMAT_MAX; j++) m.a[i][j] = rat_zero();
    return m;
}

mixmat_t mixmat_identity(uint32_t n) {
    mixmat_t m = mixmat_zero(n);
    if (!m.valid) return m;
    for (uint32_t i = 0; i < n; i++) m.a[i][i] = rat_from_int(1);
    return m;
}

bool mixmat_set_row_counts(mixmat_t *m, uint32_t row,
                           const int64_t *counts, uint32_t n) {
    if (!m || !m->valid || !counts || row >= m->n || n != m->n) return false;
    int64_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (counts[i] < 0) return false;                 /* no negative parts */
        if (__builtin_add_overflow(total, counts[i], &total)) return false;
    }
    /* A row of all zeros has no whole to take parts of. Refuse it rather than
     * emit a row that sums to 0 and silently breaks the stochastic invariant
     * everything downstream relies on. */
    if (total == 0) return false;
    for (uint32_t i = 0; i < n; i++) {
        m->a[row][i] = rat_make(counts[i], total);
        if (!m->a[row][i].valid) return false;
    }
    return true;
}

bool mixmat_is_stochastic(const mixmat_t *m) {
    if (!m || !m->valid || !dim_ok(m->n)) return false;
    const rat_t one = rat_from_int(1), zero = rat_zero();
    for (uint32_t i = 0; i < m->n; i++) {
        rat_t sum = rat_zero();
        for (uint32_t j = 0; j < m->n; j++) {
            if (!m->a[i][j].valid) return false;
            if (rat_cmp(m->a[i][j], zero) < 0) return false;   /* non-negative */
            sum = rat_add(sum, m->a[i][j]);
            if (!sum.valid) return false;
        }
        /* EXACT equality. No epsilon: with rationals, "sums to 1" is decidable,
         * so accepting 0.9999 would be a choice to be wrong. */
        if (!rat_eq(sum, one)) return false;
    }
    return true;
}

mixmat_t mixmat_mul(const mixmat_t *x, const mixmat_t *y) {
    mixmat_t r = mixmat_zero(x && dim_ok(x->n) ? x->n : 1);
    if (!x || !y || !x->valid || !y->valid || x->n != y->n) { r.valid = false; return r; }
    r.n = x->n;
    for (uint32_t i = 0; i < r.n; i++)
        for (uint32_t j = 0; j < r.n; j++) {
            rat_t acc = rat_zero();
            for (uint32_t k = 0; k < r.n; k++) {
                rat_t t = rat_mul(x->a[i][k], y->a[k][j]);
                acc = rat_add(acc, t);
                if (!acc.valid) { r.valid = false; return r; }
            }
            r.a[i][j] = acc;
        }
    return r;
}

mixmat_t mixmat_pow(const mixmat_t *x, uint32_t k) {
    if (!x || !x->valid) { mixmat_t bad = mixmat_zero(1); bad.valid = false; return bad; }
    mixmat_t acc = mixmat_identity(x->n), base = *x;
    while (k) {
        if (k & 1u) { acc = mixmat_mul(&acc, &base); if (!acc.valid) return acc; }
        k >>= 1;
        if (k) { base = mixmat_mul(&base, &base); if (!base.valid) return base; }
    }
    return acc;
}

mixvec_t mixmat_apply(const mixmat_t *m, const mixvec_t *v) {
    mixvec_t r;
    r.n = 0; r.valid = false;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) r.v[i] = rat_zero();
    if (!m || !v || !m->valid || !v->valid || m->n != v->n) return r;
    r.n = m->n;
    for (uint32_t i = 0; i < m->n; i++) {
        rat_t acc = rat_zero();
        for (uint32_t j = 0; j < m->n; j++) {
            acc = rat_add(acc, rat_mul(m->a[i][j], v->v[j]));
            if (!acc.valid) return r;
        }
        r.v[i] = acc;
    }
    r.valid = true;
    return r;
}

mixvec_t mixmat_compound(const mixmat_t *m, const mixvec_t *v, uint32_t level) {
    mixvec_t r = mixmat_apply(m, v);
    if (!r.valid) return r;
    /* scale = 2^level — the paper's repeated squaring, which in log space is
     * repeated DOUBLING. Guarded so the shift cannot be undefined. */
    if (level >= 62u) { r.valid = false; return r; }
    rat_t scale = rat_from_int((int64_t)1 << level);
    for (uint32_t i = 0; i < r.n; i++) {
        r.v[i] = rat_mul(r.v[i], scale);
        if (!r.v[i].valid) { r.valid = false; return r; }
    }
    return r;
}

/* WHY THIS IS A LINEAR SOLVE AND NOT AN ITERATION.
 *
 * The first version of this function squared M until two successive powers were
 * exactly equal. That is wrong in PRINCIPLE, not merely slow: for a genuinely
 * mixing matrix, M^k approaches its limit ASYMPTOTICALLY and never attains it,
 * so exact equality never holds and the loop always fails. It also overflows —
 * rational denominators multiply, and the denominators of M^k grow without
 * bound even though the VALUES converge.
 *
 * The stationary distribution is the solution of a linear system, so we solve
 * it: π M = π together with Σπ = 1. In exact rationals that is one Gaussian
 * elimination, giving the exact answer in finite time with no tolerance and no
 * iteration count. Equivalently: transpose to (Mᵀ − I)πᵀ = 0 and replace the
 * last (redundant) equation with the normalisation.
 *
 * A matrix can fail to have a unique stationary vector — a permutation, or one
 * with several closed communicating classes. That shows up as a singular system
 * and is reported as false, which is the honest answer rather than one of the
 * several fixed points picked arbitrarily. */
bool mixmat_stationary(const mixmat_t *m, mixvec_t *out) {
    if (!m || !m->valid || !out || !mixmat_is_stochastic(m)) return false;
    const uint32_t n = m->n;

    /* Augmented system A·x = b, n equations in n unknowns.
     * Rows 0..n-2 : (Mᵀ − I) x = 0     (one of the n is redundant)
     * Row  n-1    : Σ x_i = 1          (the normalisation) */
    rat_t A[MIXMAT_MAX][MIXMAT_MAX + 1];
    for (uint32_t i = 0; i + 1 < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            A[i][j] = m->a[j][i];                        /* transpose */
            if (i == j) A[i][j] = rat_sub(A[i][j], rat_from_int(1));
            if (!A[i][j].valid) return false;
        }
        A[i][n] = rat_zero();
    }
    for (uint32_t j = 0; j < n; j++) A[n - 1][j] = rat_from_int(1);
    A[n - 1][n] = rat_from_int(1);

    /* Gauss-Jordan over exact rationals. No pivoting tolerance is needed or
     * meaningful: a pivot is either exactly zero or it is not. */
    for (uint32_t col = 0; col < n; col++) {
        uint32_t piv = col;
        while (piv < n && rat_is_zero(A[piv][col])) piv++;
        if (piv == n) return false;                      /* singular */
        if (piv != col)
            for (uint32_t j = 0; j <= n; j++) {
                rat_t t = A[col][j]; A[col][j] = A[piv][j]; A[piv][j] = t;
            }
        rat_t d = A[col][col];
        for (uint32_t j = 0; j <= n; j++) {
            A[col][j] = rat_div(A[col][j], d);
            if (!A[col][j].valid) return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            if (i == col || rat_is_zero(A[i][col])) continue;
            rat_t f = A[i][col];
            for (uint32_t j = 0; j <= n; j++) {
                A[i][j] = rat_sub(A[i][j], rat_mul(f, A[col][j]));
                if (!A[i][j].valid) return false;
            }
        }
    }

    out->n = n; out->valid = true;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) out->v[i] = rat_zero();
    rat_t sum = rat_zero();
    for (uint32_t i = 0; i < n; i++) {
        out->v[i] = A[i][n];
        if (rat_cmp(out->v[i], rat_zero()) < 0) { out->valid = false; return false; }
        sum = rat_add(sum, out->v[i]);
    }
    /* It must itself be a distribution, or we solved the wrong thing. */
    if (!sum.valid || !rat_eq(sum, rat_from_int(1))) { out->valid = false; return false; }
    return true;
}

bool mixmat_is_permutation(const mixmat_t *m) {
    if (!mixmat_is_stochastic(m)) return false;
    const rat_t one = rat_from_int(1);
    for (uint32_t i = 0; i < m->n; i++) {
        uint32_t ones = 0;
        for (uint32_t j = 0; j < m->n; j++) {
            if (rat_eq(m->a[i][j], one)) ones++;
            else if (!rat_is_zero(m->a[i][j])) return false;
        }
        if (ones != 1) return false;
    }
    for (uint32_t j = 0; j < m->n; j++) {           /* and one per COLUMN */
        uint32_t ones = 0;
        for (uint32_t i = 0; i < m->n; i++)
            if (rat_eq(m->a[i][j], one)) ones++;
        if (ones != 1) return false;
    }
    return true;
}

uint32_t mixmat_selfcheck(void) {
    uint32_t bad = 0;

    /* The compounding rows from the source paper: H2O and THC. Counts, not
     * decimals — the weights are exact by construction. */
    mixmat_t m = mixmat_zero(4);            /* basis: H, C, N, O */
    const int64_t h2o[4] = {2, 0, 0, 1};    /* 2 H, 1 O  -> 2/3, 0, 0, 1/3 */
    const int64_t thc[4] = {30, 21, 0, 2};  /* C21H30O2  -> 30/53, 21/53, 0, 2/53 */
    const int64_t gaba[4] = {9, 4, 1, 2};   /* C4H9NO2   -> 9/16, 1/4, 1/16, 1/8 */
    const int64_t ser[4]  = {12, 10, 2, 1}; /* C10H12N2O -> 12/25, 2/5, 2/25, 1/25 */
    if (!mixmat_set_row_counts(&m, 0, h2o, 4))  bad++;
    if (!mixmat_set_row_counts(&m, 1, thc, 4))  bad++;
    if (!mixmat_set_row_counts(&m, 2, gaba, 4)) bad++;
    if (!mixmat_set_row_counts(&m, 3, ser, 4))  bad++;
    if (!mixmat_is_stochastic(&m)) bad++;

    /* Spot-check two weights are the exact rationals the formula demands. */
    if (!rat_eq(m.a[0][0], rat_make(2, 3)))   bad++;
    if (!rat_eq(m.a[1][1], rat_make(21, 53))) bad++;

    /* 1. CLOSURE — the product and every power stay stochastic. This is the
     *    property that lets these operators nest without leaving the class.
     *
     *    AND ITS HONEST LIMIT. Rational denominators MULTIPLY: this matrix has
     *    denominators 3, 53, 16 and 25, so M^k carries a denominator on the
     *    order of (3·53·16·25)^k = 63600^k, which passes int64 at k=4. The
     *    values stay perfectly well-behaved; it is the exact REPRESENTATION
     *    that runs out. So the requirement is not "powers are always valid" —
     *    that would be false — it is "a power is either valid AND stochastic,
     *    or it reports invalid". Never valid-but-wrong. */
    {
        mixmat_t p = mixmat_mul(&m, &m);
        if (!p.valid || !mixmat_is_stochastic(&p)) bad++;
        bool overflowed = false;
        for (uint32_t k = 1; k <= 8; k++) {
            mixmat_t q = mixmat_pow(&m, k);
            if (q.valid) { if (!mixmat_is_stochastic(&q)) bad++; }
            else overflowed = true;
        }
        if (!overflowed) bad++;   /* if it never overflows, this test is not
                                   * exercising the reporting path at all */
        /* Small denominators: closure must hold cleanly and deeply. */
        {
            mixmat_t s = mixmat_zero(3);
            const int64_t a0[3] = {1,1,0}, a1[3] = {0,1,1}, a2[3] = {1,0,1};
            if (!mixmat_set_row_counts(&s, 0, a0, 3)) bad++;
            if (!mixmat_set_row_counts(&s, 1, a1, 3)) bad++;
            if (!mixmat_set_row_counts(&s, 2, a2, 3)) bad++;
            for (uint32_t k = 1; k <= 12; k++) {
                mixmat_t q = mixmat_pow(&s, k);
                if (q.valid && !mixmat_is_stochastic(&q)) bad++;
            }
        }
        mixmat_t id = mixmat_identity(4);
        if (!mixmat_is_stochastic(&id)) bad++;
        mixmat_t k0 = mixmat_pow(&m, 0);          /* M^0 == I */
        for (uint32_t i = 0; i < 4; i++)
            for (uint32_t j = 0; j < 4; j++)
                if (!rat_eq(k0.a[i][j], id.a[i][j])) bad++;
    }

    /* 2. THE ALL-ONES EIGENVECTOR. M·1 == 1 for any stochastic M — this IS the
     *    row-sum condition, viewed as an eigenvalue-1 statement. */
    {
        mixvec_t ones; ones.n = 4; ones.valid = true;
        for (uint32_t i = 0; i < 4; i++) ones.v[i] = rat_from_int(1);
        for (uint32_t i = 4; i < MIXMAT_MAX; i++) ones.v[i] = rat_zero();
        mixvec_t r = mixmat_apply(&m, &ones);
        if (!r.valid) bad++;
        else for (uint32_t i = 0; i < 4; i++)
            if (!rat_eq(r.v[i], rat_from_int(1))) bad++;
    }

    /* 3. THE LEVEL. compound at level k scales by exactly 2^k and nothing else. */
    {
        mixvec_t v; v.n = 4; v.valid = true;
        for (uint32_t i = 0; i < MIXMAT_MAX; i++) v.v[i] = rat_zero();
        for (uint32_t i = 0; i < 4; i++) v.v[i] = rat_from_int((int64_t)i + 1);
        mixvec_t base = mixmat_apply(&m, &v);
        for (uint32_t lv = 0; lv <= 4; lv++) {
            mixvec_t c = mixmat_compound(&m, &v, lv);
            if (!c.valid || !base.valid) { bad++; continue; }
            rat_t s = rat_from_int((int64_t)1 << lv);
            for (uint32_t i = 0; i < 4; i++)
                if (!rat_eq(c.v[i], rat_mul(base.v[i], s))) bad++;
        }
    }

    /* 4. THE FIXED POINT. A strictly positive matrix is primitive and has a
     *    unique stationary distribution. The test that matters is not "the
     *    solver returned something" but "what it returned actually IS fixed":
     *    πM must equal π exactly. */
    {
        mixmat_t p = mixmat_zero(3);
        const int64_t r0[3] = {2, 1, 1}, r1[3] = {1, 1, 1}, r2[3] = {1, 1, 2};
        if (!mixmat_set_row_counts(&p, 0, r0, 3)) bad++;
        if (!mixmat_set_row_counts(&p, 1, r1, 3)) bad++;
        if (!mixmat_set_row_counts(&p, 2, r2, 3)) bad++;
        mixvec_t pi;
        if (!mixmat_stationary(&p, &pi)) bad++;
        else {
            /* pi M == pi, checked as a left eigenvector, exactly. */
            for (uint32_t j = 0; j < 3; j++) {
                rat_t acc = rat_zero();
                for (uint32_t i = 0; i < 3; i++)
                    acc = rat_add(acc, rat_mul(pi.v[i], p.a[i][j]));
                if (!acc.valid || !rat_eq(acc, pi.v[j])) bad++;
            }
            rat_t s = rat_zero();
            for (uint32_t i = 0; i < 3; i++) s = rat_add(s, pi.v[i]);
            if (!rat_eq(s, rat_from_int(1))) bad++;
        }
        /* The identity's stationary vector is not unique -> must be refused. */
        {
            mixmat_t id = mixmat_identity(3);
            mixvec_t junk;
            if (mixmat_stationary(&id, &junk)) bad++;
        }
    }

    /* 5. PERMUTATION vs MIXING. A permutation is stochastic but never settles;
     *    saying so is the point — a loop built from permutations cycles forever
     *    and must not be reported as convergent. */
    {
        mixmat_t sw = mixmat_zero(3);
        const int64_t a[3] = {0,1,0}, b[3] = {0,0,1}, c[3] = {1,0,0};
        mixmat_set_row_counts(&sw, 0, a, 3);
        mixmat_set_row_counts(&sw, 1, b, 3);
        mixmat_set_row_counts(&sw, 2, c, 3);
        if (!mixmat_is_stochastic(&sw))  bad++;
        if (!mixmat_is_permutation(&sw)) bad++;
        /* A 3-cycle permutes forever; its stationary vector exists (uniform)
         * but the loop never SETTLES into it. What must not happen is a
         * confident wrong answer, so we only require the solver to be
         * self-consistent: whatever it returns must genuinely satisfy piM = pi. */
        {
            mixvec_t pi;
            if (mixmat_stationary(&sw, &pi)) {
                for (uint32_t j = 0; j < 3; j++) {
                    rat_t acc = rat_zero();
                    for (uint32_t i = 0; i < 3; i++)
                        acc = rat_add(acc, rat_mul(pi.v[i], sw.a[i][j]));
                    if (!acc.valid || !rat_eq(acc, pi.v[j])) bad++;
                }
            }
        }
        if (mixmat_is_permutation(&m))         bad++;   /* m mixes, not permutes */
        mixmat_t id = mixmat_identity(3);
        if (!mixmat_is_permutation(&id)) bad++;
    }

    /* 6. Degenerate input is refused, not absorbed. */
    {
        mixmat_t z = mixmat_zero(3);
        const int64_t none[3] = {0,0,0}, neg[3] = {1,-1,1};
        if (mixmat_set_row_counts(&z, 0, none, 3)) bad++;  /* no whole */
        if (mixmat_set_row_counts(&z, 0, neg,  3)) bad++;  /* negative part */
        if (mixmat_is_stochastic(&z)) bad++;               /* all-zero rows */
    }
    return bad;
}
