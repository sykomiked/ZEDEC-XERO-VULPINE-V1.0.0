/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_caret.c — a text field's caret in visual order (zui_caret.h). */
#include "zui_caret.h"
#include "zui_bidi.h"

static zui_bidi_t B; /* large: one, reused (CA3) */

bool zui_caret_bidi(const zui_text_t *t, zui_measure_fn measure, const char *s, uint32_t len,
                    uint32_t a, uint32_t b, zui_caret_line_t *out)
{
    if (!s || !out || !measure || a > b || b > len) return false;
    /* CA1: the paragraph around the line */
    uint32_t ps = a, pe = b;
    while (ps > 0 && s[ps - 1] != '\n') ps--;
    while (pe < len && s[pe] != '\n') pe++;
    zui_bidi_paragraph(&B, s + ps, pe - ps, ZUI_BIDI_AUTO);
    if (B.truncated) return false;
    uint32_t ia = 0, ib;
    while (ia < B.n && ps + B.off[ia] < a) ia++;
    ib = ia;
    while (ib < B.n && ps + B.off[ib] < b) ib++;
    uint32_t n = ib - ia;
    out->n = 0;
    out->rtl = B.para_level & 1u;
    out->width = 0;
    if (n > ZUI_CARET_MAX) return false;
    if (!n) return true;
    uint8_t lv[ZUI_CARET_MAX];
    uint16_t order[ZUI_CARET_MAX];
    uint32_t m = zui_bidi_line(&B, ia, ib, lv, order, true);
    int32_t x = 0;
    for (uint32_t v = 0; v < m && v < n; v++) {
        uint32_t i = order[v], k = i - ia;
        if (i < ia || k >= n) continue;
        uint32_t ca = ps + B.off[i], cb = ps + B.off[i + 1u];
        int32_t w = B.removed[i] ? 0 : measure(t, s, ca, cb);
        bool rtl = (lv[k] & 1u) != 0;
        out->c[k].a = ca;
        out->c[k].b = cb;
        out->c[k].x0 = x;
        out->c[k].x1 = x + w;
        out->c[k].rtl = rtl;
        out->c[k].cp = rtl ? zui_bidi_mirror(B.cp[i]) : B.cp[i];
        x += w;
    }
    out->n = n;
    out->width = x;
    return true;
}

void zui_ui_use_bidi(zui_ui_t *ui, bool on)
{
    if (!ui) return;
    ui->caret = on ? zui_caret_bidi : 0;
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}
