/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_layout.c — size classes and pane plans. See zui_layout.h. */
#include "zui_layout.h"

static int32_t width_pt(const zui_screen_t *s) {
    if (!s || s->scale_q8 == 0) return 0;
    return (int32_t)(((uint64_t)s->px_w << 8) / s->scale_q8);
}

zui_class_t zui_class_for(const zui_screen_t *s) {
    int32_t w = width_pt(s);
    if (w < 600)  return ZUI_COMPACT;
    if (w < 1024) return ZUI_MEDIUM;
    if (w < 1600) return ZUI_EXPANDED;
    return ZUI_LARGE;
}

const char *zui_class_name(zui_class_t c) {
    switch (c) {
    case ZUI_COMPACT:  return "compact";
    case ZUI_MEDIUM:   return "medium";
    case ZUI_EXPANDED: return "expanded";
    case ZUI_LARGE:    return "large";
    }
    return "compact";
}

static zui_rect_t r(int32_t x, int32_t y, int32_t w, int32_t h) {
    zui_rect_t o = { x, y, w < 0 ? 0 : w, h < 0 ? 0 : h };
    return o;
}

void zui_plan(const zui_screen_t *s, zui_plan_t *p) {
    zui_plan_t z = {0};
    if (!s || !p) return;
    int32_t W = (int32_t)s->px_w, H = (int32_t)s->px_h;
    z.cls = zui_class_for(s);
    z.target_px = zui_px(s, s->input == ZUI_INPUT_POINTER ? ZUI_POINTER_MIN
                                                          : ZUI_TOUCH_MIN);
    z.gutter_px = zui_px(s, z.cls == ZUI_COMPACT ? ZUI_S4 : ZUI_S5);

    int32_t top_h = zui_px(s, z.cls == ZUI_COMPACT ? 52 : 44);
    z.top = r(0, 0, W, top_h);
    int32_t body_y = top_h, body_h = H - top_h;

    switch (z.cls) {
    case ZUI_COMPACT: {
        int32_t bar = zui_px(s, 64);
        z.bottom = r(0, H - bar, W, bar);
        z.main   = r(0, body_y, W, body_h - bar);
        break;
    }
    case ZUI_MEDIUM: {
        int32_t rail = zui_px(s, 72);
        z.rail = r(0, body_y, rail, body_h);
        z.main = r(rail, body_y, W - rail, body_h);
        break;
    }
    case ZUI_EXPANDED: {
        int32_t rail = zui_px(s, 72), list = zui_px(s, 300);
        z.rail = r(0, body_y, rail, body_h);
        z.list = r(rail, body_y, list, body_h);
        z.main = r(rail + list, body_y, W - rail - list, body_h);
        break;
    }
    case ZUI_LARGE: {
        int32_t rail = zui_px(s, 220), list = zui_px(s, 320),
                insp = zui_px(s, 340);
        z.rail = r(0, body_y, rail, body_h);
        z.list = r(rail, body_y, list, body_h);
        z.main = r(rail + list, body_y, W - rail - list - insp, body_h);
        z.inspector = r(W - insp, body_y, insp, body_h);
        break;
    }
    }
    *p = z;
}
