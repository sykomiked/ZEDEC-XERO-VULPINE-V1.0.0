/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_lisp.c — random program generator for the LISP and Scheme
 * differential tests (ref_lisp.sh runs sbcl and guile on the same programs
 * as zxlang and requires identical output).
 *
 * Programs are built as trees, then printed in either dialect. The
 * generator evaluates every statement before it keeps it: an expression
 * that would divide by zero or leave +-10^9 at any step, for any loop
 * iteration or argument it is used with, is thrown away and drawn again.
 * Each program is then well defined in both dialects, its numbers stay far
 * from the fixnum edge, and the references never meet an error.
 *
 * What it covers: global variables and assignment, functions of two
 * parameters calling earlier ones, let, a tail-recursive loop (labels in
 * LISP, a named let in Scheme) closing over outer variables, if/when, and,
 * or, not, the integer predicates and division operators, a counted loop
 * (dotimes / do), list construction and the list built-ins, a recursive
 * walk of a quoted list, and printing of numbers, lists and booleans.
 *
 * Modes: lisp and cl (Common Lisp: the same program, with case for if),
 * scm and rkt (Racket: #lang racket, add1/sub1, brackets, no
 * floor-quotient, and bare top-level values that racket prints itself).
 *
 * Usage: gen_lisp lisp|cl|scm|rkt <seed>   (writes the program to stdout) */
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
#define NG    3 /* global variables */
#define NF    3 /* functions */
#define NL    5 /* elements of the quoted list */
#define NODES 4096
#define SLOTS 512

enum {
    /* integer expressions */
    E_LIT,
    E_VAR,
    E_ADD,
    E_SUB,
    E_MUL,
    E_TQ,
    E_TR,
    E_FQ,
    E_FR,
    E_MAX,
    E_MIN,
    E_ABS,
    E_INC,
    E_DEC,
    E_IF,
    E_LET,
    E_LOOP,
    E_CALL,
    E_SUM,
    E_NTH,
    E_LEN,
    /* tests */
    T_LT,
    T_LE,
    T_EQ,
    T_GT,
    T_NE,
    T_EVEN,
    T_ZERO,
    T_NOT,
    T_AND,
    T_OR,
    /* list expressions */
    L_LIST,
    L_CONS,
    L_REV,
    L_APP,
    L_DATA,
};

typedef struct {
    int k;
    int a, b, c; /* children, or slot / function / literal data */
    long long v;
} node_t;

static node_t pool[NODES];
static int npool, overflowed;
static int scm, rkt, cl, bare; /* rkt implies scm; bare: a top-level Racket value */

static long long val[SLOTS];
static int nslots;
static char sname[SLOTS][12];

static int fparam[NF][2], fbody[NF], nfn;
static long long data[NL];
static int gslot[NG];

static int scope[64], nscope; /* visible slots */

static int mk(int k, int a, int b, int c, long long v)
{
    if (npool >= NODES) {
        overflowed = 1;
        npool = NODES - 1;
    }
    pool[npool] = (node_t){k, a, b, c, v};
    return npool++;
}

static int new_slot(const char *pfx)
{
    int s = nslots++;
    snprintf(sname[s], sizeof sname[s], "%s%d", pfx, s);
    return s;
}

/* ---- generation ---- */

static int gen_int(int d, int fn);
static int gen_test(int d, int fn)
{
    unsigned r = rnd(d > 0 ? 10 : 7);
    if (r < 5) return mk(T_LT + (int) r, gen_int(d - 1, fn), gen_int(d - 1, fn), 0, 0);
    if (r < 7) return mk(T_EVEN + (int) (r - 5), gen_int(d - 1, fn), 0, 0, 0);
    if (r == 7) return mk(T_NOT, gen_test(d - 1, fn), 0, 0, 0);
    return mk(T_AND + (int) (r - 8), gen_test(d - 1, fn), gen_test(d - 1, fn), 0, 0);
}

static int gen_int(int d, int fn)
{
    unsigned r = rnd(d > 0 ? 24 : 3);
    if (r == 0 || (r == 1 && !nscope)) return mk(E_LIT, 0, 0, 0, (long long) rnd(21) - 6);
    if (r <= 2) return mk(E_VAR, scope[rnd((unsigned) nscope)], 0, 0, 0);
    if (r <= 12) {
        static const int bin[] = {E_ADD, E_ADD, E_SUB, E_MUL, E_TQ, E_TR, E_FQ, E_FR, E_MAX, E_MIN};
        return mk(bin[r - 3], gen_int(d - 1, fn), gen_int(d - 1, fn), 0, 0);
    }
    if (r <= 15) return mk(E_ABS + (int) (r - 13), gen_int(d - 1, fn), 0, 0, 0);
    if (r <= 17) return mk(E_IF, gen_test(d - 1, fn), gen_int(d - 1, fn), gen_int(d - 1, fn), 0);
    if (r == 18 && nscope < 60) {
        int e = gen_int(d - 1, fn), s = new_slot("v");
        scope[nscope++] = s;
        int body = gen_int(d - 1, fn);
        nscope--;
        return mk(E_LET, s, e, body, 0);
    }
    if (r == 19 && nscope < 59) {
        int i = new_slot("i"), acc = new_slot("acc");
        new_slot("lp"); /* the loop's own name: slot i + 2 */
        scope[nscope++] = i;
        int body = gen_int(d - 2, fn);
        nscope--;
        return mk(E_LOOP, i, acc, body, (long long) rnd(7));
    }
    if (r == 20 && fn > 0) {
        int f = (int) rnd((unsigned) fn);
        return mk(E_CALL, f, gen_int(d - 1, fn), gen_int(d - 1, fn), 0);
    }
    if (r == 21) return mk(E_SUM, 0, 0, 0, 0);
    if (r == 22) return mk(E_NTH, 0, 0, 0, (long long) rnd(NL));
    if (r == 23) {
        int l = mk(L_LIST, gen_int(d - 1, fn), gen_int(d - 1, fn), 0, 0);
        return mk(E_LEN, l, 0, 0, 0);
    }
    return mk(E_LIT, 0, 0, 0, (long long) rnd(10));
}

static int gen_list(int d, int fn)
{
    switch (rnd(5)) {
    case 0:
        return mk(L_LIST, gen_int(d, fn), gen_int(d, fn), gen_int(d, fn), 0);
    case 1:
        return mk(L_CONS, gen_int(d, fn), gen_list(d - 1, fn), 0, 0);
    case 2:
        return mk(L_REV, gen_list(d - 1, fn), 0, 0, 0);
    case 3:
        return mk(L_APP, gen_list(d - 1, fn), gen_list(d - 1, fn), 0, 0);
    default:
        return mk(L_DATA, 0, 0, 0, 0);
    }
}

/* ---- evaluation: 0 when the tree is not well defined ---- */

static int ok(long long v)
{
    return v >= -LIM && v <= LIM;
}

static long long fdiv(long long a, long long b)
{
    long long q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static int eval(int n, long long *out);
static int test(int n, int *out)
{
    node_t *p = &pool[n];
    long long x = 0, y = 0;
    int s = 0, t = 0;
    switch (p->k) {
    case T_LT:
    case T_LE:
    case T_EQ:
    case T_GT:
    case T_NE:
        if (!eval(p->a, &x) || !eval(p->b, &y)) return 0;
        *out = p->k == T_LT   ? x < y
               : p->k == T_LE ? x <= y
               : p->k == T_EQ ? x == y
               : p->k == T_GT ? x > y
                              : x != y;
        return 1;
    case T_EVEN:
        if (!eval(p->a, &x)) return 0;
        *out = x % 2 == 0;
        return 1;
    case T_ZERO:
        if (!eval(p->a, &x)) return 0;
        *out = x == 0;
        return 1;
    case T_NOT:
        if (!test(p->a, &s)) return 0;
        *out = !s;
        return 1;
    case T_AND:
        if (!test(p->a, &s)) return 0;
        if (!s) return *out = 0, 1;
        if (!test(p->b, &t)) return 0;
        return *out = t, 1;
    case T_OR:
        if (!test(p->a, &s)) return 0;
        if (s) return *out = 1, 1;
        if (!test(p->b, &t)) return 0;
        return *out = t, 1;
    }
    return 0;
}

static int depth;
static int eval(int n, long long *out)
{
    node_t *p = &pool[n];
    long long x = 0, y = 0, r = 0;
    int t = 0;
    if (++depth > 200) return depth--, 0;
    switch (p->k) {
    case E_LIT:
        r = p->v;
        break;
    case E_VAR:
        r = val[p->a];
        break;
    case E_ADD:
    case E_SUB:
    case E_MUL:
    case E_TQ:
    case E_TR:
    case E_FQ:
    case E_FR:
    case E_MAX:
    case E_MIN:
        if (!eval(p->a, &x) || !eval(p->b, &y)) goto bad;
        if (p->k >= E_TQ && p->k <= E_FR && y == 0) goto bad;
        r = p->k == E_ADD   ? x + y
            : p->k == E_SUB ? x - y
            : p->k == E_MUL ? x * y
            : p->k == E_TQ  ? x / y
            : p->k == E_TR  ? x % y
            : p->k == E_FQ  ? fdiv(x, y)
            : p->k == E_FR  ? x - y * fdiv(x, y)
            : p->k == E_MAX ? (x > y ? x : y)
                            : (x < y ? x : y);
        break;
    case E_ABS:
    case E_INC:
    case E_DEC:
        if (!eval(p->a, &x)) goto bad;
        r = p->k == E_ABS ? (x < 0 ? -x : x) : p->k == E_INC ? x + 1 : x - 1;
        break;
    case E_IF:
        if (!test(p->a, &t) || !eval(t ? p->b : p->c, &r)) goto bad;
        break;
    case E_LET:
        if (!eval(p->b, &x)) goto bad;
        val[p->a] = x;
        if (!eval(p->c, &r)) goto bad;
        break;
    case E_LOOP:
        for (long long i = 0; i < p->v; i++) {
            val[p->a] = i;
            if (!eval(p->c, &x) || !ok(r + x)) goto bad;
            r += x;
        }
        break;
    case E_CALL:
        if (!eval(p->b, &x) || !eval(p->c, &y)) goto bad;
        val[fparam[p->a][0]] = x;
        val[fparam[p->a][1]] = y;
        if (!eval(fbody[p->a], &r)) goto bad;
        break;
    case E_SUM:
        for (int i = 0; i < NL; i++) r += data[i];
        break;
    case E_NTH:
        r = data[p->v];
        break;
    case E_LEN:
        if (!eval(pool[p->a].a, &x) || !eval(pool[p->a].b, &y)) goto bad;
        r = 2;
        break;
    default:
        goto bad;
    }
    depth--;
    if (!ok(r)) return 0;
    *out = r;
    return 1;
bad:
    depth--;
    return 0;
}

/* A list's printed width: sbcl's pretty printer breaks lines past 80
 * columns, which zxlang does not model, so printed lists stay short. */
static int width;
static int add_width(long long x)
{
    char b[24];
    width += snprintf(b, sizeof b, "%lld ", x);
    return 1;
}

static int eval_list(int n)
{
    node_t *p = &pool[n];
    long long x;
    switch (p->k) {
    case L_LIST:
        return eval(p->a, &x) && add_width(x) && eval(p->b, &x) && add_width(x) && eval(p->c, &x) &&
               add_width(x);
    case L_CONS:
        return eval(p->a, &x) && add_width(x) && eval_list(p->b);
    case L_REV:
        return eval_list(p->a);
    case L_APP:
        return eval_list(p->a) && eval_list(p->b);
    default:
        for (int i = 0; i < NL; i++) add_width(data[i]);
        return 1;
    }
}

/* ---- printing ---- */

static const char *gname(int g)
{
    static char b[NG][8];
    snprintf(b[g], sizeof b[g], scm ? "g%d" : "*g%d*", g);
    return b[g];
}

static const char *vname(int s)
{
    for (int g = 0; g < NG; g++)
        if (gslot[g] == s) return gname(g);
    return sname[s];
}

static void pint(int n);
static void ptest(int n)
{
    static const char *const cmp[] = {"<", "<=", "=", ">", "/="};
    node_t *p = &pool[n];
    switch (p->k) {
    case T_NE:
        if (scm) {
            printf("(not (= ");
            pint(p->a);
            printf(" ");
            pint(p->b);
            printf("))");
            return;
        }
        /* fall through */
    case T_LT:
    case T_LE:
    case T_EQ:
    case T_GT:
        printf("(%s ", cmp[p->k - T_LT]);
        pint(p->a);
        printf(" ");
        pint(p->b);
        printf(")");
        return;
    case T_EVEN:
    case T_ZERO:
        printf("(%s ", p->k == T_EVEN ? (scm ? "even?" : "evenp") : (scm ? "zero?" : "zerop"));
        pint(p->a);
        printf(")");
        return;
    case T_NOT:
        printf("(not ");
        ptest(p->a);
        printf(")");
        return;
    default:
        printf("(%s ", p->k == T_AND ? "and" : "or");
        ptest(p->a);
        printf(" ");
        ptest(p->b);
        printf(")");
    }
}

static void pint(int n)
{
    node_t *p = &pool[n];
    static const char *const lop[] = {"+",     "-",   "*",   "truncate", "rem",
                                      "floor", "mod", "max", "min"};
    static const char *const sop[] = {
        "+", "-", "*", "quotient", "remainder", "floor-quotient", "modulo", "max", "min"};
    switch (p->k) {
    case E_LIT:
        printf("%lld", p->v);
        return;
    case E_VAR:
        printf("%s", vname(p->a));
        return;
    case E_ADD:
    case E_SUB:
    case E_MUL:
    case E_TQ:
    case E_TR:
    case E_FQ:
    case E_FR:
    case E_MAX:
    case E_MIN:
        if (rkt && p->k == E_FQ) { /* racket has no floor-quotient: (a - a mod b) / b */
            printf("(quotient (- ");
            pint(p->a);
            printf(" (modulo ");
            pint(p->a);
            printf(" ");
            pint(p->b);
            printf(")) ");
            pint(p->b);
            printf(")");
            return;
        }
        printf("(%s ", (scm ? sop : lop)[p->k - E_ADD]);
        pint(p->a);
        printf(" ");
        pint(p->b);
        printf(")");
        return;
    case E_ABS:
    case E_INC:
    case E_DEC:
        if (p->k == E_ABS)
            printf("(abs ");
        else if (rkt)
            printf("(%s ", p->k == E_INC ? "add1" : "sub1");
        else if (scm)
            printf("(%s ", p->k == E_INC ? "+ 1" : "+ -1");
        else
            printf("(%s ", p->k == E_INC ? "1+" : "1-");
        pint(p->a);
        printf(")");
        return;
    case E_IF:
        if (cl) { /* Common Lisp: a case on the test's 1 or 0 */
            printf("(case (if ");
            ptest(p->a);
            printf(" 1 0) (1 ");
            pint(p->b);
            printf(") (otherwise ");
            pint(p->c);
            printf("))");
            return;
        }
        printf("(if ");
        ptest(p->a);
        printf(" ");
        pint(p->b);
        printf(" ");
        pint(p->c);
        printf(")");
        return;
    case E_LET:
        printf(rkt ? "(let ([%s " : "(let ((%s ", sname[p->a]);
        pint(p->b);
        printf(rkt ? "]) " : ")) ");
        pint(p->c);
        printf(")");
        return;
    case E_LOOP: {
        const char *i = sname[p->a], *acc = sname[p->b], *lp = sname[p->a + 2];
        if (rkt)
            printf("(let %s ([%s 0] [%s 0]) (if (< %s %lld) (%s (add1 %s) (+ %s ", lp, i, acc, i,
                   p->v, lp, i, acc);
        else if (scm)
            printf("(let %s ((%s 0) (%s 0)) (if (< %s %lld) (%s (+ %s 1) (+ %s ", lp, i, acc, i,
                   p->v, lp, i, acc);
        else
            printf("(labels ((%s (%s %s) (if (< %s %lld) (%s (+ %s 1) (+ %s ", lp, i, acc, i, p->v,
                   lp, i, acc);
        pint(p->c);
        if (scm)
            printf(")) %s))", acc);
        else
            printf(")) %s))) (%s 0 0))", acc, lp);
        return;
    }
    case E_CALL:
        printf("(f%d ", p->a);
        pint(p->b);
        printf(" ");
        pint(p->c);
        printf(")");
        return;
    case E_SUM:
        printf("(sm %s)", scm ? "data" : "*data*");
        return;
    case E_NTH:
        printf("(car ");
        for (long long i = 0; i < p->v; i++) printf("(cdr ");
        printf("%s", scm ? "data" : "*data*");
        for (long long i = 0; i <= p->v; i++) printf(")");
        return;
    case E_LEN:
        printf("(length (list ");
        pint(pool[p->a].a);
        printf(" ");
        pint(pool[p->a].b);
        printf("))");
        return;
    }
}

static void plist(int n)
{
    node_t *p = &pool[n];
    switch (p->k) {
    case L_LIST:
        printf("(list ");
        pint(p->a);
        printf(" ");
        pint(p->b);
        printf(" ");
        pint(p->c);
        printf(")");
        return;
    case L_CONS:
        printf("(cons ");
        pint(p->a);
        printf(" ");
        plist(p->b);
        printf(")");
        return;
    case L_REV:
        printf("(reverse ");
        plist(p->a);
        printf(")");
        return;
    case L_APP:
        printf("(append ");
        plist(p->a);
        printf(" ");
        plist(p->b);
        printf(")");
        return;
    default:
        printf("%s", scm ? "data" : "*data*");
    }
}

static void out_open(void)
{
    printf(rkt ? (bare ? "" : "(displayln ") : scm ? "(display " : "(print ");
}

static void out_close(void)
{
    printf(rkt ? (bare ? "\n" : ")\n") : scm ? ") (newline)\n" : ")\n");
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "lisp") && strcmp(argv[1], "scm") && strcmp(argv[1], "cl") &&
                      strcmp(argv[1], "rkt"))) {
        fprintf(stderr, "usage: gen_lisp lisp|cl|scm|rkt <seed>\n");
        return 2;
    }
    rkt = !strcmp(argv[1], "rkt");
    cl = !strcmp(argv[1], "cl");
    scm = argv[1][0] == 's' || rkt;
    rng = 0x9e3779b97f4a7c15ull ^ ((unsigned long long) strtoul(argv[2], 0, 10) * 2654435761ull);
    for (int i = 0; i < 4; i++) rnd(1);
    if (rkt) printf("#lang racket\n");
    printf(";;; generated program, seed %s\n", argv[2]);
    for (int i = 0; i < NL; i++) data[i] = (long long) rnd(41) - 20;
    printf(scm ? "(define data '(" : "(defvar *data* '(");
    for (int i = 0; i < NL; i++) printf(i ? " %lld" : "%lld", data[i]);
    printf("))\n");
    if (scm)
        printf("(define (sm l) (if (null? l) 0 (+ (car l) (sm (cdr l)))))\n");
    else
        printf("(defun sm (l) (if (null l) 0 (+ (car l) (sm (cdr l)))))\n");
    for (int g = 0; g < NG; g++) {
        gslot[g] = new_slot("g");
        val[gslot[g]] = (long long) rnd(19) - 9;
        printf(scm ? "(define %s %lld)\n" : "(defvar %s %lld)\n", gname(g), val[gslot[g]]);
    }
    /* functions: each body uses its own parameters and may call earlier functions */
    for (int f = 0; f < NF; f++) {
        fparam[f][0] = new_slot("x");
        fparam[f][1] = new_slot("y");
        int mark = npool, base = nslots;
        long long probe;
        for (int tries = 0;; tries++) {
            npool = mark;
            nslots = base;
            nscope = 2;
            scope[0] = fparam[f][0];
            scope[1] = fparam[f][1];
            fbody[f] = gen_int(3, f);
            /* the body must hold for a spread of arguments, since callers vary */
            int good = 1;
            for (long long a = -3; a <= 3 && good; a += 3)
                for (long long b = -2; b <= 2 && good; b += 2) {
                    val[fparam[f][0]] = a;
                    val[fparam[f][1]] = b;
                    good = eval(fbody[f], &probe);
                }
            if (good || tries > 200) {
                if (!good) fbody[f] = mk(E_VAR, fparam[f][0], 0, 0, 0);
                break;
            }
        }
        nfn = f + 1;
        printf(scm ? "(define (f%d %s %s) " : "(defun f%d (%s %s) ", f, sname[fparam[f][0]],
               sname[fparam[f][1]]);
        pint(fbody[f]);
        printf(")\n");
    }
    int nst = 8 + (int) rnd(10);
    for (int st = 0; st < nst; st++) {
        int mark = npool, base = nslots;
        int kind = (int) rnd(7), e = 0, t = 0, g = (int) rnd(NG), k = (int) rnd(5);
        long long saved[NG], x = 0;
        int tv = 0, good = 0;
        for (int i = 0; i < NG; i++) saved[i] = val[gslot[i]];
        for (int tries = 0; tries < 200 && !good; tries++) {
            npool = mark;
            nslots = base;
            nscope = NG;
            for (int i = 0; i < NG; i++) {
                scope[i] = gslot[i];
                val[gslot[i]] = saved[i];
            }
            switch (kind) {
            case 0:
            case 1: /* print a number */
                e = gen_int(4, nfn);
                good = eval(e, &x);
                break;
            case 2: /* assign a global */
                e = gen_int(3, nfn);
                good = eval(e, &x);
                if (good) val[gslot[g]] = x;
                break;
            case 3: /* if / when */
                t = gen_test(2, nfn);
                e = gen_int(3, nfn);
                good = test(t, &tv) && (!tv || eval(e, &x));
                break;
            case 4: /* counted loop adding to a global */
                e = new_slot("i");
                scope[nscope++] = e;
                t = gen_int(2, nfn);
                nscope--;
                good = 1;
                for (long long i = 0; i < k && good; i++) {
                    val[e] = i;
                    good = eval(t, &x) && ok(val[gslot[g]] + x);
                    if (good) val[gslot[g]] += x;
                }
                break;
            case 5: /* print a list */
                e = gen_list(2, nfn);
                width = 0;
                good = eval_list(e) && width < 64;
                break;
            default: /* print a boolean */
                t = gen_test(2, nfn);
                good = test(t, &tv);
            }
        }
        if (!good || overflowed) {
            npool = mark;
            for (int i = 0; i < NG; i++) val[gslot[i]] = saved[i];
            printf(scm ? "(display %d) (newline)\n" : "(print %d)\n", st);
            continue;
        }
        bare = rkt && (st & 1) == 0 && kind != 3; /* racket prints a bare value itself */
        switch (kind) {
        case 0:
        case 1:
            out_open();
            pint(e);
            out_close();
            break;
        case 2:
            printf(scm ? "(set! %s " : "(setq %s ", gname(g));
            pint(e);
            printf(")\n");
            break;
        case 3:
            printf("(when ");
            ptest(t);
            printf(" ");
            out_open();
            pint(e);
            printf(scm && !rkt ? ") (newline))\n" : "))\n");
            break;
        case 4:
            if (scm)
                printf("(do ((%s 0 (+ %s 1))) ((= %s %d)) (set! %s (+ %s ", sname[e], sname[e],
                       sname[e], k, gname(g), gname(g));
            else
                printf("(dotimes (%s %d) (setq %s (+ %s ", sname[e], k, gname(g), gname(g));
            pint(t);
            printf(")))\n");
            break;
        case 5:
            out_open();
            plist(e);
            out_close();
            break;
        default:
            out_open();
            ptest(t);
            out_close();
        }
        npool = mark; /* statements are printed; their trees are no longer needed */
    }
    bare = rkt;
    out_open();
    printf(scm ? "(list g0 g1 g2)" : "(list *g0* *g1* *g2*)");
    out_close();
    return 0;
}
