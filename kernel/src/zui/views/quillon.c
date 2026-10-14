/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quillon.c — Quillon's native screen (quillon.h). */
#include "quillon.h"
#include "zui_tokens.h"

/* The text area edits through the model, which keeps the text valid UTF-8
 * and drops a held answer whose range an edit may have moved. */
static bool edit(void *ctx, uint32_t a, uint32_t b, const char *s, uint32_t n)
{
    return quillon_replace((quillon_t *) ctx, a, b, s, n) == ZXM_OK;
}

void quillon_view_build(quillon_view_t *v, zui_ui_t *ui, quillon_t *m)
{
    v->m = m;
    v->asking_path = false;
    v->as_buf[0] = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Quillon");
    zv_shell_palette(s, QUILLON_AI, QUILLON_AI_COUNT);
    zui_id_t col = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, col, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, col, 0, ZUI_S2);
    zui_id_t bar = zui_box(ui, col, ZUI_ROW);
    zui_set_pad(ui, bar, 0, ZUI_S2);
    v->where = zui_label(ui, bar, "");
    zui_set_size(ui, v->where, ZUI_SIZE_FLEX, 1);
    zui_set_font(ui, v->where, ZUI_TEXT_MONO, ZUI_MUTED);
    v->save_as = zui_button(ui, bar, "Save as", ZUI_BTN_SECONDARY);
    v->save = zui_button(ui, bar, "Save", ZUI_BTN_PRIMARY);
    zui_set_shortcut(ui, v->save, "\xE2\x8C\x98S");
    v->as_field = zui_field(ui, col, v->as_buf, sizeof v->as_buf, false);
    zui_set_name(ui, v->as_field, "Save as path");
    zv_cat(zui_w(ui, v->as_field)->u.field.placeholder, ZUI_LABEL_MAX, "notes/new.txt", 0);
    v->text = zui_field(ui, col, m->text, QUILLON_TEXT_MAX + 1, true);
    zui_set_name(ui, v->text, "Document");
    zui_widget_t *t = zui_w(ui, v->text);
    if (t) {
        t->u.field.edit = edit;
        t->u.field.edit_ctx = m;
        t->font = ZUI_TEXT_BODY;
    }
    v->counts = zui_label(ui, col, "");
    zui_set_font(ui, v->counts, ZUI_TEXT_LABEL, ZUI_MUTED);
    quillon_view_sync(v);
}

void quillon_view_sync(quillon_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    quillon_t *q = v->m;
    bool dirty = quillon_dirty(q);
    char where[ZUI_LABEL_MAX];
    zv_cat(where, sizeof where, q->path[0] ? q->path : "Not saved yet",
           dirty ? "  \xE2\x80\xA2 edited" : 0);
    zui_set_text(ui, v->where, where);
    zui_set_disabled(ui, v->save, !q->path[0] || !dirty);
    zui_set_hidden(ui, v->as_field, !v->asking_path);
    zui_widget_t *t = zui_w(ui, v->text);
    if (t && t->u.field.len != q->len) zui_field_set(ui, v->text, q->len);
    char c[ZUI_LABEL_MAX];
    uint32_t n = zv_u64(c, sizeof c, q->counts.words);
    n = zv_cat(c + n, sizeof c - n, q->counts.words == 1 ? " word \xC2\xB7 " : " words \xC2\xB7 ",
               0) +
        n;
    n += zv_u64(c + n, sizeof c - n, q->counts.chars);
    n += zv_cat(c + n, sizeof c - n, " characters \xC2\xB7 ", 0);
    n += zv_u64(c + n, sizeof c - n, q->counts.lines);
    zv_cat(c + n, sizeof c - n, q->counts.lines == 1 ? " line" : " lines", 0);
    zui_set_text(ui, v->counts, c);
    zv_shell_status(&v->sh, dirty ? "Edited" : q->path[0] ? "Saved" : "");
    zv_shell_held(&v->sh, &q->held);
}

bool quillon_view_action(quillon_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    quillon_t *q = v->m;
    zv_shell_t *s = &v->sh;
    zui_ui_t *ui = s->ui;
    int rc = ZXM_OK;
    if (w == v->text && a == ZUI_ACT_CHANGE) {
        /* the model already has the edit; counts, dirty mark, held answer */
    } else if (w == v->save && a == ZUI_ACT_PRESS) {
        rc = quillon_save(q);
    } else if (w == v->save_as && a == ZUI_ACT_PRESS) {
        v->asking_path = true;
        quillon_view_sync(v);
        zui_focus(ui, v->as_field, true);
        return true;
    } else if (w == v->as_field && a == ZUI_ACT_SUBMIT) {
        if (!v->as_buf[0]) return true;
        rc = quillon_save_as(q, q->space, v->as_buf);
        if (rc == ZXM_OK) {
            v->asking_path = false;
            zui_focus(ui, v->text, false);
        }
    } else if (w == s->root && a == ZUI_ACT_PICK) {
        uint32_t a0, a1;
        zui_field_selection(ui, v->text, &a0, &a1);
        rc = quillon_ai(q, arg, a0, a1);
    } else if (w == s->held && a == ZUI_ACT_ACCEPT) {
        rc = quillon_ai_accept(q);
        if (rc == ZXM_OK) {
            zui_field_set(ui, v->text, q->len);
            zui_field_select(ui, v->text, q->sel_start, q->sel_end); /* show what came in */
        }
    } else if (w == s->held && a == ZUI_ACT_REJECT) {
        rc = quillon_ai_reject(q);
    } else {
        return false;
    }
    zv_shell_result(s, rc, q->link);
    quillon_view_sync(v);
    return true;
}
