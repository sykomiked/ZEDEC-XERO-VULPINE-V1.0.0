/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qsim.c — exact classical simulation of small quantum circuits over the
 * cyclotomic ring Z[e^(2 pi i/N)], N in {8, 16}, with a shared sqrt(2)^k
 * denominator. See qsim.h for the model and the HONEST LIMITS.
 *
 * WHAT IT DOES. Ring arithmetic on N/2 int64 coefficients (every add and
 * multiply overflow-checked), gate application on the 2^n amplitudes, the
 * sqrt(2) reduction that keeps k minimal, exact probabilities as real ring
 * elements, and an exact sign test for real elements built on a small
 * fixed-size multi-precision integer (QS_WL 32-bit limbs).
 *
 * Exact sign test. A real element of Z[z8] is a + b*sqrt2; its sign follows
 * from the signs of a and b, and when they differ from comparing a^2 with
 * 2*b^2. A real element of Z[z16] is A + alpha*B with A, B in Z[sqrt2] and
 * alpha = 2cos(pi/8) = sqrt(2 + sqrt2) > 0 (because 2cos(3pi/8) =
 * alpha*(sqrt2 - 1)); when A and B have different signs the answer follows
 * from the sign of A^2 - (2 + sqrt2)*B^2, again in Z[sqrt2].
 *
 * HONEST LIMITS. A classical simulator: exponential in the qubit count, no
 * speedup. Overflow is reported (QS_ERR_OVERFLOW), never wrapped. Integer
 * only, no allocation, no libc.
 */
#include "qsim.h"

/* ---------------------------------------------------------------- */
/* checked int64 helpers                                            */
/* ---------------------------------------------------------------- */

static int qs_add64(int64_t a, int64_t b, int64_t *r)
{
    return __builtin_add_overflow(a, b, r) ? QS_ERR_OVERFLOW : QS_OK;
}

static int qs_sub64(int64_t a, int64_t b, int64_t *r)
{
    return __builtin_sub_overflow(a, b, r) ? QS_ERR_OVERFLOW : QS_OK;
}

static int qs_mul64(int64_t a, int64_t b, int64_t *r)
{
    return __builtin_mul_overflow(a, b, r) ? QS_ERR_OVERFLOW : QS_OK;
}

/* floor(a / 2), exact for every int64 (no shift of a negative value). */
static int64_t qs_floor_half(int64_t a)
{
    return (a - (a & 1)) / 2;
}

/* ---------------------------------------------------------------- */
/* ring elements                                                    */
/* ---------------------------------------------------------------- */

static void qs_e_zero(qs_elem_t *e)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++) e->c[i] = 0;
}

static void qs_e_copy(qs_elem_t *d, const qs_elem_t *s)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++) d->c[i] = s->c[i];
}

static int qs_e_is_zero(const qs_elem_t *e)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++)
        if (e->c[i] != 0) return 0;
    return 1;
}

/* Coefficient j of x * z^e (0 <= e < 2D, j < D). */
static int qs_coef_shift(const qs_elem_t *x, uint32_t D, uint32_t e, uint32_t j, int64_t *out)
{
    int32_t i = (int32_t) j - (int32_t) e;
    int neg = 0;
    while (i < 0) {
        i += (int32_t) D;
        neg = !neg;
    }
    if (!neg) {
        *out = x->c[i];
        return QS_OK;
    }
    return qs_sub64(0, x->c[i], out);
}

/* r = x * z^e; r must not alias x. */
static int qs_e_mulz(qs_elem_t *r, const qs_elem_t *x, uint32_t D, uint32_t e)
{
    qs_e_zero(r);
    for (uint32_t j = 0; j < D; j++) {
        int rc = qs_coef_shift(x, D, e, j, &r->c[j]);
        if (rc) return rc;
    }
    return QS_OK;
}

/* r = a * b; r must not alias a or b. */
static int qs_e_mul(qs_elem_t *r, const qs_elem_t *a, const qs_elem_t *b, uint32_t D)
{
    qs_e_zero(r);
    for (uint32_t i = 0; i < D; i++) {
        if (a->c[i] == 0) continue;
        for (uint32_t j = 0; j < D; j++) {
            int64_t p;
            if (qs_mul64(a->c[i], b->c[j], &p)) return QS_ERR_OVERFLOW;
            uint32_t idx = i + j;
            if (idx >= D) {
                if (qs_sub64(r->c[idx - D], p, &r->c[idx - D])) return QS_ERR_OVERFLOW;
            } else {
                if (qs_add64(r->c[idx], p, &r->c[idx])) return QS_ERR_OVERFLOW;
            }
        }
    }
    return QS_OK;
}

/* r = complex conjugate of x: conj(z^j) = z^-j = -z^(D-j). */
static int qs_e_conj(qs_elem_t *r, const qs_elem_t *x, uint32_t D)
{
    qs_e_zero(r);
    r->c[0] = x->c[0];
    for (uint32_t j = 1; j < D; j++)
        if (qs_sub64(0, x->c[j], &r->c[D - j])) return QS_ERR_OVERFLOW;
    return QS_OK;
}

/* |x|^2 = x * conj(x), a real element. */
static int qs_e_abs2(qs_elem_t *r, const qs_elem_t *x, uint32_t D)
{
    qs_elem_t cj;
    int rc = qs_e_conj(&cj, x, D);
    if (rc) return rc;
    return qs_e_mul(r, x, &cj, D);
}

static int qs_e_add_to(qs_elem_t *acc, const qs_elem_t *x, uint32_t D)
{
    for (uint32_t j = 0; j < D; j++)
        if (qs_add64(acc->c[j], x->c[j], &acc->c[j])) return QS_ERR_OVERFLOW;
    return QS_OK;
}

/* sqrt2 = z8 - z8^3 = z^s - z^(3s) with s = N/8 = D/4. x is divisible by
 * sqrt2 iff x*sqrt2 has only even coefficients; then x/sqrt2 = x*sqrt2/2.
 * Coefficient j of x*sqrt2 is u - v with u, v = +-x[..], so the parity test
 * needs only the two source coefficients. */
static int qs_e_sqrt2_divisible(const qs_elem_t *x, uint32_t D)
{
    uint32_t s = D / 4u;
    for (uint32_t j = 0; j < D; j++) {
        int32_t i1 = (int32_t) j - (int32_t) s, i3 = (int32_t) j - (int32_t) (3u * s);
        while (i1 < 0) i1 += (int32_t) D;
        while (i3 < 0) i3 += (int32_t) D;
        if (((x->c[i1] ^ x->c[i3]) & 1) != 0) return 0;
    }
    return 1;
}

/* r = x / sqrt2, x divisible (checked by the caller). r != x. */
static int qs_e_div_sqrt2(qs_elem_t *r, const qs_elem_t *x, uint32_t D)
{
    uint32_t s = D / 4u;
    qs_e_zero(r);
    for (uint32_t j = 0; j < D; j++) {
        int64_t u, v;
        int rc = qs_coef_shift(x, D, s, j, &u);
        if (rc) return rc;
        rc = qs_coef_shift(x, D, 3u * s, j, &v);
        if (rc) return rc;
        /* u and v have equal parity, so (u - v)/2 = floor(u/2) - floor(v/2)
         * and that difference cannot overflow. */
        r->c[j] = qs_floor_half(u) - qs_floor_half(v);
    }
    return QS_OK;
}

/* ---------------------------------------------------------------- */
/* multi-precision signed integers (sign-magnitude, fixed width)    */
/* ---------------------------------------------------------------- */

#define QS_WL 20u /* 640 bits: enough for 4th powers of 128-bit values */

typedef struct {
    uint32_t m[QS_WL];
    int32_t s; /* -1, 0, +1 */
} qs_w_t;

static void qs_w_copy(qs_w_t *d, const qs_w_t *a)
{
    for (uint32_t i = 0; i < QS_WL; i++) d->m[i] = a->m[i];
    d->s = a->s;
}

static void qs_w_fix(qs_w_t *w)
{
    for (uint32_t i = 0; i < QS_WL; i++)
        if (w->m[i] != 0) return;
    w->s = 0;
}

static void qs_w_from_i64(qs_w_t *w, int64_t x)
{
    uint64_t mag = x < 0 ? (uint64_t) 0 - (uint64_t) x : (uint64_t) x;
    for (uint32_t i = 0; i < QS_WL; i++) w->m[i] = 0;
    w->m[0] = (uint32_t) mag;
    w->m[1] = (uint32_t) (mag >> 32);
    w->s = x < 0 ? -1 : (x > 0 ? 1 : 0);
}

static int qs_w_cmp_mag(const qs_w_t *a, const qs_w_t *b)
{
    for (uint32_t i = QS_WL; i-- > 0;) {
        if (a->m[i] > b->m[i]) return 1;
        if (a->m[i] < b->m[i]) return -1;
    }
    return 0;
}

/* r = a + b (signed). r may alias a or b. */
static int qs_w_add(qs_w_t *r, const qs_w_t *a, const qs_w_t *b)
{
    qs_w_t t;
    if (a->s == 0) {
        qs_w_copy(r, b);
        return QS_OK;
    }
    if (b->s == 0) {
        qs_w_copy(r, a);
        return QS_OK;
    }
    if (a->s == b->s) {
        uint64_t carry = 0;
        for (uint32_t i = 0; i < QS_WL; i++) {
            uint64_t v = (uint64_t) a->m[i] + b->m[i] + carry;
            t.m[i] = (uint32_t) v;
            carry = v >> 32;
        }
        if (carry) return QS_ERR_OVERFLOW;
        t.s = a->s;
    } else {
        const qs_w_t *big = a, *sml = b;
        int c = qs_w_cmp_mag(a, b);
        if (c < 0) {
            big = b;
            sml = a;
        }
        uint64_t borrow = 0;
        for (uint32_t i = 0; i < QS_WL; i++) {
            uint64_t v = (uint64_t) big->m[i] - sml->m[i] - borrow;
            t.m[i] = (uint32_t) v;
            borrow = (v >> 32) & 1u;
        }
        t.s = big->s;
        qs_w_fix(&t);
    }
    qs_w_copy(r, &t);
    return QS_OK;
}

static int qs_w_sub(qs_w_t *r, const qs_w_t *a, const qs_w_t *b)
{
    qs_w_t nb;
    qs_w_copy(&nb, b);
    nb.s = -nb.s;
    return qs_w_add(r, a, &nb);
}

/* r = a * b. r may alias a or b. */
static int qs_w_mul(qs_w_t *r, const qs_w_t *a, const qs_w_t *b)
{
    uint32_t t[2u * QS_WL];
    for (uint32_t i = 0; i < 2u * QS_WL; i++) t[i] = 0;
    for (uint32_t i = 0; i < QS_WL; i++) {
        if (a->m[i] == 0) continue;
        uint64_t carry = 0;
        for (uint32_t j = 0; j < QS_WL; j++) {
            uint64_t v = (uint64_t) a->m[i] * b->m[j] + t[i + j] + carry;
            t[i + j] = (uint32_t) v;
            carry = v >> 32;
        }
        t[i + QS_WL] = (uint32_t) carry;
    }
    for (uint32_t i = QS_WL; i < 2u * QS_WL; i++)
        if (t[i] != 0) return QS_ERR_OVERFLOW;
    for (uint32_t i = 0; i < QS_WL; i++) r->m[i] = t[i];
    r->s = a->s * b->s;
    qs_w_fix(r);
    return QS_OK;
}

/* Sign of a + b*sqrt2. */
static int qs_w_sign_zs2(const qs_w_t *a, const qs_w_t *b, int *sign)
{
    if (b->s == 0 || a->s == b->s) {
        *sign = a->s != 0 ? a->s : b->s;
        return QS_OK;
    }
    if (a->s == 0) {
        *sign = b->s;
        return QS_OK;
    }
    qs_w_t a2, b2;
    int rc = qs_w_mul(&a2, a, a);
    if (!rc) rc = qs_w_mul(&b2, b, b);
    if (!rc) rc = qs_w_add(&b2, &b2, &b2);
    if (rc) return rc;
    int c = qs_w_cmp_mag(&a2, &b2); /* equality is impossible: sqrt2 is irrational */
    *sign = c > 0 ? a->s : (c < 0 ? b->s : 0);
    return QS_OK;
}

/* Sign of a real element from its coordinates r[0..3] (see qs_real_coords). */
static int qs_w_sign_real(uint32_t N, qs_w_t r[4], int *sign)
{
    if (N == 8u) return qs_w_sign_zs2(&r[0], &r[1], sign);
    /* x = A + alpha*B, A = r0 + r2*sqrt2, B = (r1 - r3) + r3*sqrt2. */
    qs_w_t b0;
    int rc = qs_w_sub(&b0, &r[1], &r[3]);
    if (rc) return rc;
    int sa, sb;
    rc = qs_w_sign_zs2(&r[0], &r[2], &sa);
    if (!rc) rc = qs_w_sign_zs2(&b0, &r[3], &sb);
    if (rc) return rc;
    if (sb == 0 || sa == sb) {
        *sign = sa != 0 ? sa : sb;
        return QS_OK;
    }
    if (sa == 0) {
        *sign = sb;
        return QS_OK;
    }
    /* d = A^2 - (2 + sqrt2) B^2.  A^2 = (a0^2 + 2a1^2) + 2a0a1 sqrt2;
     * B^2 = P + Q sqrt2; (2+sqrt2)(P + Q sqrt2) = (2P + 2Q) + (P + 2Q) sqrt2. */
    qs_w_t t1, t2, a20, a21, P, Q, d0, d1;
    rc = qs_w_mul(&a20, &r[0], &r[0]);
    if (!rc) rc = qs_w_mul(&t1, &r[2], &r[2]);
    if (!rc) rc = qs_w_add(&a20, &a20, &t1);
    if (!rc) rc = qs_w_add(&a20, &a20, &t1);
    if (!rc) rc = qs_w_mul(&a21, &r[0], &r[2]);
    if (!rc) rc = qs_w_add(&a21, &a21, &a21);
    if (!rc) rc = qs_w_mul(&P, &b0, &b0);
    if (!rc) rc = qs_w_mul(&t2, &r[3], &r[3]);
    if (!rc) rc = qs_w_add(&P, &P, &t2);
    if (!rc) rc = qs_w_add(&P, &P, &t2);
    if (!rc) rc = qs_w_mul(&Q, &b0, &r[3]);
    if (!rc) rc = qs_w_add(&Q, &Q, &Q);
    /* d0 = a20 - 2P - 2Q, d1 = a21 - P - 2Q */
    if (!rc) rc = qs_w_add(&t1, &P, &Q);   /* P + Q */
    if (!rc) rc = qs_w_add(&t1, &t1, &t1); /* 2P + 2Q */
    if (!rc) rc = qs_w_sub(&d0, &a20, &t1);
    if (!rc) rc = qs_w_add(&t2, &Q, &Q);  /* 2Q */
    if (!rc) rc = qs_w_add(&t2, &t2, &P); /* P + 2Q */
    if (!rc) rc = qs_w_sub(&d1, &a21, &t2);
    if (rc) return rc;
    int sd;
    rc = qs_w_sign_zs2(&d0, &d1, &sd);
    if (rc) return rc;
    *sign = sd > 0 ? sa : (sd < 0 ? sb : 0);
    return QS_OK;
}

/* ---------------------------------------------------------------- */
/* state validation and reduction                                   */
/* ---------------------------------------------------------------- */

static int qs_valid_N(uint32_t N)
{
    return N == 8u || N == 16u;
}

static int qs_check_state(const qs_state_t *st)
{
    if (!st) return QS_ERR_NULL;
    if (st->broken) return QS_ERR_BROKEN;
    if (st->n == 0 || st->n > QS_MAX_QUBITS || !qs_valid_N(st->N) || st->D * 2u != st->N)
        return QS_ERR_ARG;
    return QS_OK;
}

/* Lower k while every amplitude is divisible by sqrt2. */
static int qs_reduce(qs_state_t *st)
{
    uint32_t dim = 1u << st->n;
    while (st->k > 0) {
        for (uint32_t i = 0; i < dim; i++)
            if (!qs_e_sqrt2_divisible(&st->amp[i], st->D)) return QS_OK;
        for (uint32_t i = 0; i < dim; i++) {
            qs_elem_t t;
            int rc = qs_e_div_sqrt2(&t, &st->amp[i], st->D);
            if (rc) {
                st->broken = 1;
                return rc;
            }
            qs_e_copy(&st->amp[i], &t);
        }
        st->k--;
    }
    return QS_OK;
}

static int qs_setup(qs_state_t *st, uint32_t n, uint32_t N)
{
    if (!st) return QS_ERR_NULL;
    if (n == 0 || n > QS_MAX_QUBITS) return QS_ERR_TOO_MANY;
    if (!qs_valid_N(N)) return QS_ERR_ARG;
    st->n = n;
    st->N = N;
    st->D = N / 2u;
    st->k = 0;
    st->broken = 0;
    st->collapsed = 0;
    for (uint32_t i = 0; i < QS_MAX_AMPS; i++) qs_e_zero(&st->amp[i]);
    return QS_OK;
}

int qs_init_basis(qs_state_t *st, uint32_t n, uint32_t N, uint32_t idx)
{
    if (!st) return QS_ERR_NULL;
    if (n == 0 || n > QS_MAX_QUBITS) return QS_ERR_TOO_MANY;
    if (idx >= (1u << n)) return QS_ERR_INDEX;
    int rc = qs_setup(st, n, N);
    if (rc) return rc;
    st->amp[idx].c[0] = 1;
    return QS_OK;
}

int qs_init(qs_state_t *st, uint32_t n, uint32_t N)
{
    return qs_init_basis(st, n, N, 0);
}

#define QS_MAX_K 4096u

int qs_load(qs_state_t *st, uint32_t n, uint32_t N, uint32_t k, const qs_elem_t *amps)
{
    if (!st || !amps) return QS_ERR_NULL;
    if (n == 0 || n > QS_MAX_QUBITS) return QS_ERR_TOO_MANY;
    if (!qs_valid_N(N) || k > QS_MAX_K) return QS_ERR_ARG;
    uint32_t dim = 1u << n;
    for (uint32_t i = 0; i < dim; i++)
        for (uint32_t j = N / 2u; j < QS_MAX_D; j++)
            if (amps[i].c[j] != 0) return QS_ERR_ARG;
    int rc = qs_setup(st, n, N);
    if (rc) return rc;
    for (uint32_t i = 0; i < dim; i++) qs_e_copy(&st->amp[i], &amps[i]);
    st->k = k;
    return qs_reduce(st);
}

int qs_amplitude(const qs_state_t *st, uint32_t idx, qs_elem_t *out, uint32_t *k)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!out || !k) return QS_ERR_NULL;
    if (idx >= (1u << st->n)) return QS_ERR_INDEX;
    qs_e_copy(out, &st->amp[idx]);
    *k = st->k;
    return QS_OK;
}

int qs_state_equal(const qs_state_t *a, const qs_state_t *b, int *equal)
{
    int rc = qs_check_state(a);
    if (!rc) rc = qs_check_state(b);
    if (rc) return rc;
    if (!equal) return QS_ERR_NULL;
    if (a->n != b->n || a->N != b->N) return QS_ERR_MISMATCH;
    *equal = 0;
    if (a->k != b->k) return QS_OK; /* both reduced: representation is canonical */
    uint32_t dim = 1u << a->n;
    for (uint32_t i = 0; i < dim; i++)
        for (uint32_t j = 0; j < QS_MAX_D; j++)
            if (a->amp[i].c[j] != b->amp[i].c[j]) return QS_OK;
    *equal = 1;
    return QS_OK;
}

/* ---------------------------------------------------------------- */
/* gates                                                            */
/* ---------------------------------------------------------------- */

static int qs_fail(qs_state_t *st, int rc)
{
    st->broken = 1;
    return rc;
}

/* Controlled X: swap amplitude pairs on target t where all mask bits are 1. */
static void qs_do_x(qs_state_t *st, uint32_t mask, uint32_t t)
{
    uint32_t dim = 1u << st->n, tb = 1u << t;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) != 0 || (i & mask) != mask) continue;
        for (uint32_t j = 0; j < QS_MAX_D; j++) {
            int64_t v = st->amp[i].c[j];
            st->amp[i].c[j] = st->amp[i | tb].c[j];
            st->amp[i | tb].c[j] = v;
        }
    }
}

/* Multiply the target-1 amplitude by z^e where all mask bits are 1. */
static int qs_do_diag(qs_state_t *st, uint32_t mask, uint32_t t, uint32_t e)
{
    uint32_t dim = 1u << st->n, tb = 1u << t;
    if (e == 0) return QS_OK;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) == 0 || (i & mask) != mask) continue;
        qs_elem_t r;
        int rc = qs_e_mulz(&r, &st->amp[i], st->D, e);
        if (rc) return qs_fail(st, rc);
        qs_e_copy(&st->amp[i], &r);
    }
    return QS_OK;
}

/* Y = [[0, -i], [i, 0]];  i = z^(D/2), -i = z^(3D/2). */
static int qs_do_y(qs_state_t *st, uint32_t t)
{
    uint32_t dim = 1u << st->n, tb = 1u << t;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) != 0) continue;
        qs_elem_t n0, n1;
        int rc = qs_e_mulz(&n0, &st->amp[i | tb], st->D, 3u * st->D / 2u);
        if (!rc) rc = qs_e_mulz(&n1, &st->amp[i], st->D, st->D / 2u);
        if (rc) return qs_fail(st, rc);
        qs_e_copy(&st->amp[i], &n0);
        qs_e_copy(&st->amp[i | tb], &n1);
    }
    return QS_OK;
}

/* H: (a0, a1) -> (a0 + a1, a0 - a1) / sqrt2; k grows by one, then reduce. */
static int qs_do_h(qs_state_t *st, uint32_t t)
{
    uint32_t dim = 1u << st->n, tb = 1u << t;
    if (st->k >= QS_MAX_K) return qs_fail(st, QS_ERR_OVERFLOW);
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) != 0) continue;
        for (uint32_t j = 0; j < st->D; j++) {
            int64_t a0 = st->amp[i].c[j], a1 = st->amp[i | tb].c[j], s, d;
            if (qs_add64(a0, a1, &s) || qs_sub64(a0, a1, &d)) return qs_fail(st, QS_ERR_OVERFLOW);
            st->amp[i].c[j] = s;
            st->amp[i | tb].c[j] = d;
        }
    }
    st->k++;
    return qs_reduce(st);
}

static void qs_do_swap(qs_state_t *st, uint32_t a, uint32_t b)
{
    uint32_t dim = 1u << st->n, ab = 1u << a, bb = 1u << b;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & ab) == 0 || (i & bb) != 0) continue;
        uint32_t o = (i & ~ab) | bb;
        for (uint32_t j = 0; j < QS_MAX_D; j++) {
            int64_t v = st->amp[i].c[j];
            st->amp[i].c[j] = st->amp[o].c[j];
            st->amp[o].c[j] = v;
        }
    }
}

static int qs_valid_m(uint32_t m)
{
    return m == 1u || m == 2u || m == 4u || m == 8u || m == 16u;
}

/* Exponent of z (in [0, N)) for the phase e^(2 pi i k/m). */
static uint32_t qs_phase_exp(int32_t k, uint32_t m, uint32_t N)
{
    int32_t r = k % (int32_t) m;
    if (r < 0) r += (int32_t) m;
    return (uint32_t) r * (N / m);
}

static int qs_needs_c0(uint32_t kind)
{
    return kind == QS_G_CNOT || kind == QS_G_CZ || kind == QS_G_SWAP || kind == QS_G_CCX ||
           kind == QS_G_CPHASE;
}

static int qs_is_phase(uint32_t kind)
{
    return kind == QS_G_PHASE || kind == QS_G_CPHASE;
}

int qs_gate(qs_state_t *st, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1, int32_t k,
            uint32_t m)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (kind >= QS_G_COUNT) return QS_ERR_ARG;
    if (t >= st->n) return QS_ERR_QUBIT;
    if (qs_needs_c0(kind) && (c0 >= st->n || c0 == t)) return QS_ERR_QUBIT;
    if (kind == QS_G_CCX && (c1 >= st->n || c1 == t || c1 == c0)) return QS_ERR_QUBIT;
    if (qs_is_phase(kind)) {
        if (!qs_valid_m(m)) return QS_ERR_ARG;
        if (st->N % m != 0) return QS_ERR_PHASE;
    }
    uint32_t D = st->D;
    switch (kind) {
    case QS_G_I:
        return QS_OK;
    case QS_G_X:
        qs_do_x(st, 0, t);
        return QS_OK;
    case QS_G_Y:
        return qs_do_y(st, t);
    case QS_G_Z:
        return qs_do_diag(st, 0, t, D);
    case QS_G_H:
        return qs_do_h(st, t);
    case QS_G_S:
        return qs_do_diag(st, 0, t, D / 2u);
    case QS_G_SDG:
        return qs_do_diag(st, 0, t, 3u * D / 2u);
    case QS_G_T:
        return qs_do_diag(st, 0, t, D / 4u);
    case QS_G_TDG:
        return qs_do_diag(st, 0, t, 2u * D - D / 4u);
    case QS_G_PHASE:
        return qs_do_diag(st, 0, t, qs_phase_exp(k, m, st->N));
    case QS_G_CNOT:
        qs_do_x(st, 1u << c0, t);
        return QS_OK;
    case QS_G_CZ:
        return qs_do_diag(st, 1u << c0, t, D);
    case QS_G_SWAP:
        qs_do_swap(st, c0, t);
        return QS_OK;
    case QS_G_CCX:
        qs_do_x(st, (1u << c0) | (1u << c1), t);
        return QS_OK;
    default: /* QS_G_CPHASE */
        return qs_do_diag(st, 1u << c0, t, qs_phase_exp(k, m, st->N));
    }
}

int qs_apply(qs_state_t *st, const qs_gate_t *g)
{
    if (!g) return QS_ERR_NULL;
    return qs_gate(st, g->kind, g->t, g->c0, g->c1, g->k, g->m);
}

int qs_circuit_init(qs_circuit_t *c)
{
    if (!c) return QS_ERR_NULL;
    c->count = 0;
    return QS_OK;
}

int qs_circuit_push(qs_circuit_t *c, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1, int32_t k,
                    uint32_t m)
{
    if (!c) return QS_ERR_NULL;
    if (c->count > QS_MAX_GATES) return QS_ERR_ARG;
    if (kind >= QS_G_COUNT) return QS_ERR_ARG;
    if (t >= QS_MAX_QUBITS || c0 >= QS_MAX_QUBITS || c1 >= QS_MAX_QUBITS) return QS_ERR_QUBIT;
    if (qs_is_phase(kind) && !qs_valid_m(m)) return QS_ERR_ARG;
    if (c->count == QS_MAX_GATES) return QS_ERR_FULL;
    qs_gate_t *g = &c->g[c->count];
    g->kind = (uint8_t) kind;
    g->t = (uint8_t) t;
    g->c0 = (uint8_t) c0;
    g->c1 = (uint8_t) c1;
    g->k = qs_is_phase(kind) ? k : 0;
    g->m = qs_is_phase(kind) ? m : 0;
    c->count++;
    return QS_OK;
}

int qs_run_circuit(qs_state_t *st, const qs_circuit_t *c, uint32_t *done)
{
    if (done) *done = 0;
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!c) return QS_ERR_NULL;
    if (c->count > QS_MAX_GATES) return QS_ERR_ARG;
    for (uint32_t i = 0; i < c->count; i++) {
        rc = qs_apply(st, &c->g[i]);
        if (rc) return rc;
        if (done) *done = i + 1u;
    }
    return QS_OK;
}

int qs_circuit_adjoint(const qs_circuit_t *in, qs_circuit_t *out)
{
    if (!in || !out) return QS_ERR_NULL;
    if (in == out || in->count > QS_MAX_GATES) return QS_ERR_ARG;
    uint32_t n = in->count;
    for (uint32_t i = 0; i < n; i++) {
        const qs_gate_t *s = &in->g[n - 1u - i];
        qs_gate_t *d = &out->g[i];
        if (s->kind >= QS_G_COUNT) return QS_ERR_ARG;
        uint32_t kind = s->kind;
        if (kind == QS_G_S)
            kind = QS_G_SDG;
        else if (kind == QS_G_SDG)
            kind = QS_G_S;
        else if (kind == QS_G_T)
            kind = QS_G_TDG;
        else if (kind == QS_G_TDG)
            kind = QS_G_T;
        d->kind = (uint8_t) kind;
        d->t = s->t;
        d->c0 = s->c0;
        d->c1 = s->c1;
        d->m = s->m;
        d->k = 0;
        if (qs_is_phase(kind)) {
            if (!qs_valid_m(s->m)) return QS_ERR_ARG;
            int32_t r = s->k % (int32_t) s->m;
            if (r < 0) r += (int32_t) s->m;
            d->k = r == 0 ? 0 : (int32_t) s->m - r; /* -k mod m */
        }
    }
    out->count = n;
    return QS_OK;
}

/* ---------------------------------------------------------------- */
/* probabilities                                                    */
/* ---------------------------------------------------------------- */

/* Sum of |a_i|^2 over basis states i with (i & mask) == val. */
static int qs_weight(const qs_state_t *st, uint32_t mask, uint32_t val, qs_elem_t *out)
{
    uint32_t dim = 1u << st->n;
    qs_e_zero(out);
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & mask) != val || qs_e_is_zero(&st->amp[i])) continue;
        qs_elem_t a2;
        int rc = qs_e_abs2(&a2, &st->amp[i], st->D);
        if (!rc) rc = qs_e_add_to(out, &a2, st->D);
        if (rc) return rc;
    }
    return QS_OK;
}

int qs_norm_sq(const qs_state_t *st, qs_elem_t *num, uint32_t *k)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!num || !k) return QS_ERR_NULL;
    *k = st->k;
    return qs_weight(st, 0, 0, num);
}

int qs_check_unit(const qs_state_t *st)
{
    qs_elem_t nrm;
    uint32_t k;
    int rc = qs_norm_sq(st, &nrm, &k);
    if (rc) return rc;
    if (k > 62u) return QS_ERR_OVERFLOW;
    if (nrm.c[0] != ((int64_t) 1 << k)) return QS_ERR_NORM;
    for (uint32_t j = 1; j < QS_MAX_D; j++)
        if (nrm.c[j] != 0) return QS_ERR_NORM;
    return QS_OK;
}

int qs_prob_basis(const qs_state_t *st, uint32_t idx, qs_prob_t *p)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!p) return QS_ERR_NULL;
    if (idx >= (1u << st->n)) return QS_ERR_INDEX;
    rc = qs_e_abs2(&p->num, &st->amp[idx], st->D);
    if (!rc) rc = qs_weight(st, 0, 0, &p->den);
    return rc;
}

int qs_prob_qubit(const qs_state_t *st, uint32_t q, qs_prob_t *p)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!p) return QS_ERR_NULL;
    if (q >= st->n) return QS_ERR_QUBIT;
    rc = qs_weight(st, 1u << q, 1u << q, &p->num);
    if (!rc) rc = qs_weight(st, 0, 0, &p->den);
    return rc;
}

int qs_real_coords(uint32_t N, const qs_elem_t *x, int64_t r[4])
{
    if (!x || !r) return QS_ERR_NULL;
    if (!qs_valid_N(N)) return QS_ERR_ARG;
    uint32_t D = N / 2u;
    for (uint32_t j = D; j < QS_MAX_D; j++)
        if (x->c[j] != 0) return QS_ERR_NOT_REAL;
    if (x->c[D / 2u] != 0) return QS_ERR_NOT_REAL;
    for (uint32_t j = 1; j < D / 2u; j++) {
        int64_t s;
        if (qs_add64(x->c[j], x->c[D - j], &s) || s != 0) return QS_ERR_NOT_REAL;
    }
    r[0] = x->c[0];
    r[1] = x->c[1];
    r[2] = N == 16u ? x->c[2] : 0;
    r[3] = N == 16u ? x->c[3] : 0;
    return QS_OK;
}

int qs_real_sign(uint32_t N, const qs_elem_t *x, int *sign)
{
    int64_t r[4];
    if (!sign) return QS_ERR_NULL;
    int rc = qs_real_coords(N, x, r);
    if (rc) return rc;
    qs_w_t w[4];
    for (uint32_t i = 0; i < 4u; i++) qs_w_from_i64(&w[i], r[i]);
    return qs_w_sign_real(N, w, sign);
}

/* sign(num/den - p/q) = sign(num*q - den*p) for den > 0, q > 0. */
static int qs_cmp_core(uint32_t N, const qs_elem_t *num, const qs_elem_t *den, int64_t p, int64_t q,
                       int *cmp)
{
    int64_t rn[4], rd[4];
    int ds;
    int rc = qs_real_coords(N, num, rn);
    if (!rc) rc = qs_real_coords(N, den, rd);
    if (!rc) rc = qs_real_sign(N, den, &ds);
    if (rc) return rc;
    if (ds <= 0) return QS_ERR_ARG;
    qs_w_t wp, wq, w[4];
    qs_w_from_i64(&wp, p);
    qs_w_from_i64(&wq, q);
    for (uint32_t i = 0; i < 4u; i++) {
        qs_w_t a, b;
        qs_w_from_i64(&a, rn[i]);
        qs_w_from_i64(&b, rd[i]);
        rc = qs_w_mul(&a, &a, &wq);
        if (!rc) rc = qs_w_mul(&b, &b, &wp);
        if (!rc) rc = qs_w_sub(&w[i], &a, &b);
        if (rc) return rc;
    }
    return qs_w_sign_real(N, w, cmp);
}

int qs_prob_cmp(uint32_t N, const qs_prob_t *pr, int64_t p, int64_t q, int *cmp)
{
    if (!pr || !cmp) return QS_ERR_NULL;
    if (!qs_valid_N(N) || q <= 0) return QS_ERR_ARG;
    return qs_cmp_core(N, &pr->num, &pr->den, p, q, cmp);
}

/* ---------------------------------------------------------------- */
/* measurement and sampling                                         */
/* ---------------------------------------------------------------- */

int qs_measure(qs_state_t *st, uint32_t q, uint32_t outcome)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (q >= st->n) return QS_ERR_QUBIT;
    if (outcome > 1u) return QS_ERR_ARG;
    uint32_t dim = 1u << st->n, qb = 1u << q, want = outcome ? qb : 0u;
    int any = 0;
    for (uint32_t i = 0; i < dim; i++)
        if ((i & qb) == want && !qs_e_is_zero(&st->amp[i])) any = 1;
    if (!any) return QS_ERR_ZERO_PROB; /* state unchanged */
    for (uint32_t i = 0; i < dim; i++)
        if ((i & qb) != want) qs_e_zero(&st->amp[i]);
    st->collapsed++;
    return qs_reduce(st);
}

int qs_renormalise(qs_state_t *st)
{
    qs_elem_t nrm;
    uint32_t k;
    int rc = qs_norm_sq(st, &nrm, &k);
    if (rc) return rc;
    for (uint32_t j = 1; j < QS_MAX_D; j++)
        if (nrm.c[j] != 0) return QS_ERR_NOT_EXACT;
    int64_t v = nrm.c[0];
    if (v <= 0 || (v & (v - 1)) != 0) return QS_ERR_NOT_EXACT;
    uint32_t m = 0;
    while (v > 1) {
        v /= 2;
        m++;
    }
    /* amplitude elem/sqrt2^k divided by sqrt(2^m / 2^k) is elem/sqrt2^m */
    st->k = m;
    return qs_reduce(st);
}

int qs_sample(const qs_state_t *st, uint32_t u, uint32_t *idx)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!idx) return QS_ERR_NULL;
    qs_elem_t den, cum, a2;
    rc = qs_weight(st, 0, 0, &den);
    if (rc) return rc;
    qs_e_zero(&cum);
    uint32_t dim = 1u << st->n;
    for (uint32_t i = 0; i < dim; i++) {
        if (qs_e_is_zero(&st->amp[i])) continue;
        rc = qs_e_abs2(&a2, &st->amp[i], st->D);
        if (!rc) rc = qs_e_add_to(&cum, &a2, st->D);
        if (rc) return rc;
        int c;
        rc = qs_cmp_core(st->N, &cum, &den, (int64_t) u, (int64_t) 1 << 32, &c);
        if (rc) return rc;
        if (c > 0) {
            *idx = i;
            return QS_OK;
        }
    }
    return QS_ERR_ARG; /* unreachable for a nonzero state: cum reaches 1 > u/2^32 */
}

int qs_measure_sample(qs_state_t *st, uint32_t q, uint32_t u, uint32_t *bit)
{
    int rc = qs_check_state(st);
    if (rc) return rc;
    if (!bit) return QS_ERR_NULL;
    if (q >= st->n) return QS_ERR_QUBIT;
    qs_elem_t p0, den;
    rc = qs_weight(st, 1u << q, 0, &p0);
    if (!rc) rc = qs_weight(st, 0, 0, &den);
    if (rc) return rc;
    int c;
    rc = qs_cmp_core(st->N, &p0, &den, (int64_t) u, (int64_t) 1 << 32, &c);
    if (rc) return rc;
    uint32_t b = c > 0 ? 0u : 1u;
    rc = qs_measure(st, q, b);
    if (rc) return rc;
    *bit = b;
    return QS_OK;
}
