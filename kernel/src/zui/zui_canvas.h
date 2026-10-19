/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_canvas.h — the drawing surface every ZXV screen is painted on.
 *
 * zui draws everything itself into an XRGB8888 pixel surface; a thin present
 * layer per platform (Metal on macOS, Direct3D/Vulkan on Windows and Linux,
 * the UEFI GOP / ramfb framebuffer on bare metal) puts the finished surface on
 * screen. No browser, no system widget set: the same pixels on every target,
 * which is also what makes the output testable byte for byte.
 *
 * Primitives are deliberately few: filled and stroked rounded rectangles with
 * anti-aliased corners, hairlines, dashed outlines (the "held" edge of an AI
 * suggestion), and anti-aliased text from the TrueType rasteriser with a
 * shared glyph cache. Integer only, no allocation: the caller owns the pixel
 * buffer and the cache.
 */
#ifndef ZXV_ZUI_CANVAS_H
#define ZXV_ZUI_CANVAS_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_layout.h"
#include "truetype.h"

typedef struct {
    uint32_t *px; /* XRGB8888, row-major                          */
    int32_t w, h;
    int32_t stride; /* in pixels                                    */
    zui_rect_t clip;
} zui_surface_t;

void zui_surface_init(zui_surface_t *s, uint32_t *px, int32_t w, int32_t h, int32_t stride);
void zui_clip(zui_surface_t *s, zui_rect_t r); /* intersect with surface */
void zui_unclip(zui_surface_t *s);

/* colour helpers */
uint32_t zui_mix(uint32_t a, uint32_t b, uint32_t t255); /* a..b by t/255 */
void zui_blend_px(zui_surface_t *s, int32_t x, int32_t y, uint32_t rgb, uint32_t a255);

/* shapes; alpha is 0..255 for the whole shape */
void zui_fill(zui_surface_t *s, zui_rect_t r, uint32_t rgb);
void zui_fill_a(zui_surface_t *s, zui_rect_t r, uint32_t rgb, uint32_t a255);
void zui_fill_rrect(zui_surface_t *s, zui_rect_t r, int32_t radius, uint32_t rgb, uint32_t a255);
void zui_stroke_rrect(zui_surface_t *s, zui_rect_t r, int32_t radius, int32_t width, uint32_t rgb,
                      uint32_t a255);
void zui_dash_rrect(zui_surface_t *s, zui_rect_t r, int32_t radius, int32_t width, int32_t dash,
                    uint32_t rgb);
/* Engraved outline: the upper/left wall of the cut in `lo`, the lower/right
 * lip catching light in `hi` at alpha a_hi. Reads as carved into the ground
 * rather than floating above it. */
void zui_engrave(zui_surface_t *s, zui_rect_t r, int32_t radius, uint32_t lo, uint32_t hi,
                 uint32_t a_hi);
void zui_hline(zui_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t thick, uint32_t rgb);
void zui_vline(zui_surface_t *s, int32_t x, int32_t y, int32_t h, int32_t thick, uint32_t rgb);

/* ---- text -------------------------------------------------------------- */
#ifndef ZUI_GLYPH_SLOTS
#    define ZUI_GLYPH_SLOTS 1024u
#endif
#ifndef ZUI_GLYPH_POOL
#    define ZUI_GLYPH_POOL (6u * 1024u * 1024u)
#endif

typedef struct {
    const ttf_font_t *font;
    uint32_t gid, size, sub; /* sub = quarter-pixel phase 0..3       */
    int32_t ox, oy;          /* pen origin inside the bitmap         */
    uint32_t w, h, at;       /* bitmap box and pool offset           */
    bool ink;                /* false: nothing to draw (space)       */
    bool used;
} zui_glyph_t;

/* A painter for characters no font in the chain has (emoji faces, say).
 * Draws cp with its pen at (x, baseline) when s is non-NULL, and returns its
 * advance in pixels, or a negative number when it cannot draw cp. */
typedef int32_t (*zui_sym_fn)(zui_surface_t *s, uint32_t cp, int32_t x, int32_t baseline,
                              uint32_t size_px, uint32_t rgb, uint32_t a255, void *ctx);

#define ZUI_FALLBACK_MAX 8u

typedef struct {
    zui_glyph_t slots[ZUI_GLYPH_SLOTS];
    uint32_t used; /* bytes of pool in use                 */
    uint32_t misses, hits;
    /* Where a character goes when the text's own font lacks it: these fonts
     * in order (Golden Latin for arrows, the script faces...), then zui's own
     * drawn keyboard symbols (see zui_symbol), then the painter, and only
     * then the font's empty box. */
    const ttf_font_t *fallback[ZUI_FALLBACK_MAX];
    uint32_t n_fallback;
    zui_sym_fn sym;
    void *sym_ctx;
    uint8_t pool[ZUI_GLYPH_POOL];
} zui_glyph_cache_t;

void zui_glyph_cache_init(zui_glyph_cache_t *c);
/* Set the fallback chain (copied, up to ZUI_FALLBACK_MAX) and the painter. */
void zui_text_fallback(zui_glyph_cache_t *c, const ttf_font_t *const *fonts, uint32_t n);
void zui_text_painter(zui_glyph_cache_t *c, zui_sym_fn fn, void *ctx);

/* zui's own keyboard and mark symbols, drawn as strokes so every shortcut
 * hint reads on every target without a symbol font:
 *   U+2318 command  U+2325 option  U+21E7 shift  U+2303 control
 *   U+23CE / U+21B5 return  U+232B erase  U+238B escape  U+21E5 tab
 *   U+2713 check  U+2715 cross
 * zui_symbol_has says whether cp is one of them; zui_symbol draws it (s may
 * be NULL to measure) and returns its advance in pixels. */
bool zui_symbol_has(uint32_t cp);
int32_t zui_symbol(zui_surface_t *s, uint32_t cp, int32_t x, int32_t baseline, uint32_t size_px,
                   uint32_t rgb, uint32_t a255);

/* An anti-aliased stroke from (x0, y0) to (x1, y1), all in 1/256 px, with
 * round ends, width in 1/256 px. */
void zui_line_q8(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width,
                 uint32_t rgb, uint32_t a255);

typedef struct {
    const ttf_font_t *font;
    zui_glyph_cache_t *cache;
    uint32_t size_px; /* pixels per em                         */
    bool tabular;     /* digits share one advance (numbers)    */
} zui_text_t;

/* Advance of a UTF-8 string in 1/256 px. */
int32_t zui_text_width_q8(const zui_text_t *t, const char *utf8);
static inline int32_t zui_text_width(const zui_text_t *t, const char *utf8)
{
    return (zui_text_width_q8(t, utf8) + 255) >> 8;
}
/* Ascender / descender (descender negative) in pixels for this size. */
int32_t zui_text_ascent(const zui_text_t *t);
int32_t zui_text_descent(const zui_text_t *t);

/* Draw at pen (x, baseline). Returns the pen x after the run, in px. */
int32_t zui_text(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t baseline, uint32_t rgb,
                 const char *utf8);
/* Same, at an overall alpha 0..255 (for engraving highlights). */
int32_t zui_text_a(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t baseline, uint32_t rgb,
                   uint32_t a255, const char *utf8);
/* Draw truncated with an ellipsis to fit max_w. */
int32_t zui_text_fit(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t baseline,
                     int32_t max_w, uint32_t rgb, const char *utf8);

#endif /* ZXV_ZUI_CANVAS_H */
