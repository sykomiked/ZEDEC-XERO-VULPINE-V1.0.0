/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* playground.c — the Playground's native screen (playground.h). */
#include "playground.h"
#include "zui_tokens.h"

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

void playground_view_build(playground_view_t *v, zui_ui_t *ui, pg_eval_fn eval, void *ctx,
                           const char *lang)
{
    v->eval = eval;
    v->eval_ctx = ctx;
    v->src[0] = v->out_buf[0] = 0;
    v->out_len = 0;
    v->last_rc = 0;
    v->runs = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Playground");
    zui_id_t left = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, left, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, left, 0, ZUI_S2);
    v->lang = zui_label(ui, left, lang && lang[0] ? lang : "Scratch pad");
    zui_set_font(ui, v->lang, ZUI_TEXT_LABEL, ZUI_MUTED);
    v->pad = zui_field(ui, left, v->src, sizeof v->src, true);
    zui_set_name(ui, v->pad, "Scratch pad");
    zui_set_size(ui, v->pad, ZUI_SIZE_FLEX, 1);
    zui_w(ui, v->pad)->font = ZUI_TEXT_MONO;
    zv_cat(zui_w(ui, v->pad)->u.field.placeholder, ZUI_LABEL_MAX, "Write a snippet here", 0);
    zui_id_t bar = zui_box(ui, left, ZUI_ROW);
    zui_set_pad(ui, bar, 0, ZUI_S2);
    zui_set_wrap(ui, bar, true);
    v->run = zui_button(ui, bar, "Run", ZUI_BTN_PRIMARY);
    zui_set_tooltip(ui, v->run, "Run the snippet (Cmd-Enter)");
    v->clear = zui_button(ui, bar, "Clear", ZUI_BTN_GHOST);
    v->use = zui_button(ui, bar, "Use output", ZUI_BTN_SECONDARY);
    zui_set_tooltip(ui, v->use, "Put the output into the pad");
    zui_id_t right = zui_panel(ui, s->body, ZUI_COL);
    zui_set_size(ui, right, ZUI_SIZE_FLEX, 1);
    v->out = zui_field(ui, right, v->out_buf, sizeof v->out_buf, true);
    zui_set_name(ui, v->out, "Output");
    zui_set_size(ui, v->out, ZUI_SIZE_FLEX, 1);
    zui_w(ui, v->out)->u.field.readonly = true;
    zui_w(ui, v->out)->font = ZUI_TEXT_MONO;
    v->none = zui_empty(ui, right, ZUI_ICON_CONSOLE, "Run a snippet to see what it prints", 0);
    zui_set_size(ui, v->none, ZUI_SIZE_FLEX, 1);
    zui_focus(ui, v->pad, false);
    playground_view_sync(v);
}

void playground_view_sync(playground_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    bool ran = v->runs > 0;
    zui_set_hidden(ui, v->out, !ran);
    zui_set_hidden(ui, v->none, ran);
    zui_set_disabled(ui, v->run, !v->eval || !v->src[0]);
    zui_set_disabled(ui, v->clear, !v->src[0]);
    zui_set_disabled(ui, v->use, !ran || !v->out_len || v->last_rc != 0);
    char st[ZUI_LABEL_MAX];
    if (!v->eval) {
        zv_cat(st, sizeof st, "No evaluator", 0);
    } else if (!ran) {
        zv_cat(st, sizeof st, "Ready", 0);
    } else {
        uint32_t n = zv_cat(st, sizeof st, v->last_rc ? "Failed" : "Done", " \xC2\xB7 run ");
        zv_u64(st + n, sizeof st - n, v->runs);
    }
    zv_shell_status(&v->sh, st);
}

int playground_run(playground_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    if (!v->eval || !v->src[0]) return -1;
    v->out_len = 0;
    v->out_buf[0] = 0;
    uint32_t len = 0;
    int rc = v->eval(v->eval_ctx, v->src, slen(v->src), v->out_buf, sizeof v->out_buf, &len);
    v->out_len = len < sizeof v->out_buf ? len : (uint32_t) sizeof v->out_buf - 1u;
    v->out_buf[v->out_len] = 0;
    v->last_rc = rc;
    v->runs++;
    zui_field_set(ui, v->out, v->out_len);
    zui_field_select(ui, v->out, 0, 0);
    zv_cat(v->sh.error_text, sizeof v->sh.error_text, rc ? "The snippet stopped with an error" : "",
           0);
    zui_set_text(ui, v->sh.error, v->sh.error_text);
    zui_set_hidden(ui, v->sh.error, rc == 0);
    playground_view_sync(v);
    return rc;
}

bool playground_view_action(playground_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    (void) arg;
    zui_ui_t *ui = v->sh.ui;
    if ((w == v->run && a == ZUI_ACT_PRESS) || (w == v->pad && a == ZUI_ACT_SUBMIT)) {
        playground_run(v);
        return true;
    }
    if (w == v->pad && a == ZUI_ACT_CHANGE) {
        playground_view_sync(v);
        return true;
    }
    if (w == v->clear && a == ZUI_ACT_PRESS) {
        v->src[0] = 0;
        zui_field_set(ui, v->pad, 0);
        zui_focus(ui, v->pad, true);
        playground_view_sync(v);
        return true;
    }
    if (w == v->use && a == ZUI_ACT_PRESS) { /* P3: only on the person's press */
        uint32_t n = zv_cat(v->src, sizeof v->src, v->out_buf, 0);
        zui_field_set(ui, v->pad, n);
        playground_view_sync(v);
        return true;
    }
    return false;
}
