/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_exec.c — running Trebago programs: values and precedence, the five
 * kernels against a reference written directly with zxv_rat_* (same
 * operation order, so refusals must agree too), column-major layout,
 * bindings and their refusals, outputs untouched on a refusal, the work
 * limit, empty arrays, and one program on four threads. */
#include <pthread.h>

#include "tb_test.h"

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

/* Run a program with one scalar output `s` and an input vector x. */
static int32_t scalar(const char *text, const int64_t *xn, const int64_t *xd, int64_t n, int64_t *on, int64_t *od,
                      uint64_t limit, tb_diag *d) {
    tb_program *p = prog(text);
    int64_t num[16], den[16];
    tb_array a[2];
    int32_t st;
    if (!p) return -9999;
    for (int64_t i = 0; i < n; i++) {
        num[i] = xn[i];
        den[i] = xd[i];
    }
    a[0] = arr1("x", num, den, n);
    a[1] = arr0("s", on, od);
    st = tb_exec(p, a, 2, limit, d);
    tb_program_destroy(p);
    return st;
}

static void value(const char *stmt, int64_t wn, int64_t wd) {
    char text[512];
    int64_t xn[3] = {2, -3, 1}, xd[3] = {1, 4, 2}, on = 77, od = 77;
    tb_diag d;
    int32_t st;
    snprintf(text, sizeof text, "trebago v\n  in x(n)\n  out s\n  s = %s\nend\n", stmt);
    st = scalar(text, xn, xd, 3, &on, &od, 0, &d);
    CHECK(st == ZXV_OK && on == wn && od == wd, "%s = %s, expected %s (%s)", stmt, rat(on, od), rat(wn, wd),
          st ? d.msg : "");
}

static void refusal(const char *stmt, int32_t want, const char *needle) {
    char text[512];
    int64_t xn[3] = {2, -3, 1}, xd[3] = {1, 4, 2}, on = 77, od = 78;
    tb_diag d;
    int32_t st;
    snprintf(text, sizeof text, "trebago v\n  in x(n)\n  out s\n  s = %s\nend\n", stmt);
    st = scalar(text, xn, xd, 3, &on, &od, 1000000, &d);
    CHECK(st == want, "%s: %s, expected %s: %s", stmt, tb_status_name(st), tb_status_name(want), d.msg);
    CHECK(on == 77 && od == 78, "%s: output written on a refusal", stmt);
    if (needle) CHECK(strstr(d.msg, needle) != NULL, "%s: message '%s' lacks '%s'", stmt, d.msg, needle);
    CHECK(d.line == 4, "%s: refusal on line %u", stmt, d.line);
}

static void test_values(void) {
    /* x = 2, -3/4, 1/2; n = 3 */
    value("x(1) + x(2) * x(3)", 13, 8);
    value("(x(1) + x(2)) * x(3)", 5, 8);
    value("x(1) - x(2) - x(3)", 9, 4);
    value("x(1) - (x(2) - x(3))", 13, 4);
    value("x(1) / x(2) / x(3)", -16, 3);
    value("x(1) / (x(2) / x(3))", -4, 3);
    value("-x(1) ** 2", -4, 1);
    value("(-x(1)) ** 2", 4, 1);
    value("(2 ** 3) ** 2", 64, 1);
    value("2 ** -3", 1, 8);
    value("x(2) ** -2", 16, 9);
    value("x(2) ** 0", 1, 1);
    value("0 ** 0", 1, 1);
    value("(-1) ** 63", -1, 1);
    value("2 ** 62", INT64_C(4611686018427387904), 1);
    value("(1/2) ** 62", 1, INT64_C(4611686018427387904));
    value("x(n) ** (n - 1)", 1, 4);
    value("--x(2)", -3, 4);
    value("abs(x(2))", 3, 4);
    value("min(x(1), x(2), x(3))", -3, 4);
    value("max(x(1), x(2), x(3))", 2, 1);
    value("min(x(3), 0.5)", 1, 2);
    value("n + n * n - 1", 11, 1);
    value("sum(i, x(i))", 7, 4);
    value("sum(i = 1:n, i)", 6, 1);
    value("sum(i = 2:n, x(i))", -1, 4);
    value("sum(i = n:1, x(i))", 0, 1);
    value("sum(i, sum(j, x(i) * x(j)))", 49, 16);
    value("sum(i = 1:n, sum(j = i:n, 1))", 6, 1);
    value("sum(i = -5:5, i ** 3)", 0, 1);
    value("x(n - 1 + 1) * 4", 2, 1);
    value("0.1 + 0.2 - 0.3", 0, 1);
    value("1/3 + 1/3 + 1/3", 1, 1);
    value("9223372036854775807 - 1", INT64_C(9223372036854775806), 1);
    value("1 / 9223372036854775807 * 9223372036854775807", 1, 1);
    refusal("9223372036854775807 + 1", ZXV_E_RANGE, "9223372036854775807 + 1 does not fit");
    refusal("(-9223372036854775807) - 2", ZXV_E_RANGE, "never rounded");
    refusal("3037000500 * 3037000500", ZXV_E_RANGE, "*");
    refusal("1 / 9223372036854775807 / 2", ZXV_E_RANGE, "/");
    refusal("2 ** 63", ZXV_E_RANGE, "(2) ** 63");
    refusal("(1/2) ** 63", ZXV_E_RANGE, "** 63");
    refusal("2 ** -63", ZXV_E_RANGE, "** -63");
    refusal("x(1) / (x(1) - 2)", TB_E_DIVZERO, "division by zero: 2 / 0");
    refusal("0 ** -2", TB_E_DIVZERO, "0 ** -2");
    refusal("x(0)", TB_E_BOUNDS, "x(0) is outside x(1:3)");
    refusal("x(n + 1)", TB_E_BOUNDS, "x(4) is outside x(1:3)");
    refusal("sum(i, x(i) * x(4 - i + 1))", TB_E_BOUNDS, "at i=1");
    refusal("x(n * 4611686018427387904)", ZXV_E_RANGE, "index expression overflows");
    refusal("sum(i = 1:9223372036854775807, 0)", TB_E_LIMIT, "work limit (1000000");
}

/* ===== the five kernels against direct libzxv arithmetic ===== */

static const char k_diff[] = "trebago diff\n"
                             "  in  a(m, n), x(n), y(n), alpha\n"
                             "  out s, d, z(n), p(n), v(m)\n"
                             "  s = sum(i, x(i))\n"
                             "  d = sum(i, x(i) * y(i))\n"
                             "  z(i) = alpha * x(i)\n"
                             "  p(i) = scan(x(i))\n"
                             "  v(i) = sum(j, a(i, j) * x(j))\n"
                             "end\n";

static void rnd_rat(int64_t *n, int64_t *d, int big) {
    if (big && (rnd() & 7u) == 0) {
        *n = (int64_t) (rnd() >> 2) * ((rnd() & 1u) ? 1 : -1);
        *d = (int64_t) (rnd() >> 3) + 1;
    } else {
        *n = (int64_t) (rnd() % 101u) - 50;
        *d = (int64_t) (rnd() % 60u) + 1;
    }
    if (rnd() % 5u == 0) *d = -*d; /* not normalized on purpose */
}

static int32_t add(int64_t *an, int64_t *ad, int64_t bn, int64_t bd) { return zxv_rat_add(*an, *ad, bn, bd, an, ad); }

static void test_kernels(void) {
    tb_program *p = prog(k_diff);
    int ok = 0, refused = 0;
    if (!p) {
        CHECK(0, "parse k_diff");
        return;
    }
    for (int it = 0; it < 4000; it++) {
        int64_t m = (int64_t) (rnd() % 5u), n = (int64_t) (rnd() % 7u);
        int big = it % 3 == 0;
        int64_t an[40], ad[40], xn[8], xd[8], yn[8], yd[8], aln, ald;
        int64_t sn = 99, sd = 99, dn = 99, dd = 99, zn[8], zd[8], pn[8], pd[8], vn[8], vd[8];
        int64_t rs[2] = {0, 1}, rd[2] = {0, 1}, rz[8][2], rp[8][2], rv[8][2], acc[2] = {0, 1};
        int32_t want = ZXV_OK, st;
        tb_array arr[10];
        tb_diag dg;
        for (int64_t k = 0; k < m * n; k++) rnd_rat(&an[k], &ad[k], big);
        for (int64_t k = 0; k < n; k++) {
            rnd_rat(&xn[k], &xd[k], big);
            rnd_rat(&yn[k], &yd[k], big);
            zn[k] = pn[k] = 99;
            zd[k] = pd[k] = 99;
        }
        for (int64_t k = 0; k < m; k++) vn[k] = vd[k] = 99;
        rnd_rat(&aln, &ald, big);
        /* reference, in the program's order of operations */
        {
            int64_t x2n[8], x2d[8], y2n[8], y2d[8], a2n[40], a2d[40], al2n, al2d;
            for (int64_t k = 0; k < n; k++) {
                zxv_rat_normalize(xn[k], xd[k], &x2n[k], &x2d[k]);
                zxv_rat_normalize(yn[k], yd[k], &y2n[k], &y2d[k]);
            }
            for (int64_t k = 0; k < m * n; k++) zxv_rat_normalize(an[k], ad[k], &a2n[k], &a2d[k]);
            zxv_rat_normalize(aln, ald, &al2n, &al2d);
            for (int64_t k = 0; k < n && !want; k++) want = add(&rs[0], &rs[1], x2n[k], x2d[k]);
            for (int64_t k = 0; k < n && !want; k++) {
                int64_t tn, td;
                want = zxv_rat_mul(x2n[k], x2d[k], y2n[k], y2d[k], &tn, &td);
                if (!want) want = add(&rd[0], &rd[1], tn, td);
            }
            for (int64_t k = 0; k < n && !want; k++)
                want = zxv_rat_mul(al2n, al2d, x2n[k], x2d[k], &rz[k][0], &rz[k][1]);
            for (int64_t k = 0; k < n && !want; k++) {
                want = add(&acc[0], &acc[1], x2n[k], x2d[k]);
                rp[k][0] = acc[0];
                rp[k][1] = acc[1];
            }
            for (int64_t i = 0; i < m && !want; i++) {
                rv[i][0] = 0;
                rv[i][1] = 1;
                for (int64_t j = 0; j < n && !want; j++) {
                    int64_t tn, td;
                    want = zxv_rat_mul(a2n[i + j * m], a2d[i + j * m], x2n[j], x2d[j], &tn, &td);
                    if (!want) want = add(&rv[i][0], &rv[i][1], tn, td);
                }
            }
        }
        arr[0] = arr2("a", an, ad, m, n);
        arr[1] = arr1("x", xn, xd, n);
        arr[2] = arr1("Y", yn, yd, n); /* names are not case-sensitive */
        arr[3] = arr0("alpha", &aln, &ald);
        arr[4] = arr0("s", &sn, &sd);
        arr[5] = arr0("d", &dn, &dd);
        arr[6] = arr1("z", zn, zd, n);
        arr[7] = arr1("p", pn, pd, n);
        arr[8] = arr1("v", vn, vd, m);
        st = tb_exec(p, arr, 9, 0, &dg);
        CHECK(st == want, "case %d: %s, reference %s: %s", it, tb_status_name(st), tb_status_name(want), dg.msg);
        if (st != ZXV_OK) {
            refused++;
            CHECK(sn == 99 && sd == 99 && dn == 99 && (n == 0 || (zn[0] == 99 && pn[0] == 99)) && (m == 0 || vn[0] == 99),
                  "case %d: outputs written on a refusal", it);
            continue;
        }
        ok++;
        CHECK(sn == rs[0] && sd == rs[1], "case %d: sum %s vs %s", it, rat(sn, sd), rat(rs[0], rs[1]));
        CHECK(dn == rd[0] && dd == rd[1], "case %d: dot %s vs %s", it, rat(dn, dd), rat(rd[0], rd[1]));
        for (int64_t k = 0; k < n; k++) {
            CHECK(zn[k] == rz[k][0] && zd[k] == rz[k][1], "case %d: scale[%lld]", it, (long long) k);
            CHECK(pn[k] == rp[k][0] && pd[k] == rp[k][1], "case %d: prefix[%lld]", it, (long long) k);
            CHECK(pd[k] > 0, "denominator positive");
        }
        for (int64_t k = 0; k < m; k++)
            CHECK(vn[k] == rv[k][0] && vd[k] == rv[k][1], "case %d: matvec[%lld] %s vs %s", it, (long long) k,
                  rat(vn[k], vd[k]), rat(rv[k][0], rv[k][1]));
    }
    CHECK(ok > 2000 && refused > 100, "the random cases cover both outcomes: %d ok, %d refused", ok, refused);
    printf("test_exec: kernels: %d random cases agreed with direct libzxv arithmetic, %d refused alike\n", ok, refused);
    tb_program_destroy(p);
}

static void test_layout(void) {
    tb_program *p = prog("trebago t\n  in a(m, n)\n  out t(n, m), r(m), c(n)\n  t(j, i) = a(i, j)\n"
                         "  r(i) = sum(j, a(i, j))\n  c(j) = sum(i, a(i, j))\nend\n");
    /* a = [1 2 3; 4 5 6], column-major */
    int64_t an[6] = {1, 4, 2, 5, 3, 6}, ad[6] = {1, 1, 1, 1, 1, 1};
    int64_t tn[6], td[6], rn[2], rd[2], cn[3], cd[3];
    tb_array arr[4];
    tb_diag d;
    if (!p) {
        CHECK(0, "parse");
        return;
    }
    arr[0] = arr2("a", an, ad, 2, 3);
    arr[1] = arr2("t", tn, td, 3, 2);
    arr[2] = arr1("r", rn, rd, 2);
    arr[3] = arr1("c", cn, cd, 3);
    CHECK(tb_exec(p, arr, 4, 0, &d) == ZXV_OK, "run: %s", d.msg);
    {
        const int64_t want_t[6] = {1, 2, 3, 4, 5, 6};
        for (int k = 0; k < 6; k++) CHECK(tn[k] == want_t[k] && td[k] == 1, "t[%d] = %lld", k, (long long) tn[k]);
    }
    CHECK(rn[0] == 6 && rn[1] == 15, "row sums");
    CHECK(cn[0] == 5 && cn[1] == 7 && cn[2] == 9, "column sums");
    /* the transpose target must have the swapped shape */
    arr[1] = arr2("t", tn, td, 2, 3);
    CHECK(tb_exec(p, arr, 4, 0, &d) == TB_E_SHAPE, "t(2,3) is refused: %s", d.msg);
    tb_program_destroy(p);
}

static void test_bindings(void) {
    tb_program *p = prog("trebago b\n  in x(n), w(3), k\n  inout z(n)\n  out s, y(n)\n  var t(n)\n"
                         "  t(i) = x(i) + w(1) * k\n  y(i) = t(i)\n  z(i) = z(i) + 1\n  s = sum(i, y(i))\nend\n");
    int64_t xn[3] = {1, 2, 3}, xd[3] = {1, 1, 1}, wn[3] = {1, 0, 0}, wd[3] = {1, 1, 1}, kn = 1, kd = -2;
    int64_t zn[3] = {0, 1, 2}, zd[3] = {1, 1, 1}, sn = 7, sd = 7, yn[3] = {7, 7, 7}, yd[3] = {7, 7, 7};
    tb_array a[6], b[7];
    tb_diag d;
    int32_t st;
    if (!p) {
        CHECK(0, "parse");
        return;
    }
    a[0] = arr1("x", xn, xd, 3);
    a[1] = arr1("w", wn, wd, 3);
    a[2] = arr0("k", &kn, &kd);
    a[3] = arr1("z", zn, zd, 3);
    a[4] = arr0("s", &sn, &sd);
    a[5] = arr1("y", yn, yd, 3);
    st = tb_exec(p, a, 6, 0, &d);
    CHECK(st == ZXV_OK, "run: %s", d.msg);
    CHECK(yn[0] == 1 && yd[0] == 2 && yn[2] == 5 && yd[2] == 2, "y = x - 1/2: %s %s", rat(yn[0], yd[0]), rat(yn[2], yd[2]));
    CHECK(sn == 9 && sd == 2, "s = 9/2, got %s", rat(sn, sd));
    CHECK(zn[0] == 1 && zn[1] == 2 && zn[2] == 3, "inout updated");
    CHECK(kn == 1 && kd == -2, "inputs are never written, even to normalize them");
    {
        uint64_t w = 0;
        CHECK(tb_last_work(&w) == ZXV_OK && w > 0 && w < 100, "work %llu", (unsigned long long) w);
        CHECK(tb_last_work(NULL) == ZXV_E_ARG, "NULL");
    }
    /* refusals, each leaving the outputs untouched */
    sn = sd = 7;
#define REFUSE(arrs, cnt, want, needle)                                                                     \
    do {                                                                                                    \
        st = tb_exec(p, arrs, cnt, 0, &d);                                                                  \
        CHECK(st == want, "%s, expected %s: %s", tb_status_name(st), tb_status_name(want), d.msg);          \
        CHECK(strstr(d.msg, needle) != NULL, "message '%s' lacks '%s'", d.msg, needle);                     \
        CHECK(sn == 7 && sd == 7, "output written on a refusal");                                          \
    } while (0)
    memcpy(b, a, sizeof a);
    REFUSE(b, 5, TB_E_NAME, "parameter 'y' is not bound");
    b[5] = arr1("q", yn, yd, 3);
    REFUSE(b, 6, TB_E_NAME, "'q' is not an in, out or inout parameter");
    b[5] = arr1("t", yn, yd, 3);
    REFUSE(b, 6, TB_E_NAME, "'t' is not an in, out or inout");
    b[5] = a[5];
    b[6] = a[0];
    REFUSE(b, 7, TB_E_NAME, "'x' is bound twice");
    memcpy(b, a, sizeof a);
    b[0] = arr2("x", xn, xd, 3, 1);
    REFUSE(b, 6, TB_E_SHAPE, "rank 1, the binding rank 2");
    memcpy(b, a, sizeof a);
    b[0] = arr1("x", xn, xd, 2);
    REFUSE(b, 6, TB_E_SHAPE, "dimension n is 2");
    memcpy(b, a, sizeof a);
    b[1] = arr1("w", wn, wd, 2);
    REFUSE(b, 6, TB_E_SHAPE, "declared with extent 3");
    memcpy(b, a, sizeof a);
    b[0].reserved = 1;
    REFUSE(b, 6, ZXV_E_ARG, "reserved must be 0");
    memcpy(b, a, sizeof a);
    b[0].extent[1] = 1;
    REFUSE(b, 6, ZXV_E_ARG, "unused extents");
    memcpy(b, a, sizeof a);
    b[0].extent[0] = -1;
    REFUSE(b, 6, ZXV_E_ARG, "extent -1");
    memcpy(b, a, sizeof a);
    b[0].extent[0] = (int64_t) TB_MAX_ELEMENTS + 1;
    REFUSE(b, 6, TB_E_LIMIT, "outside 0..");
    memcpy(b, a, sizeof a);
    b[0].num = NULL;
    REFUSE(b, 6, ZXV_E_ARG, "NULL numerator");
    memcpy(b, a, sizeof a);
    b[0].name = NULL;
    REFUSE(b, 6, ZXV_E_ARG, "has no name");
    memcpy(b, a, sizeof a);
    b[0].name = "a_name_that_is_much_longer_than_thirty_one_characters";
    REFUSE(b, 6, TB_E_NAME, "is not an in, out or inout");
    {
        int64_t bad = 0;
        memcpy(b, a, sizeof a);
        b[2] = arr0("k", &kn, &bad);
        REFUSE(b, 6, ZXV_E_ARG, "input k = 1/0 is not a valid rational (denominator 0)");
        bad = INT64_MIN;
        b[2] = arr0("k", &bad, &kd);
        st = tb_exec(p, b, 6, 0, &d);
        CHECK(st != ZXV_OK && strstr(d.msg, "INT64_MIN"), "INT64_MIN numerator: %s", d.msg);
        xd[1] = 0;
        REFUSE(a, 6, ZXV_E_ARG, "input x(2) = 2/0");
        xd[1] = 1;
    }
    CHECK(tb_exec(NULL, a, 6, 0, &d) == ZXV_E_ARG, "NULL program");
    CHECK(tb_exec(p, NULL, 1, 0, &d) == ZXV_E_ARG, "NULL arrays");
    CHECK(tb_exec(p, NULL, 0, 0, NULL) == TB_E_NAME, "nothing bound, NULL diag");
    /* the work limit */
    st = tb_exec(p, a, 6, 5, &d);
    CHECK(st == TB_E_LIMIT && strstr(d.msg, "work limit (5"), "work limit 5: %s", d.msg);
    {
        uint64_t w = 0;
        tb_last_work(&w);
        CHECK(w == 6, "the refused run did 6 units, got %llu", (unsigned long long) w);
    }
    CHECK(sn == 7, "untouched");
    tb_program_destroy(p);
}

static void test_shapes_at_run_time(void) {
    tb_program *p = prog("trebago s\n  in a(m, n), x(k)\n  out y(m)\n  y(i) = sum(j, a(i, j) * x(j))\nend\n");
    int64_t an[6] = {1, 1, 1, 1, 1, 1}, ad[6] = {1, 1, 1, 1, 1, 1}, xn[3] = {1, 2, 3}, xd[3] = {1, 1, 1};
    int64_t yn[2], yd[2];
    tb_array arr[3];
    tb_diag d;
    if (!p) {
        CHECK(0, "parse");
        return;
    }
    arr[0] = arr2("a", an, ad, 2, 3);
    arr[1] = arr1("x", xn, xd, 3);
    arr[2] = arr1("y", yn, yd, 2);
    CHECK(tb_exec(p, arr, 3, 0, &d) == ZXV_OK && yn[0] == 6, "k = n = 3: %s", d.msg);
    arr[1] = arr1("x", xn, xd, 2);
    CHECK(tb_exec(p, arr, 3, 0, &d) == TB_E_SHAPE && d.line == 4 && d.col == 29 && strstr(d.msg, "j runs over 1:3"),
          "k = 2 /= n: %s", d.msg);
    tb_program_destroy(p);
}

static void test_empty(void) {
    tb_program *p = prog("trebago e\n  in x(n)\n  out s, p(n), m(n, n)\n  s = sum(i, x(i)) + 1\n  p(i) = scan(x(i))\n"
                         "  m(i, j) = x(i) * x(j)\nend\n");
    int64_t sn = 5, sd = 5;
    tb_array arr[4];
    tb_diag d;
    if (!p) {
        CHECK(0, "parse");
        return;
    }
    arr[0] = arr1("x", NULL, NULL, 0);
    arr[1] = arr0("s", &sn, &sd);
    arr[2] = arr1("p", NULL, NULL, 0);
    arr[3] = arr2("m", NULL, NULL, 0, 0);
    CHECK(tb_exec(p, arr, 4, 0, &d) == ZXV_OK && sn == 1 && sd == 1, "empty vectors: s = 1: %s", d.msg);
    tb_program_destroy(p);
}

static void test_statement_semantics(void) {
    tb_program *p = prog("trebago r\n  inout y(n)\n  out p(n), q(n)\n  y(i) = y(n - i + 1)\n  p(i) = scan(y(i)) + scan(1)\n"
                         "  q(i) = scan(p(i))\nend\n");
    int64_t yn[4] = {1, 2, 3, 4}, yd[4] = {1, 1, 1, 1}, pn[4], pd[4], qn[4], qd[4];
    tb_array arr[3];
    tb_diag d;
    if (!p) {
        CHECK(0, "parse");
        return;
    }
    arr[0] = arr1("y", yn, yd, 4);
    arr[1] = arr1("p", pn, pd, 4);
    arr[2] = arr1("q", qn, qd, 4);
    CHECK(tb_exec(p, arr, 3, 0, &d) == ZXV_OK, "run: %s", d.msg);
    CHECK(yn[0] == 4 && yn[1] == 3 && yn[2] == 2 && yn[3] == 1, "the right-hand side is evaluated before storing");
    CHECK(pn[0] == 5 && pn[1] == 9 && pn[2] == 12 && pn[3] == 14, "two scans, each with its own sum");
    CHECK(qn[0] == 5 && qn[1] == 14 && qn[2] == 26 && qn[3] == 40, "a scan in a later statement starts at 0");
    /* running twice gives the same */
    CHECK(tb_exec(p, arr, 3, 0, &d) == ZXV_OK && yn[0] == 1 && pn[0] == 2, "second run: %s", d.msg);
    tb_program_destroy(p);
}

typedef struct {
    const tb_program *p;
    int bad;
} job;

static void *worker(void *arg) {
    job *j = (job *) arg;
    for (int it = 0; it < 300; it++) {
        int64_t xn[5], xd[5], yn[5], yd[5], sn = 0, sd = 0;
        tb_array a[3];
        tb_diag d;
        for (int k = 0; k < 5; k++) {
            xn[k] = it + k;
            xd[k] = k + 1;
            yn[k] = k + 1;
            yd[k] = 1;
        }
        a[0] = arr1("x", xn, xd, 5);
        a[1] = arr1("y", yn, yd, 5);
        a[2] = arr0("s", &sn, &sd);
        /* sum((it + k) / (k + 1) * (k + 1)) = 5 * it + 10 */
        if (tb_exec(j->p, a, 3, 0, &d) != ZXV_OK || sn != 5 * it + 10 || sd != 1) j->bad++;
    }
    return NULL;
}

static void test_threads(void) {
    tb_program *p = prog("trebago dot\n  in x(n), y(n)\n  out s\n  s = sum(i, x(i) * y(i))\nend\n");
    pthread_t th[4];
    job jobs[4];
    if (!p) return;
    for (int i = 0; i < 4; i++) {
        jobs[i].p = p;
        jobs[i].bad = 0;
        pthread_create(&th[i], NULL, worker, &jobs[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        CHECK(jobs[i].bad == 0, "thread %d: %d wrong results", i, jobs[i].bad);
    }
    tb_program_destroy(p);
}

int main(void) {
    test_values();
    test_kernels();
    test_layout();
    test_bindings();
    test_shapes_at_run_time();
    test_empty();
    test_statement_semantics();
    test_threads();
    return report("test_exec");
}
