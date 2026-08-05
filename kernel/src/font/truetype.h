/* truetype.h — a TrueType outline rasteriser (the glyph layer for src/font)
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * -----------
 * font.h routes text to a face and calls a font_glyph_ops_t backend to turn a
 * codepoint into pixels. THIS is that backend: a from-scratch TrueType parser
 * and scan-line rasteriser. It reads the sfnt table directory, maps a Unicode
 * codepoint through the `cmap` to a glyph id, walks that glyph's outline in the
 * `glyf` table, flattens its quadratic curves, and fills it with a
 * supersampled non-zero-winding scan-line rasteriser into an 8-bit coverage
 * bitmap. With this bound, the 109 ASCW faces render as real glyphs.
 *
 * THE INPUT IS UNTRUSTED. A font file may be malformed or hostile, so every
 * table offset, array length, and point count is bounds-checked against the
 * file length before it is followed. A bad font yields "no glyph", never an
 * out-of-bounds read or a hang.
 *
 * SCOPE, STATED HONESTLY
 * ----------------------
 *  - SIMPLE glyphs (quadratic contours) render. COMPOSITE glyphs (glyphs built
 *    from other glyphs) are detected and declined — ttf_render returns false
 *    for them rather than drawing something wrong. Many Latin faces use only
 *    simple glyphs; some scripts use composites, which is the follow-on.
 *  - `cmap` format 4 (the near-universal Unicode BMP format) is supported.
 *    Format 12 (astral planes) is the follow-on.
 *  - No hinting, no kerning, no ligatures. This produces correct filled
 *    outlines with anti-aliasing, not a shaping engine.
 *
 * Freestanding: integer only, no floating point, no allocation. Fixed-point
 * (16.16) is used for the scale and edge arithmetic.
 */
#ifndef ZXV_TRUETYPE_H
#define ZXV_TRUETYPE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *data;
    uint32_t len;
    uint32_t units_per_em;
    uint32_t num_glyphs;
    uint32_t loca_off, loca_len;
    bool     loca_long;         /* indexToLocFormat: 1 = 32-bit offsets       */
    uint32_t glyf_off, glyf_len;
    uint32_t cmap_sub_off;      /* offset of the chosen format-4 subtable      */
    bool     valid;
} ttf_font_t;

/* Parse the table directory and locate head/maxp/cmap/loca/glyf. Returns false
 * (and leaves f->valid == false) on any malformed or unsupported font. */
bool ttf_parse(ttf_font_t *f, const uint8_t *data, uint32_t len);

/* Map a Unicode codepoint to a glyph id via the cmap. Returns 0 (.notdef) for
 * an unmapped codepoint — which is exactly what a font does. */
uint32_t ttf_glyph_index(const ttf_font_t *f, uint32_t codepoint);

/* Render a glyph id at pixel height `size` into an w*h coverage bitmap (one
 * byte per pixel, 0..255, top-left origin, baseline placed so the glyph sits
 * inside the box). Returns true if a glyph outline was rendered; false for an
 * empty glyph (e.g. space), a composite glyph, or malformed data. */
bool ttf_render(const ttf_font_t *f, uint32_t glyph_id, uint32_t size,
                uint8_t *bitmap, uint32_t w, uint32_t h);

/* Convenience: codepoint straight to pixels (cmap + render). */
bool ttf_render_cp(const ttf_font_t *f, uint32_t codepoint, uint32_t size,
                   uint8_t *bitmap, uint32_t w, uint32_t h);

#endif /* ZXV_TRUETYPE_H */
