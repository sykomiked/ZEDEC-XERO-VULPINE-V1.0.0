/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_number.c — M canonical numbers, exact decimals and $H dates:
 * hand vectors, every refusal kind, a seeded round trip over every scale,
 * and every $H day from 1840-12-31 to 9999-12-31. */
#include "zm_internal.h"
#include "zm_test.h"

static void canonic_vectors(void) {
    static const char *yes[] = {"0", "1", "-1", "7", "10", "100", "-2.5", ".5", "-.5", "1.5",
                                "123456789012345678", "-123456789012345678",
                                "12345678901234567.8", ".123456789012345678",
                                "123456789012345678000", "1000000000000000000000",
                                "10000000000000000000000000000000000000000000000",
                                ".0000000000000000000000000000000000000000001",
                                ".0000000000000000000000000000000000000000009", "2147483648",
                                "999999999999999999", ".000000000000000001"};
    static const char *no[] = {"", "-", ".", "-.", "-0", "00", "01", "0.5", "-0.5", "1.0", "1.",
                               ".50", "+1", " 1", "1 ", "1E3", "1e3", "0x10", "1,5", "1.2.3",
                               "--1", "1-", "abc", "1234567890123456789",
                               ".1234567890123456789", "-1234567890123456789",
                               "9223372036854775807",
                               "100000000000000000000000000000000000000000000000",
                               "99999999999999999999999999999999999999999999999",
                               ".00000000000000000000000000000000000000000001", "-0.0", "0.0",
                               "\xc2\xbd", "1\n"};
    for (size_t i = 0; i < sizeof yes / sizeof *yes; i++)
        CHECK(zm_is_canonic(yes[i], strlen(yes[i])) == 1, "%s is canonical", yes[i]);
    for (size_t i = 0; i < sizeof no / sizeof *no; i++)
        CHECK(zm_is_canonic(no[i], strlen(no[i])) == 0, "'%s' is not canonical", no[i]);
    CHECK(zm_is_canonic(NULL, 0) == 0, "NULL");
    CHECK(zm_is_canonic("12", 1) == 1, "length is honoured: \"1\"");
}

static void decimal_vectors(void) {
    static const struct {
        int64_t v;
        uint32_t sc;
        const char *m, *t;
    } k[] = {
        {0, 0, "0", "0"},           {0, 2, "0", "0.00"},          {1250, 2, "12.5", "12.50"},
        {-25, 2, "-.25", "-0.25"},  {5, 1, ".5", "0.5"},          {100000, 2, "1000", "1000.00"},
        {7, 0, "7", "7"},           {-7, 0, "-7", "-7"},          {1, 18, ".000000000000000001", "0.000000000000000001"},
        {INT64_MAX, 0, "9223372036854775807", "9223372036854775807"},
        {INT64_MIN, 0, "-9223372036854775808", "-9223372036854775808"},
        {INT64_MIN, 18, "-9.223372036854775808", "-9.223372036854775808"},
        {123456789012345678LL, 4, "12345678901234.5678", "12345678901234.5678"},
        {-1000, 3, "-1", "-1.000"},
    };
    char b[32];
    for (size_t i = 0; i < sizeof k / sizeof *k; i++) {
        size_t n = zm_dec_mtext(k[i].v, k[i].sc, b);
        CHECK(strcmp(b, k[i].m) == 0 && n == strlen(b), "mtext %lld/10^%u: %s, want %s",
              (long long) k[i].v, (unsigned) k[i].sc, b, k[i].m);
        n = zm_dec_text(k[i].v, k[i].sc, b);
        CHECK(strcmp(b, k[i].t) == 0 && n == strlen(b), "text %lld/10^%u: %s, want %s",
              (long long) k[i].v, (unsigned) k[i].sc, b, k[i].t);
    }
}

static void to_dec_vectors(void) {
    int64_t v = 0;
    CHECK(zm_cnum_to_dec("12.5", 4, 15, 2, &v) == ZXV_OK && v == 1250, "12.5 at 2: %lld", (long long) v);
    CHECK(zm_cnum_to_dec("-.25", 4, 15, 2, &v) == ZXV_OK && v == -25, "-.25");
    CHECK(zm_cnum_to_dec("0", 1, 1, 0, &v) == ZXV_OK && v == 0, "0 in 1 digit");
    CHECK(zm_cnum_to_dec("9999999999999.99", 16, 15, 2, &v) == ZXV_OK && v == 999999999999999LL, "max of n15.2");
    CHECK(zm_cnum_to_dec("10000000000000", 14, 15, 2, &v) == ZM_E_FIT, "14 integer digits in n15.2");
    CHECK(zm_cnum_to_dec("1.005", 5, 15, 2, &v) == ZM_E_FIT, "3 decimals in n15.2");
    CHECK(zm_cnum_to_dec("12.50", 5, 15, 2, &v) == ZM_E_DATA, "12.50 is not canonical");
    CHECK(zm_cnum_to_dec("1.5", 3, 9, 0, &v) == ZM_E_FIT, "1.5 in an integer");
    CHECK(zm_cnum_to_dec("999999999999999999", 18, 18, 0, &v) == ZXV_OK &&
              v == 999999999999999999LL, "18 nines");
    CHECK(zm_cnum_to_dec("-999999999999999999", 19, 18, 0, &v) == ZXV_OK &&
              v == -999999999999999999LL, "-18 nines");
    CHECK(zm_cnum_to_dec(".99", 3, 2, 2, &v) == ZXV_OK && v == 99, ".99 in n2.2");
    CHECK(zm_cnum_to_dec("1", 1, 2, 2, &v) == ZM_E_FIT, "1 in n2.2");
    CHECK(zm_cnum_to_dec("1000000000000000000000", 22, 18, 0, &v) == ZM_E_FIT, "1E21 in i18");

    CHECK(zm_dec_parse("+0012.50", 8, 2, &v) == ZXV_OK && v == 1250, "loose +0012.50");
    CHECK(zm_dec_parse("-.5", 3, 2, &v) == ZXV_OK && v == -50, "loose -.5");
    CHECK(zm_dec_parse("7.", 2, 0, &v) == ZXV_OK && v == 7, "loose 7.");
    CHECK(zm_dec_parse("1.230", 5, 2, &v) == ZXV_OK && v == 123, "1.230 at 2: the extra 0 is fine");
    CHECK(zm_dec_parse("1.234", 5, 2, &v) == ZM_E_FIT, "1.234 at 2");
    CHECK(zm_dec_parse("9223372036854775807", 19, 0, &v) == ZXV_OK && v == INT64_MAX, "int64 max");
    CHECK(zm_dec_parse("9223372036854775808", 19, 0, &v) == ZM_E_FIT, "int64 max + 1");
    CHECK(zm_dec_parse("92233720368547758.08", 20, 2, &v) == ZM_E_FIT, "overflow while scaling");
    static const char *bad[] = {"", "+", "-", ".", "1e3", "1 ", " 1", "1.2.3", "--1", "1,5", "0x1"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(zm_dec_parse(bad[i], strlen(bad[i]), 2, &v) == ZXV_E_FORMAT, "loose '%s'", bad[i]);
    CHECK(zm_dec_parse("1", 1, 19, &v) == ZXV_E_FORMAT, "scale 19");
}

/* Every scale and many values: unscaled -> M text -> unscaled. */
static void round_trip(void) {
    char b[32];
    for (uint32_t sc = 0; sc <= 18; sc++) {
        for (int i = 0; i < 4000; i++) {
            uint32_t digits = 1u + (uint32_t) (rnd() % 18u);
            int64_t v = (int64_t) (rnd() % (uint64_t) zm_pow10[digits]);
            int64_t back = 0;
            size_t n;
            if (sc > digits) digits = sc;
            if (digits > 18u) digits = 18u;
            if (rnd() & 1u) v = -v;
            if (i < 3) v = i == 0 ? 0 : (i == 1 ? zm_pow10[digits] - 1 : -(zm_pow10[digits] - 1));
            n = zm_dec_mtext(v, sc, b);
            CHECK(zm_is_canonic(b, n), "M text %s of %lld/10^%u is canonical", b, (long long) v,
                  (unsigned) sc);
            CHECK(zm_cnum_to_dec(b, n, 18, sc, &back) == ZXV_OK && back == v,
                  "%s back to %lld at %u: %lld", b, (long long) v, (unsigned) sc, (long long) back);
            n = zm_dec_text(v, sc, b);
            CHECK(zm_dec_parse(b, n, sc, &back) == ZXV_OK && back == v, "display %s back", b);
        }
    }
}

static void dates(void) {
    int32_t y = 0, m = 0, d = 0;
    int64_t h = -1;
    int bad = 0;
    CHECK(zm_date_from_h(0, &y, &m, &d) == ZXV_OK && y == 1840 && m == 12 && d == 31, "day 0");
    CHECK(zm_date_from_h(1, &y, &m, &d) == ZXV_OK && y == 1841 && m == 1 && d == 1, "day 1");
    CHECK(zm_date_from_h(67854, &y, &m, &d) == ZXV_OK && y == 2026 && m == 10 && d == 11, "today");
    CHECK(zm_date_from_h(58133, &y, &m, &d) == ZXV_OK && y == 2000 && m == 2 && d == 29, "2000-02-29");
    CHECK(zm_date_from_h(ZM_MAX_DATE, &y, &m, &d) == ZXV_OK && y == 9999 && m == 12 && d == 31, "max");
    CHECK(zm_date_from_h(ZM_MAX_DATE + 1, &y, &m, &d) == ZM_E_FIT, "max + 1");
    CHECK(zm_date_from_h(-1, &y, &m, &d) == ZM_E_FIT, "-1");
    CHECK(zm_date_from_h(0, NULL, &m, &d) == ZXV_E_ARG, "NULL out");
    CHECK(zm_date_to_h(1970, 1, 1, &h) == ZXV_OK && h == 47117, "1970-01-01 is 47117");
    CHECK(zm_date_to_h(1900, 2, 29, &h) == ZXV_E_FORMAT, "1900 is not a leap year");
    CHECK(zm_date_to_h(2000, 2, 29, &h) == ZXV_OK && h == 58133, "2000 is");
    CHECK(zm_date_to_h(2026, 13, 1, &h) == ZXV_E_FORMAT, "month 13");
    CHECK(zm_date_to_h(2026, 4, 31, &h) == ZXV_E_FORMAT, "April 31");
    CHECK(zm_date_to_h(2026, 1, 0, &h) == ZXV_E_FORMAT, "day 0");
    CHECK(zm_date_to_h(1840, 12, 30, &h) == ZM_E_FIT, "before day 0");
    CHECK(zm_date_to_h(1839, 1, 1, &h) == ZM_E_FIT, "1839");
    CHECK(zm_date_to_h(10000, 1, 1, &h) == ZM_E_FIT, "10000");
    CHECK(zm_date_to_h(2026, 1, 1, NULL) == ZXV_E_ARG, "NULL out");
    /* every day, both ways, and consecutive */
    {
        int32_t py = 1840, pm = 12, pd = 30;
        for (int64_t k = 0; k <= ZM_MAX_DATE; k++) {
            int64_t back = -1;
            if (zm_date_from_h(k, &y, &m, &d) != ZXV_OK || zm_date_to_h(y, m, d, &back) != ZXV_OK ||
                back != k)
                bad++;
            else if (!((y == py && m == pm && d == pd + 1) || (y == py && m == pm + 1 && d == 1) ||
                       (y == py + 1 && m == 1 && d == 1 && pm == 12 && pd == 31)))
                bad++;
            py = y;
            pm = m;
            pd = d;
        }
    }
    CHECK(bad == 0, "every $H day 0..%d round-trips and follows the day before (%d bad)",
          ZM_MAX_DATE, bad);
}

int main(void) {
    canonic_vectors();
    decimal_vectors();
    to_dec_vectors();
    round_trip();
    dates();
    CHECK(strcmp(zm_status_name(ZM_E_KEY), "ZM_E_KEY") == 0, "status name");
    CHECK(strcmp(zm_status_name(ZXV_E_FUNDS), "ZXV_E_FUNDS") == 0, "libzxv names pass through");
    CHECK(ZM_E_SYNTAX == -2401 && ZM_E_KEY == -2405, "the pillar's range");
    return report("test_number");
}
