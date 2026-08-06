/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* art_studio.h — Miss Potimus's Art Studio: a creative suite for all mediums.
 *
 * WHAT THIS IS
 * -----------
 * A REAL, deterministic, on-device raster core. Miss Potimus paints in pixels
 * and sigils; the canvas never bleeds past its frame. An artwork lives as a
 * fixed-size 8-bit coverage/intensity buffer, painted by integer ops that write
 * ONLY within bounds, coloured from the dragon palette (theme tokens — reused,
 * never re-invented), and exported CONTENT-ADDRESSED (SHA-256 through ipfs) so a
 * finished piece is a listable, self-certifying marketplace asset.
 *
 * The raster technique is the same one src/font/truetype uses: an 8-bit coverage
 * buffer, top-left origin, row-major. The rounded blend
 *     dst = (src*alpha + dst*(255-alpha) + 127) / 255
 * is the graphics layer's exact rounded compositing arithmetic — no truncation,
 * no unrounded divide.
 *
 * OPS BOUNDARY (honest scope): this is the raster + vector + sigil core only.
 * Higher-fidelity media — audio/3D/video authoring — is out of scope; a display
 * or export codec plugs in ABOVE this layer. Nothing here fabricates a medium it
 * does not actually rasterise.
 *
 * Freestanding: integer only, no libc, no allocation, no floating point. The
 * canvas is a fixed-size array; everything is bounds-checked before it is inked.
 */
#ifndef ZXV_ART_STUDIO_H
#define ZXV_ART_STUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "theme.h"    /* the dragon palette tokens — resolved, not invented   */
#include "sigil.h"    /* the sigil/glyph structure — nodes + edges to raster  */

/* Canvas bounds. A fixed frame, so the studio never allocates and never bleeds.
 * 256x256 8-bit = 64 KiB of coverage — a real working canvas, one static array. */
#define ART_MAX_W   256u
#define ART_MAX_H   256u

/* An artwork: an 8-bit coverage/intensity buffer, row-major, top-left origin.
 * Only px[0 .. w*h) is the live image; that exact span is what gets exported. */
typedef struct {
    uint32_t w, h;
    uint8_t  px[ART_MAX_W * ART_MAX_H];
} art_canvas_t;

/* A point for stroke paths. Signed so off-canvas coordinates clip cleanly. */
typedef struct { int32_t x, y; } art_point_t;

/* The studio's palette: every theme token resolved to its 0x00RRGGBB colour.
 * Change the theme, rebuild the palette, and the whole studio recolours. */
typedef struct {
    uint32_t rgb[THEME__COUNT];
} art_palette_t;

/* ---- canvas lifecycle ---- */

/* Prepare a blank canvas of w x h (clamped to ART_MAX_W/ART_MAX_H). The whole
 * pixel span is zeroed, so the same init always yields the same empty frame. */
void art_canvas_init(art_canvas_t *c, uint32_t w, uint32_t h);

/* ---- paint ops (each writes ONLY within [0,w) x [0,h); off-frame is a no-op) ---- */

/* Fill the rectangle (x,y,w,h) with `value`. Negative/oversized rects are
 * clipped to the frame; a fully off-frame rect inks nothing. */
void art_fill_rect(art_canvas_t *c, int32_t x, int32_t y,
                   int32_t w, int32_t h, uint8_t value);

/* Horizontal / vertical runs of `len` pixels from (x,y), clipped to the frame. */
void art_hline(art_canvas_t *c, int32_t x, int32_t y, int32_t len, uint8_t value);
void art_vline(art_canvas_t *c, int32_t x, int32_t y, int32_t len, uint8_t value);

/* Composite `src` over the pixel at (x,y) with `alpha`, using the graphics
 * layer's EXACT rounded blend: dst = (src*alpha + dst*(255-alpha) + 127)/255.
 * Off-frame is a no-op. */
void art_blend_pixel(art_canvas_t *c, int32_t x, int32_t y,
                     uint8_t src, uint8_t alpha);

/* Stroke a polyline through `n` points with `value` (integer Bresenham lines
 * between consecutive points). Every plotted pixel is bounds-checked. */
void art_stroke_path(art_canvas_t *c, const art_point_t *pts, uint32_t n,
                     uint8_t value);

/* ---- palette ---- */

/* Resolve every theme token into the palette. p->rgb[THEME_ACCENT] then holds
 * the theme's real accent RGB (the house default is 0xFF6600 fire orange). */
void art_palette_from_theme(art_palette_t *p, const theme_t *t);

/* Convenience accessor (bounds-checked; returns 0 on a bad slot). */
uint32_t art_palette_color(const art_palette_t *p, theme_color_t slot);

/* ---- sigil / glyph composer ---- */

/* Rasterise a sigil card's circuit — its nodes and edges — onto the canvas.
 * Each lattice node (col,row) maps to a pixel at (ox + col*pitch, oy + row*pitch).
 * Edges are stroked as lines with `edge_val`; nodes are inked as small filled
 * squares with `node_val`. Returns the number of nodes drawn (those whose
 * centre lands on-frame), or -1 on a NULL argument. Every pixel stays in bounds. */
int32_t art_compose_sigil(art_canvas_t *c, const sigil_t *s,
                          int32_t ox, int32_t oy, int32_t pitch,
                          uint8_t node_val, uint8_t edge_val);

/* ---- content-addressed export ---- */

/* out_cid = SHA-256 over exactly the live pixel span px[0 .. w*h), computed
 * through ipfs_cid_from_bytes. This is the artwork's marketplace address: two
 * identical canvases export the identical CID; one changed pixel changes it.
 * Returns 0 on success, -1 on a NULL out_cid. */
int32_t art_export_cid(const art_canvas_t *c, uint8_t out_cid[32]);

#endif /* ZXV_ART_STUDIO_H */
