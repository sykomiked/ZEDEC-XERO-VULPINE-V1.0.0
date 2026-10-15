/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_palette.h — runtime colour schemes: the same colour roles as
 * zui_tokens.h, in three sets a person can switch between.
 *
 * zui_tokens.h fixes one scheme at compile time: the deep green ground, light
 * green text and the gold accent. Some people need more light, some need more
 * contrast. This header names every colour by its role and gives three sets:
 *
 *   C1 SAME ROLES. zui_color_role_t lists exactly the colours of zui_tokens.h,
 *      in the same order; a renderer that reads colours through
 *      zui_scheme_color() draws the same screen whichever set is chosen.
 *   C2 DARK IS THE TOKENS. ZUI_SCHEME_DARK holds the zui_tokens.h values
 *      unchanged (the tests check every one), so switching to it is a no-op.
 *   C3 LIGHT KEEPS THE FAMILY. ZUI_SCHEME_LIGHT is a light tint of the same
 *      green with dark green text; the gold accent is darkened until it reads
 *      as text on the light ground, and the label on a gold button (the
 *      ground colour) still reads on it.
 *   C4 HIGH CONTRAST. ZUI_SCHEME_HIGH_CONTRAST is a near-black ground, white
 *      text, bright gold and strong edges.
 *   C5 MEASURED, NOT GUESSED. zui_luminance_q16 and zui_contrast_q8 follow the
 *      WCAG 2.x formulas in fixed point (no floats). Every text colour on
 *      every ground meets AA (4.5:1) in every set; in high contrast every text
 *      colour meets AAA (7:1). Faint (disabled and placeholder text, which the
 *      guidelines exempt) meets 3:1. The tests check each pair.
 *   C6 THE RENDERER READS THE SET. zui_widget, zui_wm, zui_dock, zui_lock,
 *      zui_notice, zui_osk and the app views each hold a `colors` pointer to
 *      a zui_colors_t (zui_scheme_colors(scheme), or zui_ws_colors with the
 *      person's accent) and paint every colour by role through zui_color.
 *      NULL is the dark set, straight from zui_tokens.h, so a module that
 *      paints does not have to link this file, and a NULL pointer draws the
 *      same pixels as ZUI_SCHEME_DARK (C2). The pointer is borrowed: the
 *      set must outlive the module (the zui_scheme_colors sets are static).
 *
 * Values are XRGB8888. Integer only, no allocation, no libc.
 */
#ifndef ZXV_ZUI_PALETTE_H
#define ZXV_ZUI_PALETTE_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_tokens.h"

/* C1: the roles, in zui_tokens.h order. */
typedef enum {
    ZUI_C_VOID = 0,   /* the ground                       */
    ZUI_C_PANEL,      /* first carved step                */
    ZUI_C_RAISED,     /* inputs, selected rows            */
    ZUI_C_HAIRLINE,   /* dividers                         */
    ZUI_C_EDGE,       /* control outlines                 */
    ZUI_C_ENGRAVE_HI, /* lower lip of a cut (catches light) */
    ZUI_C_ENGRAVE_LO, /* upper wall of a cut (shade)      */
    ZUI_C_TEXT,       /* body text                        */
    ZUI_C_TEXT_2,     /* secondary text                   */
    ZUI_C_MUTED,      /* captions, details                */
    ZUI_C_FAINT,      /* disabled text, placeholders      */
    ZUI_C_GOLD,       /* the one accent                   */
    ZUI_C_GOLD_HI,    /* accent, hovered                  */
    ZUI_C_GOLD_GLOW,  /* accent highlight (preedit text)  */
    ZUI_C_OK,         /* status: good                     */
    ZUI_C_INFO,       /* status: information              */
    ZUI_C_ALERT,      /* status: error, danger            */
    ZUI_C_COUNT
} zui_color_role_t;

typedef enum {
    ZUI_SCHEME_DARK = 0,      /* C2: zui_tokens.h            */
    ZUI_SCHEME_LIGHT,         /* C3                          */
    ZUI_SCHEME_HIGH_CONTRAST, /* C4                          */
    ZUI_SCHEME_COUNT
} zui_scheme_t;

typedef struct {
    uint32_t c[ZUI_C_COUNT]; /* indexed by zui_color_role_t */
} zui_colors_t;

/* C6: one colour of a set by role; set NULL is the dark set (zui_tokens.h),
 * and an unknown role gives the set's text colour. Header only, so painting
 * modules need no link to zui_palette.c. */
static inline uint32_t zui_color(const zui_colors_t *set, zui_color_role_t role)
{
    static const uint32_t dark[ZUI_C_COUNT] = {
        ZUI_VOID,       ZUI_PANEL,     ZUI_RAISED, ZUI_HAIRLINE, ZUI_EDGE,  ZUI_ENGRAVE_HI,
        ZUI_ENGRAVE_LO, ZUI_TEXT,      ZUI_TEXT_2, ZUI_MUTED,    ZUI_FAINT, ZUI_GOLD,
        ZUI_GOLD_HI,    ZUI_GOLD_GLOW, ZUI_OK,     ZUI_INFO,     ZUI_ALERT};
    if ((uint32_t) role >= (uint32_t) ZUI_C_COUNT) role = ZUI_C_TEXT;
    return set ? set->c[role] : dark[role];
}

/* C6: a colour stored as a zui_tokens.h value (a label's colour, a list
 * row's lead mark) names that role: in `set` it is that role's colour.
 * Any other colour, and every colour with set NULL, is returned as is. */
static inline uint32_t zui_color_remap(const zui_colors_t *set, uint32_t rgb)
{
    if (!set) return rgb;
    for (uint32_t r = 0; r < (uint32_t) ZUI_C_COUNT; r++)
        if (zui_color(0, (zui_color_role_t) r) == rgb) return set->c[r];
    return rgb;
}

/* The set for a scheme; an unknown scheme gives the dark set. */
const zui_colors_t *zui_scheme_colors(zui_scheme_t scheme);
/* One colour by role; an unknown role gives the scheme's text colour. */
uint32_t zui_scheme_color(zui_scheme_t scheme, zui_color_role_t role);
const char *zui_scheme_name(zui_scheme_t scheme);       /* "dark", "light", "high contrast" */
const char *zui_color_role_name(zui_color_role_t role); /* "void", "text", "gold", ...      */

/* ---- C5: WCAG 2.x contrast in fixed point -------------------------------- */

#define ZUI_LUM_ONE         65536u /* relative luminance 1.0 in Q16            */
#define ZUI_CONTRAST_AA     1152u  /* 4.5:1 in Q8: body text, AA               */
#define ZUI_CONTRAST_AAA    1792u  /* 7:1 in Q8: body text, AAA                */
#define ZUI_CONTRAST_LARGE  768u   /* 3:1 in Q8: large text, UI edges, disabled */
#define ZUI_CONTRAST_MAX_Q8 5376u  /* 21:1, white on black                     */

/* sRGB channel 0..255 to linear light, Q16 (0..65536). */
uint32_t zui_srgb_linear_q16(uint8_t c);
/* Relative luminance of an XRGB colour, Q16 (0..65536). */
uint32_t zui_luminance_q16(uint32_t rgb);
/* Contrast ratio of two colours (order does not matter), Q8: 256 = 1:1,
 * 5376 = 21:1. Rounded down, so a pass is never rounded into one. */
uint32_t zui_contrast_q8(uint32_t a, uint32_t b);
/* Contrast ratio x100 (450 = 4.50:1), rounded down, for messages and logs. */
uint32_t zui_contrast_x100(uint32_t a, uint32_t b);
static inline bool zui_contrast_ok(uint32_t fg, uint32_t bg, uint32_t need_q8)
{
    return zui_contrast_q8(fg, bg) >= need_q8;
}

#endif /* ZXV_ZUI_PALETTE_H */
