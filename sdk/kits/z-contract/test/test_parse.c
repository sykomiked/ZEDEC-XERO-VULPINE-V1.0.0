/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_parse.c — the contract language: accepted texts and their canonical
 * form, refusals with their exact line, column and message, round trips,
 * digests and introspection. */
#include "zc_test.h"
#include <stdarg.h>
#include <stdlib.h>

/* Append to a test buffer; a test whose text would not fit is a bug in the
 * test, so stop rather than truncate. */
static size_t append(char *buf, size_t cap, size_t n, const char *fmt, ...) {
    if (n >= cap) abort();
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t) r >= cap - n) abort();
    return n + (size_t) r;
}

typedef struct {
    const char *text;
    const char *canonical;
} ok_case;

static const ok_case k_ok[] = {
    {"", ""},
    {"# only a comment\n", ""},
    {"fn f();", "fn f();\n"},
    {"fn fee(amount: rat[0..10^12] den<=100) -> rat den<=1000;",
     "fn fee(amount: rat[0..1000000000000] den<=100) -> rat den<=1000;\n"},
    {"fn f(a: int[-5..5], b: int[*..0], c: int[1..*], d: int[*..*]);",
     "fn f(a: int[-5..5], b: int[*..0], c: int[1..*], d: int);\n"},
    {"fn f(a: rat[-1.50..3/4], b: rat[0.1..2^10]);", "fn f(a: rat[-1.5..0.75], b: rat[0.1..1024]);\n"},
    {"fn f(a: rat[1/3..2/3] den|3);", "fn f(a: rat[1/3..2/3] den|3);\n"},
    {"fn f(t: truth{true,false}) -> truth;", "fn f(t: truth{true, false}) -> truth;\n"},
    {"fn f(x: tag{neither/neutral/emerging,true/*/manifest, */forbidden/*});",
     "fn f(x: tag{neither/neutral/emerging, true/*/manifest, */forbidden/*});\n"},
    {"fn f(b: bytes[1..12], c: bytes[0..*], d: bytes[4..4], e: bytes);",
     "fn f(b: bytes[1..12], c: bytes, d: bytes[4..4], e: bytes);\n"},
    {"fn order(order.price: rat[0..*], order.qty: int[1..*]);",
     "fn order(order.price: rat[0..*], order.qty: int[1..*]);\n"},
    {"fn fee(a: int) -> int[0..0] proposes rat[0..10^9] den|100 max 4;",
     "fn fee(a: int) -> int[0..0] proposes rat[0..1000000000] den|100 max 4;\n"},
    {"fn g() proposes int[1..*];", "fn g() proposes int[1..*] max 1;\n"},
    {"# fees\r\nfn a(x: int) ; # first\r\n\r\n  fn b(y: rat) -> tag;\n",
     "fn a(x: int);\nfn b(y: rat) -> tag;\n"},
    {"fn f(a: int[-9223372036854775807..9223372036854775807]);",
     "fn f(a: int[-9223372036854775807..9223372036854775807]);\n"},
    {"fn f(int: int, rat: rat, max: int) -> int;", "fn f(int: int, rat: rat, max: int) -> int;\n"},
    {"fn f(a: rat[-10^3..1.000]);", "fn f(a: rat[-1000..1]);\n"},
    {"fn f(a: rat[0..1/3]) # unicode in a comment: \xc3\xa9\xe2\x82\xac\n;", "fn f(a: rat[0..1/3]);\n"},
    {"fn f(a: int[0^0..1^63]);", "fn f(a: int[1..1]);\n"},
};

typedef struct {
    const char *text;
    uint32_t line, col;
    const char *msg; /* substring of the message */
} err_case;

static const err_case k_err[] = {
    {"fn", 1, 3, "expected a function name, found end of input"},
    {"fn f(", 1, 6, "expected a parameter name, found end of input"},
    {"fn f(a int);", 1, 8, "expected ':' after the parameter name, found 'int'"},
    {"fn f(a: float);", 1, 9, "unknown type 'float' (expected int, rat, truth, tag or bytes)"},
    {"fn f(a: int[5..3]);", 1, 12, "empty range: the lower bound is above the upper bound"},
    {"fn f(a: rat[1/2..1/3]);", 1, 12, "empty range"},
    {"fn f(a: int[1..2);", 1, 17, "expected ']' to close the range, found ')'"},
    {"fn f(a: int[1 2]);", 1, 15, "expected '..' in the range, found '2'"},
    {"fn f(a: int)\n  -> rat\n", 3, 1, "expected 'proposes' or ';', found end of input"},
    {"fn f(a: int);\nfn f();", 2, 4, "function 'f' is declared twice"},
    {"fn f(a: int, a: rat);", 1, 14, "parameter 'a' is declared twice"},
    {"fn f(a: int[0..99999999999999999999]);", 1, 16, "number does not fit in 64 bits"},
    {"fn f(a: int[0..2^63]);", 1, 16, "outside the exact range"},
    {"fn f(a: int[0..2^64]);", 1, 16, "power does not fit in 64 bits"},
    {"fn f(a: int[-9223372036854775808..0]);", 1, 13, "outside the exact range"},
    {"fn f(a: rat[1/0..2]);", 1, 15, "denominator is zero"},
    {"fn f(a: rat den<=0);", 1, 18, "denominator bound must be 1 .. 2^63 - 1"},
    {"fn f(a: rat den 5);", 1, 17, "expected '<=' or '|' after den, found '5'"},
    {"fn f(a: truth{true, maybe});", 1, 21, "expected neither, true, false or both, found 'maybe'"},
    {"fn f(a: truth{true, true});", 1, 21, "listed twice"},
    {"fn f(a: truth{});", 1, 15, "expected neither, true, false or both, found '}'"},
    {"fn f(a: tag{true/sideways/*});", 1, 18, "expected a polarity (entropic, neutral, forbidden, negentropic) or '*'"},
    {"fn f(a: tag{true/neutral});", 1, 25, "expected '/' between the parts of a tag, found '}'"},
    {"fn f(a: tag{*/*/*, */*/*});", 1, 20, "listed twice"},
    {"fn f() proposes truth;", 1, 17, "'proposes' takes int or rat"},
    {"fn f() proposes int max 65;", 1, 25, "max must be 1 .. 64"},
    {"fn f() proposes int max 0;", 1, 25, "max must be 1 .. 64"},
    {"fn f(a: int) @", 1, 14, "unexpected character '@'"},
    {"fn f(a: int) \xc3\xa9;", 1, 14, "unexpected byte 0xC3 (only ASCII outside comments)"},
    {"fn f();\n# \xff\n", 2, 3, "not valid UTF-8"},
    {"fn f();\n# \xed\xa0\x80 surrogate\n", 2, 3, "not valid UTF-8"},
    {"fn f();\n# \xc0\xaf overlong\n", 2, 3, "not valid UTF-8"},
    {"fn f(a.: int);", 1, 7, "expected ':' after the parameter name, found '.'"},
    {"fn f(a: rat[1. 5..2]);", 1, 14, "expected '..' in the range, found '.'"},
    {"fn f(a: int[1..2]) -> ;", 1, 23, "expected a type (int, rat, truth, tag or bytes), found ';'"},
    {"fn f()\nfn g();", 2, 1, "expected '->', 'proposes' or ';', found 'fn'"},
    {"fn f() -> int fn g();", 1, 15, "expected 'proposes' or ';', found 'fn'"},
    {"fn f() proposes int max 2 x", 1, 27, "expected ';' at the end of the declaration, found 'x'"},
    {"f();", 1, 1, "expected 'fn', found 'f'"},
    {"fn 1f();", 1, 4, "expected a function name, found '1'"},
    {"fn f(a: bytes[3..1]);", 1, 14, "empty range"},
    {"fn f(a: bytes[1..]);", 1, 18, "expected a number, found ']'"},
    {"fn f(a: rat[0.12345678901234567890..1]);", 1, 13, "too many digits"},
    {"fn f(a: int[\n\n   1..x]);", 3, 7, "expected a number, found 'x'"},
    {"fn a.b();", 1, 5, "expected '(' after the function name, found '.'"},
};

static void test_ok(void) {
    for (size_t i = 0; i < sizeof k_ok / sizeof k_ok[0]; i++) {
        const char *want = k_ok[i].canonical;
        zc_contract *c = parse_ok(k_ok[i].text);
        CHECK(c != NULL, "case %zu parses: %s", i, k_ok[i].text);
        if (!c) continue;
        CHECK(strcmp(canon(c), want) == 0, "case %zu canonical:\n  got  %s\n  want %s", i, canon(c),
              want);
        /* round trip: the canonical text parses to the same canonical text,
         * and the digests agree */
        char first[8192];
        snprintf(first, sizeof first, "%s", canon(c));
        zc_contract *c2 = parse_ok(first);
        CHECK(c2 && strcmp(canon(c2), first) == 0, "case %zu round trip", i);
        uint8_t d1[32], d2[32];
        CHECK(zc_contract_digest(c, d1) == ZXV_OK && c2 && zc_contract_digest(c2, d2) == ZXV_OK &&
                  memcmp(d1, d2, 32) == 0, "case %zu digest stable", i);
        zc_contract_destroy(c2);
        zc_contract_destroy(c);
    }
    /* negative zero and reduction in bounds */
    zc_contract *c = parse_ok("fn f(a: rat[-6/4..-0/7]);");
    CHECK(c && strcmp(canon(c), "fn f(a: rat[-1.5..0]);\n") == 0, "negative zero and reduction: %s",
          canon(c));
    zc_contract_destroy(c);
}

static void test_err(void) {
    for (size_t i = 0; i < sizeof k_err / sizeof k_err[0]; i++) {
        const err_case *e = &k_err[i];
        zc_contract *c = (zc_contract *) 1;
        uint32_t line = 0, col = 0;
        uint64_t off = 0;
        char msg[400];
        size_t mlen = 0;
        int32_t st = zc_parse(e->text, strlen(e->text), &c, &line, &col, &off, msg, sizeof msg, &mlen);
        CHECK(st == ZC_E_SYNTAX, "case %zu refused: %s (%s)", i, e->text, zc_status_name(st));
        CHECK(c == (zc_contract *) 1, "case %zu: no contract written", i);
        CHECK(line == e->line && col == e->col, "case %zu position: got %u:%u, want %u:%u (%s)", i,
              (unsigned) line, (unsigned) col, (unsigned) e->line, (unsigned) e->col, msg);
        CHECK(strstr(msg, e->msg) != NULL, "case %zu message: got \"%s\", want \"%s\"", i, msg, e->msg);
        CHECK(mlen == strlen(msg), "case %zu message length", i);
        /* the offset agrees with line and column */
        uint32_t l2 = 1;
        uint64_t ls = 0;
        for (uint64_t k = 0; k < off && k < strlen(e->text); k++)
            if (e->text[k] == '\n') {
                l2++;
                ls = k + 1u;
            }
        CHECK(l2 == line && off - ls + 1u == col, "case %zu offset %llu agrees with %u:%u", i,
              (unsigned long long) off, (unsigned) line, (unsigned) col);
        char want_prefix[64];
        snprintf(want_prefix, sizeof want_prefix, "line %u, column %u: ", (unsigned) line, (unsigned) col);
        CHECK(strncmp(msg, want_prefix, strlen(want_prefix)) == 0, "case %zu message prefix", i);
        CHECK(strcmp(last_err(), msg) == 0, "case %zu last error", i);
    }
}

static void test_limits(void) {
    zc_contract *c = NULL;
    char msg[16];
    size_t mlen = 0;
    uint32_t line = 0, col = 0;
    /* message truncation */
    int32_t st = zc_parse("fn", 2, &c, &line, &col, NULL, msg, sizeof msg, &mlen);
    CHECK(st == ZC_E_SYNTAX && strlen(msg) == sizeof msg - 1u && mlen > sizeof msg, "truncated message");
    CHECK(zc_parse("fn", 2, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZC_E_SYNTAX, "all outs NULL");
    CHECK(zc_parse(NULL, 0, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK && c, "NULL, 0 is empty");
    zc_contract_destroy(c);
    c = NULL;
    CHECK(zc_parse(NULL, 3, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_E_ARG, "NULL text");
    CHECK(zc_parse("fn f();", 7, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_E_ARG, "NULL out");
    /* not NUL-terminated: only len bytes are read */
    CHECK(zc_parse("fn f();garbage", 7, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK && c,
          "len bounds the text");
    zc_contract_destroy(c);

    /* a 64-byte name, 17 parameters, 257 functions, a 65-entry set */
    char big[40000];
    snprintf(big, sizeof big, "fn %s();", "a123456789012345678901234567890123456789012345678901234567890123");
    CHECK(zc_parse(big, strlen(big), &c, &line, &col, NULL, NULL, 0, NULL) == ZC_E_SYNTAX &&
              strstr(last_err(), "longer than 63 bytes") && col == 4, "64-byte name");
    snprintf(big, sizeof big, "fn %s();", "a12345678901234567890123456789012345678901234567890123456789012");
    CHECK(zc_parse(big, strlen(big), &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK, "63-byte name");
    zc_contract_destroy(c);
    strcpy(big, "fn f(a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.t.u.v.w.x.y.z.a.b.c.d.e.f.g: int);");
    CHECK(zc_parse(big, strlen(big), &c, NULL, NULL, NULL, NULL, 0, NULL) == ZC_E_SYNTAX &&
              strstr(last_err(), "longer than 63 bytes"), "65-byte dotted name");
    size_t n = append(big, sizeof big, 0, "fn f(");
    for (int i = 0; i < 17; i++) n = append(big, sizeof big, n, "%sp%d: int", i ? ", " : "", i);
    (void) append(big, sizeof big, n, ");");
    CHECK(zc_parse(big, strlen(big), &c, NULL, NULL, NULL, NULL, 0, NULL) == ZC_E_SYNTAX &&
              strstr(last_err(), "more than 16 parameters"), "17 parameters");
    n = 0;
    for (int i = 0; i < 256; i++) n = append(big, sizeof big, n, "fn f%d();\n", i);
    CHECK(zc_parse(big, n, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK, "256 functions");
    uint32_t count = 0;
    const char *name = NULL;
    CHECK(zc_fn_count(c, &count) == ZXV_OK && count == 256, "fn_count 256");
    CHECK(zc_fn_name(c, 255, &name) == ZXV_OK && strcmp(name, "f255") == 0, "fn_name 255");
    CHECK(zc_fn_name(c, 256, &name) == ZXV_E_NOT_FOUND, "fn_name 256");
    zc_contract_destroy(c);
    n = append(big, sizeof big, n, "fn g();\n");
    CHECK(zc_parse(big, n, &c, &line, &col, NULL, NULL, 0, NULL) == ZC_E_SYNTAX &&
              strstr(last_err(), "more than 256 functions") && line == 257 && col == 1, "257 functions");
    {
        static const char *const bn[4] = {"false", "neither", "both", "true"};
        static const char *const pn[4] = {"entropic", "neutral", "forbidden", "negentropic"};
        static const char *const hn[4] = {"potential", "emerging", "dissolving", "manifest"};
        n = append(big, sizeof big, 0, "fn f(t: tag{");
        for (int i = 0; i < 64; i++)
            n = append(big, sizeof big, n, "%s%s/%s/%s", i ? "," : "", bn[i % 4],
                                   pn[(i / 4) % 4], hn[i / 16]);
        (void) append(big, sizeof big, n, "});");
        CHECK(zc_parse(big, strlen(big), &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK, "64-entry set");
        zc_contract_destroy(c);
        (void) append(big, sizeof big, n, ",*/*/*});");
    }
    st = zc_parse(big, strlen(big), &c, NULL, NULL, NULL, NULL, 0, NULL);
    CHECK(st == ZC_E_SYNTAX && strstr(last_err(), "more than 64 entries"), "65-entry set refused");

    /* larger than ZC_MAX_TEXT */
    size_t huge = ZC_MAX_TEXT + 1u;
    char *h = (char *) malloc(huge);
    memset(h, ' ', huge);
    CHECK(zc_parse(h, huge, &c, &line, &col, NULL, NULL, 0, NULL) == ZC_E_SYNTAX &&
              strstr(last_err(), "larger than"), "text over 1 MiB");
    memset(h, '\n', ZC_MAX_TEXT);
    CHECK(zc_parse(h, ZC_MAX_TEXT, &c, &line, &col, NULL, NULL, 0, NULL) == ZXV_OK, "1 MiB of newlines");
    zc_contract_destroy(c);
    free(h);
}

static void test_introspection(void) {
    zc_contract *c = parse_ok(
        "fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0] proposes rat den|100 max 2;\n"
        "fn price(p: rat, side: truth{true, false}, raw: bytes[0..64]) -> tag;\n"
        "fn noop();\n");
    uint32_t np = 9, mx = 9;
    int32_t hr = 9, hp = 9, ty = 0;
    const char *nm = NULL;
    CHECK(c != NULL, "parse");
    CHECK(zc_fn_info(c, "fee", &np, &hr, &hp, &mx) == ZXV_OK && np == 2 && hr == 1 && hp == 1 && mx == 2,
          "fee info");
    CHECK(zc_fn_info(c, "noop", &np, &hr, &hp, &mx) == ZXV_OK && np == 0 && hr == 0 && hp == 0 && mx == 0,
          "noop info");
    CHECK(zc_fn_info(c, "nope", &np, NULL, NULL, NULL) == ZXV_E_NOT_FOUND, "unknown fn");
    CHECK(zc_slot_type(c, "price", ZC_PART_PARAMS, 1, &ty, &nm) == ZXV_OK && ty == ZC_T_TRUTH &&
              strcmp(nm, "side") == 0, "price param 1");
    CHECK(zc_slot_type(c, "price", ZC_PART_PARAMS, 2, &ty, &nm) == ZXV_OK && ty == ZC_T_BYTES, "bytes");
    CHECK(zc_slot_type(c, "price", ZC_PART_PARAMS, 3, &ty, &nm) == ZXV_E_NOT_FOUND, "param 3");
    CHECK(zc_slot_type(c, "price", ZC_PART_RESULT, 0, &ty, &nm) == ZXV_OK && ty == ZC_T_TAG &&
              strcmp(nm, "") == 0, "result tag");
    CHECK(zc_slot_type(c, "fee", ZC_PART_PROPOSALS, 0, &ty, NULL) == ZXV_OK && ty == ZC_T_RAT, "proposes");
    CHECK(zc_slot_type(c, "noop", ZC_PART_PROPOSALS, 0, &ty, NULL) == ZXV_E_NOT_FOUND, "no proposes");
    CHECK(zc_slot_type(c, "noop", 7, 0, &ty, NULL) == ZXV_E_ARG, "bad part");
    size_t need = 0;
    CHECK(zc_contract_format(c, NULL, 0, &need) == ZXV_E_BUFFER && need > 0, "R4 size query");
    char *b = (char *) malloc(need + 1u);
    size_t got = 0;
    CHECK(zc_contract_format(c, b, need, &got) == ZXV_E_BUFFER, "cap == len is too small");
    CHECK(zc_contract_format(c, b, need + 1u, &got) == ZXV_OK && got == need && b[need] == '\0', "R4");
    free(b);
    /* digests: whitespace and comments do not matter, a bound does */
    zc_contract *c2 = parse_ok("fn  fee( amount:int[0..1000000000000] ,bps:int[0..10^4] )->int[0..0]\n"
                               "  proposes rat den|100 max 2; # same\n"
                               "fn price(p: rat, side: truth{true,false}, raw: bytes[0..64]) -> tag;fn noop();");
    zc_contract *c3 = parse_ok("fn fee(amount: int[0..10^12], bps: int[0..10001]) -> int[0..0] proposes rat den|100 max 2;\n"
                               "fn price(p: rat, side: truth{true, false}, raw: bytes[0..64]) -> tag;\n"
                               "fn noop();\n");
    uint8_t d1[32], d2[32], d3[32];
    CHECK(c2 && c3 && zc_contract_digest(c, d1) == ZXV_OK && zc_contract_digest(c2, d2) == ZXV_OK &&
              zc_contract_digest(c3, d3) == ZXV_OK, "digests");
    CHECK(memcmp(d1, d2, 32) == 0, "same contract, same digest");
    CHECK(memcmp(d1, d3, 32) != 0, "another bound, another digest");
    zc_contract_destroy(c);
    zc_contract_destroy(c2);
    zc_contract_destroy(c3);
}

int main(void) {
    printf("test_parse: the z-contract language\n");
    test_ok();
    test_err();
    test_limits();
    test_introspection();
    printf("test_parse: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
