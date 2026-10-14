/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_layout.h — responsive rules: panel facts in, pane plan out.
 *
 * The screen is a runtime fact (display.h negotiates it). From the panel's
 * pixel width and its scale (pixels per point, x256) this decides the size
 * class, and from the class and the input kind it decides the pane plan every
 * app lays itself out in. Apps never test raw pixel widths.
 *
 * Integer only, freestanding.
 */
#ifndef ZXV_ZUI_LAYOUT_H
#define ZXV_ZUI_LAYOUT_H

#include <stdint.h>
#include "zui_tokens.h"

typedef struct { int32_t x, y, w, h; } zui_rect_t;

typedef struct {
    uint32_t    px_w, px_h;     /* panel in device pixels              */
    uint32_t    scale_q8;       /* pixels per point x256 (256 = 1x)    */
    zui_input_t input;
} zui_screen_t;

/* The pane plan, in device pixels. A rect with w == 0 is absent. */
typedef struct {
    zui_class_t cls;
    zui_rect_t  top;       /* title / status strip                       */
    zui_rect_t  rail;      /* navigation rail (medium and up)            */
    zui_rect_t  list;      /* list pane (expanded and up)                */
    zui_rect_t  main;      /* the work                                   */
    zui_rect_t  inspector; /* large only                                 */
    zui_rect_t  bottom;    /* bottom bar (compact only)                  */
    int32_t     target_px; /* minimum hit target for this input          */
    int32_t     gutter_px; /* outer margin                               */
} zui_plan_t;

/* points -> device pixels, rounded to nearest */
static inline int32_t zui_px(const zui_screen_t *s, int32_t pt) {
    return (int32_t)(((int64_t)pt * (int64_t)s->scale_q8 + 128) >> 8);
}

zui_class_t zui_class_for(const zui_screen_t *s);
const char *zui_class_name(zui_class_t c);
void        zui_plan(const zui_screen_t *s, zui_plan_t *out);

#endif /* ZXV_ZUI_LAYOUT_H */
