/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_i18n.c — host tests for kernel/src/i18n: registry, resolution, CLDR
 * plurals, catalogs and review status, number/currency/date formatting,
 * UTF-8 and grapheme-safe truncation. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "i18n.h"
#include "i18n_internal.h"

static int pass, fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const i18n_locale_t *L(const char *tag)
{
    const i18n_locale_t *l = i18n_locale_find(tag);
    if (!l) printf("[FAIL] locale %s missing\n", tag), fail++;
    return l;
}

#define EQS(got, want) CHECK(strcmp((got), (want)) == 0, "got \"%s\" want \"%s\"", (got), (want))

static void test_registry(void)
{
    CHECK(i18n_locale_count() >= 770, "count %u", i18n_locale_count());
    /* the six official AU languages, all from CLDR */
    const char *au[] = {"ar", "en", "fr", "pt", "es", "sw"};
    for (unsigned i = 0; i < 6; i++) {
        const i18n_locale_t *l = L(au[i]);
        CHECK(l && (i18n_locale_flags(l) & I18N_LF_CLDR), "%s not CLDR", au[i]);
        CHECK(l && i18n_locale_coverage(l) == I18N_COVERAGE_MODERN, "%s coverage", au[i]);
    }
    /* widely spoken African languages the brief names */
    const char *afr[] = {"am",      "om",  "ti",  "so",       "ha",  "yo",  "ig",      "ff",
                         "ff-Adlm", "wo",  "zu",  "xh",       "af",  "st",  "tn",      "sn",
                         "rw",      "rn",  "lg",  "ln",       "mg",  "ak",  "ee",      "zgh",
                         "tzm",     "kab", "shi", "shi-Latn", "ny",  "bm",  "bm-Nkoo", "nqo",
                         "kr",      "sg",  "ts",  "ve",       "ss",  "nd",  "nr",      "kg",
                         "ki",      "luo", "swb", "crs",      "mfe", "kea", "pcm"};
    for (unsigned i = 0; i < sizeof afr / sizeof afr[0]; i++) (void) L(afr[i]);
    /* the wider list */
    const char *world[] = {
        "qu",  "ay", "gn",  "arn",     "nah", "yua",     "quc",     "ht",      "pap", "chr",
        "iu",  "nv", "csw", "haw",     "mi",  "sm",      "to",      "cy",      "ga",  "gd",
        "eu",  "ca", "br",  "se",      "smn", "fo",      "is",      "mt",      "rom", "yi",
        "bo",  "dz", "mn",  "mn-Mong", "ug",  "km",      "lo",      "my",      "si",  "ne",
        "ta",  "te", "kn",  "ml",      "bn",  "gu",      "pa",      "pa-Arab", "or",  "as",
        "ka",  "hy", "dv",  "ps",      "ku",  "ckb",     "tg",      "fil",     "ceb", "jv",
        "su",  "ms", "vi",  "th",      "sa",  "sa-Latn", "la",      "syr",     "arc", "arc-Hebr",
        "aii", "ru", "uk",  "be",      "bg",  "sr-Cyrl", "sr-Latn", "mk",      "kk",  "ky",
        "tt",  "ba", "cv",  "cu",      "zh",  "zh-Hant", "hi",      "ur",      "ja",  "ko",
        "de",  "id", "tr",  "fa"};
    for (unsigned i = 0; i < sizeof world / sizeof world[0]; i++) (void) L(world[i]);

    struct {
        const char *tag, *script;
        bool rtl;
    } sd[] = {{"ar", "Arab", true},       {"nqo", "Nkoo", true},     {"bm-Nkoo", "Nkoo", true},
              {"am", "Ethi", false},      {"ti", "Ethi", false},     {"zgh", "Tfng", false},
              {"tzm", "Latn", false},     {"ff-Adlm", "Adlm", true}, {"syr", "Syrc", true},
              {"aii", "Syrc", true},      {"arc", "Armi", true},     {"arc-Hebr", "Hebr", true},
              {"he", "Hebr", true},       {"chr", "Cher", false},    {"iu", "Cans", false},
              {"mn-Mong", "Mong", false}, {"dv", "Thaa", true},      {"sa", "Deva", false},
              {"sa-Latn", "Latn", false}, {"ru", "Cyrl", false},     {"sr-Latn", "Latn", false},
              {"ur", "Arab", true},       {"pa-Arab", "Arab", true}, {"bo", "Tibt", false}};
    for (unsigned i = 0; i < sizeof sd / sizeof sd[0]; i++) {
        const i18n_locale_t *l = L(sd[i].tag);
        if (!l) continue;
        EQS(i18n_locale_script(l), sd[i].script);
        CHECK(i18n_locale_rtl(l) == sd[i].rtl, "%s rtl", sd[i].tag);
    }
    EQS(i18n_locale_autonym(L("sw")), "Kiswahili");
    EQS(i18n_locale_autonym(L("am")),
        "\xE1\x8A\xA0\xE1\x88\x9B\xE1\x88\xAD\xE1\x8A\x9B"); /* አማርኛ */
    EQS(i18n_locale_english(L("sw-KE")), "Swahili (Kenya)");
    CHECK(i18n_locale_flags(L("am")) & I18N_LF_AUTONYM_CLDR, "am autonym from CLDR");
    CHECK(!(i18n_locale_flags(L("kr")) & I18N_LF_CLDR), "kr not CLDR");
    CHECK(!(i18n_locale_flags(L("kr")) & I18N_LF_AUTONYM_CLDR), "kr autonym is ours");
    CHECK(i18n_locale_flags(L("sa-Latn")) & I18N_LF_DERIVED, "sa-Latn derived");
    CHECK(strstr(i18n_locale_au_states(L("sw")), "KE") != 0, "sw used in KE");
    CHECK(strstr(i18n_locale_au_states(L("sw")), "TZ") != 0, "sw used in TZ");
    CHECK(strstr(i18n_locale_au_states(L("am")), "ET") != 0, "am used in ET");
    EQS(i18n_locale_au_states(L("ja")), "");
    EQS(i18n_locale_currency(L("sw-KE")), "KES");
    EQS(i18n_locale_numbering(L("ar-EG")), "arab");
    EQS(i18n_locale_numbering(L("ar-MA")), "latn");
    EQS(i18n_locale_numbering(L("nqo")), "nkoo");
    EQS(i18n_locale_numbering(L("sa")), "deva");
    EQS(i18n_locale_numbering(L("sa-Latn")), "latn");
    CHECK(i18n_locale_digit(L("ar-EG"), 7) == 0x0667, "arab 7");
    CHECK(i18n_locale_digit(L("ff-Adlm"), 1) == 0x1E951, "adlam 1");
}

static void test_resolve(void)
{
    struct {
        const char *in, *out;
    } r[] = {{"sw-TZ", "sw"},
             {"sw-KE", "sw-KE"},
             {"SW_ke", "sw-KE"},
             {"tl", "fil"},
             {"iw", "he"},
             {"in", "id"},
             {"zh-TW", "zh-Hant"},
             {"zh-CN", "zh"},
             {"sr-ME", "sr-Latn-ME"},
             {"en-US-u-ca-gregory", "en"},
             {"xx-YY", "en"},
             {"", "en"},
             {"pt_AO", "pt-AO"},
             {"pt-BR", "pt"},
             {"ar-SA", "ar-SA"},
             {"ff-Adlm-GN", "ff-Adlm"},
             {"sa-Latn", "sa-Latn"},
             {"am-ET", "am"},
             {"uz-AF", "uz-Arab"},
             {"pa-PK", "pa-Arab"},
             {"x-private", "en"}};
    for (unsigned i = 0; i < sizeof r / sizeof r[0]; i++)
        EQS(i18n_locale_tag(i18n_locale_resolve(r[i].in)), r[i].out);
    EQS(i18n_locale_tag(i18n_locale_resolve(0)), "en");
    EQS(i18n_locale_tag(i18n_locale_parent(L("sw-KE"))), "sw");
    EQS(i18n_locale_tag(i18n_locale_parent(L("pt-AO"))), "pt-PT");
    EQS(i18n_locale_tag(i18n_locale_parent(L("pt-PT"))), "pt");
    CHECK(i18n_locale_parent(L("ff-Adlm")) == 0, "ff-Adlm inherits root, not ff");
    CHECK(i18n_locale_parent(L("sa-Latn")) == 0, "sa-Latn inherits root");
    CHECK(i18n_locale_parent(L("sw")) == 0, "sw top");
}

static void test_plural(void)
{
    const i18n_locale_t *en = L("en"), *fr = L("fr"), *ar = L("ar"), *ru = L("ru");
    CHECK(i18n_plural(en, 1) == I18N_ONE, "en 1");
    CHECK(i18n_plural(en, 0) == I18N_OTHER, "en 0");
    CHECK(i18n_plural(en, 2) == I18N_OTHER, "en 2");
    CHECK(i18n_plural_decimal(en, 10, 1) == I18N_OTHER, "en 1.0");
    CHECK(i18n_plural_decimal(en, 1, 0) == I18N_ONE, "en 1 as decimal");
    CHECK(i18n_plural(fr, 0) == I18N_ONE, "fr 0");
    CHECK(i18n_plural(fr, 1) == I18N_ONE, "fr 1");
    CHECK(i18n_plural(fr, 2) == I18N_OTHER, "fr 2");
    CHECK(i18n_plural(fr, 1000000) == I18N_MANY, "fr 1e6");
    CHECK(i18n_plural(fr, 1000001) == I18N_OTHER, "fr 1e6+1");
    CHECK(i18n_plural_decimal(fr, 15, 1) == I18N_ONE, "fr 1.5");
    i18n_plural_t arw[][2] = {{0, I18N_ZERO},   {1, I18N_ONE},     {2, I18N_TWO},
                              {3, I18N_FEW},    {10, I18N_FEW},    {11, I18N_MANY},
                              {99, I18N_MANY},  {100, I18N_OTHER}, {103, I18N_FEW},
                              {111, I18N_MANY}, {1000, I18N_OTHER}};
    for (unsigned i = 0; i < sizeof arw / sizeof arw[0]; i++)
        CHECK(i18n_plural(ar, arw[i][0]) == arw[i][1], "ar %u", (unsigned) arw[i][0]);
    CHECK(i18n_plural(ru, 1) == I18N_ONE && i18n_plural(ru, 21) == I18N_ONE, "ru one");
    CHECK(i18n_plural(ru, 2) == I18N_FEW && i18n_plural(ru, 34) == I18N_FEW, "ru few");
    CHECK(i18n_plural(ru, 5) == I18N_MANY && i18n_plural(ru, 11) == I18N_MANY &&
              i18n_plural(ru, 111) == I18N_MANY && i18n_plural(ru, 0) == I18N_MANY,
          "ru many");
    CHECK(i18n_plural_decimal(ru, 15, 1) == I18N_OTHER, "ru 1.5");
    CHECK(i18n_plural(L("pl"), 22) == I18N_FEW && i18n_plural(L("pl"), 12) == I18N_MANY, "pl");
    const i18n_locale_t *cy = L("cy");
    CHECK(i18n_plural(cy, 0) == I18N_ZERO && i18n_plural(cy, 1) == I18N_ONE &&
              i18n_plural(cy, 2) == I18N_TWO && i18n_plural(cy, 3) == I18N_FEW &&
              i18n_plural(cy, 6) == I18N_MANY && i18n_plural(cy, 4) == I18N_OTHER,
          "cy");
    CHECK(i18n_plural(L("am"), 0) == I18N_ONE && i18n_plural(L("am"), 2) == I18N_OTHER, "am");
    CHECK(i18n_plural(L("ja"), 1) == I18N_OTHER, "ja");
    CHECK(i18n_plural(L("ti"), 0) == I18N_ONE, "ti 0..1");
    CHECK(i18n_plural(L("tzm"), 11) == I18N_ONE && i18n_plural(L("tzm"), 100) == I18N_OTHER, "tzm");
    CHECK(i18n_plural(en, 10000000000000000000ull) == I18N_OTHER, "huge");
    CHECK(i18n_plural(fr, 10000000000000000000ull) == I18N_MANY, "huge fr");
    CHECK(i18n_plural_categories(ar) == 0x3F, "ar has all six");
    CHECK(i18n_plural_categories(L("sa")) == (1u << I18N_OTHER), "sa: no CLDR rules");
    CHECK(i18n__mod_u64(10000000000000000019ull, 100) == 19, "mod");
}

static void test_messages(void)
{
    i18n_msg_info_t in;
    const char *s;
    s = i18n_msg(L("en"), I18N_NAV_WALLET, &in);
    EQS(s, "Wallet");
    CHECK(in.review == I18N_REVIEW_SOURCE && !in.fallback, "en source");
    s = i18n_msg(L("fr"), I18N_NAV_WALLET, &in);
    EQS(s, "Portefeuille");
    CHECK(in.review == I18N_REVIEW_MACHINE, "fr is machine, not reviewed");
    s = i18n_msg(L("sw-KE"), I18N_NAV_WALLET, &in);
    EQS(s, "Pochi");
    CHECK(in.fallback && in.from == L("sw") && in.review == I18N_REVIEW_MACHINE, "sw-KE -> sw");
    s = i18n_msg(L("ff-Adlm"), I18N_NAV_WALLET, &in);
    EQS(s, "Wallet");
    CHECK(in.fallback && in.from == L("en"), "ff-Adlm -> en");
    s = i18n_msg(L("am"), I18N_TITHE_EXPLAIN, &in);
    CHECK(in.from == L("en"), "am core tier falls back to en for long text");
    s = i18n_msg(L("am"), I18N_BTN_SEND, &in);
    EQS(s, "\xE1\x88\x8B\xE1\x8A\xAD"); /* ላክ */
    s = i18n_msg_plural(L("am"), I18N_TIME_MINUTES_AGO, 5, &in);
    CHECK(in.review == I18N_REVIEW_CLDR && in.from == L("am"), "am time from CLDR");
    s = i18n_msg_plural(L("ar"), I18N_STATUS_PEERS, 3, &in);
    EQS(s, "{0} \xD8\xA3\xD9\x82\xD8\xB1\xD8\xA7\xD9\x86"); /* أقران */
    s = i18n_msg_plural(L("ar"), I18N_STATUS_PEERS, 2, &in);
    CHECK(strstr(s, "\xD9\x82\xD8\xB1\xD9\x8A\xD9\x86\xD8\xA7\xD9\x86") != 0, "ar dual");
    s = i18n_msg_plural(L("fr"), I18N_STATUS_PEERS, 1000000, &in);
    EQS(s, "{0} de pairs");
    s = i18n_msg_plural(L("ru"), I18N_FEED_GROUP_MEMBERS, 3, &in);
    EQS(s, "\xD0\x93\xD1\x80\xD1\x83\xD0\xBF\xD0\xBF\xD0\xB0 \xC2\xB7 {0} "
           "\xD1\x83\xD1\x87\xD0\xB0\xD1\x81\xD1\x82\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB0");
    /* a fallback from ar (6 forms) to en uses English's form for the number */
    s = i18n_msg_plural(L("ff-Adlm"), I18N_STATUS_PEERS, 1, &in);
    EQS(s, "{0} peer");
    s = i18n_msg_plural(L("sa-Latn"), I18N_STATUS_PEERS, 4, &in);
    EQS(s, "{0} sahabhāginaḥ");
    CHECK(in.review == I18N_REVIEW_MACHINE && strcmp(in.note, "translit:sa") == 0, "sa-Latn");
    s = i18n_msg(L("sa"), I18N_NAV_WALLET, &in);
    EQS(s, "\xE0\xA4\xA7\xE0\xA4\xA8\xE0\xA4\x95\xE0\xA5\x8B\xE0\xA4\xB6\xE0\xA4\x83"); /* धनकोशः */
    EQS(i18n_msg(L("la"), I18N_NAV_WALLET, 0), "Crumena");
    EQS(i18n_msg(L("zh-Hant"), I18N_NAV_WALLET, 0), "\xE9\x8C\xA2\xE5\x8C\x85");
    EQS(i18n_msg(L("sr-Latn-BA"), I18N_BTN_SEND, 0), "Pošalji");
    EQS(i18n_msg(i18n_locale_resolve("tl"), I18N_SET_LANGUAGE, 0), "Wika");

    /* every id has English text; keys round-trip */
    for (uint32_t id = 0; id < I18N_MSG__COUNT; id++) {
        s = i18n_msg_plural(L("en"), (i18n_msgid_t) id, 2, &in);
        CHECK(in.from == L("en") && s[0], "en text for %s", i18n_msg_key((i18n_msgid_t) id));
        CHECK(i18n_msg_id(i18n_msg_key((i18n_msgid_t) id)) == (int32_t) id, "key %u", id);
    }
    CHECK(i18n_msg_id("NO_SUCH") == -1, "unknown key");

    /* the review rule: nothing is marked native-reviewed, and every hand line
     * outside English is machine (CLDR lines carry a CLDR note) */
    uint32_t native = 0, bad = 0, machine = 0, cldr = 0;
    for (uint32_t c = 0; c < i18n_catalog_count; c++) {
        const char *tag = i18n_cpool + i18n_catalogs[c].tag;
        for (uint32_t k = 0; k < i18n_catalogs[c].count; k++) {
            const i18n_entry_t *e = &i18n_entries[i18n_catalogs[c].start + k];
            if (e->status == I18N_REVIEW_NATIVE) native++;
            if (e->status == I18N_REVIEW_MACHINE) machine++;
            if (e->status == I18N_REVIEW_CLDR) {
                cldr++;
                if (strncmp(i18n_cpool + e->note, "dateFields/", 11) != 0) bad++;
            }
            if (e->status == I18N_REVIEW_SOURCE && strcmp(tag, "en") != 0) bad++;
        }
    }
    CHECK(native == 0, "no line claims native review (%u)", native);
    CHECK(bad == 0, "status misuse %u", bad);
    CHECK(machine > 1500 && cldr > 900, "machine %u cldr %u", machine, cldr);
}

static void test_format_msg(void)
{
    char b[128];
    const char *a1[] = {"25.00 VFV"};
    CHECK(i18n_format_msg(L("en"), "Pay {0}", a1, 1, b, sizeof b) == 13, "len");
    EQS(b, "Pay 25.00 VFV");
    i18n_format_msg(L("ar"), "\xD8\xA7\xD8\xAF\xD9\x81\xD8\xB9 {0}", a1, 1, b, sizeof b);
    EQS(b, "\xD8\xA7\xD8\xAF\xD9\x81\xD8\xB9 \xE2\x81\xA8"
           "25.00 VFV\xE2\x81\xA9");
    const char *a2[] = {"\xD8\xB3\xD8\xA7\xD8\xB1\xD8\xA9"}; /* an Arabic name in English text */
    i18n_format_msg(L("en"), "Call from {0}.", a2, 1, b, sizeof b);
    EQS(b, "Call from \xE2\x81\xA8\xD8\xB3\xD8\xA7\xD8\xB1\xD8\xA9\xE2\x81\xA9.");
    const char *a3[] = {"x", "y"};
    i18n_format_msg(L("en"), "{1}-{0}-{5}", a3, 2, b, sizeof b);
    EQS(b, "y-x-");
    /* overflow: returns -1, output still valid UTF-8 */
    const char *a4[] = {"\xE1\x88\x8B\xE1\x8A\xAD\xE1\x88\x8B\xE1\x8A\xAD"};
    char small[8];
    CHECK(i18n_format_msg(L("am"), "{0}", a4, 1, small, sizeof small) == -1, "overflow");
    CHECK(i18n_utf8_valid((const uint8_t *) small, (uint32_t) strlen(small), 0), "valid");
    EQS(small, "\xE1\x88\x8B\xE1\x8A\xAD");
    /* the cut lands before a combining mark: the base letter goes too */
    const char *a5[] = {"abe\xCC\x81"};
    char s4[4];
    CHECK(i18n_format_msg(L("en"), "{0}", a5, 1, s4, sizeof s4) == -1, "overflow mark");
    EQS(s4, "ab");
    /* the cut lands after a virama: the conjunct is not split */
    const char *a6[] = {"\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"};
    CHECK(i18n_format_msg(L("hi"), "{0}", a6, 1, small, sizeof small) == -1, "overflow virama");
    EQS(small, "");
}

static void test_numbers(void)
{
    char b[96];
    i18n_fmt_int(L("en"), -1234567, b, sizeof b);
    EQS(b, "-1,234,567");
    i18n_fmt_int(L("fr"), -1234567, b, sizeof b);
    EQS(b, "-1\xE2\x80\xAF"
           "234\xE2\x80\xAF"
           "567");
    i18n_fmt_int(L("es"), 1234, b, sizeof b);
    EQS(b, "1234"); /* minimumGroupingDigits 2 */
    i18n_fmt_int(L("es"), 12345, b, sizeof b);
    EQS(b, "12.345");
    i18n_fmt_int(L("hi"), 1234567, b, sizeof b);
    EQS(b, "12,34,567");
    i18n_fmt_int(L("en"), 0, b, sizeof b);
    EQS(b, "0");
    i18n_fmt_int(L("en"), INT64_MIN, b, sizeof b);
    EQS(b, "-9,223,372,036,854,775,808");
    i18n_fmt_int(L("ar-EG"), 1234567, b, sizeof b);
    EQS(b, "\xD9\xA1\xD9\xAC\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xAC\xD9\xA5\xD9\xA6\xD9\xA7");
    i18n_fmt_int(L("ar-EG"), -5, b, sizeof b);
    EQS(b, "\xD8\x9C-\xD9\xA5"); /* ALM, hyphen, Arabic-Indic five */
    i18n_fmt_int(L("ar"), -5, b, sizeof b);
    EQS(b, "\xE2\x80\x8E-5"); /* LRM, hyphen: CLDR 48 ar defaults to Latin digits */
    i18n_fmt_int(L("sa"), 1234567, b, sizeof b);
    EQS(b,
        "\xE0\xA5\xA7\xE0\xA5\xA8,\xE0\xA5\xA9\xE0\xA5\xAA,\xE0\xA5\xAB\xE0\xA5\xAC\xE0\xA5\xAD");
    i18n_fmt_int(L("nqo"), 2026, b, sizeof b);
    {
        uint8_t want[] = {0xDF, 0x82, 0xD8, 0x8C, 0xDF, 0x80, 0xDF, 0x82, 0xDF, 0x86, 0};
        EQS(b, (const char *) want); /* N'Ko digits, Arabic comma as the group separator */
    }
    i18n_fmt_decimal(L("en"), 82, 2, b, sizeof b);
    EQS(b, "0.82");
    i18n_fmt_decimal(L("en"), 5, 3, b, sizeof b);
    EQS(b, "0.005");
    i18n_fmt_decimal(L("fr"), -123456789, 2, b, sizeof b);
    EQS(b, "-1\xE2\x80\xAF"
           "234\xE2\x80\xAF"
           "567,89");
    i18n_fmt_percent(L("en"), 162, 2, b, sizeof b);
    EQS(b, "1.62%");
    i18n_fmt_percent(L("fr"), 40, 0, b, sizeof b);
    EQS(b, "40\xC2\xA0%"); /* CLDR 48 fr percent: NBSP, group: NNBSP */
    i18n_fmt_percent(L("ar-EG"), 40, 0, b, sizeof b);
    EQS(b, "\xD9\xA4\xD9\xA0\xD9\xAA\xD8\x9C");
    char small[4];
    CHECK(i18n_fmt_int(L("en"), 123456, small, sizeof small) == -1, "overflow");
}

static void test_currency(void)
{
    char b[96];
    i18n_fmt_currency(L("en"), 2500, "USD", 2, 0, 0, b, sizeof b);
    EQS(b, "$25.00");
    i18n_fmt_currency(L("en"), -2500, "USD", 2, 0, 0, b, sizeof b);
    EQS(b, "-$25.00");
    i18n_fmt_currency(L("en"), 2500, "VFV", 2, 0, "VFV", b, sizeof b);
    EQS(b, "VFV\xC2\xA0"
           "25.00"); /* alphabetic symbol: alphaNextToNumber pattern */
    i18n_fmt_currency(L("en"), 2500, "KES", 2, I18N_CUR_CODE, 0, b, sizeof b);
    EQS(b, "KES\xC2\xA0"
           "25.00");
    i18n_fmt_currency(L("fr"), 123456, "EUR", 2, 0, 0, b, sizeof b);
    EQS(b, "1\xE2\x80\xAF"
           "234,56\xC2\xA0\xE2\x82\xAC");
    i18n_fmt_currency(L("sw-KE"), 100000, "KES", 2, 0, 0, b, sizeof b);
    EQS(b, "Ksh\xC2\xA0"
           "1,000.00");
    i18n_fmt_currency(L("en-NG"), 150000, "NGN", 2, 0, 0, b, sizeof b);
    EQS(b, "\xE2\x82\xA6"
           "1,500.00");
    i18n_fmt_currency(L("sw"), 150000, "NGN", 2, I18N_CUR_NARROW, 0, b, sizeof b);
    EQS(b, "\xE2\x82\xA6\xC2\xA0"
           "1,500.00");
    i18n_fmt_currency(L("fr-SN"), 2500, "XOF", 0, 0, 0, b, sizeof b);
    EQS(b, "2\xE2\x80\xAF"
           "500\xC2\xA0"
           "F\xE2\x80\xAF"
           "CFA");
    i18n_fmt_currency(L("ar-EG"), 12345, "EGP", 2, 0, 0, b, sizeof b);
    CHECK(strncmp(b, "\xE2\x80\x8F", 3) == 0, "ar-EG currency starts with RLM: %s", b);
    CHECK(strstr(b, "\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xAB\xD9\xA4\xD9\xA5") != 0, "arab digits");
    i18n_fmt_currency(L("ja"), 1234, "JPY", 0, 0, 0, b, sizeof b);
    EQS(b, "\xEF\xBF\xA5"
           "1,234");
    i18n_fmt_currency(L("de-CH"), 123456, "CHF", 2, 0, 0, b, sizeof b);
    EQS(b, "CHF\xC2\xA0"
           "1'234.56"); /* CLDR 48 de-CH group is U+0027 */
    CHECK(i18n_currency_digits("JPY") == 0 && i18n_currency_digits("BHD") == 3 &&
              i18n_currency_digits("USD") == 2 && i18n_currency_digits("UGX") == 0 &&
              i18n_currency_digits("XOF") == 0,
          "digits");
    EQS(i18n_currency_symbol(L("en"), "EUR", false), "\xE2\x82\xAC");
    EQS(i18n_currency_symbol(L("en"), "ZZZ", false), "ZZZ");
}

static void test_dates(void)
{
    char b[128];
    int32_t y;
    uint32_t m, d;
    CHECK(i18n_days_from_civil(1970, 1, 1) == 0, "epoch");
    CHECK(i18n_days_from_civil(2000, 3, 1) == 11017, "2000-03-01");
    CHECK(i18n_days_from_civil(2026, 10, 9) == 20735, "2026-10-09");
    CHECK(i18n_weekday(0) == 4 && i18n_weekday(20735) == 5 && i18n_weekday(-1) == 3, "weekday");
    int ok = 1;
    for (int32_t day = -1000000; day <= 1000000; day += 97) {
        i18n_civil_from_days(day, &y, &m, &d);
        if (i18n_days_from_civil(y, m, d) != day || m < 1 || m > 12 || d < 1 || d > 31) ok = 0;
    }
    CHECK(ok, "civil round trip");
    i18n_civil_from_days(-719162, &y, &m, &d);
    CHECK(y == 1 && m == 1 && d == 1, "0001-01-01");
    const int32_t day = 20735;
    i18n_fmt_date(L("en"), day, I18N_DATE_LONG, b, sizeof b);
    EQS(b, "October 9, 2026");
    i18n_fmt_date(L("en"), day, I18N_DATE_FULL, b, sizeof b);
    EQS(b, "Friday, October 9, 2026");
    i18n_fmt_date(L("en"), day, I18N_DATE_SHORT, b, sizeof b);
    EQS(b, "10/9/26");
    i18n_fmt_date(L("fr"), day, I18N_DATE_LONG, b, sizeof b);
    EQS(b, "9 octobre 2026");
    i18n_fmt_date(L("sw"), day, I18N_DATE_FULL, b, sizeof b);
    EQS(b, "Ijumaa, 9 Oktoba 2026");
    i18n_fmt_date(L("ar-EG"), day, I18N_DATE_LONG, b, sizeof b);
    EQS(b, "\xD9\xA9 \xD8\xA3\xD9\x83\xD8\xAA\xD9\x88\xD8\xA8\xD8\xB1 "
           "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6");
    i18n_fmt_date(L("ar"), day, I18N_DATE_MEDIUM, b, sizeof b);
    EQS(b, "09\xE2\x80\x8F/10\xE2\x80\x8F/2026"); /* RLMs from the CLDR pattern */
    i18n_fmt_date(L("am"), day, I18N_DATE_LONG, b, sizeof b);
    EQS(b, "9 \xE1\x8A\xA6\xE1\x8A\xAD\xE1\x89\xB6\xE1\x89\xA0\xE1\x88\xAD 2026"); /* ኦክቶበር */
    i18n_fmt_date(L("sa-Latn"), day, I18N_DATE_LONG, b, sizeof b);
    EQS(b, "9 aktūbaramāsaḥ 2026");
    i18n_fmt_date(L("en"), -719163, I18N_DATE_LONG, b, sizeof b);
    CHECK(i18n_fmt_date(L("en"), -719163, I18N_DATE_LONG, b, sizeof b) == -1, "BCE refused");
    EQS(i18n_month_name(L("en"), 1, true, false), "January");
    EQS(i18n_month_name(L("en"), 13, true, false), "");
    EQS(i18n_day_name(L("fr"), 0, true), "dimanche");

    /* Ge'ez script round trip: every Amharic and Tigrinya month and day name
     * decodes to Ethiopic codepoints and re-encodes to the same bytes */
    const char *eth[] = {"am", "ti"};
    for (unsigned k = 0; k < 2; k++) {
        const i18n_locale_t *l = L(eth[k]);
        for (uint32_t mo = 1; mo <= 12; mo++) {
            const char *nm = i18n_month_name(l, mo, true, false);
            uint32_t len = (uint32_t) strlen(nm), off = 0, cp, ethi = 0;
            uint8_t re[64];
            uint32_t rl = 0;
            bool okk = true;
            while (off < len) {
                int32_t n = i18n_utf8_decode((const uint8_t *) nm + off, len - off, &cp);
                if (n < 0) {
                    okk = false;
                    break;
                }
                if (cp >= 0x1200 && cp <= 0x139F) ethi++;
                rl += i18n_utf8_encode(cp, re + rl);
                off += (uint32_t) n;
            }
            CHECK(okk && ethi > 0 && rl == len && memcmp(re, nm, len) == 0, "%s month %u", eth[k],
                  mo);
        }
    }
}

static bool is_boundary(const uint8_t *s, uint32_t len, uint32_t at)
{
    uint32_t off = 0;
    if (at == 0) return true;
    while (off < len) {
        off = i18n_grapheme_next(s, len, off);
        if (off == at) return true;
        if (off > at) return false;
    }
    return false;
}

static void test_utf8(void)
{
    uint32_t bad, cp;
    CHECK(i18n_utf8_valid((const uint8_t *) "abc\xE1\x88\x80", 6, &bad) && bad == 6, "valid");
    CHECK(!i18n_utf8_valid((const uint8_t *) "a\xC0\x80", 3, &bad) && bad == 1, "overlong");
    CHECK(!i18n_utf8_valid((const uint8_t *) "\xED\xA0\x80", 3, &bad) && bad == 0, "surrogate");
    CHECK(!i18n_utf8_valid((const uint8_t *) "\xF4\x90\x80\x80", 4, &bad), "> U+10FFFF");
    CHECK(!i18n_utf8_valid((const uint8_t *) "ab\xE1\x88", 4, &bad) && bad == 2, "truncated");
    CHECK(!i18n_utf8_valid((const uint8_t *) "\x80", 1, &bad), "stray continuation");
    CHECK(!i18n_utf8_valid((const uint8_t *) "\xF8\x88\x80\x80\x80", 5, &bad), "5-byte");
    CHECK(i18n_utf8_decode((const uint8_t *) "\xF0\x9E\xA4\x80", 4, &cp) == 4 && cp == 0x1E900,
          "adlam");
    int ok = 1;
    for (uint32_t c = 0; c <= 0x10FFFF; c++) {
        uint8_t e[4];
        uint32_t n = i18n_utf8_encode(c, e), back;
        if (c >= 0xD800 && c <= 0xDFFF) {
            if (n != 0) ok = 0;
            continue;
        }
        if (n == 0 || i18n_utf8_decode(e, n, &back) != (int32_t) n || back != c) ok = 0;
    }
    CHECK(ok, "encode/decode every scalar");
    CHECK(i18n_utf8_encode(0x110000, (uint8_t[4]){0}) == 0, "out of range");
    char b[32];
    i18n_utf8_sanitize((const uint8_t *) "a\xFF"
                                         "b\xE1\x88",
                       5, b, sizeof b);
    EQS(b, "a\xEF\xBF\xBD"
           "b\xEF\xBF\xBD\xEF\xBF\xBD");
}

static void test_graphemes(void)
{
    struct {
        const char *s;
        uint32_t clusters;
    } g[] = {
        {"e\xCC\x81", 1},                            /* e + combining acute       */
        {"e\xCC\xA3\xCC\x80", 1},                    /* Yoruba e + dot below + grave */
        {"\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", 1}, /* क्ष: virama conjunct (GB9c) */
        {"\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x8D\xE0\xA4\xB0\xE0\xA5\x80", 1}, /* स्त्री */
        {"\xF0\x9F\x87\xB3\xF0\x9F\x87\xAC", 1},                 /* flag NG                   */
        {"\xF0\x9F\x87\xB3\xF0\x9F\x87\xAC\xF0\x9F\x87\xB0", 2}, /* NG + lone K      */
        {"\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
         1},                                         /* family */
        {"\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF", 1},     /* thumbs up + skin tone     */
        {"\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8", 1}, /* Hangul L V T              */
        {"\xEA\xB0\x80\xE1\x86\xA8", 1},             /* LV + T                    */
        {"\r\n", 1},
        {"\xDF\x8A\xDF\xAB", 1},                     /* N'Ko letter + tone mark   */
        {"\xD8\xA8\xD9\x8E\xD9\x91", 1},             /* Arabic beh + fatha + shadda */
        {"\xE1\x88\x80\xE1\x88\x81", 2},             /* two Ethiopic syllables    */
        {"\xE2\xB4\xB0\xE2\xB5\xAF", 2},             /* Tifinagh + U+2D6F (Lm, not Extend) */
        {"\xE2\xB4\xB0\xE2\xB5\xBF\xE2\xB4\xB1", 2}, /* joiner U+2D7F extends; no GB9c for Tfng */
        {"abc", 3},
        {"", 0},
    };
    for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++) {
        uint32_t n = i18n_grapheme_count((const uint8_t *) g[i].s, (uint32_t) strlen(g[i].s));
        CHECK(n == g[i].clusters, "clusters #%u: %u want %u", i, n, g[i].clusters);
    }
    const char *s = "abe\xCC\x81";
    CHECK(i18n_truncate((const uint8_t *) s, 5, 3) == 2, "do not split e from its accent");
    CHECK(i18n_truncate((const uint8_t *) s, 5, 4) == 2, "still not");
    CHECK(i18n_truncate((const uint8_t *) s, 5, 5) == 5, "whole");
    /* exhaustive: every cut of a mixed-script string is valid and on a boundary */
    const char *mix = "Pay \xE1\x88\x8B\xE1\x8A\xAD 25 e\xCC\x81 \xD8\xA8\xD9\x8E "
                      "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7 \xF0\x9F\x87\xB3\xF0\x9F\x87\xAC "
                      "\xF0\x9E\xA4\x80\xF0\x9E\xA5\x84 \xDF\x8A\xDF\xAB";
    uint32_t len = (uint32_t) strlen(mix);
    int ok = 1;
    for (uint32_t max = 0; max <= len; max++) {
        uint32_t r = i18n_truncate((const uint8_t *) mix, len, max);
        if (r > max || !i18n_utf8_valid((const uint8_t *) mix, r, 0) ||
            !is_boundary((const uint8_t *) mix, len, r))
            ok = 0;
    }
    CHECK(ok, "every truncation is valid UTF-8 on a cluster boundary");
    char b[32];
    i18n_truncate_ellipsis((const uint8_t *) "Kiswahili", 9, 7, b, sizeof b);
    EQS(b, "Kisw\xE2\x80\xA6");
    i18n_truncate_ellipsis((const uint8_t *) "Kiswahili", 9, 9, b, sizeof b);
    EQS(b, "Kiswahili");
    i18n_truncate_ellipsis((const uint8_t *) s, 5, 5, b, sizeof b);
    EQS(b, s);
    i18n_truncate_ellipsis((const uint8_t *) "ab\xE1\x88\x8B\xE1\x8A\xAD", 8, 7, b, sizeof b);
    EQS(b, "ab\xE2\x80\xA6"); /* 7 bytes: "ab" + ellipsis; ላ would need 8 */
    CHECK(i18n_is_grapheme_extend(0x0301) && i18n_is_grapheme_extend(0x135D) &&
              i18n_is_grapheme_extend(0x07EB) && !i18n_is_grapheme_extend('a'),
          "extend table");
}

static void test_needs(void)
{
    const i18n_range_t *r;
    uint32_t gaps;
    uint32_t n = i18n_locale_needs(L("am"), &r, &gaps);
    bool ethi = false;
    for (uint32_t i = 0; i < n; i++)
        if (r[i].lo >= 0x1200 && r[i].hi <= 0x139F) ethi = true;
    CHECK(n > 0 && ethi, "am needs Ethiopic");
    n = i18n_locale_needs(L("yo"), &r, &gaps);
    CHECK(n > 0 && gaps > 0, "yo needs codepoints the font table cannot classify (%u)", gaps);
    n = i18n_locale_needs(L("kr"), &r, &gaps);
    CHECK(n == 0, "no CLDR exemplars for kr");
}

int main(void)
{
    test_registry();
    test_resolve();
    test_plural();
    test_messages();
    test_format_msg();
    test_numbers();
    test_currency();
    test_dates();
    test_utf8();
    test_graphemes();
    test_needs();
    printf("%s test_i18n: %d passed, %d failed (%s, %u locales, %u catalogs)\n",
           fail ? "[FAIL]" : "[PASS]", pass, fail, i18n_data_version(), i18n_locale_count(),
           i18n_catalog_count);
    return fail ? 1 : 0;
}
