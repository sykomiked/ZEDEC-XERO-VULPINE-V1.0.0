/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_trebago.c - Trebago steps (sdk/pillars/fortran-trebago): exact
 * numeric kernels over int64/int64 rationals.
 *
 * Every `in` and `inout` parameter is bound from the input of the same
 * name (letter case ignored). A value is exact text, separated by commas
 * or spaces:
 *
 *   alpha = -2/3              a scalar
 *   x     = 1, 2, 1/3         a vector (its extent is the count)
 *   a     = 1, 1/2; 1/2, 1/3  a matrix, rows separated by ';'
 *
 * (`ztrebago run` takes a matrix column-major as a(2,2)=...; here the
 * rows are written as they are read, and the adapter lays them out
 * column-major for tb_exec.) An `out` parameter is sized by the extents
 * the inputs bind; a dimension nothing binds is read from an input of
 * that name (n = 7). A missing input leaves the step NEITHER.
 *
 * After the run, every `out` and `inout` parameter is an output, as the
 * text `ztrebago run` prints: "1/3, -1/3, -1", and rows joined by "; "
 * for a matrix. Nothing is rounded: a value that does not fit int64/int64
 * is refused (ZXV_E_RANGE) with the line and column of the operation.
 * `!>` golden lines in the source are comments to Trebago and ignored.
 *
 * Step options:
 *   counts=NAME[,NAME]   these inputs count whole units (cents, people,
 *                        litres): every element must be a whole number
 *                        >= 0, else the step is refused before it runs
 *   nonneg=NAME[,NAME]   these outputs must not go below zero. A negative
 *                        element is not an arithmetic error: it is a
 *                        computed shortage, so the step is HELD (BOTH)
 *                        for a person to decide, with every negative
 *                        element named
 *   labels=NAME          an input holding one word per element (days,
 *                        districts), used to name elements in the notes
 */
#include "zp_internal.h"

#include <ztrebago.h>
#include <zxv_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PARAMS 64u
#define MAX_DIMS 64u

typedef struct {
    tb_param pa;
    int64_t *num, *den;
    uint64_t count;
    int64_t extent[TB_MAX_RANK];
} bound;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }

static int name_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

/* The input named `name`, exactly, else ignoring letter case. */
static const char *find_input(const zp_step *s, const char *name) {
    const char *v = zp_input(s, name);
    uint32_t n = zp_input_count(s);
    if (v) return v;
    for (uint32_t i = 0; i < n; i++) {
        const char *k, *val;
        zp_input_at(s, i, &k, &val);
        if (k && name_eq(k, name)) return val;
    }
    return NULL;
}

/* Is `name` in the comma-separated list `list`? */
static int in_list(const char *list, const char *name) {
    size_t nl = strlen(name);
    while (list && *list) {
        const char *e = strchr(list, ',');
        size_t l = e ? (size_t) (e - list) : strlen(list);
        if (l == nl) {
            size_t k = 0;
            while (k < l && lower(list[k]) == lower(name[k])) k++;
            if (k == l) return 1;
        }
        if (!e) break;
        list = e + 1;
    }
    return 0;
}

static int is_sep(char c) { return c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* The i-th word of a space- or comma-separated list, or "". */
static void word_at(const char *s, uint32_t i, char *buf, size_t cap) {
    uint32_t k = 0;
    buf[0] = '\0';
    if (!s) return;
    while (*s) {
        const char *e;
        while (*s && is_sep(*s)) s++;
        if (!*s) break;
        e = s;
        while (*e && !is_sep(*e)) e++;
        if (k++ == i) {
            size_t n = (size_t) (e - s);
            if (n >= cap) n = cap - 1u;
            memcpy(buf, s, n);
            buf[n] = '\0';
            return;
        }
        s = e;
    }
}

/* Parse an input value into b (rows separated by ';'). Returns ZXV_OK or
 * a status with `why` filled. */
static int32_t parse_value(bound *b, const char *v, char *why, size_t whycap) {
    uint64_t rows = 1, cols = 0, cur = 0, cap = 16;
    const char *p = v;
    b->num = (int64_t *) malloc(cap * sizeof *b->num);
    b->den = (int64_t *) malloc(cap * sizeof *b->den);
    b->count = 0;
    if (!b->num || !b->den) return ZXV_E_NOMEM;
    for (;;) {
        while (*p && is_sep(*p)) p++;
        if (!*p || *p == ';') {
            if (cur == 0) {
                snprintf(why, whycap, "row %llu is empty", (unsigned long long) rows);
                return TB_E_SHAPE;
            }
            if (rows == 1) cols = cur;
            else if (cur != cols) {
                snprintf(why, whycap, "row %llu has %llu values, row 1 has %llu", (unsigned long long) rows,
                         (unsigned long long) cur, (unsigned long long) cols);
                return TB_E_SHAPE;
            }
            if (!*p) break;
            p++;
            rows++;
            cur = 0;
            continue;
        }
        {
            const char *e = p;
            char tok[64];
            size_t n;
            int32_t st;
            while (*e && !is_sep(*e) && *e != ';') e++;
            n = (size_t) (e - p);
            if (n >= sizeof tok) {
                snprintf(why, whycap, "'%.20s...' is too long for an exact value", p);
                return ZXV_E_RANGE;
            }
            memcpy(tok, p, n);
            tok[n] = '\0';
            if (b->count == cap) {
                int64_t *nn, *nd;
                if (cap >= TB_MAX_ELEMENTS) {
                    snprintf(why, whycap, "more than %u values", TB_MAX_ELEMENTS);
                    return TB_E_LIMIT;
                }
                cap *= 2u;
                nn = (int64_t *) realloc(b->num, cap * sizeof *nn);
                if (!nn) return ZXV_E_NOMEM;
                b->num = nn;
                nd = (int64_t *) realloc(b->den, cap * sizeof *nd);
                if (!nd) return ZXV_E_NOMEM;
                b->den = nd;
            }
            st = zxv_rat_parse(tok, &b->num[b->count], &b->den[b->count]);
            if (st != ZXV_OK) {
                snprintf(why, whycap, "'%.31s' is not an exact value (write 3, -0.25 or 1/3; no exponents)", tok);
                return st;
            }
            b->count++;
            cur++;
            p = e;
        }
    }
    /* shape against the declaration */
    if (b->pa.rank == 0) {
        if (b->count != 1) {
            snprintf(why, whycap, "a scalar takes one value, got %llu", (unsigned long long) b->count);
            return TB_E_SHAPE;
        }
    } else if (b->pa.rank == 1) {
        if (rows != 1) {
            snprintf(why, whycap, "a vector takes one row, got %llu rows separated by ';'", (unsigned long long) rows);
            return TB_E_SHAPE;
        }
        b->extent[0] = (int64_t) b->count;
    } else {
        /* rows as written -> column-major */
        int64_t *tn = (int64_t *) malloc(b->count * sizeof *tn);
        int64_t *td = (int64_t *) malloc(b->count * sizeof *td);
        if (!tn || !td) {
            free(tn);
            free(td);
            return ZXV_E_NOMEM;
        }
        for (uint64_t r = 0; r < rows; r++)
            for (uint64_t c = 0; c < cols; c++) {
                tn[r + c * rows] = b->num[r * cols + c];
                td[r + c * rows] = b->den[r * cols + c];
            }
        free(b->num);
        free(b->den);
        b->num = tn;
        b->den = td;
        b->extent[0] = (int64_t) rows;
        b->extent[1] = (int64_t) cols;
    }
    return ZXV_OK;
}

static void put_value(zp_buf *t, int64_t n, int64_t d) {
    char v[96];
    size_t l = 0;
    if (zxv_rat_format(n, d, v, sizeof v, &l) != ZXV_OK) snprintf(v, sizeof v, "%lld/%lld", (long long) n, (long long) d);
    zp_buf_puts(t, v);
}

/* The text `ztrebago run` prints for one output. */
static void value_text(zp_buf *t, const bound *b) {
    if (b->pa.rank < 2) {
        for (uint64_t k = 0; k < b->count; k++) {
            if (k) zp_buf_puts(t, ", ");
            put_value(t, b->num[k], b->den[k]);
        }
        return;
    }
    for (int64_t r = 0; r < b->extent[0]; r++) {
        if (r) zp_buf_puts(t, "; ");
        for (int64_t c = 0; c < b->extent[1]; c++) {
            uint64_t k = (uint64_t) r + (uint64_t) c * (uint64_t) b->extent[0];
            if (c) zp_buf_puts(t, ", ");
            put_value(t, b->num[k], b->den[k]);
        }
    }
}

/* Name element k of b for a note: a label, (i) or (i,j). */
static void elem_name(const zp_step *s, const bound *b, uint64_t k, char *buf, size_t cap) {
    const char *lo = zp_step_opt(s, "labels");
    const char *lv = lo ? find_input(s, lo) : NULL;
    char lab[48];
    if (b->pa.rank == 0) {
        snprintf(buf, cap, "%.31s", b->pa.name);
    } else if (b->pa.rank == 1) {
        word_at(lv, (uint32_t) k, lab, sizeof lab);
        if (lab[0])
            snprintf(buf, cap, "%.31s(%llu) [%.47s]", b->pa.name, (unsigned long long) k + 1u, lab);
        else
            snprintf(buf, cap, "%.31s(%llu)", b->pa.name, (unsigned long long) k + 1u);
    } else {
        snprintf(buf, cap, "%.31s(%llu,%llu)", b->pa.name, (unsigned long long) (k % (uint64_t) b->extent[0]) + 1u,
                 (unsigned long long) (k / (uint64_t) b->extent[0]) + 1u);
    }
}

void zp_lang_trebago(zp_step *s, const char *src, size_t len) {
    tb_program *p = NULL;
    tb_diag d;
    bound b[MAX_PARAMS];
    tb_array arr[MAX_PARAMS];
    char dname[MAX_DIMS][TB_MAX_NAME + 1];
    int64_t dval[MAX_DIMS];
    uint32_t np = 0, nd = 0, missing = 0;
    const char *counts = zp_step_opt(s, "counts");
    const char *nonneg = zp_step_opt(s, "nonneg");
    char why[400], msg[600];
    int32_t st;

    memset(b, 0, sizeof b);
    memset(&d, 0, sizeof d);
    st = tb_parse(src, len, &p, &d);
    if (st != ZXV_OK) {
        zp_refuse(s, st, tb_status_name(st), d.line, d.col, d.msg);
        return;
    }
    tb_param_count(p, &np);
    if (np > MAX_PARAMS) {
        zp_refuse(s, TB_E_LIMIT, tb_status_name(TB_E_LIMIT), 0, 0, "more than 64 parameters");
        goto done;
    }
    for (uint32_t i = 0; i < np; i++) tb_param_at(p, i, &b[i].pa);

    /* every option name must be a parameter of the right kind */
    for (int pass = 0; pass < 2; pass++) {
        const char *list = pass ? nonneg : counts;
        const char *q = list;
        while (q && *q) {
            const char *e = strchr(q, ',');
            size_t l = e ? (size_t) (e - q) : strlen(q);
            int ok = 0;
            for (uint32_t i = 0; i < np; i++) {
                int fits = pass ? b[i].pa.role != TB_ROLE_IN : b[i].pa.role != TB_ROLE_OUT;
                if (fits && strlen(b[i].pa.name) == l) {
                    size_t k = 0;
                    while (k < l && lower(q[k]) == lower(b[i].pa.name[k])) k++;
                    if (k == l) ok = 1;
                }
            }
            if (!ok) {
                snprintf(msg, sizeof msg, "option %.31s= names '%.*s', which is not %.31s of this program",
                         pass ? "nonneg" : "counts", (int) (l > 64 ? 64 : l), q, pass ? "an output" : "an input");
                zp_refuse(s, ZP_E_ARG, NULL, 0, 0, msg);
                goto done;
            }
            if (!e) break;
            q = e + 1;
        }
    }

    /* bind inputs; collect the dimensions they set */
    for (uint32_t i = 0; i < np; i++) {
        const char *v;
        if (b[i].pa.role == TB_ROLE_OUT) continue;
        v = find_input(s, b[i].pa.name);
        if (!v) {
            snprintf(msg, sizeof msg, "needs an input named %.31s (%.31s)", b[i].pa.name,
                     b[i].pa.role == TB_ROLE_IN ? "in" : "inout");
            zp_pending(s, msg);
            missing++;
            continue;
        }
        st = parse_value(&b[i], v, why, sizeof why);
        if (st != ZXV_OK) {
            snprintf(msg, sizeof msg, "input %.31s: %.300s", b[i].pa.name, why);
            zp_refuse(s, st, tb_status_name(st), 0, 0, msg);
            goto done;
        }
        if (counts && in_list(counts, b[i].pa.name))
            for (uint64_t k = 0; k < b[i].count; k++)
                if (b[i].den[k] != 1 || b[i].num[k] < 0) {
                    char en[96], vt[96];
                    size_t l = 0;
                    elem_name(s, &b[i], k, en, sizeof en);
                    if (zxv_rat_format(b[i].num[k], b[i].den[k], vt, sizeof vt, &l) != ZXV_OK) strcpy(vt, "?");
                    snprintf(msg, sizeof msg,
                             "input %.95s is %.95s: it counts whole units (option counts=), so it must be a whole "
                             "number >= 0",
                             en, vt);
                    zp_refuse(s, ZXV_E_RANGE, NULL, 0, 0, msg);
                    goto done;
                }
        for (uint32_t r = 0; r < b[i].pa.rank; r++) {
            int known = 0;
            if (!b[i].pa.dim[r][0]) continue;
            for (uint32_t k = 0; k < nd; k++)
                if (strcmp(dname[k], b[i].pa.dim[r]) == 0) known = 1;
            if (!known && nd < MAX_DIMS) {
                strcpy(dname[nd], b[i].pa.dim[r]);
                dval[nd++] = b[i].extent[r];
            }
        }
    }
    if (missing) goto done;

    /* size the outputs */
    for (uint32_t i = 0; i < np; i++) {
        uint64_t elems = 1;
        if (b[i].pa.role != TB_ROLE_OUT) continue;
        for (uint32_t r = 0; r < b[i].pa.rank; r++) {
            int found = 0;
            if (!b[i].pa.dim[r][0]) {
                b[i].extent[r] = b[i].pa.fixed[r];
                continue;
            }
            for (uint32_t k = 0; k < nd; k++)
                if (strcmp(dname[k], b[i].pa.dim[r]) == 0) {
                    b[i].extent[r] = dval[k];
                    found = 1;
                }
            if (!found) {
                const char *v = find_input(s, b[i].pa.dim[r]);
                int64_t n = 0, dd = 0;
                if (!v) {
                    snprintf(msg, sizeof msg, "needs the size %.31s of output %.31s: add an input %.31s = N", b[i].pa.dim[r],
                             b[i].pa.name, b[i].pa.dim[r]);
                    zp_pending(s, msg);
                    missing++;
                    continue;
                }
                if (zxv_rat_parse(v, &n, &dd) != ZXV_OK || dd != 1 || n < 0 || n > (int64_t) TB_MAX_ELEMENTS) {
                    snprintf(msg, sizeof msg, "input %.31s: a size must be a whole number from 0 to %u", b[i].pa.dim[r],
                             TB_MAX_ELEMENTS);
                    zp_refuse(s, TB_E_SHAPE, tb_status_name(TB_E_SHAPE), 0, 0, msg);
                    goto done;
                }
                b[i].extent[r] = n;
                if (nd < MAX_DIMS) {
                    strcpy(dname[nd], b[i].pa.dim[r]);
                    dval[nd++] = n;
                }
            }
        }
        if (missing) continue;
        for (uint32_t r = 0; r < b[i].pa.rank; r++) {
            uint64_t e = b[i].extent[r] > 0 ? (uint64_t) b[i].extent[r] : 0u;
            if (e > TB_MAX_ELEMENTS || (e && elems > TB_MAX_ELEMENTS / e)) {
                snprintf(msg, sizeof msg, "output %.31s would have more than %u elements", b[i].pa.name,
                         TB_MAX_ELEMENTS);
                zp_refuse(s, TB_E_LIMIT, tb_status_name(TB_E_LIMIT), 0, 0, msg);
                goto done;
            }
            elems *= e;
        }
        b[i].count = elems;
        b[i].num = (int64_t *) calloc(elems ? (size_t) elems : 1u, sizeof *b[i].num);
        b[i].den = (int64_t *) calloc(elems ? (size_t) elems : 1u, sizeof *b[i].den);
        if (!b[i].num || !b[i].den) {
            zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
            goto done;
        }
        for (uint64_t k = 0; k < (elems ? elems : 1u); k++) b[i].den[k] = 1;
    }
    if (missing) goto done;

    for (uint32_t i = 0; i < np; i++) {
        memset(&arr[i], 0, sizeof arr[i]);
        arr[i].name = b[i].pa.name;
        arr[i].num = b[i].num;
        arr[i].den = b[i].den;
        arr[i].rank = b[i].pa.rank;
        arr[i].extent[0] = b[i].pa.rank > 0 ? b[i].extent[0] : 0;
        arr[i].extent[1] = b[i].pa.rank > 1 ? b[i].extent[1] : 0;
    }
    memset(&d, 0, sizeof d);
    st = tb_exec(p, arr, np, 0, &d);
    if (st != ZXV_OK) {
        zp_refuse(s, st, tb_status_name(st), d.line, d.col, d.msg);
        goto done;
    }

    {
        uint64_t work = 0;
        char pname[TB_MAX_NAME + 1];
        size_t pl = 0;
        tb_last_work(&work);
        if (tb_program_name(p, pname, sizeof pname, &pl) != ZXV_OK) strcpy(pname, "?");
        zp_notef(s, "trebago %.31s: %llu exact rational operations, nothing rounded", pname,
                 (unsigned long long) work);
    }
    for (uint32_t i = 0; i < np; i++) {
        zp_buf t = {0};
        if (b[i].pa.role == TB_ROLE_IN) continue;
        value_text(&t, &b[i]);
        if (t.oom) {
            zp_buf_free(&t);
            zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
            goto done;
        }
        zp_output(s, b[i].pa.name, t.p ? t.p : "");
        zp_buf_free(&t);
    }
    if (nonneg)
        for (uint32_t i = 0; i < np; i++) {
            if (b[i].pa.role == TB_ROLE_IN || !in_list(nonneg, b[i].pa.name)) continue;
            for (uint64_t k = 0; k < b[i].count; k++)
                if (b[i].num[k] < 0) {
                    char en[96], vt[96];
                    size_t l = 0;
                    elem_name(s, &b[i], k, en, sizeof en);
                    if (zxv_rat_format(b[i].num[k], b[i].den[k], vt, sizeof vt, &l) != ZXV_OK) strcpy(vt, "?");
                    snprintf(msg, sizeof msg,
                             "%.95s is %.95s, below zero: a computed shortage, held for a person to decide (option nonneg=)",
                             en, vt);
                    zp_hold(s, msg);
                }
        }
done:
    for (uint32_t i = 0; i < np && i < MAX_PARAMS; i++) {
        free(b[i].num);
        free(b[i].den);
    }
    tb_program_destroy(p);
}
