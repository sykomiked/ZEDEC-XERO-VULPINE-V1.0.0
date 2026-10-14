/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* autoview.c — the screen made from an app's contracts (autoview.h). */
#include "autoview.h"
#include "zui_tokens.h"

int32_t av_parse_ints(const char *s, int64_t *out, uint32_t cap)
{
    uint32_t n = 0;
    uint32_t i = 0;
    for (;;) {
        while (s[i] == ' ' || s[i] == '\t' || s[i] == ',') i++;
        if (!s[i]) return (int32_t) n;
        bool neg = s[i] == '-';
        if (neg) i++;
        if (s[i] < '0' || s[i] > '9') return -1;
        uint64_t v = 0;
        for (; s[i] >= '0' && s[i] <= '9'; i++) {
            v = v * 10u + (uint64_t) (s[i] - '0');
            if (v > (uint64_t) INT64_MAX) return -1;
        }
        if (s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != ',') return -1;
        if (n >= cap) return -1;
        out[n++] = neg ? -(int64_t) v : (int64_t) v;
    }
}

static void entry_row(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list;
    const av_view_t *v = ctx;
    if (row < 0 || (uint32_t) row >= v->n) return;
    zv_cat(out->text, sizeof out->text, v->entry[row], 0);
    const zxapp_provide_t *P = zxapp_contract(v->app, v->entry[row]);
    zv_cat(out->detail, sizeof out->detail, P && P->kind == ZXAPP_KIND_TRUTH ? "truth" : "number",
           0);
}

static const char *truth_word(int64_t t)
{
    return t == ZXAPP_TRUE    ? "TRUE"
           : t == ZXAPP_FALSE ? "FALSE"
           : t == ZXAPP_BOTH  ? "BOTH"
                              : "NEITHER";
}

static void say_error(av_view_t *v, const char *a, const char *b)
{
    zv_shell_t *s = &v->sh;
    zv_cat(s->error_text, sizeof s->error_text, a, b);
    zui_set_text(s->ui, s->error, s->error_text);
    zui_set_hidden(s->ui, s->error, !s->error_text[0]);
}

void av_view_build(av_view_t *v, zui_ui_t *ui, zxapp_t *app, zxapp_host_t *host)
{
    v->app = app;
    v->host = host;
    v->n = 0;
    v->sel = -1;
    v->lp = -1;
    v->answer_text[0] = 0;
    for (uint32_t i = 0; i < app->n && v->n < AV_ENTRIES; i++) { /* AV1 */
        const char *e = app->logic[i].entry;
        bool dup = false;
        for (uint32_t j = 0; j < v->n && !dup; j++) {
            const char *x = v->entry[j];
            uint32_t k = 0;
            while (x[k] && x[k] == e[k]) k++;
            dup = x[k] == e[k];
        }
        if (!dup && zxapp_contract(app, e)) v->entry[v->n++] = e;
    }
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, app->name[0] ? app->name : app->id);
    v->entries = zui_list(ui, s->body, entry_row, v);
    zui_set_name(ui, v->entries, "Entries");
    zui_set_text(ui, v->entries, "Nothing to run: no entry has a contract");
    zui_set_size(ui, v->entries, ZUI_SIZE_FIXED, 260);
    zui_w(ui, v->entries)->font = ZUI_TEXT_MONO;
    zui_id_t col = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, col, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, col, 0, ZUI_S2);
    v->name = zui_label(ui, col, "");
    zui_set_font(ui, v->name, ZUI_TEXT_TITLE, ZUI_TEXT);
    v->inputs = zui_box(ui, col, ZUI_COL);
    zui_set_pad(ui, v->inputs, 0, ZUI_S2);
    for (uint32_t i = 0; i < ZXAPP_INPUTS; i++) {
        v->buf[i][0] = 0;
        v->label[i] = zui_label(ui, v->inputs, "");
        zui_set_font(ui, v->label[i], ZUI_TEXT_LABEL, ZUI_MUTED);
        v->field[i] = zui_field(ui, v->inputs, v->buf[i], AV_FIELD_MAX, false);
        zui_w(ui, v->field[i])->font = ZUI_TEXT_MONO;
        zv_cat(zui_w(ui, v->field[i])->u.field.placeholder, ZUI_LABEL_MAX, "1 2 3", 0);
    }
    v->run = zui_button(ui, col, "Run", ZUI_BTN_PRIMARY);
    zui_set_shortcut(ui, v->run, "\xE2\x86\xA9");
    v->answer = zui_label(ui, col, "");
    zui_set_font(ui, v->answer, ZUI_TEXT_MONO, ZUI_TEXT);
    av_view_sync(v);
}

void av_view_sync(av_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    zui_list_set_rows(ui, v->entries, (int32_t) v->n);
    zui_list_select(ui, v->entries, v->sel);
    const zxapp_provide_t *P = v->sel >= 0 ? zxapp_contract(v->app, v->entry[v->sel]) : 0;
    zui_set_text(ui, v->name, P ? v->entry[v->sel] : "Choose an entry to run");
    for (uint32_t i = 0; i < ZXAPP_INPUTS; i++) {
        bool on = P && i < P->ninputs;
        zui_set_hidden(ui, v->label[i], !on);
        zui_set_hidden(ui, v->field[i], !on);
        if (on) {
            zui_set_text(ui, v->label[i], P->input[i]);
            zui_set_name(ui, v->field[i], P->input[i]);
        }
    }
    zui_set_hidden(ui, v->run, !P);
    zui_set_text(ui, v->answer, v->answer_text);
    const char *st = v->lp == ZXAPP_TRUE      ? "Witnessed: the languages agree"
                     : v->lp == ZXAPP_NEITHER ? "One language answered"
                     : v->lp == ZXAPP_BOTH    ? "Held: the languages disagree"
                                              : "";
    zv_shell_status(&v->sh, st);
}

/* AV3: the answer in words: numbers, "num/den", or truth words. */
static void show(av_view_t *v, const zxapp_num_t *x, uint32_t n)
{
    char *t = v->answer_text;
    uint32_t cap = sizeof v->answer_text, k = 0;
    t[0] = 0;
    for (uint32_t i = 0; i < n && k + 24u < cap; i++) {
        if (i) k += zv_cat(t + k, cap - k, " ", 0);
        if (x[i].den == 0) {
            k += zv_cat(t + k, cap - k, truth_word(x[i].num), 0);
        } else {
            k += zv_i64(t + k, cap - k, x[i].num);
            if (x[i].den != 1) {
                k += zv_cat(t + k, cap - k, "/", 0);
                k += zv_i64(t + k, cap - k, x[i].den);
            }
        }
    }
    if (k + 24u >= cap && n) zv_cat(t + k, cap - k, " \xE2\x80\xA6", 0);
}

static void run(av_view_t *v)
{
    const char *e = v->entry[v->sel];
    const zxapp_provide_t *P = zxapp_contract(v->app, e);
    static int64_t vals[ZXAPP_INPUTS][AV_VALUES];
    zxapp_arg_t args[ZXAPP_INPUTS];
    for (uint32_t i = 0; i < P->ninputs; i++) {
        int32_t n = av_parse_ints(v->buf[i], vals[i], AV_VALUES);
        if (n <= 0) {
            v->answer_text[0] = 0;
            v->lp = -1;
            say_error(v, P->input[i], ": whole numbers only, like 3 or 1 2 3");
            return;
        }
        args[i].name = P->input[i];
        args[i].v = vals[i];
        args[i].n = (uint32_t) n;
    }
    zxapp_num_t out[AV_VALUES];
    uint32_t n = 0;
    int32_t lp = ZXAPP_NEITHER;
    int rc = zxapp_call_witnessed(v->app, v->host, e, args, P->ninputs, out, AV_VALUES, &n, &lp);
    v->answer_text[0] = 0;
    v->lp = -1;
    if (rc == ZXAPP_OK) {
        v->lp = lp;
        show(v, out, n);
        say_error(v, "", 0);
    } else if (rc == ZXAPP_E_CONFLICT) {
        v->lp = ZXAPP_BOTH;
        say_error(v, "Held: the languages give different answers. ", v->app->err);
    } else if (rc == ZXAPP_E_NORUNTIME) {
        say_error(v, "This build cannot run the language this entry is written in", 0);
    } else if (rc == ZXAPP_E_BUFFER) {
        say_error(v, "The answer is longer than this screen shows", 0);
    } else {
        say_error(v, v->app->err[0] ? v->app->err : "It did not run", 0);
    }
}

bool av_view_action(av_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    if (w == v->entries && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN)) {
        v->sel = arg >= 0 && (uint32_t) arg < v->n ? arg : -1;
        v->answer_text[0] = 0;
        v->lp = -1;
        say_error(v, "", 0);
        av_view_sync(v);
        if (a == ZUI_ACT_OPEN && v->sel >= 0) zui_focus(v->sh.ui, v->field[0], true);
        return true;
    }
    bool submit = false;
    for (uint32_t i = 0; i < ZXAPP_INPUTS; i++) {
        if (w == v->field[i] && a == ZUI_ACT_CHANGE) return true;
        submit |= w == v->field[i] && a == ZUI_ACT_SUBMIT;
    }
    if ((w == v->run && a == ZUI_ACT_PRESS) || submit) {
        if (v->sel >= 0) run(v);
        av_view_sync(v);
        return true;
    }
    return false;
}
