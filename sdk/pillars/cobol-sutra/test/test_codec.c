/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_codec.c — record bytes <-> exact values: hand-computed byte
 * vectors for every encoding and sign convention, refusals of bad bytes
 * and of values that do not fit, rationals, text, whole records, and a
 * seeded round trip over every usage, sign, digit count and scale. */
#include "zs_test.h"

static const char k_layout[] =
    "record v\n"
    "  p15: s9(13)v99 comp-3\n"
    "  p3: 9(3) comp-3\n"
    "  p4: s9(4) comp-3\n"
    "  b4: s9(4) comp\n"
    "  b9: 9(9) comp\n"
    "  b4u: 9(4) comp\n"
    "  b18: s9(18) comp\n"
    "  b18u: 9(18) comp\n"
    "  z7: s9(5)v99\n"
    "  zl: s9(5) sign leading\n"
    "  zls: s9(5) sign leading separate\n"
    "  zts: s9(5) sign trailing separate\n"
    "  u5: 9(3)v99\n"
    "  t: x(8)\n"
    "  g:\n"
    "    g1: x(2)\n"
    "    g2: 9(2)\n";

static uint8_t g_rec[256];

static void set_bytes(const zs_layout *l, const char *ref, const char *hex) {
    uint64_t off = 0;
    if (zs_find(l, ref, NULL, &off) == ZXV_OK) unhex(hex, g_rec + off, sizeof g_rec - off);
}

static const char *hex_of(const zs_layout *l, const char *ref) {
    static char b[128];
    uint64_t off = 0;
    uint32_t idx = 0;
    zs_field f;
    size_t k = 0;
    b[0] = '\0';
    if (zs_find(l, ref, &idx, &off) != ZXV_OK || zs_field_at(l, idx, &f) != ZXV_OK) return "?";
    for (uint64_t i = 0; i < f.size && k + 3u < sizeof b; i++)
        k += (size_t) snprintf(b + k, sizeof b - k, "%02x", g_rec[off + i]);
    return b;
}

static void vectors(zs_layout *l) {
    /* field, style, value, bytes: each value encodes to exactly these
     * bytes and these bytes decode to exactly this value */
    static const struct {
        const char *ref;
        int32_t style;
        const char *value, *hex;
    } v[] = {
        {"p15", 0, "1234.56", "000000000123456c"},
        {"p15", 0, "-1234.56", "000000000123456d"},
        {"p15", 0, "9999999999999.99", "999999999999999c"},
        {"p15", 0, "-0.01", "000000000000001d"},
        {"p3", 0, "7", "007f"},
        {"p3", 0, "999", "999f"},
        {"p4", 0, "1234", "01234c"},
        {"p4", 0, "-1", "00001d"},
        {"b4", 0, "-2", "fffe"},
        {"b4", 0, "9999", "270f"},
        {"b4", 0, "-9999", "d8f1"},
        {"b9", 0, "123456789", "075bcd15"},
        {"b18", 0, "-999999999999999999", "f21f494c589c0001"},
        {"b18u", 0, "999999999999999999", "0de0b6b3a763ffff"},
        {"z7", ZS_OVERPUNCH_IBM, "-123.45", "303031323334" "4e"},  /* N = -5 */
        {"z7", ZS_OVERPUNCH_IBM, "123.45", "303031323334" "45"},   /* E = +5 */
        {"z7", ZS_OVERPUNCH_IBM, "0.00", "303030303030" "7b"},     /* { = +0 */
        {"z7", ZS_OVERPUNCH_IBM, "-0.10", "303030303031" "7d"},    /* } = -0 */
        {"z7", ZS_OVERPUNCH_ASCII, "-123.45", "303031323334" "75"}, /* u = -5 */
        {"z7", ZS_OVERPUNCH_ASCII, "123.45", "303031323334" "35"},
        {"zl", ZS_OVERPUNCH_IBM, "-42", "7d30303432"},
        {"zl", ZS_OVERPUNCH_IBM, "91234", "4931323334"},           /* I = +9 */
        {"zl", ZS_OVERPUNCH_ASCII, "-42", "7030303432"},
        {"zls", 0, "-42", "2d3030303432"},
        {"zts", 0, "42", "3030303432" "2b"},
        {"u5", 0, "1.50", "3030313530"},
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        char got[4096];
        zs_layout_set_overpunch(l, v[i].style);
        zs_record_init(l, g_rec, sizeof g_rec);
        CHECK(zs_put_value(l, g_rec, sizeof g_rec, v[i].ref, v[i].value, strlen(v[i].value)) ==
                  ZXV_OK,
              "put %s = %s", v[i].ref, v[i].value);
        CHECK(strcmp(hex_of(l, v[i].ref), v[i].hex) == 0, "%s = %s encodes to %s, expected %s",
              v[i].ref, v[i].value, hex_of(l, v[i].ref), v[i].hex);
        zs_record_init(l, g_rec, sizeof g_rec);
        set_bytes(l, v[i].ref, v[i].hex);
        snprintf(got, sizeof got, "%s", val(l, g_rec, sizeof g_rec, v[i].ref));
        CHECK(strcmp(got, v[i].value) == 0, "%s bytes %s decode to %s, expected %s", v[i].ref,
              v[i].hex, got, v[i].value);
    }
    /* IBM also reads a plain digit (F zone) as positive */
    zs_layout_set_overpunch(l, ZS_OVERPUNCH_IBM);
    set_bytes(l, "z7", "30303132333435");
    CHECK(strcmp(val(l, g_rec, sizeof g_rec, "z7"), "123.45") == 0, "unsigned zone reads +");
    /* COMP-3 accepts IBM's alternative sign nibbles A, E (+) and B (-) */
    set_bytes(l, "p4", "01234a");
    CHECK(strcmp(val(l, g_rec, sizeof g_rec, "p4"), "1234") == 0, "sign A is +");
    set_bytes(l, "p4", "01234b");
    CHECK(strcmp(val(l, g_rec, sizeof g_rec, "p4"), "-1234") == 0, "sign B is -");
    set_bytes(l, "p3", "123c");
    CHECK(strcmp(val(l, g_rec, sizeof g_rec, "p3"), "123") == 0, "unsigned packed takes C");
}

static void bad_bytes(zs_layout *l) {
    static const struct {
        const char *ref;
        int32_t style;
        const char *hex, *why;
    } v[] = {
        {"p4", 0, "10000c", "the pad nibble of an even-digit packed field must be 0"},
        {"p3", 0, "0a0f", "a packed digit above 9"},
        {"p3", 0, "0075", "packed sign nibble 5"},
        {"p3", 0, "007d", "a negative sign in an unsigned packed field"},
        {"b4u", 0, "2710", "10000 in a 9(4) COMP (TRUNC(STD))"},
        {"b4", 0, "d8f0", "-10000 in a S9(4) COMP"},
        {"b18u", 0, "ffffffffffffffff", "2^64-1 in a 9(18) COMP"},
        {"b18", 0, "8000000000000000", "INT64_MIN in a S9(18) COMP"},
        {"z7", ZS_OVERPUNCH_IBM, "20203132333435", "spaces in a numeric field (S0C7)"},
        {"z7", ZS_OVERPUNCH_IBM, "30303132333475", "an ASCII overpunch read as IBM"},
        {"z7", ZS_OVERPUNCH_ASCII, "3030313233344e", "an IBM overpunch read as ASCII"},
        {"z7", ZS_OVERPUNCH_IBM, "30304a32333435", "an overpunch in the middle"},
        {"zls", 0, "203030303432", "a blank separate sign"},
        {"zts", 0, "3030302d342b", "a sign where a digit belongs"},
        {"u5", 0, "303031357d", "an overpunch on an unsigned field"},
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        int64_t u = 99;
        uint32_t sc = 99;
        zs_layout_set_overpunch(l, v[i].style);
        zs_record_init(l, g_rec, sizeof g_rec);
        set_bytes(l, v[i].ref, v[i].hex);
        CHECK(zs_get_decimal(l, g_rec, sizeof g_rec, v[i].ref, &u, &sc) == ZS_E_DATA,
              "%s: %s is ZS_E_DATA", v[i].ref, v[i].why);
        CHECK(u == 99 && sc == 99, "%s: outputs untouched on failure", v[i].ref);
        CHECK(strstr(last_err(), "") != NULL && last_err()[0] != '\0', "a message");
        CHECK(zs_record_check(l, g_rec, sizeof g_rec) == ZS_E_DATA, "record check finds it");
    }
    zs_layout_set_overpunch(l, ZS_OVERPUNCH_IBM);
}

static void fits(zs_layout *l) {
    uint8_t before[256];
    int64_t n = 0, d = 0;
    zs_record_init(l, g_rec, sizeof g_rec);
    /* rescaling is exact or refused */
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", 12345, 1) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "z7"), "1234.50") == 0,
          "1234.5 into V99 is 1234.50");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", 1230, 3) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "z7"), "1.23") == 0,
          "1.230 into V99: the extra digit is zero");
    memcpy(before, g_rec, sizeof before);
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", 1234, 3) == ZS_E_FIT, "1.234 into V99");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", 10000000, 2) == ZS_E_FIT, "100000.00 into S9(5)V99");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "u5", -1, 0) == ZS_E_FIT, "negative into unsigned");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", INT64_MAX, 0) == ZS_E_FIT, "overflow on rescale");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", INT64_MIN, 0) == ZXV_E_ARG, "INT64_MIN");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "z7", 1, 19) == ZXV_E_ARG, "scale 19");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "b4u", 10000, 0) == ZS_E_FIT, "10000 into 9(4) COMP");
    CHECK(zs_put_decimal(l, g_rec, sizeof g_rec, "t", 1, 0) == ZXV_E_ARG, "number into PIC X");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "-0.001", 6) == ZS_E_FIT, "text -0.001");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "100000", 6) == ZS_E_FIT, "text 100000");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "1e5", 3) == ZXV_E_FORMAT, "exponent");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "", 0) == ZXV_E_FORMAT, "empty text");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "NaN", 3) == ZXV_E_FORMAT, "NaN");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "t", "123456789", 9) == ZS_E_FIT, "text too long");
    CHECK(zs_put_rat(l, g_rec, sizeof g_rec, "z7", 1, 3) == ZS_E_FIT, "1/3 into V99");
    CHECK(zs_put_rat(l, g_rec, sizeof g_rec, "z7", 1, 0) == ZXV_E_ARG, "zero denominator");
    /* the double nearest 0.1 is not 0.1: refused, never rounded */
    CHECK(zxv_rat_from_ieee754(UINT64_C(0x3FB999999999999A), &n, &d) == ZXV_OK, "0.1 bits");
    CHECK(zs_put_rat(l, g_rec, sizeof g_rec, "z7", n, d) == ZS_E_FIT,
          "the double 0.1 (%lld/%lld) does not fit V99", (long long) n, (long long) d);
    CHECK(memcmp(before, g_rec, sizeof before) == 0, "nothing is written by a refused put");

    CHECK(zs_put_rat(l, g_rec, sizeof g_rec, "z7", 1, 4) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "z7"), "0.25") == 0,
          "1/4 into V99 is 0.25");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "-3/4", 4) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "z7"), "-0.75") == 0,
          "text -3/4 is -0.75");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "z7", "-123.45", 7) == ZXV_OK, "-123.45");
    CHECK(zs_get_rat(l, g_rec, sizeof g_rec, "z7", &n, &d) == ZXV_OK && n == -2469 && d == 20,
          "-123.45 is -2469/20, got %lld/%lld", (long long) n, (long long) d);
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "t", "ABC", 3) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "t"), "ABC     ") == 0,
          "text is padded with spaces");
    CHECK(zs_put_value(l, g_rec, sizeof g_rec, "g", "XY07", 4) == ZXV_OK &&
              strcmp(val(l, g_rec, sizeof g_rec, "g2"), "7") == 0,
          "a group takes bytes; its items read them");
    CHECK(strcmp(val(l, g_rec, sizeof g_rec, "g"), "XY07") == 0, "a group reads as its bytes");
    {
        int64_t u;
        uint32_t sc;
        size_t need = 0;
        char tiny[4];
        CHECK(zs_get_decimal(l, g_rec, sizeof g_rec, "t", &u, &sc) == ZXV_E_ARG, "PIC X is not numeric");
        CHECK(zs_get_decimal(l, g_rec, 3, "z7", &u, &sc) == ZXV_E_ARG, "short record");
        CHECK(zs_get_decimal(l, NULL, sizeof g_rec, "z7", &u, &sc) == ZXV_E_ARG, "NULL record");
        CHECK(zs_get_value(l, g_rec, sizeof g_rec, "z7", NULL, 0, &need) == ZXV_E_BUFFER &&
                  need == 7,
              "R4: -123.45 needs 7 bytes");
        CHECK(zs_get_value(l, g_rec, sizeof g_rec, "z7", tiny, sizeof tiny, &need) == ZXV_E_BUFFER,
              "R4 small buffer");
        CHECK(zs_get_value(l, g_rec, sizeof g_rec, "nope", tiny, sizeof tiny, &need) ==
                  ZXV_E_NOT_FOUND,
              "unknown field");
    }
}

static void records(void) {
    zs_layout *l = sutra("record s\n  n: 9(4) comp\n  line[3]:\n    amt: s9(5)v99 sign leading separate\n"
                         "    q: s9(3) comp-3\n    d: x(2)\n");
    uint8_t r[64];
    uint64_t off = 0;
    CHECK(l != NULL, "table layout");
    if (!l) return;
    CHECK(zs_record_init(l, r, sizeof r) == ZXV_OK, "init");
    CHECK(zs_record_check(l, r, sizeof r) == ZXV_OK, "an initial record is valid");
    CHECK(strcmp(val(l, r, sizeof r, "amt(3)"), "0.00") == 0, "every occurrence is zero");
    CHECK(strcmp(val(l, r, sizeof r, "d(2)"), "  ") == 0, "text is spaces");
    CHECK(zs_put_value(l, r, sizeof r, "amt(2)", "-1.25", 5) == ZXV_OK, "put amt(2)");
    CHECK(strcmp(val(l, r, sizeof r, "amt(2)"), "-1.25") == 0 &&
              strcmp(val(l, r, sizeof r, "amt(1)"), "0.00") == 0,
          "only amt(2) changed");
    zs_find(l, "q(3)", NULL, &off);
    r[off + 1u] = 0x05; /* sign nibble 5 */
    CHECK(zs_record_check(l, r, sizeof r) == ZS_E_DATA, "a bad q(3) is found");
    CHECK(strstr(last_err(), "offset") != NULL, "the message names the offset: %s", last_err());
    CHECK(zs_record_init(l, r, 4) == ZXV_E_ARG, "init needs the whole record");
    zs_layout_destroy(l);
}

/* seeded round trip: every usage x sign x digits x scale */
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static void round_trip(void) {
    static const char *const kinds[] = {"",       " sign leading", " sign leading separate",
                                        " sign trailing separate", " comp", " comp-3"};
    unsigned layouts = 0, values = 0;
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++)
        for (int sgn = 0; sgn < 2; sgn++)
            for (uint32_t dg = 1; dg <= 18; dg++)
                for (int sci = 0; sci < 3; sci++) {
                    uint32_t sc = sci == 0 ? 0 : sci == 1 ? dg / 2u : dg;
                    char text[160], pic[48];
                    zs_layout *l;
                    uint8_t r[32];
                    zs_diag d;
                    int pk = 0;
                    if (!sgn && k >= 1 && k <= 3) continue; /* sign clauses need S */
                    pk += snprintf(pic + pk, sizeof pic - (size_t) pk, "%s", sgn ? "s" : "");
                    if (dg > sc) pk += snprintf(pic + pk, sizeof pic - (size_t) pk, "9(%u)", (unsigned) (dg - sc));
                    if (sc) snprintf(pic + pk, sizeof pic - (size_t) pk, "v9(%u)", (unsigned) sc);
                    snprintf(text, sizeof text, "record r\n  a: %s%s\n  b: x\n", pic, kinds[k]);
                    l = NULL;
                    if (zs_sutra_parse(text, strlen(text), &l, &d) != ZXV_OK) {
                        CHECK(0, "layout %s: %s", text, d.msg);
                        continue;
                    }
                    layouts++;
                    for (int style = 0; style < 2; style++) {
                        zs_layout_set_overpunch(l, style);
                        for (int i = 0; i < 40; i++) {
                            int64_t lim = zs_test_pow10(dg) - 1, v, got = 0;
                            uint32_t gsc = 0;
                            v = i == 0 ? lim : i == 1 ? 0 : (int64_t) (rnd() % (uint64_t) (lim + 1));
                            if (sgn && (rnd() & 1u)) v = -v;
                            zs_record_init(l, r, sizeof r);
                            CHECK(zs_put_decimal(l, r, sizeof r, "a", v, sc) == ZXV_OK, "put %lld", (long long) v);
                            CHECK(zs_get_decimal(l, r, sizeof r, "a", &got, &gsc) == ZXV_OK && got == v &&
                                      gsc == sc,
                                  "%s: %lld came back as %lld", text, (long long) v, (long long) got);
                            CHECK(zs_record_check(l, r, sizeof r) == ZXV_OK, "record valid");
                            if (lim < INT64_MAX / 10) /* one digit too many */
                                CHECK(zs_put_decimal(l, r, sizeof r, "a", (lim + 1) * (v < 0 ? -1 : 1), sc) ==
                                          ZS_E_FIT,
                                      "%s: 10^digits does not fit", text);
                            values++;
                        }
                    }
                    zs_layout_destroy(l);
                }
    printf("round trip: %u layouts, %u values\n", layouts, values);
}

int main(void) {
    zs_layout *l = sutra(k_layout);
    CHECK(l != NULL, "codec layout");
    if (l) {
        vectors(l);
        bad_bytes(l);
        fits(l);
        zs_layout_destroy(l);
    }
    records();
    round_trip();
    return report("test_codec");
}
