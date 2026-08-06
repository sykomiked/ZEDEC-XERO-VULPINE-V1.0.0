/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* art_studio.c — the frame holds. Miss Potimus paints; nothing spills over. */

#include "art_studio.h"
#include "ipfs.h"   /* ipfs_cid_from_bytes — content-address the finished piece */

/* One inked pixel, unconditionally in-bounds. All public ops funnel here so the
 * bounds check lives in exactly one place and can never be forgotten. */
static void put(art_canvas_t *c, int32_t x, int32_t y, uint8_t value) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= c->w || (uint32_t)y >= c->h) return;
    c->px[(uint32_t)y * c->w + (uint32_t)x] = value;
}

void art_canvas_init(art_canvas_t *c, uint32_t w, uint32_t h) {
    if (!c) return;
    if (w > ART_MAX_W) w = ART_MAX_W;
    if (h > ART_MAX_H) h = ART_MAX_H;
    c->w = w;
    c->h = h;
    /* Zero the WHOLE backing store, not just w*h — a blank frame is blank to the
     * last byte, so an export is deterministic regardless of prior contents. */
    for (uint32_t i = 0; i < ART_MAX_W * ART_MAX_H; i++) c->px[i] = 0u;
}

void art_fill_rect(art_canvas_t *c, int32_t x, int32_t y,
                   int32_t w, int32_t h, uint8_t value) {
    if (!c || w <= 0 || h <= 0) return;
    /* Clamp the rect to the frame in INT64 first, so an extreme int32 offset can
     * never overflow the loop coordinate (x+i) — the writes were already clipped
     * by put(), but the arithmetic itself must not be UB. */
    int64_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int64_t x1 = (int64_t)x + w, y1 = (int64_t)y + h;
    if (x1 > (int64_t)c->w) x1 = c->w;
    if (y1 > (int64_t)c->h) y1 = c->h;
    for (int64_t py = y0; py < y1; py++)
        for (int64_t px = x0; px < x1; px++)
            put(c, (int32_t)px, (int32_t)py, value);
}

void art_hline(art_canvas_t *c, int32_t x, int32_t y, int32_t len, uint8_t value) {
    if (!c || len <= 0) return;
    int64_t x0 = x < 0 ? 0 : x, x1 = (int64_t)x + len;
    if (x1 > (int64_t)c->w) x1 = c->w;
    for (int64_t px = x0; px < x1; px++) put(c, (int32_t)px, y, value);
}

void art_vline(art_canvas_t *c, int32_t x, int32_t y, int32_t len, uint8_t value) {
    if (!c || len <= 0) return;
    int64_t y0 = y < 0 ? 0 : y, y1 = (int64_t)y + len;
    if (y1 > (int64_t)c->h) y1 = c->h;
    for (int64_t py = y0; py < y1; py++) put(c, x, (int32_t)py, value);
}

void art_blend_pixel(art_canvas_t *c, int32_t x, int32_t y,
                     uint8_t src, uint8_t alpha) {
    if (!c) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= c->w || (uint32_t)y >= c->h) return;
    uint32_t idx = (uint32_t)y * c->w + (uint32_t)x;
    uint32_t dst = c->px[idx];
    /* The graphics layer's EXACT rounded compositing. Max intermediate is
     * 255*255 + 255*255 + 127 = 130177, comfortably inside uint32. The +127
     * before /255 is round-to-nearest; drop it and this is a different (wrong)
     * blend that the known-answer test will catch. */
    uint32_t out = ((uint32_t)src * alpha + dst * (255u - alpha) + 127u) / 255u;
    c->px[idx] = (uint8_t)out;
}

/* Integer Bresenham between two points; each plotted pixel is bounds-checked. */
static void line(art_canvas_t *c, int32_t x0, int32_t y0,
                 int32_t x1, int32_t y1, uint8_t value) {
    /* Deltas + error accumulator in int64: endpoints at opposite int32 extremes
     * would overflow int32 in (x1 - x0) or (2 * err). */
    int64_t cx = x0, cy = y0;
    int64_t dx = (int64_t)x1 - x0; if (dx < 0) dx = -dx;
    int64_t dy = (int64_t)y1 - y0; if (dy < 0) dy = -dy;
    /* A line spanning more pixels than any canvas could hold (> 1M) is not
     * renderable on a bounded raster without full segment clipping; treat it as a
     * no-op rather than iterate billions of off-frame, put()-clipped steps. Real
     * strokes on a <= ART_MAX (256) canvas never approach this. */
    if (dx > (1 << 20) || dy > (1 << 20)) return;
    int64_t sx = (x0 < x1) ? 1 : -1;
    int64_t sy = (y0 < y1) ? 1 : -1;
    int64_t err = dx - dy;
    for (;;) {
        put(c, (int32_t)cx, (int32_t)cy, value);
        if (cx == x1 && cy == y1) break;
        int64_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
    }
}

void art_stroke_path(art_canvas_t *c, const art_point_t *pts, uint32_t n,
                     uint8_t value) {
    if (!c || !pts || n == 0) return;
    if (n == 1) { put(c, pts[0].x, pts[0].y, value); return; }
    for (uint32_t i = 0; i + 1 < n; i++)
        line(c, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, value);
}

void art_palette_from_theme(art_palette_t *p, const theme_t *t) {
    if (!p || !t) return;
    for (int slot = 0; slot < THEME__COUNT; slot++)
        p->rgb[slot] = theme_get(t, (theme_color_t)slot);
}

uint32_t art_palette_color(const art_palette_t *p, theme_color_t slot) {
    if (!p || slot < 0 || slot >= THEME__COUNT) return 0u;
    return p->rgb[slot];
}

int32_t art_compose_sigil(art_canvas_t *c, const sigil_t *s,
                          int32_t ox, int32_t oy, int32_t pitch,
                          uint8_t node_val, uint8_t edge_val) {
    if (!c || !s) return -1;

    /* Edges first (the data paths), then nodes on top (the operations), so a
     * node's ink is never overwritten by a trace crossing it. */
    for (uint32_t e = 0; e < s->n_edges; e++) {
        uint8_t a = s->edge[e].a, b = s->edge[e].b;
        if (a >= s->n_nodes || b >= s->n_nodes) continue;
        int32_t ax = ox + (int32_t)s->node[a].col * pitch;
        int32_t ay = oy + (int32_t)s->node[a].row * pitch;
        int32_t bx = ox + (int32_t)s->node[b].col * pitch;
        int32_t by = oy + (int32_t)s->node[b].row * pitch;
        line(c, ax, ay, bx, by, edge_val);
    }

    int32_t drawn = 0;
    for (uint32_t k = 0; k < s->n_nodes; k++) {
        int32_t cx = ox + (int32_t)s->node[k].col * pitch;
        int32_t cy = oy + (int32_t)s->node[k].row * pitch;
        /* A 3x3 nub centred on the node (small, but visibly an operation). */
        art_fill_rect(c, cx - 1, cy - 1, 3, 3, node_val);
        if (cx >= 0 && cy >= 0 &&
            (uint32_t)cx < c->w && (uint32_t)cy < c->h) drawn++;
    }
    return drawn;
}

int32_t art_export_cid(const art_canvas_t *c, uint8_t out_cid[32]) {
    if (!c || !out_cid) return -1;
    /* Hash exactly the live pixel span — the artwork IS its bytes. This is the
     * same FIPS-anchored SHA-256 that ipfs uses as a CID, so the piece is a
     * content-addressed, self-certifying asset the marketplace can list. */
    return ipfs_cid_from_bytes(c->px, c->w * c->h, out_cid);
}
