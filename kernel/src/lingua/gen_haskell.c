/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_haskell.c — random program generator for the Haskell differential
 * test (ref_haskell.sh runs ghc on the same programs as zxlang and
 * requires identical output).
 *
 * Programs are built as trees and printed as Haskell. The generator
 * evaluates every expression it prints, strictly (every argument and
 * every binding, only the branch an if, case, guard, && or || takes): an
 * expression that would divide by zero, take head, tail, last, init,
 * maximum or minimum of an empty list, index out of range, leave +-10^9 at
 * any step or build a list longer than 40 is thrown away and drawn again.
 * Each program is then well defined under both call by value and GHC's
 * lazy evaluation, its numbers stay far from the edges of Int, and the
 * reference never stops on an error.
 *
 * What it covers: top-level values and do-block lets (integers and
 * lists), functions of two parameters with signatures (a plain equation,
 * guards with otherwise, a where binding, a literal first pattern),
 * recursive helpers (a non-tail sum, an accumulator loop, a list length
 * and a pairwise sum on list patterns), let ... in, case ... of with a
 * literal and a variable alternative, if, comparisons, && || not, even,
 * odd, div mod quot rem (infix and prefix), ^, negate, abs, signum, min,
 * max, gcd, lists from literals, [a..b] and [a,b..c], :, ++, reverse,
 * take, drop, tail, init, replicate, concat, head, last, !!, length, sum,
 * product, maximum, minimum, null, elem, list == and <, and output by
 * print (integers, booleans, lists and tuples), putStrLn and putStr with
 * show and ++.
 *
 * Usage: gen_haskell <seed>   (writes the program to stdout) */
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
    X_POW,
    X_NEG,
    X_ABS,
    X_SIGNUM,
    X_MIN,
    X_MAX,
    X_IF,
    X_LET,
    X_CASE,
    X_CALL,
    X_SUMTO,
    X_LOOP,
    X_GCD,
    X_SUM,
    X_PRODUCT,
    X_MAXIMUM,
    X_MINIMUM,
    X_LEN,
    X_MYLEN,
    X_HEAD,
    X_LAST,
    X_INDEX,
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
    B_EVEN,
    B_ODD,
    B_LEQ,
    B_LLT,
    B_NULL,
    B_ELEM,
    B_TRUE,
    B_FALSE,
    /* lists */
    L_VAR,
    L_LIT,
    L_CONS,
    L_APP,
    L_REV,
    L_RANGE,
    L_RANGE3,
    L_TAIL,
    L_INIT,
    L_TAKE,
    L_DROP,
    L_REPL,
    L_CONCAT,
    L_PAIRS
};

typedef struct {
    int k, a, b, c, v; /* children; v: variable, function or form */
    long long lit;
    int kid[4], nk;
} node_t;

typedef struct {
    int n;
    long long e[LMAX];
} list_t;

static node_t nd[NODES];
static int nn;
static int bad;

static long long ival[VARS];
static list_t lval[VARS];
static char vname[VARS][8];
static int nvars;

/* functions fK a b: params, form, bodies (form 1: guard, else; form 2: the
 * where variable and its value; form 3: the body when a is 0) */
static int fpa[NF], fpb[NF], fform[NF], fbody[NF], fx[NF], fy[NF], fwv[NF], nfun;

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
        if (n->k == X_QUOT) return a / b;
        if (n->k == X_REM) return a % b;
        if (n->k == X_DIV) return fdiv(a, b);
        return a - fdiv(a, b) * b;
    case X_POW: {
        a = ev(n->a);
        b = ev(n->b);
        if (bad || b < 0 || b > 40) {
            bad = 1;
            return 0;
        }
        long long r = 1;
        for (long long i = 0; i < b && !bad; i++) {
            if (a != 0 && (r > LIM / (a < 0 ? -a : a) || r < -LIM / (a < 0 ? -a : a))) bad = 1;
            r = chk(r * a);
        }
        return r;
    }
    case X_NEG:
        return chk(-ev(n->a));
    case X_ABS:
        a = ev(n->a);
        return a < 0 ? -a : a;
    case X_SIGNUM:
        a = ev(n->a);
        return (a > 0) - (a < 0);
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
    case X_CASE:
        a = ev(n->a);
        if (a == n->lit) return ev(n->b);
        ival[n->v] = a;
        return ev(n->c);
    case X_CALL: {
        int f = n->v;
        a = ev(n->a);
        b = ev(n->b);
        if (bad) return 0;
        ival[fpa[f]] = a;
        ival[fpb[f]] = b;
        switch (fform[f]) {
        case 1:
            return evb(fbody[f]) ? ev(fx[f]) : ev(fy[f]);
        case 2:
            ival[fwv[f]] = ev(fx[f]);
            return ev(fbody[f]);
        case 3:
            return a == 0 ? ev(fx[f]) : ev(fbody[f]);
        default:
            return ev(fbody[f]);
        }
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
        a = a < 0 ? -a : a;
        b = b < 0 ? -b : b;
        while (b != 0) {
            long long r = a % b;
            a = b;
            b = r;
        }
        return a;
    case X_SUM:
    case X_PRODUCT: {
        list_t l = evl(n->a);
        long long s = n->k == X_SUM ? 0 : 1;
        for (int i = 0; i < l.n && !bad; i++) {
            if (n->k == X_SUM) {
                s = chk(s + l.e[i]);
            } else {
                long long x = l.e[i];
                if (x != 0 && (s > LIM / (x < 0 ? -x : x) || s < -LIM / (x < 0 ? -x : x))) bad = 1;
                s = chk(s * x);
            }
        }
        return s;
    }
    case X_MAXIMUM:
    case X_MINIMUM:
    case X_HEAD:
    case X_LAST: {
        list_t l = evl(n->a);
        if (bad || l.n == 0) {
            bad = 1;
            return 0;
        }
        if (n->k == X_HEAD) return l.e[0];
        if (n->k == X_LAST) return l.e[l.n - 1];
        long long m = l.e[0];
        for (int i = 1; i < l.n; i++)
            if (n->k == X_MAXIMUM ? l.e[i] > m : l.e[i] < m) m = l.e[i];
        return m;
    }
    case X_LEN:
    case X_MYLEN:
        return evl(n->a).n;
    case X_INDEX: {
        list_t l = evl(n->a);
        a = ev(n->b);
        if (bad || a < 0 || a >= l.n) {
            bad = 1;
            return 0;
        }
        return l.e[a];
    }
    }
    bad = 1;
    return 0;
}

static int lcmp(const list_t *x, const list_t *y)
{
    for (int i = 0; i < x->n && i < y->n; i++)
        if (x->e[i] != y->e[i]) return x->e[i] < y->e[i] ? -1 : 1;
    return (x->n > y->n) - (x->n < y->n);
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
        case B_LT: return a < b;
        case B_LE: return a <= b;
        case B_GT: return a > b;
        case B_GE: return a >= b;
        case B_EQ: return a == b;
        default: return a != b;
        }
    case B_AND:
        return evb(n->a) && evb(n->b);
    case B_OR:
        return evb(n->a) || evb(n->b);
    case B_NOT:
        return !evb(n->a);
    case B_EVEN:
        return ev(n->a) % 2 == 0;
    case B_ODD:
        return ev(n->a) % 2 != 0;
    case B_LEQ:
    case B_LLT: {
        list_t x = evl(n->a), y = evl(n->b);
        return n->k == B_LEQ ? lcmp(&x, &y) == 0 : lcmp(&x, &y) < 0;
    }
    case B_NULL:
        return evl(n->a).n == 0;
    case B_ELEM: {
        a = ev(n->a);
        list_t x = evl(n->b);
        for (int i = 0; i < x.n; i++)
            if (x.e[i] == a) return 1;
        return 0;
    }
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
    long long a, b, c;
    if (bad) return r;
    switch (n->k) {
    case L_VAR:
        return lval[n->v];
    case L_LIT:
        for (int i = 0; i < n->nk; i++) r.e[i] = ev(n->kid[i]);
        r.n = n->nk;
        return r;
    case L_CONS:
        a = ev(n->a);
        y = evl(n->b);
        if (y.n >= LMAX) {
            bad = 1;
            return r;
        }
        r.e[0] = a;
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
    case L_RANGE:
        a = ev(n->a);
        b = ev(n->b);
        if (bad || b - a + 1 > LMAX) {
            bad = 1;
            return r;
        }
        for (long long i = a; i <= b; i++) r.e[r.n++] = i;
        return r;
    case L_RANGE3:
        a = ev(n->a);
        b = ev(n->b);
        c = ev(n->c);
        if (bad || a == b) {
            bad = 1;
            return r;
        }
        for (long long i = a; b > a ? i <= c : i >= c; i += b - a) {
            if (r.n >= LMAX) {
                bad = 1;
                return r;
            }
            r.e[r.n++] = i;
        }
        return r;
    case L_TAIL:
    case L_INIT:
        x = evl(n->a);
        if (x.n == 0) {
            bad = 1;
            return r;
        }
        memcpy(r.e, x.e + (n->k == L_TAIL), sizeof x.e[0] * (size_t) (x.n - 1));
        r.n = x.n - 1;
        return r;
    case L_TAKE:
    case L_DROP:
        a = ev(n->a);
        x = evl(n->b);
        if (a < 0) a = 0;
        if (a > x.n) a = x.n;
        if (n->k == L_TAKE) {
            x.n = (int) a;
            return x;
        }
        memcpy(r.e, x.e + a, sizeof x.e[0] * (size_t) (x.n - a));
        r.n = x.n - (int) a;
        return r;
    case L_REPL:
        a = ev(n->a);
        b = ev(n->b);
        if (a > LMAX) {
            bad = 1;
            return r;
        }
        for (long long i = 0; i < a; i++) r.e[r.n++] = b;
        return r;
    case L_CONCAT:
        for (int i = 0; i < n->nk; i++) {
            x = evl(n->kid[i]);
            if (bad || r.n + x.n > LMAX) {
                bad = 1;
                return r;
            }
            memcpy(r.e + r.n, x.e, sizeof x.e[0] * (size_t) x.n);
            r.n += x.n;
        }
        return r;
    case L_PAIRS:
        x = evl(n->a);
        for (int i = 0; i < x.n && !bad; i += 2) r.e[r.n++] = i + 1 < x.n ? chk(x.e[i] + x.e[i + 1]) : x.e[i];
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

static int one(int k, int a)
{
    int e = newnode(k);
    nd[e].a = a;
    return e;
}

static int two(int k, int a, int b)
{
    int e = newnode(k);
    nd[e].a = a;
    nd[e].b = b;
    return e;
}

static int gint(int d)
{
    unsigned r = rnd(d <= 0 ? 3 : 36);
    int e;
    if (r == 0 || (r <= 2 && niscope == 0)) return lit((long long) rnd(41) - 20);
    if (r <= 2) {
        e = newnode(X_VAR);
        nd[e].v = iscope[rnd((unsigned) niscope)];
        return e;
    }
    switch (r) {
    case 3:
    case 4: return two(X_ADD, gint(d - 1), gint(d - 1));
    case 5: return two(X_SUB, gint(d - 1), gint(d - 1));
    case 6: return two(X_MUL, gint(d - 1), gint(d - 1));
    case 7:
        e = two(X_DIV, gint(d - 1), gint(d - 1));
        nd[e].v = (int) rnd(2); /* prefix or infix */
        return e;
    case 8:
        e = two(X_MOD, gint(d - 1), gint(d - 1));
        nd[e].v = (int) rnd(2);
        return e;
    case 9:
        e = two(X_QUOT, gint(d - 1), gint(d - 1));
        nd[e].v = (int) rnd(2);
        return e;
    case 10:
        e = two(X_REM, gint(d - 1), gint(d - 1));
        nd[e].v = (int) rnd(2);
        return e;
    case 11: return two(X_POW, gint(d - 1), lit((long long) rnd(4)));
    case 12: return two(X_MIN, gint(d - 1), gint(d - 1));
    case 13: return two(X_MAX, gint(d - 1), gint(d - 1));
    case 14:
        e = one(X_NEG, gint(d - 1));
        nd[e].v = (int) rnd(2); /* negate or - */
        return e;
    case 15: return one(X_ABS, gint(d - 1));
    case 16: return one(X_SIGNUM, gint(d - 1));
    case 17:
        e = newnode(X_IF);
        nd[e].a = gbool(d - 1);
        nd[e].b = gint(d - 1);
        nd[e].c = gint(d - 1);
        return e;
    case 18: {
        e = newnode(X_LET);
        nd[e].a = gint(d - 1);
        nd[e].v = newvar('v');
        iscope[niscope++] = nd[e].v;
        nd[e].b = gint(d - 1);
        niscope--;
        return e;
    }
    case 19: {
        e = newnode(X_CASE);
        nd[e].a = gint(d - 1);
        nd[e].lit = (long long) rnd(5) - 1;
        nd[e].b = gint(d - 1);
        nd[e].v = newvar('m');
        iscope[niscope++] = nd[e].v;
        nd[e].c = gint(d - 1);
        niscope--;
        return e;
    }
    case 20:
    case 21:
        if (nfun == 0) return lit((long long) rnd(9));
        e = two(X_CALL, gint(d - 1), gint(d - 1));
        nd[e].v = (int) rnd((unsigned) nfun);
        return e;
    case 22: return one(X_SUMTO, gint(d - 1));
    case 23: return one(X_LOOP, gint(d - 1));
    case 24: return two(X_GCD, gint(d - 1), gint(d - 1));
    case 25: return one(X_SUM, glist(d - 1));
    case 26: return one(X_PRODUCT, glist(d - 1));
    case 27: return one(X_MAXIMUM, glist(d - 1));
    case 28: return one(X_MINIMUM, glist(d - 1));
    case 29: return one(X_LEN, glist(d - 1));
    case 30: return one(X_MYLEN, glist(d - 1));
    case 31: return one(X_HEAD, glist(d - 1));
    case 32: return one(X_LAST, glist(d - 1));
    case 33: return two(X_INDEX, glist(d - 1), gint(d - 1));
    default: return two(X_ADD, gint(d - 1), gint(d - 1));
    }
}

static int gbool(int d)
{
    unsigned r = rnd(d <= 0 ? 7 : 17);
    if (r < 6) return two(B_LT + (int) r, gint(d - 1), gint(d - 1));
    switch (r) {
    case 6: return newnode(rnd(2) ? B_TRUE : B_FALSE);
    case 7:
    case 8: return two(r == 7 ? B_AND : B_OR, gbool(d - 1), gbool(d - 1));
    case 9: return one(B_NOT, gbool(d - 1));
    case 10: return one(B_EVEN, gint(d - 1));
    case 11: return one(B_ODD, gint(d - 1));
    case 12: return two(B_LEQ, glist(d - 1), glist(d - 1));
    case 13: return two(B_LLT, glist(d - 1), glist(d - 1));
    case 14: return one(B_NULL, glist(d - 1));
    default: return two(B_ELEM, gint(d - 1), glist(d - 1));
    }
}

static int glist(int d)
{
    unsigned r = rnd(d <= 0 ? 2 : 17);
    int e;
    if (r == 0 && nlscope > 0) {
        e = newnode(L_VAR);
        nd[e].v = lscope[rnd((unsigned) nlscope)];
        return e;
    }
    if (r <= 1) {
        e = newnode(L_LIT);
        nd[e].nk = 1 + (int) rnd(4);
        for (int i = 0; i < nd[e].nk; i++) nd[e].kid[i] = gint(d - 1);
        return e;
    }
    switch (r) {
    case 2: return two(L_CONS, gint(d - 1), glist(d - 1));
    case 3: return two(L_APP, glist(d - 1), glist(d - 1));
    case 4: return one(L_REV, glist(d - 1));
    case 5:
    case 6: return two(L_RANGE, gint(d - 1), gint(d - 1));
    case 7:
        e = two(L_RANGE3, gint(d - 1), gint(d - 1));
        nd[e].c = gint(d - 1);
        return e;
    case 8: return one(L_TAIL, glist(d - 1));
    case 9: return one(L_INIT, glist(d - 1));
    case 10: return two(L_TAKE, gint(d - 1), glist(d - 1));
    case 11: return two(L_DROP, gint(d - 1), glist(d - 1));
    case 12: return two(L_REPL, lit((long long) rnd(5)), gint(d - 1));
    case 13:
        e = newnode(L_CONCAT);
        nd[e].nk = 1 + (int) rnd(3);
        for (int i = 0; i < nd[e].nk; i++) nd[e].kid[i] = glist(d - 1);
        return e;
    case 14: return one(L_PAIRS, glist(d - 1));
    default:
        if (nlscope > 0) {
            e = newnode(L_VAR);
            nd[e].v = lscope[rnd((unsigned) nlscope)];
            return e;
        }
        return two(L_CONS, gint(d - 1), glist(d - 1));
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

static void pcall2(const char *f, int a, int b)
{
    printf("(%s ", f);
    pint(a);
    printf(" ");
    pint(b);
    printf(")");
}

static void pcall1(const char *f, int a, void (*p)(int))
{
    printf("(%s ", f);
    p(a);
    printf(")");
}

static void pdiv(int e, const char *op)
{
    if (nd[e].v) {
        char b[16];
        snprintf(b, sizeof b, "`%s`", op);
        pbin(e, b);
    } else {
        pcall2(op, nd[e].a, nd[e].b);
    }
}

static void pint(int e)
{
    node_t *n = &nd[e];
    switch (n->k) {
    case X_LIT:
        if (n->lit < 0)
            printf("(-%lld)", -n->lit);
        else
            printf("%lld", n->lit);
        return;
    case X_VAR: printf("%s", vname[n->v]); return;
    case X_ADD: pbin(e, "+"); return;
    case X_SUB: pbin(e, "-"); return;
    case X_MUL: pbin(e, "*"); return;
    case X_POW: pbin(e, "^"); return;
    case X_DIV: pdiv(e, "div"); return;
    case X_MOD: pdiv(e, "mod"); return;
    case X_QUOT: pdiv(e, "quot"); return;
    case X_REM: pdiv(e, "rem"); return;
    case X_MIN: pcall2("min", n->a, n->b); return;
    case X_MAX: pcall2("max", n->a, n->b); return;
    case X_GCD: pcall2("gcd", n->a, n->b); return;
    case X_NEG:
        if (n->v) {
            printf("(- ");
            pint(n->a);
            printf(")");
        } else {
            pcall1("negate", n->a, pint);
        }
        return;
    case X_ABS: pcall1("abs", n->a, pint); return;
    case X_SIGNUM: pcall1("signum", n->a, pint); return;
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
        printf("(let %s = ", vname[n->v]);
        pint(n->a);
        printf(" in ");
        pint(n->b);
        printf(")");
        return;
    case X_CASE:
        printf("(case ");
        pint(n->a);
        printf(n->lit < 0 ? " of { (%lld) -> " : " of { %lld -> ", n->lit);
        pint(n->b);
        printf("; %s -> ", vname[n->v]);
        pint(n->c);
        printf(" })");
        return;
    case X_CALL: {
        char f[8];
        snprintf(f, sizeof f, "f%d", n->v);
        pcall2(f, n->a, n->b);
        return;
    }
    case X_SUMTO: pcall1("sumto", n->a, pint); return;
    case X_LOOP:
        printf("(loop ");
        pint(n->a);
        printf(" 0)");
        return;
    case X_SUM: pcall1("sum", n->a, plist); return;
    case X_PRODUCT: pcall1("product", n->a, plist); return;
    case X_MAXIMUM: pcall1("maximum", n->a, plist); return;
    case X_MINIMUM: pcall1("minimum", n->a, plist); return;
    case X_LEN: pcall1("length", n->a, plist); return;
    case X_MYLEN: pcall1("len", n->a, plist); return;
    case X_HEAD: pcall1("head", n->a, plist); return;
    case X_LAST: pcall1("last", n->a, plist); return;
    case X_INDEX:
        printf("(");
        plist(n->a);
        printf(" !! ");
        pint(n->b);
        printf(")");
        return;
    }
}

static void pbool(int e)
{
    static const char *const cmp[] = {"<", "<=", ">", ">=", "==", "/="};
    node_t *n = &nd[e];
    switch (n->k) {
    case B_TRUE: printf("True"); return;
    case B_FALSE: printf("False"); return;
    case B_AND:
    case B_OR:
        printf("(");
        pbool(n->a);
        printf(n->k == B_AND ? " && " : " || ");
        pbool(n->b);
        printf(")");
        return;
    case B_NOT: pcall1("not", n->a, pbool); return;
    case B_EVEN: pcall1("even", n->a, pint); return;
    case B_ODD: pcall1("odd", n->a, pint); return;
    case B_LEQ:
    case B_LLT:
        printf("(");
        plist(n->a);
        printf(n->k == B_LEQ ? " == " : " < ");
        plist(n->b);
        printf(")");
        return;
    case B_NULL: pcall1("null", n->a, plist); return;
    case B_ELEM:
        printf("(elem ");
        pint(n->a);
        printf(" ");
        plist(n->b);
        printf(")");
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
    case L_VAR: printf("%s", vname[n->v]); return;
    case L_LIT:
    case L_CONCAT:
        printf(n->k == L_CONCAT ? "(concat [" : "[");
        for (int i = 0; i < n->nk; i++) {
            if (i) printf(", ");
            if (n->k == L_CONCAT)
                plist(n->kid[i]);
            else
                pint(n->kid[i]);
        }
        printf(n->k == L_CONCAT ? "])" : "]");
        return;
    case L_CONS:
        printf("(");
        pint(n->a);
        printf(" : ");
        plist(n->b);
        printf(")");
        return;
    case L_APP:
        printf("(");
        plist(n->a);
        printf(" ++ ");
        plist(n->b);
        printf(")");
        return;
    case L_REV: pcall1("reverse", n->a, plist); return;
    case L_TAIL: pcall1("tail", n->a, plist); return;
    case L_INIT: pcall1("init", n->a, plist); return;
    case L_PAIRS: pcall1("pairs", n->a, plist); return;
    case L_RANGE:
        printf("[");
        pint(n->a);
        printf(" .. ");
        pint(n->b);
        printf("]");
        return;
    case L_RANGE3:
        printf("[");
        pint(n->a);
        printf(", ");
        pint(n->b);
        printf(" .. ");
        pint(n->c);
        printf("]");
        return;
    case L_TAKE:
    case L_DROP:
        printf(n->k == L_TAKE ? "(take " : "(drop ");
        pint(n->a);
        printf(" ");
        plist(n->b);
        printf(")");
        return;
    case L_REPL:
        printf("(replicate ");
        pint(n->a);
        printf(" ");
        pint(n->b);
        printf(")");
        return;
    }
}

/* ------------------------------------------------------------ statements */

/* an integer, boolean or list expression that evaluates without trouble */
static int good(int d, int kind)
{
    for (int t = 0; t < 200; t++) {
        int mark = nn, nv = nvars;
        bad = 0;
        int e = kind == 0 ? gint(d) : kind == 1 ? gbool(d) : glist(d);
        if (!bad) {
            if (kind == 0)
                ev(e);
            else if (kind == 1)
                evb(e);
            else
                evl(e);
        }
        if (!bad) return e;
        nn = mark;
        nvars = nv;
    }
    bad = 0;
    if (kind == 0) return lit((long long) rnd(10));
    if (kind == 1) return newnode(B_TRUE);
    int e = newnode(L_LIT);
    nd[e].nk = 1;
    nd[e].kid[0] = lit(1);
    return e;
}

static void prelude(void)
{
    printf("sumto :: Int -> Int\nsumto n = if n <= 0 then 0 else n + sumto (n - 1)\n\n");
    printf("loop :: Int -> Int -> Int\nloop 0 acc = acc\nloop i acc = loop (i - 1) (acc + i `mod` 7)\n\n");
    printf("len :: [Int] -> Int\nlen [] = 0\nlen (_ : xs) = 1 + len xs\n\n");
    printf("pairs :: [Int] -> [Int]\npairs (x : y : rest) = (x + y) : pairs rest\npairs [x] = [x]\npairs [] = []\n\n");
}

/* a function of two parameters; its bodies see only the parameters */
static void function(void)
{
    int f = nfun, save = niscope;
    fpa[f] = newvar('a');
    fpb[f] = newvar('b');
    fform[f] = (int) rnd(4);
    bad = 0;
    if (fform[f] == 3) {
        iscope[niscope++] = fpb[f];
        fx[f] = gint(3);
        iscope[niscope++] = fpa[f];
    } else {
        iscope[niscope++] = fpa[f];
        iscope[niscope++] = fpb[f];
    }
    if (fform[f] == 1) {
        fbody[f] = gbool(2);
        fx[f] = gint(3);
        fy[f] = gint(3);
    } else if (fform[f] == 2) {
        fx[f] = gint(2);
        fwv[f] = newvar('w');
        iscope[niscope++] = fwv[f];
        fbody[f] = gint(3);
    } else {
        fbody[f] = gint(3);
    }
    if (bad) {
        fform[f] = 0;
        fbody[f] = lit(1);
    }
    bad = 0;
    niscope = save;
    printf("f%d :: Int -> Int -> Int\n", f);
    switch (fform[f]) {
    case 1:
        printf("f%d %s %s\n  | ", f, vname[fpa[f]], vname[fpb[f]]);
        pbool(fbody[f]);
        printf(" = ");
        pint(fx[f]);
        printf("\n  | otherwise = ");
        pint(fy[f]);
        printf("\n");
        break;
    case 2:
        printf("f%d %s %s = ", f, vname[fpa[f]], vname[fpb[f]]);
        pint(fbody[f]);
        printf("\n  where\n    %s = ", vname[fwv[f]]);
        pint(fx[f]);
        printf("\n");
        break;
    case 3:
        printf("f%d 0 %s = ", f, vname[fpb[f]]);
        pint(fx[f]);
        printf("\nf%d %s %s = ", f, vname[fpa[f]], vname[fpb[f]]);
        pint(fbody[f]);
        printf("\n");
        break;
    default:
        printf("f%d %s %s = ", f, vname[fpa[f]], vname[fpb[f]]);
        pint(fbody[f]);
        printf("\n");
        break;
    }
    printf("\n");
    nfun++;
}

static void pany(int e, int kind)
{
    if (kind == 0)
        pint(e);
    else if (kind == 1)
        pbool(e);
    else
        plist(e);
}

/* one statement of main: print, putStrLn with show, or a let */
static void stmt(void)
{
    unsigned r = rnd(12);
    if (r < 2 && niscope < 60) {
        int e = good(3, 0), v = newvar('g');
        ival[v] = ev(e);
        printf("  let %s = ", vname[v]);
        pint(e);
        printf("\n");
        iscope[niscope++] = v;
        return;
    }
    if (r == 2 && nlscope < 16) {
        int e = good(3, 2), v = newvar('l');
        lval[v] = evl(e);
        printf("  let %s = ", vname[v]);
        plist(e);
        printf("\n");
        lscope[nlscope++] = v;
        printf("  print %s\n", vname[v]);
        return;
    }
    if (r <= 5) {
        int kind = (int) rnd(3), e = good(3, kind);
        printf(rnd(2) ? "  print (" : "  print $ (");
        pany(e, kind);
        printf(")\n");
        return;
    }
    if (r <= 8) {
        int k = 2 + (int) rnd(2);
        printf("  print (");
        for (int i = 0; i < k; i++) {
            int kind = (int) rnd(3), e = good(2, kind);
            if (i) printf(", ");
            pany(e, kind);
        }
        printf(")\n");
        return;
    }
    int k = 1 + (int) rnd(2);
    printf(r == 9 ? "  putStr (\"" : "  putStrLn (\"");
    printf("v%u = \"", r);
    for (int i = 0; i < k; i++) {
        int kind = (int) rnd(3), e = good(2, kind);
        printf(i ? " ++ \", \" ++ show " : " ++ show ");
        pany(e, kind);
    }
    printf(r == 9 ? " ++ \"\\n\")\n" : ")\n");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: gen_haskell <seed>\n");
        return 2;
    }
    rng = 0x9e3779b97f4a7c15ull ^ ((unsigned long long) strtoul(argv[1], 0, 10) * 2654435761ull);
    for (int i = 0; i < 4; i++) rnd(1);
    printf("-- generated program, seed %s\n", argv[1]);
    prelude();
    int nf = 1 + (int) rnd(NF);
    for (int i = 0; i < nf; i++) function();
    printf("main :: IO ()\nmain = do\n");
    int nstmt = 10 + (int) rnd(8);
    for (int s = 0; s < nstmt; s++) stmt();
    printf("  return ()\n");
    return 0;
}
