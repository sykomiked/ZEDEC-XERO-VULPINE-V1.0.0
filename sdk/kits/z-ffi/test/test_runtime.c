/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_runtime.c — the integer-only runtime: exact rational <-> IEEE-754
 * bits, capability entry, argument and result checks. No float type here:
 * every expected encoding is written out as bits. */
#include "zf_test.h"

static void test_f64_bits(void) {
    static const struct {
        int64_t num, den;
        int32_t st;
        uint64_t bits;
    } k[] = {
        {0, 1, ZXV_OK, UINT64_C(0)},
        {0, -7, ZXV_OK, UINT64_C(0)},
        {1, 1, ZXV_OK, UINT64_C(0x3FF0000000000000)},
        {-1, 1, ZXV_OK, UINT64_C(0xBFF0000000000000)},
        {1, 2, ZXV_OK, UINT64_C(0x3FE0000000000000)},
        {3, 4, ZXV_OK, UINT64_C(0x3FE8000000000000)},
        {-5, 2, ZXV_OK, UINT64_C(0xC004000000000000)},
        {6, -8, ZXV_OK, UINT64_C(0xBFE8000000000000)},   /* reduced: -3/4 */
        {12, 1, ZXV_OK, UINT64_C(0x4028000000000000)},
        {INT64_C(9007199254740992), 1, ZXV_OK, UINT64_C(0x4340000000000000)},  /* 2^53 */
        {INT64_C(9007199254740991), 1, ZXV_OK, UINT64_C(0x433FFFFFFFFFFFFF)},  /* 2^53-1 */
        {INT64_C(4611686018427387904), 1, ZXV_OK, UINT64_C(0x43D0000000000000)}, /* 2^62 */
        {1, INT64_C(4611686018427387904), ZXV_OK, UINT64_C(0x3C10000000000000)}, /* 2^-62 */
        {INT64_C(9223372036854774784), 1, ZXV_OK, UINT64_C(0x43DFFFFFFFFFFFFF)}, /* largest < 2^63 */
        {INT64_C(9007199254740993), 1, ZF_E_INEXACT, 0},  /* 2^53 + 1 */
        {INT64_MAX, 1, ZF_E_INEXACT, 0},
        {1, 10, ZF_E_INEXACT, 0},
        {1, 3, ZF_E_INEXACT, 0},
        {INT64_C(6004799503160661), INT64_C(4503599627370496) * 4, ZXV_OK, UINT64_C(0x3FD5555555555555)},
        {1, 0, ZXV_E_ARG, 0},
        {INT64_MIN, 1, ZXV_E_RANGE, 0},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint64_t b = 0xAAAAu;
        int32_t st = zf_rat_to_f64_bits(k[i].num, k[i].den, &b);
        CHECK(st == k[i].st, "f64 %lld/%lld: status %s", (long long) k[i].num, (long long) k[i].den,
              zf_status_name(st));
        if (st == ZXV_OK && k[i].st == ZXV_OK)
            CHECK(b == k[i].bits, "f64 %lld/%lld: bits 0x%016llx, want 0x%016llx", (long long) k[i].num,
                  (long long) k[i].den, (unsigned long long) b, (unsigned long long) k[i].bits);
    }
    CHECK(zf_rat_to_f64_bits(1, 1, NULL) == ZXV_E_ARG, "NULL out");
}

static void test_f32_bits(void) {
    static const struct {
        int64_t num, den;
        int32_t st;
        uint32_t bits;
    } k[] = {
        {0, 1, ZXV_OK, 0u},
        {1, 1, ZXV_OK, 0x3F800000u},
        {1, 2, ZXV_OK, 0x3F000000u},
        {-5, 2, ZXV_OK, 0xC0200000u},
        {-3, 8, ZXV_OK, 0xBEC00000u},
        {16777216, 1, ZXV_OK, 0x4B800000u},  /* 2^24 */
        {16777215, 1, ZXV_OK, 0x4B7FFFFFu},  /* 2^24 - 1 */
        {16777217, 1, ZF_E_INEXACT, 0u},     /* 2^24 + 1 */
        {INT64_C(4611686018427387904), 1, ZXV_OK, 0x5E800000u}, /* 2^62 */
        {1, 10, ZF_E_INEXACT, 0u},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint32_t b = 0xAAAAu;
        int32_t st = zf_rat_to_f32_bits(k[i].num, k[i].den, &b);
        CHECK(st == k[i].st && (st != ZXV_OK || b == k[i].bits), "f32 %lld/%lld: %s 0x%08x",
              (long long) k[i].num, (long long) k[i].den, zf_status_name(st), (unsigned) b);
    }
}

/* Every exactly representable rational round-trips through
 * zxv_rat_from_ieee754, and the inexact neighbours are refused. */
static void test_round_trip(void) {
    uint64_t rng = 0x243F6A8885A308D3ull;
    int bad = 0, n = 0, inexact_ok = 0;
    for (int i = 0; i < 200000; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        unsigned bitsm = 1u + (unsigned) (rng % 53u);
        uint64_t m = (rng >> 8) & ((UINT64_C(1) << bitsm) - 1u);
        m |= UINT64_C(1) << (bitsm - 1u); /* exactly bitsm bits */
        unsigned k = (unsigned) ((rng >> 40) % 63u);
        int64_t num = (int64_t) m * ((rng >> 62) & 1u ? -1 : 1);
        int64_t den = (int64_t) (UINT64_C(1) << k);
        uint64_t b = 0;
        int64_t rn = 0, rd = 0, nn = 0, nd = 0;
        if (zf_rat_to_f64_bits(num, den, &b) != ZXV_OK) {
            bad++;
            continue;
        }
        if (zxv_rat_from_ieee754(b, &rn, &rd) != ZXV_OK || zxv_rat_normalize(num, den, &nn, &nd) != ZXV_OK ||
            rn != nn || rd != nd)
            bad++;
        n++;
        /* m * 2 + 1 has bitsm + 1 bits: exact up to 53 bits only */
        if (bitsm < 63u) {
            int64_t wide = (int64_t) (m * 2u + 1u);
            int32_t st = zf_rat_to_f64_bits(wide, den, &b);
            if ((bitsm + 1u <= 53u) != (st == ZXV_OK)) bad++;
            else inexact_ok++;
        }
    }
    CHECK(bad == 0 && n == 200000 && inexact_ok == 200000,
          "200000 random exact values round-trip and their widened neighbours are judged right (%d bad)", bad);
}

static void test_enter(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *budget = NULL, *other = NULL;
    CHECK(zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &other) == ZXV_OK, "caps");
    CHECK(zf_enter(NULL, 0, "t.f") == ZXV_E_CAP && err_has("t.f: no capability"), "a call needs a cap");
    CHECK(zf_enter(budget, 0, "t.f") == ZXV_OK, "needs nothing: any live attested cap");
    CHECK(zf_enter(budget, ZXV_CAP_BUDGET, "t.f") == ZXV_OK, "needs BUDGET: a BUDGET cap");
    CHECK(zf_enter(other, ZXV_CAP_BUDGET, "t.f") == ZXV_E_CAP && err_has("needs 0x20"), "a cap without the bit");
    CHECK(zxv_cap_set_attestation(budget, ZXV_LP_NEITHER) == ZXV_OK &&
              zf_enter(budget, 0, "t.f") == ZXV_E_CAP, "an unattested cap");
    CHECK(zxv_cap_set_attestation(budget, ZXV_LP_TRUE) == ZXV_OK && zxv_cap_revoke(root) == ZXV_OK &&
              zf_enter(budget, 0, "t.f") == ZXV_E_CAP, "a revoked ancestor");
    zxv_cap_destroy(other);
    zxv_cap_destroy(budget);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

static void test_args(void) {
    uint64_t b64 = 0;
    uint32_t b32 = 0;
    char *c = NULL;
    int64_t i = 0, n = 0, d = 0;
    uint64_t u = 0;
    CHECK(zf_arg_i64("t.f", 0, "x", -5, -5, 5) == ZXV_OK && zf_arg_i64("t.f", 0, "x", 5, -5, 5) == ZXV_OK,
          "inclusive bounds");
    CHECK(zf_arg_i64("t.f", 2, "x", -6, -5, 5) == ZF_E_RANGE && err_has("t.f: argument 2 'x' = -6 is below -5"),
          "below: %s", last_err());
    CHECK(zf_arg_i64("t.f", 0, "x", INT64_MAX, INT64_MIN, 0) == ZF_E_RANGE && err_has("is above 0"), "above");
    CHECK(zf_arg_u64("t.f", 1, "n", UINT64_MAX, 0, UINT64_MAX) == ZXV_OK &&
              zf_arg_u64("t.f", 1, "n", 257, 0, 256) == ZF_E_RANGE && err_has("'n' = 257 is above 256"),
          "unsigned");
    CHECK(zf_arg_f64("t.f", 0, "x", 1, 10, &b64) == ZF_E_INEXACT &&
              err_has("'x' = 1/10 has no exact binary64 (double) value (it would be rounded)"),
          "1/10 is refused as a double argument: %s", last_err());
    CHECK(zf_arg_f32("t.f", 0, "x", 16777217, 1, &b32) == ZF_E_INEXACT && err_has("binary32"), "f32 inexact");
    CHECK(zf_arg_f64("t.f", 0, "x", 1, 0, &b64) == ZXV_E_ARG && err_has("not a valid rational"), "den 0");
    CHECK(zf_arg_f64("t.f", 0, "x", 1, 1, NULL) == ZXV_E_ARG, "NULL bits");
    CHECK(zf_arg_cstr("t.f", 0, "s", "abc", 3, 0, 8, &c) == ZXV_OK && c && strcmp(c, "abc") == 0, "copy");
    zf_cstr_free(c);
    CHECK(zf_arg_cstr("t.f", 0, "s", "abcdef", 3, 0, 8, &c) == ZXV_OK && strcmp(c, "abc") == 0,
          "only len bytes are read and the copy is terminated");
    zf_cstr_free(c);
    CHECK(zf_arg_cstr("t.f", 0, "s", "a\0b", 3, 0, 8, &c) == ZF_E_RANGE && c == NULL && err_has("contains a NUL"),
          "embedded NUL");
    CHECK(zf_arg_cstr("t.f", 0, "s", "abc", 3, 4, 8, &c) == ZF_E_RANGE && err_has("has 3 bytes, outside 4..8"),
          "too short");
    CHECK(zf_arg_cstr("t.f", 0, "s", NULL, 0, 0, 8, &c) == ZXV_OK && c && c[0] == 0, "empty, NULL pointer");
    zf_cstr_free(c);
    CHECK(zf_arg_cstr("t.f", 0, "s", NULL, 2, 0, 8, &c) == ZXV_E_ARG, "NULL with a length");
    CHECK(zf_res_i64("t.f", 7, 0, 10, &i) == ZXV_OK && i == 7, "result in range");
    CHECK(zf_res_i64("t.f", -1, 0, 10, &i) == ZF_E_RESULT && err_has("t.f: result -1 is outside 0..10"),
          "result below");
    CHECK(zf_res_u64("t.f", 11, 0, 10, &u) == ZF_E_RESULT && zf_res_u64("t.f", 1, 0, 10, NULL) == ZXV_E_ARG,
          "unsigned result");
    CHECK(zf_res_f64("t.f", UINT64_C(0x3FF8000000000000), &n, &d) == ZXV_OK && n == 3 && d == 2, "1.5");
    CHECK(zf_res_f64("t.f", UINT64_C(0x3FB999999999999A), &n, &d) == ZXV_OK && n == INT64_C(3602879701896397) &&
              d == INT64_C(36028797018963968), "the double nearest 0.1 is reported exactly, not as 1/10");
    CHECK(zf_res_f64("t.f", UINT64_C(0x8000000000000000), &n, &d) == ZXV_OK && n == 0 && d == 1, "-0 is 0");
    CHECK(zf_res_f64("t.f", UINT64_C(0x7FF8000000000000), &n, &d) == ZXV_E_FLOAT && err_has("NaN"), "NaN");
    CHECK(zf_res_f64("t.f", UINT64_C(0xFFF0000000000000), &n, &d) == ZXV_E_FLOAT && err_has("an infinity"),
          "-inf");
    CHECK(zf_res_f64("t.f", UINT64_C(0x3AF0000000000000), &n, &d) == ZXV_E_RANGE &&
              err_has("no int64 rational form"), "2^-80 has no int64 form");
    CHECK(zf_res_f64("t.f", UINT64_C(0x3FF0000000000000), NULL, &d) == ZXV_E_ARG && err_has("NULL output"),
          "NULL out");
}

static void test_names(void) {
    size_t n = 0;
    char small[4];
    CHECK(strcmp(zf_status_name(ZF_E_INEXACT), "ZF_E_INEXACT") == 0 &&
              strcmp(zf_status_name(ZF_E_SPEC), "ZF_E_SPEC") == 0 &&
              strcmp(zf_status_name(ZXV_E_FLOAT), "ZXV_E_FLOAT") == 0, "status names");
    (void) zf_arg_i64("t.f", 0, "x", 9, 0, 1);
    CHECK(zf_last_error(small, sizeof small, &n) == ZXV_E_BUFFER && n > 4u && zf_last_error(NULL, 0, NULL) == ZXV_E_ARG,
          "R4 last error");
}

int main(void) {
    printf("test_runtime: exact rational <-> IEEE-754 bits, caps, argument and result checks\n");
    test_f64_bits();
    test_f32_bits();
    test_round_trip();
    test_enter();
    test_args();
    test_names();
    printf("test_runtime: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
