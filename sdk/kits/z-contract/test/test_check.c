/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_check.c — values, the runtime checker (every ZC_V_* reason, exact
 * bounds), the 64-state tag (against libzxv's zxv_hex_*), and the evidence log (hash chain,
 * the documented record hash, tampering, full log, sink). */
#include "zc_internal.h"
#include "zc_test.h"

static zc_contract *C;

/* Check `part` of `fn` with the values pushed by the caller. */
static int32_t run(const char *fn, int32_t part, zc_vals *v, int32_t *reason, uint32_t *index,
                   char *msg, size_t cap) {
    return zc_check(C, fn, part, v, reason, index, msg, cap, NULL);
}

#define EXPECT(fn, part, want_reason, want_index, msg_sub)                                     \
    do {                                                                                       \
        int32_t r_ = -1;                                                                       \
        uint32_t i_ = 99;                                                                      \
        char m_[400];                                                                          \
        int32_t st_ = run(fn, part, v, &r_, &i_, m_, sizeof m_);                               \
        CHECK(st_ == ((want_reason) ? ZC_E_VIOLATION : ZXV_OK) && r_ == (want_reason) &&       \
                  i_ == (want_index) && strstr(m_, msg_sub) != NULL,                           \
              "%s part %d: got %s reason %s index %u msg \"%s\"; want %s index %u \"%s\"", fn, \
              part, zc_status_name(st_), zc_reason_name(r_), (unsigned) i_, m_,                \
              zc_reason_name(want_reason), (unsigned) (want_index), msg_sub);                  \
        zc_vals_clear(v);                                                                      \
    } while (0)

static void test_vals(void) {
    zc_vals *v = NULL;
    uint32_t n = 9;
    int32_t kind = 0;
    int64_t num = 0, den = 0;
    CHECK(zc_vals_create(&v) == ZXV_OK && v, "create");
    CHECK(zc_vals_push_int(v, -7) == ZXV_OK && zc_vals_push_rat(v, 3, 4) == ZXV_OK &&
              zc_vals_push_truth(v, ZXV_LP_BOTH) == ZXV_OK && zc_vals_push_tag(v, 21) == ZXV_OK &&
              zc_vals_push_bytes(v, (const uint8_t *) "abc", 3) == ZXV_OK &&
              zc_vals_push_bytes(v, NULL, 0) == ZXV_OK, "push each kind");
    CHECK(zc_vals_count(v, &n) == ZXV_OK && n == 6, "count 6");
    CHECK(zc_vals_get(v, 1, &kind, &num, &den) == ZXV_OK && kind == ZC_T_RAT && num == 3 && den == 4,
          "get rat");
    CHECK(zc_vals_get(v, 4, &kind, &num, &den) == ZXV_OK && kind == ZC_T_BYTES && num == 3 && den == 1,
          "get bytes = length");
    CHECK(zc_vals_get(v, 6, &kind, NULL, NULL) == ZXV_E_NOT_FOUND, "get past the end");
    CHECK(zc_vals_push_bytes(v, NULL, 4) == ZXV_E_ARG, "NULL bytes with a length");
    uint8_t *big = (uint8_t *) calloc(1, ZC_MAX_BYTES + 1u);
    CHECK(zc_vals_push_bytes(v, big, ZC_MAX_BYTES + 1u) == ZXV_E_TOO_LONG, "bytes over 1 MiB");
    CHECK(zc_vals_push_bytes(v, big, ZC_MAX_BYTES) == ZXV_OK, "1 MiB of bytes");
    free(big);
    CHECK(zc_vals_clear(v) == ZXV_OK && zc_vals_count(v, &n) == ZXV_OK && n == 0, "clear");
    for (uint32_t i = 0; i < ZC_MAX_VALUES; i++) (void) zc_vals_push_int(v, i);
    CHECK(zc_vals_push_int(v, 0) == ZXV_E_CAPACITY, "65th value");
    CHECK(zc_vals_push_int(NULL, 0) == ZXV_E_ARG && zc_vals_count(NULL, &n) == ZXV_E_ARG, "NULL vals");
    zc_vals_destroy(v);
    zc_vals_destroy(NULL);
}

static void test_reasons(void) {
    zc_vals *v = NULL;
    C = parse_ok("fn f(a: int[-5..5], b: rat[0..1] den<=100, c: rat[0..*] den|100,\n"
                 "     t: truth{true, false}, g: tag{true/*/manifest, neither/neutral/emerging},\n"
                 "     s: bytes[1..4]) -> truth proposes rat[0..10] den|4 max 2;\n"
                 "fn none();\n"
                 "fn tiny(x: rat[0..1/10]);\n"
                 "fn wide(x: int, y: rat, z: tag, w: truth, q: bytes);\n");
    CHECK(C != NULL, "contract parses: %s", last_err());
    zc_vals_create(&v);
    int32_t tg_ok = 0, tg_bad = 0, tg_n = 0;
    zc_tag_parse("true/negentropic/manifest", &tg_ok);
    zc_tag_parse("true/neutral/emerging", &tg_bad);
    zc_tag_parse("neither/neutral/emerging", &tg_n);
#define PUSH_GOOD()                                                                            \
    do {                                                                                       \
        zc_vals_push_int(v, 5);                                                                \
        zc_vals_push_rat(v, 1, 100);                                                           \
        zc_vals_push_rat(v, 7, 4);                                                             \
        zc_vals_push_truth(v, ZXV_LP_FALSE);                                                   \
        zc_vals_push_tag(v, tg_ok);                                                            \
        zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);                                      \
    } while (0)

    PUSH_GOOD();
    EXPECT("f", ZC_PART_PARAMS, ZC_V_OK, 0, "");
    /* inclusive bounds, both ends */
    zc_vals_push_int(v, -5);
    zc_vals_push_rat(v, 0, 1);
    zc_vals_push_rat(v, 0, 5);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_n);
    zc_vals_push_bytes(v, (const uint8_t *) "abcd", 4);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_OK, 0, "");
    /* int slot takes an integral rational; unreduced input is reduced first */
    zc_vals_push_rat(v, -10, 2);
    zc_vals_push_rat(v, 2, 2);
    zc_vals_push_int(v, 3);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "a", 1);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_OK, 0, "");

    zc_vals_push_int(v, 6);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_ARITY, 0, "f: 1 values for 6 parameters");
    EXPECT("f", ZC_PART_PARAMS, ZC_V_ARITY, 0, "0 values for 6");

    zc_vals_push_int(v, 6);
    zc_vals_push_rat(v, 1, 100);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_ABOVE, 0, "f: parameter 0 'a' = 6 is above the maximum 5");

    zc_vals_push_int(v, -6);
    zc_vals_push_rat(v, 1, 100);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_BELOW, 0, "= -6 is below the minimum -5");

    zc_vals_push_rat(v, 9, 2);
    EXPECT("none", ZC_PART_PARAMS, ZC_V_ARITY, 0, "1 values for 0 parameters");

    zc_vals_push_rat(v, 9, 2);
    zc_vals_push_rat(v, 1, 100);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_NOT_INTEGER, 0, "= 4.5 (9/2) is not an integer");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 101);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_DENOMINATOR, 1, "parameter 1 'b' = 1/101: denominator 101 is above 100");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 101, 100);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_ABOVE, 1, "= 1.01 (101/100) is above the maximum 1");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, -1, 100);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_BELOW, 1, "= -0.01 (-1/100) is below the minimum 0");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 1, 40);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_DENOMINATOR, 2, "= 0.025 (1/40): denominator 40 does not divide 100");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 2, 50); /* 1/25: 25 divides 100 */
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_OK, 0, "");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 0);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_INVALID, 1, "is not an exact rational");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, INT64_MIN, 3);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_FALSE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_INVALID, 1, "INT64_MIN");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_NEITHER);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_NOT_IN_SET, 3, "parameter 3 't' = NEITHER is not in truth{true, false}");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, 4);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_INVALID, 3, "= 4 is not a truth value");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_bad);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_NOT_IN_SET, 4,
           "= true/neutral/emerging is not in tag{true/*/manifest, neither/neutral/emerging}");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, 64);
    zc_vals_push_bytes(v, (const uint8_t *) "ab", 2);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_INVALID, 4, "= 64 is not a tag value");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "abcde", 5);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_LENGTH, 5, "parameter 5 's' has 5 bytes, outside bytes[1..4]");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, NULL, 0);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_LENGTH, 5, "has 0 bytes");

    zc_vals_push_bytes(v, (const uint8_t *) "1", 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_truth(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "a", 1);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_KIND, 0, "parameter 0 'a' is of kind bytes, expected int");

    zc_vals_push_int(v, 1);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_rat(v, 7, 4);
    zc_vals_push_int(v, ZXV_LP_TRUE);
    zc_vals_push_tag(v, tg_ok);
    zc_vals_push_bytes(v, (const uint8_t *) "a", 1);
    EXPECT("f", ZC_PART_PARAMS, ZC_V_KIND, 3, "is of kind int, expected truth");

    zc_vals_push_int(v, 1);
    zc_vals_push_int(v, 2); /* an int is a rational */
    zc_vals_push_tag(v, 1);
    zc_vals_push_tag(v, 1);
    zc_vals_push_bytes(v, (const uint8_t *) "q", 1);
    EXPECT("wide", ZC_PART_PARAMS, ZC_V_KIND, 3, "is of kind tag, expected truth");

    /* exactness: the double nearest 0.1 is NOT 1/10, and is above it */
    int64_t n01 = 0, d01 = 0;
    CHECK(zxv_rat_from_ieee754(UINT64_C(0x3FB999999999999A), &n01, &d01) == ZXV_OK, "0.1 bits");
    zc_vals_push_rat(v, n01, d01);
    EXPECT("tiny", ZC_PART_PARAMS, ZC_V_ABOVE, 0, "= 3602879701896397/36028797018963968 is above the maximum 0.1");
    zc_vals_push_rat(v, 1, 10);
    EXPECT("tiny", ZC_PART_PARAMS, ZC_V_OK, 0, "");
    zc_vals_push_rat(v, INT64_C(922337203685477580), INT64_C(9223372036854775807));
    EXPECT("tiny", ZC_PART_PARAMS, ZC_V_OK, 0, ""); /* just below 1/10 */
    zc_vals_push_rat(v, INT64_C(922337203685477581), INT64_C(9223372036854775807));
    EXPECT("tiny", ZC_PART_PARAMS, ZC_V_ABOVE, 0, "above the maximum 0.1"); /* just above */

    /* the result */
    zc_vals_push_truth(v, ZXV_LP_BOTH);
    EXPECT("f", ZC_PART_RESULT, ZC_V_OK, 0, "");
    EXPECT("f", ZC_PART_RESULT, ZC_V_ARITY, 0, "f: the declared result is missing");
    zc_vals_push_truth(v, 1);
    zc_vals_push_truth(v, 1);
    EXPECT("f", ZC_PART_RESULT, ZC_V_ARITY, 0, "more than one result");
    zc_vals_push_int(v, 0);
    EXPECT("none", ZC_PART_RESULT, ZC_V_ARITY, 0, "none: a result was returned but none is declared");
    EXPECT("none", ZC_PART_RESULT, ZC_V_OK, 0, "");
    zc_vals_push_int(v, 1);
    EXPECT("f", ZC_PART_RESULT, ZC_V_KIND, 0, "f: result is of kind int, expected truth");

    /* proposals */
    EXPECT("f", ZC_PART_PROPOSALS, ZC_V_OK, 0, "");
    zc_vals_push_rat(v, 10, 1);
    zc_vals_push_rat(v, 3, 4);
    EXPECT("f", ZC_PART_PROPOSALS, ZC_V_OK, 0, "");
    zc_vals_push_rat(v, 1, 4);
    zc_vals_push_rat(v, 1, 8);
    EXPECT("f", ZC_PART_PROPOSALS, ZC_V_DENOMINATOR, 1, "f: proposal 1 = 0.125 (1/8): denominator 8 does not divide 4");
    zc_vals_push_rat(v, 1, 4);
    zc_vals_push_rat(v, 1, 4);
    zc_vals_push_rat(v, 1, 4);
    EXPECT("f", ZC_PART_PROPOSALS, ZC_V_COUNT, 2, "f: 3 proposals, at most 2 allowed");
    zc_vals_push_rat(v, 41, 4);
    EXPECT("f", ZC_PART_PROPOSALS, ZC_V_ABOVE, 0, "proposal 0 = 10.25 (41/4) is above the maximum 10");
    zc_vals_push_rat(v, 1, 1);
    EXPECT("none", ZC_PART_PROPOSALS, ZC_V_COUNT, 0, "none: 1 proposals, but the contract declares none");

    /* API edges */
    int32_t r = -1;
    uint32_t idx = 9;
    char small[8];
    size_t ml = 0;
    zc_vals_push_int(v, 99);
    CHECK(zc_check(C, "none", ZC_PART_PARAMS, v, &r, &idx, small, sizeof small, &ml) == ZC_E_VIOLATION &&
              strlen(small) == 7 && ml > 7, "message truncated, full length reported");
    CHECK(zc_check(C, "none", ZC_PART_PARAMS, v, NULL, NULL, NULL, 0, NULL) == ZC_E_VIOLATION, "NULL outs");
    CHECK(zc_check(C, "nope", ZC_PART_PARAMS, v, &r, &idx, NULL, 0, NULL) == ZXV_E_NOT_FOUND, "unknown fn");
    CHECK(zc_check(C, "none", 9, v, &r, &idx, NULL, 0, NULL) == ZXV_E_ARG, "bad part");
    CHECK(zc_check(NULL, "none", ZC_PART_PARAMS, v, &r, &idx, NULL, 0, NULL) == ZXV_E_ARG, "NULL contract");
    zc_vals_clear(v);

    /* subject digests */
    uint8_t a[32], b[32], c[32];
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_bytes(v, (const uint8_t *) "xy", 2);
    CHECK(zc_vals_digest(v, ZC_PART_PARAMS, a) == ZXV_OK && zc_vals_digest(v, ZC_PART_PARAMS, b) == ZXV_OK &&
              memcmp(a, b, 32) == 0, "digest deterministic");
    CHECK(zc_vals_digest(v, ZC_PART_PROPOSALS, c) == ZXV_OK && memcmp(a, c, 32) != 0, "part is in the digest");
    zc_vals_clear(v);
    zc_vals_push_rat(v, 1, 2);
    zc_vals_push_bytes(v, (const uint8_t *) "xz", 2);
    CHECK(zc_vals_digest(v, ZC_PART_PARAMS, c) == ZXV_OK && memcmp(a, c, 32) != 0, "bytes are in the digest");
    zc_vals_destroy(v);
    zc_contract_destroy(C);
    C = NULL;
}

static void test_tags(void) {
    char t[64];
    size_t n = 0;
    int32_t code = -1, lp = -1, pol = -1, ph = -1;
    int ok = 1;
    for (int32_t i = 0; i < 64; i++) {
        int32_t back = -1;
        if (zc_tag_format(i, t, sizeof t, &n) != ZXV_OK || zc_tag_parse(t, &back) != ZXV_OK || back != i)
            ok = 0;
        if (zc_tag_split(i, &lp, &pol, &ph) != ZXV_OK || zc_tag_make(lp, pol, ph, &back) != ZXV_OK ||
            back != i)
            ok = 0;
    }
    CHECK(ok, "all 64 tags round-trip through text and through make/split");
    /* kit API 0.2: the codes and names are libzxv's (ABI 1.3, zxv_hex_*) */
    ok = 1;
    for (int32_t i = 0; i < 64; i++) {
        uint8_t hb = 0, hp = 0, hf = 0, packed = 0;
        int32_t hlp = -1, act = -1, hact = -2;
        if (zc_tag_format(i, t, sizeof t, &n) != ZXV_OK || strcmp(t, zxv_hex_name((uint8_t) i)) != 0)
            ok = 0;
        if (zxv_hex_unpack((uint8_t) i, &hb, &hp, &hf) != ZXV_OK || zxv_hex_to_lp(hb, &hlp) != ZXV_OK ||
            zc_tag_split(i, &lp, &pol, &ph) != ZXV_OK || lp != hlp || pol != hp || ph != hf)
            ok = 0;
        if (zxv_hex_pack(hb, hp, hf, &packed) != ZXV_OK || packed != i) ok = 0;
        if (zc_tag_action(i, &act) != ZXV_OK || zxv_hex_action((uint8_t) i, &hact) != ZXV_OK || act != hact)
            ok = 0;
    }
    CHECK(ok, "all 64 tags: text = zxv_hex_name, split = zxv_hex_unpack, action = zxv_hex_action");
    CHECK(ZC_BASE_FALSE == ZXV_HEX_BASE_FALSE && ZC_BASE_NEITHER == ZXV_HEX_BASE_NEITHER &&
              ZC_BASE_BOTH == ZXV_HEX_BASE_BOTH && ZC_BASE_TRUE == ZXV_HEX_BASE_TRUE &&
              ZC_POL_ENTROPIC == ZXV_HEX_POL_ENTROPIC && ZC_POL_NEUTRAL == ZXV_HEX_POL_NEUTRAL &&
              ZC_POL_FORBIDDEN == ZXV_HEX_POL_FORBIDDEN && ZC_POL_NEGENTROPIC == ZXV_HEX_POL_NEGENTROPIC &&
              ZC_PH_POTENTIAL == ZXV_HEX_PHASE_POTENTIAL && ZC_PH_EMERGING == ZXV_HEX_PHASE_EMERGING &&
              ZC_PH_DISSOLVING == ZXV_HEX_PHASE_DISSOLVING && ZC_PH_MANIFEST == ZXV_HEX_PHASE_MANIFEST &&
              ZC_TAG_PROVISIONAL == 0 && ZC_VERSION_MINOR == 2,
          "the 0.1 aliases equal the ZXV_HEX_* constants; the tag is no longer provisional");
    {
        int32_t act = -1;
        CHECK(zc_tag_parse("false/forbidden/manifest", &code) == ZXV_OK && zc_tag_action(code, &act) == ZXV_OK &&
                  act == ZXV_HEX_ACT_ROLLBACK, "an output violation's tag means rollback");
        CHECK(zc_tag_parse("neither/neutral/emerging", &code) == ZXV_OK && zc_tag_action(code, &act) == ZXV_OK &&
                  act == ZXV_HEX_ACT_WAIT, "an unverified inbound record means wait");
        CHECK(zc_tag_action(64, &act) == ZXV_E_ARG && zc_tag_action(-1, &act) == ZXV_E_ARG &&
                  zc_tag_action(0, NULL) == ZXV_E_ARG, "zc_tag_action ranges");
    }
    CHECK(zc_tag_make(ZXV_LP_NEITHER, ZC_POL_NEUTRAL, ZC_PH_EMERGING, &code) == ZXV_OK &&
              zc_tag_format(code, t, sizeof t, &n) == ZXV_OK && strcmp(t, "neither/neutral/emerging") == 0 &&
              code == ((ZC_BASE_NEITHER << 4) | (ZC_POL_NEUTRAL << 2) | ZC_PH_EMERGING),
          "inbound default tag %d %s", code, t);
    CHECK(zc_tag_make(ZXV_LP_TRUE, ZC_POL_NEGENTROPIC, ZC_PH_MANIFEST, &code) == ZXV_OK && code == 63,
          "true/negentropic/manifest is 63 (kernel layout: true = 11)");
    CHECK(zc_tag_make(ZXV_LP_FALSE, ZC_POL_ENTROPIC, ZC_PH_POTENTIAL, &code) == ZXV_OK && code == 0,
          "false/entropic/potential is 0");
    CHECK(zc_tag_split(0x2D, &lp, &pol, &ph) == ZXV_OK && lp == ZXV_LP_BOTH && pol == ZC_POL_NEGENTROPIC &&
              ph == ZC_PH_EMERGING, "split 0x2D = both/negentropic/emerging");
    CHECK(zc_tag_make(4, 0, 0, &code) == ZXV_E_ARG && zc_tag_make(0, 4, 0, &code) == ZXV_E_ARG &&
              zc_tag_split(64, &lp, NULL, NULL) == ZXV_E_ARG && zc_tag_split(-1, NULL, NULL, NULL) == ZXV_E_ARG,
          "ranges");
    CHECK(zc_tag_parse("true/neutral", &code) == ZXV_E_FORMAT && zc_tag_parse("true/neutral/x", &code) == ZXV_E_FORMAT &&
              zc_tag_parse("TRUE/neutral/manifest", &code) == ZXV_E_FORMAT &&
              zc_tag_parse("true/neutral/manifest/", &code) == ZXV_E_FORMAT && zc_tag_parse("", &code) == ZXV_E_FORMAT,
          "parse refusals");
    CHECK(zc_tag_format(5, t, 5, &n) == ZXV_E_BUFFER && n == strlen("false/neutral/emerging"), "R4");
    CHECK(strcmp(zc_reason_name(ZC_V_DENOMINATOR), "DENOMINATOR") == 0 && strcmp(zc_reason_name(99), "UNKNOWN") == 0,
          "reason names");
    CHECK(strcmp(zc_status_name(ZC_E_SYNTAX), "ZC_E_SYNTAX") == 0 &&
              strcmp(zc_status_name(ZC_E_LOG), "ZC_E_LOG") == 0 &&
              strcmp(zc_status_name(ZXV_E_FUNDS), "ZXV_E_FUNDS") == 0, "status names");
}

typedef struct {
    int calls, refuse;
    uint64_t last_seq;
    uint8_t last_hash[32];
} sink_rec;

static int32_t sink(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                    const uint8_t subject[32], const uint8_t hash[32], const char *text) {
    sink_rec *s = (sink_rec *) user;
    (void) origin;
    (void) tag;
    (void) reason;
    (void) subject;
    (void) text;
    s->calls++;
    s->last_seq = seq;
    memcpy(s->last_hash, hash, 32);
    return s->refuse;
}

static void put_le(uint8_t *p, uint64_t v, int n) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void test_evlog(void) {
    zc_evlog *log = NULL;
    uint8_t contract[32], subject[32], head[32], h1[32], zero[32];
    uint64_t seq = 0, count = 9;
    sink_rec sr;
    memset(&sr, 0, sizeof sr);
    memset(zero, 0, 32);
    for (int i = 0; i < 32; i++) {
        contract[i] = (uint8_t) i;
        subject[i] = (uint8_t) (0xA0 + i);
    }
    CHECK(zc_evlog_create(0, &log) == ZXV_E_ARG && zc_evlog_create((1u << 20) + 1u, &log) == ZXV_E_ARG,
          "capacity bounds");
    CHECK(zc_evlog_create(3, &log) == ZXV_OK, "create");
    CHECK(zc_evlog_head(log, head) == ZXV_OK && memcmp(head, zero, 32) == 0, "empty head is zeros");
    CHECK(zc_evlog_verify(log) == ZXV_OK, "empty log verifies");
    CHECK(zc_evlog_set_sink(log, sink, &sr) == ZXV_OK, "sink");
    CHECK(zc_evlog_append(log, ZC_ORIGIN_WASM, ZC_PART_PROPOSALS, 1, ZC_V_DENOMINATOR, 0x2B, contract,
                          subject, "fee: proposal 1 = 333/80", &seq) == ZXV_OK && seq == 1, "append 1");
    CHECK(sr.calls == 1 && sr.last_seq == 1, "sink saw record 1");

    /* the documented record hash, recomputed independently */
    {
        uint8_t b[512], want[32];
        size_t k = 0;
        const char *dom = "zxv/zc-evlog/v1", *text = "fee: proposal 1 = 333/80";
        memcpy(b, dom, strlen(dom));
        k = strlen(dom);
        put_le(b + k, 1, 8); k += 8;
        memset(b + k, 0, 32); k += 32;
        put_le(b + k, ZC_ORIGIN_WASM, 4); k += 4;
        put_le(b + k, ZC_PART_PROPOSALS, 4); k += 4;
        put_le(b + k, 1, 4); k += 4;
        put_le(b + k, ZC_V_DENOMINATOR, 4); k += 4;
        put_le(b + k, 0x2B, 4); k += 4;
        memcpy(b + k, contract, 32); k += 32;
        memcpy(b + k, subject, 32); k += 32;
        put_le(b + k, strlen(text), 4); k += 4;
        memcpy(b + k, text, strlen(text)); k += strlen(text);
        zxv_sha3_256(b, k, want);
        CHECK(zc_evlog_head(log, h1) == ZXV_OK && memcmp(h1, want, 32) == 0, "record hash matches the format");
        CHECK(memcmp(sr.last_hash, want, 32) == 0, "the sink got the same hash");
    }
    CHECK(zc_evlog_append(log, ZC_ORIGIN_ADAPTER, ZC_PART_PARAMS, 0, ZC_V_ABOVE, 1, NULL, NULL, NULL, &seq) ==
              ZXV_OK && seq == 2, "append 2 (NULL digests and text)");
    sr.refuse = 1;
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, 1, 0, 1, 1, NULL, NULL, "x", &seq) == ZC_E_LOG,
          "sink refusal is ZC_E_LOG");
    CHECK(zc_evlog_count(log, &count) == ZXV_OK && count == 2, "refused record not appended");
    sr.refuse = 0;
    zc_evlog_set_sink(log, NULL, NULL);
    /* text truncation at a UTF-8 boundary: 254 ASCII bytes then a 2-byte char */
    char longtext[400];
    memset(longtext, 'a', 254);
    longtext[254] = (char) 0xC3;
    longtext[255] = (char) 0xA9;
    longtext[256] = '\0';
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, 1, 0, 1, 1, NULL, NULL, longtext, &seq) == ZXV_OK && seq == 3,
          "append 3");
    char tb[300];
    size_t tl = 0;
    uint32_t origin = 9, index = 9;
    int32_t part = 9, reason = 9, tag = 9;
    uint8_t c2[32], s2[32], hh[32];
    CHECK(zc_evlog_get(log, 3, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tb, sizeof tb, &tl) == ZXV_OK &&
              tl == 254, "text cut before the split character (%zu)", tl);
    CHECK(zc_evlog_get(log, 1, &origin, &part, &index, &reason, &tag, c2, s2, hh, NULL, 0, &tl) == ZXV_E_BUFFER &&
              tl == strlen("fee: proposal 1 = 333/80") && origin == 9, "R4 on the text");
    CHECK(zc_evlog_get(log, 1, &origin, &part, &index, &reason, &tag, c2, s2, hh, tb, sizeof tb, &tl) == ZXV_OK &&
              origin == ZC_ORIGIN_WASM && part == ZC_PART_PROPOSALS && index == 1 && reason == ZC_V_DENOMINATOR &&
              tag == 0x2B && memcmp(c2, contract, 32) == 0 && memcmp(s2, subject, 32) == 0 &&
              memcmp(hh, h1, 32) == 0 && strcmp(tb, "fee: proposal 1 = 333/80") == 0, "get record 1");
    CHECK(zc_evlog_get(log, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tb, sizeof tb, &tl) == ZXV_E_NOT_FOUND &&
              zc_evlog_get(log, 4, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tb, sizeof tb, &tl) ==
                  ZXV_E_NOT_FOUND, "seq 0 and 4");
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, 1, 0, 1, 1, NULL, NULL, "x", &seq) == ZXV_E_CAPACITY,
          "a full log refuses (fail closed)");
    CHECK(zc_evlog_verify(log) == ZXV_OK, "chain verifies");

    /* tampering */
    zc_rec saved = log->rec[1];
    log->rec[1].reason = ZC_V_OK;
    CHECK(zc_evlog_verify(log) == ZXV_E_VERIFY && strstr(last_err(), "record 2"), "altered field detected");
    log->rec[1] = saved;
    log->rec[0].text[0] = 'F';
    CHECK(zc_evlog_verify(log) == ZXV_E_VERIFY && strstr(last_err(), "record 1"), "altered text detected");
    log->rec[0].text[0] = 'f';
    CHECK(zc_evlog_verify(log) == ZXV_OK, "restored");
    zc_rec r0 = log->rec[0];
    log->rec[0] = log->rec[1];
    log->rec[1] = r0;
    CHECK(zc_evlog_verify(log) == ZXV_E_VERIFY, "reordered records detected");
    log->rec[1] = log->rec[0];
    log->rec[0] = r0;
    CHECK(zc_evlog_verify(log) == ZXV_OK, "restored again");
    log->n = 2;
    CHECK(zc_evlog_verify(log) == ZXV_E_VERIFY && strstr(last_err(), "truncated"), "dropped last record detected");
    log->n = 3;
    CHECK(zc_evlog_verify(log) == ZXV_OK, "restored");
    zc_evlog_destroy(log);
    zc_evlog_destroy(NULL);

    /* growth past the first allocation */
    CHECK(zc_evlog_create(100, &log) == ZXV_OK, "create 100");
    int ok = 1;
    for (int i = 0; i < 100; i++)
        if (zc_evlog_append(log, 0, 1, (uint32_t) i, 1, 1, NULL, NULL, "r", &seq) != ZXV_OK) ok = 0;
    CHECK(ok && zc_evlog_verify(log) == ZXV_OK && zc_evlog_count(log, &count) == ZXV_OK && count == 100,
          "100 records chain");
    zc_evlog_destroy(log);
}

static void test_bounds(void) {
    zc_bounds b;
    zc_contract *c = parse_ok("fn f(a: int[-5..10^12], r: rat[1/3..*] den<=1000, t: truth{true, false},\n"
                              "      g: tag{*/forbidden/*}, s: bytes[2..9], o: int) -> int[0..*]\n"
                              "    proposes rat[0..1] den|100 max 3;\n");
    CHECK(c != NULL, "bounds contract parses");
    if (!c) return;
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 0, &b) == ZXV_OK && b.type == ZC_T_INT && b.has_lo && b.has_hi &&
              b.lo_num == -5 && b.lo_den == 1 && b.hi_num == 1000000000000LL && b.hi_den == 1 &&
              b.den_mode == ZC_DEN_NONE, "int bounds");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 1, &b) == ZXV_OK && b.type == ZC_T_RAT && b.has_lo && !b.has_hi &&
              b.lo_num == 1 && b.lo_den == 3 && b.den_mode == ZC_DEN_LE && b.den_bound == 1000,
          "rat bounds with an open side and den<=");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 2, &b) == ZXV_OK && b.type == ZC_T_TRUTH &&
              b.set_mask == ((1u << ZXV_LP_TRUE) | (1u << ZXV_LP_FALSE)), "truth set mask");
    {
        uint64_t want = 0;
        for (uint8_t k = 0; k < 64; k++) {
            uint8_t pl = 0;
            zxv_hex_unpack(k, NULL, &pl, NULL);
            if (pl == ZXV_HEX_POL_FORBIDDEN) want |= UINT64_C(1) << k;
        }
        CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 3, &b) == ZXV_OK && b.type == ZC_T_TAG && b.set_mask == want,
              "tag set mask = the 16 forbidden codes");
    }
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 4, &b) == ZXV_OK && b.type == ZC_T_BYTES && b.lo_num == 2 &&
              b.has_hi && b.hi_num == 9, "bytes length bounds");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 5, &b) == ZXV_OK && !b.has_lo && !b.has_hi, "unbounded int");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_RESULT, 0, &b) == ZXV_OK && b.has_lo && !b.has_hi && b.lo_num == 0,
          "result bounds");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PROPOSALS, 0, &b) == ZXV_OK && b.type == ZC_T_RAT &&
              b.den_mode == ZC_DEN_DIVIDES && b.den_bound == 100 && b.hi_num == 1, "proposal bounds");
    CHECK(zc_slot_bounds(c, "f", ZC_PART_PARAMS, 6, &b) == ZXV_E_NOT_FOUND &&
              zc_slot_bounds(c, "g", ZC_PART_PARAMS, 0, &b) == ZXV_E_NOT_FOUND &&
              zc_slot_bounds(c, "f", 9, 0, &b) == ZXV_E_ARG && zc_slot_bounds(c, "f", ZC_PART_PARAMS, 0, NULL) == ZXV_E_ARG,
          "zc_slot_bounds refusals");
    zc_contract_destroy(c);
}

int main(void) {
    printf("test_check: values, checker, tags and the evidence log\n");
    test_vals();
    test_reasons();
    test_tags();
    test_evlog();
    test_bounds();
    printf("test_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
