/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_wrappers.c — the wrappers zf-gen generated from examples/libc.zf
 * and examples/libm.zf, calling the real C library. This file holds no
 * float type: arguments and results are exact rationals and integers. */
#include <limits.h>

#include "libc_zf.h"
#include "libm_zf.h"
#include "zf_test.h"

static zxv_ctx *ctx;
static zxv_cap *root, *plain, *budget;

static void test_libc(void) {
    int64_t r = 0;
    uint64_t u = 0;
    CHECK(zf_libc_abs(plain, -5, &r) == ZXV_OK && r == 5, "abs(-5) = 5");
    CHECK(zf_libc_abs(plain, -INT_MAX, &r) == ZXV_OK && r == INT_MAX, "abs(-INT_MAX)");
    r = 77;
    CHECK(zf_libc_abs(plain, INT_MIN, &r) == ZF_E_RANGE && r == 77 &&
              err_has("libc.abs: argument 0 'x' = -2147483648 is below"),
          "abs(INT_MIN), undefined behaviour in C, is refused before the call: %s", last_err());
    CHECK(zf_libc_abs(plain, (int64_t) INT_MAX + 1, &r) == ZF_E_RANGE, "a value that does not fit int");
    CHECK(zf_libc_llabs(plain, -INT64_C(9000000000000000000), &r) == ZXV_OK && r == INT64_C(9000000000000000000),
          "llabs");
    CHECK(zf_libc_llabs(plain, INT64_MIN, &r) == ZF_E_RANGE, "llabs(LLONG_MIN) refused");
    CHECK(zf_libc_abs(NULL, -5, &r) == ZXV_E_CAP && err_has("libc.abs: no capability"), "no cap, no call");
    CHECK(zf_libc_abs(plain, -5, NULL) == ZXV_E_ARG, "NULL out");

    CHECK(zf_libc_strlen(plain, "hello", 5, &u) == ZXV_OK && u == 5, "strlen");
    CHECK(zf_libc_strlen(plain, "hello, world", 5, &u) == ZXV_OK && u == 5,
          "strlen sees only the bytes the caller gave (a copy, terminated)");
    CHECK(zf_libc_strlen(plain, "a\0bc", 4, &u) == ZF_E_RANGE && err_has("contains a NUL"),
          "a NUL inside the bytes is refused (C would see a shorter string)");
    CHECK(zf_libc_strlen(plain, NULL, 0, &u) == ZXV_OK && u == 0, "empty");
    {
        static char big[4097];
        memset(big, 'x', sizeof big);
        CHECK(zf_libc_strlen(plain, big, 4096, &u) == ZXV_OK && u == 4096, "4096 bytes");
        CHECK(zf_libc_strlen(plain, big, 4097, &u) == ZF_E_RANGE && err_has("outside 0..4096"), "4097 bytes");
    }
    CHECK(zf_libc_strncmp(plain, "abc", 3, "abd", 3, 2, &r) == ZXV_OK && r == 0, "strncmp first 2");
    CHECK(zf_libc_strncmp(plain, "abc", 3, "abd", 3, 3, &r) == ZXV_OK && r < 0, "strncmp 3");
    CHECK(zf_libc_strncmp(plain, "abc", 3, "abd", 3, 257, &r) == ZF_E_RANGE && err_has("argument 2 'n'"),
          "n above 256");
    CHECK(zf_libc_strncmp(plain, "abc", 3, "a\0", 2, 1, &r) == ZF_E_RANGE && err_has("argument 1 'b'"),
          "the second string is checked (and the first copy freed: run under ASan)");

    CHECK(zf_libc_toupper(plain, 'a', &r) == ZXV_OK && r == 'A', "toupper('a')");
    CHECK(zf_libc_toupper(plain, -1, &r) == ZXV_OK && r == -1, "toupper(EOF)");
    CHECK(zf_libc_toupper(plain, 300, &r) == ZF_E_RANGE && err_has("= 300 is above 255"),
          "toupper(300), undefined behaviour in C, is refused");
    CHECK(zf_libc_toupper(plain, -2, &r) == ZF_E_RANGE, "toupper(-2) refused");
    CHECK(strlen(ZF_LIBC_SPEC_SHA3) == 64u, "the header records the spec digest");
}

static void test_libm(void) {
    int64_t n = 0, d = 0, i = 0;
    CHECK(zf_libm_sqrt(plain, 9, 4, &n, &d) == ZXV_OK && n == 3 && d == 2, "sqrt(9/4) = 3/2");
    CHECK(zf_libm_sqrt(plain, 2, 1, &n, &d) == ZXV_OK && n == INT64_C(6369051672525773) &&
              d == INT64_C(4503599627370496), "sqrt(2) is the exact rational of the double it returns");
    CHECK(zf_libm_sqrt(plain, -1, 1, &n, &d) == ZXV_E_FLOAT && err_has("libm.sqrt: the result is NaN"),
          "sqrt(-1) is NaN: refused: %s", last_err());
    CHECK(zf_libm_sqrt(plain, 1, 10, &n, &d) == ZF_E_INEXACT && err_has("1/10 has no exact binary64"),
          "0.1 is not a double: refused, not rounded");
    CHECK(zf_libm_floor(plain, -5, 2, &n, &d) == ZXV_OK && n == -3 && d == 1, "floor(-5/2) = -3");
    CHECK(zf_libm_floor(plain, 7, 1, &n, &d) == ZXV_OK && n == 7 && d == 1, "floor(7)");
    CHECK(zf_libm_ilogb(plain, 1, 8, &i) == ZXV_OK && i == -3, "ilogb(1/8) = -3");
    CHECK(zf_libm_fabsf(plain, -3, 8, &n, &d) == ZXV_OK && n == 3 && d == 8, "fabsf(-3/8), a float, widened exactly");
    CHECK(zf_libm_fabsf(plain, 16777217, 1, &n, &d) == ZF_E_INEXACT && err_has("binary32"),
          "2^24 + 1 is not a float");

    CHECK(zf_libm_ldexp(plain, 3, 4, 4, &n, &d) == ZXV_E_CAP, "ldexp needs BUDGET; a cap without it is refused");
    CHECK(zf_libm_ldexp(budget, 3, 4, 4, &n, &d) == ZXV_OK && n == 12 && d == 1, "ldexp(3/4, 4) = 12");
    CHECK(zf_libm_ldexp(budget, 1, 1, 1024, &n, &d) == ZXV_E_FLOAT && err_has("an infinity"),
          "ldexp(1, 1024) overflows to infinity: refused");
    CHECK(zf_libm_ldexp(budget, 1, 1, -80, &n, &d) == ZXV_E_RANGE && err_has("no int64 rational form"),
          "ldexp(1, -80) = 2^-80 has no int64 rational form");
    CHECK(zf_libm_ldexp(budget, 1, 1, 1101, &n, &d) == ZF_E_RANGE, "the exponent's declared range");
    CHECK(zf_libm_ldexp(budget, 1, 1, 62, &n, &d) == ZXV_OK && n == INT64_C(4611686018427387904) && d == 1,
          "2^62");
    CHECK(zf_libm_sqrt(plain, 9, 4, NULL, &d) == ZXV_E_ARG, "NULL out");
}

int main(void) {
    if (zxv_ctx_create(&ctx) || zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) ||
        zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &plain) || zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget)) {
        fprintf(stderr, "test_wrappers: setup failed\n");
        return 1;
    }
    printf("test_wrappers: generated wrappers over libc and libm\n");
    test_libc();
    test_libm();
    zxv_cap_destroy(budget);
    zxv_cap_destroy(plain);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    printf("test_wrappers: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
