/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_prolog.c — random program generator for the Prolog differential
 * test (ref_prolog.sh runs swipl on the same programs as zxlang and
 * requires identical output).
 *
 * Every program is well defined in both systems by construction. Each
 * arithmetic expression carries a bound on its magnitude, and an operation
 * is only drawn when the bound of its result stays below 2^40, so nothing
 * comes near the 62-bit edge where SWI would turn to bignums. A divisor is
 * always a nonzero literal or abs(E) + 1. Exponents and shift counts are
 * small nonnegative literals. Every printed term is ground. A unification
 * is always between a term with variables and a ground one, so whatever
 * succeeds is ground too. Recursion is bounded by its first argument.
 *
 * What a program covers: facts and first-argument selection, clause
 * chains with guards and cut, accumulator and list recursion, if-then-else
 * and disjunction, \+, findall, forall, between, length, append, member,
 * nth0/nth1, the integer evaluable functions, comparison, unification
 * (=, \=, ==, \==), the type tests, and the writer: write, print and
 * writeq of random ground terms built from operators, quoted atoms, lists,
 * curly terms and negative numbers, plus format/2 with ~w ~a ~d ~p ~q.
 *
 * Usage: gen_prolog <seed>   (writes the program to stdout) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static unsigned long long rng;
static unsigned rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (unsigned) (rng % n) : 0;
}

#define BMAX (1ll << 40)
#define SB   8192

static int fmtn(char *d, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(d, n, f, ap);
    va_end(ap);
    return r < 0 ? 0 : (size_t) r >= n ? (int) n - 1 : r;
}
#define fmt(d, ...) fmtn(d, SB, __VA_ARGS__)

/* ---- arithmetic: a string and a bound on its magnitude ---- */

typedef struct {
    char s[SB];
    long long b;
} ex_t;

static const char *xvar[8];
static long long xbound[8];
static int nxvar;

static void lit(ex_t *e, long long lo, long long hi)
{
    long long v = lo + (long long) rnd((unsigned) (hi - lo + 1));
    fmt(e->s, v < 0 ? "(%lld)" : "%lld", v);
    e->b = v < 0 ? -v : v;
}

static void gen_ex(ex_t *e, int d)
{
    unsigned r = rnd(d > 0 ? 20 : 3);
    if (r == 0 || (r <= 2 && !nxvar)) {
        lit(e, -30, 60);
        return;
    }
    if (r <= 2) {
        int i = (int) rnd((unsigned) nxvar);
        fmt(e->s, "%s", xvar[i]);
        e->b = xbound[i];
        return;
    }
    static ex_t a[64], b[64];
    if (d >= 64) d = 63;
    ex_t *x = &a[d], *y = &b[d];
    gen_ex(x, d - 1);
    if (r <= 9) {
        static const char *bin[] = {"+", "-", "*", "min", "max", "/\\", "\\/"};
        const char *op = bin[r - 3];
        int fn = r == 6 || r == 7;
        gen_ex(y, d - 1);
        long long bb = x->b > y->b ? x->b : y->b;
        long long nb = r == 5 ? x->b * y->b : r == 3 || r == 4 ? x->b + y->b : r <= 7 ? bb : 2 * bb;
        if (r == 5 && (x->b > (1 << 20) || y->b > (1 << 20))) nb = BMAX;
        if (nb >= BMAX) {
            *e = *x;
            return;
        }
        if (fn)
            fmt(e->s, "%s(%s, %s)", op, x->s, y->s);
        else
            fmt(e->s, "(%s %s %s)", x->s, op, y->s);
        e->b = nb;
        return;
    }
    if (r <= 13) {
        static const char *dv[] = {"//", "mod", "rem", "div"};
        const char *op = dv[r - 10];
        if (rnd(2)) {
            long long k = 1 + rnd(9);
            if (rnd(2)) k = -k;
            fmt(e->s, "(%s %s %lld)", x->s, op, k);
            e->b = r == 10 || r == 13 ? x->b : (k < 0 ? -k : k);
            if (e->b > x->b) e->b = x->b;
        } else {
            gen_ex(y, d - 1);
            fmt(e->s, "(%s %s (abs(%s) + 1))", x->s, op, y->s);
            e->b = x->b;
        }
        if (r == 13 || r == 10) e->b += 1;
        return;
    }
    if (r <= 16) {
        static const char *un[] = {"abs", "sign", "-"};
        if (r == 16)
            fmt(e->s, "(- %s)", x->s);
        else
            fmt(e->s, "%s(%s)", un[r - 14], x->s);
        e->b = r == 15 ? 1 : x->b;
        return;
    }
    if (r == 17 && x->b < 1000) {
        int k = (int) rnd(4);
        fmt(e->s, "(%s %s %d)", x->s, rnd(2) ? "^" : "**", k);
        long long nb = 1;
        for (int i = 0; i < k; i++) nb *= x->b > 0 ? x->b : 1;
        e->b = nb;
        return;
    }
    if (r == 18 && x->b < (1ll << 30)) {
        int k = (int) rnd(6);
        fmt(e->s, "(%s << %d)", x->s, k);
        e->b = x->b << k;
        return;
    }
    if (r == 19) {
        fmt(e->s, "(%s >> %d)", x->s, (int) rnd(5));
        e->b = x->b + 1;
        return;
    }
    if (r == 17 || r == 18) {
        gen_ex(y, d - 1);
        fmt(e->s, "xor(%s, %s)", x->s, y->s);
        e->b = 2 * (x->b > y->b ? x->b : y->b);
        if (e->b >= BMAX) *e = *x;
        return;
    }
    *e = *x;
}

static void gen_cmp(char *out, int d)
{
    static const char *cmp[] = {"=:=", "=\\=", "<", ">", "=<", ">="};
    static ex_t x, y;
    gen_ex(&x, d);
    gen_ex(&y, d);
    fmt(out, "%s %s %s", x.s, cmp[rnd(6)], y.s);
}

/* ---- ground terms for the writer and for unification ---- */

static const char *atoms[] = {"a",      "b",     "foo",   "'Bar'", "'x y'", "[]",    "'+'",
                              "'-'",    "'*'",   "'hello world'",  "'it''s'", "''",
                              "'\\\\'", "abc_1", "'1a'", "zz",    "'\\n'", "{}",    "'{}'",
                              "';'",    "'!'",   "'|'",   "'.'",  "'a.b'"};
#define NATOMS ((int) (sizeof atoms / sizeof atoms[0]))
static const char *fun2[] = {"f",    "g",    "'+'",  "'-'", "'*'",  "'/'", "mod",  "','",
                             "';'",  "'->'", "':-'", "'='", "'<'",  "'^'", "'**'", "'is'",
                             "'\\\\='", "rem", "xor", "'@'", "'|'"};
#define NFUN2 ((int) (sizeof fun2 / sizeof fun2[0]))
static const char *fun1[] = {"f", "'-'", "'+'", "'\\\\+'", "'\\\\'", "'?'", "'{}'", "':-'", "dynamic"};
#define NFUN1 ((int) (sizeof fun1 / sizeof fun1[0]))

static int nvar_used;

/* gen_term: a ground term; with vars set, some subterms become variables
 * V0..V3 in the copy written to `pat`. */
static void gen_term(char *gt, char *pat, int d, int vars)
{
    char a[SB], b[SB], pa[SB], pb[SB];
    if (vars && rnd(6) == 0) {
        int v = (int) rnd(4);
        fmt(pat, "V%d", v);
        nvar_used |= 1 << v;
        gen_term(gt, a, d - 1, 0);
        return;
    }
    unsigned r = rnd(d > 0 ? 9 : 3);
    if (r == 0) {
        long long v = (long long) rnd(41) - 20;
        fmt(gt, "%lld", v);
        if (v < 0 && rnd(2)) fmt(gt, "(%lld)", v);
    } else if (r <= 2) {
        fmt(gt, "%s", atoms[rnd(NATOMS)]);
    } else if (r <= 4) {
        const char *f = fun2[rnd(NFUN2)];
        gen_term(a, pa, d - 1, vars);
        gen_term(b, pb, d - 1, vars);
        fmt(gt, "%s(%s, %s)", f, a, b);
        if (vars) fmt(pat, "%s(%s, %s)", f, pa, pb);
        return;
    } else if (r == 5) {
        const char *f = fun1[rnd(NFUN1)];
        gen_term(a, pa, d - 1, vars);
        fmt(gt, "%s(%s)", f, a);
        if (vars) fmt(pat, "%s(%s)", f, pa);
        return;
    } else if (r <= 7) {
        int n = (int) rnd(4), bar = rnd(3) == 0;
        size_t m = 1, pm = 1;
        strcpy(gt, "[");
        strcpy(pat, "[");
        for (int i = 0; i < n; i++) {
            gen_term(a, pa, d - 1, vars);
            m += (size_t) fmtn(gt + m, SB - m, "%s%s", i ? ", " : "", a);
            if (vars) pm += (size_t) fmtn(pat + pm, SB - pm, "%s%s", i ? ", " : "", pa);
        }
        if (bar && n) {
            gen_term(a, pa, d - 1, vars);
            m += (size_t) fmtn(gt + m, SB - m, " | %s", a);
            if (vars) pm += (size_t) fmtn(pat + pm, SB - pm, " | %s", pa);
        }
        fmtn(gt + m, SB - m, "]");
        if (vars) fmtn(pat + pm, SB - pm, "]");
        return;
    } else {
        int n = 1 + (int) rnd(3);
        size_t m = 3, pm = 3;
        strcpy(gt, "f3(");
        strcpy(pat, "f3(");
        for (int i = 0; i < 3; i++) {
            gen_term(a, pa, i < n ? d - 1 : 0, vars);
            m += (size_t) fmtn(gt + m, SB - m, "%s%s", i ? ", " : "", a);
            if (vars) pm += (size_t) fmtn(pat + pm, SB - pm, "%s%s", i ? ", " : "", pa);
        }
        fmtn(gt + m, SB - m, ")");
        if (vars) fmtn(pat + pm, SB - pm, ")");
        return;
    }
    if (vars) strcpy(pat, gt);
}

/* ---- the program ---- */

static int ntask;

static void task_begin(void)
{
    printf("t%d :-\n", ntask++);
}

static void arith_task(void)
{
    static ex_t e;
    static char c[SB];
    task_begin();
    nxvar = 0;
    gen_ex(&e, 4);
    printf("    X is %s,\n", e.s);
    xvar[nxvar] = "X";
    xbound[nxvar++] = e.b;
    gen_ex(&e, 3);
    printf("    Y is %s,\n", e.s);
    xvar[nxvar] = "Y";
    xbound[nxvar++] = e.b;
    gen_cmp(c, 3);
    printf("    ( %s -> T = yes ; T = no ),\n", c);
    gen_cmp(c, 2);
    printf("    ( \\+ %s -> U = [X] ; U = [Y, X] ),\n", c);
    printf("    format(\"~d ~d ~w ~w~n\", [X, Y, T, U]).\n\n");
}

static void loop_task(void)
{
    static ex_t e;
    int k = ntask;
    long long n = rnd(25);
    nxvar = 2;
    xvar[0] = "N";
    xbound[0] = 30;
    xvar[1] = "A";
    xbound[1] = 1000003;
    gen_ex(&e, 3);
    printf("loop%d(0, A, A) :- !.\n", k);
    printf("loop%d(N, A, R) :-\n    N1 is N - 1,\n    A1 is (%s) mod 1000003,\n    loop%d(N1, A1, R).\n\n",
           k, e.s, k);
    task_begin();
    printf("    loop%d(%lld, %d, R), write(R), nl.\n\n", k, n, (int) rnd(100));
}

static void list_task(void)
{
    static ex_t e;
    static char c[SB];
    int k = ntask;
    nxvar = 1;
    xvar[0] = "N";
    xbound[0] = 20;
    gen_ex(&e, 3);
    printf("mk%d(0, []) :- !.\nmk%d(N, [E|T]) :- E is %s, N1 is N - 1, mk%d(N1, T).\n\n", k, k, e.s,
           k);
    nxvar = 1;
    xvar[0] = "X";
    xbound[0] = e.b;
    gen_cmp(c, 2);
    printf("filt%d([], [], []).\nfilt%d([X|Xs], I, O) :-\n    ( %s -> I = [X|I1], O = O1 ; I = I1, O = [X|O1] ),\n"
           "    filt%d(Xs, I1, O1).\n\n",
           k, k, c, k);
    printf("sum%d([], 0).\nsum%d([X|Xs], S) :- sum%d(Xs, S0), S is S0 + X.\n\n", k, k, k);
    task_begin();
    printf("    mk%d(%d, L), print(L), nl,\n", k, (int) rnd(12));
    printf("    filt%d(L, I, O), length(I, NI), length(O, NO),\n", k);
    printf("    sum%d(L, S), format(\"~w ~w ~d ~d ~d~n\", [I, O, NI, NO, S]),\n", k);
    printf("    append(I, O, IO), reverse(IO, R), write(R), nl,\n");
    printf("    ( member(M, L), M > %d -> write(M) ; write(none) ), nl,\n", (int) rnd(40) - 10);
    printf("    findall(P-Q, append(P, Q, I), PQ), length(PQ, NPQ), write(NPQ), nl,\n");
    printf("    ( nth0(1, L, E1) -> write(E1) ; write(short) ), write(' '),\n");
    printf("    ( nth1(2, L, E2), last(L, E3) -> write(E2/E3) ; write(short) ), nl.\n\n");
}

static void facts_task(void)
{
    static const char *col[] = {"red", "green", "blue", "'dark red'", "[]", "'Z'"};
    int k = ntask, n = 3 + (int) rnd(6);
    for (int i = 0; i < n; i++) {
        int key = (int) rnd(6);
        if (rnd(3))
            printf("fact%d(%d, %s).\n", k, key, col[rnd(6)]);
        else
            printf("fact%d(%s, %d).\n", k, col[rnd(6)], (int) rnd(50) - 10);
    }
    int g = (int) rnd(20);
    printf("\ncls%d(X, small) :- X < %d, !.\ncls%d(X, mid) :- X =:= %d, !.\ncls%d(X, big) :- X > %d.\n"
           "cls%d(_, other).\n\n",
           k, g, k, g, k, g + (int) rnd(5), k);
    task_begin();
    printf("    findall(K-V, fact%d(K, V), All), print(All), nl,\n", k);
    printf("    findall(V, fact%d(%d, V), Vs), writeq(Vs), nl,\n", k, (int) rnd(6));
    printf("    ( fact%d(%s, W) -> writeq(W) ; write(none) ), nl,\n", k, col[rnd(6)]);
    printf("    ( \\+ fact%d(_, %s) -> write(absent) ; write(present) ), nl,\n", k, col[rnd(6)]);
    printf("    forall(between(%d, %d, I), (cls%d(I, C), write(C), write(' '))), nl,\n", g - 2,
           g + 3, k);
    printf("    findall(C2, cls%d(%d, C2), Cs), write(Cs), nl,\n", k, g);
    printf("    findall(A-B, (between(1, 4, A), between(A, 4, B), (A + B) mod %d =:= 0), AB), write(AB), nl,\n",
           2 + (int) rnd(3));
    printf("    ( fact%d(_, X), integer(X) ; X = 0 ), !, write(X), nl.\n\n", k);
}

static void term_task(void)
{
    static char gt[SB], pat[SB], g2[SB], p2[SB];
    task_begin();
    nvar_used = 0;
    gen_term(gt, pat, 4, 1);
    printf("    T = %s,\n", gt);
    printf("    write(T), nl, print(T), nl, writeq(T), nl,\n");
    printf("    ( (%s) = T -> write(T) ; write(no) ), nl,\n", pat);
    gen_term(g2, p2, 3, 0);
    printf("    U = %s,\n", g2);
    printf("    writeq(U), nl, writeq(f(T, U)), nl, writeq([T|U]), nl, writeq(- U), nl, writeq(U - T), nl,\n");
    printf("    ( T == U -> write(eq) ; write(ne) ), ( T \\= U -> write(nu) ; write(un) ),\n");
    printf("    ( T \\== T -> write(ne) ; write(eq) ),\n");
    printf("    ( atom(U) -> write(a) ; true ), ( atomic(U) -> write(c) ; true ), ( compound(U) -> write(t) ; true ),\n");
    printf("    ( is_list(U) -> write(l) ; true ), ( callable(U) -> write(k) ; true ), ( integer(U) -> write(i) ; true ),\n");
    printf("    nl, format(\"~w|~q|~p~n\", [U, U, U]).\n\n");
}

static void control_task(void)
{
    static char c1[SB], c2[SB];
    int k = ntask;
    nxvar = 1;
    xvar[0] = "X";
    xbound[0] = 10;
    gen_cmp(c1, 2);
    gen_cmp(c2, 2);
    printf("pick%d(X, R) :- ( %s -> R = one ; %s -> R = two ; R = three ).\n", k, c1, c2);
    printf("alt%d(X, R) :- ( X > %d, R = hi ; X < %d, R = lo ; R = mid ).\n", k, (int) rnd(10),
           (int) rnd(10));
    printf("first%d(X, R) :- alt%d(X, R), !.\n\n", k, k);
    task_begin();
    printf("    forall(between(0, 10, X), (pick%d(X, R), write(R), write(' '))), nl,\n", k);
    printf("    findall(R2, alt%d(%d, R2), Rs), write(Rs), nl,\n", k, (int) rnd(11));
    printf("    findall(R3, first%d(%d, R3), R3s), write(R3s), nl,\n", k, (int) rnd(11));
    printf("    ( between(1, 10, Y), Y * Y > %d -> format(\"~d~n\", [Y]) ; format(\"none~n\") ),\n",
           (int) rnd(120));
    printf("    length(L, %d), ( L = [x|_] ; true ), length(L, N), format(\"~a ~d~n\", [len, N]).\n\n",
           (int) rnd(4));
}

int main(int argc, char **argv)
{
    unsigned long long seed = argc > 1 ? strtoull(argv[1], 0, 10) : 1;
    rng = seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
    for (int i = 0; i < 4; i++) rnd(1);
    printf("%% gen_prolog seed %llu\n:- initialization(main).\n\n", seed);
    int n = 4 + (int) rnd(5);
    for (int i = 0; i < n; i++) {
        switch (rnd(6)) {
        case 0: arith_task(); break;
        case 1: loop_task(); break;
        case 2: list_task(); break;
        case 3: facts_task(); break;
        case 4: term_task(); break;
        default: control_task(); break;
        }
    }
    printf("main :-\n");
    for (int i = 0; i < ntask; i++) printf("    ( t%d -> true ; write(failed(%d)), nl )%s\n", i, i, i + 1 < ntask ? "," : ".");
    return 0;
}
