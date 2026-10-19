/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_print.c — the canonical text of a program and of a value, and the
 * display text. Every append goes through kr_buf, which checks its room
 * (no offset arithmetic on snprintf results). Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

static const char *truth_word(int64_t code) {
    switch (code) {
    case ZXV_LP_TRUE: return "true";
    case ZXV_LP_FALSE: return "false";
    case ZXV_LP_BOTH: return "both";
    default: return "neither";
    }
}

static const char *variant_word(uint8_t kind, uint8_t v) {
    if (kind == N_RED) return v == V_LAST ? "reduce" : v == V_FIRST ? "reducef" : "foldl";
    return v == V_LAST ? "scan" : v == V_FIRST ? "scanf" : "scanl";
}

static void lits(const kr_program *p, const kr_node *nd, kr_buf *b, int truth) {
    for (uint32_t k = 0; k < nd->cnt; k++) {
        kr_buf_putc(b, ' ');
        if (truth) kr_buf_puts(b, truth_word(p->lnum[nd->lit + k]));
        else kr_buf_rat(b, p->lnum[nd->lit + k], p->lden[nd->lit + k]);
    }
}

static void node(const kr_program *p, int32_t n, kr_buf *b) {
    const kr_node *nd = &p->nodes[n];
    switch (nd->kind) {
    case N_NUM: kr_buf_rat(b, p->lnum[nd->lit], p->lden[nd->lit]); return;
    case N_TRUTH: kr_buf_puts(b, truth_word(p->lnum[nd->lit])); return;
    case N_VEC:
    case N_TVEC:
        kr_buf_puts(b, nd->kind == N_VEC ? "(vec" : "(tvec");
        lits(p, nd, b, nd->kind == N_TVEC);
        kr_buf_putc(b, ')');
        return;
    case N_MAT:
    case N_TMAT:
        kr_buf_puts(b, nd->kind == N_MAT ? "(mat " : "(tmat ");
        kr_buf_u64(b, nd->rows);
        kr_buf_putc(b, ' ');
        kr_buf_u64(b, nd->cols);
        lits(p, nd, b, nd->kind == N_TMAT);
        kr_buf_putc(b, ')');
        return;
    case N_NAME: kr_buf_puts(b, p->syms[nd->sym].name); return;
    case N_LET:
        kr_buf_puts(b, "(let ");
        kr_buf_puts(b, p->syms[nd->sym].name);
        break;
    case N_MON:
    case N_DYA:
        kr_buf_putc(b, '(');
        kr_buf_puts(b, kr_fn_word[nd->fn]);
        break;
    case N_RED:
    case N_SCAN:
        kr_buf_putc(b, '(');
        kr_buf_puts(b, variant_word(nd->kind, nd->variant));
        kr_buf_putc(b, ' ');
        kr_buf_puts(b, kr_fn_word[nd->fn]);
        break;
    case N_OUTER:
        kr_buf_puts(b, "(outer ");
        kr_buf_puts(b, kr_fn_word[nd->fn]);
        break;
    case N_INNER:
        kr_buf_puts(b, "(inner ");
        kr_buf_puts(b, kr_fn_word[nd->fn]);
        kr_buf_putc(b, ' ');
        kr_buf_puts(b, kr_fn_word[nd->fn2]);
        break;
    case N_INDEX2:
        kr_buf_puts(b, "(index2 ");
        node(p, nd->a, b);
        kr_buf_puts(b, nd->b < 0 ? " _" : " ");
        if (nd->b >= 0) node(p, nd->b, b);
        kr_buf_puts(b, nd->c < 0 ? " _" : " ");
        if (nd->c >= 0) node(p, nd->c, b);
        kr_buf_putc(b, ')');
        return;
    default: kr_buf_puts(b, "?"); return;
    }
    if (nd->a >= 0) {
        kr_buf_putc(b, ' ');
        node(p, nd->a, b);
    }
    if (nd->b >= 0) {
        kr_buf_putc(b, ' ');
        node(p, nd->b, b);
    }
    kr_buf_putc(b, ')');
}

int32_t kr_print_program(const kr_program *p, char **out_text, size_t *out_len) {
    kr_buf b = {NULL, 0, 0, 0};
    kr_buf_puts(&b, "kron\n");
    for (uint32_t s = 0; s < p->nstmts; s++) {
        node(p, p->stmts[s], &b);
        kr_buf_putc(&b, '\n');
    }
    if (b.oom) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    *out_text = b.b;
    *out_len = b.n;
    return ZXV_OK;
}

static void elem(const kr_value *v, uint64_t i, kr_buf *b) {
    if (v->kind == KR_KIND_TRUTH) kr_buf_puts(b, truth_word(v->t[i]));
    else kr_buf_rat(b, v->num[i], v->den[i]);
}

void kr_print_value(const kr_value *v, kr_buf *b) {
    int truth = v->kind == KR_KIND_TRUTH;
    if (v->rank == 0) {
        elem(v, 0, b);
        return;
    }
    if (v->rank == 1) {
        kr_buf_puts(b, truth ? "(tvec" : "(vec");
    } else {
        kr_buf_puts(b, truth ? "(tmat " : "(mat ");
        kr_buf_i64(b, v->shape[0]);
        kr_buf_putc(b, ' ');
        kr_buf_i64(b, v->shape[1]);
    }
    for (uint64_t i = 0; i < v->n; i++) {
        kr_buf_putc(b, ' ');
        elem(v, i, b);
    }
    kr_buf_putc(b, ')');
}

int32_t kr_value_text(const kr_value *v, char *buf, size_t cap, size_t *out_len) {
    kr_buf b = {NULL, 0, 0, 0};
    int32_t st;
    if (!v) return ZXV_E_ARG;
    kr_print_value(v, &b);
    if (b.oom) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    st = kr_out_text(b.b ? b.b : "", b.n, buf, cap, out_len);
    free(b.b);
    return st;
}

int32_t kr_value_display(const kr_value *v, char *buf, size_t cap, size_t *out_len) {
    kr_buf b = {NULL, 0, 0, 0};
    int32_t st;
    int64_t cols;
    if (!v) return ZXV_E_ARG;
    cols = v->rank == 2 ? v->shape[1] : (int64_t) v->n;
    for (uint64_t i = 0; i < v->n; i++) {
        if (i > 0) kr_buf_putc(&b, (cols > 0 && (int64_t) (i % (uint64_t) cols) == 0) ? '\n' : ' ');
        elem(v, i, &b);
    }
    if (v->rank == 2 && v->n == 0) {
        kr_buf_puts(&b, "(empty ");
        kr_buf_i64(&b, v->shape[0]);
        kr_buf_putc(&b, 'x');
        kr_buf_i64(&b, v->shape[1]);
        kr_buf_putc(&b, ')');
    }
    if (b.oom) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    st = kr_out_text(b.b ? b.b : "", b.n, buf, cap, out_len);
    free(b.b);
    return st;
}
