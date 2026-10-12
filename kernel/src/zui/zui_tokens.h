/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_tokens.h — the ZXV design tokens: one source for every shell.
 *
 * The native renderer (zui), the SwiftUI studio and the Flutter companion all
 * draw the same product, so the colours, spacing, type sizes and size classes
 * live here once and the other shells read them (see tools/zui/zui_mockup.c
 * --tokens, which prints them as JSON).
 *
 * The palette is the owner's green + gold scheme: a deep green ground, carved
 * steps of the same hue, engraved hairlines, one gold accent per screen, and
 * three status colours. Values are XRGB8888.
 *
 * Units are POINTS (density-independent). zui_px() turns points into device
 * pixels for the negotiated panel scale, so a 44 pt touch target is 44 pt on a
 * 1x framebuffer and on a 3x phone alike.
 */
#ifndef ZXV_ZUI_TOKENS_H
#define ZXV_ZUI_TOKENS_H

#include <stdint.h>

/* ---- colour ------------------------------------------------------------ */
/* Owner's scheme (2026-10-12): deep green #002911 ground, engraved with a
 * very light tint of the same green, light green text, golden-yellow accent.
 * Every surface is a step of the same hue, so depth reads as carving into one
 * material rather than cards floating on black. */
#define ZUI_VOID       0x002911u /* the ground                          */
#define ZUI_PANEL      0x04331Au /* first carved step                   */
#define ZUI_RAISED     0x0B4026u /* inputs, selected rows               */
#define ZUI_HAIRLINE   0x15502Fu /* dividers (tint of the ground)       */
#define ZUI_EDGE       0x2A6A47u /* control outlines                    */
#define ZUI_ENGRAVE_HI 0xCFE8D8u /* the light tint: lower lip of a cut  */
#define ZUI_ENGRAVE_LO 0x001A0Bu /* the shade: upper wall of a cut      */
#define ZUI_TEXT       0xD8F5E1u /* light green text                    */
#define ZUI_TEXT_2     0xB2E0C2u
#define ZUI_MUTED      0x84B795u
#define ZUI_FAINT      0x5E8F72u
#define ZUI_GOLD       0xF2C230u /* the one accent: golden yellow       */
#define ZUI_GOLD_HI    0xFFD24Du
#define ZUI_GOLD_GLOW  0xFFE38Au
#define ZUI_OK         0x7BE8A6u
#define ZUI_INFO       0x6FB8FFu
#define ZUI_ALERT      0xFF7A93u

/* ---- spacing (points, 4 pt grid) --------------------------------------- */
#define ZUI_S1 4
#define ZUI_S2 8
#define ZUI_S3 12
#define ZUI_S4 16
#define ZUI_S5 24
#define ZUI_S6 32
#define ZUI_S7 48

/* ---- radii: small and consistent; nothing is a pill except chips ------- */
#define ZUI_R_CONTROL 6
#define ZUI_R_PANEL   10

/* ---- type scale (points) ----------------------------------------------- */
#define ZUI_T_CAPTION  11
#define ZUI_T_LABEL    12
#define ZUI_T_BODY     14
#define ZUI_T_TITLE    17
#define ZUI_T_HEADLINE 22
#define ZUI_T_DISPLAY  32

/* ---- interaction ------------------------------------------------------- */
#define ZUI_TOUCH_MIN   44  /* minimum touch target                       */
#define ZUI_POINTER_MIN 28  /* minimum pointer target                     */
#define ZUI_MOTION_MS   180 /* ceiling for any transition                 */

/* ---- size classes (width in points) ------------------------------------ */
typedef enum {
    ZUI_COMPACT = 0, /* < 600: phone. One pane, bottom bar, sheets.        */
    ZUI_MEDIUM,      /* 600..1023: tablet. Rail + one pane.                */
    ZUI_EXPANDED,    /* 1024..1599: laptop. Rail + list + detail.          */
    ZUI_LARGE        /* >= 1600: desktop, TV. Three panes + inspector.     */
} zui_class_t;

typedef enum {
    ZUI_INPUT_POINTER = 0, /* mouse/trackpad + keyboard                   */
    ZUI_INPUT_TOUCH,       /* finger                                       */
    ZUI_INPUT_REMOTE,      /* TV remote / gamepad: focus moves, no pointer */
    ZUI_INPUT_NONE         /* headless server profile: no GUI              */
} zui_input_t;

#endif /* ZXV_ZUI_TOKENS_H */
