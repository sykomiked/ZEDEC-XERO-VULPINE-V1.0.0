/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_progress.c — the ZXV loading bar and launch screen. See zui_progress.h. */
#include "zui_progress.h"
#include "zui_tokens.h"

static int32_t clamp256(int32_t v)
{
    return v < 0 ? 0 : v > 256 ? 256 : v;
}

static void head(zui_surface_t *s, int32_t x, int32_t cy, int32_t r)
{
    zui_fill_rrect(s, (zui_rect_t){x - r, cy - r, 2 * r, 2 * r}, r, ZUI_GOLD_GLOW, 255u);
}

void zui_bar_draw(zui_surface_t *s, zui_rect_t tr, zui_bar_mode_t mode, uint32_t p, uint32_t tick,
                  int32_t *head_l, int32_t *head_r)
{
    if (!s || tr.w <= 0 || tr.h <= 0) return;
    int32_t th = tr.h, rad = th / 2, mid = tr.x + tr.w / 2, cy = tr.y + th / 2;
    int32_t hl, hr, hr_dot = th + 1 > 3 ? th + 1 : 3;
    zui_fill_rrect(s, tr, rad, ZUI_HAIRLINE, 255u);
    if (p == ZUI_BAR_UNKNOWN) {
        /* breathing at the centre: a short gold span that swells and settles */
        uint32_t ph = tick % 32u, tri = ph < 16u ? ph : 32u - ph; /* 0..16..0 */
        int32_t off = (tr.w / 12) * (int32_t) (4u + tri) / 20;
        hl = mid - off;
        hr = mid + off;
        zui_fill_rrect(s, (zui_rect_t){hl, tr.y, hr - hl, th}, rad, ZUI_GOLD, 255u);
        head(s, hl, cy, hr_dot);
        head(s, hr, cy, hr_dot);
    } else {
        int32_t q = (int32_t) (p > 256u ? 256u : p);
        if (mode == ZUI_BAR_GATHER) {
            hl = tr.x + (mid - tr.x) * q / 256;
            hr = tr.x + tr.w - (tr.x + tr.w - mid) * q / 256;
            if (hl > tr.x)
                zui_fill_rrect(s, (zui_rect_t){tr.x, tr.y, hl - tr.x, th}, rad, ZUI_GOLD, 255u);
            if (hr < tr.x + tr.w)
                zui_fill_rrect(s, (zui_rect_t){hr, tr.y, tr.x + tr.w - hr, th}, rad, ZUI_GOLD,
                               255u);
        } else {
            int32_t half = (tr.w / 2) * q / 256;
            hl = mid - half;
            hr = mid + half;
            if (half > 0)
                zui_fill_rrect(s, (zui_rect_t){hl, tr.y, hr - hl, th}, rad, ZUI_GOLD, 255u);
        }
        if (q > 0 && q < 256) {
            head(s, hl, cy, hr_dot);
            head(s, hr, cy, hr_dot);
        }
    }
    if (head_l) *head_l = hl;
    if (head_r) *head_r = hr;
}

uint32_t zui_bar_lit(int32_t cx, int32_t mid, int32_t hl, int32_t hr, int32_t ramp)
{
    if (ramp < 1) ramp = 1;
    if (cx < mid - ramp / 2) return (uint32_t) clamp256((hl - cx + ramp / 2) * 256 / ramp);
    if (cx > mid + ramp / 2) return (uint32_t) clamp256((cx - hr + ramp / 2) * 256 / ramp);
    return (uint32_t) clamp256(256 - (hr - hl) * 256 / ramp); /* the centre: when they meet */
}

static int32_t S(int32_t scale_q8, int32_t pt)
{
    return (pt * scale_q8 + 128) / 256;
}

void zui_launch_draw(zui_surface_t *s, zui_rect_t a, int32_t sc, const zui_launch_t *l,
                     zui_launch_state_t st, uint32_t tick)
{
    if (!s || !l) return;
    if (sc <= 0) sc = 256;
    zui_fill(s, a, ZUI_VOID);
    uint32_t n = l->n > ZUI_LAUNCH_MAX_PARTS ? ZUI_LAUNCH_MAX_PARTS : l->n;

    /* geometry: mark on top, its bar, the row of tiles, the gathering bar */
    int32_t mark = a.h * 44 / 100;
    if (mark > a.w * 30 / 100) mark = a.w * 30 / 100;
    int32_t gap = S(sc, 24), tile = 0;
    if (n) {
        tile = (a.w * 82 / 100 - (int32_t) (n - 1) * gap) / (int32_t) n;
        if (tile > a.h * 23 / 100) tile = a.h * 23 / 100;
    }
    int32_t thick = S(sc, 3) < 2 ? 2 : S(sc, 3);
    int32_t total = mark + S(sc, 26) + thick + (n ? S(sc, 22) + tile + S(sc, 30) + thick : 0);
    int32_t y = a.y + (a.h - total) / 2, mid = a.x + a.w / 2;

    /* the mark: dim until the parts are in, then lights with a brief warm flash */
    zui_rect_t mb = {mid - mark / 2, y, mark, mark};
    uint32_t lt = st.light > 256u ? 256u : st.light;
    uint32_t flash = lt <= 128u ? lt * 2u : (256u - lt) * 2u;
    if (flash)
        zui_fill_rrect(
            s, (zui_rect_t){mb.x - mark / 10, mb.y - mark / 10, mark * 12 / 10, mark * 12 / 10},
            mark / 3, ZUI_GOLD_GLOW, flash * 36u / 256u);
    if (l->mark) l->mark(s, mb, lt * 2u > 256u ? 256u : lt * 2u, l->ctx[0]);
    y += mark + S(sc, 26);

    /* the bar under the mark: releases once the mark is lit */
    int32_t bw = mark * 115 / 100;
    zui_rect_t top = {mid - bw / 2, y, bw, thick};
    if (st.light >= 256u)
        zui_bar_draw(s, top, ZUI_BAR_RELEASE, st.release, tick, 0, 0);
    else
        zui_fill_rrect(s, top, thick / 2, ZUI_HAIRLINE, 255u);
    y += thick;
    if (!n) return;
    y += S(sc, 22);

    /* the row: gathered and lit by the bottom bar */
    int32_t row_w = (int32_t) n * tile + (int32_t) (n - 1) * gap, x0 = mid - row_w / 2;
    int32_t bar_y = y + tile + S(sc, 30), hl, hr;
    zui_bar_draw(s, (zui_rect_t){x0, bar_y, row_w, thick}, ZUI_BAR_GATHER, st.gather, tick, &hl,
                 &hr);
    int32_t ramp = S(sc, 60) < tile / 2 ? S(sc, 60) : tile / 2; /* never lit before the bar moves */
    for (uint32_t i = 0; i < n; i++) {
        zui_rect_t t = {x0 + (int32_t) i * (tile + gap), y, tile, tile};
        uint32_t lit = zui_bar_lit(t.x + tile / 2, mid, hl, hr, ramp);
        int32_t r = S(sc, 14);
        zui_fill_rrect(s, t, r, ZUI_PANEL, 255u);
        zui_stroke_rrect(s, t, r, S(sc, 2) < 1 ? 1 : S(sc, 2), ZUI_ENGRAVE_LO, 255u);
        if (lit) zui_stroke_rrect(s, t, r, 1, ZUI_GOLD, lit * 150u / 256u);
        if (l->parts[i]) {
            int32_t in = S(sc, 15);
            l->parts[i](s, (zui_rect_t){t.x + in, t.y + in, tile - 2 * in, tile - 2 * in}, lit,
                        l->ctx[1 + i]);
        }
    }
}
