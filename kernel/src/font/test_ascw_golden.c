/* test_ascw_golden.c — the ASCW Golden map against the real fonts and every
 * CLDR locale.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Run from kernel/ (the Makefile does): the font files are read from
 * ../assets/fonts/ascw-golden/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ascw_golden_internal.h"
#include "truetype.h"
#include "i18n.h"

#define DIR "../assets/fonts/ascw-golden/"

static int fails;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static const char *file_of(int32_t f)
{
    const ascw_face_t *a = ascw_face_at(f);
    return a ? a->file : "(none)";
}

static uint8_t *slurp(const char *name, uint32_t *len)
{
    char path[256];
    snprintf(path, sizeof path, DIR "%s", name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *b = malloc((size_t) n);
    if (b && fread(b, 1, (size_t) n, fp) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(fp);
    *len = (uint32_t) n;
    return b;
}

static void test_faces(void)
{
    CHECK(ascw_face_count() == ASCW_FACE_N);
    int32_t p = ascw_primary();
    CHECK(p >= 0 && !strcmp(file_of(p), "ASCW-Golden-Latin2.ttf"));
    CHECK(!strcmp(ascw_face_at(p)->family, "ASCW Golden Latin II"));
    CHECK(ascw_face_at(-1) == NULL && ascw_face_at((int32_t) ASCW_FACE_N) == NULL);
    CHECK(ascw_face_find("ASCW-Golden-Cherokee.ttf") >= 0);
    CHECK(ascw_face_find("nope.ttf") == -1 && ascw_face_find(NULL) == -1);

    /* every face is on disk, is an sfnt, and the glyf faces parse */
    uint32_t parsed = 0, glyf = 0;
    for (uint32_t i = 0; i < ascw_face_count(); i++) {
        uint32_t n = 0;
        uint8_t *b = slurp(ascw_face_at((int32_t) i)->file, &n);
        CHECK(b != NULL);
        if (!b) continue;
        CHECK(n > 12 &&
              ((b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) || !memcmp(b, "OTTO", 4)));
        if (b[0] == 0) {
            glyf++;
            ttf_font_t f;
            if (ttf_parse(&f, b, n)) parsed++;
        }
        free(b);
    }
    printf("  %u faces on disk, %u TrueType, %u parsed by truetype.c\n", ascw_face_count(), glyf,
           parsed);
}

static void test_scripts(void)
{
    CHECK(ascw_face_for_script("Latn") == ascw_primary());
    CHECK(!strcmp(file_of(ascw_face_for_script("Deva")), "ASCW-Golden-Devanagari.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Hant")), "ASCW-Golden-ChineseTraditional.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Hans")), "ASCW-Golden-ChineseSimplified.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Jpan")), "ASCW-Golden-Japanese.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Kore")), "ASCW-Golden-Korean.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Ethi")), "ASCW-Golden-Ethiopic.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_script("Adlm")), "ASCW-Adlam-Regular.ttf"));
    CHECK(ascw_face_for_script("Tibt") == -1); /* no ASCW face: platform fonts */
    CHECK(ascw_face_for_script("latn") == -1); /* ISO 15924 case */
    CHECK(ascw_face_for_script(NULL) == -1);
}

static void test_locales(void)
{
    const char *persian = "ASCW-Golden-Persian.ttf";
    CHECK(!strcmp(file_of(ascw_face_for_locale("fa", "Arab")), persian));
    CHECK(!strcmp(file_of(ascw_face_for_locale("FA_af", "Arab")), persian));
    CHECK(!strcmp(file_of(ascw_face_for_locale("fa-AF-x-test", "Arab")), persian));
    CHECK(!strcmp(file_of(ascw_face_for_locale("ar", "Arab")), "ASCW-Golden-Arabic.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_locale("far", "Arab")), "ASCW-Golden-Arabic.ttf"));
    CHECK(ascw_face_for_locale("en", "Latn") == ascw_primary());
    CHECK(ascw_face_for_locale("dz", "Tibt") == -1);

    int32_t ch[4];
    CHECK(ascw_face_chain("en", "Latn", ch, 4) == 1 && ch[0] == ascw_primary());
    CHECK(ascw_face_chain("hi", "Deva", ch, 4) == 2 && ch[1] == ascw_primary());
    CHECK(ascw_face_chain("dz", "Tibt", ch, 4) == 1 && ch[0] == ascw_primary());
    CHECK(ascw_face_chain("hi", "Deva", ch, 1) == 1 && ch[0] != ascw_primary());
    CHECK(ascw_face_chain("hi", "Deva", ch, 0) == 0);

    /* every CLDR locale: a chain ending in Latin II, led by its script's face */
    uint32_t with_face = 0;
    for (uint32_t i = 0; i < i18n_locale_count(); i++) {
        const i18n_locale_t *l = i18n_locale_at(i);
        const char *tag = i18n_locale_tag(l), *sc = i18n_locale_script(l);
        uint32_t n = ascw_face_chain(tag, sc, ch, 4);
        CHECK(n >= 1 && ch[n - 1] == ascw_primary());
        int32_t f = ascw_face_for_locale(tag, sc);
        if (f >= 0) {
            with_face++;
            CHECK(ch[0] == f);
        }
    }
    printf("  %u of %u CLDR locales have an ASCW face for their script\n", with_face,
           i18n_locale_count());
    CHECK(with_face + 18u == i18n_locale_count()); /* docs/FONTS_COVERAGE.md */
    /* the resolver's answer for a bare region tag still lands on a face */
    const i18n_locale_t *tw = i18n_locale_resolve("zh-TW");
    CHECK(!strcmp(file_of(ascw_face_for_locale(i18n_locale_tag(tw), i18n_locale_script(tw))),
                  "ASCW-Golden-ChineseTraditional.ttf"));
}

static void test_codepoints(void)
{
    for (uint32_t i = 1; i < ASCW_RANGE_N; i++) CHECK(ASCW_RANGES[i - 1].hi < ASCW_RANGES[i].lo);
    for (uint32_t i = 0; i < ASCW_RANGE_N; i++) CHECK(ASCW_RANGES[i].lo <= ASCW_RANGES[i].hi);
    CHECK(ascw_face_for_codepoint('A') == ascw_primary());
    CHECK(ascw_face_for_codepoint('7') == ascw_primary());
    CHECK(ascw_face_for_codepoint(0x1EA0) == ascw_primary()); /* Vietnamese */
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x0915)), "ASCW-Golden-Devanagari.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x0627)), "ASCW-Golden-Arabic.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x1200)), "ASCW-Golden-Ethiopic.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x4E00)), "ASCW-Golden-ChineseSimplified.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x3042)), "ASCW-Golden-Japanese.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0xAC00)), "ASCW-Golden-Korean.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x13A0)), "ASCW-Golden-Cherokee.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x1E900)), "ASCW-Adlam-Regular.ttf"));
    CHECK(!strcmp(file_of(ascw_face_for_codepoint(0x10840)), "ASCW-Golden-Aramaic.ttf"));
    CHECK(ascw_face_for_codepoint(0x0F40) == -1); /* Tibetan */
    CHECK(ascw_face_for_codepoint(0x10FFFD) == -1);

    /* the table never claims a code point its face lacks: check against the
     * real cmaps of the faces truetype.c reads (BMP, format 4) */
    uint32_t checked = 0;
    for (uint32_t i = 0; i < ascw_face_count(); i++) {
        uint32_t n = 0;
        uint8_t *b = slurp(ascw_face_at((int32_t) i)->file, &n);
        ttf_font_t f;
        if (b && ttf_parse(&f, b, n)) {
            for (uint32_t r = 0; r < ASCW_RANGE_N; r++) {
                if (ASCW_RANGES[r].face != (int32_t) i || ASCW_RANGES[r].lo > 0xFFFF) continue;
                for (uint32_t cp = ASCW_RANGES[r].lo; cp <= ASCW_RANGES[r].hi && cp <= 0xFFFF;
                     cp++) {
                    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) continue; /* controls */
                    CHECK(ttf_glyph_index(&f, cp) != 0);
                    checked++;
                }
            }
        }
        free(b);
    }
    printf("  %u code points checked against their face's cmap\n", checked);
    CHECK(checked > 20000);
}

static void test_render(void)
{
    uint32_t n = 0;
    uint8_t *b = slurp("ASCW-Golden-Latin2.ttf", &n);
    ttf_font_t f;
    CHECK(b && ttf_parse(&f, b, n));
    uint8_t px[32 * 32];
    memset(px, 0, sizeof px);
    CHECK(ttf_render_cp(&f, 'Z', 28, px, 32, 32));
    uint32_t ink = 0;
    for (uint32_t i = 0; i < sizeof px; i++) ink += px[i] > 128;
    CHECK(ink > 40); /* a real Z from Latin II, not an empty box */
    free(b);
}

static void test_install(void)
{
    static font_registry_t r;
    font_registry_init(&r);
    int32_t n = ascw_install(&r, "/fonts");
    CHECK(n >= (int32_t) ascw_face_count());
    const font_face_t *lat = font_get(&r, font_resolve(&r, SCRIPT_LATIN, FONT_STYLE_REGULAR));
    CHECK(lat && !strcmp(lat->asset, "/fonts/ASCW-Golden-Latin2.ttf"));
    CHECK(!strcmp(lat->family, "ASCW Golden Latin II"));
    const font_face_t *han = font_get(&r, font_resolve(&r, SCRIPT_HAN, FONT_STYLE_REGULAR));
    CHECK(han && !strcmp(han->asset, "/fonts/ASCW-Golden-ChineseSimplified.ttf"));
    const font_face_t *kana = font_get(&r, font_resolve(&r, SCRIPT_KATAKANA, FONT_STYLE_REGULAR));
    CHECK(kana && !strcmp(kana->asset, "/fonts/ASCW-Golden-Japanese.ttf"));
    const font_face_t *nko = font_get(&r, font_resolve(&r, SCRIPT_NKO, FONT_STYLE_REGULAR));
    CHECK(nko && !strcmp(nko->asset, "/fonts/ASCW-NKo-Regular.ttf"));
    CHECK(font_get(&r, r.fallback) == lat);
    /* unknown text resolves to Latin II, never to an alternate */
    CHECK(font_get(&r, font_resolve(&r, SCRIPT_UNKNOWN, FONT_STYLE_REGULAR)) == lat);

    font_registry_init(&r);
    char longdir[FONT_ASSET_LEN];
    memset(longdir, 'd', sizeof longdir - 1);
    longdir[sizeof longdir - 1] = 0;
    CHECK(ascw_install(&r, longdir) == -1);
    CHECK(ascw_install(NULL, "/fonts") == -1 && ascw_install(&r, NULL) == -1);
}

int main(void)
{
    test_faces();
    test_scripts();
    test_locales();
    test_codepoints();
    test_render();
    test_install();
    if (fails) {
        printf("test_ascw_golden: %d failed\n", fails);
        return 1;
    }
    printf("test_ascw_golden: all passed\n");
    return 0;
}
