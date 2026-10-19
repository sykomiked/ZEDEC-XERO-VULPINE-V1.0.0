/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qeq.c — exact circuit equivalence checking on top of qsim, plus a strict
 * OpenQASM 2.0 subset parser and writer. See qeq.h for the model and the
 * HONEST LIMITS.
 *
 * WHAT IT DOES. For each basis column x it simulates the circuits exactly
 * (qsim) and compares amplitudes as ring elements over sqrt(2)^k. Two
 * amplitudes with different k are compared by multiplying the one with the
 * smaller k by sqrt(2) (= z^(N/8) - z^(3N/8)) until the k agree, with every
 * int64 operation overflow-checked.
 *
 * HONEST LIMITS. A classical checker: exponential cost, no speedup. Any
 * overflow gives NEITHER, never a guessed verdict. Integer only, no
 * allocation, no libc.
 */
#include "qeq.h"

/* ---------------------------------------------------------------- */
/* ring helpers (local copies; qsim keeps its own static)           */
/* ---------------------------------------------------------------- */

static void qeq_e_zero(qs_elem_t *e)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++) e->c[i] = 0;
}

static void qeq_e_copy(qs_elem_t *d, const qs_elem_t *s)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++) d->c[i] = s->c[i];
}

static int qeq_e_is_zero(const qs_elem_t *e)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++)
        if (e->c[i] != 0) return 0;
    return 1;
}

static int qeq_e_same(const qs_elem_t *a, const qs_elem_t *b)
{
    for (uint32_t i = 0; i < QS_MAX_D; i++)
        if (a->c[i] != b->c[i]) return 0;
    return 1;
}

/* r = x * z^e (0 <= e < 2D); r must not alias x. */
static int qeq_e_mulz(qs_elem_t *r, const qs_elem_t *x, uint32_t D, uint32_t e)
{
    qeq_e_zero(r);
    for (uint32_t j = 0; j < D; j++) {
        int32_t i = (int32_t) j - (int32_t) e;
        int neg = 0;
        while (i < 0) {
            i += (int32_t) D;
            neg = !neg;
        }
        if (!neg)
            r->c[j] = x->c[i];
        else if (__builtin_sub_overflow((int64_t) 0, x->c[i], &r->c[j]))
            return QS_ERR_OVERFLOW;
    }
    return QS_OK;
}

/* x *= sqrt2 = z^s - z^(3s), s = D/4. */
static int qeq_e_mul_sqrt2(qs_elem_t *x, uint32_t D)
{
    qs_elem_t a, b;
    int rc = qeq_e_mulz(&a, x, D, D / 4u);
    if (!rc) rc = qeq_e_mulz(&b, x, D, 3u * D / 4u);
    if (rc) return rc;
    for (uint32_t j = 0; j < D; j++)
        if (__builtin_sub_overflow(a.c[j], b.c[j], &x->c[j])) return QS_ERR_OVERFLOW;
    return QS_OK;
}

/* *eq = (a / sqrt2^ka == b / sqrt2^kb). */
static int qeq_amp_eq(const qs_elem_t *a, uint32_t ka, const qs_elem_t *b, uint32_t kb, uint32_t D,
                      int *eq)
{
    if (ka == kb) {
        *eq = qeq_e_same(a, b);
        return QS_OK;
    }
    const qs_elem_t *lo = a, *hi = b;
    uint32_t d = kb - ka;
    if (ka > kb) {
        lo = b;
        hi = a;
        d = ka - kb;
    }
    qs_elem_t t;
    qeq_e_copy(&t, lo);
    for (uint32_t i = 0; i < d; i++) {
        int rc = qeq_e_mul_sqrt2(&t, D);
        if (rc) return rc;
    }
    *eq = qeq_e_same(&t, hi);
    return QS_OK;
}

/* ---------------------------------------------------------------- */
/* gate validation                                                  */
/* ---------------------------------------------------------------- */

static int qeq_is_phase(uint32_t kind)
{
    return kind == QS_G_PHASE || kind == QS_G_CPHASE;
}

static int qeq_needs_c0(uint32_t kind)
{
    return kind == QS_G_CNOT || kind == QS_G_CZ || kind == QS_G_SWAP || kind == QS_G_CCX ||
           kind == QS_G_CPHASE;
}

static int qeq_valid_m(uint32_t m)
{
    return m == 1u || m == 2u || m == 4u || m == 8u || m == 16u;
}

/* 0 if the gate is malformed on n qubits; else the smallest N it needs. */
static uint32_t qeq_gate_N(const qs_gate_t *g, uint32_t n)
{
    uint32_t kind = g->kind;
    if (kind >= QS_G_COUNT || g->t >= n) return 0;
    if (qeq_needs_c0(kind) && (g->c0 >= n || g->c0 == g->t)) return 0;
    if (kind == QS_G_CCX && (g->c1 >= n || g->c1 == g->t || g->c1 == g->c0)) return 0;
    if (qeq_is_phase(kind)) {
        if (!qeq_valid_m(g->m)) return 0;
        if (g->m == 16u) return 16u;
    }
    return 8u;
}

int qeq_circuit_N(const qs_circuit_t *c, uint32_t n, uint32_t *N, uint32_t *bad_gate)
{
    if (!c) return QEQ_ERR_NULL;
    uint32_t need = 8u;
    if (c->count > QS_MAX_GATES || n == 0 || n > QS_MAX_QUBITS) {
        if (bad_gate) *bad_gate = c->count;
        return QEQ_ERR_ARG;
    }
    for (uint32_t i = 0; i < c->count; i++) {
        uint32_t g = qeq_gate_N(&c->g[i], n);
        if (g == 0) {
            if (bad_gate) *bad_gate = i;
            return QEQ_ERR_ARG;
        }
        if (g > need) need = g;
    }
    if (N) *N = need;
    return QEQ_OK;
}

int qeq_opts_default(qeq_opts_t *o)
{
    if (!o) return QEQ_ERR_NULL;
    o->mode = QEQ_STRICT;
    o->method = QEQ_DIRECT;
    o->N = 0;
    o->max_gate_apps = 0;
    return QEQ_OK;
}

/* ---------------------------------------------------------------- */
/* the checker                                                      */
/* ---------------------------------------------------------------- */

static void qeq_result_clear(qeq_result_t *r)
{
    r->verdict = QEQ_NEITHER;
    r->why = QEQ_WHY_NONE;
    r->qs_rc = 0;
    r->bad_circuit = 0;
    r->bad_gate = 0;
    r->N = 0;
    r->phase_known = 0;
    r->phase_exp = 0;
    r->global_phase_only = 0;
    r->columns_done = 0;
    r->gate_apps = 0;
    r->cex_in = 0;
    r->cex_out = 0;
    qeq_e_zero(&r->cex_a);
    qeq_e_zero(&r->cex_b);
    r->cex_ka = 0;
    r->cex_kb = 0;
}

static int qeq_neither(qeq_result_t *r, uint32_t why, int qs_rc)
{
    r->verdict = QEQ_NEITHER;
    r->why = why;
    r->qs_rc = qs_rc;
    r->phase_known = 0;
    r->global_phase_only = 0;
    return QEQ_OK;
}

static int qeq_run_fail(qeq_result_t *r, int rc)
{
    return qeq_neither(
        r, rc == QS_ERR_OVERFLOW || rc == QS_ERR_BROKEN ? QEQ_WHY_OVERFLOW : QEQ_WHY_GATE, rc);
}

static int qeq_false(qeq_result_t *r, uint32_t in, uint32_t out, const qs_elem_t *a, uint32_t ka,
                     const qs_elem_t *b, uint32_t kb)
{
    r->verdict = QEQ_FALSE;
    r->why = QEQ_WHY_NONE;
    r->global_phase_only = 0;
    r->cex_in = in;
    r->cex_out = out;
    qeq_e_copy(&r->cex_a, a);
    r->cex_ka = ka;
    qeq_e_copy(&r->cex_b, b);
    r->cex_kb = kb;
    return QEQ_OK;
}

/* If x / sqrt2^k is exactly a power z^j of z, *j = that power; else *j = 2D
 * (no such power). A root of unity has |x|^2 = 1, so a reduced
 * representation of z^j has k = 0 and a single coefficient +-1. */
static uint32_t qeq_root_power(const qs_elem_t *x, uint32_t k, uint32_t D)
{
    if (k != 0) return 2u * D;
    uint32_t pos = 2u * D;
    for (uint32_t i = 0; i < D; i++) {
        if (x->c[i] == 0) continue;
        if (pos != 2u * D) return 2u * D;
        if (x->c[i] == 1)
            pos = i;
        else if (x->c[i] == -1)
            pos = i + D;
        else
            return 2u * D;
    }
    return pos;
}

/* DIRECT: compare columns of s1 (U1) and z^j * s2 (U2). On the first column
 * (x == 0) in phase mode, j is found first. Returns QEQ_OK; sets *done = 1
 * if a verdict (FALSE or NEITHER) was reached. */
static int qeq_cmp_direct(qeq_work_t *w, uint32_t x, uint32_t mode, qeq_result_t *r, int *done)
{
    const qs_state_t *a = &w->s1, *b = &w->s2;
    uint32_t D = a->D, dim = 1u << a->n;
    *done = 0;
    if (x == 0) {
        if (mode == QEQ_STRICT) {
            r->phase_exp = 0;
        } else {
            uint32_t y = 0;
            while (y < dim && qeq_e_is_zero(&a->amp[y]) && qeq_e_is_zero(&b->amp[y])) y++;
            if (y == dim) { /* impossible for a unitary column; defensive */
                *done = 1;
                return qeq_neither(r, QEQ_WHY_GATE, 0);
            }
            uint32_t j = 0;
            for (; j < 2u * D; j++) {
                qs_elem_t t;
                int eq = 0, rc = qeq_e_mulz(&t, &b->amp[y], D, j);
                if (!rc) rc = qeq_amp_eq(&a->amp[y], a->k, &t, b->k, D, &eq);
                if (rc) {
                    *done = 1;
                    return qeq_neither(r, QEQ_WHY_OVERFLOW, rc);
                }
                if (eq) break;
            }
            if (j == 2u * D) {
                *done = 1;
                return qeq_false(r, 0, y, &a->amp[y], a->k, &b->amp[y], b->k);
            }
            r->phase_exp = j;
        }
        r->phase_known = 1;
    }
    for (uint32_t y = 0; y < dim; y++) {
        qs_elem_t t;
        int eq = 0, rc = qeq_e_mulz(&t, &b->amp[y], D, r->phase_exp);
        if (!rc) rc = qeq_amp_eq(&a->amp[y], a->k, &t, b->k, D, &eq);
        if (rc) {
            *done = 1;
            return qeq_neither(r, QEQ_WHY_OVERFLOW, rc);
        }
        if (!eq) {
            *done = 1;
            return qeq_false(r, x, y, &a->amp[y], a->k, &b->amp[y], b->k);
        }
    }
    return QEQ_OK;
}

/* MITER: s1 holds (C2^dag C1)|x>; it must equal z^j |x>. */
static int qeq_cmp_miter(qeq_work_t *w, uint32_t x, uint32_t mode, qeq_result_t *r, int *done)
{
    const qs_state_t *a = &w->s1;
    uint32_t D = a->D, dim = 1u << a->n;
    *done = 0;
    if (x == 0) {
        uint32_t j = 0;
        if (mode == QEQ_UP_TO_PHASE) {
            j = qeq_root_power(&a->amp[0], a->k, D);
            if (j == 2u * D) j = 0; /* not a root of unity: the check below fails */
        }
        r->phase_exp = j;
        r->phase_known = 1;
    }
    qs_elem_t one, expect;
    qeq_e_zero(&one);
    one.c[0] = 1;
    int rc = qeq_e_mulz(&expect, &one, D, r->phase_exp);
    if (rc) { /* unreachable: z^j of 1 cannot overflow */
        *done = 1;
        return qeq_neither(r, QEQ_WHY_OVERFLOW, rc);
    }
    qs_elem_t zero;
    qeq_e_zero(&zero);
    for (uint32_t y = 0; y < dim; y++) {
        if (y == x) continue;
        if (!qeq_e_is_zero(&a->amp[y])) {
            *done = 1;
            return qeq_false(r, x, y, &a->amp[y], a->k, &zero, 0);
        }
    }
    if (a->k != 0 || !qeq_e_same(&a->amp[x], &expect)) {
        *done = 1;
        return qeq_false(r, x, x, &a->amp[x], a->k, &expect, 0);
    }
    return QEQ_OK;
}

int qeq_check(const qs_circuit_t *c1, const qs_circuit_t *c2, uint32_t n, const qeq_opts_t *o,
              qeq_work_t *w, qeq_result_t *r)
{
    if (!c1 || !c2 || !w || !r) return QEQ_ERR_NULL;
    qeq_opts_t def;
    if (!o) {
        qeq_opts_default(&def);
        o = &def;
    }
    if (o->mode > QEQ_UP_TO_PHASE || o->method > QEQ_MITER) return QEQ_ERR_ARG;
    if (o->N != 0 && o->N != 8u && o->N != 16u) return QEQ_ERR_ARG;
    qeq_result_clear(r);

    if (n == 0 || n > QS_MAX_QUBITS) return qeq_neither(r, QEQ_WHY_QUBITS, QS_ERR_TOO_MANY);

    uint32_t N1 = 0, N2 = 0, bad = 0;
    if (qeq_circuit_N(c1, n, &N1, &bad) != QEQ_OK) {
        r->bad_circuit = 1;
        r->bad_gate = bad;
        return qeq_neither(r, QEQ_WHY_GATE, QS_ERR_ARG);
    }
    if (qeq_circuit_N(c2, n, &N2, &bad) != QEQ_OK) {
        r->bad_circuit = 2;
        r->bad_gate = bad;
        return qeq_neither(r, QEQ_WHY_GATE, QS_ERR_ARG);
    }
    uint32_t N = N1 > N2 ? N1 : N2;
    if (o->N != 0) {
        if (o->N < N) { /* a pi/8 phase cannot be represented at N = 8 */
            for (uint32_t ci = 0; ci < 2u; ci++) {
                const qs_circuit_t *c = ci == 0 ? c1 : c2;
                for (uint32_t i = 0; i < c->count; i++) {
                    if (qeq_is_phase(c->g[i].kind) && o->N % c->g[i].m != 0) {
                        r->bad_circuit = ci + 1u;
                        r->bad_gate = i;
                        return qeq_neither(r, QEQ_WHY_GATE, QS_ERR_PHASE);
                    }
                }
            }
        }
        N = o->N;
    }
    r->N = N;

    if (o->method == QEQ_MITER) {
        int rc = qs_circuit_adjoint(c2, &w->adj);
        if (rc) {
            r->bad_circuit = 2;
            return qeq_neither(r, QEQ_WHY_GATE, rc);
        }
    }

    uint64_t cost = (uint64_t) c1->count + (uint64_t) c2->count;
    uint32_t dim = 1u << n;
    for (uint32_t x = 0; x < dim; x++) {
        if (o->max_gate_apps != 0 && r->gate_apps + cost > o->max_gate_apps)
            return qeq_neither(r, QEQ_WHY_BUDGET, 0);
        int rc = qs_init_basis(&w->s1, n, N, x);
        if (rc) return qeq_run_fail(r, rc);
        uint32_t did = 0;
        rc = qs_run_circuit(&w->s1, c1, &did);
        r->gate_apps += did;
        if (rc) return qeq_run_fail(r, rc);
        const qs_circuit_t *second = o->method == QEQ_MITER ? &w->adj : c2;
        qs_state_t *st2 = o->method == QEQ_MITER ? &w->s1 : &w->s2;
        if (o->method == QEQ_DIRECT) {
            rc = qs_init_basis(&w->s2, n, N, x);
            if (rc) return qeq_run_fail(r, rc);
        }
        rc = qs_run_circuit(st2, second, &did);
        r->gate_apps += did;
        if (rc) return qeq_run_fail(r, rc);
        int done = 0;
        rc = o->method == QEQ_MITER ? qeq_cmp_miter(w, x, o->mode, r, &done)
                                    : qeq_cmp_direct(w, x, o->mode, r, &done);
        if (rc || done) return rc;
        r->columns_done = x + 1u;
    }
    r->verdict = QEQ_TRUE;
    r->global_phase_only = r->phase_exp != 0;
    return QEQ_OK;
}

/* ---------------------------------------------------------------- */
/* OpenQASM 2.0 subset: lexer                                       */
/* ---------------------------------------------------------------- */

enum { QEQ_T_EOF = 0, QEQ_T_ID, QEQ_T_INT, QEQ_T_REAL, QEQ_T_STR, QEQ_T_SYM, QEQ_T_BAD };

#define QEQ_INT_MAX 1000000

typedef struct {
    const char *s;
    uint32_t len, pos, line, col;
    /* current token */
    uint32_t type, tpos, tlen, tline, tcol;
    int64_t ival; /* QEQ_T_INT value, or -1 if above QEQ_INT_MAX */
    char sym;
} qeq_lex_t;

static int qeq_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int qeq_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void qeq_adv(qeq_lex_t *L)
{
    if (L->s[L->pos] == '\n') {
        L->line++;
        L->col = 1;
    } else {
        L->col++;
    }
    L->pos++;
}

static void qeq_next(qeq_lex_t *L)
{
    for (;;) {
        while (L->pos < L->len && (L->s[L->pos] == ' ' || L->s[L->pos] == '\t' ||
                                   L->s[L->pos] == '\r' || L->s[L->pos] == '\n'))
            qeq_adv(L);
        if (L->pos + 1u < L->len && L->s[L->pos] == '/' && L->s[L->pos + 1u] == '/') {
            while (L->pos < L->len && L->s[L->pos] != '\n') qeq_adv(L);
            continue;
        }
        break;
    }
    L->tpos = L->pos;
    L->tline = L->line;
    L->tcol = L->col;
    L->tlen = 0;
    L->ival = 0;
    L->sym = 0;
    if (L->pos >= L->len) {
        L->type = QEQ_T_EOF;
        return;
    }
    char c = L->s[L->pos];
    if (c == '\0') {
        L->type = QEQ_T_BAD;
        return;
    }
    if (qeq_is_alpha(c)) {
        while (L->pos < L->len && (qeq_is_alpha(L->s[L->pos]) || qeq_is_digit(L->s[L->pos])))
            qeq_adv(L);
        L->type = QEQ_T_ID;
    } else if (qeq_is_digit(c) || c == '.') {
        int real = 0;
        int32_t v = 0;
        while (L->pos < L->len && (qeq_is_digit(L->s[L->pos]) || L->s[L->pos] == '.' ||
                                   L->s[L->pos] == 'e' || L->s[L->pos] == 'E')) {
            char d = L->s[L->pos];
            if (!qeq_is_digit(d)) {
                real = 1;
            } else if (v >= 0) {
                v = v * 10 + (d - '0');
                if (v > QEQ_INT_MAX) v = -1;
            }
            qeq_adv(L);
        }
        L->type = real ? QEQ_T_REAL : QEQ_T_INT;
        L->ival = v;
    } else if (c == '"') {
        qeq_adv(L);
        while (L->pos < L->len && L->s[L->pos] != '"' && L->s[L->pos] != '\n') qeq_adv(L);
        if (L->pos >= L->len || L->s[L->pos] != '"') {
            L->type = QEQ_T_BAD;
            return;
        }
        qeq_adv(L);
        L->type = QEQ_T_STR;
    } else if (c == ';' || c == ',' || c == '[' || c == ']' || c == '(' || c == ')' || c == '*' ||
               c == '/' || c == '-' || c == '+' || c == '{' || c == '}' || c == '>' || c == '=') {
        qeq_adv(L);
        L->type = QEQ_T_SYM;
        L->sym = c;
    } else {
        L->type = QEQ_T_BAD;
        return;
    }
    L->tlen = L->pos - L->tpos;
}

/* Token text equals the NUL-terminated word w. */
static int qeq_tok_is(const qeq_lex_t *L, const char *w)
{
    uint32_t i = 0;
    for (; w[i] != '\0'; i++)
        if (i >= L->tlen || L->s[L->tpos + i] != w[i]) return 0;
    return i == L->tlen;
}

static int qeq_is_sym(const qeq_lex_t *L, char c)
{
    return L->type == QEQ_T_SYM && L->sym == c;
}

/* ---------------------------------------------------------------- */
/* parser                                                           */
/* ---------------------------------------------------------------- */

typedef struct {
    const char *name;
    uint8_t kind;
    uint8_t arity;
    uint8_t angle;
} qeq_gdef_t;

static const qeq_gdef_t qeq_gates[] = {
    {"id", QS_G_I, 1, 0},       {"h", QS_G_H, 1, 0},       {"x", QS_G_X, 1, 0},
    {"y", QS_G_Y, 1, 0},        {"z", QS_G_Z, 1, 0},       {"s", QS_G_S, 1, 0},
    {"sdg", QS_G_SDG, 1, 0},    {"t", QS_G_T, 1, 0},       {"tdg", QS_G_TDG, 1, 0},
    {"cx", QS_G_CNOT, 2, 0},    {"cz", QS_G_CZ, 2, 0},     {"swap", QS_G_SWAP, 2, 0},
    {"ccx", QS_G_CCX, 3, 0},    {"u1", QS_G_PHASE, 1, 1},  {"p", QS_G_PHASE, 1, 1},
    {"cu1", QS_G_CPHASE, 2, 1}, {"cp", QS_G_CPHASE, 2, 1},
};

#define QEQ_NGATES (sizeof(qeq_gates) / sizeof(qeq_gates[0]))

static const char *const qeq_unsupported[] = {"measure", "reset", "if", "gate",
                                              "opaque",  "U",     "CX"};

typedef struct {
    qeq_lex_t L;
    qeq_qasm_err_t *err;
    uint32_t have_qreg;
    uint32_t n;
    uint32_t rpos, rlen; /* register name span */
} qeq_parser_t;

static int qeq_perr(qeq_parser_t *P, uint32_t code)
{
    P->err->code = code;
    P->err->line = P->L.tline;
    P->err->col = P->L.tcol;
    return QEQ_ERR_ARG;
}

static int qeq_expect_sym(qeq_parser_t *P, char c)
{
    if (!qeq_is_sym(&P->L, c)) return qeq_perr(P, QEQ_Q_SYNTAX);
    qeq_next(&P->L);
    return QEQ_OK;
}

static int qeq_same_reg(const qeq_parser_t *P)
{
    if (P->L.tlen != P->rlen) return 0;
    for (uint32_t i = 0; i < P->rlen; i++)
        if (P->L.s[P->L.tpos + i] != P->L.s[P->rpos + i]) return 0;
    return 1;
}

/* qarg := REG '[' INT ']' */
static int qeq_qarg(qeq_parser_t *P, uint32_t *q)
{
    if (P->L.type != QEQ_T_ID) return qeq_perr(P, QEQ_Q_SYNTAX);
    if (!P->have_qreg || !qeq_same_reg(P)) return qeq_perr(P, QEQ_Q_REG);
    qeq_next(&P->L);
    if (!qeq_is_sym(&P->L, '[')) return qeq_perr(P, QEQ_Q_UNSUPPORTED); /* broadcast */
    qeq_next(&P->L);
    if (P->L.type != QEQ_T_INT) return qeq_perr(P, QEQ_Q_SYNTAX);
    if (P->L.ival < 0 || (uint64_t) P->L.ival >= P->n) return qeq_perr(P, QEQ_Q_INDEX);
    *q = (uint32_t) P->L.ival;
    qeq_next(&P->L);
    return qeq_expect_sym(P, ']');
}

static uint32_t qeq_gcd(uint32_t a, uint32_t b)
{
    while (b != 0) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* angle := '0' | [+-] [INT '*'] 'pi' ['/' INT]; value a*pi/b, phase k/m with
 * e^(i a pi/b) = e^(2 pi i a/(2b)). */
static int qeq_angle(qeq_parser_t *P, int32_t *k, uint32_t *m)
{
    qeq_lex_t *L = &P->L;
    int64_t sign = 1, a = 1, b = 1;
    if (L->type == QEQ_T_INT && L->ival == 0) {
        qeq_next(L);
        if (!qeq_is_sym(L, ')')) return qeq_perr(P, QEQ_Q_ANGLE);
        *k = 0;
        *m = 1;
        return QEQ_OK;
    }
    if (qeq_is_sym(L, '-') || qeq_is_sym(L, '+')) {
        if (L->sym == '-') sign = -1;
        qeq_next(L);
    }
    if (L->type == QEQ_T_INT) {
        if (L->ival <= 0) return qeq_perr(P, QEQ_Q_ANGLE);
        a = L->ival;
        qeq_next(L);
        if (!qeq_is_sym(L, '*')) return qeq_perr(P, QEQ_Q_ANGLE);
        qeq_next(L);
    }
    if (L->type != QEQ_T_ID || !qeq_tok_is(L, "pi")) return qeq_perr(P, QEQ_Q_ANGLE);
    qeq_next(L);
    if (qeq_is_sym(L, '/')) {
        qeq_next(L);
        if (L->type != QEQ_T_INT || L->ival <= 0) return qeq_perr(P, QEQ_Q_ANGLE);
        b = L->ival;
        qeq_next(L);
    }
    if (!qeq_is_sym(L, ')')) return qeq_perr(P, QEQ_Q_ANGLE);
    /* a, b <= QEQ_INT_MAX, so 32-bit arithmetic is exact (no 64-bit division
     * helpers on 32-bit targets). */
    uint32_t ua = (uint32_t) a, den = 2u * (uint32_t) b;
    uint32_t g = qeq_gcd(ua, den);
    ua /= g;
    den /= g;
    if (den != 1u && den != 2u && den != 4u && den != 8u && den != 16u)
        return qeq_perr(P, QEQ_Q_ANGLE);
    *k = (int32_t) (ua % den) * (int32_t) sign;
    *m = den;
    return QEQ_OK;
}

/* barrier args: REG or REG[INT], comma separated; ignored after checks. */
static int qeq_barrier(qeq_parser_t *P)
{
    for (;;) {
        if (P->L.type != QEQ_T_ID) return qeq_perr(P, QEQ_Q_SYNTAX);
        if (!P->have_qreg || !qeq_same_reg(P)) return qeq_perr(P, QEQ_Q_REG);
        qeq_next(&P->L);
        if (qeq_is_sym(&P->L, '[')) {
            qeq_next(&P->L);
            if (P->L.type != QEQ_T_INT) return qeq_perr(P, QEQ_Q_SYNTAX);
            if (P->L.ival < 0 || (uint64_t) P->L.ival >= P->n) return qeq_perr(P, QEQ_Q_INDEX);
            qeq_next(&P->L);
            int rc = qeq_expect_sym(P, ']');
            if (rc) return rc;
        }
        if (!qeq_is_sym(&P->L, ',')) break;
        qeq_next(&P->L);
    }
    return qeq_expect_sym(P, ';');
}

static int qeq_gate_stmt(qeq_parser_t *P, const qeq_gdef_t *gd, qs_circuit_t *out)
{
    qeq_lex_t *L = &P->L;
    int32_t k = 0;
    uint32_t m = 0;
    if (gd->angle) {
        if (!qeq_is_sym(L, '(')) return qeq_perr(P, QEQ_Q_ARITY);
        qeq_next(L);
        int rc = qeq_angle(P, &k, &m);
        if (rc) return rc;
        qeq_next(L); /* ')' checked by qeq_angle */
    } else if (qeq_is_sym(L, '(')) {
        return qeq_perr(P, QEQ_Q_ARITY);
    }
    uint32_t q[3] = {0, 0, 0}, cnt = 0;
    uint32_t gl = L->tline, gc = L->tcol;
    for (;;) {
        if (cnt == 3u) return qeq_perr(P, QEQ_Q_ARITY);
        int rc = qeq_qarg(P, &q[cnt]);
        if (rc) return rc;
        for (uint32_t i = 0; i < cnt; i++) {
            if (q[i] == q[cnt]) {
                P->err->code = QEQ_Q_DUP;
                P->err->line = gl;
                P->err->col = gc;
                return QEQ_ERR_ARG;
            }
        }
        cnt++;
        if (!qeq_is_sym(L, ',')) break;
        qeq_next(L);
    }
    if (cnt != gd->arity) {
        P->err->code = QEQ_Q_ARITY;
        P->err->line = gl;
        P->err->col = gc;
        return QEQ_ERR_ARG;
    }
    if (!qeq_is_sym(L, ';')) return qeq_perr(P, QEQ_Q_SYNTAX);
    uint32_t t = q[cnt - 1u], c0 = cnt >= 2u ? q[0] : 0, c1 = cnt == 3u ? q[1] : 0;
    int rc = qs_circuit_push(out, gd->kind, t, c0, c1, k, gd->angle ? m : 0);
    if (rc == QS_ERR_FULL) return qeq_perr(P, QEQ_Q_FULL);
    if (rc) return qeq_perr(P, QEQ_Q_SYNTAX); /* unreachable: arguments were checked */
    qeq_next(L);
    return QEQ_OK;
}

int qeq_qasm_parse(const char *src, uint32_t len, qs_circuit_t *out, uint32_t *n,
                   qeq_qasm_err_t *err)
{
    if (!src || !out || !n || !err) return QEQ_ERR_NULL;
    qeq_parser_t P;
    P.L.s = src;
    P.L.len = len;
    P.L.pos = 0;
    P.L.line = 1;
    P.L.col = 1;
    P.err = err;
    P.have_qreg = 0;
    P.n = 0;
    P.rpos = 0;
    P.rlen = 0;
    err->code = QEQ_Q_OK;
    err->line = 0;
    err->col = 0;
    out->count = 0;
    *n = 0;
    qeq_lex_t *L = &P.L;
    qeq_next(L);
    /* header: OPENQASM 2.0 ; */
    if (L->type != QEQ_T_ID || !qeq_tok_is(L, "OPENQASM")) return qeq_perr(&P, QEQ_Q_HEADER);
    qeq_next(L);
    if (L->type != QEQ_T_REAL || !qeq_tok_is(L, "2.0")) return qeq_perr(&P, QEQ_Q_HEADER);
    qeq_next(L);
    if (!qeq_is_sym(L, ';')) return qeq_perr(&P, QEQ_Q_HEADER);
    qeq_next(L);

    while (L->type != QEQ_T_EOF) {
        if (L->type != QEQ_T_ID) return qeq_perr(&P, QEQ_Q_SYNTAX);
        if (qeq_tok_is(L, "include")) {
            qeq_next(L);
            if (L->type != QEQ_T_STR) return qeq_perr(&P, QEQ_Q_SYNTAX);
            if (!qeq_tok_is(L, "\"qelib1.inc\"")) return qeq_perr(&P, QEQ_Q_UNSUPPORTED);
            qeq_next(L);
            int rc = qeq_expect_sym(&P, ';');
            if (rc) return rc;
            continue;
        }
        if (qeq_tok_is(L, "qreg") || qeq_tok_is(L, "creg")) {
            int is_q = qeq_tok_is(L, "qreg");
            if (is_q && P.have_qreg) return qeq_perr(&P, QEQ_Q_REG);
            qeq_next(L);
            if (L->type != QEQ_T_ID) return qeq_perr(&P, QEQ_Q_SYNTAX);
            uint32_t np = L->tpos, nl = L->tlen;
            qeq_next(L);
            int rc = qeq_expect_sym(&P, '[');
            if (rc) return rc;
            if (L->type != QEQ_T_INT) return qeq_perr(&P, QEQ_Q_SYNTAX);
            if (is_q && (L->ival <= 0 || L->ival > (int64_t) QS_MAX_QUBITS))
                return qeq_perr(&P, QEQ_Q_SIZE);
            int64_t size = L->ival;
            qeq_next(L);
            rc = qeq_expect_sym(&P, ']');
            if (!rc) rc = qeq_expect_sym(&P, ';');
            if (rc) return rc;
            if (is_q) {
                P.have_qreg = 1;
                P.n = (uint32_t) size;
                P.rpos = np;
                P.rlen = nl;
            }
            continue;
        }
        if (qeq_tok_is(L, "barrier")) {
            qeq_next(L);
            int rc = qeq_barrier(&P);
            if (rc) return rc;
            continue;
        }
        for (uint32_t i = 0; i < sizeof(qeq_unsupported) / sizeof(qeq_unsupported[0]); i++)
            if (qeq_tok_is(L, qeq_unsupported[i])) return qeq_perr(&P, QEQ_Q_UNSUPPORTED);
        const qeq_gdef_t *gd = 0;
        for (uint32_t i = 0; i < QEQ_NGATES; i++)
            if (qeq_tok_is(L, qeq_gates[i].name)) gd = &qeq_gates[i];
        if (!gd) return qeq_perr(&P, QEQ_Q_UNKNOWN_GATE);
        if (!P.have_qreg) return qeq_perr(&P, QEQ_Q_REG);
        qeq_next(L);
        int rc = qeq_gate_stmt(&P, gd, out);
        if (rc) return rc;
    }
    if (!P.have_qreg) return qeq_perr(&P, QEQ_Q_REG);
    *n = P.n;
    return QEQ_OK;
}

/* ---------------------------------------------------------------- */
/* writer                                                           */
/* ---------------------------------------------------------------- */

typedef struct {
    char *buf;
    uint32_t cap, len;
    int full;
} qeq_out_t;

static void qeq_put(qeq_out_t *o, const char *s)
{
    for (uint32_t i = 0; s[i] != '\0'; i++) {
        if (o->len + 1u >= o->cap) {
            o->full = 1;
            return;
        }
        o->buf[o->len++] = s[i];
    }
}

static void qeq_put_u(qeq_out_t *o, uint32_t v)
{
    char t[12];
    uint32_t i = 11;
    t[11] = '\0';
    do {
        t[--i] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    qeq_put(o, &t[i]);
}

static void qeq_put_q(qeq_out_t *o, uint32_t q)
{
    qeq_put(o, "q[");
    qeq_put_u(o, q);
    qeq_put(o, "]");
}

/* Angle of the phase e^(2 pi i k/m) as text accepted by qeq_angle. */
static void qeq_put_angle(qeq_out_t *o, int32_t k, uint32_t m)
{
    int32_t r = k % (int32_t) m; /* m <= 16 */
    if (r < 0) r += (int32_t) m;
    if (r == 0) {
        qeq_put(o, "0");
        return;
    }
    uint32_t g = qeq_gcd((uint32_t) r, m);
    uint32_t a = (uint32_t) r / g, b = m / g; /* angle = 2a/b pi */
    if (a != 1u) {
        qeq_put_u(o, a);
        qeq_put(o, "*");
    }
    qeq_put(o, "pi");
    if (b != 2u) {
        qeq_put(o, "/");
        qeq_put_u(o, b / 2u);
    }
}

int qeq_qasm_write(const qs_circuit_t *c, uint32_t n, char *buf, uint32_t cap, uint32_t *len)
{
    if (!c || !buf || !len) return QEQ_ERR_NULL;
    if (cap == 0) return QEQ_ERR_ARG;
    if (qeq_circuit_N(c, n, 0, 0) != QEQ_OK) return QEQ_ERR_ARG;
    qeq_out_t o;
    o.buf = buf;
    o.cap = cap;
    o.len = 0;
    o.full = 0;
    qeq_put(&o, "OPENQASM 2.0;\ninclude \"qelib1.inc\";\nqreg q[");
    qeq_put_u(&o, n);
    qeq_put(&o, "];\n");
    for (uint32_t i = 0; i < c->count; i++) {
        const qs_gate_t *g = &c->g[i];
        const char *name = 0;
        for (uint32_t j = 0; j < QEQ_NGATES && !name; j++)
            if (qeq_gates[j].kind == g->kind) name = qeq_gates[j].name;
        qeq_put(&o, name);
        if (qeq_is_phase(g->kind)) {
            qeq_put(&o, "(");
            qeq_put_angle(&o, g->k, g->m);
            qeq_put(&o, ")");
        }
        qeq_put(&o, " ");
        if (g->kind == QS_G_CCX) {
            qeq_put_q(&o, g->c0);
            qeq_put(&o, ",");
            qeq_put_q(&o, g->c1);
            qeq_put(&o, ",");
        } else if (qeq_needs_c0(g->kind)) {
            qeq_put_q(&o, g->c0);
            qeq_put(&o, ",");
        }
        qeq_put_q(&o, g->t);
        qeq_put(&o, ";\n");
    }
    buf[o.len] = '\0';
    *len = o.len;
    return o.full ? QEQ_ERR_FULL : QEQ_OK;
}
