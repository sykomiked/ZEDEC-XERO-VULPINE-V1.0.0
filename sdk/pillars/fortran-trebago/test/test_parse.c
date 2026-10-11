/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_parse.c — the Trebago parser and printer: accepted programs and
 * their canonical text, round trips and digests, parameters, limits, and
 * every refusal at its line and column. */
#include "tb_test.h"

/* A program from a declaration block and one statement. */
static char g_text[1 << 16];
static const char *wrap(const char *decls, const char *stmt) {
    snprintf(g_text, sizeof g_text, "trebago t\n%s\n  %s\nend\n", decls, stmt);
    return g_text;
}

static void accepts(const char *text, const char *want_canon) {
    tb_program *p = NULL, *q = NULL;
    tb_diag d;
    int32_t st = tb_parse(text, strlen(text), &p, &d);
    CHECK(st == ZXV_OK, "refused: %s\n%s", d.msg, text);
    if (st != ZXV_OK) return;
    {
        char c1[1 << 14];
        uint8_t d1[32], d2[32];
        snprintf(c1, sizeof c1, "%s", canon(p));
        if (want_canon) CHECK(strcmp(c1, want_canon) == 0, "canonical text\n%s\nexpected\n%s", c1, want_canon);
        st = tb_parse(c1, strlen(c1), &q, &d);
        CHECK(st == ZXV_OK, "canonical text refused: %s\n%s", d.msg, c1);
        if (q) {
            CHECK(strcmp(canon(q), c1) == 0, "canonical text is not a fixed point:\n%s\n%s", c1, canon(q));
            tb_program_digest(p, d1);
            tb_program_digest(q, d2);
            CHECK(memcmp(d1, d2, 32) == 0, "digest changed after a round trip");
        }
    }
    tb_program_destroy(p);
    tb_program_destroy(q);
}

static void refuses(const char *text, int32_t want, uint32_t line, uint32_t col, const char *needle) {
    tb_program *p = (tb_program *) 0x1;
    tb_diag d;
    int32_t st = tb_parse(text, strlen(text), &p, &d);
    CHECK(st == want, "status %s, expected %s: %s\n%s", tb_status_name(st), tb_status_name(want), d.msg, text);
    CHECK(p == (tb_program *) 0x1, "out_program written on a refusal");
    if (st != want) return;
    if (line) CHECK(d.line == line && d.col == col, "at %u:%u, expected %u:%u: %s", d.line, d.col, line, col, d.msg);
    if (needle) CHECK(strstr(d.msg, needle) != NULL, "message '%s' lacks '%s'", d.msg, needle);
    {
        char pre[48];
        snprintf(pre, sizeof pre, "line %u, column %u: ", d.line, d.col);
        if (d.line) CHECK(strncmp(d.msg, pre, strlen(pre)) == 0, "message does not start with the position: %s", d.msg);
    }
}

static void test_accepts(void) {
    accepts("trebago dot\n  in x(n), y(n)\n  out s\n  s = sum(i, x(i) * y(i))\nend\n",
            "trebago dot\n  in x(n)\n  in y(n)\n  out s\n  s = sum(i, x(i) * y(i))\nend\n");
    /* comments, blank lines, case, continuation, unary plus, extra parentheses */
    accepts("\n! lead\n\nTREBAGO Dot ! c\n\n  IN X(N)\n  IN Y(N)\n  OUT S\n\n  S = SUM(I, +((X(I)) * &  ! c\n    Y(I)))\nEND\n\n! trail\n",
            "trebago dot\n  in x(n)\n  in y(n)\n  out s\n  s = sum(i, x(i) * y(i))\nend\n");
    /* no final newline, CRLF line ends, tabs */
    accepts("trebago a\r\n\tin x(n)\r\n\tout s\r\n\ts = sum(i, x(i))\r\nend", NULL);
    /* constants are printed as their exact value */
    accepts(wrap("  in x(n)\n  out s\n  const h = 1/3\n  const q = 2 * h - 0.25\n  const big = 2 ** 62", "s = x(1) * q"),
            "trebago t\n  in x(n)\n  out s\n  const h = 1/3\n  const q = 5/12\n  const big = 4611686018427387904\n"
            "  s = x(1) * q\nend\n");
    accepts(wrap("  in x(n)\n  out s\n  const m = -1/3", "s = m"), "trebago t\n  in x(n)\n  out s\n  const m = -1/3\n  s = m\nend\n");
    /* precedence: minimal parentheses that keep the tree */
    accepts(wrap("  in x(n)\n  out s", "s = (x(1) + x(2)) * x(1) - (x(1) - x(2)) / (x(1) * x(2))"),
            "trebago t\n  in x(n)\n  out s\n  s = (x(1) + x(2)) * x(1) - (x(1) - x(2)) / (x(1) * x(2))\nend\n");
    accepts(wrap("  in x(n)\n  out s", "s = ((x(1) - x(2)) - x(1)) + (x(1) * (x(2) / x(1)))"),
            "trebago t\n  in x(n)\n  out s\n  s = x(1) - x(2) - x(1) + x(1) * (x(2) / x(1))\nend\n");
    accepts(wrap("  in x(n)\n  out s", "s = -x(1) ** 2 + (-x(1)) ** 2 - -x(2) * -(x(1) * x(2))"),
            "trebago t\n  in x(n)\n  out s\n  s = -x(1) ** 2 + (-x(1)) ** 2 - -x(2) * -(x(1) * x(2))\nend\n");
    accepts(wrap("  in x(n)\n  out s", "s = 2 ** -3 + (2 ** 3) ** 2 + x(1) ** -(n - 1) + x(1) ** (n * 2)"),
            "trebago t\n  in x(n)\n  out s\n  s = 2 ** -3 + (2 ** 3) ** 2 + x(1) ** -(n - 1) + x(1) ** (n * 2)\nend\n");
    accepts(wrap("  in x(n)\n  out s", "s = --x(1)"), "trebago t\n  in x(n)\n  out s\n  s = --x(1)\nend\n");
    /* decimals are exact and print as their value */
    accepts(wrap("  in x(n)\n  out s", "s = 0.50 * x(1) + 12.250 + 007"),
            "trebago t\n  in x(n)\n  out s\n  s = 0.5 * x(1) + 12.25 + 7\nend\n");
    /* matrices, vars, fixed extents, scan, explicit ranges, functions */
    accepts("trebago k\n  in a(m, n), x(n)\n  inout z(m)\n  out y(m), p(m), t(n, m), c\n  var w(3), u\n"
            "  y(i) = sum(j, a(i, j) * x(j))\n  p(i) = 2 * scan(y(i)) + 1\n  t(j, i) = a(i, j)\n"
            "  w(k) = k\n  u = sum(k = 1:3, w(k))\n  z(i) = z(m - i + 1)\n"
            "  c = abs(min(u, -u, 1, 2)) + max(m, n)\n  y(i) = sum(j = 1:i - 1, a(i, j))\nend\n",
            "trebago k\n  in a(m, n)\n  in x(n)\n  inout z(m)\n  out y(m)\n  out p(m)\n  out t(n, m)\n  out c\n"
            "  var w(3)\n  var u\n  y(i) = sum(j, a(i, j) * x(j))\n  p(i) = 2 * scan(y(i)) + 1\n  t(j, i) = a(i, j)\n"
            "  w(k) = k\n  u = sum(k = 1:3, w(k))\n  z(i) = z(m - i + 1)\n"
            "  c = abs(min(min(min(u, -u), 1), 2)) + max(m, n)\n  y(i) = sum(j = 1:i - 1, a(i, j))\nend\n");
    /* an index can be reused after its scope ends; nested sums */
    accepts(wrap("  in a(n, n)\n  out s, r(n)", "s = sum(i, sum(j, a(i, j))) + sum(i, a(i, i))"), NULL);
    accepts(wrap("  in x(n)\n  out s", "s = sum(i, sum(j = i:n, x(i) * x(j)))"), NULL);
    /* a declaration with no statements, a program with no parameters */
    accepts("trebago empty\nend\n", "trebago empty\nend\n");
    accepts("trebago c\n  const k = 3\nend\n", "trebago c\n  const k = 3\nend\n");
    /* the largest literal that fits */
    accepts(wrap("  out s", "s = 9223372036854775807"), NULL);
    /* names: digits and underscores, 31 characters */
    accepts(wrap("  in x_1(n_2)\n  out s_0123456789012345678901234", "s_0123456789012345678901234 = x_1(n_2)"), NULL);
}

static void test_same(void) {
    tb_program *a = prog("trebago dot\n in x(n), y(n)\n out s\n s = sum(i, x(i) * y(i))\nend\n");
    tb_program *b = prog("trebago DOT\nin X(N)\nin Y(N)\nout S\nS=SUM(I,X(I)*Y(I))\nend");
    tb_program *c = prog("trebago dot\n in x(n), y(n)\n out s\n s = sum(i, y(i) * x(i))\nend\n");
    tb_program *e = prog("trebago dot\n in x(n), y(n)\n out s\n s = sum(k, x(k) * y(k))\nend\n");
    uint8_t da[32], db[32], dc[32], de[32];
    char name[8];
    size_t n = 0;
    CHECK(a && b && c && e, "parse");
    if (!a || !b || !c || !e) return;
    tb_program_digest(a, da);
    tb_program_digest(b, db);
    tb_program_digest(c, dc);
    tb_program_digest(e, de);
    CHECK(memcmp(da, db, 32) == 0, "same program, different spelling: digests differ");
    CHECK(memcmp(da, dc, 32) != 0, "x*y and y*x are different texts and must differ");
    CHECK(memcmp(da, de, 32) != 0, "index names are part of the text");
    {
        uint8_t ref[32];
        const char *t = canon(a);
        zxv_sha3_256((const uint8_t *) t, strlen(t), ref);
        CHECK(memcmp(ref, da, 32) == 0, "digest is the SHA3-256 of the canonical text");
    }
    /* R4 two-call buffers */
    CHECK(tb_program_name(a, NULL, 0, &n) == ZXV_E_BUFFER && n == 3, "name length");
    CHECK(tb_program_name(a, name, 3, &n) == ZXV_E_BUFFER, "no room for the NUL");
    CHECK(tb_program_name(a, name, sizeof name, &n) == ZXV_OK && strcmp(name, "dot") == 0, "name");
    CHECK(tb_program_text(a, NULL, 0, &n) == ZXV_E_BUFFER && n == strlen(canon(a)), "text length");
    CHECK(tb_program_text(a, NULL, 0, NULL) == ZXV_E_ARG, "NULL out_len");
    CHECK(tb_program_digest(NULL, da) == ZXV_E_ARG, "NULL program");
    tb_program_destroy(a);
    tb_program_destroy(b);
    tb_program_destroy(c);
    tb_program_destroy(e);
    tb_program_destroy(NULL);
}

static void test_params(void) {
    tb_program *p = prog("trebago k\n  in a(m, 3), alpha\n  inout z(m)\n  out y(m)\n  var w(m)\n  const c = 2\n"
                         "  w(i) = z(i)\n  y(i) = w(i) * alpha\nend\n");
    uint32_t n = 0;
    tb_param pa;
    CHECK(p != NULL, "parse");
    if (!p) return;
    CHECK(tb_param_count(p, &n) == ZXV_OK && n == 4, "4 parameters, got %u", n);
    CHECK(tb_param_at(p, 0, &pa) == ZXV_OK && strcmp(pa.name, "a") == 0 && pa.role == TB_ROLE_IN && pa.rank == 2 &&
              strcmp(pa.dim[0], "m") == 0 && pa.dim[1][0] == '\0' && pa.fixed[1] == 3,
          "a(m, 3)");
    CHECK(tb_param_at(p, 1, &pa) == ZXV_OK && strcmp(pa.name, "alpha") == 0 && pa.rank == 0, "alpha");
    CHECK(tb_param_at(p, 2, &pa) == ZXV_OK && pa.role == TB_ROLE_INOUT, "z inout");
    CHECK(tb_param_at(p, 3, &pa) == ZXV_OK && pa.role == TB_ROLE_OUT && strcmp(pa.name, "y") == 0, "y out");
    CHECK(tb_param_at(p, 4, &pa) == ZXV_E_NOT_FOUND, "only 4");
    CHECK(tb_param_at(NULL, 0, &pa) == ZXV_E_ARG, "NULL");
    tb_program_destroy(p);
}

#define D "  in x(n), y(n)\n  out s, v(n)\n  var t(n)"

static void test_refusals(void) {
    /* structure */
    refuses("", TB_E_SYNTAX, 1, 1, "trebago NAME");
    refuses("program p\nend\n", TB_E_SYNTAX, 1, 1, "trebago NAME");
    refuses("trebago\nend\n", TB_E_SYNTAX, 1, 8, "program's name");
    refuses("trebago end\nend\n", TB_E_SYNTAX, 1, 9, "program's name");
    refuses("trebago p q\nend\n", TB_E_SYNTAX, 1, 11, "end of the line");
    refuses("trebago p\n  out s\n  s = 1\n", TB_E_SYNTAX, 4, 1, "expected end");
    refuses("trebago p\nend\nx\n", TB_E_SYNTAX, 3, 1, "nothing may follow end");
    refuses("trebago p\n  out s\n  s = 1\n  in x(n)\nend\n", TB_E_SYNTAX, 4, 3, "declarations come before");
    refuses("trebago p\n  out s\n  s = 1 s = 2\nend\n", TB_E_SYNTAX, 3, 9, "one statement per line");
    refuses("trebago p\n  out s\n  s = 1 &\nend\n", TB_E_SYNTAX, 4, 1, "one statement per line");
    refuses("trebago p\n  out s\n  s = 1 & x\nend\n", TB_E_SYNTAX, 3, 9, "must end its line");
    /* numbers */
    refuses(wrap(D, "s = 1.0e-3"), TB_E_SYNTAX, 5, 7, "exponent notation");
    refuses(wrap(D, "s = 1d0"), TB_E_SYNTAX, 5, 7, "exponent notation");
    refuses(wrap(D, "s = 1.0_dp"), TB_E_SYNTAX, 5, 7, "kind suffixes");
    refuses(wrap(D, "s = 2."), TB_E_SYNTAX, 5, 7, "digits after");
    refuses(wrap(D, "s = .5"), TB_E_SYNTAX, 5, 7, "unexpected character '.'");
    refuses(wrap(D, "s = 1.2.3"), TB_E_SYNTAX, 5, 7, "another '.'");
    refuses(wrap(D, "s = 9223372036854775808"), ZXV_E_RANGE, 5, 7, "does not fit");
    refuses(wrap(D, "s = 0.00000000000000000001"), ZXV_E_RANGE, 5, 7, "does not fit");
    /* characters */
    refuses(wrap(D, "s = x(1) % 2"), TB_E_SYNTAX, 5, 12, "unexpected character '%'");
    refuses(wrap(D, "s = x[1]"), TB_E_SYNTAX, 5, 8, "unexpected character '['");
    refuses("trebago p\n  out s\n  s = \"1\"\nend\n", TB_E_SYNTAX, 3, 7, "unexpected character");
    {
        static const char t[] = "trebago p\n  out s\n  s = 1\0\nend\n";
        tb_program *p = NULL;
        tb_diag d;
        CHECK(tb_parse(t, sizeof t - 1u, &p, &d) == TB_E_SYNTAX && d.line == 3 && d.col == 8, "NUL byte: %s", d.msg);
    }
    accepts("trebago p\n  out s\n  s = 1 ! caf\xc3\xa9\nend\n", NULL); /* UTF-8 in a comment */
    refuses("trebago p\n  out s\n  s = 1 ! \xff\nend\n", ZXV_E_UTF8, 3, 11, "UTF-8");
    refuses("trebago p\n  out s\n  s = \xc3\xa9\nend\n", TB_E_SYNTAX, 3, 7, "0xc3");
    /* names */
    refuses(wrap(D, "s = w"), TB_E_NAME, 5, 7, "unknown name 'w'");
    refuses(wrap(D, "s = w(1)"), TB_E_NAME, 5, 7, "not an array");
    refuses(wrap(D, "w = 1"), TB_E_NAME, 5, 3, "unknown name 'w'");
    refuses(wrap(D, "x(i) = 1"), TB_E_NAME, 5, 3, "is an input");
    refuses(wrap("  out s\n  const c = 1", "c = 2"), TB_E_NAME, 4, 3, "constant");
    refuses(wrap(D, "n = 2"), TB_E_NAME, 5, 3, "dimension");
    refuses(wrap(D, "end = 2"), TB_E_SYNTAX, 5, 7, "nothing may follow end");
    refuses(wrap("  in x(n)\n  out x", "x = 1"), TB_E_NAME, 3, 7, "declared twice");
    refuses(wrap("  in x(n)\n  out n", "n = 1"), TB_E_NAME, 3, 7, "declared twice");
    refuses(wrap("  in x(n)\n  out s(x)", "s(i) = 1"), TB_E_NAME, 3, 9, "not a dimension");
    refuses(wrap("  in sum(n)", "x = 1"), TB_E_NAME, 2, 6, "reserved");
    refuses(wrap("  in x(end)", "x = 1"), TB_E_NAME, 2, 8, "reserved");
    refuses(wrap(D, "v(n) = 1"), TB_E_NAME, 5, 5, "already a name");
    refuses(wrap(D, "v(x) = 1"), TB_E_NAME, 5, 5, "already a name");
    refuses(wrap(D, "s = sum(x, 1)"), TB_E_NAME, 5, 11, "already a name");
    refuses(wrap(D, "s = sum(i, sum(i, x(i)))"), TB_E_NAME, 5, 18, "already a name");
    refuses(wrap(D, "v(i) = sum(i, x(i))"), TB_E_NAME, 5, 14, "already a name");
    refuses(wrap(D, "s = sum(in, 1)"), TB_E_NAME, 5, 11, "reserved");
    refuses(wrap(D, "s = in"), TB_E_SYNTAX, 5, 7, "keyword");
    refuses(wrap("  in abcdefghijabcdefghijabcdefghijab", "x = 1"), TB_E_SYNTAX, 2, 6, "at most 31");
    /* shapes and ranks */
    refuses(wrap(D, "s = x"), TB_E_SHAPE, 5, 7, "subscript it");
    refuses(wrap(D, "s = x(1, 2)"), TB_E_SHAPE, 5, 10, "too many subscripts");
    refuses(wrap("  in a(n, n)\n  out s", "s = a(1)"), TB_E_SHAPE, 4, 10, "too few subscripts");
    refuses(wrap(D, "s = s(1)"), TB_E_SHAPE, 5, 7, "scalar");
    refuses(wrap(D, "s(i) = 1"), TB_E_SHAPE, 5, 4, "scalar");
    refuses(wrap(D, "v = 1"), TB_E_SHAPE, 5, 5, "assign its elements");
    refuses(wrap(D, "v(i, j) = 1"), TB_E_SHAPE, 5, 6, "too many indices");
    refuses(wrap(D, "v(1) = 1"), TB_E_SYNTAX, 5, 5, "loop index names");
    refuses(wrap(D, "v(i + 1) = 1"), TB_E_SYNTAX, 5, 7, "')'");
    refuses(wrap("  in a(n, n, n)", "x = 1"), TB_E_SHAPE, 2, 14, "at most 2");
    refuses(wrap("  in a(1.5)", "x = 1"), TB_E_SYNTAX, 2, 8, "whole number");
    refuses(wrap("  in a(0x)", "x = 1"), TB_E_SYNTAX, 2, 8, "run into a name");
    refuses(wrap("  in a(n)\n  const c(2) = 1", "x = 1"), TB_E_SHAPE, 3, 10, "constant is a scalar");
    refuses(wrap("  in x(n)\n  out s\n  var w(m)", "s = 1"), TB_E_SHAPE, 4, 7, "dimension 'm' of var 'w'");
    refuses(wrap(D, "s = sum(i, 1)"), TB_E_SHAPE, 5, 11, "cannot tell the range of 'i'");
    refuses(wrap(D, "s = sum(i, x(i + 1))"), TB_E_SHAPE, 5, 11, "cannot tell");
    /* integer contexts */
    refuses(wrap(D, "s = x(1/2)"), TB_E_SYNTAX, 5, 10, "subscript must be an integer");
    refuses(wrap(D, "s = x(0.0 + 1)"), TB_E_SYNTAX, 5, 13, "subscript must be an integer");
    refuses(wrap(D, "s = x(s)"), TB_E_SYNTAX, 5, 9, "subscript must be an integer");
    refuses(wrap(D, "s = x(x(1))"), TB_E_SYNTAX, 5, 9, "subscript must be an integer");
    refuses(wrap(D, "s = x(1) ** 0.5"), TB_E_SYNTAX, 5, 15, "fractional power");
    refuses(wrap(D, "s = x(1) ** x(2)"), TB_E_SYNTAX, 5, 15, "fractional power");
    refuses(wrap(D, "s = 2 ** (1/2)"), TB_E_SYNTAX, 5, 14, "fractional power");
    refuses(wrap(D, "s = sum(i = 1:n/2, x(i))"), TB_E_SYNTAX, 5, 18, "upper bound");
    refuses(wrap(D, "s = sum(i = 0.5:n, x(i))"), TB_E_SYNTAX, 5, 15, "lower bound");
    refuses(wrap(D, "s = sum(i = 1 n, x(i))"), TB_E_SYNTAX, 5, 17, "':'");
    /* scan */
    refuses(wrap(D, "s = scan(x(1))"), TB_E_SYNTAX, 5, 7, "scan");
    refuses(wrap(D, "v(i) = sum(j, scan(x(j)))"), TB_E_SYNTAX, 5, 17, "scan");
    refuses(wrap(D, "v(i) = scan(scan(x(i)))"), TB_E_SYNTAX, 5, 15, "scan");
    refuses(wrap("  in a(n, n)\n  out b(n, n)", "b(i, j) = scan(a(i, j))"), TB_E_SYNTAX, 4, 13, "scan");
    /* constants */
    refuses(wrap(D "\n  const c = x(1)", "s = c"), TB_E_SYNTAX, 5, 13, "constant cannot use an array");
    refuses(wrap(D "\n  const c = n", "s = c"), TB_E_SYNTAX, 5, 13, "constant cannot use a dimension");
    refuses(wrap(D "\n  const c = sum(i = 1:3, 1)", "s = c"), TB_E_SYNTAX, 5, 16, "constant cannot use sum");
    refuses(wrap(D "\n  const c = s", "s = c"), TB_E_SYNTAX, 5, 13, "earlier constants");
    refuses(wrap(D "\n  const c = d", "s = c"), TB_E_NAME, 5, 13, "unknown name");
    refuses(wrap(D "\n  const c = 2 ** 63", "s = c"), ZXV_E_RANGE, 5, 15, "does not fit");
    refuses(wrap(D "\n  const c = 1 / 0", "s = c"), TB_E_DIVZERO, 5, 15, "division by zero");
    refuses(wrap(D "\n  const c = 0 ** -1", "s = c"), TB_E_DIVZERO, 5, 15, "0 ** -1");
    /* expression syntax */
    refuses(wrap(D, "s = "), TB_E_SYNTAX, 5, 7, "expected a number");
    refuses(wrap(D, "s = (1"), TB_E_SYNTAX, 5, 9, "to close '('");
    refuses(wrap(D, "s = 1 +"), TB_E_SYNTAX, 5, 10, "expected a number");
    refuses(wrap(D, "s = 1 * * 2"), TB_E_SYNTAX, 5, 11, "expected a number");
    refuses(wrap(D, "s = min(1)"), TB_E_SYNTAX, 5, 12, "two or more arguments");
    refuses(wrap(D, "s = abs(1, 2)"), TB_E_SYNTAX, 5, 12, "abs(");
    refuses(wrap(D, "s = sum(1, 2)"), TB_E_SYNTAX, 5, 11, "loop index");
    refuses(wrap(D, "s 1"), TB_E_SYNTAX, 5, 5, "'='");
    refuses(wrap(D, "s == 1"), TB_E_SYNTAX, 5, 6, "expected a number");
}

static void test_limits(void) {
    static char big[1 << 17];
    tb_program *p = NULL;
    tb_diag d;
    size_t k;
    /* depth: 250 nested parentheses, and a 300-term chain */
    k = append(big, sizeof big, 0, "trebago p\n  out s\n  s = ");
    for (int i = 0; i < 250; i++) big[k++] = '(';
    big[k++] = '1';
    for (int i = 0; i < 250; i++) big[k++] = ')';
    k = append(big, sizeof big, k, "\nend\n");
    CHECK(tb_parse(big, k, &p, &d) == ZXV_OK, "parentheses alone add no depth: %s", d.msg);
    tb_program_destroy(p);
    p = NULL;
    k = append(big, sizeof big, 0, "trebago p\n  out s\n  s = ");
    for (int i = 0; i < 250; i++) k = append(big, sizeof big, k, "-");
    k = append(big, sizeof big, k, "1\nend\n");
    CHECK(tb_parse(big, k, &p, &d) == TB_E_LIMIT && strstr(d.msg, "too deep"), "250 negations: %s", d.msg);
    k = append(big, sizeof big, 0, "trebago p\n  out s\n  s = 1");
    for (int i = 0; i < 300; i++) k = append(big, sizeof big, k, " + 1");
    k = append(big, sizeof big, k, "\nend\n");
    CHECK(tb_parse(big, k, &p, &d) == TB_E_LIMIT, "a 300-term chain is too deep: %s", d.msg);
    k = append(big, sizeof big, 0, "trebago p\n  out s\n  s = 1");
    for (int i = 0; i < 150; i++) k = append(big, sizeof big, k, " + 1");
    k = append(big, sizeof big, k, "\nend\n");
    CHECK(tb_parse(big, k, &p, &d) == ZXV_OK, "a 150-term chain fits: %s", d.msg);
    tb_program_destroy(p);
    p = NULL;
    /* too many declarations */
    k = append(big, sizeof big, 0, "trebago p\n");
    for (int i = 0; i < 300; i++) k = append(big, sizeof big, k, "  out s%d\n", i);
    k = append(big, sizeof big, k, "end\n");
    CHECK(tb_parse(big, k, &p, &d) == TB_E_LIMIT && d.line == 258, "257th declaration: %s", d.msg);
    /* text size */
    CHECK(tb_parse(big, TB_MAX_TEXT + 1u, &p, &d) == TB_E_LIMIT, "longer than TB_MAX_TEXT");
    /* an extent above the limit */
    CHECK(tb_parse("trebago p\n in x(67108865)\nend\n", 31, &p, &d) == TB_E_LIMIT, "extent limit: %s", d.msg);
    /* arguments */
    CHECK(tb_parse(NULL, 1, &p, &d) == ZXV_E_ARG, "NULL text");
    CHECK(tb_parse("x", 1, NULL, &d) == ZXV_E_ARG, "NULL out");
    CHECK(tb_parse(NULL, 0, &p, NULL) == TB_E_SYNTAX, "empty text, NULL diag");
    CHECK(strcmp(tb_status_name(TB_E_SHAPE), "TB_E_SHAPE") == 0 && strcmp(tb_status_name(ZXV_E_RANGE), "ZXV_E_RANGE") == 0,
          "status names");
    CHECK(tb_status_class(TB_E_SYNTAX) == ZXV_CLASS_USAGE && tb_status_class(TB_E_LIMIT) == ZXV_CLASS_REFUSED &&
              tb_status_class(ZXV_E_RANGE) == ZXV_CLASS_USAGE && tb_status_class(ZXV_OK) == ZXV_CLASS_OK,
          "status classes");
    {
        char b[8];
        size_t n = 0;
        CHECK(tb_last_error(NULL, 0, &n) == ZXV_E_BUFFER && n > 0, "last error length");
        CHECK(tb_last_error(b, sizeof b, NULL) == ZXV_E_ARG, "last error NULL out_len");
    }
}

int main(void) {
    test_accepts();
    test_same();
    test_params();
    test_refusals();
    test_limits();
    return report("test_parse");
}
