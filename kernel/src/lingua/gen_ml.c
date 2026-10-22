/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_ml.c — random program generator for the ML (Standard ML) and OCaml
 * differential tests (ref_ml.sh runs mlton and ocaml on the same programs
 * as zxlang and requires identical output).
 *
 * Programs are built as trees, then printed in either dialect. The
 * generator evaluates every expression it prints, with the dialect's own
 * division (SML div/mod floor, OCaml / and mod truncate): an expression
 * that would divide by zero, take hd or tl of an empty list, leave +-10^9
 * at any step, or build a list longer than 40 is thrown away and drawn
 * again. Each program is then well defined, its numbers stay far from the
 * 32-bit edge of MLton's Int and the references never raise.
 *
 * What it covers: global values (integers and lists), functions of two
 * parameters (tupled or curried in SML) calling earlier ones, let ... in,
 * case/match on an integer with a variable rule, if, comparisons,
 * andalso/orelse (&&, ||), not, the division operators, abs, min, max,
 * negation, recursive helpers (a non-tail sum, a tail-recursive loop and
 * gcd), lists built from literals, ::, @, rev, a recursive range and sum,
 * hd, tl, length, list equality, and printing of integers and booleans.
 * All expressions are pure, so the order of evaluation (left to right in
 * SML, right to left in OCaml) cannot change the output.
 *
 * Usage: gen_ml sml|ocaml <seed>   (writes the program to stdout) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng;
static unsigned rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (unsigned) (rng % n) : 0;
}

#define LIM   1000000000ll
#define LMAX  40
#define NODES 8192
#define VARS  512
#define NF    4

enum {
    /* integers */
    X_LIT,
    X_VAR,
    X_ADD,
    X_SUB,
    X_MUL,
    X_DIV,
    X_MOD,
    X_QUOT,
    X_REM,
    X_NEG,
    X_ABS,
    X_MIN,
    X_MAX,
    X_IF,
    X_LET,
    X_MATCH,
    X_CALL,
    X_SUMTO,
    X_LOOP,
    X_GCD,
    X_SUM,
    X_LEN,
    X_HD,
    /* booleans */
    B_LT,
    B_LE,
    B_GT,
    B_GE,
    B_EQ,
    B_NE,
    B_AND,
    B_OR,
    B_NOT,
    B_LEQ,
    B_NULL,
    B_TRUE,
    B_FALSE,
    /* lists */
    L_VAR,
    L_LIT,
    L_CONS,
    L_APP,
    L_REV,
    L_RANGE,
    L_TL
};

typedef struct {
    int k, a, b, c, v; /* children; v: variable or function */
    long long lit;
    int kid[4], nk;
} node_t;

typedef struct {
    int n;
    long long e[LMAX];
} list_t;

static node_t nd[NODES];
static int nn;
static int sml;
static int bad;

static long long ival[VARS];
static list_t lval[VARS];
static char vname[VARS][8];
static int nvars;

/* functions: params, body, form (SML: 1 tupled, 0 curried) */
static int fpa[NF], fpb[NF], fbody[NF], ftup[NF], nfun;

/* the integer and list variables in scope */
static int iscope[64], niscope, lscope[16], nlscope;

static int newnode(int k)
{
    if (nn >= NODES) {
        bad = 1;
        return 0;
    }
    memset(&nd[nn], 0, sizeof nd[nn]);
    nd[nn].k = k;
    return nn++;
}

static int newvar(char c)
{
    int v = nvars++;
    snprintf(vname[v], sizeof vname[v], "%c%d", c, v);
    return v;
}

/* ------------------------------------------------------------ evaluation */

static long long chk(long long v)
{
    if (v > LIM || v < -LIM) bad = 1;
    return bad ? 0 : v;
}

static long long fdiv(long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static long long ev(int e);
static int evb(int e);
static list_t evl(int e);

static long long ev(int e)
{
    node_t *n = &nd[e];
    long long a, b;
    if (bad) return 0;
    switch (n->k) {
    case X_LIT:
        return n->lit;
    case X_VAR:
        return ival[n->v];
    case X_ADD:
        a = ev(n->a);
        b = ev(n->b);
        return chk(a + b);
    case X_SUB:
        a = ev(n->a);
        b = ev(n->b);
        return chk(a - b);
    case X_MUL:
        a = ev(n->a);
        b = ev(n->b);
        if (bad) return 0;
        if (a != 0 && (b > LIM / (a < 0 ? -a : a) || b < -LIM / (a < 0 ? -a : a))) {
            bad = 1;
            return 0;
        }
        return chk(a * b);
    case X_DIV:
    case X_MOD:
    case X_QUOT:
    case X_REM:
        a = ev(n->a);
        b = ev(n->b);
        if (bad) return 0;
        if (b == 0) {
            bad = 1;
            return 0;
        }
        if (n->k == X_QUOT || (n->k == X_DIV && !sml)) return a / b;
        if (n->k == X_REM || (n->k == X_MOD && !sml)) return a % b;
        if (n->k == X_DIV) return fdiv(a, b);
        return a - fdiv(a, b) * b;
    case X_NEG:
        return chk(-ev(n->a));
    case X_ABS:
        a = ev(n->a);
        return a < 0 ? -a : a;
    case X_MIN:
    case X_MAX:
        a = ev(n->a);
        b = ev(n->b);
        return (n->k == X_MIN) == (a < b) ? a : b;
    case X_IF:
        return evb(n->a) ? ev(n->b) : ev(n->c);
    case X_LET:
        ival[n->v] = ev(n->a);
        return ev(n->b);
    case X_MATCH:
        a = ev(n->a);
        if (a == n->lit) return ev(n->b);
        ival[n->v] = a;
        return ev(n->c);
    case X_CALL: {
        a = ev(n->a);
        b = ev(n->b);
        if (bad) return 0;
        ival[fpa[n->v]] = a;
        ival[fpb[n->v]] = b;
        return ev(fbody[n->v]);
    }
    case X_SUMTO:
        a = ev(n->a);
        if (a > 100) bad = 1;
        return a <= 0 ? 0 : a * (a + 1) / 2;
    case X_LOOP:
        a = ev(n->a);
        if (a < 0 || a > 20000) {
            bad = 1;
            return 0;
        }
        b = 0;
        for (long long i = a; i > 0; i--) b += i % 7;
        return chk(b);
    case X_GCD:
        a = ev(n->a);
        b = ev(n->b);
        while (b != 0) {
            long long r = sml ? a - fdiv(a, b) * b : a % b;
            a = b;
            b = r;
        }
        return a < 0 ? -a : a;
    case X_SUM: {
        list_t l = evl(n->a);
        long long s = 0;
        for (int i = l.n - 1; i >= 0; i--) s = chk(s + l.e[i]);
        return s;
    }
    case X_LEN:
        return evl(n->a).n;
    case X_HD: {
        list_t l = evl(n->a);
        if (l.n == 0) bad = 1;
        return bad ? 0 : l.e[0];
    }
    }
    bad = 1;
    return 0;
}

static int evb(int e)
{
    node_t *n = &nd[e];
    long long a, b;
    if (bad) return 0;
    switch (n->k) {
    case B_LT:
    case B_LE:
    case B_GT:
    case B_GE:
    case B_EQ:
    case B_NE:
        a = ev(n->a);
        b = ev(n->b);
        switch (n->k) {
        case B_LT:
            return a < b;
        case B_LE:
            return a <= b;
        case B_GT:
            return a > b;
        case B_GE:
            return a >= b;
        case B_EQ:
            return a == b;
        default:
            return a != b;
        }
    case B_AND:
        return evb(n->a) && evb(n->b);
    case B_OR:
        return evb(n->a) || evb(n->b);
    case B_NOT:
        return !evb(n->a);
    case B_LEQ: {
        list_t x = evl(n->a), y = evl(n->b);
        return x.n == y.n && memcmp(x.e, y.e, sizeof x.e[0] * (size_t) x.n) == 0;
    }
    case B_NULL:
        return evl(n->a).n == 0;
    case B_TRUE:
        return 1;
    case B_FALSE:
        return 0;
    }
    bad = 1;
    return 0;
}

static list_t evl(int e)
{
    node_t *n = &nd[e];
    list_t r = {0, {0}}, x, y;
    if (bad) return r;
    switch (n->k) {
    case L_VAR:
        return lval[n->v];
    case L_LIT:
        for (int i = 0; i < n->nk; i++) r.e[i] = ev(n->kid[i]);
        r.n = n->nk;
        return r;
    case L_CONS:
        x.e[0] = ev(n->a);
        y = evl(n->b);
        if (y.n >= LMAX) {
            bad = 1;
            return r;
        }
        r.e[0] = x.e[0];
        memcpy(r.e + 1, y.e, sizeof y.e[0] * (size_t) y.n);
        r.n = y.n + 1;
        return r;
    case L_APP:
        x = evl(n->a);
        y = evl(n->b);
        if (x.n + y.n > LMAX) {
            bad = 1;
            return r;
        }
        r = x;
        memcpy(r.e + x.n, y.e, sizeof y.e[0] * (size_t) y.n);
        r.n = x.n + y.n;
        return r;
    case L_REV:
        x = evl(n->a);
        for (int i = 0; i < x.n; i++) r.e[i] = x.e[x.n - 1 - i];
        r.n = x.n;
        return r;
    case L_RANGE: {
        long long a = ev(n->a), b = ev(n->b);
        if (bad || b - a + 1 > LMAX) {
            bad = 1;
            return r;
        }
        for (long long i = a; i <= b; i++) r.e[r.n++] = i;
        return r;
    }
    case L_TL:
        x = evl(n->a);
        if (x.n == 0) {
            bad = 1;
            return r;
        }
        memcpy(r.e, x.e + 1, sizeof x.e[0] * (size_t) (x.n - 1));
        r.n = x.n - 1;
        return r;
    }
    bad = 1;
    return r;
}

/* ------------------------------------------------------------ generation */

static int gint(int d);
static int gbool(int d);
static int glist(int d);

static int lit(long long v)
{
    int e = newnode(X_LIT);
    nd[e].lit = v;
    return e;
}

static int gint(int d)
{
    unsigned r = rnd(d <= 0 ? 3 : 26);
    int e;
    if (r == 0 || (r <= 2 && niscope == 0)) return lit((long long) rnd(41) - 20);
    if (r <= 2) {
        e = newnode(X_VAR);
        nd[e].v = iscope[rnd((unsigned) niscope)];
        return e;
    }
    switch (r) {
    case 3:
    case 4:
        e = newnode(X_ADD);
        break;
    case 5:
        e = newnode(X_SUB);
        break;
    case 6:
        e = newnode(X_MUL);
        break;
    case 7:
        e = newnode(X_DIV);
        break;
    case 8:
        e = newnode(X_MOD);
        break;
    case 9:
        e = newnode(sml ? X_QUOT : X_MIN);
        break;
    case 10:
        e = newnode(sml ? X_REM : X_MAX);
        break;
    case 11:
        e = newnode(X_MIN);
        break;
    case 12:
        e = newnode(X_MAX);
        break;
    case 13:
        e = newnode(X_NEG);
        nd[e].a = gint(d - 1);
        return e;
    case 14:
        e = newnode(X_ABS);
        nd[e].a = gint(d - 1);
        return e;
    case 15:
        e = newnode(X_IF);
        nd[e].a = gbool(d - 1);
        nd[e].b = gint(d - 1);
        nd[e].c = gint(d - 1);
        return e;
    case 16: {
        e = newnode(X_LET);
        nd[e].a = gint(d - 1);
        nd[e].v = newvar('v');
        iscope[niscope++] = nd[e].v;
        nd[e].b = gint(d - 1);
        niscope--;
        return e;
    }
    case 17: {
        e = newnode(X_MATCH);
        nd[e].a = gint(d - 1);
        nd[e].lit = (long long) rnd(5) - 1;
        nd[e].b = gint(d - 1);
        nd[e].v = newvar('m');
        iscope[niscope++] = nd[e].v;
        nd[e].c = gint(d - 1);
        niscope--;
        return e;
    }
    case 18:
    case 19:
        if (nfun == 0) return lit((long long) rnd(9));
        e = newnode(X_CALL);
        nd[e].v = (int) rnd((unsigned) nfun);
        break;
    case 20:
        e = newnode(X_SUMTO);
        nd[e].a = gint(d - 1);
        return e;
    case 21:
        e = newnode(X_LOOP);
        nd[e].a = gint(d - 1);
        return e;
    case 22:
        e = newnode(X_GCD);
        break;
    case 23:
        e = newnode(X_SUM);
        nd[e].a = glist(d - 1);
        return e;
    case 24:
        e = newnode(X_LEN);
        nd[e].a = glist(d - 1);
        return e;
    default:
        e = newnode(X_HD);
        nd[e].a = glist(d - 1);
        return e;
    }
    nd[e].a = gint(d - 1);
    nd[e].b = gint(d - 1);
    return e;
}

static int gbool(int d)
{
    unsigned r = rnd(d <= 0 ? 7 : 13);
    int e;
    if (r < 6) {
        e = newnode(B_LT + (int) r);
        nd[e].a = gint(d - 1);
        nd[e].b = gint(d - 1);
        return e;
    }
    switch (r) {
    case 6:
        return newnode(rnd(2) ? B_TRUE : B_FALSE);
    case 7:
    case 8:
        e = newnode(r == 7 ? B_AND : B_OR);
        nd[e].a = gbool(d - 1);
        nd[e].b = gbool(d - 1);
        return e;
    case 9:
        e = newnode(B_NOT);
        nd[e].a = gbool(d - 1);
        return e;
    case 10:
    case 11:
        e = newnode(B_LEQ);
        nd[e].a = glist(d - 1);
        nd[e].b = glist(d - 1);
        return e;
    default:
        e = newnode(B_NULL);
        nd[e].a = glist(d - 1);
        return e;
    }
}

static int glist(int d)
{
    unsigned r = rnd(d <= 0 ? 2 : 8);
    int e;
    if (r == 0 && nlscope > 0) {
        e = newnode(L_VAR);
        nd[e].v = lscope[rnd((unsigned) nlscope)];
        return e;
    }
    if (r <= 1) {
        e = newnode(L_LIT);
        nd[e].nk = 1 + (int) rnd(3);
        for (int i = 0; i < nd[e].nk; i++) nd[e].kid[i] = gint(d - 1);
        return e;
    }
    switch (r) {
    case 2:
        e = newnode(L_CONS);
        nd[e].a = gint(d - 1);
        nd[e].b = glist(d - 1);
        return e;
    case 3:
        e = newnode(L_APP);
        nd[e].a = glist(d - 1);
        nd[e].b = glist(d - 1);
        return e;
    case 4:
        e = newnode(L_REV);
        nd[e].a = glist(d - 1);
        return e;
    case 5:
    case 6:
        e = newnode(L_RANGE);
        nd[e].a = gint(d - 1);
        nd[e].b = gint(d - 1);
        return e;
    default:
        e = newnode(L_TL);
        nd[e].a = glist(d - 1);
        return e;
    }
}

/* ------------------------------------------------------------ printing */

static void pint(int e);
static void pbool(int e);
static void plist(int e);

static void pbin(int e, const char *op)
{
    printf("(");
    pint(nd[e].a);
    printf(" %s ", op);
    pint(nd[e].b);
    printf(")");
}

/* f (a, b) in SML, f a b in OCaml and curried SML */
static void pcall2(const char *f, int a, int b, int tupled)
{
    printf("(%s ", f);
    if (tupled) {
        printf("(");
        pint(a);
        printf(", ");
        pint(b);
        printf(")");
    } else {
        pint(a);
        printf(" ");
        pint(b);
    }
    printf(")");
}

static void pint(int e)
{
    node_t *n = &nd[e];
    switch (n->k) {
    case X_LIT:
        if (n->lit < 0)
            printf(sml ? "~%lld" : "(-%lld)", -n->lit);
        else
            printf("%lld", n->lit);
        return;
    case X_VAR:
        printf("%s", vname[n->v]);
        return;
    case X_ADD:
        pbin(e, "+");
        return;
    case X_SUB:
        pbin(e, "-");
        return;
    case X_MUL:
        pbin(e, "*");
        return;
    case X_DIV:
        pbin(e, sml ? "div" : "/");
        return;
    case X_MOD:
        pbin(e, "mod");
        return;
    case X_QUOT:
        pcall2("Int.quot", n->a, n->b, 1);
        return;
    case X_REM:
        pcall2("Int.rem", n->a, n->b, 1);
        return;
    case X_MIN:
        pcall2(sml ? "Int.min" : "min", n->a, n->b, sml);
        return;
    case X_MAX:
        pcall2(sml ? "Int.max" : "max", n->a, n->b, sml);
        return;
    case X_NEG:
        printf(sml ? "(~ " : "(- ");
        pint(n->a);
        printf(")");
        return;
    case X_ABS:
        printf("(abs ");
        pint(n->a);
        printf(")");
        return;
    case X_IF:
        printf("(if ");
        pbool(n->a);
        printf(" then ");
        pint(n->b);
        printf(" else ");
        pint(n->c);
        printf(")");
        return;
    case X_LET:
        printf(sml ? "(let val %s = " : "(let %s = ", vname[n->v]);
        pint(n->a);
        printf(" in ");
        pint(n->b);
        printf(sml ? " end)" : ")");
        return;
    case X_MATCH:
        printf(sml ? "(case " : "(match ");
        pint(n->a);
        if (n->lit < 0)
            printf(sml ? " of ~%lld => " : " with -%lld -> ", -n->lit);
        else
            printf(sml ? " of %lld => " : " with %lld -> ", n->lit);
        pint(n->b);
        printf(sml ? " | %s => " : " | %s -> ", vname[n->v]);
        pint(n->c);
        printf(")");
        return;
    case X_CALL: {
        char f[8];
        snprintf(f, sizeof f, "f%d", n->v);
        pcall2(f, n->a, n->b, sml && ftup[n->v]);
        return;
    }
    case X_SUMTO:
        printf("(sumto ");
        pint(n->a);
        printf(")");
        return;
    case X_LOOP:
        if (sml) {
            printf("(loop (");
            pint(n->a);
            printf(", 0))");
        } else {
            printf("(loop ");
            pint(n->a);
            printf(" 0)");
        }
        return;
    case X_GCD:
        pcall2("gcd", n->a, n->b, sml);
        return;
    case X_SUM:
        printf("(sum ");
        plist(n->a);
        printf(")");
        return;
    case X_LEN:
        printf(sml ? "(length " : "(List.length ");
        plist(n->a);
        printf(")");
        return;
    case X_HD:
        printf(sml ? "(hd " : "(List.hd ");
        plist(n->a);
        printf(")");
        return;
    }
}

static void pbool(int e)
{
    static const char *const cmp[] = {"<", "<=", ">", ">=", "=", "<>"};
    node_t *n = &nd[e];
    switch (n->k) {
    case B_TRUE:
        printf("true");
        return;
    case B_FALSE:
        printf("false");
        return;
    case B_AND:
    case B_OR:
        printf("(");
        pbool(n->a);
        printf(n->k == B_AND ? (sml ? " andalso " : " && ") : (sml ? " orelse " : " || "));
        pbool(n->b);
        printf(")");
        return;
    case B_NOT:
        printf("(not ");
        pbool(n->a);
        printf(")");
        return;
    case B_LEQ:
        printf("(");
        plist(n->a);
        printf(" = ");
        plist(n->b);
        printf(")");
        return;
    case B_NULL:
        printf("(");
        plist(n->a);
        printf(" = [])");
        return;
    default:
        printf("(");
        pint(n->a);
        printf(" %s ", cmp[n->k - B_LT]);
        pint(n->b);
        printf(")");
        return;
    }
}

static void plist(int e)
{
    node_t *n = &nd[e];
    switch (n->k) {
    case L_VAR:
        printf("%s", vname[n->v]);
        return;
    case L_LIT:
        printf("[");
        for (int i = 0; i < n->nk; i++) {
            if (i) printf(sml ? ", " : "; ");
            pint(n->kid[i]);
        }
        printf("]");
        return;
    case L_CONS:
        printf("(");
        pint(n->a);
        printf(" :: ");
        plist(n->b);
        printf(")");
        return;
    case L_APP:
        printf("(");
        plist(n->a);
        printf(" @ ");
        plist(n->b);
        printf(")");
        return;
    case L_REV:
        printf(sml ? "(rev " : "(List.rev ");
        plist(n->a);
        printf(")");
        return;
    case L_RANGE:
        pcall2("range", n->a, n->b, sml);
        return;
    case L_TL:
        printf(sml ? "(tl " : "(List.tl ");
        plist(n->a);
        printf(")");
        return;
    }
}

/* ------------------------------------------------------------ statements */

/* an integer, boolean or list expression that evaluates without trouble */
static int good_int(int d)
{
    for (int t = 0; t < 200; t++) {
        int mark = nn, nv = nvars;
        bad = 0;
        int e = gint(d);
        if (!bad) ev(e);
        if (!bad) return e;
        nn = mark;
        nvars = nv;
    }
    bad = 0;
    return lit((long long) rnd(10));
}

static int good_bool(int d)
{
    for (int t = 0; t < 200; t++) {
        int mark = nn, nv = nvars;
        bad = 0;
        int e = gbool(d);
        if (!bad) evb(e);
        if (!bad) return e;
        nn = mark;
        nvars = nv;
    }
    bad = 0;
    return newnode(B_TRUE);
}

static int good_list(int d)
{
    for (int t = 0; t < 200; t++) {
        int mark = nn, nv = nvars;
        bad = 0;
        int e = glist(d);
        if (!bad) evl(e);
        if (!bad) return e;
        nn = mark;
        nvars = nv;
    }
    bad = 0;
    int e = newnode(L_LIT);
    nd[e].nk = 1;
    nd[e].kid[0] = lit(1);
    return e;
}

static void prelude(void)
{
    if (sml) {
        printf("fun range (a, b) = if a > b then [] else a :: range (a + 1, b)\n");
        printf("fun sum [] = 0\n  | sum (x :: xs) = x + sum xs\n");
        printf("fun sumto n = if n <= 0 then 0 else n + sumto (n - 1)\n");
        printf("fun loop (0, acc) = acc\n  | loop (i, acc) = loop (i - 1, acc + i mod 7)\n");
        printf("fun gcd (a, 0) = abs a\n  | gcd (a, b) = gcd (b, a mod b)\n");
    } else {
        printf("let rec range a b = if a > b then [] else a :: range (a + 1) b\n");
        printf("let rec sum = function [] -> 0 | x :: xs -> x + sum xs\n");
        printf("let rec sumto n = if n <= 0 then 0 else n + sumto (n - 1)\n");
        printf("let rec loop i acc = if i = 0 then acc else loop (i - 1) (acc + i mod 7)\n");
        printf("let rec gcd a b = match b with 0 -> abs a | _ -> gcd b (a mod b)\n");
    }
}

/* print one to three integers or booleans on a line */
static void print_stmt(void)
{
    int k = 1 + (int) rnd(3), e[3], isb[3];
    for (int i = 0; i < k; i++) {
        isb[i] = rnd(4) == 0;
        e[i] = isb[i] ? good_bool(2) : good_int(3);
    }
    if (sml) {
        printf("val _ = print (");
        for (int i = 0; i < k; i++) {
            if (i) printf(" ^ \" \" ^ ");
            printf(isb[i] ? "Bool.toString " : "Int.toString ");
            if (isb[i])
                pbool(e[i]);
            else
                pint(e[i]);
        }
        printf(" ^ \"\\n\")\n");
        return;
    }
    unsigned form = rnd(3);
    if (k == 1 && !isb[0] && form == 0) {
        printf("let () = print_int ");
        pint(e[0]);
        printf("; print_newline ()\n");
    } else if (k == 1 && form == 1) {
        printf("let () = print_endline (%s ", isb[0] ? "string_of_bool" : "string_of_int");
        if (isb[0])
            pbool(e[0]);
        else
            pint(e[0]);
        printf(")\n");
    } else {
        printf("let () = Printf.printf \"");
        for (int i = 0; i < k; i++) printf(i ? " %%%c" : "%%%c", isb[i] ? 'b' : 'd');
        printf("\\n\"");
        for (int i = 0; i < k; i++) {
            printf(" ");
            if (isb[i])
                pbool(e[i]);
            else
                pint(e[i]);
        }
        printf("\n");
    }
}

static void print_list_stmt(int l)
{
    /* the elements, one at a time, through a local recursive function */
    if (sml)
        printf("val _ = let fun pr [] = print \"\\n\" | pr (x :: r) = (print (Int.toString x ^ \" "
               "\"); pr r) in pr %s end\n",
               vname[l]);
    else
        printf("let () = let rec pr = function [] -> print_newline () | x :: r -> print_int x; "
               "print_string \" \"; pr r in pr %s\n",
               vname[l]);
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "sml") && strcmp(argv[1], "ocaml"))) {
        fprintf(stderr, "usage: gen_ml sml|ocaml <seed>\n");
        return 2;
    }
    sml = argv[1][0] == 's';
    rng = 0x9e3779b97f4a7c15ull ^ ((unsigned long long) strtoul(argv[2], 0, 10) * 2654435761ull);
    for (int i = 0; i < 4; i++) rnd(1);
    printf("(* generated program, seed %s *)\n", argv[2]);
    prelude();
    int nstmt = 10 + (int) rnd(8);
    for (int s = 0; s < nstmt; s++) {
        unsigned r = rnd(10);
        if (r < 2 && niscope < 60) {
            int e = good_int(3), v = newvar('g');
            ival[v] = ev(e);
            printf(sml ? "val %s = " : "let %s = ", vname[v]);
            pint(e);
            printf("\n");
            iscope[niscope++] = v;
        } else if (r == 2 && nlscope < 16) {
            int e = good_list(3), v = newvar('l');
            lval[v] = evl(e);
            printf(sml ? "val %s = " : "let %s = ", vname[v]);
            plist(e);
            printf("\n");
            lscope[nlscope++] = v;
            print_list_stmt(v);
        } else if (r == 3 && nfun < NF) {
            /* a function of two parameters; its body sees the globals so far */
            int f = nfun, save = niscope;
            fpa[f] = newvar('a');
            fpb[f] = newvar('b');
            ftup[f] = (int) rnd(2);
            iscope[niscope++] = fpa[f];
            iscope[niscope++] = fpb[f];
            bad = 0;
            fbody[f] = gint(3);
            if (bad) fbody[f] = lit(1);
            bad = 0;
            niscope = save;
            if (sml && ftup[f])
                printf("fun f%d (%s, %s) = ", f, vname[fpa[f]], vname[fpb[f]]);
            else
                printf(sml ? "fun f%d %s %s = " : "let f%d %s %s = ", f, vname[fpa[f]],
                       vname[fpb[f]]);
            pint(fbody[f]);
            printf("\n");
            nfun++;
        } else {
            print_stmt();
        }
    }
    return 0;
}
