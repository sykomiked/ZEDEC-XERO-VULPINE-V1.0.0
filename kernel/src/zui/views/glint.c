/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glint.c — Glint's native screen (glint.h). */
#include "glint.h"
#include "zui_tokens.h"

static void row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const glint_t *g = ctx;
    if (i < 0 || (uint32_t) i >= g->n) return;
    zv_cat(out->text, sizeof out->text, g->item[i].name, 0);
    zv_bytes(out->detail, sizeof out->detail, g->item[i].size);
    out->lead_rgb = g->item[i].kind == BURROW_K_IMAGE ? ZUI_INFO : ZUI_OK;
}

/* The stage: the picture when a decoder drew it, else a carved frame with
 * the kind's mark and the file's type, so nothing is ever blank. */
static void stage(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) w;
    glint_view_t *v = ctx;
    const glint_t *g = v->m;
    int32_t rad = zui_px(&ui->screen, ZUI_R_PANEL);
    zui_fill_rrect(s, r, rad, zui_ui_color(ui, ZUI_C_PANEL), 255);
    zui_engrave(s, r, rad, zui_ui_color(ui, ZUI_C_ENGRAVE_LO), zui_ui_color(ui, ZUI_C_ENGRAVE_HI),
                40);
    if (g->cur < 0) return;
    if (v->loaded && v->decode) {
        int32_t m = zui_px(&ui->screen, ZUI_S3);
        zui_rect_t in = {r.x + m, r.y + m, r.w - 2 * m, r.h - 2 * m};
        if (v->decode(v->decode_ctx, v->buf, v->len, s, in)) return;
    }
    /* the kind's mark: a gold ring for pictures, three bars for sound and video */
    int32_t cx = r.x + r.w / 2, cy = r.y + r.h / 2, d = r.w < r.h ? r.w / 5 : r.h / 5;
    uint8_t k = g->item[g->cur].kind;
    if (k == BURROW_K_IMAGE) {
        zui_rect_t c = {cx - d / 2, cy - d / 2, d, d};
        zui_stroke_rrect(s, c, d / 2, zui_px(&ui->screen, 2), zui_ui_color(ui, ZUI_C_GOLD), 255);
    } else {
        for (int i = 0; i < 3; i++) {
            int32_t h = d * (2 + (i == 1)) / 4;
            zui_rect_t b = {cx - d / 2 + i * d / 3, cy - h / 2, d / 5, h};
            zui_fill_rrect(s, b, d / 10, zui_ui_color(ui, ZUI_C_GOLD), 255);
        }
    }
    const zui_text_t *t = &ui->theme.label;
    const char *mime = glint_mime(g->item[g->cur].name);
    int32_t tw = zui_text_width(t, mime);
    zui_text(s, t, cx - tw / 2, cy + d, zui_ui_color(ui, ZUI_C_MUTED), mime);
}

void glint_view_build(glint_view_t *v, zui_ui_t *ui, glint_t *m, uint8_t *buf, uint32_t cap)
{
    v->m = m;
    v->buf = buf;
    v->cap = cap;
    v->len = 0;
    v->loaded = false;
    v->decode = 0;
    v->decode_ctx = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Glint");
    zv_shell_palette(s, GLINT_AI, GLINT_AI_COUNT);
    v->items = zui_list(ui, s->body, row, m);
    zui_set_name(ui, v->items, "Media");
    zui_set_text(ui, v->items, "No pictures, sounds or videos here");
    zui_set_size(ui, v->items, ZUI_SIZE_FIXED, 280);
    zui_id_t col = v->pane = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, col, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, col, 0, ZUI_S2);
    v->stage = zui_canvas(ui, col, stage, v);
    zui_set_name(ui, v->stage, "Picture");
    v->caption = zui_label(ui, col, "");
    zui_set_font(ui, v->caption, ZUI_TEXT_MONO, ZUI_TEXT_2);
    zui_id_t bar = zui_box(ui, col, ZUI_ROW);
    zui_set_pad(ui, bar, 0, ZUI_S2);
    v->prev = zui_button(ui, bar, "Previous", ZUI_BTN_SECONDARY);
    v->load = zui_button(ui, bar, "Show", ZUI_BTN_PRIMARY);
    v->next = zui_button(ui, bar, "Next", ZUI_BTN_SECONDARY);
    v->none = zui_empty(ui, s->body, ZUI_ICON_PICTURE, "No pictures, sounds or videos here", 0);
    zui_set_size(ui, v->none, ZUI_SIZE_FLEX, 1);
    glint_view_sync(v);
}

void glint_view_sync(glint_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    glint_t *g = v->m;
    zui_list_set_rows(ui, v->items, (int32_t) g->n);
    zui_list_select(ui, v->items, g->cur);
    zui_set_hidden(ui, v->items, g->n == 0); /* W12 */
    zui_set_hidden(ui, v->pane, g->n == 0);
    zui_set_hidden(ui, v->none, g->n != 0);
    bool have = g->cur >= 0 && (uint32_t) g->cur < g->n;
    char cap[ZUI_LABEL_MAX] = "";
    if (have) {
        char sz[24];
        zv_bytes(sz, sizeof sz, g->item[g->cur].size);
        zv_cat(cap, sizeof cap, g->item[g->cur].name, "  \xC2\xB7  ");
        zv_cat(cap, sizeof cap, cap, sz);
    }
    zui_set_text(ui, v->caption, cap);
    zui_set_disabled(ui, v->prev, !have || g->cur == 0);
    zui_set_disabled(ui, v->next, !have || (uint32_t) g->cur + 1u >= g->n);
    zui_set_disabled(ui, v->load, !have || v->loaded);
    char st[32];
    uint32_t n = zv_u64(st, sizeof st, g->n);
    zv_cat(st + n, sizeof st - n, g->n == 1 ? " item" : " items", 0);
    zv_shell_status(&v->sh, st);
    zv_shell_held(&v->sh, &g->held);
    zui_invalidate(ui, v->stage);
}

static int show(glint_view_t *v, int32_t i)
{
    v->loaded = false;
    v->len = 0;
    return glint_view(v->m, i);
}

bool glint_view_action(glint_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    glint_t *g = v->m;
    zv_shell_t *s = &v->sh;
    int rc = ZXM_OK;
    if (w == v->items && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN)) {
        rc = show(v, arg);
        if (rc == ZXM_OK && a == ZUI_ACT_OPEN && v->buf) {
            rc = glint_load(g, v->buf, v->cap, &v->len);
            v->loaded = rc == ZXM_OK;
        }
    } else if (w == v->prev && a == ZUI_ACT_PRESS) {
        v->loaded = false;
        rc = glint_prev(g);
    } else if (w == v->next && a == ZUI_ACT_PRESS) {
        v->loaded = false;
        rc = glint_next(g);
    } else if (w == v->load && a == ZUI_ACT_PRESS) {
        if (!v->buf) return true;
        zui_set_hidden(s->ui, s->bar, false);
        rc = glint_load(g, v->buf, v->cap, &v->len);
        zui_set_hidden(s->ui, s->bar, true);
        v->loaded = rc == ZXM_OK;
    } else if (w == s->root && a == ZUI_ACT_PICK) {
        rc = glint_ai(g, arg);
    } else if (w == s->held && a == ZUI_ACT_ACCEPT) {
        rc = glint_ai_accept(g);
    } else if (w == s->held && a == ZUI_ACT_REJECT) {
        rc = glint_ai_reject(g);
    } else {
        return false;
    }
    zv_shell_result(s, rc, g->folder ? g->folder->link : 0);
    glint_view_sync(v);
    return true;
}
