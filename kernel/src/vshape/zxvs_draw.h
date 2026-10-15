/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvs_draw.h — draw a ZXVS descriptor (zxvs.h) with the native vector
 * renderer (kernel/src/zui/zui_vec.h), fitted into a box at any size: the
 * series curves as Bezier strokes, the structure tree (edges and nodes from
 * its integer layout), or the Hilbert layout (cells by class, the outline
 * stroked) under the entropy curve. Everything is drawn from the vectors in
 * the descriptor, so it scales cleanly; nothing is drawn that the
 * descriptor does not hold.
 *
 * Integer only, freestanding, no allocation: the caller's zui_vec_work_t.
 * Long curves and big trees are drawn in chunks that fit the work caps.
 * A malformed descriptor draws nothing (zxvs_check first). */
#ifndef ZXV_ZXVS_DRAW_H
#define ZXV_ZXVS_DRAW_H

#include <stdint.h>

#include "zui_vec.h"
#include "zxvs.h"

typedef struct {
    uint32_t bg;        /* 0: no background; else 0xAARRGGBB filled first   */
    uint32_t ink;       /* edges, outline, text-like marks (0: 0xFF5A6270)   */
    uint32_t stroke_q8; /* line width in 1/256 px (0: 384 = 1.5 px)          */
} zxvs_draw_opts_t;

/* 0, or -1 when the descriptor is malformed or the box is empty. Shapes the
 * work caps could not hold are skipped and flagged in *err_bits
 * (ZUI_VEC_E*) when err_bits is not NULL. o may be NULL. */
int zxvs_draw(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n, zui_rect_t box,
              const zxvs_draw_opts_t *o, uint32_t *err_bits);

#endif
