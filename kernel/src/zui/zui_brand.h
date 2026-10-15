/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_brand.h — the system's own marks, as vector (ZXVG) only.
 *
 *   BR1 Every system graphic is vector. The fox, the OS mark, is compiled
 *       in from brand/fox.svg through tools/zxvg/svg2zxvg.py (brand/
 *       fox_zxvg.inc; regenerate, never hand-edit). No PNG or JPG is drawn.
 *   BR2 zui_brand_fox gives the blob; zui_brand_fox_mark fills a
 *       zui_vec_mark_t so the boot and launch screens draw the fox at any
 *       size through zui_vec_mark (dim until lit, as their parts are).
 *   BR3 The partner logos stay in the project's brand folder until their
 *       trademark checks clear; they are drawn from bytes the shell loads. */
#ifndef ZXV_ZUI_BRAND_H
#define ZXV_ZUI_BRAND_H

#include <stdint.h>

#include "zui_vec.h"

const uint8_t *zui_brand_fox(uint32_t *len);
/* Fill m for zui_vec_mark with the fox; work is caller-owned scratch. */
void zui_brand_fox_mark(zui_vec_mark_t *m, zui_vec_work_t *work, uint32_t dim);

#endif /* ZXV_ZUI_BRAND_H */
