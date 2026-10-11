/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_exec.c — running a Trebago program on caller arrays, exactly.
 *
 * Every value is an int64 numerator over an int64 denominator, reduced,
 * denominator > 0. Every rational operation is a libzxv call (zxv_rat_add,
 * _sub, _mul, _div, _cmp, _normalize), so an exact result that does not
 * fit is ZXV_E_RANGE: refused, never rounded. Index arithmetic is checked
 * int64 arithmetic.
 *
 * A vector or matrix statement has Fortran's array-assignment meaning:
 * the right-hand side is evaluated for every element first, then stored,
 * so y(i) = y(n - i + 1) reverses y. Elements are visited in column-major
 * order (the first index fastest); scan(...) accumulates along that
 * order. The caller's arrays are written only after every statement
 * succeeded. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tb_internal.h"

typedef struct {
    const tb_program *p;
    int64_t **num, **den;   /* per symbol storage (NULL for constants) */
    int64_t (*ext)[TB_MAX_RANK];
    int64_t *dimval;
    int64_t *slot_val;
    int32_t active[64];
    uint32_t nactive;
    int64_t *acc_num, *acc_den; /* per node: scan accumulators */
    uint64_t work, limit;
    tb_diag *diag;
} X;

static int32_t eval(X *x, int32_t id, int64_t *on, int64_t *od);

static int32_t xfail(X *x, int32_t status, const tb_node *n, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static int32_t xfail(X *x, int32_t status, const tb_node *n, const char *fmt, ...) {
    char body[400];
    size_t k;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    /* the loop indices in effect, each formatted on its own and copied in
     * only when it fits whole */
    k = strlen(body);
    for (uint32_t i = 0; i < x->nactive; i++) {
        char piece[64];
        int r = snprintf(piece, sizeof piece, "%s%s=%lld", i ? ", " : " at ",
                         x->p->slots[x->active[i]].name, (long long) x->slot_val[x->active[i]]);
        if (r < 0 || (size_t) r >= sizeof piece || (size_t) r >= sizeof body - k) break;
        memcpy(body + k, piece, (size_t) r + 1u);
        k += (size_t) r;
    }
    return tb_diag_at(x->diag, status, n ? n->line : 0, n ? n->col : 0, 0, "%s", body);
}

static int32_t tick(X *x, const tb_node *n) {
    if (++x->work > x->limit)
        return xfail(x, TB_E_LIMIT, n, "the work limit (%llu rational operations) is used up",
                     (unsigned long long) x->limit);
    return ZXV_OK;
}

static const char *fmt_rat(char *s, size_t cap, int64_t n, int64_t d) {
    size_t len = 0;
    if (zxv_rat_format(n, d, s, cap, &len) != ZXV_OK) snprintf(s, cap, "?");
    return s;
}

/* Checked int64 arithmetic for index expressions. */
static int32_t evali(X *x, int32_t id, int64_t *out) {
    const tb_node *n = &x->p->nodes[id];
    int64_t a, b;
    int32_t st;
    switch (n->kind) {
    case N_NUM: *out = n->num; return ZXV_OK;
    case N_IDX: *out = x->slot_val[n->a]; return ZXV_OK;
    case N_DIM: *out = x->dimval[n->a]; return ZXV_OK;
    case N_NEG:
        if ((st = evali(x, n->a, &a)) != ZXV_OK) return st;
        if (a == INT64_MIN) return xfail(x, ZXV_E_RANGE, n, "an index expression overflows int64");
        *out = -a;
        return ZXV_OK;
    case N_ADD:
    case N_SUB:
    case N_MUL:
        if ((st = evali(x, n->a, &a)) != ZXV_OK) return st;
        if ((st = evali(x, n->b, &b)) != ZXV_OK) return st;
        if ((n->kind == N_ADD && __builtin_add_overflow(a, b, out)) ||
            (n->kind == N_SUB && __builtin_sub_overflow(a, b, out)) ||
            (n->kind == N_MUL && __builtin_mul_overflow(a, b, out)))
            return xfail(x, ZXV_E_RANGE, n, "an index expression overflows int64");
        return ZXV_OK;
    default: return xfail(x, ZXV_E_INTERNAL, n, "not an integer expression");
    }
}

static int32_t rat_op(X *x, const tb_node *n, int kind, int64_t an, int64_t ad, int64_t bn,
                      int64_t bd, int64_t *on, int64_t *od) {
    int32_t st;
    char s1[96], s2[96];
    static const char *const sym[] = {"+", "-", "*", "/"};
    if ((st = tick(x, n)) != ZXV_OK) return st;
    switch (kind) {
    case N_ADD: st = zxv_rat_add(an, ad, bn, bd, on, od); break;
    case N_SUB: st = zxv_rat_sub(an, ad, bn, bd, on, od); break;
    case N_MUL: st = zxv_rat_mul(an, ad, bn, bd, on, od); break;
    default:
        if (bn == 0)
            return xfail(x, TB_E_DIVZERO, n, "division by zero: %s / 0", fmt_rat(s1, sizeof s1, an, ad));
        st = zxv_rat_div(an, ad, bn, bd, on, od);
        break;
    }
    if (st == ZXV_E_RANGE)
        return xfail(x, ZXV_E_RANGE, n,
                     "%s %s %s does not fit int64/int64 exactly (refused, never rounded)",
                     fmt_rat(s1, sizeof s1, an, ad), sym[kind - N_ADD], fmt_rat(s2, sizeof s2, bn, bd));
    if (st != ZXV_OK) return xfail(x, st, n, "rational %s refused", sym[kind - N_ADD]);
    return ZXV_OK;
}

static int32_t elem_index(X *x, const tb_node *n, uint64_t *out) {
    int64_t sub[TB_MAX_RANK] = {1, 1};
    const tb_sym *s = &x->p->syms[n->a];
    int32_t st;
    if ((st = evali(x, n->b, &sub[0])) != ZXV_OK) return st;
    if (n->c >= 0 && (st = evali(x, n->c, &sub[1])) != ZXV_OK) return st;
    for (uint32_t r = 0; r < s->rank; r++) {
        if (sub[r] < 1 || sub[r] > x->ext[n->a][r]) {
            if (s->rank == 1)
                return xfail(x, TB_E_BOUNDS, n, "%s(%lld) is outside %s(1:%lld)", s->name,
                             (long long) sub[0], s->name, (long long) x->ext[n->a][0]);
            return xfail(x, TB_E_BOUNDS, n, "%s(%lld, %lld) is outside %s(1:%lld, 1:%lld)", s->name,
                         (long long) sub[0], (long long) sub[1], s->name, (long long) x->ext[n->a][0],
                         (long long) x->ext[n->a][1]);
        }
    }
    *out = (uint64_t) (sub[0] - 1) + (s->rank > 1 ? (uint64_t) (sub[1] - 1) * (uint64_t) x->ext[n->a][0] : 0u);
    return ZXV_OK;
}

static int32_t eval_pow(X *x, const tb_node *n, int64_t *on, int64_t *od) {
    int64_t bn, bd, b0n, b0d, e, rn = 1, rd = 1;
    uint64_t m;
    int32_t st;
    char s1[96];
    if ((st = eval(x, n->a, &bn, &bd)) != ZXV_OK) return st;
    if ((st = evali(x, n->b, &e)) != ZXV_OK) return st;
    b0n = bn;
    b0d = bd;
    if (e < 0 && bn == 0) return xfail(x, TB_E_DIVZERO, n, "0 ** %lld divides by zero", (long long) e);
    m = e < 0 ? (uint64_t) 0 - (uint64_t) e : (uint64_t) e;
    while (m) {
        if (m & 1u) {
            if ((st = tick(x, n)) != ZXV_OK) return st;
            st = zxv_rat_mul(rn, rd, bn, bd, &rn, &rd);
            if (st != ZXV_OK) break;
        }
        m >>= 1;
        if (m) {
            if ((st = tick(x, n)) != ZXV_OK) return st;
            st = zxv_rat_mul(bn, bd, bn, bd, &bn, &bd);
            if (st != ZXV_OK) break;
        }
    }
    if (st == ZXV_OK && e < 0) st = zxv_rat_div(1, 1, rn, rd, &rn, &rd);
    if (st != ZXV_OK) {
        return xfail(x, st == ZXV_E_RANGE ? ZXV_E_RANGE : st, n,
                     "(%s) ** %lld does not fit int64/int64 exactly (refused, never rounded)",
                     fmt_rat(s1, sizeof s1, b0n, b0d), (long long) e);
    }
    *on = rn;
    *od = rd;
    return ZXV_OK;
}

static int32_t eval_sum(X *x, const tb_node *n, int64_t *on, int64_t *od) {
    const tb_slot *sl = &x->p->slots[n->a];
    int64_t lo = 1, hi, an = 0, ad = 1;
    int32_t st;
    if (n->c >= 0) {
        if ((st = evali(x, n->c, &lo)) != ZXV_OK) return st;
        if ((st = evali(x, n->d, &hi)) != ZXV_OK) return st;
    } else {
        hi = x->ext[sl->range_sym][sl->range_axis];
    }
    if (x->nactive >= 64u) return xfail(x, TB_E_LIMIT, n, "loops nested too deep");
    x->active[x->nactive++] = n->a;
    for (int64_t v = lo; v <= hi; v++) {
        int64_t bn, bd;
        x->slot_val[n->a] = v;
        if ((st = tick(x, n)) != ZXV_OK) return st;
        if ((st = eval(x, n->b, &bn, &bd)) != ZXV_OK) return st;
        if ((st = rat_op(x, n, N_ADD, an, ad, bn, bd, &an, &ad)) != ZXV_OK) return st;
        if (v == INT64_MAX) break;
    }
    x->nactive--;
    *on = an;
    *od = ad;
    return ZXV_OK;
}

static int32_t eval(X *x, int32_t id, int64_t *on, int64_t *od) {
    const tb_node *n = &x->p->nodes[id];
    int64_t an, ad, bn, bd;
    int32_t st, c;
    switch (n->kind) {
    case N_NUM:
        *on = n->num;
        *od = n->den;
        return ZXV_OK;
    case N_IDX:
        *on = x->slot_val[n->a];
        *od = 1;
        return ZXV_OK;
    case N_DIM:
        *on = x->dimval[n->a];
        *od = 1;
        return ZXV_OK;
    case N_SYM: {
        const tb_sym *s = &x->p->syms[n->a];
        if (s->role == TB_ROLE_CONST) {
            *on = s->cnum;
            *od = s->cden;
        } else {
            *on = x->num[n->a][0];
            *od = x->den[n->a][0];
        }
        return ZXV_OK;
    }
    case N_ELEM: {
        uint64_t k = 0;
        if ((st = elem_index(x, n, &k)) != ZXV_OK) return st;
        *on = x->num[n->a][k];
        *od = x->den[n->a][k];
        return ZXV_OK;
    }
    case N_NEG:
    case N_ABS:
        if ((st = eval(x, n->a, &an, &ad)) != ZXV_OK) return st;
        if (an == INT64_MIN) return xfail(x, ZXV_E_RANGE, n, "negation overflows int64");
        *on = (n->kind == N_NEG || an < 0) ? -an : an;
        *od = ad;
        return ZXV_OK;
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV:
        if ((st = eval(x, n->a, &an, &ad)) != ZXV_OK) return st;
        if ((st = eval(x, n->b, &bn, &bd)) != ZXV_OK) return st;
        return rat_op(x, n, n->kind, an, ad, bn, bd, on, od);
    case N_POW: return eval_pow(x, n, on, od);
    case N_SUM: return eval_sum(x, n, on, od);
    case N_SCAN:
        if ((st = eval(x, n->a, &an, &ad)) != ZXV_OK) return st;
        if ((st = rat_op(x, n, N_ADD, x->acc_num[id], x->acc_den[id], an, ad, &x->acc_num[id],
                         &x->acc_den[id])) != ZXV_OK)
            return st;
        *on = x->acc_num[id];
        *od = x->acc_den[id];
        return ZXV_OK;
    case N_MIN:
    case N_MAX:
        if ((st = eval(x, n->a, &an, &ad)) != ZXV_OK) return st;
        if ((st = eval(x, n->b, &bn, &bd)) != ZXV_OK) return st;
        if ((st = tick(x, n)) != ZXV_OK) return st;
        if ((st = zxv_rat_cmp(an, ad, bn, bd, &c)) != ZXV_OK) return xfail(x, st, n, "comparison refused");
        if ((n->kind == N_MIN) == (c <= 0)) {
            *on = an;
            *od = ad;
        } else {
            *on = bn;
            *od = bd;
        }
        return ZXV_OK;
    default: return xfail(x, ZXV_E_INTERNAL, n, "unknown node");
    }
}

int32_t tb_fold(const tb_program *p, int32_t node, int64_t *out_num, int64_t *out_den,
                tb_diag *diag) {
    X x;
    memset(&x, 0, sizeof x);
    x.p = p;
    x.limit = TB_DEFAULT_WORK;
    x.diag = diag;
    return eval(&x, node, out_num, out_den);
}

static int32_t run_stmt(X *x, const tb_stmt *st) {
    const tb_program *p = x->p;
    const tb_sym *s = &p->syms[st->sym];
    const tb_node *at = &p->nodes[st->rhs];
    int32_t rc = ZXV_OK;
    int64_t *tn, *td, e0, e1;
    uint64_t count, k = 0;
    for (uint32_t i = 0; i < p->nnodes; i++)
        if (p->nodes[i].kind == N_SCAN) {
            x->acc_num[i] = 0;
            x->acc_den[i] = 1;
        }
    x->nactive = 0;
    if (s->rank == 0) {
        int64_t n, d;
        if ((rc = eval(x, st->rhs, &n, &d)) != ZXV_OK) return rc;
        x->num[st->sym][0] = n;
        x->den[st->sym][0] = d;
        return ZXV_OK;
    }
    e0 = x->ext[st->sym][0];
    e1 = s->rank > 1 ? x->ext[st->sym][1] : 1;
    count = (uint64_t) e0 * (uint64_t) e1;
    tn = (int64_t *) malloc((size_t) (count ? count : 1u) * sizeof(int64_t));
    td = (int64_t *) malloc((size_t) (count ? count : 1u) * sizeof(int64_t));
    if (!tn || !td) {
        free(tn);
        free(td);
        return xfail(x, ZXV_E_NOMEM, at, "out of memory");
    }
    for (uint32_t r = 0; r < s->rank; r++) x->active[x->nactive++] = st->idx[r];
    for (int64_t j = 1; j <= e1 && rc == ZXV_OK; j++) {
        if (s->rank > 1) x->slot_val[st->idx[1]] = j;
        for (int64_t i = 1; i <= e0; i++) {
            x->slot_val[st->idx[0]] = i;
            if ((rc = tick(x, at)) != ZXV_OK) break;
            if ((rc = eval(x, st->rhs, &tn[k], &td[k])) != ZXV_OK) break;
            k++;
        }
    }
    if (rc == ZXV_OK) {
        memcpy(x->num[st->sym], tn, (size_t) count * sizeof(int64_t));
        memcpy(x->den[st->sym], td, (size_t) count * sizeof(int64_t));
    }
    free(tn);
    free(td);
    x->nactive = 0;
    return rc;
}

static int32_t find_param(const tb_program *p, const char *name) {
    char low[TB_MAX_NAME + 1];
    size_t n = strlen(name);
    if (n == 0 || n > TB_MAX_NAME) return -1;
    for (size_t i = 0; i <= n; i++) {
        char c = name[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    for (uint32_t i = 0; i < p->nsyms; i++)
        if (strcmp(p->syms[i].name, low) == 0) return (int32_t) i;
    return -1;
}

int32_t tb_exec(const tb_program *p, const tb_array *arrays, uint32_t count, uint64_t work_limit,
                tb_diag *diag) {
    X x;
    int32_t rc = ZXV_OK;
    int32_t *bound = NULL;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!p || (!arrays && count)) return ZXV_E_ARG;
    memset(&x, 0, sizeof x);
    x.p = p;
    x.diag = diag;
    x.limit = work_limit ? work_limit : TB_DEFAULT_WORK;
    x.num = (int64_t **) calloc(p->nsyms ? p->nsyms : 1u, sizeof(int64_t *));
    x.den = (int64_t **) calloc(p->nsyms ? p->nsyms : 1u, sizeof(int64_t *));
    x.ext = (int64_t (*)[TB_MAX_RANK]) calloc(p->nsyms ? p->nsyms : 1u, sizeof *x.ext);
    x.dimval = (int64_t *) malloc((p->ndims ? p->ndims : 1u) * sizeof(int64_t));
    x.slot_val = (int64_t *) calloc(p->nslots ? p->nslots : 1u, sizeof(int64_t));
    x.acc_num = (int64_t *) calloc(p->nnodes ? p->nnodes : 1u, sizeof(int64_t));
    x.acc_den = (int64_t *) calloc(p->nnodes ? p->nnodes : 1u, sizeof(int64_t));
    bound = (int32_t *) calloc(p->nsyms ? p->nsyms : 1u, sizeof(int32_t));
    if (!x.num || !x.den || !x.ext || !x.dimval || !x.slot_val || !x.acc_num || !x.acc_den || !bound) {
        rc = tb_diag_at(diag, ZXV_E_NOMEM, 0, 0, 0, "out of memory");
        goto done;
    }
    for (uint32_t i = 0; i < p->ndims; i++) x.dimval[i] = -1;

    /* 1. bindings: names, ranks, extents, dimensions */
    for (uint32_t b = 0; b < count; b++) {
        const tb_array *a = &arrays[b];
        int32_t s;
        const tb_sym *sy;
        uint64_t elems = 1;
        if (!a->name) {
            rc = tb_diag_at(diag, ZXV_E_ARG, 0, 0, 0, "binding %u has no name", (unsigned) b);
            goto done;
        }
        s = find_param(p, a->name);
        if (s < 0 || p->syms[s].role < TB_ROLE_IN || p->syms[s].role > TB_ROLE_INOUT) {
            rc = tb_diag_at(diag, TB_E_NAME, 0, 0, 0, "'%.40s' is not an in, out or inout parameter of %s",
                            a->name, p->name);
            goto done;
        }
        sy = &p->syms[s];
        if (bound[s]) {
            rc = tb_diag_at(diag, TB_E_NAME, 0, 0, 0, "'%s' is bound twice", sy->name);
            goto done;
        }
        bound[s] = 1;
        if (a->rank != sy->rank) {
            rc = tb_diag_at(diag, TB_E_SHAPE, sy->line, sy->col, 0, "'%s' has rank %u, the binding rank %u",
                            sy->name, (unsigned) sy->rank, (unsigned) a->rank);
            goto done;
        }
        if (a->reserved != 0) {
            rc = tb_diag_at(diag, ZXV_E_ARG, 0, 0, 0, "'%s': reserved must be 0", sy->name);
            goto done;
        }
        for (uint32_t r = 0; r < TB_MAX_RANK; r++) {
            int64_t e = a->extent[r];
            if (r >= sy->rank) {
                if (e != 0) {
                    rc = tb_diag_at(diag, ZXV_E_ARG, 0, 0, 0, "'%s': unused extents must be 0", sy->name);
                    goto done;
                }
                continue;
            }
            if (e < 0 || e > (int64_t) TB_MAX_ELEMENTS) {
                rc = tb_diag_at(diag, e < 0 ? ZXV_E_ARG : TB_E_LIMIT, 0, 0, 0,
                                "'%s': extent %lld is outside 0..%u", sy->name, (long long) e, TB_MAX_ELEMENTS);
                goto done;
            }
            if (sy->dim[r] >= 0) {
                int64_t *dv = &x.dimval[sy->dim[r]];
                if (*dv >= 0 && *dv != e) {
                    rc = tb_diag_at(diag, TB_E_SHAPE, sy->line, sy->col, 0,
                                    "dimension %s is %lld from an earlier binding but %lld from '%s'",
                                    p->dims[sy->dim[r]].name, (long long) *dv, (long long) e, sy->name);
                    goto done;
                }
                *dv = e;
            } else if (e != sy->fixed[r]) {
                rc = tb_diag_at(diag, TB_E_SHAPE, sy->line, sy->col, 0,
                                "'%s' is declared with extent %lld, the binding has %lld", sy->name,
                                (long long) sy->fixed[r], (long long) e);
                goto done;
            }
            x.ext[s][r] = e;
            elems *= (uint64_t) e;
        }
        if (elems > TB_MAX_ELEMENTS) {
            rc = tb_diag_at(diag, TB_E_LIMIT, 0, 0, 0, "'%s' has more than %u elements", sy->name, TB_MAX_ELEMENTS);
            goto done;
        }
        if (elems && (!a->num || !a->den)) {
            rc = tb_diag_at(diag, ZXV_E_ARG, 0, 0, 0, "'%s': NULL numerator or denominator array", sy->name);
            goto done;
        }
    }
    for (uint32_t i = 0; i < p->nsyms; i++) {
        const tb_sym *sy = &p->syms[i];
        if (sy->role >= TB_ROLE_IN && sy->role <= TB_ROLE_INOUT && !bound[i]) {
            rc = tb_diag_at(diag, TB_E_NAME, sy->line, sy->col, 0, "parameter '%s' is not bound", sy->name);
            goto done;
        }
    }

    /* 2. storage: variables get their extents from the bound dimensions */
    for (uint32_t i = 0; i < p->nsyms; i++) {
        const tb_sym *sy = &p->syms[i];
        uint64_t elems = 1;
        if (sy->role == TB_ROLE_CONST) continue;
        for (uint32_t r = 0; r < sy->rank; r++) {
            if (sy->role == TB_ROLE_VAR) x.ext[i][r] = sy->dim[r] >= 0 ? x.dimval[sy->dim[r]] : sy->fixed[r];
            elems *= (uint64_t) x.ext[i][r];
        }
        if (elems > TB_MAX_ELEMENTS) {
            rc = tb_diag_at(diag, TB_E_LIMIT, sy->line, sy->col, 0, "'%s' has more than %u elements", sy->name,
                            TB_MAX_ELEMENTS);
            goto done;
        }
        x.num[i] = (int64_t *) calloc((size_t) (elems ? elems : 1u), sizeof(int64_t));
        x.den[i] = (int64_t *) malloc((size_t) (elems ? elems : 1u) * sizeof(int64_t));
        if (!x.num[i] || !x.den[i]) {
            rc = tb_diag_at(diag, ZXV_E_NOMEM, 0, 0, 0, "out of memory");
            goto done;
        }
        for (uint64_t k = 0; k < (elems ? elems : 1u); k++) x.den[i][k] = 1;
    }

    /* 3. inputs, normalized; a bad value is refused before anything runs */
    for (uint32_t b = 0; b < count; b++) {
        const tb_array *a = &arrays[b];
        int32_t s = find_param(p, a->name);
        const tb_sym *sy = &p->syms[s];
        uint64_t elems = 1;
        if (sy->role == TB_ROLE_OUT) continue;
        for (uint32_t r = 0; r < sy->rank; r++) elems *= (uint64_t) x.ext[s][r];
        for (uint64_t k = 0; k < elems; k++) {
            int32_t st = zxv_rat_normalize(a->num[k], a->den[k], &x.num[s][k], &x.den[s][k]);
            if (st != ZXV_OK) {
                char where[128];
                if (sy->rank == 0)
                    snprintf(where, sizeof where, "%s", sy->name);
                else if (sy->rank == 1)
                    snprintf(where, sizeof where, "%s(%llu)", sy->name, (unsigned long long) k + 1u);
                else
                    snprintf(where, sizeof where, "%s(%llu, %llu)", sy->name,
                             (unsigned long long) (k % (uint64_t) x.ext[s][0]) + 1u,
                             (unsigned long long) (k / (uint64_t) x.ext[s][0]) + 1u);
                rc = tb_diag_at(diag, st, 0, 0, 0, "input %s = %lld/%lld is not a valid rational%s", where,
                                (long long) a->num[k], (long long) a->den[k],
                                a->den[k] == 0 ? " (denominator 0)" : " (INT64_MIN is refused)");
                goto done;
            }
        }
    }

    /* 4. conformance: an index alone as a subscript runs over that axis exactly */
    for (uint32_t u = 0; u < p->nuses; u++) {
        const tb_use *us = &p->uses[u];
        const tb_slot *sl = &p->slots[us->slot];
        int64_t want, have;
        if (sl->range_axis < 0) continue;
        want = x.ext[sl->range_sym][sl->range_axis];
        have = x.ext[us->sym][us->axis];
        if (want != have) {
            rc = tb_diag_at(diag, TB_E_SHAPE, us->line, us->col, 0,
                            "%s runs over 1:%lld (%s, axis %d) but %s has extent %lld on axis %d",
                            sl->name, (long long) want, p->syms[sl->range_sym].name, sl->range_axis + 1,
                            p->syms[us->sym].name, (long long) have, us->axis + 1);
            goto done;
        }
    }

    /* 5. the statements, in order */
    for (uint32_t i = 0; i < p->nstmts && rc == ZXV_OK; i++) rc = run_stmt(&x, &p->stmts[i]);
    if (rc != ZXV_OK) goto done;

    /* 6. write back out and inout: only now, after everything succeeded */
    for (uint32_t b = 0; b < count; b++) {
        const tb_array *a = &arrays[b];
        int32_t s = find_param(p, a->name);
        const tb_sym *sy = &p->syms[s];
        uint64_t elems = 1;
        if (sy->role == TB_ROLE_IN) continue;
        for (uint32_t r = 0; r < sy->rank; r++) elems *= (uint64_t) x.ext[s][r];
        if (!elems) continue;
        memcpy(a->num, x.num[s], (size_t) elems * sizeof(int64_t));
        memcpy(a->den, x.den[s], (size_t) elems * sizeof(int64_t));
    }
    tb_set_error("");

done:
    tb_set_work(x.work);
    if (x.num)
        for (uint32_t i = 0; i < p->nsyms; i++) free(x.num[i]);
    if (x.den)
        for (uint32_t i = 0; i < p->nsyms; i++) free(x.den[i]);
    free(x.num);
    free(x.den);
    free(x.ext);
    free(x.dimval);
    free(x.slot_val);
    free(x.acc_num);
    free(x.acc_den);
    free(bound);
    return rc;
}
