/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_para.h — a paragraph of any script, ready to draw.
 *
 * zui_text draws code points left to right as stored. Real text needs three
 * steps first, all of them already in zui:
 *
 *   1. Arabic joining: each letter takes its isolated / initial / medial /
 *      final form (zui_shape S5), so words join as they should
 *   2. Indic reordering: pre-base vowel signs move before their consonant
 *      (zui_shape S8), so Devanagari and its kin read right
 *   3. the Unicode Bidirectional Algorithm (zui_bidi, UAX #9): right-to-left
 *      runs (Arabic, Hebrew) are reversed for display, mirrored brackets
 *      flip, numbers keep their order
 *
 * zui_para_layout runs the three and gives the visual string; zui_text_para
 * draws it with zui_text (so the glyph fallback chain finds the Arabic,
 * Hebrew or Devanagari face). A right-to-left paragraph reports rtl so the
 * caller can align it to the right edge.
 *
 * The caller owns the workspace (about 30 KB); keep it static or pooled.
 * Integer only, no allocation.
 */
#ifndef ZXV_ZUI_PARA_H
#define ZXV_ZUI_PARA_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_canvas.h"
#include "zui_bidi.h"

#define ZUI_PARA_BYTES (4u * ZUI_BIDI_MAX + 1u)

typedef struct {
    zui_bidi_t bidi;
    char a[ZUI_PARA_BYTES], b[ZUI_PARA_BYTES];
    uint32_t cp[ZUI_BIDI_MAX], cq[ZUI_BIDI_MAX];
    bool rtl;       /* the paragraph runs right to left    */
    bool truncated; /* longer than ZUI_BIDI_MAX code points */
} zui_para_t;

/* The visual-order UTF-8 of utf8 (len bytes; 0 = up to the NUL), or "" on
 * bad arguments. The result lives in p until the next call. */
const char *zui_para_layout(zui_para_t *p, const char *utf8, uint32_t len, zui_bidi_dir_t dir);

/* Lay out and draw / measure. zui_text_para returns the pen x after it. */
int32_t zui_text_para(zui_surface_t *s, const zui_text_t *t, zui_para_t *p, int32_t x,
                      int32_t baseline, uint32_t rgb, const char *utf8, zui_bidi_dir_t dir);
int32_t zui_text_para_width(const zui_text_t *t, zui_para_t *p, const char *utf8,
                            zui_bidi_dir_t dir);

#endif /* ZXV_ZUI_PARA_H */
