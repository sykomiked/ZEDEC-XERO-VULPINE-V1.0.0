/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_sutra.c — the Sutra runtime for app manifests (zxapp.h A2), over the
 * cobol-sutra pillar (sdk/pillars/cobol-sutra/include/zsutra.h). Hosted
 * only: the pillar sits on libzxv.
 *
 * The pillar reads records: a Sutra layout is a COBOL record, and its
 * picture arithmetic stores and reads exact decimals in PIC fields,
 * refusing what does not fit (ZS_E_FIT). It has no procedure division, so
 * this file adds the smallest one that runs on it. A Sutra entry is a
 * Sutra record, then "procedure", then COBOL-style statements:
 *
 *     record fee
 *       amount: 9(9)          # cents
 *       rate: 9(4)            # basis points
 *       fee: 9(7)
 *     procedure
 *       compute fee = amount * rate / 10000
 *       if fee < 50
 *         compute fee = 50
 *       end-if
 *       answer fee
 *
 *   SU1 THE RECORD is parsed by the pillar (zs_sutra_parse) and is the
 *       entry's working storage. Each row starts from zs_record_init
 *       (zero in every number) and each argument is stored in the numeric
 *       field it names with zs_put_decimal, in the field's own units: an
 *       argument for s9(7)v99 is in hundredths. A value the picture cannot
 *       hold is the pillar's refusal, passed on.
 *   SU2 THE PROCEDURE is new here, one statement per line, case-insensitive,
 *       an optional "." at the end, "#" to the end of a line a comment:
 *         compute <field> = <expression>
 *         if <condition> [then] ... [else ...] end-if    (nested to 16)
 *         answer <expression> | answer <condition>
 *       Expressions are + - * / ( ), unary minus, numeric fields and
 *       literals (42, 2.5), evaluated exactly with libzxv's zxv_rat_*.
 *       Conditions compare two expressions (= <> < > <= >=) and join with
 *       not, and, or (that precedence; no parentheses around conditions).
 *       As in COBOL a binary minus needs spaces: "a-1" is a field name.
 *   SU3 COMPUTE stores through the pillar: the exact value is truncated
 *       toward zero to the field's decimals (COBOL's COMPUTE without
 *       ROUNDED) and written with zs_put_decimal, so a value with more
 *       digits than the picture, or a negative one in an unsigned field,
 *       is a size error: refused, never stored short.
 *   SU4 ANSWER ends the row. A field answers its stored value as an
 *       integer in the field's units (SU1); any other expression must be a
 *       whole number; a condition answers TRUE or FALSE (den = 0, as
 *       KRON's truths). A row that ends without answering is refused.
 *   SU5 Arguments are integer vectors of one length n, or n = 1 for a
 *       value every row shares; row k answers out[k]. A name that is not a
 *       numeric field, an overflow, a division by zero: ZXAPP_E_RUNTIME,
 *       with the pillar's or this file's words. Integers only, never a
 *       float. */
#include <stdlib.h>
#include <string.h>

#include "zsutra.h"
#include "zxapp_sutra.h"

#define SU_NODES 512u
#define SU_STMTS 256u
#define SU_NAMES 64u
#define SU_DEPTH 16u /* nested ifs */
#define SU_EXPR  48u /* expression nesting */

enum {
    N_NUM,
    N_FIELD,
    N_NEG,
    N_ADD,
    N_SUB,
    N_MUL,
    N_DIV, /* numbers */
    N_EQ,
    N_NE,
    N_LT,
    N_LE,
    N_GT,
    N_GE,
    N_AND,
    N_OR,
    N_NOT /* conditions */
};
enum { S_COMPUTE, S_IF, S_JUMP, S_ANSWER };

typedef struct {
    uint8_t op;
    uint16_t a, b, field;
    int64_t num, den;
} su_node;

typedef struct {
    uint8_t kind;
    uint16_t field, expr, jump; /* S_IF: where to go when false; S_JUMP: where */
    uint32_t line;
} su_stmt;

typedef struct {
    zs_layout *lay;
    uint64_t size;
    su_node node[SU_NODES];
    uint32_t nn;
    su_stmt st[SU_STMTS];
    uint32_t ns;
    char field[SU_NAMES][ZS_MAX_NAME + 1];
    uint32_t nf;
} su_prog;

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

/* m1 then m2, clamped to cap. */
static void say2(char *err, uint32_t cap, const char *m1, const char *m2)
{
    if (!err || !cap) return;
    uint32_t k = 0;
    for (const char *p = m1; p && *p && k + 1u < cap; p++) err[k++] = *p;
    for (const char *p = m2; p && *p && k + 1u < cap; p++) err[k++] = *p;
    err[k] = 0;
}

/* The pillar's own words for its last refusal, after a prefix. */
static void say_pillar(char *err, uint32_t cap, const char *pre)
{
    char m[160];
    size_t len = 0;
    if (zs_last_error(m, sizeof m, &len) != ZXV_OK || !m[0])
        say2(err, cap, pre, "refused");
    else
        say2(err, cap, pre, m);
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int word_is(const char *s, uint32_t n, const char *w)
{
    uint32_t i = 0;
    for (; i < n && w[i]; i++)
        if (lower((unsigned char) s[i]) != w[i]) return 0;
    return i == n && !w[i];
}

/* ---- the procedure's lexer: one line at a time ------------------------------ */

enum { T_END, T_NAME, T_NUM, T_OP };

typedef struct {
    const char *p, *end; /* the rest of this line */
    uint32_t line;
    int t;
    const char *s; /* token text */
    uint32_t n;
    su_prog *g;
    char *err;
    uint32_t cap;
    int bad;
    uint32_t depth;
} lex_t;

static int is_name(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

static void fail(lex_t *L, const char *m)
{
    if (L->bad) return;
    L->bad = 1;
    char head[32];
    uint32_t k = 0, v = L->line, dig[10], nd = 0;
    const char *pre = "line ";
    while (*pre) head[k++] = *pre++;
    do dig[nd++] = v % 10u;
    while ((v /= 10u) && nd < 10u);
    while (nd) head[k++] = (char) ('0' + dig[--nd]);
    head[k++] = ':';
    head[k++] = ' ';
    head[k] = 0;
    say2(L->err, L->cap, head, m);
}

static void next(lex_t *L)
{
    while (L->p < L->end && (*L->p == ' ' || *L->p == '\t' || *L->p == '\r')) L->p++;
    L->s = L->p;
    L->n = 0;
    if (L->p >= L->end) {
        L->t = T_END;
        return;
    }
    char c = *L->p;
    if (c >= '0' && c <= '9') {
        while (L->p < L->end && *L->p >= '0' && *L->p <= '9') L->p++;
        if (L->p + 1 < L->end && *L->p == '.' && L->p[1] >= '0' && L->p[1] <= '9') {
            L->p++;
            while (L->p < L->end && *L->p >= '0' && *L->p <= '9') L->p++;
        }
        L->t = T_NUM;
    } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        while (L->p < L->end && is_name((unsigned char) *L->p)) L->p++;
        L->t = T_NAME;
    } else {
        L->p++;
        if ((c == '<' || c == '>') && L->p < L->end && (*L->p == '=' || (c == '<' && *L->p == '>')))
            L->p++;
        L->t = T_OP;
    }
    L->n = (uint32_t) (L->p - L->s);
}

static int is_op(const lex_t *L, const char *o)
{
    return L->t == T_OP && L->n == strlen(o) && memcmp(L->s, o, L->n) == 0;
}

static int is_kw(const lex_t *L, const char *w)
{
    return L->t == T_NAME && word_is(L->s, L->n, w);
}

static uint16_t add_node(lex_t *L, uint8_t op, uint16_t a, uint16_t b)
{
    su_prog *g = L->g;
    if (g->nn >= SU_NODES) {
        fail(L, "the procedure is too long");
        return 0;
    }
    su_node *x = &g->node[g->nn];
    memset(x, 0, sizeof *x);
    x->op = op;
    x->a = a;
    x->b = b;
    return (uint16_t) g->nn++;
}

/* A numeric field of the record, by name; its slot in g->field. */
static int field_of(lex_t *L, const char *s, uint32_t n, uint16_t *slot)
{
    su_prog *g = L->g;
    char name[ZS_MAX_NAME + 1];
    if (n > ZS_MAX_NAME) {
        fail(L, "a name longer than 30 characters");
        return 0;
    }
    memcpy(name, s, n);
    name[n] = 0;
    uint32_t idx = 0;
    zs_field f;
    if (zs_find(g->lay, name, &idx, 0) != ZXV_OK || zs_field_at(g->lay, idx, &f) != ZXV_OK) {
        char m[64];
        say2(m, sizeof m, name, " is not a field of the record");
        fail(L, m);
        return 0;
    }
    if (f.kind != ZS_K_NUM || f.dims) {
        char m[64];
        say2(m, sizeof m, name, " is not a numeric field (no subscripts)");
        fail(L, m);
        return 0;
    }
    for (uint32_t i = 0; i < g->nf; i++)
        if (strcmp(g->field[i], f.name) == 0) {
            *slot = (uint16_t) i;
            return 1;
        }
    if (g->nf >= SU_NAMES) {
        fail(L, "too many fields used");
        return 0;
    }
    memcpy(g->field[g->nf], f.name, sizeof f.name);
    *slot = (uint16_t) g->nf++;
    return 1;
}

static int reserved(const lex_t *L)
{
    static const char *const kw[] = {"compute", "if",  "then", "else", "end-if",
                                     "answer",  "and", "or",   "not"};
    for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++)
        if (is_kw(L, kw[i])) return 1;
    return 0;
}

/* An exact literal: digits [. digits], at most 18 digits in all. */
static uint16_t number(lex_t *L)
{
    int64_t v = 0, den = 1;
    uint32_t digits = 0, frac = 0;
    for (uint32_t i = 0; i < L->n; i++) {
        if (L->s[i] == '.') {
            frac = 1;
            continue;
        }
        if (++digits > 18u) {
            fail(L, "a literal with more than 18 digits");
            return 0;
        }
        v = v * 10 + (L->s[i] - '0');
        if (frac) den *= 10;
    }
    uint16_t k = add_node(L, N_NUM, 0, 0);
    if (L->bad) return 0;
    int64_t rn, rd;
    zxv_rat_normalize(v, den, &rn, &rd);
    L->g->node[k].num = rn;
    L->g->node[k].den = rd;
    next(L);
    return k;
}

static uint16_t expr(lex_t *L);

static uint16_t primary(lex_t *L)
{
    if (L->bad) return 0;
    if (++L->depth > SU_EXPR) {
        fail(L, "an expression nested too deeply");
        return 0;
    }
    uint16_t k = 0;
    if (is_op(L, "-") || is_op(L, "+")) {
        int neg = is_op(L, "-");
        next(L);
        uint16_t x = primary(L);
        k = neg ? add_node(L, N_NEG, x, 0) : x;
    } else if (is_op(L, "(")) {
        next(L);
        k = expr(L);
        if (!is_op(L, ")"))
            fail(L, "a ')' is missing");
        else
            next(L);
    } else if (L->t == T_NUM) {
        k = number(L);
    } else if (L->t == T_NAME && !reserved(L)) {
        uint16_t slot = 0;
        if (field_of(L, L->s, L->n, &slot)) {
            k = add_node(L, N_FIELD, 0, 0);
            if (!L->bad) L->g->node[k].field = slot;
        }
        next(L);
    } else {
        fail(L, "a number or a field is expected");
    }
    L->depth--;
    return k;
}

static uint16_t term(lex_t *L)
{
    uint16_t l = primary(L);
    while (!L->bad && (is_op(L, "*") || is_op(L, "/"))) {
        uint8_t op = is_op(L, "*") ? N_MUL : N_DIV;
        next(L);
        uint16_t r = primary(L);
        l = add_node(L, op, l, r);
    }
    return l;
}

static uint16_t expr(lex_t *L)
{
    uint16_t l = term(L);
    while (!L->bad && (is_op(L, "+") || is_op(L, "-"))) {
        uint8_t op = is_op(L, "+") ? N_ADD : N_SUB;
        next(L);
        uint16_t r = term(L);
        l = add_node(L, op, l, r);
    }
    return l;
}

static int is_cond(const su_prog *g, uint16_t k)
{
    return g->node[k].op >= N_EQ;
}

static uint16_t cond_not(lex_t *L, int allow_num)
{
    if (L->bad) return 0;
    if (is_kw(L, "not")) {
        if (++L->depth > SU_EXPR) {
            fail(L, "a condition nested too deeply");
            return 0;
        }
        next(L);
        uint16_t x = cond_not(L, 0);
        L->depth--;
        return add_node(L, N_NOT, x, 0);
    }
    uint16_t l = expr(L);
    static const char *const rel[] = {"=", "<>", "<", "<=", ">", ">="};
    static const uint8_t op[] = {N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE};
    for (size_t i = 0; i < 6u; i++)
        if (is_op(L, rel[i])) {
            next(L);
            uint16_t r = expr(L);
            return add_node(L, op[i], l, r);
        }
    if (!allow_num) fail(L, "a condition compares two expressions (= <> < > <= >=)");
    return l;
}

static uint16_t cond_and(lex_t *L, int allow_num)
{
    uint16_t l = cond_not(L, allow_num);
    while (!L->bad && is_kw(L, "and")) {
        if (!is_cond(L->g, l)) fail(L, "and joins conditions");
        next(L);
        uint16_t r = cond_not(L, 0);
        l = add_node(L, N_AND, l, r);
    }
    return l;
}

static uint16_t cond_or(lex_t *L, int allow_num)
{
    uint16_t l = cond_and(L, allow_num);
    while (!L->bad && is_kw(L, "or")) {
        if (!is_cond(L->g, l)) fail(L, "or joins conditions");
        next(L);
        uint16_t r = cond_and(L, 0);
        l = add_node(L, N_OR, l, r);
    }
    return l;
}

static su_stmt *add_stmt(lex_t *L, uint8_t kind)
{
    su_prog *g = L->g;
    if (g->ns >= SU_STMTS) {
        fail(L, "the procedure has too many statements");
        return 0;
    }
    su_stmt *s = &g->st[g->ns++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->line = L->line;
    return s;
}

/* Compile the procedure text [p, end), whose first line is `line`. */
static int compile(su_prog *g, const char *p, const char *end, uint32_t line, char *err,
                   uint32_t cap)
{
    lex_t L;
    memset(&L, 0, sizeof L);
    L.g = g;
    L.err = err;
    L.cap = cap;
    uint16_t open[SU_DEPTH]; /* the S_IF of each open block            */
    uint16_t jump[SU_DEPTH]; /* its S_JUMP past the else, or UINT16_MAX */
    uint32_t depth = 0;
    for (L.line = line; p < end && !L.bad; L.line++) {
        const char *e = memchr(p, '\n', (size_t) (end - p));
        if (!e) e = end;
        const char *q = e;
        for (const char *h = p; h < e; h++)
            if (*h == '#') {
                q = h;
                break;
            }
        while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r')) q--;
        if (q > p && q[-1] == '.') q--; /* COBOL's period */
        L.p = p;
        L.end = q;
        p = e < end ? e + 1 : end;
        next(&L);
        if (L.t == T_END) continue;
        if (is_kw(&L, "compute")) {
            next(&L);
            uint16_t slot = 0;
            if (L.t != T_NAME || reserved(&L) || !field_of(&L, L.s, L.n, &slot)) {
                fail(&L, "compute names a numeric field");
                break;
            }
            next(&L);
            if (!is_op(&L, "=")) {
                fail(&L, "compute <field> = <expression>");
                break;
            }
            next(&L);
            uint16_t x = expr(&L);
            su_stmt *s = add_stmt(&L, S_COMPUTE);
            if (s) {
                s->field = slot;
                s->expr = x;
            }
        } else if (is_kw(&L, "if")) {
            next(&L);
            uint16_t x = cond_or(&L, 0);
            if (is_kw(&L, "then")) next(&L);
            if (depth >= SU_DEPTH) fail(&L, "ifs nested deeper than 16");
            su_stmt *s = add_stmt(&L, S_IF);
            if (s && !L.bad) {
                s->expr = x;
                open[depth] = (uint16_t) (g->ns - 1u);
                jump[depth++] = UINT16_MAX;
            }
        } else if (is_kw(&L, "else")) {
            next(&L);
            if (!depth || jump[depth - 1u] != UINT16_MAX) {
                fail(&L, "an else with no open if");
                break;
            }
            su_stmt *s = add_stmt(&L, S_JUMP);
            if (s) {
                jump[depth - 1u] = (uint16_t) (g->ns - 1u);
                g->st[open[depth - 1u]].jump = (uint16_t) g->ns;
            }
        } else if (is_kw(&L, "end-if")) {
            next(&L);
            if (!depth) {
                fail(&L, "an end-if with no open if");
                break;
            }
            depth--;
            if (jump[depth] == UINT16_MAX)
                g->st[open[depth]].jump = (uint16_t) g->ns;
            else
                g->st[jump[depth]].jump = (uint16_t) g->ns;
        } else if (is_kw(&L, "answer")) {
            next(&L);
            uint16_t x = cond_or(&L, 1);
            su_stmt *s = add_stmt(&L, S_ANSWER);
            if (s) s->expr = x;
        } else {
            fail(&L, "a statement is compute, if, else, end-if or answer");
            break;
        }
        if (!L.bad && L.t != T_END) fail(&L, "unexpected text at the end of the statement");
    }
    if (!L.bad && depth) {
        L.line = g->st[open[depth - 1u]].line;
        fail(&L, "an if with no end-if");
    }
    if (!L.bad && !g->ns) say(err, cap, "the procedure is empty");
    return L.bad || !g->ns ? ZXAPP_E_RUNTIME : ZXAPP_OK;
}

static int s_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    /* SU1/SU2: the record is everything before a line that is "procedure". */
    const char *p = src, *end = src + n, *proc = 0, *rec_end = end;
    uint32_t line = 1, proc_line = 0;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t) (end - p));
        if (!e) e = end;
        const char *a = p, *b = e;
        while (a < b && (*a == ' ' || *a == '\t')) a++;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '.')) b--;
        if (word_is(a, (uint32_t) (b - a), "procedure")) {
            rec_end = p;
            proc = e < end ? e + 1 : end;
            proc_line = line + 1u;
            break;
        }
        p = e < end ? e + 1 : end;
        line++;
    }
    if (!proc) {
        say(err, cap, "a Sutra entry is a record, then \"procedure\" and its statements");
        return ZXAPP_E_RUNTIME;
    }
    su_prog *g = calloc(1, sizeof *g);
    if (!g) {
        say(err, cap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    zs_diag d;
    memset(&d, 0, sizeof d);
    if (zs_sutra_parse(src, (size_t) (rec_end - src), &g->lay, &d) != ZXV_OK) {
        say(err, cap, d.msg[0] ? d.msg : "the Sutra record did not parse");
        free(g);
        return ZXAPP_E_RUNTIME;
    }
    zs_layout_size(g->lay, &g->size);
    int rc = compile(g, proc, end, proc_line, err, cap);
    if (rc != ZXAPP_OK) {
        zs_layout_destroy(g->lay);
        free(g);
        return rc;
    }
    *compiled = g;
    return ZXAPP_OK;
}

/* ---- running a row ----------------------------------------------------------- */

typedef struct {
    const su_prog *g;
    uint8_t *rec;
    char *err;
    uint32_t cap;
} run_t;

static int num_of(run_t *R, uint16_t k, int64_t *n, int64_t *d);

static int rat_status(run_t *R, int32_t s)
{
    if (s == ZXV_OK) return ZXAPP_OK;
    say(R->err, R->cap,
        s == ZXV_E_RANGE ? "a value does not fit int64 exactly (refused, never rounded)"
                         : "an arithmetic refusal");
    return ZXAPP_E_RUNTIME;
}

static int num_of(run_t *R, uint16_t k, int64_t *n, int64_t *d)
{
    const su_node *x = &R->g->node[k];
    int64_t an = 0, ad = 1, bn = 0, bd = 1;
    int rc;
    switch (x->op) {
    case N_NUM:
        *n = x->num;
        *d = x->den;
        return ZXAPP_OK;
    case N_FIELD:
        if (zs_get_rat(R->g->lay, R->rec, R->g->size, R->g->field[x->field], n, d) != ZXV_OK) {
            say_pillar(R->err, R->cap, "");
            return ZXAPP_E_RUNTIME;
        }
        return ZXAPP_OK;
    case N_NEG:
        if ((rc = num_of(R, x->a, &an, &ad)) != ZXAPP_OK) return rc;
        return rat_status(R, zxv_rat_sub(0, 1, an, ad, n, d));
    default:
        break;
    }
    if ((rc = num_of(R, x->a, &an, &ad)) != ZXAPP_OK) return rc;
    if ((rc = num_of(R, x->b, &bn, &bd)) != ZXAPP_OK) return rc;
    switch (x->op) {
    case N_ADD:
        return rat_status(R, zxv_rat_add(an, ad, bn, bd, n, d));
    case N_SUB:
        return rat_status(R, zxv_rat_sub(an, ad, bn, bd, n, d));
    case N_MUL:
        return rat_status(R, zxv_rat_mul(an, ad, bn, bd, n, d));
    default:
        if (bn == 0) {
            say(R->err, R->cap, "division by zero");
            return ZXAPP_E_RUNTIME;
        }
        return rat_status(R, zxv_rat_div(an, ad, bn, bd, n, d));
    }
}

static int truth_of(run_t *R, uint16_t k, int *t)
{
    const su_node *x = &R->g->node[k];
    int a = 0, b = 0, rc;
    switch (x->op) {
    case N_NOT:
        if ((rc = truth_of(R, x->a, &a)) != ZXAPP_OK) return rc;
        *t = !a;
        return ZXAPP_OK;
    case N_AND:
    case N_OR:
        if ((rc = truth_of(R, x->a, &a)) != ZXAPP_OK) return rc;
        if ((rc = truth_of(R, x->b, &b)) != ZXAPP_OK) return rc;
        *t = x->op == N_AND ? a && b : a || b;
        return ZXAPP_OK;
    default:
        break;
    }
    int64_t an, ad, bn, bd;
    int32_t c = 0;
    if ((rc = num_of(R, x->a, &an, &ad)) != ZXAPP_OK) return rc;
    if ((rc = num_of(R, x->b, &bn, &bd)) != ZXAPP_OK) return rc;
    if ((rc = rat_status(R, zxv_rat_cmp(an, ad, bn, bd, &c))) != ZXAPP_OK) return rc;
    switch (x->op) {
    case N_EQ:
        *t = c == 0;
        break;
    case N_NE:
        *t = c != 0;
        break;
    case N_LT:
        *t = c < 0;
        break;
    case N_LE:
        *t = c <= 0;
        break;
    case N_GT:
        *t = c > 0;
        break;
    default:
        *t = c >= 0;
        break;
    }
    return ZXAPP_OK;
}

static const zs_field *field_info(const su_prog *g, const char *name, zs_field *f)
{
    uint32_t idx = 0;
    if (zs_find(g->lay, name, &idx, 0) != ZXV_OK || zs_field_at(g->lay, idx, f) != ZXV_OK) return 0;
    return f->kind == ZS_K_NUM && !f->dims ? f : 0;
}

/* SU3: truncate toward zero to the field's decimals, store through the pillar. */
static int store(run_t *R, uint16_t slot, int64_t n, int64_t d)
{
    zs_field f;
    const char *name = R->g->field[slot];
    if (!field_info(R->g, name, &f)) {
        say2(R->err, R->cap, name, " is not a numeric field");
        return ZXAPP_E_RUNTIME;
    }
    int64_t pow = 1;
    for (uint32_t i = 0; i < f.scale; i++) pow *= 10;
    int64_t mn, md;
    int rc = rat_status(R, zxv_rat_mul(n, d, pow, 1, &mn, &md));
    if (rc != ZXAPP_OK) return rc;
    if (zs_put_decimal(R->g->lay, R->rec, R->g->size, name, mn / md, f.scale) != ZXV_OK) {
        say_pillar(R->err, R->cap, "size error: ");
        return ZXAPP_E_RUNTIME;
    }
    return ZXAPP_OK;
}

static int run_row(run_t *R, zxapp_num_t *ans)
{
    const su_prog *g = R->g;
    for (uint32_t pc = 0; pc < g->ns;) {
        const su_stmt *s = &g->st[pc];
        int rc, t = 0;
        int64_t n, d;
        switch (s->kind) {
        case S_COMPUTE:
            if ((rc = num_of(R, s->expr, &n, &d)) != ZXAPP_OK) return rc;
            if ((rc = store(R, s->field, n, d)) != ZXAPP_OK) return rc;
            pc++;
            break;
        case S_IF:
            if ((rc = truth_of(R, s->expr, &t)) != ZXAPP_OK) return rc;
            pc = t ? pc + 1u : s->jump;
            break;
        case S_JUMP:
            pc = s->jump;
            break;
        default: /* S_ANSWER */
            if (is_cond(g, s->expr)) {
                if ((rc = truth_of(R, s->expr, &t)) != ZXAPP_OK) return rc;
                *ans = (zxapp_num_t){t ? ZXAPP_TRUE : ZXAPP_FALSE, 0};
                return ZXAPP_OK;
            }
            if (g->node[s->expr].op == N_FIELD) { /* SU4: in the field's units */
                uint32_t scale = 0;
                if (zs_get_decimal(g->lay, R->rec, g->size, g->field[g->node[s->expr].field], &n,
                                   &scale) != ZXV_OK) {
                    say_pillar(R->err, R->cap, "");
                    return ZXAPP_E_RUNTIME;
                }
                *ans = (zxapp_num_t){n, 1};
                return ZXAPP_OK;
            }
            if ((rc = num_of(R, s->expr, &n, &d)) != ZXAPP_OK) return rc;
            if (d != 1) {
                say(R->err, R->cap, "the answer is not a whole number: compute it into a field");
                return ZXAPP_E_RUNTIME;
            }
            *ans = (zxapp_num_t){n, 1};
            return ZXAPP_OK;
        }
    }
    say(R->err, R->cap, "the procedure ended without an answer");
    return ZXAPP_E_RUNTIME;
}

static int s_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    const su_prog *g = compiled;
    uint32_t rows = 1;
    uint32_t scale[ZXAPP_INPUTS * 4u];
    if (nargs > sizeof scale / sizeof scale[0]) {
        say(err, errcap, "too many arguments");
        return ZXAPP_E_RUNTIME;
    }
    for (uint32_t i = 0; i < nargs; i++) {
        zs_field f;
        if (!args[i].n || !args[i].name || !field_info(g, args[i].name, &f)) {
            say2(err, errcap, "an argument that is not a numeric field of the record: ",
                 args[i].name ? args[i].name : "?");
            return ZXAPP_E_RUNTIME;
        }
        scale[i] = f.scale;
        if (args[i].n == 1u) continue;
        if (rows != 1u && args[i].n != rows) {
            say(err, errcap, "Sutra arguments of two lengths");
            return ZXAPP_E_RUNTIME;
        }
        rows = args[i].n;
    }
    *n_out = rows;
    if (rows > cap) return ZXAPP_E_BUFFER;
    uint8_t *rec = malloc(g->size ? (size_t) g->size : 1u);
    if (!rec) {
        say(err, errcap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    run_t R = {g, rec, err, errcap};
    int rc = ZXAPP_OK;
    for (uint32_t k = 0; k < rows && rc == ZXAPP_OK; k++) {
        zs_record_init(g->lay, rec, g->size);
        for (uint32_t i = 0; i < nargs && rc == ZXAPP_OK; i++) {
            int64_t x = args[i].v[args[i].n == 1u ? 0 : k];
            if (zs_put_decimal(g->lay, rec, g->size, args[i].name, x, scale[i]) != ZXV_OK) {
                say_pillar(err, errcap, "an argument the record cannot hold: ");
                rc = ZXAPP_E_RUNTIME;
            }
        }
        if (rc == ZXAPP_OK) rc = run_row(&R, &out[k]);
    }
    free(rec);
    return rc;
}

static void s_unload(void *ctx, void *compiled)
{
    (void) ctx;
    su_prog *g = compiled;
    if (!g) return;
    zs_layout_destroy(g->lay);
    free(g);
}

int zxapp_sutra_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_SUTRA, 0, s_load, s_call, s_unload};
    return zxapp_host_register(h, &rt);
}
