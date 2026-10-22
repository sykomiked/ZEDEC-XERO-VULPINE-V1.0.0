/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_progress.c — the ZXV loading bar and launch screen. See zui_progress.h. */
#include "zui_progress.h"
#include "zui_palette.h"
#include "zui_tokens.h"

static int32_t clamp256(int32_t v)
{
    return v < 0 ? 0 : v > 256 ? 256 : v;
}

static void head(zui_surface_t *s, int32_t x, int32_t cy, int32_t r, uint32_t rgb)
{
    zui_fill_rrect(s, (zui_rect_t){x - r, cy - r, 2 * r, 2 * r}, r, rgb, 255u);
}

void zui_bar_draw(zui_surface_t *s, zui_rect_t tr, zui_bar_mode_t mode, uint32_t p, uint32_t tick,
                  int32_t *head_l, int32_t *head_r)
{
    zui_bar_draw_in(s, tr, mode, p, tick, head_l, head_r, 0);
}

void zui_bar_draw_in(zui_surface_t *s, zui_rect_t tr, zui_bar_mode_t mode, uint32_t p,
                     uint32_t tick, int32_t *head_l, int32_t *head_r, const zui_colors_t *set)
{
    if (!s || tr.w <= 0 || tr.h <= 0) return;
    uint32_t gold = zui_color(set, ZUI_C_GOLD), glow = zui_color(set, ZUI_C_GOLD_GLOW);
    int32_t th = tr.h, rad = th / 2, mid = tr.x + tr.w / 2, cy = tr.y + th / 2;
    int32_t hl, hr, hr_dot = th + 1 > 3 ? th + 1 : 3;
    zui_fill_rrect(s, tr, rad, zui_color(set, ZUI_C_HAIRLINE), 255u);
    if (p == ZUI_BAR_UNKNOWN) {
        /* breathing at the centre: a short gold span that swells and settles */
        uint32_t ph = tick % 32u, tri = ph < 16u ? ph : 32u - ph; /* 0..16..0 */
        int32_t off = (tr.w / 12) * (int32_t) (4u + tri) / 20;
        hl = mid - off;
        hr = mid + off;
        zui_fill_rrect(s, (zui_rect_t){hl, tr.y, hr - hl, th}, rad, gold, 255u);
        head(s, hl, cy, hr_dot, glow);
        head(s, hr, cy, hr_dot, glow);
    } else {
        int32_t q = (int32_t) (p > 256u ? 256u : p);
        if (mode == ZUI_BAR_GATHER) {
            hl = tr.x + (mid - tr.x) * q / 256;
            hr = tr.x + tr.w - (tr.x + tr.w - mid) * q / 256;
            if (hl > tr.x)
                zui_fill_rrect(s, (zui_rect_t){tr.x, tr.y, hl - tr.x, th}, rad, gold, 255u);
            if (hr < tr.x + tr.w)
                zui_fill_rrect(s, (zui_rect_t){hr, tr.y, tr.x + tr.w - hr, th}, rad, gold, 255u);
        } else {
            int32_t half = (tr.w / 2) * q / 256;
            hl = mid - half;
            hr = mid + half;
            if (half > 0) zui_fill_rrect(s, (zui_rect_t){hl, tr.y, hr - hl, th}, rad, gold, 255u);
        }
        if (q > 0 && q < 256) {
            head(s, hl, cy, hr_dot, glow);
            head(s, hr, cy, hr_dot, glow);
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

static void rise(const zui_launch_t *l, zui_surface_t *s, zui_rect_t r, int32_t radius, int32_t z)
{
    if (l->rise && l->rise->fn) l->rise->fn(s, l->rise->ctx, r, radius, z);
}

static void draw_row(zui_surface_t *s, int32_t sc, const zui_launch_t *l, zui_launch_state_t st,
                     uint32_t tick, int32_t mid, int32_t y, uint32_t n, int32_t tile, int32_t gap,
                     int32_t thick)
{
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
        if (lit) rise(l, s, t, r, S(sc, 14) * (int32_t) lit / 256); /* lit tiles rise */
    }
}

static int32_t smoother(int32_t t)
{ /* 0..256 -> 0..256, smootherstep */
    int64_t u = t < 0 ? 0 : t > 256 ? 256 : t;
    return (int32_t) ((u * u * u * (u * (u * 6 - 15 * 256) + 10 * 65536)) >> 32);
}

/* five vault bolts across the centre seam: they lock in as the top bar fills
 * and throw back into their doors over the first quarter of the opening */
static void draw_bolts(zui_surface_t *s, zui_rect_t a, int32_t sc, zui_launch_state_t st)
{
    int32_t mid = a.x + a.w / 2, bl = S(sc, 86), bh = S(sc, 22), kw = S(sc, 58),
            kh = bh + S(sc, 18);
    if (bh < 4) bh = 4;
    int32_t thr = st.open >= 64u ? 256 : (int32_t) st.open * 4;
    thr = smoother(thr);
    for (int32_t i = 0; i < ZUI_VAULT_BOLTS; i++) {
        uint32_t at = (uint32_t) (i + 1) * 256u / (ZUI_VAULT_BOLTS + 1);
        if (st.release == ZUI_BAR_UNKNOWN || st.release < at) continue;
        int32_t in = (int32_t) (st.release - at) * 256 / 40; /* locks over 40/256 of the bar */
        in = smoother(in > 256 ? 256 : in);
        int32_t y = a.y + a.h * (2 * i + 1) / (2 * ZUI_VAULT_BOLTS), al = in * 255 / 256;
        zui_fill_rrect(s, (zui_rect_t){mid - kw / 2, y - kh / 2, kw, kh}, S(sc, 8), ZUI_PANEL,
                       (uint32_t) al);
        zui_stroke_rrect(s, (zui_rect_t){mid - kw / 2, y - kh / 2, kw, kh}, S(sc, 8),
                         S(sc, 2) < 1 ? 1 : S(sc, 2), ZUI_GOLD, (uint32_t) al);
        for (int32_t side = -1; side <= 1; side += 2) {
            int32_t tip =
                mid + side * (S(sc, 2) + (256 - in) * bl * 9 / 2560 + thr * bl * 95 / 25600);
            int32_t x0 = side < 0 ? tip - bl : tip, r = bh / 2;
            zui_fill_rrect(s, (zui_rect_t){x0, y - r, bl, bh}, r, ZUI_GOLD, (uint32_t) al);
            zui_fill_rrect(s,
                           (zui_rect_t){x0 + S(sc, 4), y - r + S(sc, 3), bl - S(sc, 8), S(sc, 5)},
                           S(sc, 3), ZUI_GOLD_GLOW, (uint32_t) (al * 230 / 255));
            int32_t bx = side < 0 ? x0 : x0 + bl, br = S(sc, 15); /* the boss on the door */
            zui_fill_rrect(s, (zui_rect_t){bx - br, y - br, 2 * br, 2 * br}, br, ZUI_GOLD,
                           (uint32_t) al);
            zui_fill_rrect(s, (zui_rect_t){bx - br / 2, y - br / 2, br, br}, br / 2, ZUI_GOLD_GLOW,
                           (uint32_t) al);
        }
    }
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
    if (lt) rise(l, s, mb, 0, S(sc, 24) * (int32_t) lt / 256);
    y += mark + S(sc, 26);

    /* the bar under the mark: releases once the mark is lit */
    int32_t bw = mark * 115 / 100;
    zui_rect_t top = {mid - bw / 2, y, bw, thick};
    if (st.light >= 256u)
        zui_bar_draw(s, top, ZUI_BAR_RELEASE, st.release, tick, 0, 0);
    else
        zui_fill_rrect(s, top, thick / 2, ZUI_HAIRLINE, 255u);
    y += thick;
    if (n) draw_row(s, sc, l, st, tick, mid, y, n, tile, gap, thick);
    if (st.light >= 256u) draw_bolts(s, a, sc, st);
}

void zui_vault_draw(zui_surface_t *s, zui_rect_t a, int32_t sc, const zui_surface_t *face,
                    const zui_surface_t *behind, uint32_t open)
{
    if (!s || !face || !behind || a.w <= 0 || a.h <= 0) return;
    if (face->w < a.w || face->h < a.h || behind->w < a.w || behind->h < a.h) return;
    if (sc <= 0) sc = 256;
    int32_t half = a.w / 2, o = open <= 64u ? 0 : smoother(((int32_t) open - 64) * 256 / 192);
    int32_t gap = (half + S(sc, 30)) * o / 256, lip = S(sc, 3) < 2 ? 2 : S(sc, 3);
    for (int32_t y = 0; y < a.h; y++) {
        int32_t sy = a.y + y;
        if (sy < s->clip.y || sy >= s->clip.y + s->clip.h) continue;
        const uint32_t *fr = face->px + (int64_t) y * face->stride,
                       *br = behind->px + (int64_t) y * behind->stride;
        uint32_t *dr = s->px + (int64_t) sy * s->stride;
        for (int32_t x = 0; x < a.w; x++) {
            int32_t sx = a.x + x;
            if (sx < s->clip.x || sx >= s->clip.x + s->clip.w) continue;
            uint32_t c;
            if (x < half - gap)
                c = fr[x + gap]; /* left door, slid left   */
            else if (x >= half + gap)
                c = fr[x - gap]; /* right door, slid right */
            else
                c = gap ? zui_mix(br[x], 0x001408u, (uint32_t) (150 - 150 * o / 256)) : fr[x];
            if (gap && ((x >= half - gap - lip && x < half - gap) ||
                        (x >= half + gap && x < half + gap + lip)))
                c = ZUI_GOLD; /* each door's gold lip   */
            dr[sx] = c;
        }
    }
}
