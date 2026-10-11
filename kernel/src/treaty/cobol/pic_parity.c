/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pic_parity.c — PIC S9(18) parity between zxt_pic_* and GnuCOBOL.
 *
 *   pic_parity gen N SEED vec.txt    write N vectors (op a b)
 *   cobcrun / ./PICPAR               COBOL computes out.txt (PICPAR.cob)
 *   pic_parity check vec.txt out.txt compare every answer with zxt_pic_*
 *
 * The vectors lean on the edges that matter: 0, +-1, the 9- and 10-digit
 * boundary (products near 10^18), +-(10^18 - 1) and their neighbours,
 * plus values of every digit length and both signs. */
#include "zxt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_rng;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 2685821657736338717ULL;
}

static int64_t value(void)
{
    static const int64_t edge[] = {0,
                                   1,
                                   2,
                                   3,
                                   7,
                                   9,
                                   10,
                                   99,
                                   999999999,
                                   1000000000,
                                   1000000001,
                                   31622776,
                                   316227766,
                                   999999999999999999LL,
                                   999999999999999998LL,
                                   500000000000000000LL,
                                   499999999999999999LL,
                                   500000000000000001LL,
                                   333333333333333333LL,
                                   576460752303423488LL};
    int64_t v;
    if (rnd() % 3 == 0) {
        v = edge[rnd() % (sizeof edge / sizeof edge[0])];
    } else {
        v = 0;
        for (uint64_t d = 1 + rnd() % 18; d > 0; d--) v = v * 10 + (int64_t) (rnd() % 10);
    }
    return rnd() % 2 ? -v : v;
}

static void put_s18(char *d, int64_t v)
{
    uint64_t m = (uint64_t) (v < 0 ? -v : v);
    d[0] = v < 0 ? '-' : '+';
    for (int i = 18; i >= 1; i--) {
        d[i] = (char) ('0' + m % 10);
        m /= 10;
    }
}

static bool get_s18(const char *s, int64_t *v)
{
    int64_t m = 0;
    if (s[0] != '+' && s[0] != '-') return false;
    for (int i = 1; i <= 18; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        m = m * 10 + (s[i] - '0');
    }
    *v = s[0] == '-' ? -m : m;
    return true;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "gen") == 0) {
        unsigned long long n = strtoull(argv[2], NULL, 10);
        g_rng = strtoull(argv[3], NULL, 10) * 0x9E3779B97F4A7C15ULL + 1;
        FILE *f = fopen(argv[4], "w");
        if (!f) return 2;
        char line[40];
        for (unsigned long long i = 0; i < n; i++) {
            line[0] = "ASMD"[rnd() % 4];
            put_s18(line + 1, value());
            put_s18(line + 20, line[0] == 'D' && rnd() % 16 == 0 ? 0 : value());
            line[39] = 0;
            fprintf(f, "%s\n", line);
        }
        return fclose(f) == 0 ? 0 : 2;
    }
    if (argc == 4 && strcmp(argv[1], "check") == 0) {
        FILE *v = fopen(argv[2], "r"), *o = fopen(argv[3], "r");
        if (!v || !o) return 2;
        char vl[64], ol[64];
        unsigned long long n = 0, bad = 0, size_err = 0, by_op[4] = {0};
        while (fgets(vl, sizeof vl, v)) {
            int64_t a, b, r = 0, rem = 0, cr, crem;
            if (!fgets(ol, sizeof ol, o) || !get_s18(vl + 1, &a) || !get_s18(vl + 20, &b) ||
                !get_s18(ol + 1, &cr) || !get_s18(ol + 20, &crem)) {
                printf("line %llu: malformed\n", n + 1);
                return 1;
            }
            bool ok;
            switch (vl[0]) {
            case 'A':
                ok = zxt_pic_add(a, b, &r);
                by_op[0]++;
                break;
            case 'S':
                ok = zxt_pic_sub(a, b, &r);
                by_op[1]++;
                break;
            case 'M':
                ok = zxt_pic_mul(a, b, &r);
                by_op[2]++;
                break;
            default:
                ok = zxt_pic_div(a, b, &r, &rem);
                by_op[3]++;
                break;
            }
            n++;
            size_err += !ok;
            bool cobol_ok = ol[0] == 'K';
            if (ok != cobol_ok || (ok && (r != cr || rem != crem))) {
                if (bad++ < 10)
                    printf("line %llu: %c %lld %lld: C %s %lld r%lld, COBOL %c %lld r%lld\n", n,
                           vl[0], (long long) a, (long long) b, ok ? "ok" : "size error",
                           (long long) r, (long long) rem, ol[0], (long long) cr, (long long) crem);
            }
        }
        if (fgets(ol, sizeof ol, o)) {
            printf("COBOL wrote more answers than vectors\n");
            return 1;
        }
        printf("%s: %llu vectors (A %llu, S %llu, M %llu, D %llu), %llu size errors, %llu "
               "mismatches\n",
               bad || !n ? "FAIL" : "PASS", n, by_op[0], by_op[1], by_op[2], by_op[3], size_err,
               bad);
        return bad || !n || !size_err ? 1 : 0;
    }
    fprintf(stderr, "usage: pic_parity gen N SEED FILE | check VEC OUT\n");
    return 2;
}
