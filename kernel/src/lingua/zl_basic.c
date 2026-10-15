/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_basic.c — BASIC front-end: ECMA-55 Minimal BASIC (1978, the standard
 * form of Dartmouth BASIC, 1964), integer subset, and its twin "Bede".
 *
 * Admitted: LET (or an implied LET), PRINT with ';' ',' and string
 * literals, IF rel THEN line, GOTO / GO TO, GOSUB / GO SUB, RETURN,
 * ON expr GOTO, FOR / NEXT with STEP, END, STOP, REM, INPUT, DIM (one
 * dimension), OPTION BASE 0|1, DATA / READ / RESTORE, DEF FNx(p) = expr,
 * ABS, SGN, INT, + - * / ^ and a leading sign.
 *
 * Exact subset: numbers are integers. A literal with a decimal point or an
 * exponent is refused; '/' must divide exactly and '^' must give an
 * integer, or the program stops with ZL_E_INEXACT rather than round;
 * INT(a / b) and INT(-a / b) are exact (the floor of the quotient), so the classic
 * "IF INT(I/J)*J = I" divisibility test works.
 * Refused by name: string variables, two-dimensional arrays, SQR, RND and
 * the other real-valued functions, RANDOMIZE. INPUT reads the caller's
 * input list and prints no "?" prompt.
 *
 * Bede (the twin) is the same program in one spelling: upper case, LET
 * always written, GOTO and GOSUB closed up, one space around binary
 * operators and after keywords and separators, minimal parentheses, no
 * unary plus, no "STEP 1", line numbers without leading zeros, REM text
 * trimmed, blank lines dropped. */
#include "zl_int.h"

enum { N_NUM = 1, N_VAR, N_ARR, N_NEG, N_BIN, N_FN, N_UFN };
enum { F_ABS = 1, F_SGN, F_INT };

typedef struct {
    uint8_t kind, op;
    int16_t name; /* variable: letter*11 + digit+1; array/ufn: letter */
    int32_t l, r;
    int64_t v;
} bnode_t;

#define B_POOL  1024
#define B_LINES 2048
#define B_FIX   2048
#define B_DATA  1024
#define B_DEFS  26

typedef struct {
    bnode_t pool[B_POOL];
    uint32_t npool, pool_floor; /* DEF bodies live below pool_floor */
    uint16_t lineno[B_LINES];
    int32_t lineaddr[B_LINES];
    uint32_t nlines;
    struct {
        int32_t at;
        uint32_t target, srcline;
    } fix[B_FIX];
    uint32_t nfix;
    int32_t varslot[26 * 11];
    int32_t arrbase[26], arrlen[26]; /* arrlen = number of elements */
    int base1, base_set, any_array;
    struct {
        int16_t var;
        int32_t top, exit1, exit2, step_slot;
    } fors[ZL_MAX_DEPTH];
    uint32_t nfor;
    int64_t data[B_DATA];
    uint32_t ndata;
    int32_t data_ptr_slot;
    struct {
        int32_t at;
        int kind;
    } dfix[B_FIX]; /* 0: PUSH ndata, 1: LOADX */
    uint32_t ndfix;
    struct {
        int defined;
        int16_t param;
        int32_t body, pslot;
    } defs[B_DEFS];
    int32_t tmp_slot;
    uint32_t depth;
    const char *s; /* current line */
    size_t n, i;
    zl_cc_t *c;
    zl_out_t *tw;
} bw_t;

ZL_STATIC_ASSERT(sizeof(bw_t) <= sizeof(((zl_work_t *) 0)->fe), bw_fits);

/* ------------------------------------------------------------ lexing */

static void sp(bw_t *w)
{
    while (w->i < w->n && zl_isspace((unsigned char) w->s[w->i])) w->i++;
}
static int peek(bw_t *w)
{
    sp(w);
    return w->i < w->n ? (unsigned char) w->s[w->i] : 0;
}
static int bfail(bw_t *w, int st, const char *msg)
{
    w->c->col = (uint32_t) w->i + 1;
    return zl_fail(w->c, st, msg);
}

/* Match a keyword at the cursor (case-insensitive; no spaces inside). */
static int kw(bw_t *w, const char *k)
{
    sp(w);
    size_t j = 0;
    while (k[j] && w->i + j < w->n && zl_upper((unsigned char) w->s[w->i + j]) == k[j]) j++;
    if (k[j]) return 0;
    w->i += j;
    return 1;
}

/* GO TO / GOTO and GO SUB / GOSUB */
static int kw2(bw_t *w, const char *second)
{
    size_t save = w->i;
    if (kw(w, "GO") && kw(w, second)) return 1;
    w->i = save;
    return 0;
}

static int expect(bw_t *w, int ch, const char *msg)
{
    if (peek(w) != ch) return bfail(w, ZL_E_SYNTAX, msg);
    w->i++;
    return ZL_OK;
}

static int number(bw_t *w, int64_t *out)
{
    sp(w);
    if (w->i >= w->n || !zl_isdigit((unsigned char) w->s[w->i])) {
        if (w->i < w->n && w->s[w->i] == '.')
            return bfail(w, ZL_E_UNSUPPORTED, "non-integer number (exact subset)");
        return bfail(w, ZL_E_SYNTAX, "number expected");
    }
    int64_t v = 0;
    while (w->i < w->n && zl_isdigit((unsigned char) w->s[w->i])) {
        if (__builtin_mul_overflow(v, 10, &v) || __builtin_add_overflow(v, w->s[w->i] - '0', &v))
            return bfail(w, ZL_E_UNSUPPORTED, "number too large for 64 bits");
        w->i++;
    }
    if (w->i < w->n && (w->s[w->i] == '.' || zl_upper((unsigned char) w->s[w->i]) == 'E'))
        return bfail(w, ZL_E_UNSUPPORTED, "non-integer number (exact subset)");
    *out = v;
    return ZL_OK;
}

static int lineref(bw_t *w, uint32_t *out)
{
    int64_t v;
    int s = number(w, &v);
    if (s) return s;
    if (v < 1 || v > 9999) return bfail(w, ZL_E_SYNTAX, "line number must be 1 to 9999");
    *out = (uint32_t) v;
    return ZL_OK;
}

/* A simple variable: letter [digit]. Returns the name code or -1. */
static int16_t varname(bw_t *w)
{
    sp(w);
    if (w->i >= w->n || !zl_isalpha((unsigned char) w->s[w->i])) return -1;
    int16_t code = (int16_t) ((zl_upper((unsigned char) w->s[w->i]) - 'A') * 11);
    w->i++;
    if (w->i < w->n && zl_isdigit((unsigned char) w->s[w->i])) {
        code = (int16_t) (code + 1 + (w->s[w->i] - '0'));
        w->i++;
    }
    return code;
}

static int32_t newnode(bw_t *w, int kind)
{
    if (w->npool >= B_POOL) {
        bfail(w, ZL_E_LIMIT, "expression too large");
        return -1;
    }
    bnode_t *n = &w->pool[w->npool];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->l = n->r = -1;
    return (int32_t) w->npool++;
}

/* ------------------------------------------------------------ parsing */

static int32_t expr(bw_t *w);

static int32_t primary(bw_t *w)
{
    int ch = peek(w);
    int32_t n;
    if (ch == '(') {
        w->i++;
        n = expr(w);
        if (n < 0 || expect(w, ')', "')' expected")) return -1;
        return n;
    }
    if (zl_isdigit(ch) || ch == '.') {
        int64_t v;
        if (number(w, &v)) return -1;
        if ((n = newnode(w, N_NUM)) < 0) return -1;
        w->pool[n].v = v;
        return n;
    }
    if (ch == '"') {
        bfail(w, ZL_E_SYNTAX, "a string is not a number");
        return -1;
    }
    if (!zl_isalpha(ch)) {
        bfail(w, ZL_E_SYNTAX, "expression expected");
        return -1;
    }
    static const char *const real_fns[] = {"SQR(", "RND",  "SIN(", "COS(", "TAN(",
                                           "ATN(", "EXP(", "LOG(", 0};
    for (int k = 0; real_fns[k]; k++) {
        size_t save = w->i;
        if (kw(w, real_fns[k])) {
            w->i = save;
            bfail(w, ZL_E_UNSUPPORTED, "real-valued function (exact subset)");
            return -1;
        }
    }
    static const char *const fns[] = {"ABS(", "SGN(", "INT("};
    for (int k = 0; k < 3; k++) {
        if (kw(w, fns[k])) {
            int32_t a = expr(w);
            if (a < 0 || expect(w, ')', "')' expected")) return -1;
            if ((n = newnode(w, N_FN)) < 0) return -1;
            w->pool[n].op = (uint8_t) (F_ABS + k);
            w->pool[n].l = a;
            return n;
        }
    }
    {
        size_t save = w->i;
        if (kw(w, "FN") && w->i < w->n && zl_isalpha((unsigned char) w->s[w->i])) {
            int16_t f = (int16_t) (zl_upper((unsigned char) w->s[w->i]) - 'A');
            w->i++;
            if (expect(w, '(', "'(' expected after a function name")) return -1;
            int32_t a = expr(w);
            if (a < 0 || expect(w, ')', "')' expected")) return -1;
            if (!w->defs[f].defined) {
                bfail(w, ZL_E_UNDEFINED, "function used before its DEF");
                return -1;
            }
            if ((n = newnode(w, N_UFN)) < 0) return -1;
            w->pool[n].name = f;
            w->pool[n].l = a;
            return n;
        }
        w->i = save;
    }
    int16_t v = varname(w);
    if (w->i < w->n && w->s[w->i] == '$') {
        bfail(w, ZL_E_UNSUPPORTED, "string variables are not in this subset");
        return -1;
    }
    if (peek(w) == '(') {
        if (v % 11 != 0) {
            bfail(w, ZL_E_SYNTAX, "an array name is a single letter");
            return -1;
        }
        w->i++;
        int32_t a = expr(w);
        if (a < 0) return -1;
        if (peek(w) == ',') {
            bfail(w, ZL_E_UNSUPPORTED, "two-dimensional arrays are not in this subset");
            return -1;
        }
        if (expect(w, ')', "')' expected")) return -1;
        if ((n = newnode(w, N_ARR)) < 0) return -1;
        w->pool[n].name = (int16_t) (v / 11);
        w->pool[n].l = a;
        return n;
    }
    if ((n = newnode(w, N_VAR)) < 0) return -1;
    w->pool[n].name = v;
    return n;
}

static int32_t bin(bw_t *w, int op, int32_t l, int32_t r)
{
    int32_t n;
    if (l < 0 || r < 0 || (n = newnode(w, N_BIN)) < 0) return -1;
    w->pool[n].op = (uint8_t) op;
    w->pool[n].l = l;
    w->pool[n].r = r;
    return n;
}

static int32_t factor(bw_t *w)
{
    int32_t l = primary(w);
    while (l >= 0 && peek(w) == '^') {
        w->i++;
        l = bin(w, '^', l, primary(w)); /* ECMA-55: '^' associates left */
    }
    return l;
}

static int32_t term(bw_t *w)
{
    int32_t l = factor(w);
    int ch;
    while (l >= 0 && ((ch = peek(w)) == '*' || ch == '/')) {
        w->i++;
        l = bin(w, ch, l, factor(w));
    }
    return l;
}

static int32_t expr(bw_t *w)
{
    int32_t l;
    int ch = peek(w);
    if (++w->depth > ZL_MAX_DEPTH) {
        bfail(w, ZL_E_LIMIT, "expression nested too deeply");
        return -1;
    }
    if (ch == '+' || ch == '-') {
        w->i++;
        l = term(w);
        if (l >= 0 && ch == '-') {
            int32_t n = newnode(w, N_NEG);
            if (n < 0) return -1;
            w->pool[n].l = l;
            l = n;
        }
    } else {
        l = term(w);
    }
    while (l >= 0 && ((ch = peek(w)) == '+' || ch == '-')) {
        w->i++;
        l = bin(w, ch, l, term(w));
    }
    w->depth--;
    return l;
}

/* ------------------------------------------------------------ printing */

static int prec(const bnode_t *n)
{
    if (n->kind == N_NEG) return 1;
    if (n->kind == N_BIN) return n->op == '+' || n->op == '-' ? 1 : n->op == '^' ? 3 : 2;
    return 4;
}

static void pvar(zl_out_t *o, int16_t code)
{
    zl_putc(o, (char) ('A' + code / 11));
    if (code % 11) zl_putc(o, (char) ('0' + code % 11 - 1));
}

static void pexpr(bw_t *w, int32_t id, int need)
{
    zl_out_t *o = w->tw;
    const bnode_t *n = &w->pool[id];
    int p = prec(n), paren = p < need;
    if (paren) zl_putc(o, '(');
    switch (n->kind) {
    case N_NUM:
        zl_puti(o, n->v);
        break;
    case N_VAR:
        pvar(o, n->name);
        break;
    case N_ARR:
        zl_putc(o, (char) ('A' + n->name));
        zl_putc(o, '(');
        pexpr(w, n->l, 0);
        zl_putc(o, ')');
        break;
    case N_FN:
        zl_puts(o, n->op == F_ABS ? "ABS(" : n->op == F_SGN ? "SGN(" : "INT(");
        pexpr(w, n->l, 0);
        zl_putc(o, ')');
        break;
    case N_UFN:
        zl_puts(o, "FN");
        zl_putc(o, (char) ('A' + n->name));
        zl_putc(o, '(');
        pexpr(w, n->l, 0);
        zl_putc(o, ')');
        break;
    case N_NEG:
        zl_putc(o, '-');
        pexpr(w, n->l, 2);
        break;
    case N_BIN:
        pexpr(w, n->l, p);
        zl_putc(o, ' ');
        zl_putc(o, (char) n->op);
        zl_putc(o, ' ');
        pexpr(w, n->r, p + 1);
        break;
    }
    if (paren) zl_putc(o, ')');
}

static void tw_expr(bw_t *w, int32_t id)
{
    if (w->tw) pexpr(w, id, 0);
}
static void tw_s(bw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}
static void tw_i(bw_t *w, int64_t v)
{
    if (w->tw) zl_puti(w->tw, v);
}
static void tw_var(bw_t *w, int16_t code)
{
    if (w->tw) pvar(w->tw, code);
}

/* ------------------------------------------------------------ lowering */

static int32_t slot_var(bw_t *w, int16_t code)
{
    if (w->arrlen[code / 11] && code % 11 == 0) {
        bfail(w, ZL_E_SYNTAX, "a letter cannot name both an array and a variable");
        return -1;
    }
    if (w->varslot[code] < 0) w->varslot[code] = zl_slots(w->c, 1);
    return w->varslot[code];
}

static int arr_ensure(bw_t *w, int16_t letter, int64_t ub)
{
    if (w->arrlen[letter]) return ZL_OK;
    if (w->varslot[letter * 11] >= 0)
        return bfail(w, ZL_E_SYNTAX, "a letter cannot name both an array and a variable");
    int64_t len = ub + 1 - w->base1;
    if (len < 1 || len > ZL_MAX_SLOTS) return bfail(w, ZL_E_LIMIT, "array bound out of range");
    int32_t b = zl_slots(w->c, (uint32_t) len);
    if (b < 0) return ZL_E_LIMIT;
    w->arrbase[letter] = b;
    w->arrlen[letter] = (int32_t) len;
    w->any_array = 1;
    return ZL_OK;
}

static int lower(bw_t *w, int32_t id, int16_t param, int32_t pslot, uint32_t depth)
{
    zl_cc_t *c = w->c;
    const bnode_t *n = &w->pool[id];
    int s;
    if (depth > ZL_MAX_DEPTH) return bfail(w, ZL_E_LIMIT, "expression nested too deeply");
    switch (n->kind) {
    case N_NUM:
        zl_emit(c, ZL_PUSH, n->v, 0);
        break;
    case N_VAR:
        if (param >= 0 && n->name == param) {
            zl_emit(c, ZL_LOAD, pslot, 0);
            break;
        }
        {
            int32_t sl = slot_var(w, n->name);
            if (sl < 0) return ZL_E_SYNTAX;
            zl_emit(c, ZL_LOAD, sl, 0);
        }
        break;
    case N_ARR:
        if ((s = arr_ensure(w, n->name, 10))) return s;
        if ((s = lower(w, n->l, param, pslot, depth + 1))) return s;
        if (w->base1) {
            zl_emit(c, ZL_PUSH, 1, 0);
            zl_emit(c, ZL_SUB, 0, 0);
        }
        zl_emit(c, ZL_LOADX, w->arrbase[n->name], w->arrlen[n->name]);
        break;
    case N_NEG:
        if ((s = lower(w, n->l, param, pslot, depth + 1))) return s;
        zl_emit(c, ZL_NEG, 0, 0);
        break;
    case N_FN:
        if (n->op == F_INT) {
            /* INT(a / b) and INT(-(a / b)) are floors of exact quotients */
            const bnode_t *q = &w->pool[n->l];
            int neg = q->kind == N_NEG;
            if (neg) q = &w->pool[q->l];
            if (q->kind == N_BIN && q->op == '/') {
                if ((s = lower(w, q->l, param, pslot, depth + 1))) return s;
                if (neg) zl_emit(c, ZL_NEG, 0, 0);
                if ((s = lower(w, q->r, param, pslot, depth + 1))) return s;
                zl_emit(c, ZL_DIVF, 0, 0);
                break;
            }
        }
        if ((s = lower(w, n->l, param, pslot, depth + 1))) return s;
        if (n->op == F_ABS)
            zl_emit(c, ZL_ABS, 0, 0);
        else if (n->op == F_SGN)
            zl_emit(c, ZL_SGN, 0, 0);
        break; /* INT of an integer is itself */
    case N_UFN: {
        if ((s = lower(w, n->l, param, pslot, depth + 1))) return s;
        zl_emit(c, ZL_STORE, w->defs[n->name].pslot, 0);
        if ((s = lower(w, w->defs[n->name].body, w->defs[n->name].param, w->defs[n->name].pslot,
                       depth + 1)))
            return s;
        break;
    }
    case N_BIN:
        if ((s = lower(w, n->l, param, pslot, depth + 1))) return s;
        if ((s = lower(w, n->r, param, pslot, depth + 1))) return s;
        switch (n->op) {
        case '+':
            zl_emit(c, ZL_ADD, 0, 0);
            break;
        case '-':
            zl_emit(c, ZL_SUB, 0, 0);
            break;
        case '*':
            zl_emit(c, ZL_MUL, 0, 0);
            break;
        case '/':
            zl_emit(c, ZL_DIVX, 0, 0);
            break;
        default:
            zl_emit(c, ZL_POW, 1, 0);
            break;
        }
        break;
    }
    return c->failed ? c->d->status : ZL_OK;
}

static int lower_expr(bw_t *w, int32_t id)
{
    return lower(w, id, -1, -1, 0);
}

static int add_fix(bw_t *w, int32_t at, uint32_t target)
{
    if (at < 0) return ZL_E_LIMIT;
    if (w->nfix >= B_FIX) return bfail(w, ZL_E_LIMIT, "too many jumps");
    w->fix[w->nfix].at = at;
    w->fix[w->nfix].target = target;
    w->fix[w->nfix].srcline = w->c->line;
    w->nfix++;
    return ZL_OK;
}

static int add_dfix(bw_t *w, int32_t at, int kind)
{
    if (at < 0) return ZL_E_LIMIT;
    if (w->ndfix >= B_FIX) return bfail(w, ZL_E_LIMIT, "too many READ statements");
    w->dfix[w->ndfix].at = at;
    w->dfix[w->ndfix].kind = kind;
    w->ndfix++;
    return ZL_OK;
}

/* An assignment target: variable or array element. Lowered as
 * [index] value STORE / STOREX by the caller via store_target. */
typedef struct {
    int16_t var;
    int16_t arr;
    int32_t idx;
} target_t;

static int parse_target(bw_t *w, target_t *t)
{
    t->var = -1;
    t->arr = -1;
    t->idx = -1;
    int16_t v = varname(w);
    if (v < 0) return bfail(w, ZL_E_SYNTAX, "variable expected");
    if (w->i < w->n && w->s[w->i] == '$')
        return bfail(w, ZL_E_UNSUPPORTED, "string variables are not in this subset");
    if (peek(w) == '(') {
        if (v % 11 != 0) return bfail(w, ZL_E_SYNTAX, "an array name is a single letter");
        w->i++;
        t->idx = expr(w);
        if (t->idx < 0) return ZL_E_SYNTAX;
        if (peek(w) == ',')
            return bfail(w, ZL_E_UNSUPPORTED, "two-dimensional arrays are not in this subset");
        if (expect(w, ')', "')' expected")) return ZL_E_SYNTAX;
        t->arr = (int16_t) (v / 11);
    } else {
        t->var = v;
    }
    return ZL_OK;
}

static void tw_target(bw_t *w, const target_t *t)
{
    if (!w->tw) return;
    if (t->arr >= 0) {
        zl_putc(w->tw, (char) ('A' + t->arr));
        zl_putc(w->tw, '(');
        pexpr(w, t->idx, 0);
        zl_putc(w->tw, ')');
    } else {
        pvar(w->tw, t->var);
    }
}

/* Emit the index part (before the value) of a store. */
static int store_pre(bw_t *w, const target_t *t)
{
    int s;
    if (t->arr < 0) return ZL_OK;
    if ((s = arr_ensure(w, t->arr, 10))) return s;
    if ((s = lower_expr(w, t->idx))) return s;
    if (w->base1) {
        zl_emit(w->c, ZL_PUSH, 1, 0);
        zl_emit(w->c, ZL_SUB, 0, 0);
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int store_post(bw_t *w, const target_t *t)
{
    if (t->arr >= 0) {
        zl_emit(w->c, ZL_STOREX, w->arrbase[t->arr], w->arrlen[t->arr]);
    } else {
        int32_t sl = slot_var(w, t->var);
        if (sl < 0) return ZL_E_SYNTAX;
        zl_emit(w->c, ZL_STORE, sl, 0);
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

/* ------------------------------------------------------------ statements */

static int st_let(bw_t *w)
{
    target_t t;
    int s;
    if ((s = parse_target(w, &t))) return s;
    if ((s = expect(w, '=', "'=' expected"))) return s;
    int32_t e = expr(w);
    if (e < 0) return w->c->d->status;
    tw_s(w, "LET ");
    tw_target(w, &t);
    tw_s(w, " = ");
    tw_expr(w, e);
    if ((s = store_pre(w, &t))) return s;
    if ((s = lower_expr(w, e))) return s;
    return store_post(w, &t);
}

static int st_print(bw_t *w)
{
    int prev = 0; /* 0: keyword, 1: item, 2: separator */
    tw_s(w, "PRINT");
    for (;;) {
        int ch = peek(w);
        if (ch == 0) break;
        if (ch == ';' || ch == ',') {
            w->i++;
            if (prev != 1) tw_s(w, " ");
            tw_s(w, ch == ';' ? ";" : ",");
            if (ch == ',') zl_emit(w->c, ZL_TAB, 15, 0);
            prev = 2;
            continue;
        }
        if (prev == 1) return bfail(w, ZL_E_SYNTAX, "';' or ',' expected between PRINT items");
        tw_s(w, " ");
        if (ch == '"') {
            size_t a = ++w->i;
            while (w->i < w->n && w->s[w->i] != '"') w->i++;
            if (w->i >= w->n) return bfail(w, ZL_E_SYNTAX, "unterminated string");
            int32_t k = zl_addstr(w->c, w->s + a, w->i - a);
            if (k < 0) return ZL_E_LIMIT;
            if (w->tw) {
                zl_putc(w->tw, '"');
                zl_put(w->tw, w->s + a, w->i - a);
                zl_putc(w->tw, '"');
            }
            w->i++;
            zl_emit(w->c, ZL_OUTS, k, 0);
        } else {
            int32_t e = expr(w);
            if (e < 0) return w->c->d->status;
            tw_expr(w, e);
            int s = lower_expr(w, e);
            if (s) return s;
            zl_emit(w->c, ZL_OUTI, ZL_FMT_SIGNSPACE | ZL_FMT_TRAIL, 0);
        }
        prev = 1;
    }
    if (prev != 2) zl_emit(w->c, ZL_NL, 0, 0);
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int st_if(bw_t *w)
{
    int32_t l = expr(w), r;
    int op;
    if (l < 0) return w->c->d->status;
    sp(w);
    if (kw(w, "<>"))
        op = ZL_NE;
    else if (kw(w, "<="))
        op = ZL_LE;
    else if (kw(w, ">="))
        op = ZL_GE;
    else if (kw(w, "<"))
        op = ZL_LT;
    else if (kw(w, ">"))
        op = ZL_GT;
    else if (kw(w, "="))
        op = ZL_EQ;
    else
        return bfail(w, ZL_E_SYNTAX, "relation expected (= <> < > <= >=)");
    r = expr(w);
    if (r < 0) return w->c->d->status;
    if (!kw(w, "THEN")) return bfail(w, ZL_E_SYNTAX, "THEN expected");
    uint32_t target;
    int s = lineref(w, &target);
    if (s) return s;
    tw_s(w, "IF ");
    tw_expr(w, l);
    tw_s(w, op == ZL_NE   ? " <> "
            : op == ZL_LE ? " <= "
            : op == ZL_GE ? " >= "
            : op == ZL_LT ? " < "
            : op == ZL_GT ? " > "
                          : " = ");
    tw_expr(w, r);
    tw_s(w, " THEN ");
    tw_i(w, target);
    if ((s = lower_expr(w, l)) || (s = lower_expr(w, r))) return s;
    zl_emit(w->c, op, 0, 0);
    return add_fix(w, zl_emit(w->c, ZL_JNZ, 0, 0), target);
}

static int st_for(bw_t *w)
{
    zl_cc_t *c = w->c;
    int16_t v = varname(w);
    int s;
    if (v < 0) return bfail(w, ZL_E_SYNTAX, "loop variable expected");
    if (peek(w) == '(') return bfail(w, ZL_E_SYNTAX, "the loop variable must be a simple variable");
    if ((s = expect(w, '=', "'=' expected"))) return s;
    int32_t e1 = expr(w), e2, e3 = -1;
    if (e1 < 0) return c->d->status;
    if (!kw(w, "TO")) return bfail(w, ZL_E_SYNTAX, "TO expected");
    if ((e2 = expr(w)) < 0) return c->d->status;
    if (kw(w, "STEP") && (e3 = expr(w)) < 0) return c->d->status;
    if (w->nfor >= ZL_MAX_DEPTH) return bfail(w, ZL_E_LIMIT, "FOR loops nested too deeply");
    tw_s(w, "FOR ");
    tw_var(w, v);
    tw_s(w, " = ");
    tw_expr(w, e1);
    tw_s(w, " TO ");
    tw_expr(w, e2);
    if (e3 >= 0 && !(w->pool[e3].kind == N_NUM && w->pool[e3].v == 1)) {
        tw_s(w, " STEP ");
        tw_expr(w, e3);
    }
    int32_t vs = slot_var(w, v), lim = zl_slots(c, 1), step = zl_slots(c, 1);
    if (vs < 0 || lim < 0 || step < 0) return c->d->status;
    if ((s = lower_expr(w, e1))) return s;
    zl_emit(c, ZL_STORE, vs, 0);
    if ((s = lower_expr(w, e2))) return s;
    zl_emit(c, ZL_STORE, lim, 0);
    if (e3 >= 0) {
        if ((s = lower_expr(w, e3))) return s;
    } else
        zl_emit(c, ZL_PUSH, 1, 0);
    zl_emit(c, ZL_STORE, step, 0);
    int32_t top = zl_emit(c, ZL_LOAD, step, 0);
    zl_emit(c, ZL_PUSH, 0, 0);
    zl_emit(c, ZL_LT, 0, 0);
    int32_t jneg = zl_emit(c, ZL_JNZ, 0, 0);
    zl_emit(c, ZL_LOAD, vs, 0);
    zl_emit(c, ZL_LOAD, lim, 0);
    zl_emit(c, ZL_GT, 0, 0);
    int32_t x1 = zl_emit(c, ZL_JNZ, 0, 0);
    int32_t jbody = zl_emit(c, ZL_JMP, 0, 0);
    zl_patch(c, jneg, (int64_t) c->p->ncode);
    zl_emit(c, ZL_LOAD, vs, 0);
    zl_emit(c, ZL_LOAD, lim, 0);
    zl_emit(c, ZL_LT, 0, 0);
    int32_t x2 = zl_emit(c, ZL_JNZ, 0, 0);
    zl_patch(c, jbody, (int64_t) c->p->ncode);
    w->fors[w->nfor].var = v;
    w->fors[w->nfor].top = top;
    w->fors[w->nfor].exit1 = x1;
    w->fors[w->nfor].exit2 = x2;
    w->fors[w->nfor].step_slot = step;
    w->nfor++;
    return c->failed ? c->d->status : ZL_OK;
}

static int st_next(bw_t *w)
{
    zl_cc_t *c = w->c;
    int16_t v = varname(w);
    if (v < 0) return bfail(w, ZL_E_SYNTAX, "NEXT needs its loop variable");
    if (w->nfor == 0 || w->fors[w->nfor - 1].var != v)
        return bfail(w, ZL_E_SYNTAX, "NEXT does not match the innermost FOR");
    w->nfor--;
    tw_s(w, "NEXT ");
    tw_var(w, v);
    int32_t vs = w->varslot[v];
    zl_emit(c, ZL_LOAD, vs, 0);
    zl_emit(c, ZL_LOAD, w->fors[w->nfor].step_slot, 0);
    zl_emit(c, ZL_ADD, 0, 0);
    zl_emit(c, ZL_STORE, vs, 0);
    zl_emit(c, ZL_JMP, w->fors[w->nfor].top, 0);
    zl_patch(c, w->fors[w->nfor].exit1, (int64_t) c->p->ncode);
    zl_patch(c, w->fors[w->nfor].exit2, (int64_t) c->p->ncode);
    return c->failed ? c->d->status : ZL_OK;
}

static int st_on(bw_t *w)
{
    zl_cc_t *c = w->c;
    int32_t e = expr(w);
    int s;
    if (e < 0) return c->d->status;
    if (!kw(w, "GOTO") && !kw2(w, "TO"))
        return bfail(w, ZL_E_SYNTAX, "GOTO expected after ON expression");
    tw_s(w, "ON ");
    tw_expr(w, e);
    tw_s(w, " GOTO");
    if (w->tmp_slot < 0 && (w->tmp_slot = zl_slots(c, 1)) < 0) return c->d->status;
    if ((s = lower_expr(w, e))) return s;
    zl_emit(c, ZL_STORE, w->tmp_slot, 0);
    for (int64_t k = 1;; k++) {
        uint32_t t;
        if ((s = lineref(w, &t))) return s;
        tw_s(w, k == 1 ? " " : ", ");
        tw_i(w, t);
        zl_emit(c, ZL_LOAD, w->tmp_slot, 0);
        zl_emit(c, ZL_PUSH, k, 0);
        zl_emit(c, ZL_EQ, 0, 0);
        if ((s = add_fix(w, zl_emit(c, ZL_JNZ, 0, 0), t))) return s;
        if (peek(w) != ',') break;
        w->i++;
    }
    zl_emit(c, ZL_TRAP, ZL_E_DOMAIN, 0);
    return c->failed ? c->d->status : ZL_OK;
}

static int st_input_read(bw_t *w, int read)
{
    zl_cc_t *c = w->c;
    int s;
    tw_s(w, read ? "READ" : "INPUT");
    for (int first = 1;; first = 0) {
        target_t t;
        if ((s = parse_target(w, &t))) return s;
        tw_s(w, first ? " " : ", ");
        tw_target(w, &t);
        if ((s = store_pre(w, &t))) return s;
        if (read) {
            if (w->data_ptr_slot < 0 && (w->data_ptr_slot = zl_slots(c, 1)) < 0)
                return c->d->status;
            zl_emit(c, ZL_LOAD, w->data_ptr_slot, 0);
            if ((s = add_dfix(w, zl_emit(c, ZL_PUSH, 0, 0), 0))) return s;
            zl_emit(c, ZL_LT, 0, 0);
            int32_t ok = zl_emit(c, ZL_JNZ, 0, 0);
            zl_emit(c, ZL_TRAP, ZL_E_INPUT, 0);
            zl_patch(c, ok, (int64_t) c->p->ncode);
            zl_emit(c, ZL_LOAD, w->data_ptr_slot, 0);
            if ((s = add_dfix(w, zl_emit(c, ZL_LOADX, 0, 1), 1))) return s;
            zl_emit(c, ZL_LOAD, w->data_ptr_slot, 0);
            zl_emit(c, ZL_PUSH, 1, 0);
            zl_emit(c, ZL_ADD, 0, 0);
            zl_emit(c, ZL_STORE, w->data_ptr_slot, 0);
        } else {
            zl_emit(c, ZL_IN, 0, 0);
        }
        if ((s = store_post(w, &t))) return s;
        if (peek(w) != ',') break;
        w->i++;
    }
    return c->failed ? c->d->status : ZL_OK;
}

static int st_data(bw_t *w)
{
    tw_s(w, "DATA");
    for (int first = 1;; first = 0) {
        int ch = peek(w), neg = 0;
        int64_t v;
        if (ch == '"') return bfail(w, ZL_E_UNSUPPORTED, "string DATA is not in this subset");
        if (ch == '+' || ch == '-') {
            neg = ch == '-';
            w->i++;
        }
        int s = number(w, &v);
        if (s) return s;
        if (neg) v = -v;
        if (w->ndata >= B_DATA) return bfail(w, ZL_E_LIMIT, "too many DATA values");
        w->data[w->ndata++] = v;
        tw_s(w, first ? " " : ", ");
        tw_i(w, v);
        if (peek(w) != ',') break;
        w->i++;
    }
    return ZL_OK;
}

static int st_dim(bw_t *w)
{
    tw_s(w, "DIM");
    for (int first = 1;; first = 0) {
        int16_t v = varname(w);
        int64_t ub;
        int s;
        if (v < 0 || v % 11) return bfail(w, ZL_E_SYNTAX, "array name (one letter) expected");
        if ((s = expect(w, '(', "'(' expected"))) return s;
        if ((s = number(w, &ub))) return s;
        if (peek(w) == ',')
            return bfail(w, ZL_E_UNSUPPORTED, "two-dimensional arrays are not in this subset");
        if ((s = expect(w, ')', "')' expected"))) return s;
        if (w->arrlen[v / 11])
            return bfail(w, ZL_E_SYNTAX, "array dimensioned twice or used before its DIM");
        if ((s = arr_ensure(w, (int16_t) (v / 11), ub))) return s;
        tw_s(w, first ? " " : ", ");
        tw_var(w, v);
        tw_s(w, "(");
        tw_i(w, ub);
        tw_s(w, ")");
        if (peek(w) != ',') break;
        w->i++;
    }
    return ZL_OK;
}

static int st_def(bw_t *w)
{
    if (!kw(w, "FN") || w->i >= w->n || !zl_isalpha((unsigned char) w->s[w->i]))
        return bfail(w, ZL_E_SYNTAX, "DEF FNx expected");
    int16_t f = (int16_t) (zl_upper((unsigned char) w->s[w->i]) - 'A');
    int s;
    w->i++;
    if (w->defs[f].defined) return bfail(w, ZL_E_SYNTAX, "function defined twice");
    int16_t param = -1;
    if (peek(w) == '(') {
        w->i++;
        param = varname(w);
        if (param < 0) return bfail(w, ZL_E_SYNTAX, "parameter name expected");
        if ((s = expect(w, ')', "')' expected"))) return s;
    } else {
        return bfail(w, ZL_E_UNSUPPORTED, "a function without a parameter is not in this subset");
    }
    if ((s = expect(w, '=', "'=' expected"))) return s;
    int32_t body = expr(w);
    if (body < 0) return w->c->d->status;
    int32_t ps = zl_slots(w->c, 1);
    if (ps < 0) return w->c->d->status;
    w->defs[f].defined = 1;
    w->defs[f].param = param;
    w->defs[f].body = body;
    w->defs[f].pslot = ps;
    w->pool_floor = w->npool;
    tw_s(w, "DEF FN");
    if (w->tw) zl_putc(w->tw, (char) ('A' + f));
    tw_s(w, "(");
    tw_var(w, param);
    tw_s(w, ") = ");
    tw_expr(w, body);
    return ZL_OK;
}

static int statement(bw_t *w, int *ended)
{
    zl_cc_t *c = w->c;
    int s;
    uint32_t t;
    if (kw(w, "REM")) {
        size_t a = w->i, b = w->n;
        while (a < b && zl_isspace((unsigned char) w->s[a])) a++;
        while (b > a && zl_isspace((unsigned char) w->s[b - 1])) b--;
        tw_s(w, "REM");
        if (w->tw && b > a) {
            zl_putc(w->tw, ' ');
            zl_put(w->tw, w->s + a, b - a);
        }
        w->i = w->n;
        return ZL_OK;
    }
    if (kw(w, "LET")) return st_let(w);
    if (kw(w, "PRINT")) return st_print(w);
    if (kw(w, "IF")) return st_if(w);
    if (kw(w, "GOTO") || kw2(w, "TO")) {
        if ((s = lineref(w, &t))) return s;
        tw_s(w, "GOTO ");
        tw_i(w, t);
        return add_fix(w, zl_emit(c, ZL_JMP, 0, 0), t);
    }
    if (kw(w, "GOSUB") || kw2(w, "SUB")) {
        if ((s = lineref(w, &t))) return s;
        tw_s(w, "GOSUB ");
        tw_i(w, t);
        return add_fix(w, zl_emit(c, ZL_CALL, 0, 0), t);
    }
    if (kw(w, "RETURN")) {
        tw_s(w, "RETURN");
        zl_emit(c, ZL_RET, 0, 0);
        return ZL_OK;
    }
    if (kw(w, "ON")) return st_on(w);
    if (kw(w, "FOR")) return st_for(w);
    if (kw(w, "NEXT")) return st_next(w);
    if (kw(w, "END")) {
        tw_s(w, "END");
        zl_emit(c, ZL_HALT, 0, 0);
        *ended = 1;
        return ZL_OK;
    }
    if (kw(w, "STOP")) {
        tw_s(w, "STOP");
        zl_emit(c, ZL_HALT, 0, 0);
        return ZL_OK;
    }
    if (kw(w, "INPUT")) return st_input_read(w, 0);
    if (kw(w, "READ")) return st_input_read(w, 1);
    if (kw(w, "RESTORE")) {
        tw_s(w, "RESTORE");
        if (w->data_ptr_slot < 0 && (w->data_ptr_slot = zl_slots(c, 1)) < 0) return c->d->status;
        zl_emit(c, ZL_PUSH, 0, 0);
        zl_emit(c, ZL_STORE, w->data_ptr_slot, 0);
        return ZL_OK;
    }
    if (kw(w, "DATA")) return st_data(w);
    if (kw(w, "DIM")) return st_dim(w);
    if (kw(w, "DEF")) return st_def(w);
    if (kw(w, "OPTION")) {
        int64_t b;
        if (!kw(w, "BASE")) return bfail(w, ZL_E_SYNTAX, "OPTION BASE expected");
        if ((s = number(w, &b))) return s;
        if (b != 0 && b != 1) return bfail(w, ZL_E_SYNTAX, "OPTION BASE must be 0 or 1");
        if (w->base_set || w->any_array)
            return bfail(w, ZL_E_SYNTAX, "OPTION BASE must come once, before any array");
        w->base_set = 1;
        w->base1 = (int) b;
        tw_s(w, b ? "OPTION BASE 1" : "OPTION BASE 0");
        return ZL_OK;
    }
    if (kw(w, "RANDOMIZE"))
        return bfail(w, ZL_E_UNSUPPORTED, "RANDOMIZE (no random numbers in the exact subset)");
    /* implied LET */
    {
        size_t save = w->i;
        if (varname(w) >= 0) {
            w->i = save;
            return st_let(w);
        }
    }
    return bfail(w, ZL_E_SYNTAX, "unknown statement");
}

int zl_basic_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    bw_t *w = (bw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->tw = tw;
    w->data_ptr_slot = -1;
    w->tmp_slot = -1;
    for (int k = 0; k < 26 * 11; k++) w->varslot[k] = -1;
    int32_t jinit = zl_emit(c, ZL_JMP, 0, 0);
    int ended = 0, s;
    uint32_t prev = 0;
    size_t pos = 0;
    c->line = 0;
    while (pos < len) {
        size_t e = pos;
        while (e < len && src[e] != '\n') e++;
        c->line++;
        c->col = 0;
        w->s = src + pos;
        w->n = e - pos;
        if (w->n && w->s[w->n - 1] == '\r') w->n--;
        w->i = 0;
        pos = e < len ? e + 1 : e;
        if (w->n > ZL_MAX_LINE) return bfail(w, ZL_E_LIMIT, "line too long");
        if (peek(w) == 0) continue; /* blank line */
        if (ended) return bfail(w, ZL_E_SYNTAX, "END must be the last line");
        uint32_t ln;
        if ((s = lineref(w, &ln))) return s;
        if (ln <= prev) return bfail(w, ZL_E_SYNTAX, "line numbers must increase");
        prev = ln;
        if (w->nlines >= B_LINES) return bfail(w, ZL_E_LIMIT, "too many lines");
        w->lineno[w->nlines] = (uint16_t) ln;
        w->lineaddr[w->nlines] = (int32_t) c->p->ncode;
        w->nlines++;
        tw_i(w, ln);
        tw_s(w, " ");
        w->npool = w->pool_floor;
        w->depth = 0;
        if ((s = statement(w, &ended))) return s;
        if (c->failed) return c->d->status;
        if (peek(w) != 0) return bfail(w, ZL_E_SYNTAX, "unexpected text after the statement");
        tw_s(w, "\n");
    }
    if (w->nfor) return bfail(w, ZL_E_SYNTAX, "FOR without NEXT");
    if (!ended) zl_emit(c, ZL_HALT, 0, 0);
    /* resolve line-number jumps */
    for (uint32_t k = 0; k < w->nfix; k++) {
        uint32_t lo = 0, hi = w->nlines;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (w->lineno[mid] < w->fix[k].target)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo >= w->nlines || w->lineno[lo] != w->fix[k].target) {
            c->line = w->fix[k].srcline;
            c->col = 0;
            return zl_fail(c, ZL_E_UNDEFINED, "jump to a line that does not exist");
        }
        zl_patch(c, w->fix[k].at, w->lineaddr[lo]);
    }
    /* DATA: one table, filled before the first line runs */
    int32_t dbase = -1;
    if (w->ndfix) {
        dbase = zl_slots(c, w->ndata ? w->ndata : 1);
        if (dbase < 0) return c->d->status;
        if (w->data_ptr_slot < 0) return zl_fail(c, ZL_E_ARG, "internal: READ without its pointer");
    }
    for (uint32_t k = 0; k < w->ndfix; k++) {
        zl_ins_t *in = &c->p->code[w->dfix[k].at];
        if (w->dfix[k].kind == 0)
            in->a = w->ndata;
        else {
            in->a = dbase;
            in->b = (int32_t) (w->ndata ? w->ndata : 1);
        }
    }
    zl_patch(c, jinit, (int64_t) c->p->ncode);
    c->line = 0;
    if (dbase >= 0)
        for (uint32_t k = 0; k < w->ndata; k++) {
            zl_emit(c, ZL_PUSH, w->data[k], 0);
            zl_emit(c, ZL_STORE, dbase + (int32_t) k, 0);
        }
    zl_emit(c, ZL_JMP, 1, 0);
    return c->failed ? c->d->status : ZL_OK;
}
