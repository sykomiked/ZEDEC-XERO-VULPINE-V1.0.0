/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_palette.c — the three colour sets and the WCAG contrast arithmetic
 * (zui_palette.h). */
#include "zui_palette.h"
#include "zui_tokens.h"

/* C2, C3, C4: the sets, in zui_color_role_t order. */
static const zui_colors_t SETS[ZUI_SCHEME_COUNT] = {
    /* dark: zui_tokens.h, value for value */
    {{ZUI_VOID, ZUI_PANEL, ZUI_RAISED, ZUI_HAIRLINE, ZUI_EDGE, ZUI_ENGRAVE_HI, ZUI_ENGRAVE_LO,
      ZUI_TEXT, ZUI_TEXT_2, ZUI_MUTED, ZUI_FAINT, ZUI_GOLD, ZUI_GOLD_HI, ZUI_GOLD_GLOW, ZUI_OK,
      ZUI_INFO, ZUI_ALERT}},
    /* light: a light tint of the same green, dark green text, darkened gold */
    {{
        0xF1F8F3u, /* void: the light ground                    */
        0xE6F2EAu, /* panel                                      */
        0xD9EBDFu, /* raised                                     */
        0xBFD9C8u, /* hairline                                   */
        0x5E8F72u, /* edge: 3:1 on every ground                  */
        0xFFFFFFu, /* engrave hi                                 */
        0x9CC0AAu, /* engrave lo                                 */
        0x002911u, /* text: the dark ground's green              */
        0x0B4026u, /* text 2                                     */
        0x2F5E43u, /* muted                                      */
        0x5A7D68u, /* faint                                      */
        0x7A5600u, /* gold, darkened to read as text             */
        0x6A4A00u, /* gold hi: darker still (hover on light)     */
        0x855F00u, /* gold glow                                  */
        0x0F6B37u, /* ok                                         */
        0x0B4F9Cu, /* info                                       */
        0xA8203Du  /* alert                                      */
    }},
    /* high contrast: near-black ground, white text, bright gold, strong edges */
    {{
        0x000A04u, /* void                                       */
        0x061209u, /* panel                                      */
        0x0D1F14u, /* raised                                     */
        0x8FBFA2u, /* hairline: visible                          */
        0xE8FFF0u, /* edge: strong                               */
        0xFFFFFFu, /* engrave hi                                 */
        0x000000u, /* engrave lo                                 */
        0xFFFFFFu, /* text                                       */
        0xF2FFF6u, /* text 2                                     */
        0xD0EBD9u, /* muted                                      */
        0xA8C7B3u, /* faint                                      */
        0xFFD84Au, /* gold                                       */
        0xFFE680u, /* gold hi                                    */
        0xFFF0B0u, /* gold glow                                  */
        0x8CFFB8u, /* ok                                         */
        0x9CD2FFu, /* info                                       */
        0xFF9EB0u  /* alert                                      */
    }}};

const zui_colors_t *zui_scheme_colors(zui_scheme_t scheme)
{
    if ((uint32_t) scheme >= (uint32_t) ZUI_SCHEME_COUNT) scheme = ZUI_SCHEME_DARK;
    return &SETS[scheme];
}

uint32_t zui_scheme_color(zui_scheme_t scheme, zui_color_role_t role)
{
    const zui_colors_t *c = zui_scheme_colors(scheme);
    if ((uint32_t) role >= (uint32_t) ZUI_C_COUNT) role = ZUI_C_TEXT;
    return c->c[role];
}

const char *zui_scheme_name(zui_scheme_t scheme)
{
    switch (scheme) {
    case ZUI_SCHEME_DARK:
        return "dark";
    case ZUI_SCHEME_LIGHT:
        return "light";
    case ZUI_SCHEME_HIGH_CONTRAST:
        return "high contrast";
    default:
        return "?";
    }
}

const char *zui_color_role_name(zui_color_role_t role)
{
    static const char *const N[ZUI_C_COUNT] = {
        "void",       "panel",     "raised", "hairline", "edge",  "engrave hi",
        "engrave lo", "text",      "text 2", "muted",    "faint", "gold",
        "gold hi",    "gold glow", "ok",     "info",     "alert"};
    if ((uint32_t) role >= (uint32_t) ZUI_C_COUNT) return "?";
    return N[role];
}

/* ---- C5: contrast -------------------------------------------------------- */

/* Generated: round(65536 * linear(c / 255)) with the sRGB transfer function
 * (c/12.92 at or below 0.04045, else ((c + 0.055) / 1.055)^2.4). */
static const uint32_t LIN_Q16[256] = {
    0,     20,    40,    60,    80,    99,    119,   139,   159,   179,   199,   219,   241,
    264,   288,   313,   340,   367,   396,   427,   458,   491,   526,   562,   599,   637,
    677,   718,   761,   805,   851,   898,   947,   997,   1048,  1101,  1156,  1212,  1270,
    1330,  1391,  1453,  1517,  1583,  1651,  1720,  1791,  1863,  1937,  2013,  2090,  2170,
    2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,  2961,  3058,  3157,  3258,  3360,
    3464,  3570,  3678,  3788,  3900,  4014,  4129,  4247,  4366,  4488,  4611,  4736,  4864,
    4993,  5124,  5257,  5392,  5530,  5669,  5810,  5953,  6099,  6246,  6395,  6547,  6701,
    6856,  7014,  7174,  7336,  7500,  7666,  7834,  8004,  8177,  8352,  8529,  8708,  8889,
    9072,  9258,  9446,  9636,  9828,  10022, 10219, 10418, 10619, 10822, 11028, 11236, 11446,
    11658, 11873, 12090, 12309, 12531, 12754, 12981, 13209, 13440, 13673, 13909, 14147, 14387,
    14629, 14874, 15122, 15372, 15624, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727,
    18001, 18278, 18556, 18838, 19121, 19408, 19696, 19988, 20281, 20578, 20876, 21178, 21481,
    21788, 22096, 22408, 22722, 23038, 23357, 23679, 24003, 24329, 24659, 24991, 25325, 25662,
    26002, 26344, 26689, 27036, 27387, 27739, 28095, 28453, 28813, 29177, 29543, 29911, 30283,
    30657, 31033, 31413, 31795, 32180, 32567, 32957, 33350, 33746, 34144, 34545, 34949, 35355,
    35765, 36177, 36591, 37009, 37429, 37852, 38278, 38707, 39138, 39572, 40009, 40449, 40892,
    41337, 41786, 42237, 42691, 43147, 43607, 44069, 44534, 45003, 45474, 45947, 46424, 46904,
    47386, 47871, 48360, 48851, 49345, 49842, 50342, 50844, 51350, 51859, 52370, 52884, 53402,
    53922, 54445, 54972, 55501, 56033, 56568, 57106, 57647, 58191, 58738, 59288, 59841, 60397,
    60956, 61518, 62083, 62651, 63222, 63796, 64373, 64953, 65536,
};

uint32_t zui_srgb_linear_q16(uint8_t c)
{
    return LIN_Q16[c];
}

uint32_t zui_luminance_q16(uint32_t rgb)
{
    /* Rec. 709 weights x10000: 0.2126 R + 0.7152 G + 0.0722 B */
    uint32_t r = LIN_Q16[(rgb >> 16) & 255u];
    uint32_t g = LIN_Q16[(rgb >> 8) & 255u];
    uint32_t b = LIN_Q16[rgb & 255u];
    uint64_t y = (uint64_t) r * 2126u + (uint64_t) g * 7152u + (uint64_t) b * 722u;
    return (uint32_t) ((y + 5000u) / 10000u);
}

/* (L1 + 0.05) / (L2 + 0.05) with both sides x5: 0.05 in Q16 is 3276.8, and
 * 5 x 3276.8 = 16384 exactly, so the flare term carries no rounding. */
#define FLARE_X5 16384u

static void order(uint32_t a, uint32_t b, uint64_t *hi, uint64_t *lo)
{
    uint32_t la = zui_luminance_q16(a), lb = zui_luminance_q16(b);
    *hi = 5u * (uint64_t) (la > lb ? la : lb) + FLARE_X5;
    *lo = 5u * (uint64_t) (la > lb ? lb : la) + FLARE_X5;
}

uint32_t zui_contrast_q8(uint32_t a, uint32_t b)
{
    uint64_t hi, lo;
    order(a, b, &hi, &lo);
    return (uint32_t) ((hi * 256u) / lo);
}

uint32_t zui_contrast_x100(uint32_t a, uint32_t b)
{
    uint64_t hi, lo;
    order(a, b, &hi, &lo);
    return (uint32_t) ((hi * 100u) / lo);
}
