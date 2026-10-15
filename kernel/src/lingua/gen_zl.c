/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_zl.c — random program generator for differential testing against the
 * reference implementations (ref_check.sh runs gfortran and gforth on the
 * same programs as zxlang and requires identical output).
 *
 * The generator executes what it writes: every expression is evaluated
 * with the target language's rules (truncating division for FORTRAN,
 * floored for Forth) and any step that would leave 32 bits or divide by
 * zero is rewritten before it is printed, so each program is well defined
 * in the language and the references never meet undefined behaviour.
 *
 * Usage: gen_zl f|fs <seed>   (writes the program to stdout) */
#include <stdarg.h>
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

static int forth;
static long long var[6];
static const char *const fvn[6] = {"IA", "JB", "KC", "LD", "MX", "NY"};
static const char *const qvn[6] = {"QA", "QB", "QC", "QD", "QE", "QF"};
static const char *const *vn = fvn;

static int fits(long long v)
{
    return v >= -2147483647ll && v <= 2147483647ll;
}

static long long fdiv(long long a, long long b)
{
    long long q = a / b;
    if (forth && (a % b) && ((a % b < 0) != (b < 0))) q--;
    return q;
}

static long long fmod_(long long a, long long b)
{
    return a - fdiv(a, b) * b;
}

/* Expression of the given depth: text into out, value returned. */
static long long expr(char *out, int depth)
{
    unsigned k = depth <= 0 ? rnd(2) : rnd(10);
    if (k == 0) {
        long long c = rnd(40);
        sprintf(out, "%lld", c);
        return c;
    }
    if (k == 1) {
        int v = (int) rnd(6);
        sprintf(out, forth ? "%s @" : "%s", vn[v]);
        return var[v];
    }
    char a[4096], b[4096];
    long long x = expr(a, depth - 1), y = expr(b, depth - 1), r;
    static const char *const fop[] = {"+", "-", "*", "/", "MOD", "MAX", "MIN", "ABS", "NEG"};
    unsigned op = rnd(9);
    if ((op == 3 || op == 4) && y == 0) op = 0;
    switch (op) {
    case 0:
        r = x + y;
        break;
    case 1:
        r = x - y;
        break;
    case 2:
        r = x * y;
        break;
    case 3:
        r = forth ? fdiv(x, y) : x / y;
        break;
    case 4:
        r = forth ? fmod_(x, y) : x % y;
        break;
    case 5:
        r = x > y ? x : y;
        break;
    case 6:
        r = x < y ? x : y;
        break;
    case 7:
        r = x < 0 ? -x : x;
        break;
    default:
        r = -x;
        break;
    }
    if (!fits(r)) { /* keep it in 32 bits: fall back to the left operand */
        strcpy(out, a);
        return x;
    }
    if (forth) {
        if (op == 7)
            sprintf(out, "%s ABS", a);
        else if (op == 8)
            sprintf(out, "%s NEGATE", a);
        else
            sprintf(out, "%s %s %s", a, b, fop[op]);
    } else {
        const char *sp = rnd(3) ? " " : "";
        if (op <= 3)
            sprintf(out, "(%s%s%s%s%s)", a, sp, fop[op], sp, b);
        else if (op == 4)
            sprintf(out, "MOD(%s, %s)", a, b);
        else if (op <= 6)
            sprintf(out, "%s(%s,%s)", fop[op], a, b);
        else if (op == 7)
            sprintf(out, "ABS(%s)", a);
        else
            sprintf(out, "(-%s)", a);
    }
    return r;
}

/* One FORTRAN statement, split into 66-column pieces with '&' in column 6
 * (blanks do not count outside character constants, so any split is fine). */
static void femit(const char *fmt, ...)
{
    char t[16384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    size_t n = strlen(t), off = 0;
    printf("      ");
    for (;;) {
        size_t take = n - off > 66 ? 66 : n - off;
        fwrite(t + off, 1, take, stdout);
        putchar('\n');
        off += take;
        if (off >= n) break;
        printf("     &");
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: gen_zl f|fs seed\n");
        return 2;
    }
    forth = !strcmp(argv[1], "fs");
    if (forth) vn = qvn;
    rng = strtoull(argv[2], 0, 10) * 0x9E3779B97F4A7C15ull + 7;
    static char e1[4096], e2[4096], e3[4096];
    if (forth) {
        for (int v = 0; v < 6; v++) {
            var[v] = (long long) rnd(50) - 10;
            printf("VARIABLE %s %lld %s !\n", vn[v], var[v], vn[v]);
        }
    } else {
        printf("      PROGRAM G\n      INTEGER IA, JB, KC, LD, MX, NY\n");
        for (int v = 0; v < 6; v++) {
            var[v] = (long long) rnd(50) - 10;
            femit("%s = %lld", vn[v], var[v]);
        }
    }
    int nstmt = 10 + (int) rnd(30);
    for (int s = 0; s < nstmt; s++) {
        unsigned kind = rnd(3);
        int v = (int) rnd(6);
        int d = 1 + (int) rnd(4);
        if (kind == 0) { /* assignment */
            long long x = expr(e1, d);
            var[v] = x;
            if (forth)
                printf("%s %s !\n", e1, vn[v]);
            else
                femit("%s = %s", vn[v], e1);
        } else if (kind == 1) { /* print */
            long long x = expr(e1, d), y = expr(e2, d);
            (void) x;
            (void) y;
            if (forth)
                printf("%s . %s . CR\n", e1, e2);
            else
                femit("PRINT *, %s, %s", e1, e2);
        } else { /* conditional assignment */
            long long x = expr(e1, d), y = expr(e2, d), z = expr(e3, d);
            int rel = (int) rnd(3);
            int t = rel == 0 ? x < y : rel == 1 ? x == y : x > y;
            if (forth) {
                static const char *const fr[] = {"<", "=", ">"};
                printf(": T%d %s %s %s IF %s %s ! THEN ; T%d\n", s, e1, e2, fr[rel], e3, vn[v], s);
            } else {
                static const char *const rr[] = {".LT.", ".EQ.", ".GT."};
                if (rnd(2)) {
                    femit("IF (%s %s %s) %s = %s", e1, rr[rel], e2, vn[v], e3);
                } else {
                    femit("IF (%s %s %s) THEN", e1, rr[rel], e2);
                    femit("  %s = %s", vn[v], e3);
                    femit("END IF");
                }
            }
            if (t) var[v] = z;
        }
    }
    if (forth) {
        for (int v = 0; v < 6; v++) printf("%s ? ", vn[v]);
        printf("CR\n");
    } else {
        printf("      PRINT *, IA, JB, KC, LD, MX, NY\n      END\n");
    }
    return 0;
}
