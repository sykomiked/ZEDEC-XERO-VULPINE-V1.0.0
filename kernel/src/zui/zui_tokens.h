/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_tokens.h — the ZXV design tokens: one source for every shell.
 *
 * The native renderer (zui), the SwiftUI studio and the Flutter companion all
 * draw the same product, so the colours, spacing, type sizes and size classes
 * live here once and the other shells read them (see tools/zui/zui_mockup.c
 * --tokens, which prints them as JSON).
 *
 * The palette continues the approved dark + gold mockups (zxv-ui/mockups):
 * a near-black void, two panel steps, hairlines rather than shadows, one gold
 * accent per screen, and three status colours. Values are XRGB8888.
 *
 * Units are POINTS (density-independent). zui_px() turns points into device
 * pixels for the negotiated panel scale, so a 44 pt touch target is 44 pt on a
 * 1x framebuffer and on a 3x phone alike.
 */
#ifndef ZXV_ZUI_TOKENS_H
#define ZXV_ZUI_TOKENS_H

#include <stdint.h>

/* ---- colour ------------------------------------------------------------ */
#define ZUI_VOID        0x0C0F14u   /* window background                  */
#define ZUI_PANEL       0x141922u   /* first surface step                 */
#define ZUI_RAISED      0x1B212Bu   /* inputs, selected rows              */
#define ZUI_HAIRLINE    0x222A35u   /* dividers                           */
#define ZUI_EDGE        0x2C3442u   /* control outlines                   */
#define ZUI_TEXT        0xE7E9EEu   /* primary text                       */
#define ZUI_TEXT_2      0xC3C9D4u   /* secondary text                     */
#define ZUI_MUTED       0x9AA3B2u   /* labels, captions                   */
#define ZUI_FAINT       0x5D6675u   /* disabled, placeholders             */
#define ZUI_GOLD        0xD9A92Eu   /* the one accent                     */
#define ZUI_GOLD_HI     0xE7B84Au   /* accent hover                       */
#define ZUI_GOLD_GLOW   0xF3CF78u   /* accent highlight / glow            */
#define ZUI_OK          0x5CC98Au
#define ZUI_INFO        0x4C8DFFu
#define ZUI_ALERT       0xE05FA8u

/* ---- spacing (points, 4 pt grid) --------------------------------------- */
#define ZUI_S1   4
#define ZUI_S2   8
#define ZUI_S3  12
#define ZUI_S4  16
#define ZUI_S5  24
#define ZUI_S6  32
#define ZUI_S7  48

/* ---- radii: small and consistent; nothing is a pill except chips ------- */
#define ZUI_R_CONTROL  6
#define ZUI_R_PANEL    10

/* ---- type scale (points) ----------------------------------------------- */
#define ZUI_T_CAPTION  11
#define ZUI_T_LABEL    12
#define ZUI_T_BODY     14
#define ZUI_T_TITLE    17
#define ZUI_T_HEADLINE 22
#define ZUI_T_DISPLAY  32

/* ---- interaction ------------------------------------------------------- */
#define ZUI_TOUCH_MIN   44   /* minimum touch target                       */
#define ZUI_POINTER_MIN 28   /* minimum pointer target                     */
#define ZUI_MOTION_MS  180   /* ceiling for any transition                 */

/* ---- size classes (width in points) ------------------------------------ */
typedef enum {
    ZUI_COMPACT = 0,   /* < 600: phone. One pane, bottom bar, sheets.        */
    ZUI_MEDIUM,        /* 600..1023: tablet. Rail + one pane.                */
    ZUI_EXPANDED,      /* 1024..1599: laptop. Rail + list + detail.          */
    ZUI_LARGE          /* >= 1600: desktop, TV. Three panes + inspector.     */
} zui_class_t;

typedef enum {
    ZUI_INPUT_POINTER = 0,  /* mouse/trackpad + keyboard                   */
    ZUI_INPUT_TOUCH,        /* finger                                       */
    ZUI_INPUT_REMOTE,       /* TV remote / gamepad: focus moves, no pointer */
    ZUI_INPUT_NONE          /* headless server profile: no GUI              */
} zui_input_t;

#endif /* ZXV_ZUI_TOKENS_H */
