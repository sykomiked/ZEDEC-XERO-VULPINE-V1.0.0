/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_palette.c — host tests for the runtime colour sets: the dark set
 * is the tokens, the fixed-point WCAG arithmetic, and every text pair in
 * every set meets its contrast level. */
#include <stdio.h>
#include <string.h>

#include "zui_palette.h"
#include "zui_tokens.h"

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

static uint32_t C(zui_scheme_t s, zui_color_role_t r)
{
    return zui_scheme_color(s, r);
}

/* ---- C2: the dark set is zui_tokens.h ----------------------------------- */

static void test_dark_is_tokens(void)
{
    static const uint32_t T[ZUI_C_COUNT] = {
        ZUI_VOID,       ZUI_PANEL,     ZUI_RAISED, ZUI_HAIRLINE, ZUI_EDGE,  ZUI_ENGRAVE_HI,
        ZUI_ENGRAVE_LO, ZUI_TEXT,      ZUI_TEXT_2, ZUI_MUTED,    ZUI_FAINT, ZUI_GOLD,
        ZUI_GOLD_HI,    ZUI_GOLD_GLOW, ZUI_OK,     ZUI_INFO,     ZUI_ALERT};
    for (int r = 0; r < ZUI_C_COUNT; r++)
        CHECK(C(ZUI_SCHEME_DARK, (zui_color_role_t) r) == T[r], "dark %s is the token (%06X)",
              zui_color_role_name((zui_color_role_t) r), C(ZUI_SCHEME_DARK, (zui_color_role_t) r));
    /* spot values the owner named */
    CHECK(C(ZUI_SCHEME_DARK, ZUI_C_VOID) == 0x002911u, "ground #002911");
    CHECK(C(ZUI_SCHEME_DARK, ZUI_C_TEXT) == 0xD8F5E1u, "text #D8F5E1");
    CHECK(C(ZUI_SCHEME_DARK, ZUI_C_GOLD) == 0xF2C230u, "gold #F2C230");
}

/* ---- lookups --------------------------------------------------------------- */

static void test_lookup(void)
{
    CHECK(zui_scheme_colors(ZUI_SCHEME_DARK) != zui_scheme_colors(ZUI_SCHEME_LIGHT),
          "distinct sets");
    CHECK(zui_scheme_colors((zui_scheme_t) 99) == zui_scheme_colors(ZUI_SCHEME_DARK),
          "an unknown scheme is dark");
    CHECK(zui_scheme_colors((zui_scheme_t) -1) == zui_scheme_colors(ZUI_SCHEME_DARK),
          "a negative scheme is dark");
    CHECK(zui_scheme_color(ZUI_SCHEME_LIGHT, (zui_color_role_t) 99) ==
              C(ZUI_SCHEME_LIGHT, ZUI_C_TEXT),
          "an unknown role is the text colour");
    CHECK(zui_scheme_color(ZUI_SCHEME_LIGHT, ZUI_C_COUNT) == C(ZUI_SCHEME_LIGHT, ZUI_C_TEXT),
          "COUNT is not a role");
    for (int s = 0; s < ZUI_SCHEME_COUNT; s++)
        for (int r = 0; r < ZUI_C_COUNT; r++)
            CHECK(zui_scheme_colors((zui_scheme_t) s)->c[r] ==
                      zui_scheme_color((zui_scheme_t) s, (zui_color_role_t) r),
                  "struct and lookup agree (%d, %d)", s, r);
    for (int s = 0; s < ZUI_SCHEME_COUNT; s++)
        for (int r = 0; r < ZUI_C_COUNT; r++)
            CHECK((C((zui_scheme_t) s, (zui_color_role_t) r) >> 24) == 0, "XRGB: top byte clear");
    CHECK(strcmp(zui_scheme_name(ZUI_SCHEME_DARK), "dark") == 0 &&
              strcmp(zui_scheme_name(ZUI_SCHEME_LIGHT), "light") == 0 &&
              strcmp(zui_scheme_name(ZUI_SCHEME_HIGH_CONTRAST), "high contrast") == 0 &&
              strcmp(zui_scheme_name((zui_scheme_t) 7), "?") == 0,
          "scheme names");
    CHECK(strcmp(zui_color_role_name(ZUI_C_VOID), "void") == 0 &&
              strcmp(zui_color_role_name(ZUI_C_TEXT_2), "text 2") == 0 &&
              strcmp(zui_color_role_name(ZUI_C_ALERT), "alert") == 0 &&
              strcmp(zui_color_role_name(ZUI_C_COUNT), "?") == 0,
          "role names");
    for (int r = 0; r < ZUI_C_COUNT; r++)
        CHECK(zui_color_role_name((zui_color_role_t) r)[0] != '?', "role %d named", r);
}

/* ---- C5: the arithmetic ---------------------------------------------------- */

static void test_linear(void)
{
    CHECK(zui_srgb_linear_q16(0) == 0, "0 is dark");
    CHECK(zui_srgb_linear_q16(255) == ZUI_LUM_ONE, "255 is 1.0");
    CHECK(zui_srgb_linear_q16(10) == 199, "the straight segment: 10/255/12.92");
    CHECK(zui_srgb_linear_q16(11) == 219, "11 is still on the straight segment");
    CHECK(zui_srgb_linear_q16(128) >= 14146 && zui_srgb_linear_q16(128) <= 14148,
          "mid grey is about 0.2158 (%u)", zui_srgb_linear_q16(128));
    bool up = true;
    for (int i = 1; i < 256; i++)
        if (zui_srgb_linear_q16((uint8_t) i) <= zui_srgb_linear_q16((uint8_t) (i - 1))) up = false;
    CHECK(up, "strictly increasing");
}

static void test_luminance(void)
{
    CHECK(zui_luminance_q16(0xFFFFFFu) == ZUI_LUM_ONE, "white is 1.0");
    CHECK(zui_luminance_q16(0x000000u) == 0, "black is 0");
    CHECK(zui_luminance_q16(0xFF0000u) == 13933, "red 0.2126 (%u)", zui_luminance_q16(0xFF0000u));
    CHECK(zui_luminance_q16(0x00FF00u) == 46871, "green 0.7152 (%u)", zui_luminance_q16(0x00FF00u));
    CHECK(zui_luminance_q16(0x0000FFu) == 4732, "blue 0.0722 (%u)", zui_luminance_q16(0x0000FFu));
    CHECK(zui_luminance_q16(0xFF000000u) == 0, "the X byte is ignored");
    CHECK(zui_luminance_q16(0x808080u) == zui_srgb_linear_q16(128),
          "a grey's luminance is its level");
    CHECK(zui_luminance_q16(0x00FF00u) > zui_luminance_q16(0xFF0000u) &&
              zui_luminance_q16(0xFF0000u) > zui_luminance_q16(0x0000FFu),
          "green > red > blue");
}

static void test_contrast(void)
{
    CHECK(zui_contrast_q8(0xFFFFFFu, 0x000000u) == ZUI_CONTRAST_MAX_Q8, "white on black is 21:1");
    CHECK(zui_contrast_x100(0xFFFFFFu, 0x000000u) == 2100, "21.00");
    CHECK(zui_contrast_q8(0x000000u, 0xFFFFFFu) == ZUI_CONTRAST_MAX_Q8, "order does not matter");
    CHECK(zui_contrast_q8(0x123456u, 0x123456u) == 256, "same colour is 1:1");
    CHECK(zui_contrast_x100(0xABCDEFu, 0xABCDEFu) == 100, "1.00");
    /* the classic edge: #767676 on white passes AA, #777777 does not */
    CHECK(zui_contrast_ok(0x767676u, 0xFFFFFFu, ZUI_CONTRAST_AA), "#767676 on white passes (%u)",
          zui_contrast_x100(0x767676u, 0xFFFFFFu));
    CHECK(!zui_contrast_ok(0x777777u, 0xFFFFFFu, ZUI_CONTRAST_AA), "#777777 on white fails (%u)",
          zui_contrast_x100(0x777777u, 0xFFFFFFu));
    CHECK(zui_contrast_x100(0x767676u, 0xFFFFFFu) == 454, "4.54 (%u)",
          zui_contrast_x100(0x767676u, 0xFFFFFFu));
    CHECK(zui_contrast_x100(0x777777u, 0xFFFFFFu) == 447, "4.47 (%u)",
          zui_contrast_x100(0x777777u, 0xFFFFFFu));
    /* #595959 on white is the usual AAA edge (7.00) */
    CHECK(zui_contrast_ok(0x595959u, 0xFFFFFFu, ZUI_CONTRAST_AAA), "#595959 on white is AAA (%u)",
          zui_contrast_x100(0x595959u, 0xFFFFFFu));
    CHECK(!zui_contrast_ok(0x5A5A5Au, 0xFFFFFFu, ZUI_CONTRAST_AAA), "#5A5A5A on white is not");
    CHECK(ZUI_CONTRAST_AA == 9u * 256u / 2u && ZUI_CONTRAST_AAA == 7u * 256u &&
              ZUI_CONTRAST_LARGE == 3u * 256u,
          "thresholds");
    /* the ratio grows as one side darkens */
    uint32_t last = 0;
    bool grows = true;
    for (int v = 255; v >= 0; v -= 5) {
        uint32_t c = zui_contrast_q8(0xFFFFFFu, (uint32_t) (v * 0x010101));
        if (c < last) grows = false;
        last = c;
    }
    CHECK(grows, "monotonic in the darker colour");
    CHECK(zui_contrast_q8(0xFFFFFFu, 0x010101u) <= ZUI_CONTRAST_MAX_Q8, "never above 21:1");
}

/* ---- C3, C4, C5: every text pair ---------------------------------------- */

static const zui_color_role_t GROUNDS[] = {ZUI_C_VOID, ZUI_C_PANEL, ZUI_C_RAISED};
static const zui_color_role_t TEXTS[] = {ZUI_C_TEXT, ZUI_C_TEXT_2,  ZUI_C_MUTED,
                                         ZUI_C_GOLD, ZUI_C_GOLD_HI, ZUI_C_GOLD_GLOW,
                                         ZUI_C_OK,   ZUI_C_INFO,    ZUI_C_ALERT};

static void pair(zui_scheme_t s, zui_color_role_t fg, zui_color_role_t bg, uint32_t need,
                 const char *what)
{
    uint32_t q = zui_contrast_q8(C(s, fg), C(s, bg));
    CHECK(q >= need, "%s: %s %s on %s is %u.%02u:1", zui_scheme_name(s), what,
          zui_color_role_name(fg), zui_color_role_name(bg),
          zui_contrast_x100(C(s, fg), C(s, bg)) / 100, zui_contrast_x100(C(s, fg), C(s, bg)) % 100);
}

static void test_pairs(void)
{
    for (int s = 0; s < ZUI_SCHEME_COUNT; s++) {
        zui_scheme_t sc = (zui_scheme_t) s;
        uint32_t body = sc == ZUI_SCHEME_HIGH_CONTRAST ? ZUI_CONTRAST_AAA : ZUI_CONTRAST_AA;
        for (unsigned g = 0; g < sizeof GROUNDS / sizeof GROUNDS[0]; g++) {
            for (unsigned t = 0; t < sizeof TEXTS / sizeof TEXTS[0]; t++)
                pair(sc, TEXTS[t], GROUNDS[g], body, "text");
            /* disabled and placeholder text: 3:1 */
            pair(sc, ZUI_C_FAINT, GROUNDS[g], ZUI_CONTRAST_LARGE, "faint");
        }
        /* the label on a gold button is the ground colour */
        pair(sc, ZUI_C_VOID, ZUI_C_GOLD, body, "button label");
        pair(sc, ZUI_C_VOID, ZUI_C_GOLD_HI, body, "hovered button label");
        /* menu shortcuts are muted on the panel; held meta is gold on the panel */
        pair(sc, ZUI_C_MUTED, ZUI_C_PANEL, body, "shortcut");
        /* the hierarchy reads in order */
        uint32_t t1 = zui_contrast_q8(C(sc, ZUI_C_TEXT), C(sc, ZUI_C_VOID));
        uint32_t t2 = zui_contrast_q8(C(sc, ZUI_C_TEXT_2), C(sc, ZUI_C_VOID));
        uint32_t t3 = zui_contrast_q8(C(sc, ZUI_C_MUTED), C(sc, ZUI_C_VOID));
        uint32_t t4 = zui_contrast_q8(C(sc, ZUI_C_FAINT), C(sc, ZUI_C_VOID));
        CHECK(t1 >= t2 && t2 >= t3 && t3 >= t4, "%s: text > text 2 > muted > faint",
              zui_scheme_name(sc));
        /* the carved steps move away from the ground, in one direction */
        uint32_t lv = zui_luminance_q16(C(sc, ZUI_C_VOID));
        uint32_t lp = zui_luminance_q16(C(sc, ZUI_C_PANEL));
        uint32_t lr = zui_luminance_q16(C(sc, ZUI_C_RAISED));
        bool dark_ground = lv < ZUI_LUM_ONE / 2;
        CHECK(dark_ground ? (lv < lp && lp < lr) : (lv > lp && lp > lr),
              "%s: void, panel, raised step one way", zui_scheme_name(sc));
        CHECK(lp != lv && lr != lp, "%s: the steps are visible", zui_scheme_name(sc));
    }
    /* edges: light and high contrast outline controls at 3:1 (non-text) */
    for (unsigned g = 0; g < 2; g++) {
        pair(ZUI_SCHEME_LIGHT, ZUI_C_EDGE, GROUNDS[g], ZUI_CONTRAST_LARGE, "edge");
        pair(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_EDGE, GROUNDS[g], ZUI_CONTRAST_AAA, "strong edge");
        pair(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_HAIRLINE, GROUNDS[g], ZUI_CONTRAST_LARGE, "hairline");
    }
}

static void test_light(void)
{
    zui_scheme_t L = ZUI_SCHEME_LIGHT, D = ZUI_SCHEME_DARK;
    CHECK(zui_luminance_q16(C(L, ZUI_C_VOID)) > ZUI_LUM_ONE * 4 / 5, "a light ground");
    CHECK(zui_luminance_q16(C(L, ZUI_C_TEXT)) < ZUI_LUM_ONE / 20, "dark text");
    CHECK(C(L, ZUI_C_TEXT) == C(D, ZUI_C_VOID), "the light set's text is the dark ground's green");
    /* the same green family: green leads in the ground, the steps and the text */
    static const zui_color_role_t G[] = {ZUI_C_VOID, ZUI_C_PANEL,  ZUI_C_RAISED, ZUI_C_HAIRLINE,
                                         ZUI_C_TEXT, ZUI_C_TEXT_2, ZUI_C_MUTED,  ZUI_C_FAINT};
    for (unsigned i = 0; i < sizeof G / sizeof G[0]; i++) {
        uint32_t c = C(L, G[i]);
        uint32_t r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
        CHECK(g > r && g >= b, "light %s is green (%06X)", zui_color_role_name(G[i]), c);
    }
    /* gold darkened, still a gold: red > green > blue */
    uint32_t gl = C(L, ZUI_C_GOLD);
    CHECK(zui_luminance_q16(gl) < zui_luminance_q16(C(D, ZUI_C_GOLD)), "the gold is darker");
    CHECK(((gl >> 16) & 255u) > ((gl >> 8) & 255u) && ((gl >> 8) & 255u) > (gl & 255u),
          "still a gold hue (%06X)", gl);
    CHECK(zui_contrast_q8(gl, C(D, ZUI_C_GOLD)) > 256, "differs from the dark gold");
}

static void test_high_contrast(void)
{
    zui_scheme_t H = ZUI_SCHEME_HIGH_CONTRAST, D = ZUI_SCHEME_DARK;
    CHECK(zui_luminance_q16(C(H, ZUI_C_VOID)) < ZUI_LUM_ONE / 100, "a near-black ground");
    CHECK(C(H, ZUI_C_TEXT) == 0xFFFFFFu, "white text");
    CHECK(zui_luminance_q16(C(H, ZUI_C_GOLD)) > zui_luminance_q16(C(D, ZUI_C_GOLD)),
          "a brighter gold");
    CHECK(zui_contrast_q8(C(H, ZUI_C_TEXT), C(H, ZUI_C_VOID)) >= 20u * 256u,
          "text at 20:1 or more");
    CHECK(zui_contrast_q8(C(H, ZUI_C_EDGE), C(H, ZUI_C_VOID)) >
              zui_contrast_q8(C(D, ZUI_C_EDGE), C(D, ZUI_C_VOID)),
          "edges stronger than the dark set's");
    CHECK(zui_contrast_q8(C(H, ZUI_C_HAIRLINE), C(H, ZUI_C_VOID)) >
              zui_contrast_q8(C(D, ZUI_C_HAIRLINE), C(D, ZUI_C_VOID)),
          "hairlines stronger too");
    for (int g = 0; g < 3; g++)
        for (unsigned t = 0; t < sizeof TEXTS / sizeof TEXTS[0]; t++)
            CHECK(zui_contrast_q8(C(H, TEXTS[t]), C(H, GROUNDS[g])) >=
                      zui_contrast_q8(C(D, TEXTS[t]), C(D, GROUNDS[g])),
                  "high contrast %s on %s is no weaker than dark", zui_color_role_name(TEXTS[t]),
                  zui_color_role_name(GROUNDS[g]));
}

int main(void)
{
    test_dark_is_tokens();
    test_lookup();
    test_linear();
    test_luminance();
    test_contrast();
    test_pairs();
    test_light();
    test_high_contrast();
    printf("%s test_zui_palette: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
