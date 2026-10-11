/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_gen.c — the spec parser and generator: accepted specs and the text
 * they produce, and refusals with their exact line, column and message. */
#include "zf_test.h"
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

static int gen(const char *spec, char **h, char **c, uint32_t *line, uint32_t *col, uint64_t *off, char *msg,
               size_t cap) {
    return zf_gen(spec, strlen(spec), "x_zf.h", h, NULL, c, NULL, line, col, off, msg, cap);
}

static const struct {
    const char *spec;
    uint32_t line, col;
    const char *msg;
} k_bad[] = {
    {"", 1, 1, "a spec starts with 'library NAME;'"},
    {"fn f() -> void;", 1, 1, "a spec starts with 'library NAME;'"},
    {"library m;", 1, 11, "the spec declares no fn"},
    {"library m; library n;", 1, 12, "'library' is given twice"},
    {"library M;", 1, 9, "a library name is lower-case letters, digits and '_'"},
    {"library m; fn f() -> double;", 1, 22, "unknown result type 'double'"},
    {"library m; fn f(x: f64) -> void;", 1, 20, "a parameter of type f64 must say 'from rat' (it is given as an exact rational)"},
    {"library m; fn f() -> f64;", 1, 22, "a result of type f64 is refused unless the spec says 'as rat' (exact conversion with zxv_rat_from_ieee754)"},
    {"library m; fn f() -> f64 as int;", 1, 22, "a result of type f64 is refused unless the spec says 'as rat' (exact conversion with zxv_rat_from_ieee754)"},
    {"library m; fn f() -> cstr;", 1, 22, "pointer results are not supported (z-ffi cannot know who owns them)"},
    {"library m; fn f(x: int[5..1]) -> void;", 1, 23, "the range is empty (lo > hi)"},
    {"library m; fn f(x: uint[-1..5]) -> void;", 1, 26, "a negative bound for the unsigned type uint"},
    {"library m; fn f(x: uint[-UINT_MAX..5]) -> void;", 1, 26, "a negative bound for the unsigned type uint"},
    {"library m; fn f(x: int[0..int_max]) -> void;", 1, 27, "a bound must be a number or an upper-case macro such as INT_MAX, found 'int_max'"},
    {"library m; fn f(x: int[0..9223372036854775808]) -> void;", 1, 27, "a bound beyond the int64 range (use INT64_MIN or INT64_MAX)"},
    {"library m; fn f(x: u64[0..18446744073709551616]) -> void;", 1, 27, "a number does not fit in 64 bits"},
    {"library m; fn f(x: int[007..9]) -> void;", 1, 24, "a number has a leading zero"},
    {"library m; fn f(x: int[1..2x]) -> void;", 1, 28, "a number is followed by a letter"},
    {"library m; fn f(x: int[1...2]) -> void;", 1, 27, "expected a bound (a number or a macro), found '.'"},
    {"library m; fn f(x: int, x: int) -> void;", 1, 25, "parameter 'x' is declared twice"},
    {"library m; fn f(x: cstr, x_len: int) -> void;", 1, 26, "parameter 'x_len' clashes with parameter 'x'"},
    {"library m; fn f(x_num: int, x: f64 from rat) -> void;", 1, 29, "parameter 'x' clashes with parameter 'x_num'"},
    {"library m; fn f(int: int) -> void;", 1, 17, "parameter 'int' is a C keyword or a type name"},
    {"library m; fn f(zf_x: int) -> void;", 1, 17, "parameter 'zf_x': names starting with zf_ are reserved for generated code"},
    {"library m; fn f(f: int) -> void;", 1, 17, "parameter 'f' would hide the function it calls"},
    {"library m; fn f() -> void; fn f() -> void;", 1, 31, "function 'f' is declared twice"},
    {"library m; fn f() -> void needs ROOT;", 1, 33, "unknown capability 'ROOT' (one of LEDGER_READ, LEDGER_POST, CRYPTO_SIGN, CRYPTO_KEM, AI_INFERENCE, BUDGET, PHASE, ATTEST, SWARM, EVM)"},
    {"library m; fn f() -> void needs BUDGET|BUDGET;", 1, 40, "capability 'BUDGET' is named twice"},
    {"library m; fn f() -> void needs;", 1, 32, "expected a capability name or 'none'"},
    {"library m; fn f() -> void", 1, 26, "expected ';' (or 'needs'), found end of input"},
    {"library m; fn f(a: int b: int) -> void;", 1, 24, "expected ',' or ')', found 'b'"},
    {"library m; fn f(a: int[1..2]) void;", 1, 31, "expected '->' and a result type, found 'void'"},
    {"library m; fn f(a: cstr[5..2]) -> void;", 1, 28, "the length range is empty (lo > hi)"},
    {"library m; fn f(a: cstr[0..4294967296]) -> void;", 1, 28, "a cstr length bound above 2^31-1"},
    {"library m; fn f(a: blob) -> void;", 1, 20, "unknown parameter type 'blob'"},
    {"library m; fn f() -> blob;", 1, 22, "unknown result type 'blob'"},
    {"library m; include <../etc/passwd>;", 1, 21, "a header path must be 1..120 bytes, relative, without '..'"},
    {"library m; include </abs.h>;", 1, 21, "a header path must be 1..120 bytes, relative, without '..'"},
    {"library m; include <a b.h>;", 1, 22, "a header path may hold letters, digits, '_', '.', '/', '-', '+'"},
    {"library m; include <a.h;", 1, 24, "a header path may hold letters, digits, '_', '.', '/', '-', '+'"},
    {"library m; include a.h;", 1, 20, "expected <header.h> or \"header.h\", found 'a'"},
    {"library m;\n\n  fn f(a: i9) -> void;", 3, 11, "unknown parameter type 'i9'"},
    {"library m; @", 1, 12, "expected 'library', 'include' or 'fn', found '@'"},
    {"library m; fn f(a,b) -> void;", 1, 18, "expected ':', found ','"},
    {"library m; fn f(a: i8, b: i8, c: i8, d: i8, e: i8, g: i8, h: i8, i: i8, j: i8) -> void;", 1, 73, "more than 8 parameters"},
    {"library m; fn", 1, 14, "expected a function name"},
    {"library m; fn 9f() -> void;", 1, 15, "expected a function name"},
    {"library m; fn f() -> void; library n;", 1, 28, "'library' is given twice"},
    {"library zf_m;", 1, 9, "a library name is at most 24 bytes and does not start with zf_"},
    {"library m; fn f() -> void; frob;", 1, 28, "expected 'library', 'include' or 'fn', found 'frob'"},
    {"library m; fn f(\xc3\xa9: int) -> void;", 1, 17, "expected a parameter name, found byte 0xc3"},
};

static void test_refusals(void) {
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        char *h = NULL, *c = NULL, msg[400], want[480];
        uint32_t line = 0, col = 0;
        uint64_t off = 0, ls = 0;
        const char *s = k_bad[i].spec;
        int32_t st = gen(s, &h, &c, &line, &col, &off, msg, sizeof msg);
        snprintf(want, sizeof want, "line %u, column %u: %s", (unsigned) k_bad[i].line, (unsigned) k_bad[i].col,
                 k_bad[i].msg);
        CHECK(st == ZF_E_SPEC && h == NULL && c == NULL, "case %zu refused", i);
        CHECK(strcmp(msg, want) == 0, "case %zu:\n  got  %s\n  want %s", i, msg, want);
        /* the offset agrees with line and column */
        uint32_t l = 1;
        for (uint64_t k = 0; k < off; k++)
            if (s[k] == '\n') {
                l++;
                ls = k + 1u;
            }
        CHECK(off <= strlen(s) && l == line && off - ls + 1u == col, "case %zu: offset %llu", i,
              (unsigned long long) off);
        CHECK(err_has(want), "case %zu: the last error carries the message", i);
    }
}

static const char k_libm[] =
    "# comment\n"
    "library libm;\n"
    "include <math.h>;\n"
    "fn sqrt(x: f64 from rat) -> f64 as rat;\n"
    "fn ldexp(x: f64 from rat, e: int[-1100..1100]) -> f64 as rat needs BUDGET | ATTEST;\n"
    "fn fabsf(x: f32 from rat) -> f32 as rat needs none;\n"
    "fn strlen(s: cstr[1..64]) -> size;\n"
    "fn noop() -> void;\n"
    "fn big(a: u64[0..18446744073709551615], b: i64[INT64_MIN..-1]) -> u8[1..UINT8_MAX];\n";

static void test_accepted(void) {
    char *h = NULL, *c = NULL, msg[400];
    size_t hl = 0, cl = 0;
    uint8_t dg[32];
    char hex[65];
    int32_t st = zf_gen(k_libm, strlen(k_libm), "libm_zf.h", &h, &hl, &c, &cl, NULL, NULL, NULL, msg, sizeof msg);
    CHECK(st == ZXV_OK && h && c && hl == strlen(h) && cl == strlen(c), "accepted (%s)", msg);
    if (st != ZXV_OK) return;
    zxv_sha3_256((const uint8_t *) k_libm, strlen(k_libm), dg);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);
    static const char *const in_h[] = {
        "#define ZF_LIBM_SPEC_SHA3 \"",
        ("int32_t zf_libm_sqrt(const zxv_cap *zf_cap, int64_t x_num, int64_t x_den, int64_t *zf_out_num, "
        "int64_t *zf_out_den);"),
        ("int32_t zf_libm_ldexp(const zxv_cap *zf_cap, int64_t x_num, int64_t x_den, int64_t e, int64_t *zf_out_num, "
        "int64_t *zf_out_den);"),
        "int32_t zf_libm_strlen(const zxv_cap *zf_cap, const char *s, size_t s_len, uint64_t *zf_out);",
        "int32_t zf_libm_noop(const zxv_cap *zf_cap);",
        "int32_t zf_libm_big(const zxv_cap *zf_cap, uint64_t a, int64_t b, uint64_t *zf_out);",
        "needs      BUDGET|ATTEST",
        "e          int in [(-INT64_C(1100)) .. INT64_C(1100)]",
        "s          const char *, 1..64 bytes, no NUL, copied",
        "-> result  size_t in [0 .. SIZE_MAX]",
        "#ifndef ZF_GEN_LIBM_H",
    };
    static const char *const in_c[] = {
        "#include <math.h>\n\n#include \"libm_zf.h\"",
        "_Static_assert(_Generic(&sqrt, double (*)(double): 1, default: 0),",
        "_Static_assert(_Generic(&ldexp, double (*)(double, int): 1, default: 0),",
        "_Static_assert(_Generic(&fabsf, float (*)(float): 1, default: 0),",
        "_Static_assert(_Generic(&strlen, size_t (*)(const char *): 1, default: 0),",
        "_Static_assert(_Generic(&noop, void (*)(void): 1, default: 0),",
        "_Static_assert(_Generic(&big, uint8_t (*)(uint64_t, int64_t): 1, default: 0),",
        "zf_st = zf_enter(zf_cap, (ZXV_CAP_BUDGET | ZXV_CAP_ATTEST), zf_fn);",
        "zf_st = zf_enter(zf_cap, UINT64_C(0), zf_fn);",
        "zf_st = zf_arg_i64(zf_fn, 1, \"e\", e, (-INT64_C(1100)), INT64_C(1100));",
        "zf_st = zf_arg_f32(zf_fn, 0, \"x\", x_num, x_den, &zf_b0);",
        "double zf_rd = (double) zf_r; /* exact: binary32 widens to binary64 */",
        "zf_st = zf_arg_cstr(zf_fn, 0, \"s\", s, s_len, (size_t) 1u, (size_t) 64u, &zf_s0);",
        "zf_cstr_free(zf_s0);",
        "zf_st = zf_res_u64(zf_fn, (uint64_t) zf_r, UINT64_C(0), (uint64_t) (SIZE_MAX), zf_out);",
        "zf_st = zf_arg_u64(zf_fn, 0, \"a\", a, UINT64_C(0), UINT64_C(18446744073709551615));",
        "zf_st = zf_arg_i64(zf_fn, 1, \"b\", b, (INT64_MIN), (-INT64_C(1)));",
        "zf_st = zf_res_u64(zf_fn, (uint64_t) zf_r, UINT64_C(1), (UINT8_MAX), zf_out);",
        ("_Static_assert((uintmax_t) (UINT64_C(1)) <= (uintmax_t) ((UINT8_MAX)) && (uintmax_t) ((UINT8_MAX)) <= "
        "(uintmax_t) (UINT8_MAX),"),
        "noop();\n        zf_st = ZXV_OK;",
        "zf_r = ldexp(zf_a0, (int) e);",
    };
    CHECK(strstr(h, hex) != NULL && strstr(c, hex) != NULL, "both files record the spec digest %s", hex);
    for (size_t i = 0; i < sizeof in_h / sizeof in_h[0]; i++) CHECK(strstr(h, in_h[i]) != NULL, "header has: %s", in_h[i]);
    for (size_t i = 0; i < sizeof in_c / sizeof in_c[0]; i++) CHECK(strstr(c, in_c[i]) != NULL, "source has: %s", in_c[i]);
    CHECK(strstr(c, "float") == NULL || strstr(c, "float zf_a0;") != NULL, "float appears only as a call-local value");

    char *h2 = NULL, *c2 = NULL;
    CHECK(zf_gen(k_libm, strlen(k_libm), "libm_zf.h", &h2, NULL, &c2, NULL, NULL, NULL, NULL, NULL, 0) == ZXV_OK &&
              strcmp(h, h2) == 0 && strcmp(c, c2) == 0, "deterministic");
    zf_gen_free(h2);
    zf_gen_free(c2);
    zf_gen_free(h);
    zf_gen_free(c);
}

static void test_limits_and_misuse(void) {
    char *h = NULL, *c = NULL, msg[400], small[16];
    uint32_t line = 0, col = 0;
    size_t cap = 300u * 40u + 64u;
    char *big = (char *) malloc(cap);
    size_t n = append(big, cap, 0, "library m;\n");
    for (int i = 0; i < 256; i++) n = append(big, cap, n, "fn g%d() -> void;\n", i);
    CHECK(zf_gen(big, n, "m.h", &h, NULL, &c, NULL, NULL, NULL, NULL, msg, sizeof msg) == ZXV_OK, "256 functions");
    zf_gen_free(h);
    zf_gen_free(c);
    n = append(big, cap, n, "fn g256() -> void;\n");
    CHECK(zf_gen(big, n, "m.h", &h, NULL, &c, NULL, &line, &col, NULL, msg, sizeof msg) == ZF_E_SPEC &&
              strstr(msg, "more than 256 functions") && line == 258u, "257 functions: %s", msg);
    free(big);
    CHECK(gen("library m; fn f(a: i8, b: i8, c: i8, d: i8, e: i8, g: i8, h: i8, i: i8) -> void;", &h, &c, NULL, NULL,
              NULL, msg, sizeof msg) == ZXV_OK, "8 parameters");
    zf_gen_free(h);
    zf_gen_free(c);
    CHECK(gen("library m; fn abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabc() -> void;", &h, &c,
              NULL, NULL, NULL, msg, sizeof msg) == ZXV_OK, "63-byte name");
    zf_gen_free(h);
    zf_gen_free(c);
    CHECK(gen("library m; fn abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcd() -> void;", &h, &c,
              NULL, NULL, NULL, msg, sizeof msg) == ZF_E_SPEC && strstr(msg, "longer than 63 bytes"), "64-byte name");
    {
        size_t L = ZF_MAX_SPEC + 1u;
        char *t = (char *) malloc(L);
        memset(t, ' ', L);
        CHECK(zf_gen(t, L, "m.h", &h, NULL, &c, NULL, NULL, NULL, NULL, msg, sizeof msg) == ZF_E_SPEC &&
                  strstr(msg, "longer than 1048576 bytes"), "over 1 MiB");
        free(t);
    }
    CHECK(gen("library M;", &h, &c, NULL, NULL, NULL, small, sizeof small) == ZF_E_SPEC && strlen(small) == 15u &&
              strncmp(small, "line 1, column ", 15) == 0, "a truncated message is a NUL-terminated prefix");
    CHECK(zf_gen("library m; fn f() -> void;", 26, "m.h", NULL, NULL, &c, NULL, NULL, NULL, NULL, NULL, 0) == ZXV_E_ARG &&
              zf_gen(NULL, 3, "m.h", &h, NULL, &c, NULL, NULL, NULL, NULL, NULL, 0) == ZXV_E_ARG &&
              zf_gen("x", 1, "", &h, NULL, &c, NULL, NULL, NULL, NULL, NULL, 0) == ZXV_E_ARG &&
              zf_gen("x", 1, "a\"b.h", &h, NULL, &c, NULL, NULL, NULL, NULL, NULL, 0) == ZXV_E_ARG,
          "API misuse and a header name that would break the #include");
    /* a spec is not NUL-terminated: len bounds it */
    CHECK(zf_gen("library m; fn f() -> void;GARBAGE", 26, "m.h", &h, NULL, &c, NULL, NULL, NULL, NULL, NULL, 0) ==
              ZXV_OK, "len bounds the spec");
    zf_gen_free(h);
    zf_gen_free(c);
}

int main(void) {
    printf("test_gen: the z-ffi spec parser and wrapper generator\n");
    test_refusals();
    test_accepted();
    test_limits_and_misuse();
    printf("test_gen: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
