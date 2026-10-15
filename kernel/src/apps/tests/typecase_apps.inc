/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Typecase's model against zxvd's real dispatcher: files.list, files.read
 * and files.write on the scratch Hoard, with fonts built here byte by byte:
 * a TrueType font with a format 4 map and a family name, a CFF font with a
 * format 12 map, and fonts that are not, or not accepted. */
#include "typecase_model.h"

#define TC_BIG 40000u

static void tc_p16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void tc_p32(uint8_t *p, uint32_t v)
{
    tc_p16(p, v >> 16);
    tc_p16(p + 2, v & 0xFFFFu);
}

/* An sfnt of n tables (TM1) into f: each body after the directory, padded
 * to four bytes; its size. */
static uint32_t tc_font(uint8_t *f, uint32_t ver, uint32_t n, const uint32_t *tag,
                        const uint8_t *const *body, const uint32_t *blen)
{
    uint32_t at = 12u + 16u * n;
    memset(f, 0, TC_BIG);
    tc_p32(f, ver);
    tc_p16(f + 4, n);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *r = f + 12u + 16u * i;
        tc_p32(r, tag[i]);
        tc_p32(r + 8, at);
        tc_p32(r + 12, blen[i]);
        if (blen[i]) memcpy(f + at, body[i], blen[i]);
        at += (blen[i] + 3u) & ~3u;
    }
    return at;
}

/* A format 4 map: A-Z and a-z to glyphs 1..52, Alpha..Epsilon through the
 * glyph array with Beta at glyph 0 (not covered), and the end marker. */
static uint32_t tc_cmap4(uint8_t *c)
{
    static const uint32_t end[4] = {0x5A, 0x7A, 0x395, 0xFFFF},
                          start[4] = {0x41, 0x61, 0x391, 0xFFFF};
    static const uint32_t delta[4] = {0xFFC0, 0xFFBA, 0, 1}, ro[4] = {0, 0, 4, 0};
    static const uint32_t glyphs[5] = {60, 0, 61, 62, 63};
    uint8_t *s = c + 12;
    memset(c, 0, 12u + 58u);
    tc_p16(c + 2, 1); /* one subtable */
    tc_p16(c + 4, 3); /* Windows */
    tc_p16(c + 6, 1); /* Unicode BMP */
    tc_p32(c + 8, 12);
    tc_p16(s, 4);
    tc_p16(s + 2, 58); /* length */
    tc_p16(s + 6, 8);  /* segCountX2 */
    for (uint32_t i = 0; i < 4; i++) {
        tc_p16(s + 14 + 2 * i, end[i]);
        tc_p16(s + 24 + 2 * i, start[i]);
        tc_p16(s + 32 + 2 * i, delta[i]);
        tc_p16(s + 40 + 2 * i, ro[i]);
    }
    for (uint32_t i = 0; i < 5; i++) tc_p16(s + 48 + 2 * i, glyphs[i]);
    return 12u + 58u;
}

/* A name table: Mac Roman "Old", Windows "Bold" (name 2), Windows "Testface". */
static uint32_t tc_name(uint8_t *n)
{
    static const char face[] = "Testface";
    memset(n, 0, 42u + 27u);
    tc_p16(n + 2, 3);
    tc_p16(n + 4, 42);
    uint8_t *r = n + 6;
    tc_p16(r, 1), tc_p16(r + 6, 1), tc_p16(r + 8, 3), tc_p16(r + 10, 0);
    r += 12;
    tc_p16(r, 3), tc_p16(r + 2, 1), tc_p16(r + 4, 0x409), tc_p16(r + 6, 2), tc_p16(r + 8, 8);
    tc_p16(r + 10, 3);
    r += 12;
    tc_p16(r, 3), tc_p16(r + 2, 1), tc_p16(r + 4, 0x409), tc_p16(r + 6, 1), tc_p16(r + 8, 16);
    tc_p16(r + 10, 11);
    memcpy(n + 42, "Old", 3);
    for (uint32_t i = 0; i < 4; i++) tc_p16(n + 45 + 2 * i, (uint32_t) "Bold"[i]);
    for (uint32_t i = 0; i < 8; i++) tc_p16(n + 53 + 2 * i, (uint32_t) face[i]);
    return 42u + 27u;
}

/* A format 12 map, reached by two records (the format 4 one finds no
 * format 4 there): all of Hiragana, ten Han ideographs the first of which
 * is glyph 0, and one Hangul syllable. g2 swaps the last two groups. */
static uint32_t tc_cmap12(uint8_t *c, bool unordered)
{
    static const uint32_t grp[3][3] = {
        {0x3041, 0x3096, 1}, {0x4E00, 0x4E09, 0}, {0xAC00, 0xAC00, 200}};
    uint8_t *s = c + 20;
    memset(c, 0, 20u + 52u);
    tc_p16(c + 2, 2);
    tc_p16(c + 4, 3), tc_p16(c + 6, 1), tc_p32(c + 8, 20);
    tc_p16(c + 12, 3), tc_p16(c + 14, 10), tc_p32(c + 16, 20);
    tc_p16(s, 12);
    tc_p32(s + 4, 52);
    tc_p32(s + 12, 3);
    for (uint32_t g = 0; g < 3; g++) {
        uint32_t k = unordered && g ? 3u - g : g;
        tc_p32(s + 16 + 12 * g, grp[k][0]);
        tc_p32(s + 20 + 12 * g, grp[k][1]);
        tc_p32(s + 24 + 12 * g, grp[k][2]);
    }
    return 20u + 52u;
}

static bool tc_put(const char *rel, const uint8_t *d, uint32_t n)
{
    uint64_t size = 0;
    return zxf_write(rel, d, n, ZXF_W_REPLACE, false, &size) == 0 && size == n;
}

static bool tc_same(const char *rel, const uint8_t *want, uint32_t n)
{
    static uint8_t buf[TC_BIG];
    uint32_t got = 0, at = 0;
    uint64_t size = 0;
    while (at < n) {
        if (zxf_read(rel, at, buf + at, TC_BIG - at, false, &got, &size) != 0 || !got) return false;
        at += got;
    }
    return size == n && !memcmp(buf, want, n);
}

APPS_TEST(typecase)
{
    static typecase_t T;
    static uint8_t font[TC_BIG], cmap[512], cmap12[512], name[128], fill[30000], other[TC_BIG];
    static char pv[128];
    static const uint32_t tags3[3] = {0x636D6170, 0x676C7966, 0x6E616D65}; /* cmap glyf name */
    typecase_init(&T, &LINK);

    /* typecase_script_points: the letters counted (TM2) */
    CHECK(typecase_script_points(0) == 450 && typecase_script_points(1) == 49 &&
              typecase_script_points(2) == 256 && typecase_script_points(7) == 86 &&
              typecase_script_points(9) == 11172 && typecase_script_points(10) == 20992 &&
              typecase_script_points(TYPECASE_SCRIPTS) == 0 &&
              !strcmp(typecase_script_name(0), "Latin") &&
              !strcmp(typecase_script_name(10), "Han") &&
              typecase_script_name(TYPECASE_SCRIPTS) == NULL,
          "eleven scripts, each counted");
    for (uint32_t s = 0; s < TYPECASE_SCRIPTS; s++)
        CHECK(typecase_coverage_x100(typecase_script_points(s), typecase_script_points(s)) == 10000,
              "%s fits TR2", typecase_script_name(s));

    /* the fonts in the Hoard */
    for (uint32_t i = 0; i < sizeof fill; i++) fill[i] = (uint8_t) (i * 7u + 3u);
    const uint8_t *body3[3] = {cmap, fill, name};
    uint32_t len3[3] = {tc_cmap4(cmap), sizeof fill, tc_name(name)};
    uint32_t fa = tc_font(font, 0x00010000, 3, tags3, body3, len3);
    CHECK(zxf_mkdir("fonts") == 0 && zxf_mkdir("fonts/old.ttf") == 0 &&
              tc_put("fonts/a-test.ttf", font, fa) && tc_put("fonts/notes.txt", font, 12),
          "files");

    /* files.list: .ttf and .otf, not a folder or a note */
    CHECK(typecase_list(&T, "fonts") == ZXM_OK && T.nfiles == 1 &&
              !strcmp(T.file[0].path, "fonts/a-test.ttf") && T.file[0].size == fa,
          "listed: %u %s", T.nfiles, LINK.err);
    int before = sent;
    CHECK(typecase_list(&T, NULL) == ZXM_E_ARG && typecase_open(&T, "") == ZXM_E_ARG &&
              typecase_open(&T, NULL) == ZXM_E_ARG && sent == before,
          "nothing asked without a folder or a path");
    CHECK(typecase_open(&T, "fonts/none.ttf") == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_FILES,
          "a file that is not there: zxvd says so");

    /* the TrueType font: accepted, named, its scripts counted (TM1..TM5) */
    CHECK(typecase_open(&T, "fonts/a-test.ttf") == ZXM_OK && T.accepted == 1 &&
              T.fault == TYPECASE_F_NONE && T.ntables == 3 && T.size == fa &&
              T.table[0].off == 60 && T.table[1].len == (int64_t) sizeof fill,
          "opened: %d %s", T.fault, LINK.err);
    CHECK(!strcmp(T.family, "Testface"), "the family, Windows first (TM4): %s", T.family);
    CHECK(T.covered[0] == 52 && T.cover_x100[0] == 1155 && T.covered[1] == 4 &&
              T.cover_x100[1] == 816 && T.covered[2] == 0 && T.cover_x100[10] == 0,
          "Latin 52 of 450, Greek 4 of 49 (Beta at glyph 0 is not covered): %lld %lld",
          (long long) T.cover_x100[0], (long long) T.cover_x100[1]);
    CHECK(typecase_preview(&T, 0, pv, sizeof pv) == 16 && !strcmp(pv, "ABCDEFGHIJKLMNOP") &&
              typecase_preview(&T, 1, pv, sizeof pv) == 8 &&
              !strcmp(pv, "\xCE\x91\xCE\x93\xCE\x94\xCE\x95") &&
              typecase_preview(&T, 2, pv, sizeof pv) == 0 && !pv[0] &&
              typecase_preview(&T, TYPECASE_SCRIPTS, pv, sizeof pv) == -1 &&
              typecase_preview(&T, 1, pv, 8) == -1,
          "previews: the first letters each script has (TM5)");

    /* TM6: install, in parts, never over a file that is there */
    before = sent;
    CHECK(typecase_install(&T, "fonts") == ZXM_E_ARG && sent == before, "never over itself");
    CHECK(zxf_mkdir("Fonts") == 0 && typecase_install(&T, "Fonts") == ZXM_OK &&
              !strcmp(T.installed, "Fonts/a-test.ttf") && tc_same("Fonts/a-test.ttf", font, fa) &&
              sent - before == 8,
          "installed byte for byte, four parts read and four written: %d %s", sent - before,
          LINK.err);
    CHECK(typecase_install(&T, "Fonts") == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_FILES &&
              tc_same("Fonts/a-test.ttf", font, fa),
          "a second install is refused, the first stays: %s", LINK.err);
    CHECK(typecase_install(&T, "Missing") == ZXM_E_REMOTE, "the folder must exist");

    /* the CFF font: a format 12 map, no name (TM3, TM4) */
    {
        static const uint32_t tags1[1] = {0x636D6170};
        const uint8_t *body1[1] = {cmap12};
        uint32_t len1[1] = {tc_cmap12(cmap12, false)};
        uint32_t fb = tc_font(other, 0x4F54544F, 1, tags1, body1, len1);
        CHECK(tc_put("fonts/b-test.otf", other, fb) && typecase_list(&T, "fonts") == ZXM_OK &&
                  T.nfiles == 2 && !strcmp(T.file[1].path, "fonts/b-test.otf"),
              "both listed");
        CHECK(typecase_open(&T, "fonts/b-test.otf") == ZXM_OK && !T.family[0] &&
                  T.covered[7] == 86 && T.cover_x100[7] == 10000 && T.covered[10] == 9 &&
                  T.cover_x100[10] == 4 && T.covered[9] == 1 && T.cover_x100[9] == 0 &&
                  T.covered[0] == 0 && !strcmp(T.installed, "Fonts/a-test.ttf"),
              "Hiragana whole, 9 Han of 20992, 1 Hangul of 11172, no name: %s", LINK.err);
        CHECK(typecase_preview(&T, 10, pv, sizeof pv) == 27 &&
                  !memcmp(pv, "\xE4\xB8\x81\xE4\xB8\x82", 6),
              "Han from U+4E01: U+4E00 is glyph 0");
        len1[0] = tc_cmap12(cmap12, true);
        fb = tc_font(other, 0x4F54544F, 1, tags1, body1, len1);
        CHECK(tc_put("fonts/c-test.otf", other, fb) &&
                  typecase_open(&T, "fonts/c-test.otf") == ZXM_E_ARG &&
                  T.fault == TYPECASE_F_CMAP && T.accepted == 1 && T.covered[7] == 0 &&
                  T.cover_x100[7] == 0,
              "groups that do not rise: no map, nothing covered (TM3)");
    }

    /* what is not a font, or not accepted (TM1, TR1) */
    {
        uint32_t fa2 = tc_font(other, 0x00010000, 3, tags3, body3, len3);
        uint32_t cut = fa2 - ((len3[2] + 3u) & ~3u); /* the name table left out */
        CHECK(tc_put("fonts/d-test.ttf", other, cut) &&
                  typecase_open(&T, "fonts/d-test.ttf") == ZXM_E_ARG && T.accepted == 0 &&
                  T.fault == TYPECASE_F_TABLES && T.ntables == 3 && !T.family[0] &&
                  T.covered[0] == 0,
              "a table past the end: not accepted, nothing more read (TR1)");
        before = sent;
        CHECK(typecase_install(&T, "Fonts") == ZXM_E_STATE && sent == before,
              "a font not accepted is never installed");
        tc_p32(other + 12 + 32 + 12, 0); /* the name now empty, at the very end */
        CHECK(tc_put("fonts/d-test.ttf", other, cut) &&
                  typecase_open(&T, "fonts/d-test.ttf") == ZXM_OK && T.accepted == 1 &&
                  T.fault == TYPECASE_F_NONE && !T.family[0] && T.covered[0] == 52,
              "an empty table ending at the end: accepted, no family: %d", T.fault);
        memcpy(other, "ttcf", 4);
        CHECK(tc_put("fonts/e-test.ttf", other, 400) &&
                  typecase_open(&T, "fonts/e-test.ttf") == ZXM_E_ARG &&
                  T.fault == TYPECASE_F_FORM && T.accepted == 0,
              "a collection is not one font (TM1)");
        CHECK(tc_put("fonts/f-test.ttf", (const uint8_t *) "\0\1\0\0\0\3", 6) &&
                  typecase_open(&T, "fonts/f-test.ttf") == ZXM_E_ARG && T.fault == TYPECASE_F_FORM,
              "shorter than a header");
        tc_p32(other, 0x00010000);
        tc_p16(other + 4, 3);
        CHECK(tc_put("fonts/g-test.ttf", other, 40) &&
                  typecase_open(&T, "fonts/g-test.ttf") == ZXM_E_ARG && T.fault == TYPECASE_F_FORM,
              "shorter than its directory");
        tc_p16(other + 4, TYPECASE_TABLES_MAX + 1u);
        CHECK(tc_put("fonts/h-test.ttf", other, 400) &&
                  typecase_open(&T, "fonts/h-test.ttf") == ZXM_E_ARG &&
                  T.fault == TYPECASE_F_TABLES && T.accepted == 0 &&
                  T.ntables == TYPECASE_TABLES_MAX + 1u,
              "257 tables: not accepted, the records never read (TR1)");
    }

    /* a format 4 glyph array index past the subtable (TM3) */
    {
        uint32_t fa3 = tc_font(other, 0x00010000, 3, tags3, body3, len3);
        tc_p16(other + 60 + 12 + 44, 400); /* Alpha's idRangeOffset */
        CHECK(tc_put("fonts/i-test.ttf", other, fa3) &&
                  typecase_open(&T, "fonts/i-test.ttf") == ZXM_E_ARG &&
                  T.fault == TYPECASE_F_CMAP && !strcmp(T.family, "Testface") && T.covered[0] == 0,
              "a glyph array index outside the map: no map");
    }

    typecase_init(&T, &LINK);
    before = sent;
    CHECK(typecase_install(&T, "Fonts") == ZXM_E_STATE &&
              typecase_preview(&T, 0, pv, sizeof pv) == -1 && sent == before,
          "no font open: nothing to do, nothing sent");
}
