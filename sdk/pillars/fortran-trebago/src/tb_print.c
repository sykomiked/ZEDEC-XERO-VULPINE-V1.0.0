/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_print.c — the canonical Trebago text of a program and its digest.
 *
 * Canonical form: "trebago NAME", then one declaration per line in source
 * order (constants as their exact value), then the statements, then
 * "end"; two-space indentation, lower-case names, single spaces around
 * binary operators, minimal parentheses, no comments, "\n" line ends.
 * tb_parse of the canonical text prints the same text again (the tests
 * and the fuzzer check this). Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tb_internal.h"

void tb_buf_puts(tb_buf *b, const char *s) {
    size_t n = strlen(s);
    if (b->oom) return;
    if (b->n + n + 1u > b->cap) {
        size_t nc = b->cap ? b->cap : 256u;
        char *nb;
        while (b->n + n + 1u > nc) nc *= 2u;
        nb = (char *) realloc(b->b, nc);
        if (!nb) {
            b->oom = 1;
            return;
        }
        b->b = nb;
        b->cap = nc;
    }
    memcpy(b->b + b->n, s, n);
    b->n += n;
    b->b[b->n] = '\0';
}

static void put_rat(tb_buf *b, int64_t num, int64_t den) {
    char s[96];
    size_t n = 0;
    if (zxv_rat_format(num, den, s, sizeof s, &n) != ZXV_OK) snprintf(s, sizeof s, "?");
    tb_buf_puts(b, s);
}

static int prec(const tb_node *n) {
    switch (n->kind) {
    case N_ADD:
    case N_SUB: return 1;
    case N_MUL:
    case N_DIV: return 2;
    case N_NEG: return 3;
    case N_POW: return 4;
    default: return 5;
    }
}

static void child(const tb_program *p, int32_t c, int parens, tb_buf *b) {
    if (parens) tb_buf_puts(b, "(");
    tb_print_node(p, c, b);
    if (parens) tb_buf_puts(b, ")");
}

void tb_print_node(const tb_program *p, int32_t id, tb_buf *b) {
    const tb_node *n = &p->nodes[id];
    int pr = prec(n);
    switch (n->kind) {
    case N_NUM: put_rat(b, n->num, n->den); return;
    case N_IDX: tb_buf_puts(b, p->slots[n->a].name); return;
    case N_DIM: tb_buf_puts(b, p->dims[n->a].name); return;
    case N_SYM: tb_buf_puts(b, p->syms[n->a].name); return;
    case N_ELEM:
        tb_buf_puts(b, p->syms[n->a].name);
        tb_buf_puts(b, "(");
        tb_print_node(p, n->b, b);
        if (n->c >= 0) {
            tb_buf_puts(b, ", ");
            tb_print_node(p, n->c, b);
        }
        tb_buf_puts(b, ")");
        return;
    case N_NEG:
        tb_buf_puts(b, "-");
        child(p, n->a, prec(&p->nodes[n->a]) < 3, b);
        return;
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV: {
        static const char *const ops[] = {" + ", " - ", " * ", " / "};
        child(p, n->a, prec(&p->nodes[n->a]) < pr, b);
        tb_buf_puts(b, ops[n->kind - N_ADD]);
        child(p, n->b, prec(&p->nodes[n->b]) <= pr, b);
        return;
    }
    case N_POW:
        child(p, n->a, prec(&p->nodes[n->a]) < 5, b);
        tb_buf_puts(b, " ** ");
        child(p, n->b, prec(&p->nodes[n->b]) < 3, b);
        return;
    case N_SUM:
        tb_buf_puts(b, "sum(");
        tb_buf_puts(b, p->slots[n->a].name);
        if (n->c >= 0) {
            tb_buf_puts(b, " = ");
            tb_print_node(p, n->c, b);
            tb_buf_puts(b, ":");
            tb_print_node(p, n->d, b);
        }
        tb_buf_puts(b, ", ");
        tb_print_node(p, n->b, b);
        tb_buf_puts(b, ")");
        return;
    case N_SCAN:
    case N_ABS:
        tb_buf_puts(b, n->kind == N_SCAN ? "scan(" : "abs(");
        tb_print_node(p, n->a, b);
        tb_buf_puts(b, ")");
        return;
    case N_MIN:
    case N_MAX:
        tb_buf_puts(b, n->kind == N_MIN ? "min(" : "max(");
        tb_print_node(p, n->a, b);
        tb_buf_puts(b, ", ");
        tb_print_node(p, n->b, b);
        tb_buf_puts(b, ")");
        return;
    default: tb_buf_puts(b, "?"); return;
    }
}

int32_t tb_print_program(const tb_program *p, char **out_text, size_t *out_len) {
    static const char *const roles[] = {"", "in", "out", "inout", "var", "const"};
    tb_buf b = {NULL, 0, 0, 0};
    char num[32];
    tb_buf_puts(&b, "trebago ");
    tb_buf_puts(&b, p->name);
    tb_buf_puts(&b, "\n");
    for (uint32_t i = 0; i < p->nsyms; i++) {
        const tb_sym *s = &p->syms[i];
        tb_buf_puts(&b, "  ");
        tb_buf_puts(&b, roles[s->role]);
        tb_buf_puts(&b, " ");
        tb_buf_puts(&b, s->name);
        if (s->role == TB_ROLE_CONST) {
            tb_buf_puts(&b, " = ");
            put_rat(&b, s->cnum, s->cden);
        } else if (s->rank) {
            tb_buf_puts(&b, "(");
            for (uint32_t r = 0; r < s->rank; r++) {
                if (r) tb_buf_puts(&b, ", ");
                if (s->dim[r] >= 0) {
                    tb_buf_puts(&b, p->dims[s->dim[r]].name);
                } else {
                    snprintf(num, sizeof num, "%lld", (long long) s->fixed[r]);
                    tb_buf_puts(&b, num);
                }
            }
            tb_buf_puts(&b, ")");
        }
        tb_buf_puts(&b, "\n");
    }
    for (uint32_t i = 0; i < p->nstmts; i++) {
        const tb_stmt *st = &p->stmts[i];
        const tb_sym *s = &p->syms[st->sym];
        tb_buf_puts(&b, "  ");
        tb_buf_puts(&b, s->name);
        if (s->rank) {
            tb_buf_puts(&b, "(");
            for (uint32_t r = 0; r < s->rank; r++) {
                if (r) tb_buf_puts(&b, ", ");
                tb_buf_puts(&b, p->slots[st->idx[r]].name);
            }
            tb_buf_puts(&b, ")");
        }
        tb_buf_puts(&b, " = ");
        tb_print_node(p, st->rhs, &b);
        tb_buf_puts(&b, "\n");
    }
    tb_buf_puts(&b, "end\n");
    if (b.oom) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    *out_text = b.b;
    *out_len = b.n;
    return ZXV_OK;
}

int32_t tb_program_text(const tb_program *p, char *buf, size_t cap, size_t *out_len) {
    char *t;
    size_t n;
    int32_t st;
    if (!p || !out_len) return ZXV_E_ARG;
    st = tb_print_program(p, &t, &n);
    if (st != ZXV_OK) return st;
    st = tb_out_text(t, n, buf, cap, out_len);
    free(t);
    return st;
}

int32_t tb_program_digest(const tb_program *p, uint8_t out[32]) {
    char *t;
    size_t n;
    int32_t st;
    if (!p || !out) return ZXV_E_ARG;
    st = tb_print_program(p, &t, &n);
    if (st != ZXV_OK) return st;
    st = zxv_sha3_256((const uint8_t *) t, n, out);
    free(t);
    return st;
}
