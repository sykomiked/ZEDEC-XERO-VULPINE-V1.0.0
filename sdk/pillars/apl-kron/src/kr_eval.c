/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_eval.c — the evaluator. Right operands before left ones, as in APL
 * and K. Every arithmetic step is a libzxv zxv_rat_* call (exact,
 * reduced, overflow refused); four-valued logic uses zxv_lp_*. A value
 * is never modified once made, so names share values by reference.
 * Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

typedef struct {
    const kr_program *p;
    kr_value **env;
    uint64_t work, limit;
    kr_diag *diag;
} E;

typedef struct {
    int64_t n, d; /* a rational; for truth, n is the ZXV_LP_* code */
    int kind;
} cell;

#define AT(nd) (nd)->line, (nd)->col, (nd)->offset

static int32_t tick(E *e, const kr_node *nd, uint64_t k) {
    if (e->work > e->limit || k > e->limit - e->work) {
        e->work = e->limit;
        return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the work limit (%llu operations) is reached",
                          (unsigned long long) e->limit);
    }
    e->work += k;
    return ZXV_OK;
}

static int32_t nomem(E *e, const kr_node *nd) { return kr_diag_at(e->diag, ZXV_E_NOMEM, AT(nd), "out of memory"); }

/* A new array, after the element limit and the work it costs. */
static int32_t newval(E *e, const kr_node *nd, int kind, uint32_t rank, uint64_t s0, uint64_t s1,
                      kr_value **out) {
    uint64_t n = rank == 0 ? 1u : rank == 1 ? s0 : s0 * s1;
    int32_t st;
    if (rank > 2) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "the result would have rank %u (KRON stops at 2)", (unsigned) rank);
    if (s0 > KR_MAX_ELEMENTS || s1 > KR_MAX_ELEMENTS || n > KR_MAX_ELEMENTS)
        return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the result would have more than %u elements",
                          (unsigned) KR_MAX_ELEMENTS);
    if ((st = tick(e, nd, n)) != ZXV_OK) return st;
    *out = kr_val_new(kind, rank, (int64_t) s0, (int64_t) s1);
    return *out ? ZXV_OK : nomem(e, nd);
}

static cell get(const kr_value *v, uint64_t i) {
    cell c;
    c.kind = v->kind;
    if (v->kind == KR_KIND_TRUTH) {
        c.n = v->t[i];
        c.d = 1;
    } else {
        c.n = v->num[i];
        c.d = v->den[i];
    }
    return c;
}

static void put(kr_value *v, uint64_t i, cell c) {
    if (v->kind == KR_KIND_TRUTH) v->t[i] = (int8_t) c.n;
    else {
        v->num[i] = c.n;
        v->den[i] = c.d;
    }
}

static const char *cell_str(cell c) {
    static const char *const tw[4] = {"neither", "true", "false", "both"};
    if (c.kind == KR_KIND_TRUTH) return (c.n >= 0 && c.n <= 3) ? tw[c.n] : "?";
    return kr_rat_str(c.n, c.d);
}

static int64_t floor_div(int64_t n, int64_t d) { /* d > 0 */
    int64_t q = n / d;
    if (n % d != 0 && n < 0) q--;
    return q;
}

static int64_t ceil_div(int64_t n, int64_t d) {
    int64_t q = n / d;
    if (n % d != 0 && n > 0) q++;
    return q;
}

static int32_t range(E *e, const kr_node *nd, const char *what, cell a, cell b, const char *op) {
    if (op)
        return kr_diag_at(e->diag, ZXV_E_RANGE, AT(nd),
                          "%s %s %s does not fit int64/int64 exactly (refused, never rounded)", cell_str(a), op,
                          cell_str(b));
    return kr_diag_at(e->diag, ZXV_E_RANGE, AT(nd), "%s of %s does not fit int64/int64 exactly (refused, never rounded)",
                      what, cell_str(a));
}

static int32_t zx(E *e, const kr_node *nd, int32_t st, cell a, cell b, const char *op) {
    if (st == ZXV_OK) return ZXV_OK;
    if (st == ZXV_E_RANGE) return range(e, nd, NULL, a, b, op);
    return kr_diag_at(e->diag, st, AT(nd), "%s %s %s: %s", cell_str(a), op, cell_str(b), zxv_status_name(st));
}

static int32_t domain2(E *e, const kr_node *nd, int fn, cell a, cell b) {
    return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s %s %s: %s", cell_str(a), kr_fn_word[fn], cell_str(b),
                      (a.kind == KR_KIND_TRUTH || b.kind == KR_KIND_TRUTH)
                          ? ((fn == F_AND || fn == F_OR) ? "and/or take truth values only"
                                                         : "truth is not a number (it is never collapsed to 0/1)")
                          : "and/or take truth values only");
}

static int32_t truthv(int32_t lp, cell *r) {
    r->kind = KR_KIND_TRUTH;
    r->n = lp;
    r->d = 1;
    return ZXV_OK;
}

/* One scalar dyadic step: r = a fn b. */
static int32_t f2(E *e, const kr_node *nd, int fn, cell a, cell b, cell *r) {
    int32_t st, c = 0;
    int num = a.kind == KR_KIND_NUM && b.kind == KR_KIND_NUM;
    int tru = a.kind == KR_KIND_TRUTH && b.kind == KR_KIND_TRUTH;
    if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
    r->kind = KR_KIND_NUM;
    r->d = 1;
    switch (fn) {
    case F_EQ:
    case F_NE: {
        int eq;
        if (!num && !tru) return domain2(e, nd, fn, a, b);
        eq = a.n == b.n && a.d == b.d;
        return truthv((eq == (fn == F_EQ)) ? ZXV_LP_TRUE : ZXV_LP_FALSE, r);
    }
    case F_AND:
    case F_OR: {
        int32_t o;
        if (!tru) return domain2(e, nd, fn, a, b);
        st = fn == F_AND ? zxv_lp_and((int32_t) a.n, (int32_t) b.n, &o) : zxv_lp_or((int32_t) a.n, (int32_t) b.n, &o);
        if (st != ZXV_OK) return kr_diag_at(e->diag, st, AT(nd), "four-valued %s refused", kr_fn_word[fn]);
        return truthv(o, r);
    }
    case F_MAX:
    case F_MIN:
        if (tru) { /* on truth: the four-valued or / and */
            int32_t o;
            st = fn == F_MIN ? zxv_lp_and((int32_t) a.n, (int32_t) b.n, &o) : zxv_lp_or((int32_t) a.n, (int32_t) b.n, &o);
            if (st != ZXV_OK) return kr_diag_at(e->diag, st, AT(nd), "four-valued %s refused", kr_fn_word[fn]);
            return truthv(o, r);
        }
        break;
    default: break;
    }
    if (!num)
        return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s %s %s: truth is not a number (it is never collapsed to 0/1)",
                          cell_str(a), kr_fn_word[fn], cell_str(b));
    switch (fn) {
    case F_ADD: return zx(e, nd, zxv_rat_add(a.n, a.d, b.n, b.d, &r->n, &r->d), a, b, "+");
    case F_SUB: return zx(e, nd, zxv_rat_sub(a.n, a.d, b.n, b.d, &r->n, &r->d), a, b, "-");
    case F_MUL: return zx(e, nd, zxv_rat_mul(a.n, a.d, b.n, b.d, &r->n, &r->d), a, b, "×");
    case F_DIV:
        if (b.n == 0)
            return kr_diag_at(e->diag, KR_E_DIVZERO, AT(nd), "%s ÷ 0: division by an exact zero", cell_str(a));
        return zx(e, nd, zxv_rat_div(a.n, a.d, b.n, b.d, &r->n, &r->d), a, b, "÷");
    case F_MAX:
    case F_MIN:
        zxv_rat_cmp(a.n, a.d, b.n, b.d, &c);
        *r = ((c >= 0) == (fn == F_MAX)) ? a : b;
        return ZXV_OK;
    case F_LT:
    case F_LE:
    case F_GE:
    case F_GT: {
        int t;
        zxv_rat_cmp(a.n, a.d, b.n, b.d, &c);
        t = fn == F_LT ? c < 0 : fn == F_LE ? c <= 0 : fn == F_GE ? c >= 0 : c > 0;
        return truthv(t ? ZXV_LP_TRUE : ZXV_LP_FALSE, r);
    }
    case F_RES: { /* APL a|b: b - a × floor(b ÷ a); 0|b is b */
        cell q, m;
        if (a.n == 0) {
            *r = b;
            return ZXV_OK;
        }
        if ((st = zx(e, nd, zxv_rat_div(b.n, b.d, a.n, a.d, &q.n, &q.d), b, a, "÷")) != ZXV_OK) return st;
        q.n = floor_div(q.n, q.d);
        q.d = 1;
        q.kind = KR_KIND_NUM;
        if ((st = zx(e, nd, zxv_rat_mul(a.n, a.d, q.n, 1, &m.n, &m.d), a, q, "×")) != ZXV_OK) return st;
        m.kind = KR_KIND_NUM;
        return zx(e, nd, zxv_rat_sub(b.n, b.d, m.n, m.d, &r->n, &r->d), b, m, "-");
    }
    case F_POW: { /* a * b, b an integer: square and multiply */
        cell base = a, acc;
        uint64_t k;
        if (b.d != 1)
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s * %s: a fractional power is not exact", cell_str(a),
                              cell_str(b));
        if (a.n == 0 && b.n < 0)
            return kr_diag_at(e->diag, KR_E_DIVZERO, AT(nd), "0 * %s: division by an exact zero", cell_str(b));
        acc.n = 1;
        acc.d = 1;
        acc.kind = KR_KIND_NUM;
        k = b.n < 0 ? (uint64_t) 0 - (uint64_t) b.n : (uint64_t) b.n;
        while (k) {
            if (k & 1u) {
                cell t = acc;
                if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
                st = zxv_rat_mul(t.n, t.d, base.n, base.d, &acc.n, &acc.d);
                if (st == ZXV_E_RANGE)
                    return kr_diag_at(e->diag, ZXV_E_RANGE, AT(nd),
                                      "%s * %s does not fit int64/int64 exactly (refused, never rounded)", cell_str(a),
                                      cell_str(b));
                if (st != ZXV_OK) return zx(e, nd, st, a, b, "*");
            }
            k >>= 1;
            if (k) {
                cell t = base;
                if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
                st = zxv_rat_mul(t.n, t.d, t.n, t.d, &base.n, &base.d);
                if (st == ZXV_E_RANGE)
                    return kr_diag_at(e->diag, ZXV_E_RANGE, AT(nd),
                                      "%s * %s does not fit int64/int64 exactly (refused, never rounded)", cell_str(a),
                                      cell_str(b));
                if (st != ZXV_OK) return zx(e, nd, st, a, b, "*");
            }
        }
        if (b.n < 0) {
            cell one = {1, 1, KR_KIND_NUM};
            return zx(e, nd, zxv_rat_div(1, 1, acc.n, acc.d, &r->n, &r->d), one, acc, "÷");
        }
        *r = acc;
        return ZXV_OK;
    }
    default: return kr_diag_at(e->diag, ZXV_E_INTERNAL, AT(nd), "unknown function %d", fn);
    }
}

/* One scalar monadic step. */
static int32_t f1(E *e, const kr_node *nd, int fn, cell a, cell *r) {
    int32_t st;
    if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
    if (fn == F_NOT) {
        int32_t o;
        if (a.kind != KR_KIND_TRUTH)
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "not %s: not takes truth values only", cell_str(a));
        if ((st = zxv_lp_not((int32_t) a.n, &o)) != ZXV_OK) return kr_diag_at(e->diag, st, AT(nd), "not refused");
        return truthv(o, r);
    }
    if (a.kind != KR_KIND_NUM)
        return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s %s: truth is not a number", kr_fn_word[fn], cell_str(a));
    r->kind = KR_KIND_NUM;
    r->d = 1;
    switch (fn) {
    case F_ID: *r = a; return ZXV_OK;
    case F_NEG: {
        cell z = {0, 1, KR_KIND_NUM};
        return zx(e, nd, zxv_rat_sub(0, 1, a.n, a.d, &r->n, &r->d), z, a, "-");
    }
    case F_SGN: r->n = a.n > 0 ? 1 : a.n < 0 ? -1 : 0; return ZXV_OK;
    case F_RECIP:
        if (a.n == 0) return kr_diag_at(e->diag, KR_E_DIVZERO, AT(nd), "÷ 0: division by an exact zero");
        {
            cell one = {1, 1, KR_KIND_NUM};
            return zx(e, nd, zxv_rat_div(1, 1, a.n, a.d, &r->n, &r->d), one, a, "÷");
        }
    case F_CEIL: r->n = ceil_div(a.n, a.d); return ZXV_OK;
    case F_FLOOR: r->n = floor_div(a.n, a.d); return ZXV_OK;
    case F_ABS:
        if (a.n >= 0) {
            *r = a;
            return ZXV_OK;
        }
        {
            cell z = {0, 1, KR_KIND_NUM};
            return zx(e, nd, zxv_rat_sub(0, 1, a.n, a.d, &r->n, &r->d), z, a, "-");
        }
    default: return kr_diag_at(e->diag, ZXV_E_INTERNAL, AT(nd), "unknown function %d", fn);
    }
}

static int kind_of_scalar2(int fn, int ka, int kb) {
    switch (fn) {
    case F_LT: case F_LE: case F_EQ: case F_GE: case F_GT: case F_NE: case F_AND: case F_OR:
        return KR_KIND_TRUTH;
    case F_MAX:
    case F_MIN: return (ka == KR_KIND_TRUTH && kb == KR_KIND_TRUTH) ? KR_KIND_TRUTH : KR_KIND_NUM;
    default: return KR_KIND_NUM;
    }
}

static int32_t eval(E *e, int32_t n, kr_value **out);

/* ===== Scalar functions over arrays ===== */

static int32_t scalar2(E *e, const kr_node *nd, int fn, const kr_value *A, const kr_value *B, kr_value **out) {
    const kr_value *S = A->rank >= B->rank ? A : B;
    kr_value *R;
    int32_t st;
    if (A->rank && B->rank) {
        if (A->rank != B->rank)
            return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "%s of a rank-%u and a rank-%u array", kr_fn_word[fn],
                              (unsigned) A->rank, (unsigned) B->rank);
        if (A->shape[0] != B->shape[0] || A->shape[1] != B->shape[1])
            return kr_diag_at(e->diag, KR_E_LENGTH, AT(nd), "%s of extents %lld %lld and %lld %lld", kr_fn_word[fn],
                              (long long) A->shape[0], (long long) A->shape[1], (long long) B->shape[0],
                              (long long) B->shape[1]);
    }
    if ((st = newval(e, nd, kind_of_scalar2(fn, A->kind, B->kind), S->rank, (uint64_t) S->shape[0],
                     (uint64_t) S->shape[1], &R)) != ZXV_OK)
        return st;
    for (uint64_t i = 0; i < R->n; i++) {
        cell r;
        if ((st = f2(e, nd, fn, get(A, A->rank ? i : 0), get(B, B->rank ? i : 0), &r)) != ZXV_OK) {
            kr_val_unref(R);
            return st;
        }
        if ((int) r.kind != (int) R->kind) { /* cannot happen: kinds are uniform */
            kr_val_unref(R);
            return kr_diag_at(e->diag, ZXV_E_INTERNAL, AT(nd), "mixed result kinds");
        }
        put(R, i, r);
    }
    *out = R;
    return ZXV_OK;
}

static int32_t scalar1(E *e, const kr_node *nd, int fn, const kr_value *A, kr_value **out) {
    kr_value *R;
    int32_t st;
    int kind = fn == F_NOT ? KR_KIND_TRUTH : KR_KIND_NUM;
    if ((st = newval(e, nd, kind, A->rank, (uint64_t) A->shape[0], (uint64_t) A->shape[1], &R)) != ZXV_OK) return st;
    for (uint64_t i = 0; i < A->n; i++) {
        cell r;
        if ((st = f1(e, nd, fn, get(A, i), &r)) != ZXV_OK) {
            kr_val_unref(R);
            return st;
        }
        put(R, i, r);
    }
    *out = R;
    return ZXV_OK;
}

/* ===== Reductions and scans ===== */

static int identity(int fn, cell *c) {
    c->d = 1;
    c->kind = KR_KIND_NUM;
    switch (fn) {
    case F_ADD: case F_SUB: case F_RES: c->n = 0; return 1;
    case F_MUL: case F_DIV: case F_POW: c->n = 1; return 1;
    case F_AND: c->kind = KR_KIND_TRUTH; c->n = ZXV_LP_TRUE; return 1;
    case F_OR: c->kind = KR_KIND_TRUTH; c->n = ZXV_LP_FALSE; return 1;
    default: return 0;
    }
}

/* Fold `len` cells xs[0], xs[step], ... : right fold (APL) or left fold (K). */
static int32_t fold(E *e, const kr_node *nd, int fn, int left, const kr_value *X, uint64_t base, uint64_t step,
                    uint64_t len, cell *out) {
    int32_t st;
    cell acc;
    if (len == 0) {
        if (!identity(fn, out))
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s over an empty axis has no exact identity",
                              kr_fn_word[fn]);
        return ZXV_OK;
    }
    if (left) {
        acc = get(X, base);
        for (uint64_t k = 1; k < len; k++) {
            cell r;
            if ((st = f2(e, nd, fn, acc, get(X, base + k * step), &r)) != ZXV_OK) return st;
            acc = r;
        }
    } else {
        acc = get(X, base + (len - 1u) * step);
        for (uint64_t k = len - 1u; k-- > 0;) {
            cell r;
            if ((st = f2(e, nd, fn, get(X, base + k * step), acc, &r)) != ZXV_OK) return st;
            acc = r;
        }
    }
    *out = acc;
    return ZXV_OK;
}

/* Lines along an axis: count, length, and where line i starts / its step. */
typedef struct {
    uint64_t lines, len, step, rows, cols;
    int last;
} axis_t;

static axis_t axis_of(const kr_value *X, int last) {
    axis_t a;
    memset(&a, 0, sizeof a);
    a.last = last || X->rank < 2;
    if (X->rank == 1) {
        a.lines = 1;
        a.len = (uint64_t) X->shape[0];
        a.step = 1;
    } else {
        a.rows = (uint64_t) X->shape[0];
        a.cols = (uint64_t) X->shape[1];
        a.lines = a.last ? a.rows : a.cols;
        a.len = a.last ? a.cols : a.rows;
        a.step = a.last ? 1u : a.cols;
    }
    return a;
}

static uint64_t line_base(const axis_t *a, uint64_t i) { return a->last ? i * a->len : i; }

static int32_t reduce(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    axis_t ax;
    kr_value *R = NULL;
    int32_t st;
    cell *res;
    int kind = -1;
    if (X->rank == 0) {
        *out = kr_val_ref((kr_value *) X);
        return ZXV_OK;
    }
    ax = axis_of(X, nd->variant == V_LAST);
    res = (cell *) malloc((ax.lines ? ax.lines : 1u) * sizeof *res);
    if (!res) return nomem(e, nd);
    for (uint64_t i = 0; i < ax.lines; i++) {
        if ((st = fold(e, nd, nd->fn, nd->variant == V_LEFT, X, line_base(&ax, i), ax.step, ax.len, &res[i])) != ZXV_OK)
            goto out;
        if (kind < 0) kind = res[i].kind;
        else if (kind != res[i].kind) {
            st = kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "the lines of the reduction give numbers and truth values");
            goto out;
        }
    }
    if (kind < 0) kind = X->kind;
    if ((st = newval(e, nd, kind, X->rank - 1u, ax.lines, 0, &R)) != ZXV_OK) goto out;
    for (uint64_t i = 0; i < ax.lines; i++) put(R, i, res[i]);
    *out = R;
out:
    free(res);
    return st;
}

static int32_t scan(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    axis_t ax;
    kr_value *R = NULL;
    int32_t st = ZXV_OK;
    cell *res;
    int kind = -1;
    int running = nd->variant == V_LEFT || F_IS_ASSOC(nd->fn);
    if (X->rank == 0) {
        *out = kr_val_ref((kr_value *) X);
        return ZXV_OK;
    }
    ax = axis_of(X, nd->variant == V_LAST);
    res = (cell *) malloc((X->n ? X->n : 1u) * sizeof *res);
    if (!res) return nomem(e, nd);
    for (uint64_t i = 0; i < ax.lines; i++) {
        uint64_t b = line_base(&ax, i);
        cell acc;
        for (uint64_t k = 0; k < ax.len; k++) {
            uint64_t at = b + k * ax.step;
            if (k == 0) acc = get(X, at);
            else if (running) {
                cell r;
                if ((st = f2(e, nd, nd->fn, acc, get(X, at), &r)) != ZXV_OK) goto out;
                acc = r;
            } else if ((st = fold(e, nd, nd->fn, 0, X, b, ax.step, k + 1u, &acc)) != ZXV_OK) {
                goto out;
            }
            if (kind < 0) kind = acc.kind;
            else if (kind != acc.kind) {
                st = kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "the scan gives numbers and truth values");
                goto out;
            }
            res[at] = acc;
        }
    }
    if (kind < 0) kind = X->kind;
    if ((st = newval(e, nd, kind, X->rank, (uint64_t) X->shape[0], (uint64_t) X->shape[1], &R)) != ZXV_OK) goto out;
    for (uint64_t i = 0; i < X->n; i++) put(R, i, res[i]);
    *out = R;
out:
    free(res);
    return st;
}

/* ===== Products ===== */

static int32_t outer(E *e, const kr_node *nd, const kr_value *A, const kr_value *B, kr_value **out) {
    uint32_t rank = (uint32_t) A->rank + B->rank;
    int64_t sh[4] = {0, 0, 0, 0};
    uint32_t k = 0;
    kr_value *R;
    int32_t st;
    if (rank > 2)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "an outer product of rank %u and rank %u is above rank 2",
                          (unsigned) A->rank, (unsigned) B->rank);
    for (uint32_t i = 0; i < A->rank; i++) sh[k++] = A->shape[i];
    for (uint32_t i = 0; i < B->rank; i++) sh[k++] = B->shape[i];
    if ((st = newval(e, nd, kind_of_scalar2(nd->fn, A->kind, B->kind), rank, rank ? (uint64_t) sh[0] : 0,
                     rank == 2 ? (uint64_t) sh[1] : 0, &R)) != ZXV_OK)
        return st;
    for (uint64_t i = 0; i < A->n; i++)
        for (uint64_t j = 0; j < B->n; j++) {
            cell r;
            if ((st = f2(e, nd, nd->fn, get(A, i), get(B, j), &r)) != ZXV_OK) {
                kr_val_unref(R);
                return st;
            }
            if ((int) r.kind != (int) R->kind) {
                kr_val_unref(R);
                return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "mixed result kinds");
            }
            put(R, i * B->n + j, r);
        }
    *out = R;
    return ZXV_OK;
}

/* Right fold of a cell array (inner products). */
static int32_t fold_cells(E *e, const kr_node *nd, int fn, const cell *xs, uint64_t len, cell *out) {
    int32_t st;
    cell acc;
    if (len == 0) {
        if (!identity(fn, out))
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s over an empty axis has no exact identity",
                              kr_fn_word[fn]);
        return ZXV_OK;
    }
    acc = xs[len - 1u];
    for (uint64_t k = len - 1u; k-- > 0;) {
        cell r;
        if ((st = f2(e, nd, fn, xs[k], acc, &r)) != ZXV_OK) return st;
        acc = r;
    }
    *out = acc;
    return ZXV_OK;
}

static int32_t inner(E *e, const kr_node *nd, const kr_value *A, const kr_value *B, kr_value **out) {
    uint64_t k, rows, cols;
    uint32_t rank;
    kr_value *R = NULL;
    int32_t st = ZXV_OK;
    int kind = -1;
    cell *res = NULL, *t = NULL;
    if (A->rank == 0 || B->rank == 0)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "an inner product needs vectors or matrices on both sides");
    k = (uint64_t) A->shape[A->rank - 1u];
    if (k != (uint64_t) B->shape[0])
        return kr_diag_at(e->diag, KR_E_LENGTH, AT(nd),
                          "inner product: the last axis of the left (%llu) is not the first axis of the right (%llu)",
                          (unsigned long long) k, (unsigned long long) B->shape[0]);
    rows = A->rank == 2 ? (uint64_t) A->shape[0] : 1u;
    cols = B->rank == 2 ? (uint64_t) B->shape[1] : 1u;
    rank = (uint32_t) (A->rank - 1u) + (uint32_t) (B->rank - 1u);
    if (rows * cols > KR_MAX_ELEMENTS)
        return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the inner product is above the element limit");
    res = (cell *) malloc((rows * cols != 0 ? rows * cols : 1u) * sizeof *res);
    t = (cell *) malloc((k ? k : 1u) * sizeof *t);
    if (!res || !t) {
        st = nomem(e, nd);
        goto out;
    }
    for (uint64_t i = 0; i < rows; i++)
        for (uint64_t j = 0; j < cols; j++) {
            cell *r = &res[i * cols + j];
            for (uint64_t m = 0; m < k; m++)
                if ((st = f2(e, nd, nd->fn2, get(A, i * k + m), get(B, m * cols + j), &t[m])) != ZXV_OK) goto out;
            if ((st = fold_cells(e, nd, nd->fn, t, k, r)) != ZXV_OK) goto out;
            if (kind < 0) kind = r->kind;
            else if (kind != r->kind) {
                st = kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "the inner product gives numbers and truth values");
                goto out;
            }
        }
    if (kind < 0) kind = KR_KIND_NUM;
    {
        uint64_t s0 = A->rank == 2 ? rows : cols, s1 = (A->rank == 2 && B->rank == 2) ? cols : 0;
        if ((st = newval(e, nd, kind, rank, s0, s1, &R)) != ZXV_OK) goto out;
    }
    for (uint64_t i = 0; i < rows * cols; i++) put(R, i, res[i]);
    *out = R;
out:
    free(t);
    free(res);
    return st;
}

/* ===== Structural functions ===== */

/* A whole number in [lo, hi] from a cell. */
static int32_t whole(E *e, const kr_node *nd, cell c, int64_t lo, int64_t hi, const char *what, int64_t *out) {
    if (c.kind != KR_KIND_NUM || c.d != 1)
        return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "%s must be a whole number, not %s", what, cell_str(c));
    if (c.n < lo || c.n > hi)
        return kr_diag_at(e->diag, c.n < lo && lo == 0 ? KR_E_DOMAIN : KR_E_LIMIT, AT(nd), "%s %s is outside %lld..%lld",
                          what, cell_str(c), (long long) lo, (long long) hi);
    *out = c.n;
    return ZXV_OK;
}

static int32_t copy_cells(E *e, const kr_node *nd, int kind, uint32_t rank, uint64_t s0, uint64_t s1,
                          const kr_value *X, const uint64_t *idx, kr_value **out) {
    kr_value *R;
    int32_t st = newval(e, nd, kind, rank, s0, s1, &R);
    if (st != ZXV_OK) return st;
    for (uint64_t i = 0; i < R->n; i++) put(R, i, get(X, idx[i]));
    *out = R;
    return ZXV_OK;
}

static int32_t iota(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    int64_t n;
    kr_value *R;
    int32_t st;
    if (X->rank != 0)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "iota takes a scalar (KRON has no odometer)");
    if ((st = whole(e, nd, get(X, 0), 0, KR_MAX_ELEMENTS, "the iota count", &n)) != ZXV_OK) return st;
    if ((st = newval(e, nd, KR_KIND_NUM, 1, (uint64_t) n, 0, &R)) != ZXV_OK) return st;
    for (int64_t i = 0; i < n; i++) R->num[i] = i;
    *out = R;
    return ZXV_OK;
}

static int32_t shape_of(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    kr_value *R;
    int32_t st = newval(e, nd, KR_KIND_NUM, 1, X->rank, 0, &R);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < X->rank; i++) R->num[i] = X->shape[i];
    *out = R;
    return ZXV_OK;
}

static int32_t tally(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    kr_value *R;
    int32_t st = newval(e, nd, KR_KIND_NUM, 0, 0, 0, &R);
    if (st != ZXV_OK) return st;
    R->num[0] = X->rank ? X->shape[0] : 1;
    *out = R;
    return ZXV_OK;
}

/* rev (last axis), revf (first axis), flip, ravel */
static int32_t restructure(E *e, const kr_node *nd, int fn, const kr_value *X, kr_value **out) {
    uint64_t *idx = (uint64_t *) malloc((X->n ? X->n : 1u) * sizeof *idx);
    uint64_t r = X->rank == 2 ? (uint64_t) X->shape[0] : 1u, c = X->rank == 2 ? (uint64_t) X->shape[1] : X->n;
    uint32_t rank = X->rank;
    uint64_t s0 = (uint64_t) X->shape[0], s1 = (uint64_t) X->shape[1];
    int32_t st;
    if (!idx) return nomem(e, nd);
    for (uint64_t i = 0; i < r; i++)
        for (uint64_t j = 0; j < c; j++) {
            uint64_t o = i * c + j;
            switch (fn) {
            case F_REV: idx[o] = i * c + (c - 1u - j); break;
            case F_REVF: idx[o] = X->rank == 2 ? (r - 1u - i) * c + j : (c - 1u - j); break;
            case F_FLIP: idx[o] = X->rank == 2 ? (o % r) * c + (o / r) : o; break;
            default: idx[o] = o; break;
            }
        }
    if (fn == F_FLIP && X->rank == 2) {
        s0 = (uint64_t) X->shape[1];
        s1 = (uint64_t) X->shape[0];
    }
    if (fn == F_RAVEL) {
        rank = 1;
        s0 = X->n;
        s1 = 0;
    }
    st = copy_cells(e, nd, X->kind, rank, s0, s1, X, idx, out);
    free(idx);
    return st;
}

static int32_t cmp_cells(E *e, const kr_node *nd, const kr_value *X, uint64_t a, uint64_t b, uint64_t w, int *out) {
    int32_t st;
    for (uint64_t k = 0; k < w; k++) {
        int32_t c = 0;
        cell x = get(X, a * w + k), y = get(X, b * w + k);
        if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
        zxv_rat_cmp(x.n, x.d, y.n, y.d, &c);
        if (c) {
            *out = c;
            return ZXV_OK;
        }
    }
    *out = 0;
    return ZXV_OK;
}

/* Stable grade (merge sort) of the major cells. */
static int32_t grade(E *e, const kr_node *nd, int down, const kr_value *X, uint64_t **out, uint64_t *out_n) {
    uint64_t n, w, *a, *t;
    int32_t st = ZXV_OK;
    if (X->rank == 0) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "grade and sort need a vector or a matrix");
    if (X->kind != KR_KIND_NUM)
        return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "truth values have no order: grade needs numbers");
    n = (uint64_t) X->shape[0];
    w = X->rank == 2 ? (uint64_t) X->shape[1] : 1u;
    a = (uint64_t *) malloc((n ? n : 1u) * sizeof *a);
    t = (uint64_t *) malloc((n ? n : 1u) * sizeof *t);
    if (!a || !t) {
        free(a);
        free(t);
        return nomem(e, nd);
    }
    for (uint64_t i = 0; i < n; i++) a[i] = i;
    for (uint64_t width = 1; width < n && st == ZXV_OK; width *= 2u) {
        for (uint64_t lo = 0; lo < n && st == ZXV_OK; lo += 2u * width) {
            uint64_t mid = lo + width < n ? lo + width : n, hi = lo + 2u * width < n ? lo + 2u * width : n;
            uint64_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                int c = 0;
                if ((st = cmp_cells(e, nd, X, a[j], a[i], w, &c)) != ZXV_OK) break;
                /* take from the right half only when strictly before: stable */
                if (down ? c > 0 : c < 0) t[k++] = a[j++];
                else t[k++] = a[i++];
            }
            while (i < mid) t[k++] = a[i++];
            while (j < hi) t[k++] = a[j++];
            if (st == ZXV_OK) memcpy(a + lo, t + lo, (hi - lo) * sizeof *a);
        }
    }
    free(t);
    if (st != ZXV_OK) {
        free(a);
        return st;
    }
    *out = a;
    *out_n = n;
    return ZXV_OK;
}

static int32_t grade_val(E *e, const kr_node *nd, int down, const kr_value *X, kr_value **out) {
    uint64_t *g, n;
    kr_value *R;
    int32_t st = grade(e, nd, down, X, &g, &n);
    if (st != ZXV_OK) return st;
    if ((st = newval(e, nd, KR_KIND_NUM, 1, n, 0, &R)) == ZXV_OK) {
        for (uint64_t i = 0; i < n; i++) R->num[i] = (int64_t) g[i];
        *out = R;
    }
    free(g);
    return st;
}

static int32_t sort_val(E *e, const kr_node *nd, const kr_value *X, kr_value **out) {
    uint64_t *g, n, w, *idx;
    int32_t st = grade(e, nd, 0, X, &g, &n);
    if (st != ZXV_OK) return st;
    w = X->rank == 2 ? (uint64_t) X->shape[1] : 1u;
    idx = (uint64_t *) malloc((X->n ? X->n : 1u) * sizeof *idx);
    if (!idx) {
        free(g);
        return nomem(e, nd);
    }
    for (uint64_t i = 0; i < n; i++)
        for (uint64_t k = 0; k < w; k++) idx[i * w + k] = g[i] * w + k;
    st = copy_cells(e, nd, X->kind, X->rank, (uint64_t) X->shape[0], (uint64_t) X->shape[1], X, idx, out);
    free(idx);
    free(g);
    return st;
}

static int32_t match(E *e, const kr_node *nd, const kr_value *A, const kr_value *B, kr_value **out) {
    int same = A->kind == B->kind && A->rank == B->rank && A->shape[0] == B->shape[0] && A->shape[1] == B->shape[1];
    kr_value *R;
    int32_t st;
    for (uint64_t i = 0; same && i < A->n; i++) {
        cell x = get(A, i), y = get(B, i);
        if ((st = tick(e, nd, 1)) != ZXV_OK) return st;
        same = x.n == y.n && x.d == y.d;
    }
    if ((st = newval(e, nd, KR_KIND_TRUTH, 0, 0, 0, &R)) != ZXV_OK) return st;
    R->t[0] = (int8_t) (same ? ZXV_LP_TRUE : ZXV_LP_FALSE);
    *out = R;
    return ZXV_OK;
}

/* The integer list of a left argument: a scalar or a vector of at most
 * `maxlen` whole numbers. */
static int32_t int_list(E *e, const kr_node *nd, const kr_value *L, uint32_t maxlen, const char *what, int64_t lo,
                        int64_t *v, uint32_t *n) {
    int32_t st;
    if (L->rank > 1 || L->n > maxlen)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "%s must be a scalar or a vector of at most %u whole numbers",
                          what, (unsigned) maxlen);
    for (uint64_t i = 0; i < L->n; i++)
        if ((st = whole(e, nd, get(L, i), lo, KR_MAX_ELEMENTS, what, &v[i])) != ZXV_OK) return st;
    *n = (uint32_t) L->n;
    return ZXV_OK;
}

static int32_t reshape(E *e, const kr_node *nd, const kr_value *L, const kr_value *X, kr_value **out) {
    int64_t sh[2] = {0, 0};
    uint32_t r;
    kr_value *R;
    int32_t st;
    if (L->rank > 1 || L->n > 2)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "a reshape to rank %llu: KRON arrays have rank <= 2",
                          (unsigned long long) L->n);
    if ((st = int_list(e, nd, L, 2, "a reshape extent", 0, sh, &r)) != ZXV_OK) return st;
    if (L->rank == 0) r = 1;
    if ((uint64_t) sh[0] * (uint64_t) (r == 2 ? sh[1] : 1) > KR_MAX_ELEMENTS)
        return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the reshape is above the element limit");
    if ((st = newval(e, nd, X->kind, r, (uint64_t) sh[0], (uint64_t) sh[1], &R)) != ZXV_OK) return st;
    if (R->n && X->n == 0) {
        if (X->kind == KR_KIND_TRUTH) {
            kr_val_unref(R);
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "reshaping an empty truth array has no fill");
        }
        /* numbers fill with 0, as in APL: kr_val_new made 0/1 already */
    } else {
        for (uint64_t i = 0; i < R->n; i++) put(R, i, get(X, i % X->n));
    }
    *out = R;
    return ZXV_OK;
}

/* A view of X as rows x cols for catenation along `last` or first axis. */
static int32_t cat(E *e, const kr_node *nd, int last, const kr_value *A, const kr_value *B, kr_value **out) {
    int kind;
    kr_value *R;
    int32_t st;
    if (A->kind != B->kind && A->n && B->n)
        return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "joining numbers and truth values");
    kind = A->n ? A->kind : B->kind;
    if (A->rank <= 1 && B->rank <= 1) {
        if ((st = newval(e, nd, kind, 1, A->n + B->n, 0, &R)) != ZXV_OK) return st;
        for (uint64_t i = 0; i < A->n; i++) put(R, i, get(A, i));
        for (uint64_t i = 0; i < B->n; i++) put(R, A->n + i, get(B, i));
        *out = R;
        return ZXV_OK;
    }
    {
        /* along the last axis: rows agree; along the first: cols agree */
        const kr_value *M = A->rank == 2 ? A : B;
        uint64_t across = last ? (uint64_t) M->shape[0] : (uint64_t) M->shape[1];
        uint64_t wa, wb, rows, cols;
        const kr_value *X[2];
        uint64_t w[2];
        X[0] = A;
        X[1] = B;
        for (int s = 0; s < 2; s++) {
            const kr_value *V = X[s];
            if (V->rank == 2) {
                uint64_t a2 = last ? (uint64_t) V->shape[0] : (uint64_t) V->shape[1];
                if (a2 != across)
                    return kr_diag_at(e->diag, KR_E_LENGTH, AT(nd), "joining matrices of %llu and %llu %s",
                                      (unsigned long long) across, (unsigned long long) a2, last ? "rows" : "columns");
                w[s] = last ? (uint64_t) V->shape[1] : (uint64_t) V->shape[0];
            } else if (V->rank == 1) {
                if ((uint64_t) V->shape[0] != across)
                    return kr_diag_at(e->diag, KR_E_LENGTH, AT(nd), "joining a vector of %llu to a matrix of %llu %s",
                                      (unsigned long long) V->shape[0], (unsigned long long) across,
                                      last ? "rows" : "columns");
                w[s] = 1;
            } else {
                w[s] = 1;
            }
        }
        wa = w[0];
        wb = w[1];
        rows = last ? across : wa + wb;
        cols = last ? wa + wb : across;
        if ((st = newval(e, nd, kind, 2, rows, cols, &R)) != ZXV_OK) return st;
        for (uint64_t i = 0; i < rows; i++)
            for (uint64_t j = 0; j < cols; j++) {
                /* (p, q) inside piece s */
                uint64_t along = last ? j : i, cross = last ? i : j;
                int s = along < wa ? 0 : 1;
                uint64_t p = along - (s ? wa : 0);
                const kr_value *V = X[s];
                uint64_t src;
                if (V->rank == 0) src = 0;
                else if (V->rank == 1) src = cross;
                else src = last ? cross * w[s] + p : p * across + cross;
                put(R, i * cols + j, get(V, src));
            }
        *out = R;
        return ZXV_OK;
    }
}

/* take (drop = 0) or drop (drop = 1): APL ↑ ↓. */
static int32_t takedrop(E *e, const kr_node *nd, int drop, const kr_value *L, const kr_value *X, kr_value **out) {
    int64_t cnt[2] = {0, 0};
    uint32_t nl, rank = X->rank ? X->rank : 1u;
    int64_t xs[2], rs[2], off[2];
    kr_value *R;
    int32_t st;
    if (L->rank > 1 || L->n > rank || L->n == 0)
        return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "%s needs one count per axis (at most %u)", drop ? "drop" : "take",
                          (unsigned) rank);
    for (uint64_t i = 0; i < L->n; i++)
        if ((st = whole(e, nd, get(L, i), -(int64_t) KR_MAX_ELEMENTS, KR_MAX_ELEMENTS, "a count", &cnt[i])) != ZXV_OK)
            return st;
    nl = (uint32_t) L->n;
    xs[0] = X->rank ? X->shape[0] : 1;
    xs[1] = X->rank == 2 ? X->shape[1] : 1;
    for (uint32_t a = 0; a < 2; a++) {
        int64_t c = a < nl ? cnt[a] : (drop ? 0 : xs[a]);
        int64_t m = c < 0 ? -c : c;
        if (a >= rank) {
            rs[a] = 1;
            off[a] = 0;
            continue;
        }
        if (drop) {
            rs[a] = m >= xs[a] ? 0 : xs[a] - m;
            off[a] = c >= 0 ? m : 0;
        } else {
            rs[a] = m;
            off[a] = c >= 0 ? 0 : xs[a] - m; /* may be negative: pad in front */
        }
    }
    if ((st = newval(e, nd, X->kind, rank, (uint64_t) rs[0], rank == 2 ? (uint64_t) rs[1] : 0, &R)) != ZXV_OK) return st;
    for (int64_t i = 0; i < rs[0]; i++)
        for (int64_t j = 0; j < rs[1]; j++) {
            int64_t si = i + off[0], sj = j + off[1];
            uint64_t o = (uint64_t) (i * rs[1] + j);
            if (si < 0 || si >= xs[0] || sj < 0 || sj >= xs[1]) {
                if (X->kind == KR_KIND_TRUTH) {
                    kr_val_unref(R);
                    return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "an overtake of truth values has no fill");
                }
                continue; /* 0 */
            }
            put(R, o, get(X, (uint64_t) (si * xs[1] + sj)));
        }
    *out = R;
    return ZXV_OK;
}

static int32_t rotate(E *e, const kr_node *nd, const kr_value *L, const kr_value *X, kr_value **out) {
    int64_t k;
    uint64_t r, c, *idx;
    int32_t st;
    if (L->rank != 0) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "rotate takes a scalar count");
    if ((st = whole(e, nd, get(L, 0), INT64_MIN + 1, INT64_MAX, "a rotation", &k)) != ZXV_OK) return st;
    if (X->rank == 0) {
        *out = kr_val_ref((kr_value *) X);
        return ZXV_OK;
    }
    r = X->rank == 2 ? (uint64_t) X->shape[0] : 1u;
    c = X->rank == 2 ? (uint64_t) X->shape[1] : X->n;
    idx = (uint64_t *) malloc((X->n ? X->n : 1u) * sizeof *idx);
    if (!idx) return nomem(e, nd);
    for (uint64_t i = 0; i < r; i++)
        for (uint64_t j = 0; j < c; j++) {
            int64_t m = (int64_t) c, s = (k % m + m) % m;
            idx[i * c + j] = i * c + (j + (uint64_t) s) % c;
        }
    st = copy_cells(e, nd, X->kind, X->rank, (uint64_t) X->shape[0], (uint64_t) X->shape[1], X, idx, out);
    free(idx);
    return st;
}

static int32_t find(E *e, const kr_node *nd, const kr_value *A, const kr_value *B, kr_value **out) {
    kr_value *R;
    int32_t st;
    if (A->rank != 1) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "find (⍳ ?) searches a vector");
    if ((st = newval(e, nd, KR_KIND_NUM, B->rank, (uint64_t) B->shape[0], (uint64_t) B->shape[1], &R)) != ZXV_OK) return st;
    for (uint64_t j = 0; j < B->n; j++) {
        cell y = get(B, j);
        uint64_t i;
        for (i = 0; i < A->n; i++) {
            cell x = get(A, i);
            if ((st = tick(e, nd, 1)) != ZXV_OK) {
                kr_val_unref(R);
                return st;
            }
            if (x.kind == y.kind && x.n == y.n && x.d == y.d) break;
        }
        R->num[j] = (int64_t) i;
    }
    *out = R;
    return ZXV_OK;
}

/* An index vector (or scalar) of whole numbers in 0 .. n-1. */
static int32_t index_list(E *e, const kr_node *nd, const kr_value *I, uint64_t n, uint64_t *v) {
    for (uint64_t i = 0; i < I->n; i++) {
        cell c = get(I, i);
        if (c.kind != KR_KIND_NUM || c.d != 1)
            return kr_diag_at(e->diag, KR_E_DOMAIN, AT(nd), "an index must be a whole number, not %s", cell_str(c));
        if (c.n < 0 || (uint64_t) c.n >= n)
            return kr_diag_at(e->diag, KR_E_INDEX, AT(nd), "index %s is outside 0..%lld", cell_str(c), (long long) n - 1);
        v[i] = (uint64_t) c.n;
    }
    return ZXV_OK;
}

static int32_t index1(E *e, const kr_node *nd, const kr_value *X, const kr_value *I, kr_value **out) {
    uint64_t *iv, w, *idx;
    uint32_t rank;
    int32_t st;
    if (X->rank == 0) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "indexing a scalar");
    rank = (uint32_t) I->rank + X->rank - 1u;
    if (rank > 2) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "the indexed result would have rank %u", (unsigned) rank);
    w = X->rank == 2 ? (uint64_t) X->shape[1] : 1u;
    if (I->n * w > KR_MAX_ELEMENTS)
        return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the indexed result is above the element limit");
    iv = (uint64_t *) malloc((I->n ? I->n : 1u) * sizeof *iv);
    idx = (uint64_t *) malloc((I->n * w != 0 ? I->n * w : 1u) * sizeof *idx);
    if (!iv || !idx) {
        free(iv);
        free(idx);
        return nomem(e, nd);
    }
    if ((st = index_list(e, nd, I, (uint64_t) X->shape[0], iv)) == ZXV_OK) {
        uint64_t s0, s1;
        for (uint64_t i = 0; i < I->n; i++)
            for (uint64_t k = 0; k < w; k++) idx[i * w + k] = iv[i] * w + k;
        if (I->rank == 0) {
            s0 = w;
            s1 = 0;
        } else if (I->rank == 1) {
            s0 = (uint64_t) I->shape[0];
            s1 = w;
        } else {
            s0 = (uint64_t) I->shape[0];
            s1 = (uint64_t) I->shape[1];
        }
        st = copy_cells(e, nd, X->kind, rank, s0, s1, X, idx, out);
    }
    free(iv);
    free(idx);
    return st;
}

static int32_t index2(E *e, const kr_node *nd, const kr_value *X, const kr_value *I, const kr_value *J, kr_value **out) {
    uint64_t ni, nj, *iv = NULL, *jv = NULL, *idx = NULL;
    uint32_t ri, rj;
    int32_t st;
    uint64_t sh[2] = {0, 0};
    uint32_t k = 0;
    if (X->rank != 2) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "x[i;j] needs a matrix");
    ri = I ? I->rank : 1u;
    rj = J ? J->rank : 1u;
    if (ri + rj > 2) return kr_diag_at(e->diag, KR_E_RANK, AT(nd), "the indexed result would have rank %u", (unsigned) (ri + rj));
    ni = I ? I->n : (uint64_t) X->shape[0];
    nj = J ? J->n : (uint64_t) X->shape[1];
    if (ni * nj > KR_MAX_ELEMENTS) return kr_diag_at(e->diag, KR_E_LIMIT, AT(nd), "the indexed result is above the element limit");
    iv = (uint64_t *) malloc((ni ? ni : 1u) * sizeof *iv);
    jv = (uint64_t *) malloc((nj ? nj : 1u) * sizeof *jv);
    idx = (uint64_t *) malloc((ni * nj != 0 ? ni * nj : 1u) * sizeof *idx);
    if (!iv || !jv || !idx) {
        st = nomem(e, nd);
        goto out;
    }
    if (I) {
        if ((st = index_list(e, nd, I, (uint64_t) X->shape[0], iv)) != ZXV_OK) goto out;
    } else {
        for (uint64_t i = 0; i < ni; i++) iv[i] = i;
    }
    if (J) {
        if ((st = index_list(e, nd, J, (uint64_t) X->shape[1], jv)) != ZXV_OK) goto out;
    } else {
        for (uint64_t j = 0; j < nj; j++) jv[j] = j;
    }
    for (uint64_t i = 0; i < ni; i++)
        for (uint64_t j = 0; j < nj; j++) idx[i * nj + j] = iv[i] * (uint64_t) X->shape[1] + jv[j];
    if (!I) sh[k++] = ni;
    else for (uint32_t q = 0; q < I->rank; q++) sh[k++] = (uint64_t) I->shape[q];
    if (!J) sh[k++] = nj;
    else for (uint32_t q = 0; q < J->rank; q++) sh[k++] = (uint64_t) J->shape[q];
    st = copy_cells(e, nd, X->kind, ri + rj, sh[0], sh[1], X, idx, out);
out:
    free(iv);
    free(jv);
    free(idx);
    return st;
}

/* ===== The tree walk ===== */

static int32_t literal(E *e, const kr_node *nd, kr_value **out) {
    const kr_program *p = e->p;
    int truth = nd->kind == N_TRUTH || nd->kind == N_TVEC || nd->kind == N_TMAT;
    uint32_t rank = (nd->kind == N_NUM || nd->kind == N_TRUTH) ? 0u : (nd->kind == N_VEC || nd->kind == N_TVEC) ? 1u : 2u;
    uint64_t s0 = rank == 1 ? nd->cnt : nd->rows, s1 = nd->cols;
    kr_value *R;
    int32_t st = newval(e, nd, truth ? KR_KIND_TRUTH : KR_KIND_NUM, rank, s0, s1, &R);
    if (st != ZXV_OK) return st;
    for (uint64_t i = 0; i < R->n; i++) {
        cell c;
        c.kind = R->kind;
        c.n = p->lnum[nd->lit + i];
        c.d = p->lden[nd->lit + i];
        put(R, i, c);
    }
    *out = R;
    return ZXV_OK;
}

static int32_t mon(E *e, const kr_node *nd, const kr_value *A, kr_value **out) {
    switch (nd->fn) {
    case F_IOTA: return iota(e, nd, A, out);
    case F_SHAPE: return shape_of(e, nd, A, out);
    case F_TALLY: return tally(e, nd, A, out);
    case F_REV:
    case F_REVF:
    case F_FLIP:
    case F_RAVEL: return restructure(e, nd, nd->fn, A, out);
    case F_GUP:
    case F_GDOWN: return grade_val(e, nd, nd->fn == F_GDOWN, A, out);
    case F_SORT: return sort_val(e, nd, A, out);
    default: return scalar1(e, nd, nd->fn, A, out);
    }
}

static int32_t dya(E *e, const kr_node *nd, const kr_value *A, const kr_value *B, kr_value **out) {
    if (F_IS_SCALAR2(nd->fn)) return scalar2(e, nd, nd->fn, A, B, out);
    switch (nd->fn) {
    case F_MATCH: return match(e, nd, A, B, out);
    case F_RESHAPE: return reshape(e, nd, A, B, out);
    case F_CAT: return cat(e, nd, 1, A, B, out);
    case F_CATF: return cat(e, nd, 0, A, B, out);
    case F_TAKE: return takedrop(e, nd, 0, A, B, out);
    case F_DROP: return takedrop(e, nd, 1, A, B, out);
    case F_ROTATE: return rotate(e, nd, A, B, out);
    case F_FIND: return find(e, nd, A, B, out);
    case F_INDEX: return index1(e, nd, A, B, out);
    default: return kr_diag_at(e->diag, ZXV_E_INTERNAL, AT(nd), "unknown function %d", nd->fn);
    }
}

static int32_t eval(E *e, int32_t n, kr_value **out) {
    const kr_node *nd = &e->p->nodes[n];
    kr_value *A = NULL, *B = NULL, *C = NULL;
    int32_t st = ZXV_OK;
    *out = NULL;
    switch (nd->kind) {
    case N_NUM:
    case N_VEC:
    case N_MAT:
    case N_TRUTH:
    case N_TVEC:
    case N_TMAT: return literal(e, nd, out);
    case N_NAME:
        if (!e->env[nd->sym])
            return kr_diag_at(e->diag, KR_E_NAME, AT(nd), "'%s' has no value", e->p->syms[nd->sym].name);
        *out = kr_val_ref(e->env[nd->sym]);
        return ZXV_OK;
    default: break;
    }
    /* right before left: b, c, then a */
    if (nd->b >= 0 && (st = eval(e, nd->b, &B)) != ZXV_OK) goto done;
    if (nd->c >= 0 && (st = eval(e, nd->c, &C)) != ZXV_OK) goto done;
    if (nd->a >= 0 && (st = eval(e, nd->a, &A)) != ZXV_OK) goto done;
    switch (nd->kind) {
    case N_LET:
        kr_val_unref(e->env[nd->sym]);
        e->env[nd->sym] = kr_val_ref(A);
        *out = kr_val_ref(A);
        break;
    case N_MON: st = mon(e, nd, A, out); break;
    case N_DYA: st = dya(e, nd, A, B, out); break;
    case N_RED: st = reduce(e, nd, A, out); break;
    case N_SCAN: st = scan(e, nd, A, out); break;
    case N_OUTER: st = outer(e, nd, A, B, out); break;
    case N_INNER: st = inner(e, nd, A, B, out); break;
    case N_INDEX2: st = index2(e, nd, A, B, C, out); break;
    default: st = kr_diag_at(e->diag, ZXV_E_INTERNAL, AT(nd), "unknown node %d", nd->kind); break;
    }
done:
    kr_val_unref(A);
    kr_val_unref(B);
    kr_val_unref(C);
    return st;
}

int32_t kr_eval(const kr_program *p, const kr_binding *bindings, uint32_t count, uint64_t work_limit,
                kr_value **out_value, kr_diag *diag) {
    E e;
    int32_t st = ZXV_OK;
    kr_value *last = NULL;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_value) return ZXV_E_ARG;
    *out_value = NULL;
    if (!p || (count && !bindings)) return ZXV_E_ARG;
    memset(&e, 0, sizeof e);
    e.p = p;
    e.limit = work_limit ? work_limit : KR_DEFAULT_WORK;
    e.diag = diag;
    e.env = (kr_value **) calloc(p->nsyms + 1u, sizeof *e.env);
    if (!e.env) return ZXV_E_NOMEM;
    for (uint32_t i = 0; i < count && st == ZXV_OK; i++) {
        const kr_binding *b = &bindings[i];
        uint32_t f;
        if (!b->name || !b->value) {
            st = kr_diag_at(diag, ZXV_E_ARG, 0, 0, 0, "binding %u has no name or value", (unsigned) i);
            break;
        }
        for (f = 0; f < p->nfree; f++)
            if (strcmp(p->syms[p->free_syms[f]].name, b->name) == 0) break;
        if (f == p->nfree) {
            st = kr_diag_at(diag, KR_E_NAME, 0, 0, 0, "binding '%s' names no free name of the program", b->name);
            break;
        }
        if (e.env[p->free_syms[f]]) {
            st = kr_diag_at(diag, KR_E_NAME, 0, 0, 0, "'%s' is bound twice", b->name);
            break;
        }
        e.env[p->free_syms[f]] = kr_val_ref((kr_value *) b->value);
    }
    for (uint32_t s = 0; st == ZXV_OK && s < p->nstmts; s++) {
        kr_value *v = NULL;
        st = eval(&e, p->stmts[s], &v);
        kr_val_unref(last);
        last = v;
    }
    for (uint32_t i = 0; i <= p->nsyms; i++) kr_val_unref(e.env[i]);
    free(e.env);
    kr_set_work(e.work);
    if (st != ZXV_OK) {
        kr_val_unref(last);
        return st;
    }
    *out_value = last;
    return ZXV_OK;
}
