/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_logo.c — random Logo programs for differential testing against
 * UCBLogo (ref_logo.sh runs ucblogo and zxlang on the same programs and
 * requires identical output).
 *
 * A program is built as a tree, then run by a small interpreter here that
 * follows UCBLogo's rules (dynamic scope, stop ending the innermost repeat,
 * repcount, for counting down, exact division only). An attempt that would
 * leave +-10^12, divide inexactly or by zero, take first of the empty list,
 * recurse or loop too long, or use too many list cells is thrown away and
 * the next attempt is generated, so every program printed is well defined
 * in UCBLogo and in the Lingua subset. Then it is printed with Logo's
 * spelling freedoms: abbreviations, upper case, redundant parentheses,
 * infix or prefix arithmetic, lists spread over lines.
 *
 * Usage: gen_logo <seed>   (writes the program to stdout) */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng;
static unsigned rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (unsigned) (rng % n) : 0;
}

/* ---------------------------------------------------------------- the tree */

enum {
    /* integers */
    I_NUM = 1,
    I_VAR,
    I_BIN, /* op '+' '-' '*' '/'; style 1: prefix sum difference product quotient */
    I_REM, /* op 'r' remainder, 'm' modulo */
    I_NEG, /* style 0: unary minus, 1: minus */
    I_COUNT,
    I_FIRST,
    I_LAST,
    I_ITEM, /* v: index */
    I_IFELSE,
    I_CALL,
    I_REPCOUNT,
    /* lists of integers */
    L_LIT, /* v: literal index */
    L_VAR,
    L_FPUT,
    L_LPUT,
    L_LIST,
    L_SE,
    L_BF,
    L_BL,
    L_CALL,
    L_IFELSE,
    /* conditions */
    B_CMP, /* op '<' '>' '=' 'n' (<>) 'l' (<=) 'g' (>=); style 1: prefix */
    B_EQL,
    B_NOT,
    B_AND, /* op '&' '|' */
    B_EMPTYP,
    B_MEMBERP,
    B_NUMBERP,
    B_LISTP,
    /* print-only values */
    W_WORD, /* v: word index */
    W_DATA, /* v: mixed literal index */
    /* statements */
    S_MAKE,
    S_PRINT, /* op 'p' print, 's' show, 't' type; n inputs */
    S_REPEAT,
    S_FOR, /* var, a: start, b: end, c: step (or -1) */
    S_WHILE,
    S_IF,
    S_IFELSE,
    S_CALL,
    S_OUTPUT,
    S_STOP,
    S_LOCAL
};

#define MAXN 20000
typedef struct {
    int k, op, style, n, var, paren;
    long long v;
    int a[6];
    int body, body2, next;
} node_t;
static node_t nd[MAXN];
static int nn;

static int mk(int k)
{
    if (nn >= MAXN) {
        fprintf(stderr, "gen_logo: tree too large\n");
        exit(2);
    }
    memset(&nd[nn], 0, sizeof nd[nn]);
    nd[nn].k = k;
    nd[nn].body = nd[nn].body2 = nd[nn].next = -1;
    for (int i = 0; i < 6; i++) nd[nn].a[i] = -1;
    return nn++;
}

/* variables: 0..4 integers a b c d n; 5, 6 for loops i j; 7, 8 while counters
 * w u; 9, 10 lists l m */
static const char *const vname[] = {"a", "b", "c", "d", "n", "i", "j", "w", "u", "l", "m"};
#define NV      11
#define V_I     5
#define V_W     7
#define V_L     9
#define IS_L(v) ((v) >= V_L)

static const char *const words[] = {"apple", "Zeta", "x1", "true", "false", "quux_2"};

/* literal integer lists */
#define MAXLIT 256
static long long lit[MAXLIT][6];
static int litn[MAXLIT], nlit;

/* procedures */
enum { PK_INT = 1, PK_LIST, PK_CMD };
typedef struct {
    int kind, rec, np, params[3], body;
    char name[8];
} proc_t;
static proc_t pr[6];
static int npr;

/* ---------------------------------------------------------------- generation */

typedef struct {
    int proc;           /* -1 at top level */
    unsigned readable;  /* bit per variable that is bound here */
    int nonempty_param; /* a list procedure past its emptyp test */
    int for_depth, while_depth, in_loop;
} ctx_t;

static int gen_int(ctx_t *x, int d);
static int gen_list(ctx_t *x, int d);
static int gen_bool(ctx_t *x, int d);

static int pick_var(ctx_t *x, int lists)
{
    int c[NV], m = 0;
    for (int v = 0; v < NV; v++)
        if ((x->readable >> v & 1) && IS_L(v) == lists) c[m++] = v;
    return m ? c[rnd((unsigned) m)] : -1;
}

static int num(long long v)
{
    int n = mk(I_NUM);
    nd[n].v = v;
    return n;
}

static int call_args(int n, ctx_t *x, int p, int d)
{
    for (int i = 0; i < pr[p].np; i++) {
        if (pr[p].kind == PK_LIST)
            nd[n].a[i] = gen_list(x, d - 1);
        else if (pr[p].rec) { /* small arguments keep the recursion short */
            if (rnd(2)) {
                nd[n].a[i] = num((long long) rnd(9));
            } else {
                int r = mk(I_REM);
                nd[r].op = 'r';
                nd[r].a[0] = gen_int(x, d - 1);
                nd[r].a[1] = num(7);
                nd[n].a[i] = r;
            }
        } else
            nd[n].a[i] = gen_int(x, d - 1);
    }
    return n;
}

/* a procedure of this kind the current context may call */
static int callee(ctx_t *x, int kind)
{
    int c[6], m = 0;
    int lim = x->proc < 0 ? npr : x->proc;
    for (int p = 0; p < lim; p++)
        if (pr[p].kind == kind) c[m++] = p;
    return m ? c[rnd((unsigned) m)] : -1;
}

static int gen_int(ctx_t *x, int d)
{
    unsigned k = d <= 0 ? rnd(3) : rnd(22);
    int n, v;
    if (k == 0 || (k == 1 && pick_var(x, 0) < 0)) return num((long long) rnd(30) - 8);
    if (k <= 2) {
        if (x->nonempty_param && rnd(3) == 0) {
            n = mk(I_FIRST);
            nd[n].a[0] = mk(L_VAR);
            nd[nd[n].a[0]].var = pr[x->proc].params[0];
            return n;
        }
        v = pick_var(x, 0);
        n = mk(I_VAR);
        nd[n].var = v;
        return n;
    }
    switch (k) {
    case 3:
    case 4:
    case 5:
    case 6:
        n = mk(I_BIN);
        nd[n].op = "+-*+"[rnd(4)];
        nd[n].style = rnd(4) == 0;
        nd[n].a[0] = gen_int(x, d - 1);
        nd[n].a[1] = gen_int(x, d - 1);
        if (nd[n].op == '*' && rnd(2)) nd[n].a[1] = num((long long) rnd(7) - 2);
        return n;
    case 7: { /* exact division: (e - remainder e k) / k */
        int e = gen_int(x, d - 1),
            kk = num((long long) (rnd(2) ? 1 + (int) rnd(6) : -1 - (int) rnd(4)));
        int r = mk(I_REM), s = mk(I_BIN);
        nd[r].op = rnd(2) ? 'r' : 'm';
        nd[r].a[0] = e;
        nd[r].a[1] = kk;
        nd[s].op = '-';
        nd[s].a[0] = e;
        nd[s].a[1] = r;
        n = mk(I_BIN);
        nd[n].op = '/';
        nd[n].style = rnd(3) == 0;
        nd[n].a[0] = s;
        nd[n].a[1] = kk;
        return n;
    }
    case 8:
        n = mk(I_REM);
        nd[n].op = rnd(2) ? 'r' : 'm';
        nd[n].a[0] = gen_int(x, d - 1);
        nd[n].a[1] = rnd(4) ? num((long long) (rnd(2) ? 2 + (int) rnd(8) : -2 - (int) rnd(5)))
                            : gen_int(x, d - 1);
        return n;
    case 9:
        n = mk(I_NEG);
        nd[n].style = rnd(2);
        nd[n].a[0] = gen_int(x, d - 1);
        return n;
    case 10:
        n = mk(I_COUNT);
        nd[n].a[0] = gen_list(x, d - 1);
        return n;
    case 11:
    case 12:
        n = mk(k == 11 ? I_FIRST : I_LAST);
        nd[n].a[0] = gen_list(x, d - 1);
        return n;
    case 13:
        n = mk(I_ITEM);
        nd[n].a[0] = num(1 + rnd(3));
        nd[n].a[1] = gen_list(x, d - 1);
        return n;
    case 14:
        n = mk(I_IFELSE);
        nd[n].a[0] = gen_bool(x, d - 1);
        nd[n].a[1] = gen_int(x, d - 1);
        nd[n].a[2] = gen_int(x, d - 1);
        return n;
    case 15:
    case 16: {
        int p = callee(x, PK_INT);
        if (p < 0) return num((long long) rnd(10));
        n = mk(I_CALL);
        nd[n].var = p;
        return call_args(n, x, p, d);
    }
    case 17:
        return mk(I_REPCOUNT);
    default:
        return num((long long) rnd(100) - 30);
    }
}

static int gen_list(ctx_t *x, int d)
{
    unsigned k = d <= 0 ? rnd(2) : rnd(14);
    int n;
    if (k == 0) {
        n = mk(L_LIT);
        if (nlit >= MAXLIT) {
            nd[n].v = 0;
            return n;
        }
        nd[n].v = nlit;
        litn[nlit] = (int) rnd(6);
        for (int i = 0; i < litn[nlit]; i++) lit[nlit][i] = (long long) rnd(40) - 10;
        nlit++;
        return n;
    }
    if (k == 1) {
        n = mk(L_VAR);
        nd[n].var = pick_var(x, 1);
        if (nd[n].var < 0) nd[n].var = V_L;
        return n;
    }
    switch (k) {
    case 2:
    case 3:
        n = mk(k == 2 ? L_FPUT : L_LPUT);
        nd[n].a[0] = gen_int(x, d - 1);
        nd[n].a[1] = gen_list(x, d - 1);
        return n;
    case 4:
        n = mk(L_LIST);
        nd[n].n = rnd(4) == 0 ? (int) rnd(4) : 2;
        for (int i = 0; i < nd[n].n; i++) nd[n].a[i] = gen_int(x, d - 1);
        return n;
    case 5:
    case 6:
        n = mk(L_SE);
        nd[n].n = rnd(4) == 0 ? 3 : 2;
        for (int i = 0; i < nd[n].n; i++)
            nd[n].a[i] = rnd(3) ? gen_list(x, d - 1) : gen_int(x, d - 1);
        return n;
    case 7:
    case 8:
        n = mk(k == 7 ? L_BF : L_BL);
        nd[n].a[0] = gen_list(x, d - 1);
        return n;
    case 9:
    case 10: {
        int p = callee(x, PK_LIST);
        if (p < 0) return gen_list(x, 0);
        n = mk(L_CALL);
        nd[n].var = p;
        return call_args(n, x, p, d);
    }
    case 11:
        n = mk(L_IFELSE);
        nd[n].a[0] = gen_bool(x, d - 1);
        nd[n].a[1] = gen_list(x, d - 1);
        nd[n].a[2] = gen_list(x, d - 1);
        return n;
    default:
        return gen_list(x, 0);
    }
}

static int gen_bool(ctx_t *x, int d)
{
    unsigned k = d <= 0 ? 0 : rnd(12);
    int n;
    switch (k) {
    case 0:
    case 1:
    case 2:
    case 3:
        n = mk(B_CMP);
        nd[n].op = "<>=nlg"[rnd(6)];
        nd[n].style = rnd(4) == 0;
        nd[n].a[0] = gen_int(x, d - 1);
        nd[n].a[1] = gen_int(x, d - 1);
        return n;
    case 4:
        n = mk(B_EQL);
        nd[n].style = rnd(2);
        nd[n].a[0] = gen_list(x, d - 1);
        nd[n].a[1] = gen_list(x, d - 1);
        return n;
    case 5:
        n = mk(B_NOT);
        nd[n].a[0] = gen_bool(x, d - 1);
        return n;
    case 6:
    case 7:
        n = mk(B_AND);
        nd[n].op = rnd(2) ? '&' : '|';
        nd[n].n = rnd(4) == 0 ? 3 : 2;
        for (int i = 0; i < nd[n].n; i++) nd[n].a[i] = gen_bool(x, d - 1);
        return n;
    case 8:
        n = mk(B_EMPTYP);
        nd[n].a[0] = gen_list(x, d - 1);
        return n;
    case 9:
        n = mk(B_MEMBERP);
        nd[n].a[0] = gen_int(x, d - 1);
        nd[n].a[1] = gen_list(x, d - 1);
        return n;
    case 10:
        n = mk(B_NUMBERP);
        nd[n].a[0] = gen_int(x, d - 1);
        return n;
    default:
        n = mk(B_LISTP);
        nd[n].a[0] = rnd(2) ? gen_list(x, d - 1) : gen_int(x, d - 1);
        return n;
    }
}

static int gen_any(ctx_t *x, int d)
{
    unsigned k = rnd(10);
    if (k < 4) return gen_int(x, d);
    if (k < 7) return gen_list(x, d);
    if (k < 8) return gen_bool(x, d);
    int n = mk(k == 8 ? W_WORD : W_DATA);
    nd[n].v = rnd(6);
    return n;
}

static int gen_block(ctx_t *x, int d, int nmax);

static int gen_stmt(ctx_t *x, int d)
{
    unsigned k = d <= 0 ? rnd(3) : rnd(20);
    int n, v;
    int op_proc = x->proc >= 0 && pr[x->proc].kind != PK_CMD;
    switch (k) {
    case 0:
    case 1:
    case 2:
    case 3:
        n = mk(S_PRINT);
        nd[n].op = "ppst"[rnd(4)];
        nd[n].n = rnd(4) == 0 ? (int) rnd(4) : 1;
        for (int i = 0; i < nd[n].n; i++) nd[n].a[i] = gen_any(x, d - 1);
        return n;
    case 4:
    case 5:
    case 6:
        n = mk(S_MAKE);
        v = rnd(3) == 0 ? V_L + (int) rnd(2) : (int) rnd(5);
        nd[n].var = v;
        nd[n].a[0] = IS_L(v) ? gen_list(x, d - 1) : gen_int(x, d - 1);
        return n;
    case 7:
    case 8:
        n = mk(S_REPEAT);
        nd[n].a[0] = num((long long) rnd(5));
        x->in_loop++;
        nd[n].body = gen_block(x, d - 1, 3);
        x->in_loop--;
        return n;
    case 9:
    case 10: {
        if (x->for_depth >= 2) return gen_stmt(x, 0);
        n = mk(S_FOR);
        v = V_I + x->for_depth;
        nd[n].var = v;
        for (int i = 0; i < 2; i++) {
            int e = pick_var(x, 0);
            nd[n].a[i] = (e >= 0 && rnd(3) == 0) ? (nd[mk(I_VAR)].var = e, nn - 1)
                                                 : num((long long) rnd(9) - 2);
        }
        if (rnd(3) == 0) nd[n].a[2] = num(rnd(2) ? 1 + (int) rnd(3) : -1 - (int) rnd(3));
        unsigned save = x->readable;
        x->readable |= 1u << v;
        x->for_depth++;
        x->in_loop++;
        nd[n].body = gen_block(x, d - 1, 3);
        x->in_loop--;
        x->for_depth--;
        x->readable = save;
        return n;
    }
    case 11: {
        if (x->while_depth >= 2) return gen_stmt(x, 0);
        v = V_W + x->while_depth;
        n = mk(S_WHILE);
        nd[n].var = v;
        nd[n].op = rnd(2) ? 'w' : 'u';
        nd[n].v = 1 + rnd(4);
        unsigned save = x->readable;
        x->readable |= 1u << v;
        x->while_depth++;
        x->in_loop++;
        nd[n].body = gen_block(x, d - 1, 3);
        x->in_loop--;
        x->while_depth--;
        x->readable = save;
        return n;
    }
    case 12:
    case 13:
    case 14:
        n = mk(k == 12 ? S_IFELSE : S_IF);
        nd[n].a[0] = gen_bool(x, d - 1);
        nd[n].body = gen_block(x, d - 1, 3);
        if (k == 12) nd[n].body2 = gen_block(x, d - 1, 3);
        return n;
    case 15:
    case 16: {
        int p = callee(x, PK_CMD);
        if (p < 0) return gen_stmt(x, 0);
        n = mk(S_CALL);
        nd[n].var = p;
        return call_args(n, x, p, d);
    }
    case 17:
        if (x->proc < 0) return gen_stmt(x, 0);
        if (op_proc) {
            n = mk(S_OUTPUT);
            nd[n].a[0] = pr[x->proc].kind == PK_LIST ? gen_list(x, d - 1) : gen_int(x, d - 1);
            return n;
        }
        n = mk(S_IF);
        nd[n].a[0] = gen_bool(x, d - 1);
        nd[n].body = mk(S_STOP);
        return n;
    default:
        return gen_stmt(x, 0);
    }
}

static int gen_block(ctx_t *x, int d, int nmax)
{
    int first = -1, last = -1, m = (int) rnd((unsigned) nmax + 1);
    for (int i = 0; i < m; i++) {
        int s = gen_stmt(x, d);
        if (last < 0)
            first = s;
        else
            nd[last].next = s;
        last = s;
    }
    return first;
}

static int append(int first, int s)
{
    if (first < 0) return s;
    int t = first;
    while (nd[t].next >= 0) t = nd[t].next;
    nd[t].next = s;
    return first;
}

/* A stop inside a repeat that ends a procedure escapes it in UCBLogo (the
 * last instruction is a tail call); zl_logo refuses it. rt: the innermost
 * enclosing repeat is in tail position. */
static int tail_stop(int s, int tail, int rt)
{
    for (; s >= 0; s = nd[s].next) {
        int t = tail && nd[s].next < 0, k = nd[s].k;
        if (k == S_STOP && rt) return 1;
        if (k == S_REPEAT && tail_stop(nd[s].body, t, t)) return 1;
        if ((k == S_FOR || k == S_WHILE || k == S_IF || k == S_IFELSE) &&
            tail_stop(nd[s].body, t, rt))
            return 1;
        if (k == S_IFELSE && tail_stop(nd[s].body2, t, rt)) return 1;
    }
    return 0;
}

static void gen_proc(int p)
{
    proc_t *q = &pr[p];
    ctx_t x;
    memset(&x, 0, sizeof x);
    x.proc = p;
    x.readable = (1u << 5) - 1; /* the integer globals */
    x.readable |= 3u << V_L;
    int body = -1;
    snprintf(q->name, sizeof q->name, "%s%d",
             q->kind == PK_INT    ? "f"
             : q->kind == PK_LIST ? "g"
                                  : "c",
             p);
    if (q->kind == PK_LIST) {
        q->np = 1;
        q->params[0] = V_L + (int) rnd(2);
    } else {
        q->np = q->rec ? 1 : (int) rnd(3);
        for (int i = 0; i < q->np; i++) {
            int v;
            do v = (int) rnd(5);
            while (i && v == q->params[0]);
            q->params[i] = v;
        }
    }
    /* locals at the start of command and integer procedures */
    if (q->kind != PK_LIST && rnd(3) == 0) {
        int v = (int) rnd(5), dup = 0;
        for (int i = 0; i < q->np; i++) dup |= q->params[i] == v;
        if (!dup) {
            int l = mk(S_LOCAL), m = mk(S_MAKE);
            nd[l].var = v;
            nd[m].var = v;
            nd[m].a[0] = gen_int(&x, 2);
            body = append(append(body, l), m);
        }
    }
    if (q->kind == PK_LIST) {
        int pv = q->params[0];
        int t = mk(S_IF), b = mk(B_EMPTYP), o = mk(S_OUTPUT), lv = mk(L_VAR);
        nd[lv].var = pv;
        nd[b].a[0] = lv;
        nd[t].a[0] = b;
        nd[o].a[0] = rnd(2) ? (nd[mk(L_LIT)].v = 0, nn - 1) : gen_list(&x, 1);
        nd[t].body = o;
        body = append(body, t);
        x.nonempty_param = 1;
        int rest = mk(L_BF), self = mk(L_CALL), out = mk(S_OUTPUT), comb;
        nd[rest].a[0] = mk(L_VAR);
        nd[nd[rest].a[0]].var = pv;
        nd[self].var = p;
        nd[self].a[0] = rest;
        switch (rnd(3)) {
        case 0:
            comb = mk(L_FPUT);
            nd[comb].a[0] = gen_int(&x, 2);
            nd[comb].a[1] = self;
            break;
        case 1:
            comb = mk(L_LPUT);
            nd[comb].a[0] = gen_int(&x, 2);
            nd[comb].a[1] = self;
            break;
        default:
            comb = mk(L_SE);
            nd[comb].n = 2;
            nd[comb].a[0] = self;
            nd[comb].a[1] = rnd(2) ? gen_int(&x, 2) : gen_list(&x, 1);
            break;
        }
        nd[out].a[0] = comb;
        body = append(body, out);
        q->body = body;
        return;
    }
    for (int i = 0; i < q->np; i++) x.readable |= 1u << q->params[i];
    if (q->rec) {
        int pv = q->params[0];
        int t = mk(S_IF), c = mk(B_CMP), o = mk(S_OUTPUT), pvn = mk(I_VAR);
        nd[pvn].var = pv;
        nd[c].op = '<';
        nd[c].a[0] = pvn;
        nd[c].a[1] = num(1);
        nd[t].a[0] = c;
        nd[o].a[0] = gen_int(&x, 1);
        nd[t].body = o;
        body = append(body, t);
        int arg = mk(I_BIN), self = mk(I_CALL), comb = mk(I_BIN), out = mk(S_OUTPUT);
        nd[arg].op = '-';
        nd[arg].a[0] = mk(I_VAR);
        nd[nd[arg].a[0]].var = pv;
        nd[arg].a[1] = num(1 + rnd(2));
        nd[self].var = p;
        nd[self].a[0] = arg;
        nd[comb].op = "+-*"[rnd(3)];
        nd[comb].style = rnd(4) == 0;
        nd[comb].a[0] = gen_int(&x, 1);
        nd[comb].a[1] = self;
        if (nd[comb].op == '*') nd[comb].a[0] = num(rnd(2) ? 2 : -1);
        nd[out].a[0] = comb;
        q->body = append(body, out);
        return;
    }
    body = append(body, gen_block(&x, 2, 3));
    if (q->kind == PK_INT) {
        int o = mk(S_OUTPUT);
        nd[o].a[0] = gen_int(&x, 3);
        body = append(body, o);
    }
    if (tail_stop(body, 1, 0)) { /* end on another instruction instead */
        int n = mk(S_PRINT), wd = mk(W_WORD);
        nd[n].op = 'p';
        nd[n].n = 1;
        nd[wd].v = rnd(6);
        nd[n].a[0] = wd;
        body = append(body, n);
    }
    q->body = body;
}

/* ---------------------------------------------------------------- interpreter */

static jmp_buf reject;
#define BAD() longjmp(reject, 1)
#define LIMIT 1000000000000ll
#define CELLS 12000

static long long car_[CELLS + 8];
static int cdr_[CELLS + 8];
static int ncell, steps, depth, printed;
static long long ival[NV];
static int lval[NV], bound[NV], rc;

enum { R_NORMAL = 0, R_STOP, R_OUTPUT };
static long long ret_i;
static int ret_l;

static void step(void)
{
    if (++steps > 20000) BAD();
}
static long long chk(long long v)
{
    if (v > LIMIT || v < -LIMIT) BAD();
    return v;
}
static int cons(long long a, int d)
{
    if (ncell >= CELLS) BAD();
    car_[ncell] = a;
    cdr_[ncell] = d;
    return ncell++;
}
static int len(int l)
{
    int n = 0;
    for (; l >= 0; l = cdr_[l]) n++;
    return n;
}
static int copy_onto(int a, int tail) /* a copy of a ending in tail */
{
    if (a < 0) return tail;
    int h = cons(car_[a], -1), t = h;
    for (a = cdr_[a]; a >= 0; a = cdr_[a]) {
        int c = cons(car_[a], -1);
        cdr_[t] = c;
        t = c;
    }
    cdr_[t] = tail;
    return h;
}

static long long ev_int(int n);
static int ev_list(int n);
static int ev_bool(int n);
static int exec(int s);

static void call_proc(int p, const int *args)
{
    proc_t *q = &pr[p];
    long long si[3], ai[3];
    int sl[3], sb[3], al[3];
    for (int i = 0; i < q->np; i++) { /* inputs are evaluated in the caller */
        if (q->kind == PK_LIST)
            al[i] = ev_list(args[i]);
        else
            ai[i] = ev_int(args[i]);
    }
    if (++depth > 40) BAD();
    for (int i = 0; i < q->np; i++) {
        int v = q->params[i];
        si[i] = ival[v];
        sl[i] = lval[v];
        sb[i] = bound[v];
        if (q->kind == PK_LIST)
            lval[v] = al[i];
        else
            ival[v] = ai[i];
        bound[v] = 1;
    }
    /* locals: saved at the local instruction, restored here */
    long long li = 0;
    int lv = -1, lb = 0;
    int r = R_NORMAL;
    for (int s = q->body; s >= 0 && r == R_NORMAL; s = nd[s].next) {
        if (nd[s].k == S_LOCAL) {
            lv = nd[s].var;
            li = ival[lv];
            lb = bound[lv];
            bound[lv] = 0;
            continue;
        }
        r = exec(s);
    }
    if (lv >= 0) {
        ival[lv] = li;
        bound[lv] = lb;
    }
    for (int i = q->np - 1; i >= 0; i--) {
        int v = q->params[i];
        ival[v] = si[i];
        lval[v] = sl[i];
        bound[v] = sb[i];
    }
    depth--;
    if (q->kind == PK_CMD ? r == R_OUTPUT : r != R_OUTPUT) BAD();
}

static long long ev_int(int n)
{
    node_t *x = &nd[n];
    long long a, b;
    int l;
    step();
    switch (x->k) {
    case I_NUM:
        return x->v;
    case I_VAR:
        if (!bound[x->var]) BAD();
        return ival[x->var];
    case I_BIN:
        a = ev_int(x->a[0]);
        b = ev_int(x->a[1]);
        switch (x->op) {
        case '+':
            return chk(a + b);
        case '-':
            return chk(a - b);
        case '*':
            if ((a > 1000000 || a < -1000000) && (b > 1000000 || b < -1000000)) BAD();
            return chk(a * b);
        default:
            if (b == 0 || a % b) BAD();
            return chk(a / b);
        }
    case I_REM:
        a = ev_int(x->a[0]);
        b = ev_int(x->a[1]);
        if (b == 0) BAD();
        if (x->op == 'r') return a % b;
        if (a % b == 0) return b < 0 ? b : 0; /* UCBLogo 6.2: modulo 6 -3 is -3 */
        if ((a % b < 0) == (b < 0)) return a % b;
        return a % b + b; /* modulo takes the sign of the divisor */
    case I_NEG:
        return -ev_int(x->a[0]);
    case I_COUNT:
        return len(ev_list(x->a[0]));
    case I_FIRST:
    case I_LAST:
        l = ev_list(x->a[0]);
        if (l < 0) BAD();
        if (x->k == I_LAST)
            while (cdr_[l] >= 0) l = cdr_[l];
        return car_[l];
    case I_ITEM:
        a = ev_int(x->a[0]);
        l = ev_list(x->a[1]);
        while (a > 1 && l >= 0) {
            l = cdr_[l];
            a--;
        }
        if (l < 0 || a < 1) BAD();
        return car_[l];
    case I_IFELSE:
        return ev_bool(x->a[0]) ? ev_int(x->a[1]) : ev_int(x->a[2]);
    case I_CALL:
        call_proc(x->var, x->a);
        return ret_i;
    default: /* I_REPCOUNT */
        return rc;
    }
}

static int ev_list(int n)
{
    node_t *x = &nd[n];
    int l, m;
    long long a;
    step();
    switch (x->k) {
    case L_LIT: /* built once, before the program runs */
        return x->body;
    case L_VAR:
        if (!bound[x->var]) BAD();
        return lval[x->var];
    case L_FPUT:
        a = ev_int(x->a[0]);
        return cons(a, ev_list(x->a[1]));
    case L_LPUT:
        a = ev_int(x->a[0]);
        l = ev_list(x->a[1]);
        return copy_onto(l, cons(a, -1));
    case L_LIST: {
        long long v[6];
        for (int i = 0; i < x->n; i++) v[i] = ev_int(x->a[i]);
        l = -1;
        for (int i = x->n - 1; i >= 0; i--) l = cons(v[i], l);
        return l;
    }
    case L_SE: {
        int part[6] = {-1, -1, -1, -1, -1, -1};
        for (int i = 0; i < x->n; i++) {
            if (nd[x->a[i]].k < L_LIT)
                part[i] = cons(ev_int(x->a[i]), -1);
            else
                part[i] = ev_list(x->a[i]);
        }
        l = part[x->n - 1];
        for (int i = x->n - 2; i >= 0; i--) l = copy_onto(part[i], l);
        return l;
    }
    case L_BF:
    case L_BL:
        l = ev_list(x->a[0]);
        if (l < 0) BAD();
        if (x->k == L_BF) return cdr_[l];
        m = -1;
        {
            int h = -1, t = -1;
            for (; cdr_[l] >= 0; l = cdr_[l]) {
                int c = cons(car_[l], -1);
                if (t < 0)
                    h = c;
                else
                    cdr_[t] = c;
                t = c;
            }
            m = h;
        }
        return m;
    case L_CALL:
        call_proc(x->var, x->a);
        return ret_l;
    default: /* L_IFELSE */
        return ev_bool(x->a[0]) ? ev_list(x->a[1]) : ev_list(x->a[2]);
    }
}

static int leq(int a, int b)
{
    for (; a >= 0 && b >= 0; a = cdr_[a], b = cdr_[b])
        if (car_[a] != car_[b]) return 0;
    return a < 0 && b < 0;
}

static int ev_bool(int n)
{
    node_t *x = &nd[n];
    long long a, b;
    int l, r;
    step();
    switch (x->k) {
    case B_CMP:
        a = ev_int(x->a[0]);
        b = ev_int(x->a[1]);
        switch (x->op) {
        case '<':
            return a < b;
        case '>':
            return a > b;
        case '=':
            return a == b;
        case 'n':
            return a != b;
        case 'l':
            return a <= b;
        default:
            return a >= b;
        }
    case B_EQL:
        l = ev_list(x->a[0]);
        return leq(l, ev_list(x->a[1]));
    case B_NOT:
        return !ev_bool(x->a[0]);
    case B_AND:
        r = x->op == '&';
        for (int i = 0; i < x->n; i++) { /* every input is evaluated */
            int v = ev_bool(x->a[i]);
            r = x->op == '&' ? (r && v) : (r || v);
        }
        return r;
    case B_EMPTYP:
        return ev_list(x->a[0]) < 0;
    case B_MEMBERP:
        a = ev_int(x->a[0]);
        for (l = ev_list(x->a[1]); l >= 0; l = cdr_[l])
            if (car_[l] == a) return 1;
        return 0;
    case B_NUMBERP:
        ev_int(x->a[0]);
        return 1;
    default: /* B_LISTP */
        if (nd[x->a[0]].k < L_LIT) {
            ev_int(x->a[0]);
            return 0;
        }
        ev_list(x->a[0]);
        return 1;
    }
}

static void ev_any(int n)
{
    int k = nd[n].k;
    if (k == W_WORD || k == W_DATA)
        printed += 20;
    else if (k < L_LIT)
        ev_int(n), printed += 14;
    else if (k < B_CMP)
        printed += 14 * len(ev_list(n)) + 2;
    else
        ev_bool(n), printed += 6;
    if (printed > 3000) BAD();
}

static int block(int s)
{
    for (; s >= 0; s = nd[s].next) {
        int r = exec(s);
        if (r != R_NORMAL) return r;
    }
    return R_NORMAL;
}

static int exec(int s)
{
    node_t *x = &nd[s];
    int r;
    step();
    switch (x->k) {
    case S_MAKE:
        if (IS_L(x->var))
            lval[x->var] = ev_list(x->a[0]);
        else
            ival[x->var] = ev_int(x->a[0]);
        bound[x->var] = 1;
        return R_NORMAL;
    case S_PRINT:
        for (int i = 0; i < x->n; i++) ev_any(x->a[i]);
        return R_NORMAL;
    case S_REPEAT: {
        long long n = ev_int(x->a[0]);
        int save = rc;
        r = R_NORMAL;
        for (long long k = 1; k <= n; k++) {
            rc = (int) k;
            r = block(x->body);
            if (r == R_STOP) { /* UCBLogo 6.2: stop ends the innermost repeat */
                r = R_NORMAL;
                break;
            }
            if (r != R_NORMAL) break;
        }
        rc = save;
        return r;
    }
    case S_FOR: {
        long long a = ev_int(x->a[0]), b = ev_int(x->a[1]);
        long long st = x->a[2] >= 0 ? ev_int(x->a[2]) : (a > b ? -1 : 1);
        long long si = ival[x->var];
        int sb = bound[x->var];
        r = R_NORMAL;
        for (long long cur = a;; cur += st) {
            ival[x->var] = cur;
            bound[x->var] = 1;
            if (st < 0 ? cur < b : cur > b) break;
            r = block(x->body);
            if (r != R_NORMAL) break;
        }
        ival[x->var] = si;
        bound[x->var] = sb;
        return r;
    }
    case S_WHILE: {
        long long si = ival[x->var];
        int sb = bound[x->var];
        ival[x->var] = 0;
        bound[x->var] = 1;
        r = R_NORMAL;
        while (ival[x->var] < x->v) {
            r = block(x->body);
            if (r != R_NORMAL) break;
            ival[x->var]++;
        }
        /* the counter is an ordinary variable: it keeps its last value */
        (void) si;
        (void) sb;
        return r;
    }
    case S_IF:
        return ev_bool(x->a[0]) ? block(x->body) : R_NORMAL;
    case S_IFELSE:
        return ev_bool(x->a[0]) ? block(x->body) : block(x->body2);
    case S_CALL:
        call_proc(x->var, x->a);
        return R_NORMAL;
    case S_OUTPUT:
        if (nd[x->a[0]].k < L_LIT)
            ret_i = ev_int(x->a[0]);
        else
            ret_l = ev_list(x->a[0]);
        return R_OUTPUT;
    case S_STOP:
        return R_STOP;
    default:
        return R_NORMAL;
    }
}

/* ---------------------------------------------------------------- printing */

static char outb[1 << 20];
static size_t on;
static void out(const char *s)
{
    size_t n = strlen(s);
    if (on + n >= sizeof outb) {
        fprintf(stderr, "gen_logo: program too long\n");
        exit(2);
    }
    memcpy(outb + on, s, n);
    on += n;
}
static void outi(long long v)
{
    char b[32];
    snprintf(b, sizeof b, "%lld", v);
    out(b);
}

/* a primitive name, sometimes abbreviated or in upper case */
static void kw(const char *full, const char *abbr)
{
    const char *s = abbr && rnd(2) ? abbr : full;
    if (rnd(8) == 0) {
        char b[32];
        size_t i = 0;
        for (; s[i] && i < sizeof b - 1; i++)
            b[i] = (char) (s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
        b[i] = 0;
        out(b);
    } else
        out(s);
}
static void var(int v)
{
    out(rnd(10) == 0 ? (v == 0 ? "A" : v == 4 ? "N" : vname[v]) : vname[v]);
}

static int prec(int n)
{
    node_t *x = &nd[n];
    if (x->k == I_BIN && !x->style) return x->op == '+' || x->op == '-' ? 2 : 3;
    if (x->k == B_CMP && !x->style) return 1;
    if (x->k == B_EQL && !x->style) return 1;
    if (x->k == I_NEG && !x->style) return 4;
    return 5;
}
static int is_call(int n) /* a prefix procedure call with inputs */
{
    int k = nd[n].k;
    if (k == I_NUM || k == I_VAR || k == L_VAR || k == L_LIT || k == I_REPCOUNT || k == W_WORD ||
        k == W_DATA)
        return 0;
    if (k == L_LIST && nd[n].n == 0) return 0;
    if ((k == I_CALL || k == L_CALL) && pr[nd[n].var].np == 0) return 0;
    return prec(n) == 5;
}

static void pe(int n, int need, int tail);

/* pc: the inputs of a parenthesized call, where UCBLogo lets a call after an
 * infix operator take every input up to ')' (tail 2, and 3 after an operator). */
static void args(int n, int cnt, int pc)
{
    for (int i = 0; i < cnt; i++) {
        out(" ");
        pe(nd[n].a[i], 0, pc ? 2 : 1);
    }
}

static void plist(const long long *v, int m)
{
    out("[");
    for (int i = 0; i < m; i++) {
        if (i) out(" ");
        outi(v[i]);
    }
    out("]");
}

static void pe(int n, int need, int tail)
{
    node_t *x = &nd[n];
    int p = prec(n), paren = p < need || (is_call(n) && (!tail || tail == 3)) || rnd(12) == 0;
    int variadic = 0;
    if (paren) out("(");
    switch (x->k) {
    case I_NUM:
        outi(x->v);
        break;
    case I_VAR:
    case L_VAR:
        out(":");
        var(x->var);
        break;
    case I_BIN:
        if (x->style) {
            kw(x->op == '+'   ? "sum"
               : x->op == '-' ? "difference"
               : x->op == '*' ? "product"
                              : "quotient",
               0);
            args(n, 2, paren || variadic);
        } else {
            pe(x->a[0], p, 0);
            int r = x->a[1];
            while ((nd[r].k == I_BIN || nd[r].k == B_CMP || nd[r].k == B_EQL) && !nd[r].style)
                r = nd[r].a[0]; /* the leftmost operand */
            int neg = (nd[r].k == I_NUM && nd[r].v < 0) || (nd[r].k == I_NEG && !nd[r].style);
            int tight = !neg && rnd(4) == 0; /* never 2*-3: UCBLogo reads 2*-3+1 as 2*(-3+1) */
            out(x->op == '+'   ? (tight ? "+" : " + ")
                : x->op == '-' ? (tight ? "-" : " - ")
                : x->op == '*' ? (tight ? "*" : " * ")
                               : " / ");
            pe(x->a[1], p + 1, paren ? 1 : tail >= 2 ? 3 : tail);
        }
        break;
    case I_REM:
        kw(x->op == 'r' ? "remainder" : "modulo", 0);
        args(n, 2, paren || variadic);
        break;
    case I_NEG:
        if (x->style) {
            kw("minus", 0);
            args(n, 1, paren || variadic);
        } else {
            int a = x->a[0];
            out("-");
            int k = nd[a].k;
            if (k == I_VAR)
                pe(a, 5, 0);
            else {
                out("(");
                pe(a, 0, 1);
                out(")");
            }
        }
        break;
    case I_COUNT:
        kw("count", 0);
        args(n, 1, paren || variadic);
        break;
    case I_FIRST:
        kw("first", 0);
        args(n, 1, paren || variadic);
        break;
    case I_LAST:
        kw("last", 0);
        args(n, 1, paren || variadic);
        break;
    case I_ITEM:
        kw("item", 0);
        args(n, 2, paren || variadic);
        break;
    case I_IFELSE:
    case L_IFELSE:
        kw("ifelse", 0);
        out(" ");
        pe(x->a[0], 0, 1);
        out(" [");
        pe(x->a[1], 0, 1);
        out("] [");
        pe(x->a[2], 0, 1);
        out("]");
        break;
    case I_CALL:
    case L_CALL:
        out(pr[x->var].name);
        args(n, pr[x->var].np, paren || variadic);
        break;
    case I_REPCOUNT:
        kw("repcount", 0);
        break;
    case L_LIT:
        plist(lit[x->v], litn[x->v]);
        break;
    case L_FPUT:
    case L_LPUT:
        kw(x->k == L_FPUT ? "fput" : "lput", 0);
        args(n, 2, paren || variadic);
        break;
    case L_LIST:
    case L_SE:
    case B_AND:
        variadic = x->n != 2;
        if (variadic && !paren) out("(");
        kw(x->k == L_LIST ? "list"
           : x->k == L_SE ? "sentence"
           : x->op == '&' ? "and"
                          : "or",
           x->k == L_SE ? "se" : 0);
        args(n, x->n, paren || variadic);
        if (variadic && !paren) out(")");
        break;
    case L_BF:
        kw("butfirst", "bf");
        args(n, 1, paren || variadic);
        break;
    case L_BL:
        kw("butlast", "bl");
        args(n, 1, paren || variadic);
        break;
    case B_CMP:
        if (x->style) {
            static const char *const nm[] = {"lessp",     "greaterp",   "equalp",
                                             "notequalp", "lessequalp", "greaterequalp"};
            kw(nm[strchr("<>=nlg", x->op) - "<>=nlg"], 0);
            args(n, 2, paren || variadic);
        } else {
            pe(x->a[0], 1, 0);
            out(x->op == '<'   ? " < "
                : x->op == '>' ? " > "
                : x->op == '=' ? " = "
                : x->op == 'n' ? " <> "
                : x->op == 'l' ? " <= "
                               : " >= ");
            pe(x->a[1], 2, paren ? 1 : tail >= 2 ? 3 : tail);
        }
        break;
    case B_EQL:
        if (x->style) {
            kw("equalp", 0);
            args(n, 2, paren || variadic);
        } else {
            pe(x->a[0], 1, 0);
            out(" = ");
            pe(x->a[1], 2, paren ? 1 : tail >= 2 ? 3 : tail);
        }
        break;
    case B_NOT:
        kw("not", 0);
        args(n, 1, paren || variadic);
        break;
    case B_EMPTYP:
        kw("emptyp", 0);
        args(n, 1, paren || variadic);
        break;
    case B_MEMBERP:
        kw("memberp", 0);
        args(n, 2, paren || variadic);
        break;
    case B_NUMBERP:
        kw("numberp", 0);
        args(n, 1, paren || variadic);
        break;
    case B_LISTP:
        kw("listp", 0);
        args(n, 1, paren || variadic);
        break;
    case W_WORD:
        out("\"");
        out(words[x->v]);
        break;
    default: /* W_DATA */
        out(x->v == 0   ? "[]"
            : x->v == 1 ? "[a [b 2] [] -3]"
            : x->v == 2 ? "[[1] [[2 x1]]]"
            : x->v == 3 ? "[true false Zeta]"
            : x->v == 4 ? "[quux_2 7]"
                        : "[[]]");
        break;
    }
    if (paren) out(")");
}

static void ps(int s, int ind);

static void pblock(int s, int ind)
{
    int multi = s >= 0 && rnd(5) == 0;
    out("[");
    for (int first = 1; s >= 0; s = nd[s].next, first = 0) {
        if (multi) {
            out("\n");
            for (int i = 0; i < ind + 2; i++) out(" ");
        } else if (!first)
            out(" ");
        ps(s, ind + 2);
    }
    if (multi) out("\n");
    out("]");
}

static void ps(int s, int ind)
{
    node_t *x = &nd[s];
    switch (x->k) {
    case S_MAKE:
        kw("make", 0);
        out(" \"");
        var(x->var);
        out(" ");
        pe(x->a[0], 0, 1);
        break;
    case S_LOCAL:
        kw("local", 0);
        out(" \"");
        var(x->var);
        break;
    case S_PRINT:
        if (x->n != 1) out("(");
        if (x->op == 'p')
            kw("print", "pr");
        else
            kw(x->op == 's' ? "show" : "type", 0);
        args(s, x->n, x->n != 1);
        if (x->n != 1) out(")");
        break;
    case S_REPEAT:
        kw("repeat", 0);
        out(" ");
        pe(x->a[0], 0, 1);
        out(" ");
        pblock(x->body, ind);
        break;
    case S_FOR:
        kw("for", 0);
        out(" [");
        var(x->var);
        for (int i = 0; i < 3 && x->a[i] >= 0; i++) {
            out(" "); /* a number or a :name, never parenthesized */
            if (nd[x->a[i]].k == I_NUM)
                outi(nd[x->a[i]].v);
            else {
                out(":");
                var(nd[x->a[i]].var);
            }
        }
        out("] ");
        pblock(x->body, ind);
        break;
    case S_WHILE: {
        kw("make", 0);
        out(" \"");
        var(x->var);
        out(" 0 ");
        kw(x->op == 'w' ? "while" : "until", 0);
        out(" [:");
        var(x->var);
        out(x->op == 'w' ? " < " : " >= ");
        outi(x->v);
        out("] ");
        /* the body, then the counter step */
        out("[");
        for (int t = x->body; t >= 0; t = nd[t].next) {
            ps(t, ind + 2);
            out(" ");
        }
        kw("make", 0);
        out(" \"");
        var(x->var);
        out(" :");
        var(x->var);
        out(" + 1]");
        break;
    }
    case S_IF:
    case S_IFELSE:
        kw(x->k == S_IF ? "if" : "ifelse", 0);
        out(" ");
        pe(x->a[0], 0, 1);
        out(" ");
        pblock(x->body, ind);
        if (x->k == S_IFELSE) {
            out(" ");
            pblock(x->body2, ind);
        }
        break;
    case S_CALL:
        out(pr[x->var].name);
        args(s, pr[x->var].np, 0);
        break;
    case S_OUTPUT:
        kw("output", "op");
        out(" ");
        pe(x->a[0], 0, 1);
        break;
    default:
        kw("stop", 0);
        break;
    }
}

/* ---------------------------------------------------------------- main */

static void lits_build(void)
{
    /* every literal list (and its slot) is built once, as Lingua does */
    for (int i = 0; i < nn; i++)
        if (nd[i].k == L_LIT) {
            int l = -1, li = (int) nd[i].v;
            for (int k = litn[li] - 1; k >= 0; k--) l = cons(lit[li][k], l);
            nd[i].body = l;
        }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gen_logo seed\n");
        return 2;
    }
    unsigned long long seed = strtoull(argv[1], 0, 10);
    int top = -1;
    for (int attempt = 0;; attempt++) {
        if (attempt > 5000) {
            fprintf(stderr, "gen_logo: no well-defined program for seed %llu\n", seed);
            return 1;
        }
        rng = (seed * 1000 + (unsigned long long) attempt) * 0x9E3779B97F4A7C15ull + 7;
        nn = 0;
        nlit = 1;
        litn[0] = 0;
        npr = (int) rnd(5);
        for (int p = 0; p < npr; p++) {
            unsigned k = rnd(5);
            pr[p].kind = k < 2 ? PK_INT : k < 3 ? PK_LIST : PK_CMD;
            pr[p].rec = pr[p].kind == PK_INT && rnd(2);
            gen_proc(p);
        }
        ctx_t x;
        memset(&x, 0, sizeof x);
        x.proc = -1;
        x.readable = ((1u << 5) - 1) | (3u << V_L);
        /* every global gets a value first */
        top = -1;
        for (int v = 0; v < NV; v++) {
            if (v >= V_I && v < V_L) continue;
            int m = mk(S_MAKE);
            nd[m].var = v;
            if (IS_L(v)) {
                nd[m].a[0] = mk(L_LIT);
                nd[nd[m].a[0]].v = nlit;
                litn[nlit] = 1 + (int) rnd(5);
                for (int i = 0; i < litn[nlit]; i++) lit[nlit][i] = (long long) rnd(30) - 5;
                nlit++;
            } else
                nd[m].a[0] = num((long long) rnd(40) - 10);
            top = append(top, m);
        }
        top = append(top, gen_block(&x, 4, 12));
        for (int i = 0; i < 4; i++) { /* show the final values */
            int s = mk(S_PRINT);
            nd[s].op = "ps"[rnd(2)];
            nd[s].n = 1;
            nd[s].a[0] = mk(i < 2 ? I_VAR : L_VAR);
            nd[nd[s].a[0]].var = i < 2 ? (int) rnd(5) : V_L + i - 2;
            top = append(top, s);
        }
        /* run it */
        ncell = steps = depth = printed = 0;
        rc = -1;
        memset(bound, 0, sizeof bound);
        if (setjmp(reject)) continue;
        lits_build();
        if (block(top) != R_NORMAL) continue;
        break;
    }
    /* print it */
    on = 0;
    out("; generated by gen_logo ");
    outi((long long) seed);
    out("\n");
    for (int p = 0; p < npr; p++) {
        kw("to", 0);
        out(" ");
        out(pr[p].name);
        for (int i = 0; i < pr[p].np; i++) {
            out(" :");
            var(pr[p].params[i]);
        }
        out("\n");
        for (int s = pr[p].body; s >= 0; s = nd[s].next) {
            out(rnd(2) ? "  " : "");
            ps(s, 2);
            out("\n");
        }
        kw("end", 0);
        out("\n");
    }
    for (int s = top; s >= 0; s = nd[s].next) {
        ps(s, 0);
        out(rnd(6) == 0 && nd[s].next >= 0 ? " " : "\n");
        if (rnd(10) == 0) out("\n");
    }
    if (on && outb[on - 1] != '\n') out("\n");
    fwrite(outb, 1, on, stdout);
    return 0;
}
