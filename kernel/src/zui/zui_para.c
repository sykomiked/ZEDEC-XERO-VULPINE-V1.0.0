/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_para.c — shape, reorder, then draw. See zui_para.h. */
#include "zui_para.h"
#include "zui_shape.h"

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

const char *zui_para_layout(zui_para_t *p, const char *utf8, uint32_t len, zui_bidi_dir_t dir)
{
    if (!p) return "";
    p->rtl = false;
    p->truncated = false;
    p->a[0] = p->b[0] = 0;
    if (!utf8) return p->a;
    if (!len) len = slen(utf8);

    /* 1. Arabic joining forms, still in logical order */
    uint32_t n1 = zui_shape_utf8(utf8, len, p->a, ZUI_PARA_BYTES);

    /* 2. Indic reordering, on code points */
    uint32_t i = 0, n = 0;
    while (i < n1 && n < ZUI_BIDI_MAX) p->cp[n++] = zui_utf8_decode(p->a, n1, &i);
    if (i < n1) p->truncated = true;
    uint32_t m = zui_indic_visual(p->cp, n, p->cq, 0, ZUI_BIDI_MAX);
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k++) {
        uint32_t w = zui_utf8_encode(p->cq[k], p->b + o, ZUI_PARA_BYTES - 1u - o);
        if (!w) break;
        o += w;
    }
    p->b[o] = 0;

    /* 3. the bidirectional algorithm: visual order, mirrored brackets */
    zui_bidi_paragraph(&p->bidi, p->b, o, dir);
    if (p->bidi.truncated) p->truncated = true;
    p->rtl = p->bidi.para_level == 1;
    zui_bidi_visual(&p->bidi, 0, p->bidi.n, p->a, ZUI_PARA_BYTES);
    return p->a;
}

int32_t zui_text_para(zui_surface_t *s, const zui_text_t *t, zui_para_t *p, int32_t x,
                      int32_t baseline, uint32_t rgb, const char *utf8, zui_bidi_dir_t dir)
{
    if (!s || !t || !p || !utf8) return x;
    return zui_text(s, t, x, baseline, rgb, zui_para_layout(p, utf8, 0, dir));
}

int32_t zui_text_para_width(const zui_text_t *t, zui_para_t *p, const char *utf8,
                            zui_bidi_dir_t dir)
{
    if (!t || !p || !utf8) return 0;
    return zui_text_width(t, zui_para_layout(p, utf8, 0, dir));
}
