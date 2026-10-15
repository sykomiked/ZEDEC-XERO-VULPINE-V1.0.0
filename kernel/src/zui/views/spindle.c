/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* spindle.c — Spindle's native screen (spindle.h). */
#include "spindle.h"
#include "zui_tokens.h"

static void method_row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const spindle_t *s = ctx;
    if (i >= 0 && (uint32_t) i < s->n_methods)
        zv_cat(out->text, sizeof out->text, s->methods[i], 0);
}

static void hist_row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const spindle_entry_t *e = spindle_entry((const spindle_t *) ctx, (uint32_t) i);
    if (!e) return;
    zv_cat(out->text, sizeof out->text, e->method, 0);
    if (e->rc == ZXM_OK) {
        zv_cat(out->detail, sizeof out->detail, "ok", 0);
        out->lead_rgb = ZUI_OK;
    } else {
        zv_i64(out->detail, sizeof out->detail, e->err_code);
        out->lead_rgb = ZUI_ALERT;
    }
}

void spindle_view_build(spindle_view_t *v, zui_ui_t *ui, spindle_t *m)
{
    v->m = m;
    v->shown = -1;
    v->method_buf[0] = v->params_buf[0] = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Spindle");
    zv_shell_palette(s, SPINDLE_AI, SPINDLE_AI_COUNT);
    v->methods = zui_list(ui, s->body, method_row, m);
    zui_set_name(ui, v->methods, "Methods");
    zui_set_text(ui, v->methods, "No methods listed");
    zui_set_size(ui, v->methods, ZUI_SIZE_FIXED, 240);
    zui_w(ui, v->methods)->u.list.row_pt = 24;
    zui_w(ui, v->methods)->font = ZUI_TEXT_MONO;
    zui_id_t col = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, col, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, col, 0, ZUI_S2);
    zui_id_t bar = zui_box(ui, col, ZUI_ROW);
    zui_set_pad(ui, bar, 0, ZUI_S2);
    v->method = zui_field(ui, bar, v->method_buf, sizeof v->method_buf, false);
    zui_set_name(ui, v->method, "Method");
    zui_set_size(ui, v->method, ZUI_SIZE_FIXED, 200);
    zui_w(ui, v->method)->font = ZUI_TEXT_MONO;
    zv_cat(zui_w(ui, v->method)->u.field.placeholder, ZUI_LABEL_MAX, "swarm.state", 0);
    v->params = zui_field(ui, bar, v->params_buf, sizeof v->params_buf, false);
    zui_set_name(ui, v->params, "Parameters");
    zui_set_size(ui, v->params, ZUI_SIZE_FLEX, 1);
    zui_w(ui, v->params)->font = ZUI_TEXT_MONO;
    zv_cat(zui_w(ui, v->params)->u.field.placeholder, ZUI_LABEL_MAX, "{\"key\": \"value\"}", 0);
    v->call = zui_button(ui, bar, "Call", ZUI_BTN_PRIMARY);
    zui_set_shortcut(ui, v->call, "\xE2\x86\xA9");
    zui_id_t row2 = zui_box(ui, col, ZUI_ROW);
    zui_set_size(ui, row2, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, row2, 0, ZUI_S2);
    v->history = zui_list(ui, row2, hist_row, m);
    zui_set_name(ui, v->history, "Replies");
    zui_set_text(ui, v->history, "Nothing called yet");
    zui_set_size(ui, v->history, ZUI_SIZE_FIXED, 220);
    zui_w(ui, v->history)->font = ZUI_TEXT_MONO;
    v->reply = zui_field(ui, row2, 0, 0, true);
    zui_set_name(ui, v->reply, "Reply");
    zui_w(ui, v->reply)->u.field.readonly = true;
    zui_w(ui, v->reply)->font = ZUI_TEXT_MONO;
    v->none = zui_empty(ui, row2, ZUI_ICON_CONSOLE, "Nothing called yet", 0);
    zui_set_size(ui, v->none, ZUI_SIZE_FLEX, 1);
    spindle_view_sync(v);
}

void spindle_view_sync(spindle_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    spindle_t *m = v->m;
    zui_list_set_rows(ui, v->methods, (int32_t) m->n_methods);
    zui_list_set_rows(ui, v->history, (int32_t) spindle_history_len(m));
    zui_list_select(ui, v->history, v->shown);
    bool none = spindle_history_len(m) == 0; /* W12 */
    zui_set_hidden(ui, v->history, none);
    zui_set_hidden(ui, v->reply, none);
    zui_set_hidden(ui, v->none, !none);
    const spindle_entry_t *e = v->shown >= 0 ? spindle_entry(m, (uint32_t) v->shown) : 0;
    zui_widget_t *r = zui_w(ui, v->reply);
    if (r) {
        r->u.field.buf = e ? (char *) (uintptr_t) e->reply : 0;
        r->u.field.cap = e ? e->reply_len + 1u : 0;
        zui_field_set(ui, v->reply, e ? e->reply_len : 0);
        zui_field_select(ui, v->reply, 0, 0);
    }
    zui_set_disabled(ui, v->call, v->method_buf[0] == 0);
    zv_shell_status(&v->sh, m->protocol[0] ? m->protocol : "");
    zv_shell_held(&v->sh, &m->held);
}

bool spindle_view_action(spindle_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    spindle_t *m = v->m;
    zv_shell_t *s = &v->sh;
    zui_ui_t *ui = s->ui;
    int rc = ZXM_OK;
    if (w == v->methods && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN) && arg >= 0) {
        uint32_t n = zv_cat(v->method_buf, sizeof v->method_buf, m->methods[arg], 0);
        zui_field_set(ui, v->method, n);
        if (a == ZUI_ACT_OPEN) zui_focus(ui, v->params, true);
    } else if (w == v->method && a == ZUI_ACT_CHANGE) {
        /* Call enables once there is a method */
    } else if ((w == v->call && a == ZUI_ACT_PRESS) ||
               ((w == v->params || w == v->method) && a == ZUI_ACT_SUBMIT)) {
        if (!v->method_buf[0]) return true;
        rc = spindle_call(m, v->method_buf, v->params_buf);
        if (rc == ZXM_OK || rc == ZXM_E_REMOTE) v->shown = 0;
        if (rc == ZXM_E_REMOTE) rc = ZXM_OK; /* the refusal is in the history, as is */
    } else if (w == v->history && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN)) {
        v->shown = arg;
    } else if (w == s->root && a == ZUI_ACT_PICK) {
        rc = v->shown >= 0 ? spindle_ai(m, arg, (uint32_t) v->shown) : ZXM_E_STATE;
    } else if (w == s->held && a == ZUI_ACT_ACCEPT) {
        rc = spindle_ai_accept(m);
    } else if (w == s->held && a == ZUI_ACT_REJECT) {
        rc = spindle_ai_reject(m);
    } else {
        return false;
    }
    zv_shell_result(s, rc, m->link);
    spindle_view_sync(v);
    return true;
}
