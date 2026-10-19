/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_brand.c — the system's own marks (zui_brand.h BR1..BR3). */
#include "zui_brand.h"

#include "brand/fox_zxvg.inc"

const uint8_t *zui_brand_fox(uint32_t *len)
{
    if (len) *len = zxv_fox_len;
    return zxv_fox;
}

void zui_brand_fox_mark(zui_vec_mark_t *m, zui_vec_work_t *work, uint32_t dim)
{
    uint8_t *b = (uint8_t *) m;
    for (uint32_t i = 0; i < (uint32_t) sizeof *m; i++) b[i] = 0;
    m->data = zxv_fox;
    m->len = zxv_fox_len;
    m->work = work;
    m->dim = dim;
}
