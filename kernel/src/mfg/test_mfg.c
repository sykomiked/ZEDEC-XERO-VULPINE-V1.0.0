/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mfg.c — host tests for the manufacturing modules (F.2, F.4, F.6, F.10).
 *
 * WHAT IT DOES
 *   Exercises the G-code checker (exact parsing, every check with its line
 *   number, arcs, segment/box edge cases, limits, fuzzing), the four-valued
 *   QC truth table, nature budgets and the admission queue.
 *
 * HONEST LIMITS
 *   Host-only test; programs here are synthetic, not from real machines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mfg_gcode.h"
#include "mfg_qc.h"
#include "mfg_budget.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}
static int64_t rnd_range(int64_t lo, int64_t hi)
{
    return lo + (int64_t) (rnd() % (uint64_t) (hi - lo + 1));
}

/* ---- G-code helpers --------------------------------------------------- */

static mfg_gc_profile_t P;
static mfg_gc_result_t R;

static void set3(int64_t *d, int64_t x, int64_t y, int64_t z)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
}

static void prof_default(void)
{
    memset(&P, 0, sizeof P);
    set3(P.env_lo, 0, 0, -100000);
    set3(P.env_hi, 500000, 300000, 100000);
    set3(P.start, 0, 0, 50000);
    P.safe_z = 5000;
    P.feed_min_cut = 10000;
    P.feed_max = 5000000;
    P.spindle_min = 100;
    P.spindle_max = 24000;
    P.arc_r2_tol = 0;
    P.nbox = 2;
    set3(P.box[0].lo, 100000, 100000, -100000);
    set3(P.box[0].hi, 120000, 120000, 20000);
    P.box[0].kind = MFG_BOX_FIXTURE;
    set3(P.box[1].lo, 200000, 0, -100000);
    set3(P.box[1].hi, 300000, 100000, 10000);
    P.box[1].kind = MFG_BOX_KEEPOUT;
}

/* Append s to dst (capacity cap, current length *len, kept NUL-terminated).
 * Returns 0 and leaves dst untouched if it would not fit. */
static int append(char *dst, size_t cap, size_t *len, const char *s)
{
    size_t k = strlen(s);
    if (*len >= cap || k >= cap - *len) return 0;
    memcpy(dst + *len, s, k + 1);
    *len += k;
    return 1;
}

static int run(const char *prog)
{
    return mfg_gc_check(prog, strlen(prog), &P, &R);
}

static int has(int code, uint32_t line)
{
    uint32_t i;
    for (i = 0; i < R.nissue; i++)
        if (R.issue[i].code == code && R.issue[i].line == line) return 1;
    return 0;
}

static int count_code(int code)
{
    uint32_t i;
    int n = 0;
    for (i = 0; i < R.nissue; i++)
        if (R.issue[i].code == code) n++;
    return n;
}

/* run and expect exactly this one issue on this line with this verdict */
static int only(const char *prog, int code, uint32_t line, int verdict)
{
    if (run(prog) != MFG_OK) return 0;
    return R.nissue == 1 && R.dropped == 0 && has(code, line) && R.verdict == verdict;
}

#define HDR "G21 G90 G17\n"
#define CUT "S10000 M3\n"

static void test_parse(void)
{
    int64_t v = 0;
    CHECK(mfg_gc_parse_um("1.5", 3, 0, &v) == MFG_OK && v == 1500);
    CHECK(mfg_gc_parse_um("-0.001", 6, 0, &v) == MFG_OK && v == -1);
    CHECK(mfg_gc_parse_um("+2.500", 6, 0, &v) == MFG_OK && v == 2500);
    CHECK(mfg_gc_parse_um("-.5", 3, 0, &v) == MFG_OK && v == -500);
    CHECK(mfg_gc_parse_um("5.", 2, 0, &v) == MFG_OK && v == 5000);
    CHECK(mfg_gc_parse_um("007", 3, 0, &v) == MFG_OK && v == 7000);
    CHECK(mfg_gc_parse_um("0", 1, 0, &v) == MFG_OK && v == 0);
    CHECK(mfg_gc_parse_um("12.000000000", 12, 0, &v) == MFG_OK && v == 12000);
    CHECK(mfg_gc_parse_um("0.0001", 6, 0, &v) == MFG_ERR_STATE);
    CHECK(mfg_gc_parse_um("0.000000001", 11, 0, &v) == MFG_ERR_STATE);
    CHECK(mfg_gc_parse_um("1.0000000000", 12, 0, &v) == MFG_ERR_STATE); /* 10 digits */
    /* inches: 1 in = 25400 um exactly */
    CHECK(mfg_gc_parse_um("1", 1, 1, &v) == MFG_OK && v == 25400);
    CHECK(mfg_gc_parse_um("1.0000", 6, 1, &v) == MFG_OK && v == 25400);
    CHECK(mfg_gc_parse_um("0.01", 4, 1, &v) == MFG_OK && v == 254);
    CHECK(mfg_gc_parse_um("0.005", 5, 1, &v) == MFG_OK && v == 127);
    CHECK(mfg_gc_parse_um("-2.5", 4, 1, &v) == MFG_OK && v == -63500);
    CHECK(mfg_gc_parse_um("0.001", 5, 1, &v) == MFG_ERR_STATE);  /* 25.4 um */
    CHECK(mfg_gc_parse_um("0.0005", 6, 1, &v) == MFG_ERR_STATE); /* 12.7 um */
    CHECK(mfg_gc_parse_um("0.0125", 6, 1, &v) == MFG_ERR_STATE); /* 317.5 um */
    /* malformed */
    CHECK(mfg_gc_parse_um("", 0, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um(".", 1, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("-", 1, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("+.", 2, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("1.2.3", 5, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("1e3", 3, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("1-", 2, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("--1", 3, 0, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("99999999999999999999", 20, 0, &v) == MFG_ERR_OVERFLOW);
    CHECK(mfg_gc_parse_um("9223372036854776", 16, 0, &v) == MFG_ERR_OVERFLOW); /* *1000 */
    CHECK(mfg_gc_parse_um("9223372036854.775", 17, 0, &v) == MFG_OK && v == 9223372036854775LL);
    CHECK(mfg_gc_parse_um(NULL, 1, 0, &v) == MFG_ERR_NULL);
    CHECK(mfg_gc_parse_um("1", 1, 0, NULL) == MFG_ERR_NULL);
    CHECK(mfg_gc_parse_um("1", 1, 2, &v) == MFG_ERR_RANGE);
    CHECK(mfg_gc_parse_um("1", 1, -1, &v) == MFG_ERR_RANGE);
    /* property: mm text with 3 decimals round-trips exactly */
    {
        int i, ok = 1, ok2 = 1;
        char buf[64];
        for (i = 0; i < 5000; i++) {
            int64_t u = rnd_range(-500000000, 500000000), a = u < 0 ? -u : u, got = 0;
            int n = snprintf(buf, sizeof buf, "%s%lld.%03lld", u < 0 ? "-" : "",
                             (long long) (a / 1000), (long long) (a % 1000));
            if (mfg_gc_parse_um(buf, (size_t) n, 0, &got) != MFG_OK || got != u) ok = 0;
            /* inches in hundredths: m/100 in = 254 m um */
            {
                int64_t m = rnd_range(-1000000, 1000000), b = m < 0 ? -m : m;
                n = snprintf(buf, sizeof buf, "%s%lld.%02lld", m < 0 ? "-" : "",
                             (long long) (b / 100), (long long) (b % 100));
                if (mfg_gc_parse_um(buf, (size_t) n, 1, &got) != MFG_OK || got != 254 * m) ok2 = 0;
            }
        }
        CHECK(ok);
        CHECK(ok2);
    }
}

static void test_isqrt(void)
{
    int i, ok = 1;
    CHECK(mfg_isqrt_u64(0) == 0);
    CHECK(mfg_isqrt_u64(1) == 1);
    CHECK(mfg_isqrt_u64(2) == 1);
    CHECK(mfg_isqrt_u64(3) == 1);
    CHECK(mfg_isqrt_u64(4) == 2);
    CHECK(mfg_isqrt_u64(15) == 3);
    CHECK(mfg_isqrt_u64(16) == 4);
    CHECK(mfg_isqrt_u64(17) == 4);
    CHECK(mfg_isqrt_u64(200000000) == 14142);
    CHECK(mfg_isqrt_u64(UINT64_MAX) == 4294967295u);
    CHECK(mfg_isqrt_u64(4294967295ull * 4294967295ull) == 4294967295u);
    CHECK(mfg_isqrt_u64(4294967295ull * 4294967295ull - 1) == 4294967294u);
    for (i = 0; i < 20000; i++) {
        uint64_t v = rnd() >> (rnd() % 64), r = mfg_isqrt_u64(v);
        if (!(r * r <= v && (r + 1 > 4294967295u || (r + 1) * (r + 1) > v))) ok = 0;
    }
    CHECK(ok);
}

static void box_set(mfg_box_t *b, int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1,
                    int64_t z1)
{
    set3(b->lo, x0, y0, z0);
    set3(b->hi, x1, y1, z1);
    b->kind = MFG_BOX_FIXTURE;
}

static int seg(int64_t ax, int64_t ay, int64_t az, int64_t bx, int64_t by, int64_t bz,
               const mfg_box_t *b)
{
    int64_t a[3], c[3];
    set3(a, ax, ay, az);
    set3(c, bx, by, bz);
    return mfg_gc_segment_hits_box(a, c, b);
}

static void test_segbox(void)
{
    mfg_box_t b;
    int i, ok = 1;
    int64_t a[3] = {0, 0, 0};
    box_set(&b, 0, 0, 0, 10, 10, 10);
    CHECK(seg(-5, 5, 5, 15, 5, 5, &b) == 1);   /* through */
    CHECK(seg(15, 5, 5, -5, 5, 5, &b) == 1);   /* reversed */
    CHECK(seg(-5, 10, 5, 15, 10, 5, &b) == 1); /* along a face (parallel, on it) */
    CHECK(seg(-5, 11, 5, 15, 11, 5, &b) == 0); /* parallel, 1 um outside */
    CHECK(seg(-5, -1, 5, 15, -1, 5, &b) == 0);
    CHECK(seg(-5, 5, 5, 0, 5, 5, &b) == 1);   /* ends on a face */
    CHECK(seg(-5, 5, 5, -1, 5, 5, &b) == 0);  /* stops 1 um short */
    CHECK(seg(5, 15, 15, 15, 5, 5, &b) == 1); /* touches only the corner (10,10,10) */
    CHECK(seg(5, 16, 15, 15, 6, 5, &b) == 0); /* misses the corner */
    CHECK(seg(0, 20, 5, 20, 0, 5, &b) == 1);  /* touches edge x=y=10 */
    CHECK(seg(0, 21, 5, 21, 0, 5, &b) == 0);
    CHECK(seg(5, 5, 5, 5, 5, 5, &b) == 1);   /* zero length inside */
    CHECK(seg(11, 5, 5, 11, 5, 5, &b) == 0); /* zero length outside */
    CHECK(seg(10, 10, 10, 10, 10, 10, &b) == 1);
    CHECK(seg(2, 2, 2, 3, 3, 3, &b) == 1); /* fully inside */
    CHECK(seg(-1, -1, -1, 11, 11, 11, &b) == 1);
    CHECK(seg(-1, 22, 5, 22, -1, 5, &b) == 0); /* diagonal near-miss past corner */
    CHECK(seg(-1, 21, 5, 21, -1, 5, &b) == 1); /* through the edge point (10,10) */
    /* rational boundary: line x from 0 to 3, y from 30 to 0; enters y<=10 at
     * t=2/3 where x=2: box x in [2,5] touches, [3,5] -> t=1 y=0 inside too */
    box_set(&b, 2, 0, 0, 5, 10, 0);
    CHECK(seg(0, 30, 0, 3, 0, 0, &b) == 1);
    box_set(&b, 0, 0, 0, 1, 10, 0); /* x<=1 needs t<=1/3 -> y>=20 */
    CHECK(seg(0, 30, 0, 3, 0, 0, &b) == 0);
    box_set(&b, 0, 0, 0, 1, 20, 0); /* t=1/3 exactly: (1,20) */
    CHECK(seg(0, 30, 0, 3, 0, 0, &b) == 1);
    box_set(&b, 0, 0, 0, 1, 19, 0);
    CHECK(seg(0, 30, 0, 3, 0, 0, &b) == 0);
    /* errors */
    CHECK(mfg_gc_segment_hits_box(NULL, a, &b) == MFG_ERR_NULL);
    CHECK(mfg_gc_segment_hits_box(a, NULL, &b) == MFG_ERR_NULL);
    CHECK(mfg_gc_segment_hits_box(a, a, NULL) == MFG_ERR_NULL);
    CHECK(seg(MFG_GC_COORD_MAX + 1, 0, 0, 0, 0, 0, &b) == MFG_ERR_RANGE);
    CHECK(seg(0, 0, 0, 0, -MFG_GC_COORD_MAX - 1, 0, &b) == MFG_ERR_RANGE);
    CHECK(seg(MFG_GC_COORD_MAX, 0, 0, -MFG_GC_COORD_MAX, 0, 0, &b) == 1);
    box_set(&b, 1, 0, 0, 0, 10, 10);
    CHECK(seg(0, 0, 0, 1, 1, 1, &b) == MFG_ERR_RANGE); /* reversed box */
    box_set(&b, 0, 0, 0, 1, 1, 1);
    b.kind = 7;
    CHECK(seg(0, 0, 0, 1, 1, 1, &b) == MFG_ERR_RANGE);
    /* property: compare with exact sampling on the grid t = i/840 (every
     * entry/exit parameter has a denominator dividing 840 here) */
    for (i = 0; i < 3000; i++) {
        int64_t p0[3], p1[3], k, exp = 0;
        int ax;
        for (ax = 0; ax < 3; ax++) {
            int64_t l = rnd_range(-4, 4), h = rnd_range(-4, 4);
            b.lo[ax] = l < h ? l : h;
            b.hi[ax] = l < h ? h : l;
            p0[ax] = rnd_range(-4, 4);
            p1[ax] = rnd_range(-4, 4);
        }
        b.kind = MFG_BOX_FIXTURE;
        for (k = 0; k <= 840 && !exp; k++) {
            int in = 1;
            for (ax = 0; ax < 3; ax++) {
                int64_t q = p0[ax] * 840 + k * (p1[ax] - p0[ax]);
                if (q < b.lo[ax] * 840 || q > b.hi[ax] * 840) in = 0;
            }
            if (in) exp = 1;
        }
        if (mfg_gc_segment_hits_box(p0, p1, &b) != exp) ok = 0;
    }
    CHECK(ok);
}

static void test_clean(void)
{
    const char *prog = "%\n"
                       "(clean test program)\n"
                       "N10 G21 G90 G17\n"
                       "N20 G0 Z10\n"
                       "N30 G0 X50 Y50 ; position\n"
                       "N40 S12000 M3\n"
                       "N50 G1 Z-1 F300\n"
                       "N60 G1 X80 Y50 F1200.5\n"
                       "N70 G3 X90 Y60 I0 J10\n"
                       "N80 G0 Z10\n"
                       "N90 M5\n"
                       "N100 M30\n"
                       "%\n";
    prof_default();
    CHECK(run(prog) == MFG_OK);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(R.nissue == 0 && R.dropped == 0);
    CHECK(R.moves == 6);
    CHECK(R.lines == 13);
    CHECK(R.end_pos[0] == 90000 && R.end_pos[1] == 60000 && R.end_pos[2] == 10000);
    /* CRLF and lower case */
    CHECK(run("g21 g90\r\ng0 z10\r\nm30\r\n") == MFG_OK && R.verdict == MFG_RELEASE_OK &&
          R.nissue == 0 && R.end_pos[2] == 10000);
    /* inch program, incremental */
    CHECK(run("G20 G91\nG0 X1 Y0.5\nG0 X1\nM2\n") == MFG_OK && R.verdict == MFG_RELEASE_OK &&
          R.end_pos[0] == 50800 && R.end_pos[1] == 12700 && R.end_pos[2] == 50000);
    /* inch feed: F10 in/min = 254000 um/min, fine */
    CHECK(run("G20 G90\nS1000 M3\nG1 X1 F10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* N word recorded */
    CHECK(run(HDR "N77 G0 X600\nM30\n") == MFG_OK && R.nissue == 1 && R.issue[0].nword == 77 &&
          R.issue[0].line == 2);
    CHECK(run(HDR "G0 X600\nM30\n") == MFG_OK && R.issue[0].nword == -1);
    /* empty program: no end -> HOLD */
    CHECK(mfg_gc_check(NULL, 0, &P, &R) == MFG_OK && R.verdict == MFG_HOLD &&
          has(MFG_ISS_NO_END, 0));
    /* comment-only lines and blank lines */
    CHECK(run("(a)\n\n  ; b\nG21 G90 (units) G0 Z20 (up)\nM30") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK && R.end_pos[2] == 20000 && R.lines == 5);
}

static void test_checks(void)
{
    char big[400];
    prof_default();
    /* words */
    CHECK(only(HDR "G0 Z10\nT1 M6\nM30\n", MFG_ISS_UNKNOWN_WORD, 3, MFG_REJECT));
    CHECK(only(HDR "G18\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "G1.1 X1\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "M8\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "M3.5\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "G2 X10 R5\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "G-1\nM30\n", MFG_ISS_UNKNOWN_WORD, 2, MFG_REJECT));
    CHECK(only(HDR "G1.0 X1 F100\nM30\n", MFG_ISS_CUT_SPINDLE_OFF, 2, MFG_REJECT)); /* G1.0==G1 */
    /* malformed */
    CHECK(only(HDR "G0 X1..2\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X 1\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1 $\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1 (open\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1 (a (b) c)\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1 N5\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "N1.5 G0 X1\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "% G0\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "%%\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X99999999999999999999\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1.0000000001\nM30\n", MFG_ISS_TOO_PRECISE, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1\nS100.5\nM30\n", MFG_ISS_MALFORMED, 3, MFG_REJECT));
    CHECK(only(HDR "S-5\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "F-5\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X10 I5\nM30\n", MFG_ISS_MALFORMED, 2, MFG_REJECT));
    {
        const char p[] = "G21 G90\nG0 X1\0\nM30\n";
        CHECK(mfg_gc_check(p, sizeof p - 1, &P, &R) == MFG_OK && R.nissue == 1 &&
              has(MFG_ISS_MALFORMED, 2));
    }
    /* conflicts */
    CHECK(only(HDR "G0 G1 X1\nM30\n", MFG_ISS_CONFLICT, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X1 X2\nM30\n", MFG_ISS_CONFLICT, 2, MFG_REJECT));
    CHECK(only("G20 G21\nM30\n", MFG_ISS_CONFLICT, 1, MFG_REJECT));
    CHECK(only("G90 G91 G21\nM30\n", MFG_ISS_CONFLICT, 1, MFG_REJECT));
    CHECK(only(HDR "M3 M5\nM30\n", MFG_ISS_CONFLICT, 2, MFG_REJECT));
    CHECK(only(HDR "M2 M30\n", MFG_ISS_CONFLICT, 2, MFG_REJECT) == 0); /* also NO_END */
    CHECK(R.verdict == MFG_REJECT && has(MFG_ISS_CONFLICT, 2) && has(MFG_ISS_NO_END, 0));
    CHECK(only(HDR "G17 G17 G0 X1\nM30\n", MFG_ISS_CONFLICT, 2, MFG_REJECT) ==
          0); /* G17 twice ok */
    CHECK(R.verdict == MFG_RELEASE_OK);
    /* precision */
    CHECK(only(HDR "G0 X0.0001\nM30\n", MFG_ISS_TOO_PRECISE, 2, MFG_REJECT));
    CHECK(only("G20 G90\nG0 X0.001\nM30\n", MFG_ISS_TOO_PRECISE, 2, MFG_REJECT));
    CHECK(only("G20 G90\nS1000 M3\nG1 X0.01 F0.001\nM30\n", MFG_ISS_TOO_PRECISE, 3, MFG_REJECT));
    /* line too long */
    memset(big, ' ', sizeof big);
    memcpy(big, "G21 G90\n", 8);
    big[8 + MFG_GC_MAX_LINE] = '\n';
    memcpy(big + 9 + MFG_GC_MAX_LINE, "M30\n", 4);
    CHECK(mfg_gc_check(big, 13 + MFG_GC_MAX_LINE, &P, &R) == MFG_OK && R.nissue == 0);
    big[8 + MFG_GC_MAX_LINE] = ' ';
    big[9 + MFG_GC_MAX_LINE] = '\n';
    memcpy(big + 10 + MFG_GC_MAX_LINE, "M30\n", 4);
    CHECK(mfg_gc_check(big, 14 + MFG_GC_MAX_LINE, &P, &R) == MFG_OK && R.nissue == 1 &&
          has(MFG_ISS_LINE_TOO_LONG, 2) && R.verdict == MFG_REJECT);
    /* modal state */
    CHECK(only("G0 Z10\nM30\n", MFG_ISS_MODAL_UNSET, 1, MFG_HOLD));
    CHECK(only("G21\nG0 Z10\nG0 Z20\nM30\n", MFG_ISS_MODAL_UNSET, 2, MFG_HOLD)); /* once */
    CHECK(count_code(MFG_ISS_MODAL_UNSET) == 1);
    CHECK(only("G90\nG0 Z10\nM30\n", MFG_ISS_MODAL_UNSET, 2, MFG_HOLD));
    CHECK(only(HDR "X10\nM30\n", MFG_ISS_MOTION_UNSET, 2, MFG_REJECT));
    CHECK(only(HDR "G0 Z10\n", MFG_ISS_NO_END, 0, MFG_HOLD));
    CHECK(only(HDR "M30\nG0 Z10\nG0 Z20\n", MFG_ISS_AFTER_END, 3, MFG_HOLD));
    CHECK(R.end_pos[2] == 50000); /* not executed */
    CHECK(only(HDR "M2\n(comment)\n%\n", MFG_ISS_AFTER_END, 3, MFG_HOLD) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    /* envelope */
    CHECK(only(HDR "G0 X500.001\nM30\n", MFG_ISS_ENVELOPE, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X500\nM30\n", MFG_ISS_ENVELOPE, 2, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR "G0 Z10\nG0 Y-0.001\nM30\n", MFG_ISS_ENVELOPE, 3, MFG_REJECT));
    CHECK(only(HDR "G0 Z100.001\nM30\n", MFG_ISS_ENVELOPE, 2, MFG_REJECT));
    /* coordinate range stops checking (no NO_END added) */
    CHECK(only(HDR "G0 X500000.001\nM30\n", MFG_ISS_COORD_RANGE, 2, MFG_REJECT));
    CHECK(only(HDR "G0 X-500000.001\n", MFG_ISS_COORD_RANGE, 2, MFG_REJECT));
    CHECK(run(HDR "G91\nG0 X400000\nG0 X200000\nM30\n") == MFG_OK && R.verdict == MFG_REJECT &&
          has(MFG_ISS_ENVELOPE, 3) && has(MFG_ISS_COORD_RANGE, 4) && R.nissue == 2);
    CHECK(only(HDR "G2 X1 I500000.001\nM30\n", MFG_ISS_COORD_RANGE, 2, MFG_REJECT));
    CHECK(only(HDR "F99999999999999999\nM30\n", MFG_ISS_COORD_RANGE, 2, MFG_REJECT));
    /* feed */
    CHECK(only(HDR CUT "G1 X10\nM30\n", MFG_ISS_FEED_UNSET, 3, MFG_REJECT));
    CHECK(only(HDR CUT "G1 X10 F5000.001\nM30\n", MFG_ISS_FEED_HIGH, 3, MFG_REJECT));
    CHECK(only(HDR CUT "G1 X10 F5000\nM30\n", MFG_ISS_FEED_HIGH, 3, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G1 X10 F9.999\nM30\n", MFG_ISS_FEED_LOW, 3, MFG_REJECT));
    CHECK(only(HDR CUT "G1 X10 F0\nM30\n", MFG_ISS_FEED_LOW, 3, MFG_REJECT));
    CHECK(only(HDR CUT "G1 X10 F10\nM30\n", MFG_ISS_FEED_LOW, 3, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "F9000\nG0 X10\nM30\n", MFG_ISS_FEED_HIGH, 4, MFG_REJECT) == 0); /* rapid */
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only("G20 G90\nS1000 M3\nG1 X1 F197\nM30\n", MFG_ISS_FEED_HIGH, 3, MFG_REJECT));
    CHECK(only("G20 G90\nS1000 M3\nG1 X1 F196.85\nM30\n", MFG_ISS_FEED_HIGH, 3, MFG_REJECT) == 0);
    /* spindle */
    CHECK(only(HDR "S99 M3\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT));
    CHECK(only(HDR "S100 M4\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR "S24000 M3\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR "S24001 M3\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT));
    CHECK(only(HDR "M3\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT));
    CHECK(only(HDR "S1000 M3\nS30000\nM30\n", MFG_ISS_SPINDLE_RANGE, 3, MFG_REJECT));
    CHECK(only(HDR "S30000\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT) == 0); /* off */
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR "S30000\nS1000 M3\nM30\n", MFG_ISS_SPINDLE_RANGE, 2, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR "G1 X10 F100\nM30\n", MFG_ISS_CUT_SPINDLE_OFF, 2, MFG_REJECT));
    CHECK(only(HDR CUT "G1 X10 F100\nG1 X20 M5\nM30\n", MFG_ISS_CUT_SPINDLE_OFF, 4, MFG_REJECT));
    CHECK(only(HDR CUT "M5\nG0 X20\nM30\n", MFG_ISS_CUT_SPINDLE_OFF, 4, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    /* rapids */
    CHECK(only(HDR "G0 Z4.999\nM30\n", MFG_ISS_RAPID_LOW, 2, MFG_REJECT));
    CHECK(only(HDR "G0 Z5\nG0 X50\nM30\n", MFG_ISS_RAPID_LOW, 2, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G1 Z-1 F100\nG0 X50\nM30\n", MFG_ISS_RAPID_LOW, 4, MFG_REJECT));
    CHECK(only(HDR CUT "G1 Z-1 F100\nG0 Z10\nM30\n", MFG_ISS_RAPID_LOW, 4, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK); /* vertical retract up is fine */
    CHECK(only(HDR CUT "G1 Z-1 F100\nG0 Z4\nM30\n", MFG_ISS_RAPID_LOW, 4, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK); /* still below, but going up */
    CHECK(only(HDR CUT "G1 Z-1 F100\nG0 Z-2\nM30\n", MFG_ISS_RAPID_LOW, 4, MFG_REJECT));
    CHECK(only(HDR CUT "G1 Z-1 F100\nG0 X1 Z10\nM30\n", MFG_ISS_RAPID_LOW, 4, MFG_REJECT));
    CHECK(only(HDR "G0 Z5\nG0 X1 Z4.999\nM30\n", MFG_ISS_RAPID_LOW, 3, MFG_REJECT));
    CHECK(only(HDR "G0 Z10\nG0 X250 Y50\nM30\n", MFG_ISS_RAPID_KEEPOUT, 3, MFG_REJECT));
    CHECK(R.issue[0].box == 1);
    CHECK(only(HDR "G0 Z10.001\nG0 X250 Y50\nM30\n", MFG_ISS_RAPID_KEEPOUT, 3, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z10\nG1 X250 Y50 F100\nM30\n", MFG_ISS_RAPID_KEEPOUT, 4, MFG_REJECT) ==
          0); /* cutting may enter a keep-out */
    CHECK(R.verdict == MFG_RELEASE_OK);
    /* fixture (box 0: x,y in [100,120] mm, z <= 20 mm) */
    CHECK(only(HDR "G0 Z20\nG0 Y110\nG0 X200\nM30\n", MFG_ISS_FIXTURE, 4, MFG_REJECT));
    CHECK(R.issue[0].box == 0);
    CHECK(only(HDR "G0 Z20.001\nG0 Y110\nG0 X200\nM30\n", MFG_ISS_FIXTURE, 4, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z10\nG0 Y110\nG1 X200 F100\nM30\n", MFG_ISS_FIXTURE, 5, MFG_REJECT));
    CHECK(only(HDR "G0 Z30\nG0 X100 Y100\nG0 Z20\nM30\n", MFG_ISS_FIXTURE, 4,
               MFG_REJECT)); /* corner */
    CHECK(only(HDR "G0 Z10\nG0 X99.999 Y200\nG0 Y0\nM30\n", MFG_ISS_FIXTURE, 4, MFG_REJECT) == 0);
    CHECK(R.verdict == MFG_RELEASE_OK);
    CHECK(
        only(HDR "G0 Z10\nG0 X100 Y200\nG0 Y0\nM30\n", MFG_ISS_FIXTURE, 4, MFG_REJECT)); /* face */
}

static void test_arcs(void)
{
    prof_default();
    P.nbox = 0;
#define ARC0 HDR CUT "G0 Z-1\nG0 X80 Y50\nF100\n" /* 5 lines; the arc is line 6 */
    P.safe_z = -100000;
    /* quarter CCW, radius exact */
    CHECK(run(ARC0 "G3 X90 Y60 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* radius mismatch */
    CHECK(only(ARC0 "G3 X90 Y61 I0 J10\nM30\n", MFG_ISS_ARC_RADIUS, 6, MFG_REJECT));
    P.arc_r2_tol = 20000000; /* (10000^2 vs 10000^2+1000^2): diff 1e6... tol 2e7 */
    CHECK(run(ARC0 "G3 X90 Y61 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    P.arc_r2_tol = 1000000;
    CHECK(run(ARC0 "G3 X90 Y61 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    P.arc_r2_tol = 999999;
    CHECK(only(ARC0 "G3 X90 Y61 I0 J10\nM30\n", MFG_ISS_ARC_RADIUS, 6, MFG_REJECT));
    P.arc_r2_tol = 0;
    /* centre */
    CHECK(only(ARC0 "G2 X90 Y60\nM30\n", MFG_ISS_ARC_CENTRE, 6, MFG_REJECT));
    CHECK(only(ARC0 "G2 X90 Y60 I0 J0\nM30\n", MFG_ISS_ARC_CENTRE, 6, MFG_REJECT));
    CHECK(only(ARC0 "G2 X600 Y60 I0 J0\nM30\n", MFG_ISS_ARC_CENTRE, 6, MFG_REJECT) == 0);
    CHECK(has(MFG_ISS_ARC_CENTRE, 6) && has(MFG_ISS_ENVELOPE, 6) && R.nissue == 2);
    CHECK(only(ARC0 "G2 X600 Y61 I0 J10\nM30\n", MFG_ISS_ARC_RADIUS, 6, MFG_REJECT) == 0);
    CHECK(has(MFG_ISS_ARC_RADIUS, 6) && has(MFG_ISS_ENVELOPE, 6) && R.nissue == 2);
    /* arcs need feed and spindle too */
    CHECK(
        only(HDR "G0 X80 Y50\nG3 X90 Y60 J10 F100\nM30\n", MFG_ISS_CUT_SPINDLE_OFF, 3, MFG_REJECT));
    CHECK(only(HDR CUT "G0 X80 Y50\nG3 X90 Y60 J10\nM30\n", MFG_ISS_FEED_UNSET, 4, MFG_REJECT));
    /* direction: same end points, G3 = quarter, G2 = three quarters through
     * the -X extreme (70,60). Fixture around (70,60). */
    P.nbox = 1;
    box_set(&P.box[0], 69000, 59000, -5000, 71000, 61000, 5000);
    CHECK(run(ARC0 "G3 X90 Y60 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    CHECK(only(ARC0 "G2 X90 Y60 I0 J10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    /* semicircle (80,50)->(80,70): G2 passes -X (70,60), G3 passes +X (90,60) */
    CHECK(only(ARC0 "G2 X80 Y70 I0 J10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    CHECK(run(ARC0 "G3 X80 Y70 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* the arc point (70,60) lies exactly on the box face x=70 */
    box_set(&P.box[0], 60000, 59000, -5000, 70000, 61000, 5000);
    CHECK(only(ARC0 "G2 X80 Y70 I0 J10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    /* box 1 um short of the extreme: bounding box doesn't reach it */
    box_set(&P.box[0], 60000, 59000, -5000, 69999, 61000, 5000);
    CHECK(run(ARC0 "G2 X80 Y70 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* box in the bounding-box corner the quarter arc never reaches -> HOLD */
    box_set(&P.box[0], 89000, 50000, -5000, 95000, 51000, 5000);
    CHECK(only(ARC0 "G3 X90 Y60 I0 J10\nM30\n", MFG_ISS_FIXTURE_MAYBE, 6, MFG_HOLD));
    CHECK(R.issue[0].severity == MFG_HOLD);
    /* end point inside a fixture: definite */
    box_set(&P.box[0], 89000, 59000, -5000, 95000, 61000, 5000);
    CHECK(only(ARC0 "G3 X90 Y60 I0 J10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    /* keep-out boxes do not apply to arcs */
    P.box[0].kind = MFG_BOX_KEEPOUT;
    CHECK(run(ARC0 "G3 X90 Y60 I0 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* full circle G2 I10 from (80,50): centre (90,50), +X extreme (100,50) */
    box_set(&P.box[0], 99500, 49000, -5000, 105000, 51000, 5000);
    CHECK(only(ARC0 "G2 I10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    CHECK(R.end_pos[0] == 80000 && R.end_pos[1] == 50000);
    box_set(&P.box[0], 100001, 49000, -5000, 105000, 51000, 5000);
    CHECK(run(ARC0 "G2 I10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* helix: z changes, box must contain the whole z range for a definite hit */
    box_set(&P.box[0], 99500, 49000, -5000, 105000, 51000, 5000);
    CHECK(only(ARC0 "G2 I10 Z-3\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    box_set(&P.box[0], 99500, 49000, -2000, 105000, 51000, 5000);
    CHECK(only(ARC0 "G2 I10 Z-3\nM30\n", MFG_ISS_FIXTURE_MAYBE, 6, MFG_HOLD));
    /* irrational radius: I10 J10 -> r = 14142.13.. um, floor 14142, ceil 14143.
     * centre (90,60); +X extreme in [104142, 104143]. */
    box_set(&P.box[0], 104142, 59000, -5000, 105000, 61000, 5000);
    CHECK(only(ARC0 "G2 I10 J10\nM30\n", MFG_ISS_FIXTURE, 6, MFG_REJECT));
    box_set(&P.box[0], 104143, 59000, -5000, 105000, 61000, 5000);
    CHECK(only(ARC0 "G2 I10 J10\nM30\n", MFG_ISS_FIXTURE_MAYBE, 6, MFG_HOLD));
    box_set(&P.box[0], 104144, 59000, -5000, 105000, 61000, 5000);
    CHECK(run(ARC0 "G2 I10 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    /* envelope: circle centre (490,50) r 10 touches x=500 exactly: fine */
    P.nbox = 0;
    CHECK(run(HDR CUT "G0 Z-1\nG0 X480 Y50\nF100\nG2 I10\nM30\n") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z-1\nG0 X480.001 Y50\nF100\nG2 I10\nM30\n", MFG_ISS_ENVELOPE, 6,
               MFG_REJECT));
    /* irrational: centre x 485.858 + 14142 = 500000 exactly (floor) but
     * ceil reaches 500001 -> possible only -> HOLD */
    CHECK(only(HDR CUT "G0 Z-1\nG0 X475.858 Y50\nF100\nG2 I10 J10\nM30\n", MFG_ISS_ENVELOPE_MAYBE,
               6, MFG_HOLD));
    CHECK(run(HDR CUT "G0 Z-1\nG0 X475.857 Y50\nF100\nG2 I10 J10\nM30\n") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z-1\nG0 X475.859 Y50\nF100\nG2 I10 J10\nM30\n", MFG_ISS_ENVELOPE, 6,
               MFG_REJECT));
    /* radius mismatch of 2 um in r: r0 = 5001, r1 = 4999 */
    CHECK(only(HDR CUT "G0 Z-1\nG0 X50 Y5\nF100\nG3 X50 Y15 J5.001\nM30\n", MFG_ISS_ARC_RADIUS, 6,
               MFG_REJECT));
    /* -Y extreme: circle centre (50,5) r 5 touches y=0: fine; 1 um lower: reject */
    CHECK(run(HDR CUT "G0 Z-1\nG0 X50 Y10\nF100\nG2 J-5\nM30\n") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z-1\nG0 X50 Y9.999\nF100\nG2 J-5\nM30\n", MFG_ISS_ENVELOPE, 6,
               MFG_REJECT));
    /* -X extreme: centre (5,50) r 5 */
    CHECK(run(HDR CUT "G0 Z-1\nG0 X10 Y50\nF100\nG3 I-5\nM30\n") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK);
    CHECK(only(HDR CUT "G0 Z-1\nG0 X9.999 Y50\nF100\nG3 I-5\nM30\n", MFG_ISS_ENVELOPE, 6,
               MFG_REJECT));
    /* incremental end point: G91 X10 Y10 from (80,50) -> (90,60) */
    CHECK(run(HDR CUT "G0 Z-1\nG0 X80 Y50\nF100\nG91 G3 X10 Y10 J10\nM30\n") == MFG_OK &&
          R.verdict == MFG_RELEASE_OK && R.end_pos[0] == 90000 && R.end_pos[1] == 60000);
    /* end direction exactly on +X with a smaller end radius (inside the
     * tolerance): the +X extreme is still bounded by the larger radius */
    P.nbox = 1;
    P.arc_r2_tol = 20000;
    box_set(&P.box[0], 90000, 59000, -5000, 95000, 61000, 5000);
    CHECK(only(ARC0 "G3 X89.999 Y60 J10\nM30\n", MFG_ISS_FIXTURE_MAYBE, 6, MFG_HOLD));
    box_set(&P.box[0], 90001, 59000, -5000, 95000, 61000, 5000);
    CHECK(run(ARC0 "G3 X89.999 Y60 J10\nM30\n") == MFG_OK && R.verdict == MFG_RELEASE_OK);
    P.arc_r2_tol = 0;
    P.nbox = 0;
    /* end at centre: radius mismatch */
    CHECK(only(ARC0 "G3 X80 Y60 J10\nM30\n", MFG_ISS_ARC_RADIUS, 6, MFG_REJECT));
    /* each of the four quadrant extremes is found */
    {
        static const char *ends[4] = {"G3 X90 Y60 J10\n", "G3 X80 Y70 J10\n", "G3 X70 Y60 J10\n",
                                      "G3 X80 Y50 J10\n"};
        /* quarter: +X? no. CCW from -Y: reaches +X at end of quarter. */
        int k;
        P.nbox = 1;
        for (k = 0; k < 4; k++) {
            char prog[256];
            /* box around +Y extreme (80,70): hit only from the half circle on */
            box_set(&P.box[0], 79000, 69500, -5000, 81000, 75000, 5000);
            snprintf(prog, sizeof prog, ARC0 "%sM30\n", ends[k]);
            CHECK(run(prog) == MFG_OK);
            CHECK((R.verdict == MFG_REJECT) == (k >= 1));
        }
    }
    P.safe_z = 5000;
}

static void test_limits_and_errors(void)
{
    static char buf[MFG_GC_MAX_LINES + 10];
    mfg_gc_profile_t q;
    int i, ok = 1;
    prof_default();
    /* too many lines */
    memset(buf, '\n', sizeof buf);
    CHECK(mfg_gc_check(buf, MFG_GC_MAX_LINES, &P, &R) == MFG_OK && R.lines == MFG_GC_MAX_LINES &&
          R.verdict == MFG_HOLD && has(MFG_ISS_NO_END, 0));
    CHECK(mfg_gc_check(buf, MFG_GC_MAX_LINES + 1, &P, &R) == MFG_OK &&
          has(MFG_ISS_TOO_MANY_LINES, MFG_GC_MAX_LINES + 1) && R.verdict == MFG_REJECT);
    CHECK(mfg_gc_check(buf, (size_t) MFG_GC_MAX_LINES * (MFG_GC_MAX_LINE + 2u) + 1, &P, &R) ==
          MFG_ERR_RANGE);
    /* issue overflow: stored up to capacity, rest counted, verdict kept */
    {
        static char prog[200 * 4];
        size_t n = 0;
        for (i = 0; i < 100; i++) CHECK(append(prog, sizeof prog, &n, "T1\n"));
        CHECK(run(prog) == MFG_OK && R.nissue == MFG_GC_MAX_ISSUES &&
              R.dropped == 100 - MFG_GC_MAX_ISSUES + 1 && R.verdict == MFG_REJECT);
    }
    {
        static char prog[64 * 24 + 32];
        size_t n = 0;
        for (i = 0; i < 64; i++) CHECK(append(prog, sizeof prog, &n, HDR "G0 Z200\n"));
        /* 64 envelope REJECTs fill the list; NO_END HOLD is dropped */
        CHECK(run(prog) == MFG_OK && R.nissue == 64 && R.dropped == 1 && R.verdict == MFG_REJECT);
    }
    /* argument errors */
    CHECK(mfg_gc_check("M30", 3, NULL, &R) == MFG_ERR_NULL);
    CHECK(mfg_gc_check("M30", 3, &P, NULL) == MFG_ERR_NULL);
    CHECK(mfg_gc_check(NULL, 3, &P, &R) == MFG_ERR_NULL);
    CHECK(mfg_gc_profile_check(NULL) == MFG_ERR_NULL);
    CHECK(mfg_gc_profile_check(&P) == MFG_OK);
#define BAD(stmt, err)                                                                             \
    do {                                                                                           \
        q = P;                                                                                     \
        stmt;                                                                                      \
        CHECK(mfg_gc_profile_check(&q) == (err));                                                  \
        CHECK(mfg_gc_check("M30", 3, &q, &R) == (err));                                            \
    } while (0)
    BAD(q.env_lo[0] = 600000, MFG_ERR_RANGE);
    BAD(q.env_hi[1] = MFG_GC_COORD_MAX + 1, MFG_ERR_RANGE);
    BAD(q.env_lo[2] = -MFG_GC_COORD_MAX - 1, MFG_ERR_RANGE);
    BAD(q.start[0] = -1, MFG_ERR_RANGE);
    BAD(q.start[2] = 100001, MFG_ERR_RANGE);
    BAD(q.safe_z = MFG_GC_COORD_MAX + 1, MFG_ERR_RANGE);
    BAD(q.feed_min_cut = 0, MFG_ERR_RANGE);
    BAD(q.feed_max = 9999, MFG_ERR_RANGE);
    BAD(q.feed_max = MFG_GC_FEED_MAX + 1, MFG_ERR_RANGE);
    BAD(q.spindle_min = 0, MFG_ERR_RANGE);
    BAD(q.spindle_max = 99, MFG_ERR_RANGE);
    BAD(q.spindle_max = MFG_GC_RPM_MAX + 1, MFG_ERR_RANGE);
    BAD(q.arc_r2_tol = -1, MFG_ERR_RANGE);
    BAD(q.nbox = MFG_GC_MAX_BOXES + 1, MFG_ERR_CAPACITY);
    BAD(q.box[1].kind = 2, MFG_ERR_RANGE);
    BAD(q.box[0].lo[2] = q.box[0].hi[2] + 1, MFG_ERR_RANGE);
    BAD(q.box[0].hi[0] = MFG_GC_COORD_MAX + 1, MFG_ERR_RANGE);
    BAD((void) 0, MFG_OK);
    BAD(q.feed_max = q.feed_min_cut, MFG_OK);
    BAD(q.spindle_max = q.spindle_min, MFG_OK);
    BAD(q.nbox = MFG_GC_MAX_BOXES, MFG_OK);
    /* fuzz: random byte soup and random token programs never crash, and a
     * non-OK verdict always comes with at least one issue */
    for (i = 0; i < 3000; i++) {
        static const char *tok[] = {"G0 ", "G1 ",  "G2 ", "G3 ", "G20 ", "G21 ", "G90 ", "G91 ",
                                    "X",   "Y",    "Z",   "I",   "J",    "F",    "S",    "M3 ",
                                    "M5 ", "M30",  "(c)", ";x",  "\n",   "1",    "-2.5", "0.001",
                                    ".",   "N10 ", " ",   "%",   "\r\n", "Q",    "(",    ")"};
        char prog[400];
        int n = 0, k, nt = (int) (rnd() % 60);
        for (k = 0; k < nt; k++) {
            const char *t = (rnd() % 8 == 0) ? NULL : tok[rnd() % (sizeof tok / sizeof tok[0])];
            if (t) {
                size_t tl = strlen(t);
                if ((size_t) n + tl >= sizeof prog) break;
                memcpy(prog + n, t, tl);
                n += (int) tl;
            } else if ((size_t) n + 1 < sizeof prog) {
                prog[n++] = (char) (rnd() & 0xff);
            }
        }
        if (mfg_gc_check(prog, (size_t) n, &P, &R) != MFG_OK) ok = 0;
        if (R.verdict != MFG_RELEASE_OK && R.nissue + R.dropped == 0) ok = 0;
        if (R.verdict == MFG_RELEASE_OK && R.nissue != 0) ok = 0;
        if (R.nissue > MFG_GC_MAX_ISSUES) ok = 0;
    }
    CHECK(ok);
}

/* ---- QC --------------------------------------------------------------- */

static mfg_qc_gauge_t G(int64_t v, int64_t u)
{
    mfg_qc_gauge_t g;
    g.value = v;
    g.uncertainty = u;
    g.valid = 1;
    return g;
}

static mfg_tv_t gd(int64_t v, int64_t u)
{
    mfg_qc_gauge_t g = G(v, u);
    mfg_tv_t t = (mfg_tv_t) 99;
    if (mfg_qc_gauge(&g, 9990, 10010, &t) != MFG_OK) return (mfg_tv_t) 98;
    return t;
}

static void feat(mfg_qc_feature_t *f, int64_t nom, int64_t tm, int64_t tp)
{
    f->nominal = nom;
    f->tol_minus = tm;
    f->tol_plus = tp;
    f->ngauge = 0;
}

static void test_qc(void)
{
    mfg_qc_feature_t f[4];
    mfg_qc_feature_result_t fr, res[4];
    mfg_qc_gauge_t g;
    mfg_tv_t t, ps;
    mfg_verdict_t vd;
    int a, b, i, ok = 1;
    /* gauge decisions around band [9990, 10010] */
    CHECK(gd(10000, 5) == MFG_TRUE);
    CHECK(gd(9995, 5) == MFG_TRUE);  /* [9990,10000] touches lo from inside */
    CHECK(gd(10005, 5) == MFG_TRUE); /* [10000,10010] */
    CHECK(gd(9994, 5) == MFG_BOTH);
    CHECK(gd(10006, 5) == MFG_BOTH);
    CHECK(gd(9985, 5) == MFG_BOTH); /* [9980,9990] touches lo from outside */
    CHECK(gd(10015, 5) == MFG_BOTH);
    CHECK(gd(9984, 5) == MFG_FALSE); /* [9979,9989] */
    CHECK(gd(10016, 5) == MFG_FALSE);
    CHECK(gd(9990, 0) == MFG_TRUE);
    CHECK(gd(10010, 0) == MFG_TRUE);
    CHECK(gd(9989, 0) == MFG_FALSE);
    CHECK(gd(10011, 0) == MFG_FALSE);
    CHECK(gd(10000, 10) == MFG_TRUE); /* exactly fills the band */
    CHECK(gd(10000, 11) == MFG_BOTH); /* wider than the band */
    CHECK(gd(10000, 1000) == MFG_BOTH);
    g = G(0, 0);
    g.valid = 0;
    CHECK(mfg_qc_gauge(&g, 9990, 10010, &t) == MFG_OK && t == MFG_NEITHER);
    g.value = -MFG_QC_VALUE_MAX - 1; /* invalid gauge isn't range-checked */
    CHECK(mfg_qc_gauge(&g, 9990, 10010, &t) == MFG_OK && t == MFG_NEITHER);
    g = G(10000, -1);
    CHECK(mfg_qc_gauge(&g, 9990, 10010, &t) == MFG_ERR_RANGE);
    g = G(MFG_QC_VALUE_MAX + 1, 0);
    CHECK(mfg_qc_gauge(&g, 9990, 10010, &t) == MFG_ERR_RANGE);
    g = G(0, MFG_QC_VALUE_MAX + 1);
    CHECK(mfg_qc_gauge(&g, 9990, 10010, &t) == MFG_ERR_RANGE);
    g = G(MFG_QC_VALUE_MAX, MFG_QC_VALUE_MAX);
    CHECK(mfg_qc_gauge(&g, -MFG_QC_VALUE_MAX, MFG_QC_VALUE_MAX, &t) == MFG_OK && t == MFG_BOTH);
    g = G(0, 0);
    CHECK(mfg_qc_gauge(&g, 10, 9, &t) == MFG_ERR_RANGE);
    CHECK(mfg_qc_gauge(&g, MFG_QC_VALUE_MAX + 1, MFG_QC_VALUE_MAX + 2, &t) == MFG_ERR_RANGE);
    CHECK(mfg_qc_gauge(&g, -MFG_QC_VALUE_MAX - 1, 0, &t) == MFG_ERR_RANGE);
    CHECK(mfg_qc_gauge(NULL, 0, 1, &t) == MFG_ERR_NULL);
    CHECK(mfg_qc_gauge(&g, 0, 1, NULL) == MFG_ERR_NULL);
    /* property: guard band vs brute force over the integer interval */
    for (i = 0; i < 20000; i++) {
        int64_t lo = rnd_range(-50, 50), hi = lo + rnd_range(0, 40);
        int64_t v = rnd_range(-80, 80), u = rnd_range(0, 30), x, in = 0, out = 0;
        mfg_tv_t e;
        g = G(v, u);
        for (x = v - u; x <= v + u; x++) {
            if (x >= lo && x <= hi)
                in++;
            else
                out++;
        }
        e = out == 0 ? MFG_TRUE : in == 0 ? MFG_FALSE : MFG_BOTH;
        if (mfg_qc_gauge(&g, lo, hi, &t) != MFG_OK || t != e) ok = 0;
    }
    CHECK(ok);
    /* feature truth table: two gauges from {IN, OUT, AMBIGUOUS, INVALID} */
    {
        mfg_qc_gauge_t kinds[4];
        /* expected[a][b] */
        static const mfg_tv_t expct[4][4] = {
            {MFG_TRUE, MFG_BOTH, MFG_BOTH, MFG_TRUE},
            {MFG_BOTH, MFG_FALSE, MFG_BOTH, MFG_FALSE},
            {MFG_BOTH, MFG_BOTH, MFG_BOTH, MFG_BOTH},
            {MFG_TRUE, MFG_FALSE, MFG_BOTH, MFG_NEITHER},
        };
        kinds[0] = G(10000, 5);
        kinds[1] = G(10020, 5);
        kinds[2] = G(10008, 5);
        kinds[3] = G(10000, 5);
        kinds[3].valid = 0;
        for (a = 0; a < 4; a++) {
            for (b = 0; b < 4; b++) {
                feat(&f[0], 10000, 10, 10);
                f[0].ngauge = 2;
                f[0].gauge[0] = kinds[a];
                f[0].gauge[1] = kinds[b];
                CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == expct[a][b]);
                CHECK(fr.n_in + fr.n_out + fr.n_ambiguous + fr.n_invalid == 2);
            }
            /* single gauge */
            feat(&f[0], 10000, 10, 10);
            f[0].ngauge = 1;
            f[0].gauge[0] = kinds[a];
            CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == expct[a][3]);
        }
    }
    feat(&f[0], 10000, 10, 10);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == MFG_NEITHER); /* no gauges */
    f[0].ngauge = 3;
    f[0].gauge[0] = G(10000, 1);
    f[0].gauge[1] = G(10001, 2);
    f[0].gauge[2] = G(9999, 3);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == MFG_TRUE && fr.n_in == 3);
    f[0].gauge[2] = G(9999, 10); /* one ambiguous among two IN */
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == MFG_BOTH && fr.n_ambiguous == 1);
    /* asymmetric band [9995, 10020] */
    feat(&f[0], 10000, 5, 20);
    f[0].ngauge = 1;
    f[0].gauge[0] = G(10015, 5);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == MFG_TRUE);
    f[0].gauge[0] = G(9997, 3);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_OK && fr.state == MFG_BOTH);
    /* errors */
    CHECK(mfg_qc_feature(NULL, &fr) == MFG_ERR_NULL);
    CHECK(mfg_qc_feature(&f[0], NULL) == MFG_ERR_NULL);
    f[0].ngauge = MFG_QC_MAX_GAUGES + 1;
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_CAPACITY);
    feat(&f[0], 10000, -1, 10);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], 10000, 10, -1);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], MFG_QC_VALUE_MAX + 1, 0, 0);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], -MFG_QC_VALUE_MAX, 1, 0);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], MFG_QC_VALUE_MAX, 0, 1);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], 0, MFG_QC_VALUE_MAX + 1, 0);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], 0, 0, MFG_QC_VALUE_MAX + 1);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    feat(&f[0], 0, 5, 5);
    f[0].ngauge = 1;
    f[0].gauge[0] = G(0, -2);
    CHECK(mfg_qc_feature(&f[0], &fr) == MFG_ERR_RANGE);
    /* parts */
    for (i = 0; i < 4; i++) {
        feat(&f[i], 10000, 10, 10);
        f[i].ngauge = 1;
        f[i].gauge[0] = G(10000, 2);
    }
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_OK && ps == MFG_TRUE && vd == MFG_RELEASE_OK);
    CHECK(res[3].state == MFG_TRUE);
    CHECK(mfg_qc_part(f, 4, NULL, &ps, &vd) == MFG_OK && ps == MFG_TRUE);
    f[2].gauge[0] = G(10009, 2); /* ambiguous */
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_OK && ps == MFG_BOTH && vd == MFG_HOLD);
    CHECK(res[2].state == MFG_BOTH);
    f[2].gauge[0].valid = 0; /* no evidence */
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_OK && ps == MFG_NEITHER && vd == MFG_HOLD);
    f[1].gauge[0] = G(10009, 2); /* NEITHER + BOTH -> BOTH */
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_OK && ps == MFG_BOTH && vd == MFG_HOLD);
    f[3].gauge[0] = G(10100, 2); /* plus a definite fail -> reject */
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_OK && ps == MFG_FALSE && vd == MFG_REJECT);
    CHECK(mfg_qc_part(f, 3, res, &ps, &vd) == MFG_OK && ps == MFG_BOTH); /* first 3 only */
    CHECK(mfg_qc_part(f, 0, res, &ps, &vd) == MFG_ERR_RANGE);
    CHECK(mfg_qc_part(f, MFG_QC_MAX_FEATURES + 1, res, &ps, &vd) == MFG_ERR_RANGE);
    CHECK(mfg_qc_part(NULL, 1, res, &ps, &vd) == MFG_ERR_NULL);
    CHECK(mfg_qc_part(f, 1, res, NULL, &vd) == MFG_ERR_NULL);
    CHECK(mfg_qc_part(f, 1, res, &ps, NULL) == MFG_ERR_NULL);
    f[0].tol_minus = -3;
    CHECK(mfg_qc_part(f, 4, res, &ps, &vd) == MFG_ERR_RANGE);
}

/* ---- budgets and queue ------------------------------------------------ */

static const int64_t LIM[MFG_RES_N] = {1000, 500, 200, 50, 100};

static void needv(int64_t *n, int64_t e, int64_t w, int64_t m, int64_t ws, int64_t em)
{
    n[0] = e;
    n[1] = w;
    n[2] = m;
    n[3] = ws;
    n[4] = em;
}

static void test_budget(void)
{
    mfg_site_t s;
    int64_t n[MFG_RES_N], bad[MFG_RES_N];
    mfg_tv_t pr[MFG_RES_N];
    mfg_admit_t d;
    int r;
    CHECK(mfg_site_init(&s, 1, LIM) == MFG_OK);
    needv(n, 10, 0, 0, 0, 0);
    /* no meter -> NEITHER -> wait; unneeded resources are TRUE */
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_NEITHER &&
          pr[1] == MFG_TRUE && pr[4] == MFG_TRUE);
    needv(n, 0, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT);
    CHECK(mfg_site_meter(&s, MFG_RES_ENERGY_WH, 900) == MFG_OK);
    needv(n, 100, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT && pr[0] == MFG_TRUE);
    needv(n, 101, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_FALSE);
    CHECK(mfg_site_check(&s, n, NULL, &d) == MFG_OK && d == MFG_WAIT);
    /* reservation */
    needv(n, 60, 0, 0, 0, 0);
    CHECK(mfg_site_reserve(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT && s.reserved[0] == 60);
    needv(n, 41, 0, 0, 0, 0);
    CHECK(mfg_site_reserve(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && s.reserved[0] == 60);
    needv(n, 40, 0, 0, 0, 0);
    CHECK(mfg_site_reserve(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT && s.reserved[0] == 100);
    needv(n, 1, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT);
    needv(n, 60, 0, 0, 0, 0);
    CHECK(mfg_site_release(&s, n) == MFG_OK && s.reserved[0] == 40);
    needv(n, 41, 0, 0, 0, 0);
    CHECK(mfg_site_release(&s, n) == MFG_ERR_RANGE && s.reserved[0] == 40);
    needv(n, 60, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT);
    /* meter going backwards is contradictory: BOTH, stays until cleared */
    CHECK(mfg_site_meter(&s, 0, 950) == MFG_OK && s.meter[0] == MFG_TRUE);
    CHECK(mfg_site_meter(&s, 0, 950) == MFG_OK && s.meter[0] == MFG_TRUE); /* equal ok */
    CHECK(mfg_site_meter(&s, 0, 949) == MFG_OK && s.meter[0] == MFG_BOTH && s.used[0] == 950);
    needv(n, 1, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_BOTH);
    CHECK(mfg_site_meter(&s, 0, 960) == MFG_OK && s.meter[0] == MFG_BOTH);
    CHECK(mfg_site_meter_clear(&s, 0) == MFG_OK && s.meter[0] == MFG_NEITHER && s.used[0] == 0);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_NEITHER);
    CHECK(mfg_site_meter(&s, 0, 960) == MFG_OK && s.meter[0] == MFG_TRUE);
    /* 960 measured + 40 reserved + 1 = 1001 > 1000 */
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_FALSE);
    /* all resources together: each must pass */
    for (r = 1; r < MFG_RES_N; r++) CHECK(mfg_site_meter(&s, (uint32_t) r, 0) == MFG_OK);
    needv(n, 0, 500, 200, 50, 100);
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_ADMIT);
    for (r = 1; r < MFG_RES_N; r++) {
        needv(n, 0, 500, 200, 50, 100);
        n[r]++;
        CHECK(mfg_site_check(&s, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[r] == MFG_FALSE);
    }
    /* zero limit: nothing that needs it fits */
    {
        mfg_site_t z;
        int64_t zl[MFG_RES_N] = {0, 0, 0, 0, 0};
        CHECK(mfg_site_init(&z, 5, zl) == MFG_OK);
        CHECK(mfg_site_meter(&z, 3, 0) == MFG_OK);
        needv(n, 0, 0, 0, 1, 0);
        CHECK(mfg_site_check(&z, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[3] == MFG_FALSE);
        needv(n, 0, 0, 0, 0, 0);
        CHECK(mfg_site_check(&z, n, pr, &d) == MFG_OK && d == MFG_ADMIT);
        /* maximum values do not overflow */
        for (r = 0; r < MFG_RES_N; r++) zl[r] = MFG_BUDGET_MAX;
        CHECK(mfg_site_init(&z, 5, zl) == MFG_OK);
        CHECK(mfg_site_meter(&z, 0, MFG_BUDGET_MAX) == MFG_OK);
        needv(n, MFG_BUDGET_MAX, 0, 0, 0, 0);
        CHECK(mfg_site_check(&z, n, pr, &d) == MFG_OK && d == MFG_WAIT && pr[0] == MFG_FALSE);
    }
    /* new epoch */
    CHECK(mfg_site_new_epoch(&s, 1) == MFG_ERR_RANGE);
    CHECK(mfg_site_new_epoch(&s, 0) == MFG_ERR_RANGE);
    CHECK(mfg_site_new_epoch(&s, 2) == MFG_OK && s.epoch == 2 && s.meter[0] == MFG_NEITHER &&
          s.reserved[0] == 0 && s.used[0] == 0 && s.limit[0] == 1000);
    /* errors */
    needv(bad, 0, -1, 0, 0, 0);
    CHECK(mfg_site_init(NULL, 1, LIM) == MFG_ERR_NULL);
    CHECK(mfg_site_init(&s, 1, NULL) == MFG_ERR_NULL);
    CHECK(mfg_site_init(&s, 1, bad) == MFG_ERR_RANGE);
    needv(bad, 0, 0, 0, 0, MFG_BUDGET_MAX + 1);
    CHECK(mfg_site_init(&s, 1, bad) == MFG_ERR_RANGE);
    CHECK(mfg_site_check(&s, bad, pr, &d) == MFG_ERR_RANGE);
    CHECK(mfg_site_release(&s, bad) == MFG_ERR_RANGE);
    needv(bad, -1, 0, 0, 0, 0);
    CHECK(mfg_site_check(&s, bad, pr, &d) == MFG_ERR_RANGE);
    CHECK(mfg_site_reserve(&s, bad, pr, &d) == MFG_ERR_RANGE);
    CHECK(mfg_site_release(&s, bad) == MFG_ERR_RANGE);
    CHECK(mfg_site_check(NULL, n, pr, &d) == MFG_ERR_NULL);
    CHECK(mfg_site_check(&s, NULL, pr, &d) == MFG_ERR_NULL);
    CHECK(mfg_site_check(&s, n, pr, NULL) == MFG_ERR_NULL);
    CHECK(mfg_site_reserve(NULL, n, pr, &d) == MFG_ERR_NULL);
    CHECK(mfg_site_release(NULL, n) == MFG_ERR_NULL);
    CHECK(mfg_site_release(&s, NULL) == MFG_ERR_NULL);
    CHECK(mfg_site_meter(NULL, 0, 1) == MFG_ERR_NULL);
    CHECK(mfg_site_meter(&s, MFG_RES_N, 1) == MFG_ERR_RANGE);
    CHECK(mfg_site_meter(&s, 0, -1) == MFG_ERR_RANGE);
    CHECK(mfg_site_meter(&s, 0, MFG_BUDGET_MAX + 1) == MFG_ERR_RANGE);
    CHECK(mfg_site_meter_clear(NULL, 0) == MFG_ERR_NULL);
    CHECK(mfg_site_meter_clear(&s, MFG_RES_N) == MFG_ERR_RANGE);
    CHECK(mfg_site_new_epoch(NULL, 9) == MFG_ERR_NULL);
    s.init = 0;
    CHECK(mfg_site_check(&s, n, pr, &d) == MFG_ERR_STATE);
    CHECK(mfg_site_meter(&s, 0, 1) == MFG_ERR_STATE);
    CHECK(mfg_site_meter_clear(&s, 0) == MFG_ERR_STATE);
    CHECK(mfg_site_release(&s, n) == MFG_ERR_STATE);
    CHECK(mfg_site_new_epoch(&s, 9) == MFG_ERR_STATE);
}

static mfg_queue_t Q;

static void site_metered(mfg_site_t *s)
{
    int r;
    mfg_site_init(s, 1, LIM);
    for (r = 0; r < MFG_RES_N; r++) mfg_site_meter(s, (uint32_t) r, 0);
}

static mfg_job_t *job(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < Q.njob; i++)
        if (Q.job[i].id == id) return &Q.job[i];
    return NULL;
}

static void test_queue(void)
{
    mfg_site_t s;
    int64_t n[MFG_RES_N], z[MFG_RES_N] = {0, 0, 0, 0, 0};
    uint32_t ord[16], na = 0, i;
    needv(n, 10, 0, 0, 0, 0);
    /* monopoly cap: A has the three highest bids */
    site_metered(&s);
    CHECK(mfg_queue_init(&Q) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 1, 100, 100, 20, n) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 2, 100, 99, 20, n) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 3, 100, 98, 20, n) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 4, 200, 10, 20, n) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 5, 300, 5, 20, n) == MFG_OK);
    CHECK(mfg_queue_run(&Q, &s, 90, MFG_SHARE_NUM, MFG_SHARE_DEN, ord, 16, &na) == MFG_OK);
    CHECK(na == 4 && ord[0] == 1 && ord[1] == 4 && ord[2] == 5 && ord[3] == 2);
    CHECK(job(3)->state == MFG_JOB_PENDING && job(3)->wait == MFG_WAIT_CAPACITY);
    CHECK(s.reserved[0] == 40);
    /* a second run admits nothing more (capacity is per run) unless room */
    CHECK(mfg_queue_run(&Q, &s, 20, 1, 3, ord, 16, &na) == MFG_OK && na == 1 && ord[0] == 3);
    CHECK(mfg_queue_run(&Q, &s, 20, 1, 3, ord, 16, &na) == MFG_OK && na == 0);
    /* share 1/1: pure bid order */
    site_metered(&s);
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 1, 100, 100, 20, n);
    mfg_queue_add(&Q, 2, 100, 99, 20, n);
    mfg_queue_add(&Q, 3, 100, 98, 20, n);
    mfg_queue_add(&Q, 4, 200, 10, 20, n);
    mfg_queue_add(&Q, 5, 300, 5, 20, n);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 1, ord, 16, &na) == MFG_OK);
    CHECK(na == 4 && ord[0] == 1 && ord[1] == 2 && ord[2] == 3 && ord[3] == 4);
    CHECK(job(5)->wait == MFG_WAIT_CAPACITY);
    /* exactly at the share is allowed: cap 30 of 90, A job of 30 then B */
    site_metered(&s);
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 1, 100, 100, 30, n);
    mfg_queue_add(&Q, 2, 100, 99, 1, n);
    mfg_queue_add(&Q, 3, 200, 1, 10, n);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 16, &na) == MFG_OK && na == 3);
    CHECK(ord[0] == 1 && ord[1] == 3 && ord[2] == 2);
    /* alone, an owner may use the whole machine (nobody else waits) */
    site_metered(&s);
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 1, 100, 1, 40, n);
    mfg_queue_add(&Q, 2, 100, 2, 40, n);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 16, &na) == MFG_OK && na == 2 && ord[0] == 2);
    /* ties: equal bids by submission order */
    site_metered(&s);
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 9, 1, 50, 10, n);
    mfg_queue_add(&Q, 3, 2, 50, 10, n);
    mfg_queue_add(&Q, 7, 3, 50, 10, n);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 1, ord, 16, &na) == MFG_OK && na == 3 && ord[0] == 9 &&
          ord[1] == 3 && ord[2] == 7);
    /* a high bid cannot buy past a boundary */
    site_metered(&s);
    mfg_site_meter_clear(&s, MFG_RES_WATER_ML); /* water meter offline */
    mfg_queue_init(&Q);
    needv(n, 10, 5, 0, 0, 0);
    mfg_queue_add(&Q, 1, 1, MFG_Q_BID_MAX, 10, n); /* needs water */
    needv(n, 10, 0, 0, 0, 0);
    mfg_queue_add(&Q, 2, 2, 1, 10, n);
    needv(n, 1001, 0, 0, 0, 0);
    mfg_queue_add(&Q, 3, 3, MFG_Q_BID_MAX, 10, n); /* would exceed the energy limit */
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 16, &na) == MFG_OK && na == 1 && ord[0] == 2);
    CHECK(job(1)->wait == MFG_WAIT_BUDGET && job(1)->res_state[1] == MFG_NEITHER &&
          job(1)->res_state[0] == MFG_TRUE);
    CHECK(job(3)->wait == MFG_WAIT_BUDGET && job(3)->res_state[0] == MFG_FALSE);
    CHECK(s.reserved[0] == 10);
    /* exactly at the limit is admitted */
    needv(n, 990, 0, 0, 0, 0);
    CHECK(mfg_queue_remove(&Q, 3) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 3, 3, 1, 10, n) == MFG_OK);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 16, &na) == MFG_OK && na == 1 && ord[0] == 3 &&
          s.reserved[0] == 1000);
    /* budget consumed by an earlier admission in the same run */
    site_metered(&s);
    mfg_queue_init(&Q);
    needv(n, 600, 0, 0, 0, 0);
    mfg_queue_add(&Q, 1, 1, 10, 10, n);
    mfg_queue_add(&Q, 2, 2, 9, 10, n);
    mfg_queue_add(&Q, 3, 3, 1, 10, z);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 16, &na) == MFG_OK && na == 2 && ord[0] == 1 &&
          ord[1] == 3 && job(2)->wait == MFG_WAIT_BUDGET);
    /* order truncation */
    site_metered(&s);
    mfg_queue_init(&Q);
    for (i = 0; i < 3; i++) mfg_queue_add(&Q, i, i, 1, 1, z);
    ord[1] = 777;
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, ord, 1, &na) == MFG_OK && na == 3 && ord[1] == 777);
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 1, 1, 1, 1, z);
    CHECK(mfg_queue_run(&Q, &s, 90, 1, 3, NULL, 0, &na) == MFG_OK && na == 1);
    /* zero capacity admits nothing */
    mfg_queue_init(&Q);
    mfg_queue_add(&Q, 1, 1, 1, 1, z);
    CHECK(mfg_queue_run(&Q, &s, 0, 1, 3, ord, 16, &na) == MFG_OK && na == 0 &&
          job(1)->wait == MFG_WAIT_CAPACITY);
    /* add/remove errors and capacity */
    mfg_queue_init(&Q);
    CHECK(mfg_queue_add(&Q, 1, 1, -1, 1, z) == MFG_ERR_RANGE);
    CHECK(mfg_queue_add(&Q, 1, 1, MFG_Q_BID_MAX + 1, 1, z) == MFG_ERR_RANGE);
    CHECK(mfg_queue_add(&Q, 1, 1, 0, 0, z) == MFG_ERR_RANGE);
    CHECK(mfg_queue_add(&Q, 1, 1, 0, MFG_Q_TIME_MAX + 1, z) == MFG_ERR_RANGE);
    needv(n, 0, 0, -1, 0, 0);
    CHECK(mfg_queue_add(&Q, 1, 1, 0, 1, n) == MFG_ERR_RANGE);
    CHECK(mfg_queue_add(NULL, 1, 1, 0, 1, z) == MFG_ERR_NULL);
    CHECK(mfg_queue_add(&Q, 1, 1, 0, 1, NULL) == MFG_ERR_NULL);
    CHECK(mfg_queue_add(&Q, 1, 1, 0, 1, z) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 1, 2, 0, 1, z) == MFG_ERR_STATE); /* duplicate id */
    for (i = 2; i <= MFG_Q_MAX_JOBS; i++) CHECK(mfg_queue_add(&Q, i, 1, 0, 1, z) == MFG_OK);
    CHECK(mfg_queue_add(&Q, 999, 1, 0, 1, z) == MFG_ERR_CAPACITY);
    CHECK(mfg_queue_remove(&Q, 999) == MFG_ERR_NOT_FOUND);
    CHECK(mfg_queue_remove(&Q, 1) == MFG_OK && Q.njob == MFG_Q_MAX_JOBS - 1 && job(1) == NULL &&
          Q.job[0].id == 2 && Q.job[MFG_Q_MAX_JOBS - 2].id == MFG_Q_MAX_JOBS);
    CHECK(mfg_queue_remove(&Q, MFG_Q_MAX_JOBS) == MFG_OK && Q.njob == MFG_Q_MAX_JOBS - 2);
    CHECK(mfg_queue_remove(NULL, 1) == MFG_ERR_NULL);
    CHECK(mfg_queue_init(NULL) == MFG_ERR_NULL);
    site_metered(&s);
    CHECK(mfg_queue_run(NULL, &s, 1, 1, 3, ord, 16, &na) == MFG_ERR_NULL);
    CHECK(mfg_queue_run(&Q, NULL, 1, 1, 3, ord, 16, &na) == MFG_ERR_NULL);
    CHECK(mfg_queue_run(&Q, &s, 1, 1, 3, ord, 16, NULL) == MFG_ERR_NULL);
    CHECK(mfg_queue_run(&Q, &s, 1, 1, 3, NULL, 1, &na) == MFG_ERR_NULL);
    CHECK(mfg_queue_run(&Q, &s, -1, 1, 3, ord, 16, &na) == MFG_ERR_RANGE);
    CHECK(mfg_queue_run(&Q, &s, MFG_Q_TIME_MAX + 1, 1, 3, ord, 16, &na) == MFG_ERR_RANGE);
    CHECK(mfg_queue_run(&Q, &s, 1, 0, 3, ord, 16, &na) == MFG_ERR_RANGE);
    CHECK(mfg_queue_run(&Q, &s, 1, 4, 3, ord, 16, &na) == MFG_ERR_RANGE);
    CHECK(mfg_queue_run(&Q, &s, 1, 1, 0, ord, 16, &na) == MFG_ERR_RANGE);
    CHECK(mfg_queue_run(&Q, &s, 1, 1, MFG_Q_SHARE_MAX + 1, ord, 16, &na) == MFG_ERR_RANGE);
    s.init = 0;
    CHECK(mfg_queue_run(&Q, &s, 1, 1, 3, ord, 16, &na) == MFG_ERR_STATE);
    Q.njob = MFG_Q_MAX_JOBS + 1;
    CHECK(mfg_queue_add(&Q, 5000, 1, 0, 1, z) == MFG_ERR_STATE);
    CHECK(mfg_queue_remove(&Q, 2) == MFG_ERR_STATE);
    site_metered(&s);
    CHECK(mfg_queue_run(&Q, &s, 1, 1, 3, ord, 16, &na) == MFG_ERR_STATE);
    /* property: admitted jobs never exceed capacity or any budget; with
     * several owners waiting nobody goes over the share unless all
     * remaining admissible jobs were over their own share */
    {
        int t, ok = 1;
        for (t = 0; t < 300; t++) {
            int64_t cap = rnd_range(0, 200), tot = 0, eu = 0;
            uint32_t k, cnt = (uint32_t) rnd_range(1, 40);
            site_metered(&s);
            mfg_site_meter(&s, 0, rnd_range(0, 1000));
            mfg_queue_init(&Q);
            for (k = 0; k < cnt; k++) {
                needv(n, rnd_range(0, 300), 0, 0, 0, 0);
                mfg_queue_add(&Q, k, (uint32_t) rnd_range(0, 3), rnd_range(0, 100),
                              rnd_range(1, 60), n);
            }
            if (mfg_queue_run(&Q, &s, cap, 1, 3, ord, 16, &na) != MFG_OK) ok = 0;
            for (k = 0; k < Q.njob; k++)
                if (Q.job[k].state == MFG_JOB_ADMITTED) {
                    tot += Q.job[k].minutes;
                    eu += Q.job[k].need[0];
                }
            if (tot > cap || s.used[0] + eu > LIM[0] || eu != s.reserved[0]) ok = 0;
        }
        CHECK(ok);
    }
}

int main(void)
{
    test_parse();
    test_isqrt();
    test_segbox();
    test_clean();
    test_checks();
    test_arcs();
    test_limits_and_errors();
    test_qc();
    test_budget();
    test_queue();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
