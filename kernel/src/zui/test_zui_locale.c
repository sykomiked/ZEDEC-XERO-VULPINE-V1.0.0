/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_locale.c — locales (zui_locale.h L1..L10): lookup and fallback,
 * direction, plurals, numbers, dates, the week, the reader's words, phrasing
 * and the speech shim, and the real screen reader speaking through it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_bidi.h"
#include "zui_locale.h"

#include "zui_locale_samples.inc"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static int f0, c0;
static void begin(void)
{
    f0 = fails;
    c0 = checks;
}
static void end(const char *name)
{
    printf("[%s] %s: %d/%d checks\n", fails > f0 ? "FAIL" : "PASS", name,
           checks - c0 - (fails - f0), checks - c0);
}

#define N(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))
#define LOC  zui_locale_find

static char OUT[1024];

/* 2024-10-12 12:00:00 UTC */
#define NOON 1728734400LL

static bool valid_utf8(const char *s)
{
    uint32_t n = (uint32_t) strlen(s), i = 0;
    while (i < n) {
        uint32_t at = i;
        if (zui_utf8_decode(s, n, &i) == 0xFFFD &&
            !(i - at == 3 && !memcmp(s + at, "\xEF\xBF\xBD", 3)))
            return false;
    }
    return true;
}

static void test_find(void)
{
    begin();
    static const struct {
        const char *in, *want;
    } T[] = {
        {"en", "en"},
        {"en-US", "en"},
        {"EN_gb", "en"},
        {"pt-BR", "pt"},
        {"pt_br", "pt"},
        {"PT", "pt"},
        {"pt-PT", "pt"},
        {"es-419", "es"},
        {"es-MX", "es"},
        {"zh", "zh-Hans"},
        {"zh-CN", "zh-Hans"},
        {"zh-SG", "zh-Hans"},
        {"zh-TW", "zh-Hant"},
        {"zh-HK", "zh-Hant"},
        {"zh-MO", "zh-Hant"},
        {"zh-Hant", "zh-Hant"},
        {"zh-hant-hk", "zh-Hant"},
        {"zh-Hans-TW", "zh-Hans"},
        {"ZH_HANT", "zh-Hant"},
        {"iw", "he"},
        {"iw-IL", "he"},
        {"in", "id"},
        {"ar-EG", "ar"},
        {"fa-IR", "fa"},
        {"ur-PK", "ur"},
        {"sr-Latn", "en"},
        {"xx", "en"},
        {"", "en"},
        {"toolonglanguage", "en"},
        {"de-CH-1996", "de"},
        {"am_ET", "am"},
        {"yo-NG", "yo"},
        {"ha", "ha"},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(!strcmp(zui_locale_tag(LOC(T[k].in)), T[k].want), "%s -> %s, want %s", T[k].in,
              zui_locale_tag(LOC(T[k].in)), T[k].want);
    CHECK(!strcmp(zui_locale_tag(LOC(NULL)), "en"), "NULL -> en");
    CHECK(!strcmp(zui_locale_tag(NULL), "en"), "NULL locale is en");
    static const char *const ALL[] = {
        "en", "es",  "fr",  "de", "pt",      "it",      "ru",  "uk", "pl", "ar",  "he",  "fa",
        "ur", "hi",  "bn",  "ta", "zh-Hans", "zh-Hant", "ja",  "ko", "th", "vi",  "tr",  "id",
        "sw", "am",  "yo",  "ha", "el",      "nl",      "mr",  "te", "gu", "kn",  "ml",  "pa",
        "or", "as",  "ne",  "si", "sd",      "ps",      "sat", "dv", "ms", "fil", "jv",  "su",
        "my", "km",  "lo",  "mn", "bo",      "yue",     "kk",  "uz", "ky", "tg",  "tk",  "az",
        "ka", "hy",  "ckb", "ug", "ro",      "cs",      "hu",  "sv", "bg", "sr",  "hr",  "sk",
        "da", "fi",  "nb",  "lt", "be",      "ca",      "cy",  "ga", "zu", "xh",  "ig",  "so",
        "om", "ti",  "rw",  "mg", "sn",      "wo",      "ln",  "af", "ff", "nqo", "zgh", "qu",
        "gn", "chr", "nv",  "mi", "haw",     "iu",      "se",  "ht"};
    CHECK(zui_locale_count_all() == N(ALL) && N(ALL) == 104, "104 locales");
    for (uint32_t k = 0; k < N(ALL); k++) {
        const zui_locale_t *l = LOC(ALL[k]);
        CHECK(!strcmp(zui_locale_tag(l), ALL[k]) && l == zui_locale_at(k), "own tag %s", ALL[k]);
        CHECK(zui_locale_name(l)[0] != 0, "name %s", ALL[k]);
    }
    CHECK(zui_locale_at(104) == NULL, "past the end");
    static const struct {
        const char *in, *want;
    } A[] = {{"tl", "fil"},     {"tl-PH", "fil"},    {"no", "nb"},      {"no-NO", "nb"},
             {"mo", "ro"},      {"sh", "sr"},        {"ji", "en"},      {"yue-Hant-HK", "yue"},
             {"ckb-IQ", "ckb"}, {"sat-Olck", "sat"}, {"haw-US", "haw"}, {"iu-Cans-CA", "iu"},
             {"chr", "chr"},    {"nqo-GN", "nqo"}};
    for (uint32_t k = 0; k < N(A); k++)
        CHECK(!strcmp(zui_locale_tag(LOC(A[k].in)), A[k].want), "%s -> %s, want %s", A[k].in,
              zui_locale_tag(LOC(A[k].in)), A[k].want);
    /* scripts, which pick the ASCW Golden face */
    static const struct {
        const char *tag, *script;
    } SC[] = {{"en", "Latn"},  {"ru", "Cyrl"}, {"zh-Hant", "Hant"}, {"yue", "Hant"},
              {"sat", "Olck"}, {"dv", "Thaa"}, {"nqo", "Nkoo"},     {"zgh", "Tfng"},
              {"chr", "Cher"}, {"iu", "Cans"}, {"bo", "Tibt"},      {"ti", "Ethi"}};
    for (uint32_t k = 0; k < N(SC); k++)
        CHECK(!strcmp(zui_locale_script(LOC(SC[k].tag)), SC[k].script), "script %s", SC[k].tag);
    /* only English is the source; every other locale's words are machine-made until a
     * native speaker reviews them, and none claims a review it has not had */
    CHECK(zui_locale_review(LOC("en")) == ZUI_REVIEW_SOURCE, "en is the source");
    for (uint32_t i = 1; i < zui_locale_count_all(); i++)
        CHECK(zui_locale_review(zui_locale_at(i)) == ZUI_REVIEW_MACHINE, "%s unreviewed",
              zui_locale_tag(zui_locale_at(i)));
    CHECK(!strcmp(zui_locale_name(LOC("de")), "Deutsch") &&
              !strcmp(zui_locale_name(LOC("ja")), "日本語") &&
              !strcmp(zui_locale_name(LOC("ar")), "العربية"),
          "names");
    end("test_find");
}

static void test_direction_week(void)
{
    begin();
    for (uint32_t i = 0; i < zui_locale_count_all(); i++) {
        const zui_locale_t *l = zui_locale_at(i);
        const char *t = zui_locale_tag(l);
        static const char *const RTL[] = {"ar", "he", "fa",  "ur", "sd",
                                          "ps", "dv", "ckb", "ug", "nqo"};
        bool rtl = false;
        for (uint32_t k = 0; k < N(RTL); k++)
            if (!strcmp(t, RTL[k])) rtl = true;
        CHECK(zui_locale_rtl(l) == rtl, "direction %s", t);
    }
    static const struct {
        const char *tag;
        uint8_t day;
    } D[] = {{"en", 0}, {"es", 1}, {"fr", 1}, {"de", 1}, {"pt", 0},      {"it", 1},
             {"ru", 1}, {"uk", 1}, {"pl", 1}, {"ar", 6}, {"he", 0},      {"fa", 6},
             {"ur", 0}, {"hi", 0}, {"bn", 0}, {"ta", 0}, {"zh-Hans", 1}, {"zh-Hant", 0},
             {"ja", 0}, {"ko", 0}, {"th", 0}, {"vi", 1}, {"tr", 1},      {"id", 0},
             {"sw", 1}, {"am", 0}, {"yo", 1}, {"ha", 1}, {"el", 1},      {"nl", 1}};
    for (uint32_t k = 0; k < N(D); k++)
        CHECK(zui_locale_first_day(LOC(D[k].tag)) == D[k].day, "first day %s", D[k].tag);
    end("test_direction_week");
}

static void test_plurals(void)
{
    begin();
    enum {
        Z = ZUI_PLURAL_ZERO,
        O = ZUI_PLURAL_ONE,
        T = ZUI_PLURAL_TWO,
        F = ZUI_PLURAL_FEW,
        M = ZUI_PLURAL_MANY,
        X = ZUI_PLURAL_OTHER
    };
    static const struct {
        const char *tag;
        int64_t n;
        int want;
    } P[] = {
        {"en", 0, X},       {"en", 1, O},       {"en", 2, X},      {"en", -1, O},
        {"en", 11, X},      {"de", 1, O},       {"nl", 1, O},      {"sw", 2, X},
        {"es", 0, X},       {"es", 1, O},       {"es", 2, X},      {"es", 1000000, M},
        {"es", 2000000, M}, {"es", 1000001, X}, {"es", 100000, X}, {"it", 1000000, M},
        {"it", 0, X},       {"fr", 0, O},       {"fr", 1, O},      {"fr", 2, X},
        {"fr", 1000000, M}, {"pt", 0, O},       {"pt", 1, O},      {"pt", 2, X},
        {"pt", 3000000, M}, {"ru", 1, O},       {"ru", 2, F},      {"ru", 4, F},
        {"ru", 5, M},       {"ru", 11, M},      {"ru", 12, M},     {"ru", 14, M},
        {"ru", 21, O},      {"ru", 22, F},      {"ru", 25, M},     {"ru", 111, M},
        {"ru", 112, M},     {"ru", 1001, O},    {"ru", 0, M},      {"ru", -2, F},
        {"uk", 3, F},       {"uk", 31, O},      {"uk", 10, M},     {"pl", 1, O},
        {"pl", 2, F},       {"pl", 5, M},       {"pl", 12, M},     {"pl", 21, M},
        {"pl", 22, F},      {"pl", 0, M},       {"pl", 101, M},    {"pl", 104, F},
        {"ar", 0, Z},       {"ar", 1, O},       {"ar", 2, T},      {"ar", 3, F},
        {"ar", 10, F},      {"ar", 11, M},      {"ar", 99, M},     {"ar", 100, X},
        {"ar", 101, X},     {"ar", 102, X},     {"ar", 103, F},    {"ar", 111, M},
        {"ar", 1000, X},    {"he", 1, O},       {"he", 2, T},      {"he", 3, X},
        {"he", 0, X},       {"he", 20, X},      {"fa", 0, O},      {"fa", 1, O},
        {"fa", 2, X},       {"hi", 0, O},       {"hi", 1, O},      {"hi", 2, X},
        {"bn", 1, O},       {"am", 0, O},       {"ur", 0, X},      {"ur", 1, O},
        {"ta", 1, O},       {"ta", 0, X},       {"tr", 1, O},      {"el", 1, O},
        {"ha", 1, O},       {"ja", 1, X},       {"zh-Hans", 1, X}, {"ko", 1, X},
        {"th", 1, X},       {"vi", 1, X},       {"id", 1, X},      {"yo", 1, X},
    };
    for (uint32_t k = 0; k < N(P); k++) {
        zui_plural_t p = zui_locale_plural(LOC(P[k].tag), P[k].n);
        CHECK((int) p == P[k].want, "%s %lld -> %s, want %s", P[k].tag, (long long) P[k].n,
              zui_plural_name(p), zui_plural_name((zui_plural_t) P[k].want));
    }
    /* every @integer sample CLDR gives for every locale's rules */
    uint32_t bad = 0;
    for (uint32_t k = 0; k < N(PLURAL_SAMPLES); k++) {
        zui_plural_t p =
            zui_locale_plural(LOC(PLURAL_SAMPLES[k].tag), (int64_t) PLURAL_SAMPLES[k].n);
        if (p != PLURAL_SAMPLES[k].want && bad++ < 10)
            printf("  %s %llu -> %s, CLDR says %s\n", PLURAL_SAMPLES[k].tag,
                   (unsigned long long) PLURAL_SAMPLES[k].n, zui_plural_name(p),
                   zui_plural_name(PLURAL_SAMPLES[k].want));
    }
    CHECK(bad == 0 && N(PLURAL_SAMPLES) > 1000, "CLDR samples: %u of %u wrong", bad,
          (unsigned) N(PLURAL_SAMPLES));
    CHECK(zui_locale_plural(LOC("ru"), INT64_MIN) == ZUI_PLURAL_MANY, "INT64_MIN");
    CHECK(!strcmp(zui_plural_name(ZUI_PLURAL_FEW), "few") &&
              !strcmp(zui_plural_name(ZUI_PLURAL_ZERO), "zero") &&
              !strcmp(zui_plural_name((zui_plural_t) 42), "other"),
          "plural names");
    end("test_plurals");
}

static const char *num(const char *tag, int64_t v, uint32_t scale, zui_digits_t d)
{
    zui_locale_number(LOC(tag), v, scale, d, OUT, sizeof OUT);
    return OUT;
}

static void test_numbers(void)
{
    begin();
    static const struct {
        const char *tag;
        int64_t v;
        uint32_t scale;
        zui_digits_t d;
        const char *want;
    } T[] = {
        {"en", 123456789, 2, ZUI_DIGITS_DEFAULT, "1,234,567.89"},
        {"en", -123456789, 2, ZUI_DIGITS_DEFAULT, "-1,234,567.89"},
        {"en", 1234, 0, ZUI_DIGITS_DEFAULT, "1,234"},
        {"en", 999, 0, ZUI_DIGITS_DEFAULT, "999"},
        {"en", 0, 0, ZUI_DIGITS_DEFAULT, "0"},
        {"en", 5, 2, ZUI_DIGITS_DEFAULT, "0.05"},
        {"en", -5, 3, ZUI_DIGITS_DEFAULT, "-0.005"},
        {"en", 1000000, 0, ZUI_DIGITS_DEFAULT, "1,000,000"},
        {"en", INT64_MAX, 0, ZUI_DIGITS_DEFAULT, "9,223,372,036,854,775,807"},
        {"en", INT64_MIN, 0, ZUI_DIGITS_DEFAULT, "-9,223,372,036,854,775,808"},
        {"de", 123456789, 2, ZUI_DIGITS_DEFAULT, "1.234.567,89"},
        {"de", 1234, 0, ZUI_DIGITS_DEFAULT, "1.234"},
        {"es", 1234, 0, ZUI_DIGITS_DEFAULT, "1234"},
        {"es", 12345, 0, ZUI_DIGITS_DEFAULT, "12.345"},
        {"es", -12345, 1, ZUI_DIGITS_DEFAULT, "-1234,5"},
        {"it", 1234, 0, ZUI_DIGITS_DEFAULT, "1234"},
        {"it", 123456, 0, ZUI_DIGITS_DEFAULT, "123.456"},
        {"pl", 1234, 0, ZUI_DIGITS_DEFAULT, "1234"},
        {"pl", 12345, 0, ZUI_DIGITS_DEFAULT, "12 345"},
        {"fr", 1234567, 0, ZUI_DIGITS_DEFAULT, "1 234 567"},
        {"fr", 15, 1, ZUI_DIGITS_DEFAULT, "1,5"},
        {"ru", 1234, 0, ZUI_DIGITS_DEFAULT, "1 234"},
        {"pt", 1234, 2, ZUI_DIGITS_DEFAULT, "12,34"},
        {"nl", 1234567, 0, ZUI_DIGITS_DEFAULT, "1.234.567"},
        {"hi", 123456789, 2, ZUI_DIGITS_DEFAULT, "12,34,567.89"},
        {"hi", 12345678, 0, ZUI_DIGITS_DEFAULT, "1,23,45,678"},
        {"hi", 1000, 0, ZUI_DIGITS_DEFAULT, "1,000"},
        {"hi", 100000, 0, ZUI_DIGITS_DEFAULT, "1,00,000"},
        {"hi", 1234, 0, ZUI_DIGITS_NATIVE, "१,२३४"},
        {"ta", 1234567, 0, ZUI_DIGITS_DEFAULT, "12,34,567"},
        {"ta", 10, 0, ZUI_DIGITS_NATIVE, "௧௦"},
        {"bn", 1234567, 0, ZUI_DIGITS_DEFAULT, "১২,৩৪,৫৬৭"},
        {"bn", 1234567, 0, ZUI_DIGITS_LATIN, "12,34,567"},
        {"ar", 123456789, 2, ZUI_DIGITS_DEFAULT, "1,234,567.89"},
        {"ar", -5, 0, ZUI_DIGITS_DEFAULT, "‎-5"},
        {"ar", 123456789, 2, ZUI_DIGITS_NATIVE, "١٬٢٣٤٬٥٦٧٫٨٩"},
        {"ar", -5, 0, ZUI_DIGITS_NATIVE, "؜-٥"},
        {"fa", 123456789, 2, ZUI_DIGITS_DEFAULT, "۱٬۲۳۴٬۵۶۷٫۸۹"},
        {"fa", -12, 0, ZUI_DIGITS_DEFAULT, "‎−۱۲"},
        {"fa", 1234, 0, ZUI_DIGITS_LATIN, "1,234"},
        {"ur", 1234, 0, ZUI_DIGITS_DEFAULT, "1,234"},
        {"ur", -1234, 1, ZUI_DIGITS_NATIVE, "‎-‎۱۲۳٫۴"},
        {"he", -7, 0, ZUI_DIGITS_DEFAULT, "‎-7"},
        {"th", 1234, 0, ZUI_DIGITS_DEFAULT, "1,234"},
        {"th", 1234, 0, ZUI_DIGITS_NATIVE, "๑,๒๓๔"},
        {"zh-Hans", 2024, 0, ZUI_DIGITS_NATIVE, "二,〇二四"},
        {"zh-Hans", 2024, 0, ZUI_DIGITS_DEFAULT, "2,024"},
        {"ja", 1234567, 0, ZUI_DIGITS_DEFAULT, "1,234,567"},
        {"vi", 1234567, 1, ZUI_DIGITS_DEFAULT, "123.456,7"},
        {"tr", 1234, 0, ZUI_DIGITS_DEFAULT, "1.234"},
        {"id", 1234, 0, ZUI_DIGITS_DEFAULT, "1.234"},
        {"el", 1234, 0, ZUI_DIGITS_DEFAULT, "1.234"},
        {"sw", 1234, 0, ZUI_DIGITS_DEFAULT, "1,234"},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(!strcmp(num(T[k].tag, T[k].v, T[k].scale, T[k].d), T[k].want), "%s %lld: %s want %s",
              T[k].tag, (long long) T[k].v, OUT, T[k].want);
    CHECK(zui_locale_integer(LOC("de"), 1234567, OUT, sizeof OUT) == 9 && !strcmp(OUT, "1.234.567"),
          "integer");
    CHECK(zui_locale_zero(LOC("ar"), ZUI_DIGITS_DEFAULT) == '0' &&
              zui_locale_zero(LOC("ar"), ZUI_DIGITS_NATIVE) == 0x0660 &&
              zui_locale_zero(LOC("fa"), ZUI_DIGITS_DEFAULT) == 0x06F0 &&
              zui_locale_zero(LOC("ur"), ZUI_DIGITS_NATIVE) == 0x06F0 &&
              zui_locale_zero(LOC("bn"), ZUI_DIGITS_DEFAULT) == 0x09E6 &&
              zui_locale_zero(LOC("hi"), ZUI_DIGITS_NATIVE) == 0x0966 &&
              zui_locale_zero(LOC("th"), ZUI_DIGITS_NATIVE) == 0x0E50 &&
              zui_locale_zero(LOC("ta"), ZUI_DIGITS_NATIVE) == 0x0BE6 &&
              zui_locale_zero(LOC("zh-Hant"), ZUI_DIGITS_NATIVE) == 0x3007 &&
              zui_locale_zero(LOC("fa"), ZUI_DIGITS_LATIN) == '0' &&
              zui_locale_zero(LOC("en"), ZUI_DIGITS_NATIVE) == '0',
          "zero digits");
    /* small buffers: cut at a whole character, NUL-terminated */
    char small[6];
    uint32_t w = zui_locale_number(LOC("fa"), 1234, 0, ZUI_DIGITS_DEFAULT, small, sizeof small);
    CHECK(w == 4 && !strcmp(small, "۱٬"), "cut fa: %u", w);
    CHECK(zui_locale_number(LOC("en"), 1, 0, ZUI_DIGITS_DEFAULT, small, 0) == 0, "cap 0");
    CHECK(zui_locale_number(LOC("en"), 1, 0, ZUI_DIGITS_DEFAULT, NULL, 9) == 0, "NULL out");
    /* the added locales: CLDR default digits in their own scripts */
    static const struct {
        const char *tag, *want;
    } NEWN[] = {{"mr", "-१२,३४,५६७.८९"},           {"my", "-၁,၂၃၄,၅၆၇.၈၉"},
                {"sat", "-᱑,᱒᱓᱔,᱕᱖᱗.᱘᱙"},          {"nqo", "-߁،߂߃߄،߅߆߇.߈߉"},
                {"as", "-১২,৩৪,৫৬৭.৮৯"},           {"hu", "-1\u00A0234\u00A0567,89"},
                {"ka", "-1\u00A0234\u00A0567,89"}, {"cy", "-1,234,567.89"}};
    for (uint32_t k = 0; k < N(NEWN); k++)
        CHECK(!strcmp(num(NEWN[k].tag, -123456789, 2, ZUI_DIGITS_DEFAULT), NEWN[k].want),
              "%s: %s want %s", NEWN[k].tag, OUT, NEWN[k].want);
    CHECK(zui_locale_zero(LOC("te"), ZUI_DIGITS_NATIVE) == 0x0C66 &&
              zui_locale_zero(LOC("km"), ZUI_DIGITS_NATIVE) == 0x17E0 &&
              zui_locale_zero(LOC("bo"), ZUI_DIGITS_NATIVE) == 0x0F20 &&
              zui_locale_zero(LOC("yue"), ZUI_DIGITS_NATIVE) == 0x3007 &&
              zui_locale_zero(LOC("jv"), ZUI_DIGITS_NATIVE) == 0xA9D0,
          "native zeros of the added scripts");
    end("test_numbers");
}

static void test_civil(void)
{
    begin();
    zui_civil_t c = zui_locale_civil(0, 0);
    CHECK(c.year == 1970 && c.month == 1 && c.day == 1 && c.hour == 0 && c.weekday == 4, "epoch");
    c = zui_locale_civil(-1, 0);
    CHECK(c.year == 1969 && c.month == 12 && c.day == 31 && c.hour == 23 && c.minute == 59 &&
              c.second == 59 && c.weekday == 3,
          "before the epoch");
    c = zui_locale_civil(-5 * 86400, 0);
    CHECK(c.year == 1969 && c.month == 12 && c.day == 27 && c.weekday == 6, "1969-12-27 Saturday");
    c = zui_locale_civil(-6 * 86400, 0);
    CHECK(c.day == 26 && c.weekday == 5, "1969-12-26 Friday");
    c = zui_locale_civil(NOON, 0);
    CHECK(c.year == 2024 && c.month == 10 && c.day == 12 && c.hour == 12 && c.weekday == 6,
          "2024-10-12 is a Saturday");
    c = zui_locale_civil(NOON, 12 * 3600);
    CHECK(c.day == 13 && c.hour == 0 && c.weekday == 0, "offset crosses midnight");
    c = zui_locale_civil(NOON, -13 * 3600);
    CHECK(c.day == 11 && c.hour == 23, "negative offset");
    c = zui_locale_civil(NOON, 5 * 3600 + 1800);
    CHECK(c.hour == 17 && c.minute == 30, "+05:30");
    c = zui_locale_civil(1709164800LL, 0); /* 2024-02-29 */
    CHECK(c.year == 2024 && c.month == 2 && c.day == 29 && c.weekday == 4, "leap day");
    c = zui_locale_civil(951868800LL, 0); /* 2000-03-01 */
    CHECK(c.year == 2000 && c.month == 3 && c.day == 1, "2000 is a leap year");
    c = zui_locale_civil(4107542400LL, 0); /* 2100-03-01 */
    CHECK(c.year == 2100 && c.month == 3 && c.day == 1, "2100 is not");
    c = zui_locale_civil(-2208988800LL, 0); /* 1900-01-01 */
    CHECK(c.year == 1900 && c.month == 1 && c.day == 1 && c.weekday == 1, "1900");
    c = zui_locale_civil(253402300799LL, 0); /* 9999-12-31 23:59:59 */
    CHECK(c.year == 9999 && c.month == 12 && c.day == 31 && c.second == 59, "9999");
    /* the Solar Hijri calendar */
    static const struct {
        int64_t epoch;
        int32_t y;
        uint8_t m, d;
    } P[] = {
        {0, 1348, 10, 11},             /* 1970-01-01 */
        {946684800LL, 1378, 10, 11},   /* 2000-01-01 */
        {1710892800LL, 1403, 1, 1},    /* 2024-03-20: Nowruz */
        {1710806400LL, 1402, 12, 29},  /* 2024-03-19 */
        {1742428800LL, 1403, 12, 30},  /* 2025-03-20: 1403 is leap */
        {1742515200LL, 1404, 1, 1},    /* 2025-03-21 */
        {NOON, 1403, 7, 21},           /* 2024-10-12 */
        {1726963200LL, 1403, 7, 1},    /* 2024-09-22: Mehr */
        {1726876800LL, 1403, 6, 31},   /* 2024-09-21 */
        {1679356800LL, 1402, 1, 1},    /* 2023-03-21 */
        {-5420908800LL, 1177, 1, 1},   /* 1798-03-21: a cycle's 4-year jump */
        {-5420995200LL, 1176, 12, 30}, /* the day before */
        {23485248000LL, 2093, 1, 1},   /* 2714-03-22 */
    };
    for (uint32_t k = 0; k < N(P); k++) {
        c = zui_locale_persian(P[k].epoch, 0);
        CHECK(c.year == P[k].y && c.month == P[k].m && c.day == P[k].d,
              "persian %lld: %d/%u/%u want %d/%u/%u", (long long) P[k].epoch, c.year, c.month,
              c.day, P[k].y, P[k].m, P[k].d);
    }
    c = zui_locale_persian(1742470200LL, 3 * 3600 + 1800); /* 2025-03-20 11:30 UTC = 15:00 Tehran */
    CHECK(c.year == 1403 && c.month == 12 && c.day == 30 && c.hour == 15, "persian with offset");
    end("test_civil");
}

static void test_dates(void)
{
    begin();
    const int64_t T = NOON + 13 * 3600 + 5 * 60; /* 2024-10-13 01:05 UTC */
    static const struct {
        const char *tag, *date, *time;
    } D[] = {
        {"en", "10/13/24", "1:05 AM"},      {"es", "13/10/24", "1:05"},
        {"fr", "13/10/2024", "01:05"},      {"de", "13.10.24", "01:05"},
        {"pt", "13/10/2024", "01:05"},      {"it", "13/10/24", "01:05"},
        {"ru", "13.10.2024", "01:05"},      {"uk", "13.10.24", "01:05"},
        {"pl", "13.10.2024", "01:05"},      {"ar", "13‏/10‏/2024", "1:05 ص"},
        {"he", "13.10.2024", "1:05"},       {"fa", "۱۴۰۳/۷/۲۲", "۱:۰۵"},
        {"ur", "13/10/24", "1:05 AM"},      {"hi", "13/10/24", "1:05 am"},
        {"bn", "১৩/১০/২৪", "১:০৫ AM"},      {"ta", "13/10/24", "1:05 AM"},
        {"zh-Hans", "2024/10/13", "01:05"}, {"zh-Hant", "2024/10/13", "凌晨1:05"},
        {"ja", "2024/10/13", "1:05"},       {"ko", "24. 10. 13.", "오전 1:05"},
        {"th", "13/10/67", "01:05"},        {"vi", "13/10/24", "01:05"},
        {"tr", "13.10.2024", "01:05"},      {"id", "13/10/24", "01.05"},
        {"sw", "13/10/2024", "01:05"},      {"am", "13/10/2024", "1:05 ጥዋት"},
        {"yo", "13/10/2024", "1:5"},        {"ha", "13/10/24", "01:05"},
        {"el", "13/10/24", "1:05 π.μ."},    {"nl", "13-10-2024", "01:05"},
    };
    for (uint32_t k = 0; k < N(D); k++) {
        zui_locale_date(LOC(D[k].tag), T, 0, OUT, sizeof OUT);
        CHECK(!strcmp(OUT, D[k].date), "date %s: %s want %s", D[k].tag, OUT, D[k].date);
        zui_locale_time(LOC(D[k].tag), T, 0, OUT, sizeof OUT);
        CHECK(!strcmp(OUT, D[k].time), "time %s: %s want %s", D[k].tag, OUT, D[k].time);
    }
    /* the afternoon, and 12 o'clock */
    zui_locale_time(LOC("en"), NOON + 3600 * 3 + 42 * 60, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "3:42 PM"), "en pm: %s", OUT);
    zui_locale_time(LOC("en"), NOON, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "12:00 PM"), "en noon: %s", OUT);
    zui_locale_time(LOC("en"), NOON, 12 * 3600, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "12:00 AM"), "en midnight: %s", OUT);
    zui_locale_time(LOC("de"), NOON + 3600 * 11 + 59 * 60, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "23:59"), "de 23:59: %s", OUT);
    zui_locale_time(LOC("ar"), NOON + 3600 * 3, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "3:00 م"), "ar pm: %s", OUT);
    zui_locale_time(LOC("ko"), NOON + 3600 * 6, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "오후 6:00"), "ko pm: %s", OUT);
    /* zh-Hant day periods */
    static const struct {
        int h, m;
        const char *want;
    } B[] = {{0, 0, "午夜12:00"},   {0, 30, "凌晨12:30"}, {4, 59, "凌晨4:59"},
             {5, 0, "清晨5:00"},    {7, 59, "清晨7:59"},  {8, 0, "上午8:00"},
             {11, 59, "上午11:59"}, {12, 0, "中午12:00"}, {12, 59, "中午12:59"},
             {13, 0, "下午1:00"},   {18, 59, "下午6:59"}, {19, 0, "晚上7:00"},
             {23, 59, "晚上11:59"}};
    for (uint32_t k = 0; k < N(B); k++) {
        zui_locale_time(LOC("zh-Hant"), NOON - 12 * 3600 + B[k].h * 3600 + B[k].m * 60, 0, OUT,
                        sizeof OUT);
        CHECK(!strcmp(OUT, B[k].want), "zh-Hant %d:%02d: %s want %s", B[k].h, B[k].m, OUT,
              B[k].want);
    }
    /* datetime, offsets, calendars */
    zui_locale_datetime(LOC("de"), NOON, 2 * 3600, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "12.10.24 14:00"), "de datetime: %s", OUT);
    zui_locale_datetime(LOC("fa"), 1742515200LL, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "۱۴۰۴/۱/۱ ۰:۰۰"), "fa nowruz: %s", OUT);
    zui_locale_date(LOC("th"), 946684800LL, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "1/1/43"), "th 2000 is 2543: %s", OUT);
    zui_locale_date(LOC("en"), 946684800LL, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "1/1/00"), "en 2000: %s", OUT);
    zui_locale_date(LOC("ja"), 946684800LL, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "2000/01/01"), "ja pads: %s", OUT);
    char small[4];
    CHECK(zui_locale_date(LOC("fa"), NOON, 0, small, sizeof small) == 2 && !strcmp(small, "۱"),
          "date cut: %s", small);
    /* the added locales: 2025-10-15 00:00 UTC */
    static const struct {
        const char *tag, *want;
    } NEWD[] = {{"hu", "2025. 10. 15."}, {"bg", "15.10.25\u202Fг."}, {"sv", "2025-10-15"},
                {"fil", "10/15/25"},     {"yue", "2025/10/15"},      {"cy", "15/10/25"}};
    for (uint32_t k = 0; k < N(NEWD); k++) {
        zui_locale_date(LOC(NEWD[k].tag), 1760486400LL, 0, OUT, sizeof OUT);
        CHECK(!strcmp(OUT, NEWD[k].want), "%s date %s want %s", NEWD[k].tag, OUT, NEWD[k].want);
    }
    /* Pashto's region is Afghanistan, which keeps the Solar Hijri calendar: 23 Mehr 1404 */
    zui_locale_date(LOC("ps"), 1760486400LL, 0, OUT, sizeof OUT);
    CHECK(strstr(OUT, "۱۴۰۴/۷/۲۳") != NULL, "ps date %s", OUT);
    zui_locale_time(LOC("yue"), 1760486400LL + 15 * 3600, 0, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "下晝3:00"), "yue time %s", OUT);
    end("test_dates");
}

static void test_words(void)
{
    begin();
    /* zui_reader.c's English, word for word */
    static const struct {
        zui_word_t w;
        const char *en;
    } E[] = {{ZUI_WORD_BUTTON, "button"},
             {ZUI_WORD_READONLY, "read-only text"},
             {ZUI_WORD_AI, "AI suggestion"},
             {ZUI_WORD_PARTLY_CHECKED, "partly checked"},
             {ZUI_WORD_NO_PREV_AI, "no previous AI suggestion"},
             {ZUI_WORD_ACCEPT_HINT, "accept with Command Return, or reject"},
             {ZUI_WORD_LM_STATUS, "status landmark"},
             {ZUI_WORD_VERB_HIGH, "verbosity high"},
             {ZUI_WORD_ERROR_PREFIX, "Error: "},
             {ZUI_WORD_POSITION, "{k} of {n}"}};
    for (uint32_t k = 0; k < N(E); k++)
        CHECK(!strcmp(zui_locale_english(E[k].w), E[k].en) &&
                  !strcmp(zui_locale_word(LOC("en"), E[k].w), E[k].en),
              "english %u", (unsigned) k);
    CHECK(!strcmp(zui_locale_english(ZUI_WORD_COUNT), "") &&
              !strcmp(zui_locale_word(LOC("de"), ZUI_WORD_COUNT), ""),
          "out of range");
    /* every locale has every word, with the same placeholders, and its
     * words differ from English where a translation is expected */
    for (uint32_t i = 0; i < zui_locale_count_all(); i++) {
        const zui_locale_t *l = zui_locale_at(i);
        bool ok = true, ph = true;
        uint32_t same = 0;
        for (uint32_t w = 0; w < ZUI_WORD_COUNT; w++) {
            const char *s = zui_locale_word(l, (zui_word_t) w), *e = zui_locale_english(w);
            if (!s[0] || !valid_utf8(s)) ok = false;
            for (const char *p = "xnk"; *p; p++) {
                char tok[4] = {'{', *p, '}', 0};
                if ((strstr(s, tok) != NULL) != (strstr(e, tok) != NULL)) ph = false;
            }
            if (!strcmp(s, e)) same++;
        }
        CHECK(ok, "%s: words present", zui_locale_tag(l));
        CHECK(ph, "%s: placeholders", zui_locale_tag(l));
        CHECK(i == 0 ? same == ZUI_WORD_COUNT : same <= 3, "%s: %u words left in English",
              zui_locale_tag(l), same);
    }
    static const struct {
        const char *tag;
        zui_word_t w;
        const char *want;
    } T[] = {
        {"es", ZUI_WORD_BUTTON, "botón"},
        {"fr", ZUI_WORD_CHECKBOX, "case à cocher"},
        {"de", ZUI_WORD_SELECTED, "ausgewählt"},
        {"de", ZUI_WORD_HEADING, "Überschrift"},
        {"pt", ZUI_WORD_TEXTFIELD, "campo de texto"},
        {"it", ZUI_WORD_LIST, "elenco"},
        {"ru", ZUI_WORD_DISABLED, "недоступно"},
        {"uk", ZUI_WORD_BUTTON, "кнопка"},
        {"pl", ZUI_WORD_CHECKED, "zaznaczone"},
        {"ar", ZUI_WORD_BUTTON, "زر"},
        {"he", ZUI_WORD_LIST, "רשימה"},
        {"fa", ZUI_WORD_BUTTON, "دکمه"},
        {"ur", ZUI_WORD_BUTTON, "بٹن"},
        {"hi", ZUI_WORD_LIST, "सूची"},
        {"bn", ZUI_WORD_BUTTON, "বোতাম"},
        {"ta", ZUI_WORD_BUTTON, "பொத்தான்"},
        {"zh-Hans", ZUI_WORD_BUTTON, "按钮"},
        {"zh-Hant", ZUI_WORD_BUTTON, "按鈕"},
        {"ja", ZUI_WORD_HEADING, "見出し"},
        {"ko", ZUI_WORD_BUTTON, "버튼"},
        {"th", ZUI_WORD_BUTTON, "ปุ่ม"},
        {"vi", ZUI_WORD_BUTTON, "nút"},
        {"tr", ZUI_WORD_BUTTON, "düğme"},
        {"id", ZUI_WORD_BUTTON, "tombol"},
        {"sw", ZUI_WORD_BUTTON, "kitufe"},
        {"am", ZUI_WORD_BUTTON, "አዝራር"},
        {"el", ZUI_WORD_BUTTON, "κουμπί"},
        {"nl", ZUI_WORD_BUTTON, "knop"},
        {"fr", ZUI_WORD_ERROR_PREFIX, "Erreur : "},
        {"zh-Hans", ZUI_WORD_ERROR_PREFIX, "错误："},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(!strcmp(zui_locale_word(LOC(T[k].tag), T[k].w), T[k].want), "%s word %d: %s",
              T[k].tag, (int) T[k].w, zui_locale_word(LOC(T[k].tag), T[k].w));
    CHECK(!strcmp(zui_locale_list_separator(LOC("ar")), "، ") &&
              !strcmp(zui_locale_list_separator(LOC("ja")), "、") &&
              !strcmp(zui_locale_list_separator(LOC("zh-Hant")), "，") &&
              !strcmp(zui_locale_list_separator(LOC("th")), " ") &&
              !strcmp(zui_locale_list_separator(LOC("de")), ", "),
          "list separators");
    end("test_words");
}

static void test_counts(void)
{
    begin();
    static const struct {
        const char *tag;
        zui_count_t noun;
        int64_t n;
        const char *want;
    } T[] = {
        {"en", ZUI_COUNT_ITEM, 1, "1 item"},
        {"en", ZUI_COUNT_ITEM, 0, "0 items"},
        {"en", ZUI_COUNT_ITEM, 1234, "1,234 items"},
        {"en", ZUI_COUNT_CHARACTER, 1, "1 character"},
        {"en", ZUI_COUNT_TOKEN, 2, "2 tokens"},
        {"en", ZUI_COUNT_PERCENT, 1, "1 percent"},
        {"es", ZUI_COUNT_ITEM, 1, "1 elemento"},
        {"es", ZUI_COUNT_ITEM, 1000000, "1.000.000 de elementos"},
        {"fr", ZUI_COUNT_ITEM, 0, "0 élément"},
        {"fr", ZUI_COUNT_ITEM, 2, "2 éléments"},
        {"fr", ZUI_COUNT_TOKEN, 2000000, "2 000 000 de jetons"},
        {"de", ZUI_COUNT_CHARACTER, 5, "5 Zeichen"},
        {"pt", ZUI_COUNT_ITEM, 0, "0 item"},
        {"pt", ZUI_COUNT_ITEM, 2, "2 itens"},
        {"it", ZUI_COUNT_ITEM, 1000000, "1.000.000 di elementi"},
        {"ru", ZUI_COUNT_ITEM, 1, "1 элемент"},
        {"ru", ZUI_COUNT_ITEM, 3, "3 элемента"},
        {"ru", ZUI_COUNT_ITEM, 5, "5 элементов"},
        {"ru", ZUI_COUNT_ITEM, 11, "11 элементов"},
        {"ru", ZUI_COUNT_ITEM, 21, "21 элемент"},
        {"ru", ZUI_COUNT_ITEM, 22, "22 элемента"},
        {"ru", ZUI_COUNT_PERCENT, 42, "42 процента"},
        {"ru", ZUI_COUNT_TOKEN, 1000, "1 000 токенов"},
        {"uk", ZUI_COUNT_CHARACTER, 2, "2 символи"},
        {"uk", ZUI_COUNT_CHARACTER, 7, "7 символів"},
        {"pl", ZUI_COUNT_ITEM, 1, "1 element"},
        {"pl", ZUI_COUNT_ITEM, 3, "3 elementy"},
        {"pl", ZUI_COUNT_ITEM, 12, "12 elementów"},
        {"pl", ZUI_COUNT_ITEM, 23, "23 elementy"},
        {"pl", ZUI_COUNT_PERCENT, 3, "3 procent"},
        {"ar", ZUI_COUNT_ITEM, 1, "عنصر واحد"},
        {"ar", ZUI_COUNT_ITEM, 2, "عنصران"},
        {"ar", ZUI_COUNT_ITEM, 3, "3 عناصر"},
        {"ar", ZUI_COUNT_ITEM, 11, "11 عنصرًا"},
        {"ar", ZUI_COUNT_ITEM, 100, "100 عنصر"},
        {"ar", ZUI_COUNT_ITEM, 0, "0 عنصر"},
        {"he", ZUI_COUNT_ITEM, 1, "פריט אחד"},
        {"he", ZUI_COUNT_ITEM, 2, "שני פריטים"},
        {"he", ZUI_COUNT_ITEM, 3, "3 פריטים"},
        {"fa", ZUI_COUNT_ITEM, 12, "۱۲ مورد"},
        {"ur", ZUI_COUNT_ITEM, 1, "1 آئٹم"},
        {"ur", ZUI_COUNT_ITEM, 2, "2 آئٹمز"},
        {"hi", ZUI_COUNT_ITEM, 100000, "1,00,000 आइटम"},
        {"bn", ZUI_COUNT_ITEM, 3, "৩টি আইটেম"},
        {"ta", ZUI_COUNT_ITEM, 2, "2 உருப்படிகள்"},
        {"zh-Hans", ZUI_COUNT_ITEM, 3, "3 项"},
        {"ja", ZUI_COUNT_CHARACTER, 8, "8 文字"},
        {"ko", ZUI_COUNT_ITEM, 3, "3개 항목"},
        {"th", ZUI_COUNT_PERCENT, 50, "50 เปอร์เซ็นต์"},
        {"tr", ZUI_COUNT_PERCENT, 50, "yüzde 50"},
        {"sw", ZUI_COUNT_ITEM, 1, "kipengee 1"},
        {"sw", ZUI_COUNT_ITEM, 4, "vipengee 4"},
        {"ha", ZUI_COUNT_ITEM, 4, "abubuwa 4"},
        {"am", ZUI_COUNT_ITEM, 0, "0 ንጥል"},
        {"el", ZUI_COUNT_ITEM, 2, "2 στοιχεία"},
        {"nl", ZUI_COUNT_ITEM, 1, "1 item"},
    };
    for (uint32_t k = 0; k < N(T); k++) {
        zui_locale_count(LOC(T[k].tag), T[k].noun, T[k].n, OUT, sizeof OUT);
        CHECK(!strcmp(OUT, T[k].want), "%s count %lld: %s want %s", T[k].tag, (long long) T[k].n,
              OUT, T[k].want);
    }
    CHECK(zui_locale_count(LOC("en"), ZUI_COUNT_NOUNS, 1, OUT, sizeof OUT) == 0 && !OUT[0],
          "bad noun");
    static const struct {
        const char *tag;
        uint64_t k, n;
        const char *want;
    } P[] = {
        {"en", 3, 12, "3 of 12"},
        {"es", 3, 12, "3 de 12"},
        {"fr", 3, 12, "3 sur 12"},
        {"de", 3, 12, "3 von 12"},
        {"ru", 3, 12, "3 из 12"},
        {"ar", 3, 12, "3 من 12"},
        {"he", 3, 12, "3 מתוך 12"},
        {"fa", 3, 12, "۳ از ۱۲"},
        {"ur", 3, 12, "12 میں سے 3"},
        {"hi", 3, 12, "12 में से 3"},
        {"bn", 3, 12, "১২টির মধ্যে ৩"},
        {"zh-Hans", 3, 12, "第 3 项，共 12 项"},
        {"ja", 3, 12, "12 件中 3 件目"},
        {"ko", 3, 12, "12개 중 3번째"},
        {"en", 1500, 2000, "1,500 of 2,000"},
    };
    for (uint32_t k = 0; k < N(P); k++) {
        zui_locale_position(LOC(P[k].tag), P[k].k, P[k].n, OUT, sizeof OUT);
        CHECK(!strcmp(OUT, P[k].want), "%s position: %s want %s", P[k].tag, OUT, P[k].want);
    }
    zui_locale_heading(LOC("de"), 2, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "Überschrift Ebene 2"), "de heading: %s", OUT);
    zui_locale_heading(LOC("fa"), 3, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "سرعنوان سطح ۳"), "fa heading: %s", OUT);
    zui_locale_heading(LOC("en"), 6, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "heading level 6"), "en heading: %s", OUT);
    end("test_counts");
}

static const char *phrase(const char *tag, const char *en)
{
    zui_locale_phrase(LOC(tag), en, OUT, sizeof OUT);
    return OUT;
}

static void test_phrase(void)
{
    begin();
    static const struct {
        const char *tag, *en, *want;
    } T[] = {
        {"en", "Save, button, disabled", "Save, button, disabled"},
        {"de", "Save, button, disabled", "Save, Schaltfläche, nicht verfügbar"},
        {"de", "Files, list, 12 items, report.pdf, selected, 3 of 12",
         "Files, Liste, 12 Elemente, report.pdf, ausgewählt, 3 von 12"},
        {"ru", "Files, list, 12 items, report.pdf, selected, 3 of 12",
         "Files, список, 12 элементов, report.pdf, выбрано, 3 из 12"},
        {"ru", "Files, list, 1 item", "Files, список, 1 элемент"},
        {"ar", "Save, button", "Save، زر"},
        {"ar", "Files, list, 3 items", "Files، قائمة، 3 عناصر"},
        {"fa", "Files, list, 12 items, 3 of 12", "Files، فهرست، ۱۲ مورد، ۳ از ۱۲"},
        {"ja", "Save, button, disabled", "Save、ボタン、使用不可"},
        {"zh-Hans", "Save, button", "Save，按钮"},
        {"th", "Save, button", "Save ปุ่ม"},
        {"fr", "Settings, heading level 2", "Settings, titre de niveau 2"},
        {"es", "Settings, heading", "Settings, encabezado"},
        {"de", "Save, button, shortcut Command S", "Save, Schaltfläche, Tastenkürzel Command S"},
        {"de",
         "AI suggestion, from Fox, pending, about 1200 tokens, Hello, accept with Command "
         "Return, or reject",
         "KI-Vorschlag, von Fox, ausstehend, etwa 1.200 Token, Hello, mit Befehl Zeilenschalter "
         "annehmen oder ablehnen"},
        {"ru", "AI suggestion, from Fox, accepted, 1 token",
         "предложение ИИ, от Fox, принято, 1 токен"},
        {"es", "Rewrite, option, 2 of 4, by editor, about 600 tokens",
         "Rewrite, opción, 2 de 4, por editor, aproximadamente 600 tokens"},
        {"ko", "Notes, from Fox", "Notes, Fox에서"},
        {"de", "Error: Disk is full", "Fehler: Disk is full"},
        {"fr", "Error: Disk, full", "Erreur : Disk, full"},
        {"zh-Hant", "Error: x", "錯誤：x"},
        {"de", "verbosity high", "Ausführlichkeit hoch"},
        {"it", "screen reader off", "lettore schermo disattivato"},
        {"pl", "no next heading", "brak następnego nagłówka"},
        {"he", "Places, navigation landmark", "Places, ציון דרך ניווט"},
        {"de", "Password, text field, secure, 4 characters",
         "Password, Textfeld, geschützt, 4 Zeichen"},
        {"ru", "Upload, progress bar, 42 percent", "Upload, индикатор выполнения, 42 процента"},
        {"de", "Show hidden files, check box, not checked",
         "Show hidden files, Kontrollkästchen, nicht aktiviert"},
        {"de", "Ask, dialog", "Ask, Dialogfeld"},
        {"de", "menu, 3 items, Open, menu item, 1 of 3",
         "Menü, 3 Elemente, Open, Menüeintrag, 1 von 3"},
        {"de", "Smith, John", "Smith, John"},
        {"ar", "Smith, John, button", "Smith, John، زر"},
        {"de", "12 of", "12 of"},
        {"de", "3 of 12x", "3 of 12x"},
        {"de", "heading level x", "heading level x"},
        {"de", "about tokens", "about tokens"},
        {"de", "12 itemsx", "12 itemsx"},
        {"de", "from ", "from "},
        {"de", "button,button", "button,button"},
        {"de", "button, ", "Schaltfläche, "},
        {"de", "", ""},
        {"de", "accept with Command Return", "accept with Command Return"},
        {"de", "99999999999999999999 items", "99999999999999999999 items"},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(!strcmp(phrase(T[k].tag, T[k].en), T[k].want), "%s \"%s\": \"%s\" want \"%s\"",
              T[k].tag, T[k].en, OUT, T[k].want);
    CHECK(zui_locale_phrase(LOC("de"), NULL, OUT, sizeof OUT) == 0 && !OUT[0], "NULL text");
    /* every cut is a whole-character prefix of the full phrase */
    const char *en = "Files, list, 12 items, report.pdf, selected, 3 of 12";
    for (uint32_t i = 0; i < zui_locale_count_all(); i++) {
        const zui_locale_t *l = zui_locale_at(i);
        char full[512], cut[512];
        uint32_t n = zui_locale_phrase(l, en, full, sizeof full);
        bool ok = n == strlen(full) && valid_utf8(full);
        for (uint32_t cap = 1; cap < n + 2 && ok; cap++) {
            uint32_t w = zui_locale_phrase(l, en, cut, cap);
            ok = w < cap && cut[w] == 0 && !strncmp(cut, full, w) && valid_utf8(cut) &&
                 (w == n || w + 4u >= cap); /* at most a partial character is lost */
        }
        CHECK(ok, "%s: cuts", zui_locale_tag(l));
    }
    end("test_phrase");
}

/* ---- L9: the shim, and the real reader speaking through it ------------------------- */

static char HEARD[16][1024];
static zui_say_t HOW[16];
static int NHEARD;
static void hear(const char *text, zui_say_t how, void *ctx)
{
    (void) ctx;
    if (NHEARD < 16) {
        snprintf(HEARD[NHEARD], sizeof HEARD[0], "%s", text);
        HOW[NHEARD] = how;
    }
    NHEARD++;
}

static zui_ui_t UI;
static zui_reader_t RD;
static zui_locale_voice_t VOICE;
static zui_screen_t SCREEN = {1280, 800, 256, ZUI_INPUT_POINTER};
static uint32_t FB[1280 * 800];
static const char *ROWS[] = {"alpha.txt", "beta.png", "gamma"};
static void row_fn(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list, (void) ctx;
    snprintf(out->text, sizeof out->text, "%s", ROWS[row % 3]);
}

static void test_shim(void)
{
    begin();
    zui_locale_voice_init(&VOICE, LOC("de"), hear, NULL);
    NHEARD = 0;
    zui_locale_speak("Save, button", ZUI_SAY_INTERRUPT, &VOICE);
    CHECK(NHEARD == 1 && !strcmp(HEARD[0], "Save, Schaltfläche") && HOW[0] == ZUI_SAY_INTERRUPT,
          "phrased and passed on: %s", HEARD[0]);
    zui_locale_speak("", ZUI_SAY_INTERRUPT, &VOICE);
    CHECK(NHEARD == 2 && HEARD[1][0] == 0 && HOW[1] == ZUI_SAY_INTERRUPT, "stop passes through");
    zui_locale_speak(NULL, ZUI_SAY_QUEUE, &VOICE);
    CHECK(NHEARD == 3 && HEARD[2][0] == 0, "NULL is a stop");
    CHECK(VOICE.phrased == 1, "phrased count");
    zui_locale_speak("x", ZUI_SAY_QUEUE, NULL);
    CHECK(NHEARD == 3, "no voice");
    zui_locale_voice_init(&VOICE, NULL, NULL, NULL);
    zui_locale_speak("x", ZUI_SAY_QUEUE, &VOICE);
    CHECK(NHEARD == 3 && !strcmp(zui_locale_tag(VOICE.loc), "en"), "no next");
    zui_locale_voice_init(NULL, NULL, NULL, NULL);

    /* the screen reader itself, speaking Russian and Arabic */
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16;
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, &SCREEN, &th);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_id_t list = zui_list(&UI, root, row_fn, 0);
    zui_set_name(&UI, list, "Files");
    zui_list_set_rows(&UI, list, 3);
    zui_id_t save = zui_button(&UI, root, "Save", ZUI_BTN_PRIMARY);
    zui_set_shortcut(&UI, save, "\xE2\x8C\x98S"); /* Command S */
    zui_id_t check = zui_button(&UI, root, "Hidden", ZUI_BTN_GHOST);
    zui_id_t title = zui_label(&UI, root, "Settings");
    zui_set_font(&UI, title, ZUI_TEXT_TITLE, 0);
    zui_surface_t s;
    zui_surface_init(&s, FB, 1280, 800, 1280);
    zui_ui_paint(&UI, &s);
    zui_locale_voice_init(&VOICE, LOC("ru"), hear, NULL);
    zui_reader_init(&RD, &UI, zui_locale_speak, &VOICE);
    zui_reader_set_check(&RD, check, ZUI_CHECK_ON);
    char d[ZUI_READER_UTTER];

    zui_reader_describe(&RD, list, -1, d, sizeof d);
    zui_locale_phrase(LOC("ru"), d, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "Files, список, 3 элемента"), "reader list in ru: %s (%s)", OUT, d);
    zui_reader_describe(&RD, list, 1, d, sizeof d);
    zui_locale_phrase(LOC("ru"), d, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "beta.png, 2 из 3"), "reader row in ru: %s (%s)", OUT, d);
    zui_reader_describe(&RD, check, -1, d, sizeof d);
    zui_locale_phrase(LOC("ar"), d, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "Hidden، مربع اختيار، مؤشر عليه"), "reader check box in ar: %s (%s)", OUT,
          d);
    zui_reader_describe(&RD, title, -1, d, sizeof d);
    zui_locale_phrase(LOC("ja"), d, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "Settings、見出しレベル 1"), "reader heading in ja: %s (%s)", OUT, d);
    zui_reader_set_verbosity(&RD, ZUI_VERB_LOW);
    zui_reader_describe(&RD, title, -1, d, sizeof d);
    zui_locale_phrase(LOC("ja"), d, OUT, sizeof OUT);
    CHECK(!strcmp(OUT, "Settings、見出し"), "low verbosity heading in ja: %s", OUT);
    zui_reader_set_verbosity(&RD, ZUI_VERB_MEDIUM);

    /* spoken through the shim */
    NHEARD = 0;
    zui_reader_set_on(&RD, false);
    CHECK(NHEARD == 1 && !strcmp(HEARD[0], "чтение экрана выключено"), "off in ru: %s", HEARD[0]);
    zui_reader_set_on(&RD, true);
    CHECK(NHEARD == 2 && !strcmp(HEARD[1], "чтение экрана включено"), "on in ru: %s", HEARD[1]);
    zui_reader_error(&RD, "Disk is full");
    CHECK(NHEARD == 3 && !strcmp(HEARD[2], "Ошибка: Disk is full") && HOW[2] == ZUI_SAY_INTERRUPT,
          "error in ru: %s", HEARD[2]);
    zui_reader_announce(&RD, "Saved", ZUI_SAY_QUEUE);
    CHECK(NHEARD == 4 && !strcmp(HEARD[3], "Saved"), "app text kept: %s", HEARD[3]);
    UI.focus = save;
    zui_reader_update(&RD);
    bool said = false;
    for (int i = 4; i < NHEARD && i < 16; i++)
        if (strstr(HEARD[i], "Save, кнопка")) said = true;
    CHECK(said, "focus spoken in ru (%d utterances)", NHEARD);
    zui_reader_move(&RD, ZUI_READ_FIRST);
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(!strcmp(HEARD[(NHEARD - 1) % 16], "начало"), "top in ru: %s", HEARD[(NHEARD - 1) % 16]);
    /* every word the reader can say about these widgets, at every verbosity, is
     * translated in every locale: nothing but the names is left in English */
    static const char *const NAMES[] = {"Files",    "Save",  "Hidden",  "Settings", "alpha.txt",
                                        "beta.png", "gamma", "Command", "S"};
    bool all = true;
    for (uint32_t i = 1; i < zui_locale_count_all(); i++) {
        const zui_locale_t *l = zui_locale_at(i);
        for (int v = 0; v < ZUI_VERB_COUNT; v++) {
            zui_reader_set_verbosity(&RD, (zui_verbosity_t) v);
            zui_id_t ids[] = {list, save, check, title};
            for (uint32_t k = 0; k < 4; k++) {
                for (int32_t row = -1; row < (ids[k] == list ? 3 : 0); row++) {
                    zui_reader_describe(&RD, ids[k], row, d, sizeof d);
                    zui_locale_phrase(l, d, OUT, sizeof OUT);
                    /* every English part is a name or comes out translated: it is
                     * not one of the parts said (split on the separators) */
                    const char *sep = zui_locale_list_separator(l);
                    for (char *p = d; *p;) {
                        char *e = strstr(p, ", ");
                        size_t n = e ? (size_t) (e - p) : strlen(p);
                        bool name = false;
                        for (uint32_t j = 0; j < N(NAMES); j++)
                            if (strlen(NAMES[j]) == n && !strncmp(p, NAMES[j], n)) name = true;
                        /* Dutch and Afrikaans say "items" too */
                        if ((!strcmp(zui_locale_tag(l), "nl") ||
                             !strcmp(zui_locale_tag(l), "af")) &&
                            n > 5 && !strncmp(p + n - 5, "items", 5))
                            name = true;
                        for (const char *q = OUT; !name && *q;) {
                            const char *qe = strstr(q, sep);
                            const char *qe2 = strstr(q, ", ");
                            if (!qe || (qe2 && qe2 < qe)) qe = qe2;
                            size_t qn = qe ? (size_t) (qe - q) : strlen(q);
                            if (qn == n && !strncmp(q, p, n)) {
                                all = false;
                                printf("  untranslated in %s: \"%.*s\" in \"%s\"\n",
                                       zui_locale_tag(l), (int) n, p, OUT);
                            }
                            q += qn;
                            if (*q) q += (qe == qe2) ? 2 : strlen(sep);
                        }
                        p += n;
                        if (*p) p += 2;
                    }
                }
            }
        }
    }
    CHECK(all, "every reader word translated in every locale");
    end("test_shim");
}

int main(void)
{
    test_find();
    test_direction_week();
    test_plurals();
    test_numbers();
    test_civil();
    test_dates();
    test_words();
    test_counts();
    test_phrase();
    test_shim();
    printf("%s test_zui_locale: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
