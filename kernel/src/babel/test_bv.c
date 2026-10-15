/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_bv.c — the Babel value: builders, readers, the canonical encoding,
 * equality and the both-sides witness, then generated values (round trips)
 * and random bytes (decode never crashes, and what it accepts re-encodes
 * to the same bytes). Usage: test_bv [iterations]. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bv.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static bv_arena_t A, B;
static uint8_t buf[1u << 20], buf2[1u << 20];

static int32_t S(bv_arena_t *a, const char *s)
{
    return bv_symbol(a, s, (uint32_t) strlen(s));
}
static int32_t T(bv_arena_t *a, const char *s)
{
    return bv_text(a, s, (uint32_t) strlen(s));
}
static int32_t D(bv_arena_t *a, const char *s)
{
    return bv_decimal(a, s, (uint32_t) strlen(s));
}

/* encode, decode into B, compare, re-encode: the same bytes */
static bool round_trip(int32_t v)
{
    int32_t n = bv_encode(&A, v, buf, sizeof buf);
    if (n < 0) return false;
    bv_reset(&B);
    int32_t w = bv_decode(&B, buf, (uint32_t) n);
    if (w < 0 || !bv_equal(&A, v, &B, w) || !bv_equal(&B, w, &A, v)) return false;
    int32_t m = bv_encode(&B, w, buf2, sizeof buf2);
    return m == n && memcmp(buf, buf2, (size_t) n) == 0;
}

/* decode from an exact-size heap copy, so a read past the end is caught */
static int32_t dec(const uint8_t *p, uint32_t n)
{
    uint8_t *q = malloc(n ? n : 1u);
    memcpy(q, p, n);
    bv_reset(&B);
    int32_t v = bv_decode(&B, q, n);
    free(q);
    return v;
}

static void test_numbers(void)
{
    bv_reset(&A);
    int32_t v = bv_int(&A, -7);
    CHECK(v == 0 && bv_kind(&A, v) == BV_INTEGER && bv_num(&A, v) == -7 && bv_den(&A, v) == 1,
          "an integer");
    v = bv_ratio(&A, 6, -4);
    CHECK(bv_kind(&A, v) == BV_RATIO && bv_num(&A, v) == -3 && bv_den(&A, v) == 2,
          "6/-4 is -3/2: sign on top, lowest terms");
    v = bv_ratio(&A, -8, -4);
    CHECK(bv_kind(&A, v) == BV_INTEGER && bv_num(&A, v) == 2, "an exact ratio is an integer");
    v = bv_ratio(&A, 0, -5);
    CHECK(bv_kind(&A, v) == BV_INTEGER && bv_num(&A, v) == 0, "0/-5 is 0");
    CHECK(bv_ratio(&A, 1, 0) == BV_E_DOMAIN, "a zero denominator");
    v = bv_ratio(&A, INT64_MIN, 2);
    CHECK(bv_kind(&A, v) == BV_INTEGER && bv_num(&A, v) == INT64_MIN / 2, "INT64_MIN/2");
    CHECK(bv_ratio(&A, INT64_MIN, -1) == BV_E_OVERFLOW, "-INT64_MIN does not fit");
    v = bv_ratio(&A, INT64_MIN, -2);
    CHECK(bv_kind(&A, v) == BV_INTEGER && bv_num(&A, v) == (int64_t) 1 << 62, "INT64_MIN/-2");
    v = bv_ratio(&A, 1, INT64_MIN);
    CHECK(v == BV_E_OVERFLOW, "1/INT64_MIN: the denominator 2^63 does not fit");
    v = bv_ratio(&A, 3, INT64_MIN);
    CHECK(v == BV_E_OVERFLOW, "3/INT64_MIN: no common factor, 2^63 does not fit");
    v = bv_ratio(&A, 2, INT64_MIN);
    CHECK(bv_kind(&A, v) == BV_RATIO && bv_num(&A, v) == -1 && bv_den(&A, v) == (int64_t) 1 << 62,
          "2/INT64_MIN is -1/2^62");
    v = bv_ratio(&A, INT64_MAX, INT64_MAX - 1);
    CHECK(bv_kind(&A, v) == BV_RATIO && bv_num(&A, v) == INT64_MAX &&
              bv_den(&A, v) == INT64_MAX - 1,
          "co-prime extremes");
    CHECK(bv_truth(&A, BV_BOTH) >= 0 && bv_truth(&A, 4) == BV_E_DOMAIN &&
              bv_truth(&A, -1) == BV_E_DOMAIN,
          "truths are 0..3");

    static const struct {
        const char *s;
        int64_t num, den;
        int32_t err;
    } d[] = {
        {"1.25", 5, 4, 0},
        {"-12.50", -25, 2, 0},
        {"+3", 3, 1, 0},
        {"007", 7, 1, 0},
        {"0.000", 0, 1, 0},
        {"-0", 0, 1, 0},
        {"0.1", 1, 10, 0},
        {"9223372036854775807", INT64_MAX, 1, 0},
        {"-9223372036854775808", INT64_MIN, 1, 0},
        {"9223372036854775808", 0, 0, BV_E_OVERFLOW},
        {"18446744073709551616", 0, 0, BV_E_OVERFLOW},
        {"0.0000000000000000001", 0, 0, BV_E_OVERFLOW},
        {"0.000000000000000001", 1, 1000000000000000000, 0},
        {"92233720368547758.08", INT64_C(2305843009213693952), 25, 0},
        {"1844674407370955161.5", INT64_C(3689348814741910323), 2, 0},
        {"0.9223372036854775809", 0, 0, BV_E_OVERFLOW},
        {"18446744073709551.613", 0, 0, BV_E_OVERFLOW},
        {"184467440737095516.16", 0, 0, BV_E_OVERFLOW}, /* the digits must fit first */
        {"", 0, 0, BV_E_FORM},
        {"-", 0, 0, BV_E_FORM},
        {".5", 0, 0, BV_E_FORM},
        {"5.", 0, 0, BV_E_FORM},
        {"1.2.3", 0, 0, BV_E_FORM},
        {"1e3", 0, 0, BV_E_FORM},
        {" 1", 0, 0, BV_E_FORM},
        {"--1", 0, 0, BV_E_FORM},
    };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) {
        v = D(&A, d[i].s);
        if (d[i].err)
            CHECK(v == d[i].err, "decimal \"%s\" refused %s, got %s", d[i].s,
                  bv_status_name(d[i].err), bv_status_name(v));
        else
            CHECK(v >= 0 && bv_num(&A, v) == d[i].num && bv_den(&A, v) == d[i].den,
                  "decimal \"%s\" is %lld/%lld", d[i].s, (long long) d[i].num,
                  (long long) d[i].den);
    }
}

static void test_text(void)
{
    bv_reset(&A);
    int32_t v = T(&A, "Grüße, 世界 🐉");
    CHECK(v >= 0 && bv_kind(&A, v) == BV_TEXT && bv_len(&A, v) == strlen("Grüße, 世界 🐉") &&
              memcmp(bv_data(&A, v), "Grüße", 6) == 0,
          "UTF-8 text");
    CHECK(T(&A, "") >= 0, "empty text is text");
    static const char *bad[] = {
        "\x80",         "\xC0\xAF",         "\xC1\xBF",         "\xE0\x80\xAF",
        "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF",
        "a\xC3",        "\xE2\x82",         "\xC3\x28",         "\xF0\x8F\xBF\xBF"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(T(&A, bad[i]) == BV_E_TEXT && S(&A, bad[i]) == BV_E_TEXT, "bad UTF-8 #%u", i);
    static const char *good[] = {
        "\x7F",         "\xC2\x80",         "\xDF\xBF",         "\xE0\xA0\x80",
        "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF", "\xED\x9F\xBF",
        "\xEE\x80\x80"};
    for (unsigned i = 0; i < sizeof good / sizeof good[0]; i++)
        CHECK(T(&A, good[i]) >= 0, "edge UTF-8 #%u", i);
    uint8_t raw[] = {0xFF, 0, 0x80};
    v = bv_bytes(&A, raw, 3);
    CHECK(v >= 0 && bv_kind(&A, v) == BV_BYTES && bv_len(&A, v) == 3 && bv_data(&A, v)[0] == 0xFF,
          "bytes take anything");
    CHECK(bv_symbol(&A, "", 0) == BV_E_NAME, "a symbol is never empty");
    CHECK(bv_kind(&A, S(&A, "car")) == BV_SYMBOL, "a symbol");
    CHECK(!bv_equal(&A, T(&A, "x"), &A, S(&A, "x")), "text x is not symbol x");
    CHECK(bv_data(&A, bv_int(&A, 1)) == 0 && bv_len(&A, bv_int(&A, 1)) == 0,
          "numbers have no bytes");
}

static void test_structures(void)
{
    bv_reset(&A);
    int32_t e[3] = {bv_int(&A, 1), bv_ratio(&A, 1, 2), T(&A, "three")};
    int32_t l = bv_list(&A, e, 3);
    CHECK(l >= 0 && bv_kind(&A, l) == BV_LIST && bv_len(&A, l) == 3 && bv_at(&A, l, 2) == e[2] &&
              bv_at(&A, l, 3) == BV_E_HANDLE,
          "a list");
    CHECK(bv_list(&A, 0, 0) >= 0, "the empty list");
    int32_t names[2] = {S(&A, "x"), S(&A, "y")}, vals[2] = {bv_int(&A, 3), l};
    int32_t r = bv_record(&A, names, vals, 2);
    CHECK(r >= 0 && bv_kind(&A, r) == BV_RECORD && bv_len(&A, r) == 2 &&
              bv_get(&A, r, "y", 1) == l && bv_get(&A, r, "x", 1) == vals[0] &&
              bv_get(&A, r, "z", 1) == BV_E_NAME && bv_get(&A, r, "xy", 2) == BV_E_NAME &&
              bv_name(&A, r, 1) == names[1] && bv_at(&A, r, 0) == vals[0],
          "a record");
    CHECK(bv_name(&A, l, 0) == BV_E_HANDLE && bv_get(&A, l, "x", 1) == BV_E_HANDLE,
          "a list has no names");
    int32_t dup[2] = {S(&A, "x"), S(&A, "x")};
    CHECK(bv_record(&A, dup, vals, 2) == BV_E_NAME, "a repeated name");
    int32_t notsym[1] = {T(&A, "x")};
    CHECK(bv_record(&A, notsym, vals, 1) == BV_E_NAME, "a name is a symbol");
    int32_t badh[1] = {99999};
    CHECK(bv_list(&A, badh, 1) == BV_E_HANDLE && bv_record(&A, names, badh, 1) == BV_E_HANDLE,
          "handles from nowhere");
    int32_t neg[1] = {-1};
    CHECK(bv_list(&A, neg, 1) == BV_E_HANDLE, "a status is not a handle");

    /* depth: 64 nested lists fit, 65 do not */
    bv_reset(&A);
    int32_t v = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH; i++) v = bv_list(&A, &v, 1);
    CHECK(v >= 0, "depth %u fits", BV_MAX_DEPTH);
    CHECK(round_trip(v), "the deepest value round-trips");
    CHECK(bv_list(&A, &v, 1) == BV_E_LIMIT, "one deeper is LIMIT");
    int32_t nm = S(&A, "k");
    CHECK(bv_record(&A, &nm, &v, 1) == BV_E_LIMIT, "in a record too");
}

static void test_pairs(void)
{
    bv_reset(&A);
    int32_t h = S(&A, "bob"), t = bv_int(&A, 25);
    int32_t p = bv_pair(&A, h, t);
    CHECK(p >= 0 && bv_kind(&A, p) == BV_PAIR && bv_len(&A, p) == 2 && bv_at(&A, p, 0) == h &&
              bv_at(&A, p, 1) == t && bv_at(&A, p, 2) == BV_E_HANDLE,
          "(bob . 25)");
    CHECK(round_trip(p), "a pair round-trips");
    int32_t e[2] = {bv_int(&A, 2), bv_int(&A, 3)};
    int32_t l = bv_list(&A, e, 2);
    int32_t q = bv_pair(&A, bv_int(&A, 1), l);
    CHECK(bv_kind(&A, q) == BV_LIST && bv_len(&A, q) == 3 && bv_num(&A, bv_at(&A, q, 0)) == 1 &&
              bv_at(&A, q, 2) == e[1],
          "(1 . (2 3)) is (1 2 3)");
    int32_t one = bv_pair(&A, h, bv_list(&A, 0, 0));
    CHECK(bv_kind(&A, one) == BV_LIST && bv_len(&A, one) == 1 && bv_at(&A, one, 0) == h,
          "(bob . ()) is (bob)");
    int32_t z = bv_pair(&A, l, p);
    CHECK(bv_kind(&A, z) == BV_PAIR && round_trip(z), "((2 3) . (bob . 25)) stays a pair");
    CHECK(bv_pair(&A, -1, t) == BV_E_HANDLE && bv_pair(&A, h, 99999) == BV_E_HANDLE,
          "pair handles");
    CHECK(!bv_equal(&A, p, &A, l) && !bv_equal(&A, p, &A, bv_pair(&A, h, bv_int(&A, 26))),
          "pairs compare by content");
    CHECK(bv_equal(&A, p, &A, bv_pair(&A, S(&A, "bob"), bv_int(&A, 25))), "equal pairs");
    /* a pair whose tail is a list is not canonical */
    static const uint8_t bad[] = {9, 1, 0, 0, 0, 0, 0, 0, 0, 1, 7, 0, 0, 0, 0};
    CHECK(dec(bad, sizeof bad) == BV_E_FORM && B.nn == 0, "(1 . ()) as a pair is refused");
    static const uint8_t ok[] = {9, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    CHECK(dec(ok, sizeof ok) >= 0 && bv_kind(&B, 2) == BV_PAIR, "(1 . nothing) decodes");
    /* depth through pairs */
    bv_reset(&A);
    int32_t v = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH; i++) v = bv_pair(&A, v, bv_int(&A, 0));
    CHECK(v >= 0 && round_trip(v), "64 nested pairs");
    CHECK(bv_pair(&A, v, bv_int(&A, 0)) == BV_E_LIMIT &&
              bv_pair(&A, bv_int(&A, 0), v) == BV_E_LIMIT,
          "65 is LIMIT, head or tail");
    int32_t deep = bv_list(&A, &v, 0);
    v = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH - 1u; i++) v = bv_list(&A, &v, 1);
    CHECK(bv_pair(&A, bv_int(&A, 1), v) >= 0, "a list tail keeps its depth");
    (void) deep;
}

static void test_edges(void)
{
    bv_reset(&A);
    int32_t h = bv_int(&A, 1);
    int32_t past = (int32_t) A.nn;
    A.node[past] = A.node[h]; /* a stale node just past the end */
    CHECK(bv_list(&A, &past, 1) == BV_E_HANDLE && bv_kind(&A, past) == BV_NOTHING,
          "the handle one past the last is not live");
    CHECK(D(&A, "-9223372036854775809") == BV_E_OVERFLOW, "one below INT64_MIN");
    int32_t v = bv_ratio(&A, 1, INT64_MAX);
    CHECK(bv_kind(&A, v) == BV_RATIO && bv_den(&A, v) == INT64_MAX, "1/INT64_MAX fits");
    CHECK(bv_truth(&A, BV_NEITHER) >= 0 && bv_num(&A, bv_truth(&A, BV_NEITHER)) == 0,
          "NEITHER is a truth");
    CHECK(T(&A, "\xFC\x8F\xBF\xBF") == BV_E_TEXT && T(&A, "\xF8\x88\x80\x80") == BV_E_TEXT,
          "0xF8..0xFF never lead");
    CHECK(T(&A, "\xED\xBF\xBF") == BV_E_TEXT, "U+DFFF is a surrogate");
    struct {
        char x[2];
        unsigned char y;
    } cut = {{'a', (char) 0xC3}, 0x80};
    CHECK(bv_text(&A, cut.x, 2) == BV_E_TEXT, "a sequence cut by the length, not by a NUL");
    char *ex = malloc(3);
    memcpy(ex, "abc", 3);
    v = bv_text(&A, ex, 3);
    CHECK(v >= 0 && bv_len(&A, v) == 3, "exact-size text (no read past it)");
    free(ex);
    CHECK(bv_num(&A, T(&A, "x")) == 0 && bv_den(&A, T(&A, "x")) == 0 &&
              bv_num(&A, bv_list(&A, 0, 0)) == 0,
          "only numbers and truths have a numerator");

    /* records: names compared at every position, and in full */
    bv_reset(&A);
    int32_t n3[3] = {S(&A, "ab"), S(&A, "ba"), S(&A, "ba")}, v3[3];
    for (int i = 0; i < 3; i++) v3[i] = bv_int(&A, i);
    CHECK(bv_record(&A, n3, v3, 3) == BV_E_NAME, "a repeat at the second and third name");
    int32_t r = bv_record(&A, n3, v3, 2);
    CHECK(bv_get(&A, r, "ba", 2) == v3[1] && bv_get(&A, r, "ab", 2) == v3[0] &&
              bv_get(&A, r, "bb", 2) == BV_E_NAME && bv_get(&A, r, "aa", 2) == BV_E_NAME,
          "names compared byte by byte");
    CHECK(bv_name(&A, r, 2) == BV_E_HANDLE && bv_name(&A, r, 1) == n3[1], "bv_name in range");
    int32_t zn = S(&A, "z"), r2 = bv_record(&A, &zn, v3, 1);
    CHECK(r2 >= 0 && bv_get(&A, r, "z", 1) == BV_E_NAME,
          "a name in the next record is not this record's");
    uint8_t enc1[64];
    int32_t m = bv_encode(&A, r, enc1, sizeof enc1);
    static const uint8_t dupe[] = {8, 0, 0, 0, 3,   6, 0, 0, 0, 1, 'a', 0,   6,
                                   0, 0, 0, 1, 'b', 0, 6, 0, 0, 0, 1,   'b', 0};
    CHECK(m > 0 && dec(enc1, (uint32_t) m) >= 0 && dec(dupe, sizeof dupe) == BV_E_FORM,
          "decode: a repeat at the second and third name");

    /* counts and lengths at the arena's edges leave nothing behind */
    bv_reset(&A);
    while (bv_int(&A, 0) >= 0) {
    }
    uint32_t nb = A.nb, nk = A.nk;
    int32_t z = 0;
    CHECK(T(&A, "x") == BV_E_LIMIT && A.nb == nb, "no node: no bytes taken");
    CHECK(bv_list(&A, &z, 1) == BV_E_LIMIT && A.nk == nk, "no node: no kids taken");

    /* encoding into exactly enough, and one short (nothing written past cap) */
    bv_reset(&A);
    v = T(&A, "hi");
    memset(buf, 0xAA, 16);
    CHECK(bv_encode(&A, v, buf, 7) == 7 && buf[7] == 0xAA, "exactly 7");
    memset(buf, 0xAA, 16);
    CHECK(bv_encode(&A, v, buf, 6) == BV_E_LIMIT && buf[6] == 0xAA && buf[5] == 0xAA,
          "6 is short and nothing goes past it");

    /* decode depth: a record holding a 64-deep value is 65 deep */
    bv_reset(&A);
    v = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH; i++) v = bv_list(&A, &v, 1);
    int32_t len = bv_encode(&A, v, buf, sizeof buf);
    uint8_t pre[] = {8, 0, 0, 0, 1, 6, 0, 0, 0, 1, 'k'};
    memmove(buf + sizeof pre, buf, (size_t) len);
    memcpy(buf, pre, sizeof pre);
    CHECK(dec(buf, (uint32_t) len + sizeof pre) == BV_E_LIMIT, "decode: a record 65 deep");
    int32_t w = dec(buf + sizeof pre, (uint32_t) len);
    CHECK(w >= 0 && bv_list(&B, &w, 1) == BV_E_LIMIT, "a decoded 64-deep list knows its depth");
    static const uint8_t pp[] = {9, 1, 0, 0, 0, 0, 0, 0, 0, 1};
    memcpy(buf, pp, 10);
    len = bv_encode(&A, v, buf + 10, sizeof buf - 10);
    CHECK(dec(buf, (uint32_t) len + 10) == BV_E_LIMIT, "decode: a pair whose tail is 64 deep");
    w = dec(buf + 10, (uint32_t) len);
    CHECK(w >= 0 && bv_pair(&B, w, bv_int(&B, 1)) == BV_E_LIMIT, "and its head");

    /* a million nested lists: refused, without recursing a million deep */
    static uint8_t huge[5u * 100000u + 1u];
    for (uint32_t i = 0; i < 100000u; i++) {
        huge[5 * i] = 7;
        huge[5 * i + 1] = huge[5 * i + 2] = huge[5 * i + 3] = 0;
        huge[5 * i + 4] = 1;
    }
    huge[500000] = 0;
    bv_reset(&B);
    CHECK(bv_decode(&B, huge, sizeof huge) == BV_E_LIMIT, "100000 levels: LIMIT");

    /* equality */
    bv_reset(&A);
    int32_t e3[3] = {bv_int(&A, 1), bv_int(&A, 2), bv_int(&A, 3)};
    int32_t l2 = bv_list(&A, e3, 2), l3 = bv_list(&A, e3, 3);
    CHECK(!bv_equal(&A, l2, &A, l3) && !bv_equal(&A, l3, &A, l2), "lengths differ");
    int32_t f3[3] = {e3[0], e3[1], bv_int(&A, 4)};
    CHECK(!bv_equal(&A, l3, &A, bv_list(&A, f3, 3)), "the last element differs");
    CHECK(bv_equal(&A, l3, &A, bv_list(&A, e3, 3)), "the same list");
    CHECK(!bv_equal(&A, l3, &A, -1) && !bv_equal(&A, -1, &A, l3) && !bv_equal(&A, -1, &A, -1),
          "a missing side is never equal");
}

/* bv_pair onto a list, and decoded pairs, at every edge */
static void test_pair_edges(void)
{
    bv_reset(&A);
    int32_t h = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH - 1u; i++) h = bv_list(&A, &h, 1); /* depth 63 */
    int32_t l = bv_pair(&A, h, bv_list(&A, 0, 0));
    CHECK(l >= 0 && bv_kind(&A, l) == BV_LIST && bv_list(&A, &l, 1) == BV_E_LIMIT,
          "(h . ()) with h 63 deep is 64 deep");
    int32_t h64 = bv_list(&A, &h, 1);
    CHECK(bv_pair(&A, h64, bv_list(&A, 0, 0)) == BV_E_LIMIT, "a 64-deep head onto a list");
    int32_t t = bv_int(&A, 1);
    for (unsigned i = 0; i < BV_MAX_DEPTH; i++) t = bv_list(&A, &t, 1); /* depth 64 */
    int32_t l2 = bv_pair(&A, bv_int(&A, 2), t);
    CHECK(l2 >= 0 && bv_len(&A, l2) == 2 && bv_list(&A, &l2, 1) == BV_E_LIMIT,
          "a head onto a 64-deep list stays 64 deep");
    int32_t t1 = bv_list(&A, &h, 1); /* depth 64, its element 63 deep */
    int32_t sh = bv_int(&A, 3);
    for (unsigned i = 0; i < BV_MAX_DEPTH - 2u; i++) sh = bv_list(&A, &sh, 1); /* depth 62 */
    int32_t l3 = bv_pair(&A, sh, t1);
    CHECK(l3 >= 0 && bv_list(&A, &l3, 1) == BV_E_LIMIT, "a shallower head keeps the tail's depth");

    /* kids: the new list needs exactly n + 1 */
    bv_reset(&A);
    int32_t txt = T(&A, "keep");
    int32_t e2[2] = {bv_int(&A, 1), bv_int(&A, 2)};
    int32_t two = bv_list(&A, e2, 2);
    static int32_t fill[BV_MAX_KIDS];
    for (unsigned i = 0; i < BV_MAX_KIDS; i++) fill[i] = e2[0];
    CHECK(bv_list(&A, fill, BV_MAX_KIDS - A.nk - 3u) >= 0 && BV_MAX_KIDS - A.nk == 3u,
          "three kids left");
    int32_t p3 = bv_pair(&A, e2[1], two);
    CHECK(p3 >= 0 && bv_len(&A, p3) == 3 && A.nk == BV_MAX_KIDS, "exactly enough for (2 1 2)");
    CHECK(bv_pair(&A, e2[0], two) == BV_E_LIMIT && memcmp(bv_data(&A, txt), "keep", 4) == 0,
          "then none, and nothing written past the kids");
    bv_reset(&A);
    e2[0] = bv_int(&A, 1);
    e2[1] = bv_int(&A, 2);
    two = bv_list(&A, e2, 2);
    bv_list(&A, fill, BV_MAX_KIDS - A.nk - 2u);
    CHECK(bv_pair(&A, e2[0], two) == BV_E_LIMIT, "two kids left is one short");
    bv_reset(&A);
    e2[0] = bv_int(&A, 1);
    two = bv_list(&A, e2, 1);
    while (bv_int(&A, 0) >= 0) {
    }
    uint32_t nk = A.nk;
    CHECK(bv_pair(&A, e2[0], two) == BV_E_LIMIT && A.nk == nk, "no node left: no kids taken");

    /* decoding a pair or list into the last kids */
    static const uint8_t pr[] = {9, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    static const uint8_t ls[] = {7, 0, 0, 0, 2, 0, 0};
    for (unsigned left = 0; left <= 3; left++) {
        bv_reset(&B);
        int32_t one = bv_int(&B, 0);
        for (unsigned i = 0; i < BV_MAX_KIDS - left; i++) fill[i] = one;
        bv_list(&B, fill, BV_MAX_KIDS - left);
        CHECK((bv_decode(&B, pr, sizeof pr) >= 0) == (left >= 2), "a pair with %u kids left", left);
        bv_reset(&B);
        one = bv_int(&B, 0);
        bv_list(&B, fill, BV_MAX_KIDS - left);
        CHECK((bv_decode(&B, ls, sizeof ls) >= 0) == (left >= 2), "a list of two with %u left",
              left);
    }
    /* decoded pairs know their depth */
    bv_reset(&A);
    h = bv_int(&A, 0);
    for (unsigned i = 0; i < BV_MAX_DEPTH - 1u; i++) h = bv_list(&A, &h, 1);
    int32_t p = bv_pair(&A, h, bv_int(&A, 5)); /* depth 64 */
    int32_t n = bv_encode(&A, p, buf, sizeof buf);
    int32_t w = dec(buf, (uint32_t) n);
    CHECK(w >= 0 && bv_list(&B, &w, 1) == BV_E_LIMIT, "a decoded 64-deep pair");
    h64 = bv_list(&A, &h, 1);
    n = bv_encode(&A, h64, buf + 1, sizeof buf - 1);
    buf[0] = 9;
    buf[n + 1] = 0;
    CHECK(dec(buf, (uint32_t) n + 2u) == BV_E_LIMIT, "a decoded pair with a 64-deep head");
    static const uint8_t many[] = {7, 0, 0, 0x23, 0x28, 0};
    CHECK(dec(many, sizeof many) == BV_E_FORM, "a count past the input is FORM, not LIMIT");
}

static void test_limits(void)
{
    bv_reset(&A);
    unsigned made = 0;
    while (bv_int(&A, 1) >= 0) made++;
    CHECK(made == BV_MAX_NODES && bv_nothing(&A) == BV_E_LIMIT && bv_truth(&A, 1) == BV_E_LIMIT,
          "nodes run out at %u", BV_MAX_NODES);
    bv_reset(&A);
    static char big[BV_MAX_BYTES + 1];
    memset(big, 'a', sizeof big);
    CHECK(bv_text(&A, big, BV_MAX_BYTES + 1) == BV_E_LIMIT, "bytes past the arena");
    CHECK(bv_text(&A, big, BV_MAX_BYTES) >= 0 && bv_text(&A, big, 1) == BV_E_LIMIT &&
              bv_text(&A, big, 0) >= 0,
          "exactly the arena, then nothing more");
    bv_reset(&A);
    static int32_t many[BV_MAX_KIDS + 1];
    for (unsigned i = 0; i < 8; i++) many[i] = bv_int(&A, i);
    for (unsigned i = 8; i <= BV_MAX_KIDS; i++) many[i] = many[i % 8];
    CHECK(bv_list(&A, many, BV_MAX_KIDS + 1) == BV_E_LIMIT, "kids past the arena");
    int32_t full = bv_list(&A, many, BV_MAX_KIDS);
    CHECK(full >= 0 && bv_list(&A, many, 1) == BV_E_LIMIT && bv_list(&A, many, 0) >= 0,
          "exactly the arena");
    int32_t n = bv_encode(&A, full, buf, sizeof buf);
    CHECK(n == 5 + 9 * (int32_t) BV_MAX_KIDS, "a full list encodes: %d", n);
    for (uint32_t cap = 0; cap < 20; cap++)
        CHECK(bv_encode(&A, full, buf, cap) == BV_E_LIMIT, "short buffer %u", cap);
    CHECK(bv_encode(&A, full, buf, (uint32_t) n - 1) == BV_E_LIMIT &&
              bv_encode(&A, full, buf, (uint32_t) n) == n,
          "one byte short, then exact");
    int32_t t = T(&A, "");
    (void) t;
}

static void test_encoding(void)
{
    bv_reset(&A);
    static const struct {
        int kind;
        int64_t x, y;
        const char *hex;
    } e[] = {
        {BV_NOTHING, 0, 0, "00"},
        {BV_INTEGER, 1, 0, "010000000000000001"},
        {BV_INTEGER, -2, 0, "01fffffffffffffffe"},
        {BV_RATIO, -3, 2, "02fffffffffffffffd0000000000000002"},
        {BV_TRUTH, BV_BOTH, 0, "0303"},
    };
    for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) {
        int32_t v = e[i].kind == BV_NOTHING   ? bv_nothing(&A)
                    : e[i].kind == BV_INTEGER ? bv_int(&A, e[i].x)
                    : e[i].kind == BV_RATIO   ? bv_ratio(&A, e[i].x, e[i].y)
                                              : bv_truth(&A, e[i].x);
        int32_t n = bv_encode(&A, v, buf, sizeof buf);
        char hx[128] = {0};
        for (int32_t j = 0; j < n && j < 60; j++) sprintf(hx + 2 * j, "%02x", buf[j]);
        CHECK(strcmp(hx, e[i].hex) == 0, "encoding %u: %s", i, hx);
        CHECK(round_trip(v), "round trip %u", i);
    }
    int32_t names[1] = {S(&A, "a")}, vals[1];
    int32_t inner[2] = {T(&A, "hi"), bv_truth(&A, BV_TRUE)};
    vals[0] = bv_list(&A, inner, 2);
    int32_t r = bv_record(&A, names, vals, 1);
    int32_t n = bv_encode(&A, r, buf, sizeof buf);
    static const uint8_t want[] = {8, 0, 0, 0, 1, 6, 0, 0, 0,   1,   'a', 7, 0,
                                   0, 0, 2, 5, 0, 0, 0, 2, 'h', 'i', 3,   1};
    CHECK(n == (int32_t) sizeof want && memcmp(buf, want, sizeof want) == 0,
          "a record's exact bytes");
    CHECK(round_trip(r), "and back");
    CHECK(bv_encode(&A, -1, buf, sizeof buf) == BV_E_HANDLE &&
              bv_encode(&A, 99999, buf, sizeof buf) == BV_E_HANDLE,
          "encode refuses a bad handle");

    /* every proper prefix and every one-byte extension is refused */
    for (int32_t k = 0; k < n; k++) CHECK(dec(want, (uint32_t) k) < 0, "prefix %d refused", k);
    uint8_t ext[sizeof want + 1];
    memcpy(ext, want, sizeof want);
    ext[sizeof want] = 0;
    CHECK(dec(ext, sizeof ext) == BV_E_FORM && B.nn == 0 && B.nk == 0 && B.nb == 0,
          "a trailing byte; nothing half-built stays");

    /* non-canonical forms */
    static const struct {
        const char *what;
        uint8_t b[24];
        uint32_t n;
    } bad[] = {
        {"unknown kind", {9}, 1},
        {"kind 255", {255}, 1},
        {"truth 4", {3, 4}, 2},
        {"ratio den 1", {2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1}, 17},
        {"ratio den 0", {2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0}, 17},
        {"ratio den < 0", {2, 0, 0, 0, 0, 0, 0, 0, 3, 255, 255, 255, 255, 255, 255, 255, 254}, 17},
        {"ratio not lowest", {2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 4}, 17},
        {"ratio zero", {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4}, 17},
        {"text bad utf-8", {5, 0, 0, 0, 1, 0x80}, 6},
        {"empty symbol", {6, 0, 0, 0, 0}, 5},
        {"length past the end", {5, 0, 0, 0, 2, 'a'}, 6},
        {"count past the end", {7, 0, 0, 0, 2, 0}, 6},
        {"huge count", {7, 255, 255, 255, 255, 0}, 6},
        {"record name not a symbol", {8, 0, 0, 0, 1, 5, 0, 0, 0, 1, 'a', 0}, 12},
        {"record repeated name", {8, 0, 0, 0, 2, 6, 0, 0, 0, 1, 'a', 0, 6, 0, 0, 0, 1, 'a', 0}, 19},
        {"empty input", {0}, 0},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(dec(bad[i].b, bad[i].n) < 0 && B.nn == 0, "refused: %s", bad[i].what);
    static const uint8_t ok[] = {2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 2};
    CHECK(dec(ok, sizeof ok) == 0 && bv_num(&B, 0) == 3 && bv_den(&B, 0) == 2, "3/2 decodes");

    /* nesting past the depth limit in the bytes */
    static uint8_t deep[5 * 70 + 1];
    uint32_t k = 0;
    for (unsigned i = 0; i < 70; i++) {
        deep[k++] = 7;
        deep[k++] = 0;
        deep[k++] = 0;
        deep[k++] = 0;
        deep[k++] = 1;
    }
    deep[k++] = 0;
    CHECK(dec(deep, k) == BV_E_LIMIT, "70 levels in the bytes: LIMIT");
    k = 5 * 64;
    deep[k] = 0;
    CHECK(dec(deep, k + 1) >= 0, "64 levels decode");
    deep[5 * 64] = 7; /* restore */
}

static void test_witness(void)
{
    bv_reset(&A);
    bv_reset(&B);
    int32_t a[2] = {bv_int(&A, 4), D(&A, "0.5")};
    int32_t b[2] = {bv_int(&B, 4), bv_ratio(&B, 2, 4)};
    int32_t x = bv_list(&A, a, 2), y = bv_list(&B, b, 2);
    CHECK(bv_witness(&A, x, &B, y) == BV_TRUE, "0.5 and 2/4 agree across arenas: TRUE");
    int32_t c[2] = {bv_int(&B, 4), bv_ratio(&B, 1, 3)};
    CHECK(bv_witness(&A, x, &B, bv_list(&B, c, 2)) == BV_BOTH, "1/2 and 1/3 disagree: BOTH");
    CHECK(bv_witness(&A, x, &B, b[0]) == BV_BOTH, "a list and a number: BOTH");
    CHECK(bv_witness(&A, x, &B, -1) == BV_NEITHER && bv_witness(&A, 9999, &B, y) == BV_NEITHER,
          "a missing side: NEITHER");
    CHECK(!bv_equal(&A, bv_int(&A, 1), &A, bv_truth(&A, BV_TRUE)), "1 is not TRUE");
    CHECK(!bv_equal(&A, bv_nothing(&A), &A, bv_list(&A, 0, 0)), "nothing is not the empty list");
    int32_t n1[1] = {S(&A, "a")}, n2[1] = {S(&A, "b")}, v1[1] = {bv_int(&A, 1)};
    CHECK(!bv_equal(&A, bv_record(&A, n1, v1, 1), &A, bv_record(&A, n2, v1, 1)),
          "field names count");
    CHECK(!bv_equal(&A, T(&A, "ab"), &A, T(&A, "abc")) &&
              !bv_equal(&A, T(&A, "ab"), &A, T(&A, "ac")),
          "text by bytes");
    CHECK(bv_kind(&A, -3) == BV_NOTHING && bv_num(&A, -3) == 0 && bv_den(&A, -3) == 0 &&
              bv_at(&A, -3, 0) == BV_E_HANDLE,
          "a bad handle reads as nothing");
    CHECK(strcmp(bv_status_name(BV_E_FORM), "FORM") == 0 && strcmp(bv_status_name(5), "OK") == 0 &&
              strcmp(bv_status_name(-99), "?") == 0,
          "status names");
}

/* ---- generated values ------------------------------------------------------ */

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static int32_t gen(bv_arena_t *a, unsigned depth)
{
    unsigned k = (unsigned) (rnd() % (depth < 5 ? 10u : 7u));
    char s[16];
    switch (k) {
    case 0:
        return bv_nothing(a);
    case 1:
        return bv_int(a, (int64_t) rnd() >> (rnd() % 64));
    case 2: {
        int64_t d = (int64_t) (rnd() % 1000u) - 500;
        return bv_ratio(a, (int64_t) (rnd() % 100000u) - 50000, d ? d : 7);
    }
    case 3:
        return bv_truth(a, (int64_t) (rnd() % 4u));
    case 4: {
        uint8_t b[8];
        unsigned n = (unsigned) (rnd() % 9u);
        for (unsigned i = 0; i < n; i++) b[i] = (uint8_t) rnd();
        return bv_bytes(a, b, n);
    }
    case 5:
    case 6: {
        unsigned n = 1u + (unsigned) (rnd() % 6u);
        for (unsigned i = 0; i < n; i++) s[i] = (char) ('a' + rnd() % 4u);
        return k == 5 ? bv_text(a, s, n) : bv_symbol(a, s, n);
    }
    case 7: {
        int32_t e[6];
        unsigned n = (unsigned) (rnd() % 6u);
        for (unsigned i = 0; i < n; i++) e[i] = gen(a, depth + 1);
        return bv_list(a, e, n);
    }
    case 9:
        return bv_pair(a, gen(a, depth + 1), gen(a, depth + 1));
    default: {
        int32_t nm[4], v[4];
        unsigned n = (unsigned) (rnd() % 4u);
        for (unsigned i = 0; i < n; i++) {
            s[0] = (char) ('p' + i);
            nm[i] = bv_symbol(a, s, 1);
            v[i] = gen(a, depth + 1);
        }
        return bv_record(a, nm, v, n);
    }
    }
}

static void test_generated(unsigned iters)
{
    unsigned ok = 0, accepted = 0;
    for (unsigned it = 0; it < iters; it++) {
        bv_reset(&A);
        int32_t v = gen(&A, 0);
        if (v < 0) continue;
        if (round_trip(v))
            ok++;
        else
            CHECK(0, "generated value %u round-trips", it);
    }
    CHECK(ok > iters * 9u / 10u, "most generated values are built: %u of %u", ok, iters);
    /* random and mutated bytes: decode never crashes; what it accepts is canonical */
    for (unsigned it = 0; it < iters; it++) {
        uint32_t n;
        if (it & 1u) {
            bv_reset(&A);
            int32_t v = gen(&A, 0);
            int32_t m = v >= 0 ? bv_encode(&A, v, buf, sizeof buf) : 0;
            n = m > 0 ? (uint32_t) m : 1u;
            buf[rnd() % n] ^= (uint8_t) (1u << (rnd() % 8u));
        } else {
            n = (uint32_t) (rnd() % 40u);
            for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t) (rnd() % 10u);
        }
        int32_t w = dec(buf, n);
        if (w < 0) continue;
        accepted++;
        int32_t m = bv_encode(&B, w, buf2, sizeof buf2);
        if (m != (int32_t) n || memcmp(buf, buf2, n) != 0)
            CHECK(0, "accepted bytes %u re-encode", it);
    }
    printf("generated: %u round trips, %u random inputs accepted and canonical\n", ok, accepted);
}

int main(int argc, char **argv)
{
    unsigned iters = argc > 1 ? (unsigned) strtoul(argv[1], 0, 10) : 20000u;
    test_numbers();
    test_text();
    test_structures();
    test_pairs();
    test_limits();
    test_edges();
    test_pair_edges();
    test_encoding();
    test_witness();
    test_generated(iters);
    printf("%u checks, %u failures\n", checks, fails);
    return fails ? 1 : 0;
}
