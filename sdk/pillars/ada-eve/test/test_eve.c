/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_eve.c — the Eve front end and the equivalence rule: an Eve text and
 * an Ada specification that declare the same types have the same
 * canonical text and the same SHA3-256 digest; any difference in a range,
 * a scale, a literal, an order or a name changes the digest. */
#include "ze_test.h"

static const char k_ada[] =
    "package Payments is\n"
    "   pragma Pure;\n"
    "   type Currency is (NGN, GHS, KES, ZAR, USD);\n"
    "   type Account_Id is range 0 .. 2**32 - 1;\n"
    "   type Money is delta 0.01 digits 15;\n"
    "   subtype Fee is Money range -999.99 .. 999.99;\n"
    "   type Date8 is range 1900_01_01 .. 2999_12_31;\n"
    "   type Line_No is range 1 .. 4;\n"
    "   type Status is (Fresh, Posted, Refused);\n"
    "   type Lines is array (Line_No) of Money;\n"
    "   type Payment is record\n"
    "      Ref        : String (1 .. 16);\n"
    "      Amount     : Money;\n"
    "      Ccy        : Currency;\n"
    "      From, To   : Account_Id;\n"
    "      Value_Date : Date8 := 2024_01_01;\n"
    "      Fee_Charge : Fee;\n"
    "      Breakdown  : Lines;\n"
    "      State      : Status range Fresh .. Posted;\n"
    "   end record;\n"
    "end Payments;\n";

static const char k_eve[] =
    "eve payments\n"
    "currency = (ngn, ghs, kes, zar, usd)\n"
    "account_id = 0 .. 2**32 - 1\n"
    "money = dec(15, 2)\n"
    "fee = money -999.99 .. 999.99\n"
    "date8 = 19000101 .. 29991231   # YYYYMMDD\n"
    "line_no = 1 .. 4\n"
    "status = (fresh, posted, refused)\n"
    "lines = [line_no] money\n"
    "payment {\n"
    "  ref: str(16)\n"
    "  amount: money\n"
    "  ccy: currency\n"
    "  from, to: account_id\n"
    "  value_date: date8\n"
    "  fee_charge: fee\n"
    "  breakdown: lines\n"
    "  state: status fresh .. posted\n"
    "}\n";

static const char k_canon[] =
    "eve payments\n"
    "currency = (ngn, ghs, kes, zar, usd)\n"
    "account_id = 0 .. 4294967295\n"
    "money = dec(15, 2)\n"
    "fee = money -999.99 .. 999.99\n"
    "date8 = 19000101 .. 29991231\n"
    "line_no = 1 .. 4\n"
    "status = (fresh, posted, refused)\n"
    "lines = [line_no] money\n"
    "payment {\n"
    "  ref: str(16)\n"
    "  amount: money\n"
    "  ccy: currency\n"
    "  from: account_id\n"
    "  to: account_id\n"
    "  value_date: date8\n"
    "  fee_charge: fee\n"
    "  breakdown: lines\n"
    "  state: status fresh .. posted\n"
    "}\n";

static const char k_digest[] = "2c3a553116257c1d64a240a9d44ce63bbc61ccabd2a03b0595006af5ba945b46";

static int same(const ze_pkg *a, const ze_pkg *b) {
    int32_t s = -1;
    if (!a || !b || ze_pkg_same(a, b, &s) != ZXV_OK) return -1;
    return s;
}

static void test_equivalence(void) {
    ze_pkg *a = parse(1, k_ada), *e = parse(0, k_eve), *c, *pa;
    uint8_t da[32], de[32], dx[32];
    CHECK(a && e, "both forms parse");
    if (!a || !e) return;
    CHECK(same(a, e) == 1, "Ada and Eve are one layout");
    CHECK(strcmp(canon(a), k_canon) == 0, "canonical text:\n%s", canon(a));
    CHECK(strcmp(canon(e), k_canon) == 0, "the Eve form's canonical text");
    CHECK(ze_pkg_digest(a, da) == ZXV_OK && ze_pkg_digest(e, de) == ZXV_OK && memcmp(da, de, 32) == 0,
          "equal digests");
    CHECK(strcmp(hex(da, 32), k_digest) == 0, "digest %s", hex(da, 32));
    CHECK(zxv_sha3_256((const uint8_t *) k_canon, strlen(k_canon), dx) == ZXV_OK && memcmp(dx, da, 32) == 0,
          "the digest is SHA3-256 of the canonical text");
    c = parse(0, k_canon);
    CHECK(same(c, a) == 1, "the canonical text re-parses to the same layout");
    pa = parse(1, as_ada(e));
    CHECK(same(pa, e) == 1, "the printed Ada re-parses to the same layout");
    CHECK(strstr(as_ada(e), "type Money is delta 0.01 digits 15;") != NULL, "printed Ada:\n%s", as_ada(e));
    CHECK(strstr(as_ada(e), "State : Status range Fresh .. Posted;") != NULL, "a constrained component");
    ze_pkg_destroy(c);
    ze_pkg_destroy(pa);
    ze_pkg_destroy(a);
    ze_pkg_destroy(e);
}

/* Each variant differs from k_eve in one place and must change the digest;
 * each spelling differs only in spelling and must not. */
static void test_differences(void) {
    static const char *const differ[][2] = {
        {"money = dec(15, 2)", "money = dec(15, 3)"},
        {"money = dec(15, 2)", "money = dec(14, 2)"},
        {"line_no = 1 .. 4", "line_no = 1 .. 5"},
        {"(ngn, ghs, kes, zar, usd)", "(ngn, kes, ghs, zar, usd)"},
        {"(ngn, ghs, kes, zar, usd)", "(ngn, ghs, kes, zar, usd, eur)"},
        {"  ref: str(16)", "  ref: str(17)"},
        {"  state: status fresh .. posted", "  state: status"},
        {"  value_date: date8\n  fee_charge: fee", "  fee_charge: fee\n  value_date: date8"},
        {"eve payments", "eve payments2"},
        {"fee = money -999.99 .. 999.99", "fee = money -999.98 .. 999.99"},
    };
    static const char *const spell[][2] = {
        {"account_id = 0 .. 2**32 - 1", "account_id = 0 .. 4294967295"},
        {"line_no = 1 .. 4", "LINE_NO = 1 .. 2 * 2"},
        {"lines = [line_no] money", "lines = [line_no] Money   # same"},
        {"  from, to: account_id", "  from: account_id; to: account_id"},
        {"money = dec(15, 2)", "money = dec(15, 2) -9999999999999.99 .. 9999999999999.99"},
        {"date8 = 19000101 .. 29991231", "date8 = 19_000_101 .. 29_991_231"},
    };
    ze_pkg *base = parse(0, k_eve);
    char text[2048];
    for (size_t pass = 0; pass < 2; pass++) {
        size_t n = pass ? sizeof spell / sizeof spell[0] : sizeof differ / sizeof differ[0];
        for (size_t i = 0; i < n; i++) {
            const char *from = pass ? spell[i][0] : differ[i][0], *to = pass ? spell[i][1] : differ[i][1];
            const char *at = strstr(k_eve, from);
            size_t k = 0;
            ze_pkg *v;
            CHECK(at != NULL, "variant %zu: '%s' not in the text", i, from);
            if (!at) continue;
            k = append(text, sizeof text, k, "%.*s%s%s", (int) (at - k_eve), k_eve, to, at + strlen(from));
            (void) k;
            v = parse(0, text);
            CHECK(v && same(base, v) == (pass ? 1 : 0), "%s %zu: '%s' -> '%s'", pass ? "spelling" : "difference",
                  i, from, to);
            ze_pkg_destroy(v);
        }
    }
    ze_pkg_destroy(base);
}

static void test_forms(void) {
    ze_pkg *p = parse(0,
                      "eve forms\n"
                      "# comments and blank lines\n\n"
                      "small = -128 .. 127\n"
                      "rate = dec(9, 4) -1 .. 1\n"
                      "color = (red, green, blue)\n"
                      "warm = color red .. green\n"
                      "name = str(8)\n"
                      "grid = [color, 1 .. 3] small\n"
                      "row = [12] small\n"
                      "cell { hue: warm; qty: small 0 .. 10\n"
                      "  tag: name\n  hits: grid }\n"
                      "bools = [boolean] boolean\n");
    ze_type t;
    ze_dim d;
    CHECK(p != NULL, "forms parse");
    if (!p) return;
    CHECK(ze_type_at(p, type_of(p, "rate"), &t) == ZXV_OK && t.lo == -10000 && t.hi == 10000,
          "an integer bound of a decimal in Eve");
    CHECK(ze_array_dim(p, type_of(p, "row"), 0, &d) == ZXV_OK && d.index == UINT32_MAX && d.lo == 1 && d.hi == 12,
          "[12] is 1 .. 12");
    CHECK(ze_type_at(p, type_of(p, "cell"), &t) == ZXV_OK && t.count == 4 && t.size == 1 + 1 + 8 + 9, "cell");
    CHECK(strstr(canon(p), "row = [1 .. 12] small\n") != NULL, "canonical literal index:\n%s", canon(p));
    CHECK(strstr(canon(p), "  qty: small 0 .. 10\n") != NULL, "an anonymous constrained component");
    CHECK(strstr(as_ada(p), "Qty : Small range 0 .. 10;") != NULL, "printed as Ada:\n%s", as_ada(p));
    {
        ze_pkg *q = parse(1, as_ada(p));
        CHECK(same(p, q) == 1, "the printed Ada of every form re-parses:\n%s", as_ada(p));
        ze_pkg_destroy(q);
    }
    ze_pkg_destroy(p);
}

typedef struct {
    const char *text;
    int32_t status;
    uint32_t line, col;
} refusal;

static const refusal k_refusals[] = {
    {"payments\n", ZE_E_SYNTAX, 1, 1},
    {"eve p\nx = 5 .. 1\n", ZE_E_CONSTRAINT, 2, 1},
    {"eve p\nx = 1 .. 2\nX = 1 .. 3\n", ZE_E_NAME, 3, 1},
    {"eve p\nx = {\n", ZE_E_SYNTAX, 2, 5},
    {"eve p\nx = dec(19, 2)\n", ZE_E_CONSTRAINT, 2, 9},
    {"eve p\nx = dec(5, 2) 0 .. 0.005\n", ZE_E_CONSTRAINT, 2, 20},
    {"eve p\nx = str(0)\n", ZE_E_CONSTRAINT, 2, 5},
    {"eve p\nx = (a, b, A)\n", ZE_E_NAME, 2, 12},
    {"eve p\nr {\n  a: nothing\n}\n", ZE_E_NAME, 3, 6},
    {"eve p\nr {\n  a: boolean\n", ZE_E_SYNTAX, 4, 1},
    {"eve p\nx = 0 .. 2**63\n", ZE_E_CONSTRAINT, 2, 10},
    {"eve p\ninteger = 0 .. 9\n", ZE_E_NAME, 2, 1},
    {"eve p\nbegin = 0 .. 9\n", ZE_E_NAME, 2, 1},
    {"eve p\nx = [1 .. 2, 1 .. 2, 1 .. 2, 1 .. 2, 1 .. 2] boolean\n", ZE_E_LIMIT, 2, 0},
    {"eve p\nx = 0 .. 9 junk\n", ZE_E_SYNTAX, 2, 12},
    {"eve p\nx = 1.5 .. 2\n", ZE_E_CONSTRAINT, 2, 5},
};

static void test_refusals(void) {
    char text[512];
    for (size_t i = 0; i < sizeof k_refusals / sizeof k_refusals[0]; i++) {
        const refusal *r = &k_refusals[i];
        ze_diag d;
        size_t n;
        int32_t st;
        memset(&d, 0, sizeof d);
        st = refuse(0, r->text, &d);
        CHECK(st == r->status, "case %zu: %s, want %s: %s", i, ze_status_name(st), ze_status_name(r->status), d.msg);
        if (r->col) CHECK(d.line == r->line && d.col == r->col, "case %zu at %u:%u, want %u:%u: %s", i, d.line,
                          d.col, r->line, r->col, d.msg);
        else CHECK(d.line == r->line, "case %zu on line %u, want %u: %s", i, d.line, r->line, d.msg);
        n = append(text, sizeof text, 0, "line %u, column %u: ", d.line, d.col);
        CHECK(strncmp(d.msg, text, n) == 0, "case %zu: the message starts with its position: %s", i, d.msg);
    }
}

int main(void) {
    test_equivalence();
    test_differences();
    test_forms();
    test_refusals();
    return report("test_eve");
}
