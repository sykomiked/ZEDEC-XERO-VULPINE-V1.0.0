/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_codec.c — the canonical encoding and the exact codecs: fixed byte
 * vectors for every width and sign, paths into arrays and records, a
 * seeded round trip of random in-range values, and every refusal (a value
 * outside its range or scale, a bad path, a short buffer) leaving the
 * bytes untouched. */
#include "ze_test.h"

static const char k_layout[] =
    "eve codec\n"
    "u8 = 0 .. 255\n"
    "s8 = -128 .. 127\n"
    "u16 = 0 .. 65535\n"
    "s16 = -32768 .. 32767\n"
    "u32 = 0 .. 2**32 - 1\n"
    "s32 = -2**31 .. 2**31 - 1\n"
    "s64 = -2**63 .. 2**63 - 1\n"
    "u33 = 0 .. 2**32\n"
    "money = dec(15, 2)\n"
    "tiny = dec(2, 1)\n"
    "ccy = (ngn, ghs, kes)\n"
    "grid = [ccy, 2] s16\n"
    "inner { a: u8; b: s8 }\n"
    "outer {\n"
    "  name: str(4)\n"
    "  i: inner\n"
    "  g: grid\n"
    "  m: money\n"
    "  w: s64\n"
    "  t: tiny -0.5 .. 0.5\n"
    "  c: ccy ghs .. kes\n"
    "  f: boolean\n"
    "}\n";

typedef struct {
    const char *type, *value, *bytes;
} vec;

/* each type's encoding of one value, as hex */
static const vec k_vectors[] = {
    {"u8", "0", "00"},
    {"u8", "255", "ff"},
    {"s8", "-1", "ff"},
    {"s8", "-128", "80"},
    {"u16", "258", "0102"},
    {"s16", "-2", "fffe"},
    {"u32", "4294967295", "ffffffff"},
    {"s32", "-2147483648", "80000000"},
    {"s64", "-9223372036854775808", "8000000000000000"},
    {"s64", "9223372036854775807", "7fffffffffffffff"},
    {"u33", "4294967296", "0000000100000000"},
    {"money", "-0.01", "ffffffffffffffff"},
    {"money", "1234.56", "000000000001e240"},
    {"money", "9999999999999.99", "00038d7ea4c67fff"},
    {"tiny", "-9.9", "9d"},
    {"ccy", "kes", "02"},
    {"inner", NULL, "0000"},
};

static void test_vectors(ze_pkg *p) {
    uint8_t v[256], w[256];
    for (size_t i = 0; i < sizeof k_vectors / sizeof k_vectors[0]; i++) {
        const vec *x = &k_vectors[i];
        uint32_t t = type_of(p, x->type);
        size_t n = (size_t) size_of(p, t);
        memset(v, 0xAA, sizeof v);
        CHECK(n * 2 == strlen(x->bytes), "%s is %zu bytes", x->type, n);
        CHECK(ze_value_init(p, t, v, n) == ZXV_OK, "init %s", x->type);
        if (x->value) {
            CHECK(put(p, t, v, n, "", x->value) == ZXV_OK, "put %s %s", x->type, x->value);
            CHECK(strcmp(get(p, t, v, n, ""), x->value) == 0, "get %s: %s, want %s", x->type, get(p, t, v, n, ""),
                  x->value);
        }
        CHECK(strcmp(hex(v, n), x->bytes) == 0, "%s %s encodes as %s, want %s", x->type, x->value ? x->value : "init",
              hex(v, n), x->bytes);
        CHECK(v[n] == 0xAA, "%s: nothing written past the value", x->type);
        /* decoding the bytes gives the value back */
        memcpy(w, v, n);
        CHECK(ze_value_check(p, t, w, n) == ZXV_OK, "check %s", x->type);
    }
}

static void test_paths(ze_pkg *p) {
    uint32_t t = type_of(p, "outer"), ft;
    uint64_t off = 0;
    size_t n = (size_t) size_of(p, t);
    uint8_t v[128], w[128];
    int64_t units = 0, num = 0, den = 0;
    uint32_t scale = 0, pos = 0;
    CHECK(n == 4 + 2 + 12 + 8 + 8 + 1 + 1 + 1, "outer is %zu bytes", n);
    CHECK(ze_value_init(p, t, v, n) == ZXV_OK, "init");
    CHECK(strcmp(get(p, t, v, n, "t"), "0.0") == 0 && strcmp(get(p, t, v, n, "c"), "ghs") == 0 &&
              strcmp(get(p, t, v, n, "f"), "false") == 0 && strcmp(get(p, t, v, n, "name"), "    ") == 0,
          "initial values: %s %s %s", get(p, t, v, n, "t"), get(p, t, v, n, "c"), get(p, t, v, n, "f"));
    CHECK(ze_find(p, t, "g(kes, 2)", &ft, &off) == ZXV_OK && ft == type_of(p, "s16") && off == 6 + 10,
          "g(kes, 2) is the last s16 of the grid, at %llu", (unsigned long long) off);
    CHECK(ze_find(p, t, "I.B", &ft, &off) == ZXV_OK && off == 5, "names are case-insensitive");
    CHECK(ze_find(p, t, " g ( ngn , 1 ) ", &ft, &off) == ZXV_OK && off == 6, "spaces in a path");
    CHECK(put(p, t, v, n, "g(ghs, 1)", "-300") == ZXV_OK && strcmp(get(p, t, v, n, "g(GHS, 1)"), "-300") == 0,
          "an enumeration index is a literal");
    CHECK(strcmp(get(p, t, v, n, "g(1, 1)"), "<ZXV_E_FORMAT>") == 0, "not a position: %s", get(p, t, v, n, "g(1, 1)"));
    CHECK(put(p, t, v, n, "name", "AB") == ZXV_OK && strcmp(get(p, t, v, n, "name"), "AB  ") == 0, "text padded");
    CHECK(ze_put_decimal(p, t, v, n, "m", 150, 2) == ZXV_OK && ze_get_decimal(p, t, v, n, "m", &units, &scale) ==
              ZXV_OK && units == 150 && scale == 2,
          "decimal units");
    CHECK(ze_put_decimal(p, t, v, n, "m", 15, 1) == ZXV_OK && ze_get_decimal(p, t, v, n, "m", &units, &scale) ==
              ZXV_OK && units == 150,
          "a coarser scale is exact");
    CHECK(ze_put_decimal(p, t, v, n, "m", 15000, 4) == ZXV_OK, "extra zero decimals are exact");
    CHECK(ze_put_rat(p, t, v, n, "m", -3, 4) == ZXV_OK && ze_get_rat(p, t, v, n, "m", &num, &den) == ZXV_OK &&
              num == -3 && den == 4 && strcmp(get(p, t, v, n, "m"), "-0.75") == 0,
          "a rational in and out");
    CHECK(ze_put_enum(p, t, v, n, "c", 2) == ZXV_OK && ze_get_enum(p, t, v, n, "c", &pos) == ZXV_OK && pos == 2,
          "enumeration position");
    CHECK(put(p, t, v, n, "f", "TRUE") == ZXV_OK && strcmp(get(p, t, v, n, "f"), "true") == 0, "Boolean");
    CHECK(ze_value_check(p, t, v, n) == ZXV_OK, "valid");

    /* refusals leave the value as it was */
    memcpy(w, v, n);
    CHECK(put(p, t, v, n, "m", "0.001") == ZE_E_FIT, "more decimals than the type");
    CHECK(ze_put_rat(p, t, v, n, "m", 1, 3) == ZE_E_FIT, "1/3 is not a multiple of 0.01");
    CHECK(put(p, t, v, n, "m", "10000000000000.00") == ZE_E_FIT, "16 digits");
    CHECK(put(p, t, v, n, "t", "0.6") == ZE_E_FIT, "outside -0.5 .. 0.5");
    CHECK(ze_put_enum(p, t, v, n, "c", 0) == ZE_E_FIT, "ngn is outside ghs .. kes");
    CHECK(put(p, t, v, n, "c", "ngn") == ZE_E_FIT, "ngn by name");
    CHECK(put(p, t, v, n, "c", "eur") == ZE_E_FIT, "not a literal");
    CHECK(put(p, t, v, n, "name", "ABCDE") == ZE_E_FIT, "longer than str(4) is never truncated");
    CHECK(put(p, t, v, n, "i.a", "256") == ZE_E_FIT, "256 into 0 .. 255");
    CHECK(put(p, t, v, n, "i.a", "1e2") == ZXV_E_FORMAT, "no exponents");
    CHECK(put(p, t, v, n, "i.a", "") == ZXV_E_FORMAT, "empty text");
    CHECK(put(p, t, v, n, "g(ngn, 3)", "1") == ZXV_E_RANGE, "index 3 of 1 .. 2");
    CHECK(put(p, t, v, n, "g(eur, 1)", "1") == ZXV_E_RANGE, "an unknown index literal");
    CHECK(put(p, t, v, n, "g(ngn)", "1") == ZXV_E_FORMAT, "too few indexes");
    CHECK(put(p, t, v, n, "g(ngn, 1, 1)", "1") == ZXV_E_FORMAT, "too many indexes");
    CHECK(put(p, t, v, n, "i.c", "1") == ZXV_E_NOT_FOUND, "no component c");
    CHECK(put(p, t, v, n, "i", "1") == ZXV_E_ARG, "a record is not a scalar");
    CHECK(put(p, t, v, n, "g(ngn, 1", "1") == ZXV_E_FORMAT, "unclosed");
    CHECK(put(p, t, v, n, "i..a", "1") == ZXV_E_FORMAT, "empty name");
    CHECK(ze_put_decimal(p, t, v, n, "name", 1, 0) == ZXV_E_ARG, "a String is not a number");
    CHECK(ze_put_value(p, t, v, n - 1, "m", "1", 1) == ZXV_E_ARG, "a short buffer");
    CHECK(ze_get_enum(p, t, v, n, "m", &pos) == ZXV_E_ARG, "money is not an enumeration");
    CHECK(memcmp(v, w, n) == 0, "every refusal left the bytes unchanged");

    /* a bad byte is found by check, with its path */
    v[n - 2] = 0; /* c is the second-last byte: ngn is outside ghs .. kes */
    CHECK(ze_value_check(p, t, v, n) == ZE_E_DATA && strstr(last_err(), "c") != NULL, "check names c: %s",
          last_err());
    memcpy(v, w, n);
    v[n - 1] = 2; /* f: Boolean is 0 or 1 */
    CHECK(ze_value_check(p, t, v, n) == ZE_E_DATA && strstr(last_err(), "f") != NULL, "a Boolean byte of 2");
    memcpy(v, w, n);
    v[0] = 0xC3; /* name: one byte of a two-byte sequence is still a byte */
    CHECK(ze_value_check(p, t, v, n) == ZXV_OK, "String bytes are not checked as UTF-8");
}

/* xorshift64: a fixed stream, so a failure is reproducible */
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static void test_round_trip(ze_pkg *p) {
    static const char *const scalars[] = {"u8", "s8", "u16", "s16", "u32", "s32", "s64", "u33", "money", "tiny"};
    uint8_t v[16];
    char text[64];
    int bad = 0;
    for (int it = 0; it < 20000; it++) {
        uint32_t t = type_of(p, scalars[rnd() % (sizeof scalars / sizeof scalars[0])]);
        ze_type ti;
        int64_t units, got = 0;
        uint32_t scale = 0;
        uint64_t span;
        size_t n;
        if (ze_type_at(p, t, &ti) != ZXV_OK) abort();
        n = (size_t) ti.size;
        span = (uint64_t) ti.hi - (uint64_t) ti.lo;
        units = (int64_t) ((uint64_t) ti.lo + (span == UINT64_MAX ? rnd() : rnd() % (span + 1u)));
        if (ze_put_decimal(p, t, v, n, "", units, ti.scale) != ZXV_OK ||
            ze_get_decimal(p, t, v, n, "", &got, &scale) != ZXV_OK || got != units || scale != ti.scale) {
            if (bad++ < 5) CHECK(0, "round trip %s %lld", ti.name, (long long) units);
            continue;
        }
        /* text form round-trips too, and agrees with the integer form */
        {
            size_t k = 0, len = 0;
            uint8_t w[16];
            if (ze_get_value(p, t, v, n, "", text, sizeof text, &len) != ZXV_OK ||
                ze_put_value(p, t, w, n, "", text, len) != ZXV_OK || memcmp(v, w, n) != 0) {
                if (bad++ < 5) CHECK(0, "text round trip %s %s", ti.name, text);
            }
            (void) k;
        }
        /* one past either end is refused */
        if (units == ti.hi && ti.hi < INT64_MAX && ze_put_decimal(p, t, v, n, "", units + 1, ti.scale) != ZE_E_FIT)
            if (bad++ < 5) CHECK(0, "hi + 1 accepted for %s", ti.name);
    }
    CHECK(bad == 0, "20000 random values round-trip exactly (%d bad)", bad);
    /* the ends of every range */
    for (size_t i = 0; i < sizeof scalars / sizeof scalars[0]; i++) {
        uint32_t t = type_of(p, scalars[i]);
        ze_type ti;
        int64_t got = 0;
        uint32_t sc = 0;
        if (ze_type_at(p, t, &ti) != ZXV_OK) abort();
        CHECK(ze_put_decimal(p, t, v, ti.size, "", ti.lo, ti.scale) == ZXV_OK &&
                  ze_get_decimal(p, t, v, ti.size, "", &got, &sc) == ZXV_OK && got == ti.lo,
              "%s lo", ti.name);
        CHECK(ze_put_decimal(p, t, v, ti.size, "", ti.hi, ti.scale) == ZXV_OK &&
                  ze_get_decimal(p, t, v, ti.size, "", &got, &sc) == ZXV_OK && got == ti.hi,
              "%s hi", ti.name);
        if (ti.lo > INT64_MIN)
            CHECK(ze_put_decimal(p, t, v, ti.size, "", ti.lo - 1, ti.scale) == ZE_E_FIT, "%s lo - 1", ti.name);
    }
}

int main(void) {
    ze_pkg *p = parse(0, k_layout);
    CHECK(p != NULL, "the codec layout parses");
    if (!p) return report("test_codec");
    test_vectors(p);
    test_paths(p);
    test_round_trip(p);
    ze_pkg_destroy(p);
    return report("test_codec");
}
