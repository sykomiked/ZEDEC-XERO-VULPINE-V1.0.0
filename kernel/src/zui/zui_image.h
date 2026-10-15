/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_image.h — pictures from PNG files, fitted to where they are drawn.
 *
 * The app icons and the boot's five logos arrive as PNG files the owner
 * made (1024 px icons drawn on the ZXV green; logos up to about 3800 px with
 * their own transparency). They are read from disk when they are needed, not
 * built into the system, so a new or redrawn icon shows the next time its app
 * opens.
 *
 *   IM1 DECODE. Any non-interlaced PNG: greyscale, RGB, palette, grey with
 *       alpha, RGBA; 1, 2, 4, 8 or 16 bits; tRNS transparency. zlib streams of
 *       every kind (stored, fixed and dynamic Huffman) across any number of
 *       IDAT chunks. Interlaced files are refused (ZUI_IMG_EUNSUPPORTED).
 *   IM2 KEY. A picture with no transparency of its own can have a ground
 *       colour keyed out (the icons are drawn on #002911): a pixel within a
 *       small distance of it is clear, then fades in, as the mockups did.
 *   IM3 CROP AND FIT. The drawn part (every pixel not fully clear) is fitted
 *       inside the box asked for, aspect kept, never enlarged, each pixel the
 *       average of the source pixels it covers. The decode streams row by
 *       row in two passes (find the drawn part, then fit it), so a 3800 px
 *       logo needs a few rows of memory, never the whole picture.
 *   IM4 DRAW. A picture is drawn at any strength 0..256, in its own colours,
 *       or held to one colour (the palette's gold for a system app's icon):
 *       the picture's own light and shade, in gold.
 *
 * Integer only, no allocation: the caller gives the pixels and the scratch.
 */
#ifndef ZXV_ZUI_IMAGE_H
#define ZXV_ZUI_IMAGE_H

#include <stdint.h>

#include "zui_canvas.h"

typedef struct {
    uint32_t *px; /* premultiplied ARGB, w * h, row-major */
    int32_t w, h;
} zui_image_t;

enum {
    ZUI_IMG_OK = 0,
    ZUI_IMG_EFORMAT = -1,      /* not a PNG, or a bad header             */
    ZUI_IMG_EUNSUPPORTED = -2, /* interlaced, or an unknown filter      */
    ZUI_IMG_ESPACE = -3,       /* the scratch is too small              */
    ZUI_IMG_ECORRUPT = -4,     /* the compressed data does not decode   */
    ZUI_IMG_EEMPTY = -5        /* nothing drawn: every pixel clear      */
};

#define ZUI_IMG_NO_KEY     0xFFFFFFFFu
#define ZUI_IMG_KEY_GROUND 0x002911u /* the ZXV green the icons are drawn on */

/* Scratch bytes zui_image_load needs for this file fitted max_w wide, or 0
 * when it is not a PNG it can read. */
uint32_t zui_image_work_size(const uint8_t *png, uint32_t n, int32_t max_w);

/* IM1-IM3: decode `png`, key out `key` (or ZUI_IMG_NO_KEY), crop to the drawn
 * part and fit it inside max_w x max_h into px (room for max_w * max_h).
 * Returns ZUI_IMG_OK and fills img, or a ZUI_IMG_E* code. */
int zui_image_load(zui_image_t *img, uint32_t *px, int32_t max_w, int32_t max_h, const uint8_t *png,
                   uint32_t n, uint32_t key, uint8_t *work, uint32_t work_cap);

/* IM4: draw img centred in box at strength 0..256, shrunk to fit when the
 * box is smaller than it. tint_rgb ZUI_IMG_NO_KEY
 * keeps its own colours; any other RGB holds it to that colour. */
void zui_image_draw(zui_surface_t *s, const zui_image_t *img, zui_rect_t box, uint32_t strength,
                    uint32_t tint_rgb);

#endif /* ZXV_ZUI_IMAGE_H */
