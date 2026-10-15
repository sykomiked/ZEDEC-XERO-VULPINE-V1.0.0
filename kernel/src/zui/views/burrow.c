/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burrow.c — Burrow's native screen (burrow.h). */
#include "burrow.h"
#include "zui_tokens.h"

static const char *const KIND[] = {"Folder", "Text", "Picture", "Sound", "Video", "File"};

static uint32_t kind_rgb(uint8_t k)
{
    switch (k) {
    case BURROW_K_DIR:
        return ZUI_GOLD_HI;
    case BURROW_K_TEXT:
        return ZUI_TEXT_2;
    case BURROW_K_IMAGE:
        return ZUI_INFO;
    case BURROW_K_AUDIO:
    case BURROW_K_VIDEO:
        return ZUI_OK;
    default:
        return ZUI_FAINT;
    }
}

static void row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const burrow_t *b = ctx;
    if (i < 0 || (uint32_t) i >= b->n) return;
    const burrow_entry_t *e = &b->e[i];
    zv_cat(out->text, sizeof out->text, e->name, e->dir ? "/" : 0);
    if (e->dir)
        zv_cat(out->detail, sizeof out->detail, "folder", 0);
    else
        zv_bytes(out->detail, sizeof out->detail, e->size);
    out->lead_rgb = kind_rgb(e->kind);
}

void burrow_view_build(burrow_view_t *v, zui_ui_t *ui, burrow_t *m)
{
    v->m = m;
    v->naming = false;
    v->show_detail = false;
    v->name_buf[0] = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Burrow");
    zv_shell_palette(s, BURROW_AI, BURROW_AI_COUNT);

    v->list_pane = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, v->list_pane, ZUI_SIZE_FLEX, 3);
    zui_set_pad(ui, v->list_pane, 0, ZUI_S2);
    zui_id_t bar = zui_box(ui, v->list_pane, ZUI_ROW);
    zui_set_pad(ui, bar, 0, ZUI_S2);
    v->up = zui_button(ui, bar, "Up", ZUI_BTN_GHOST);
    v->path = zui_label(ui, bar, "");
    zui_set_size(ui, v->path, ZUI_SIZE_FLEX, 1);
    zui_set_font(ui, v->path, ZUI_TEXT_MONO, ZUI_MUTED);
    v->new_dir = zui_button(ui, bar, "New folder", ZUI_BTN_SECONDARY);
    v->new_text = zui_button(ui, bar, "New text", ZUI_BTN_SECONDARY);
    v->trash_view = zui_button(ui, bar, "Trash", ZUI_BTN_GHOST);
    v->empty = zui_button(ui, bar, "Empty Trash", ZUI_BTN_DANGER);
    v->list = zui_list(ui, v->list_pane, row, m);
    zui_set_name(ui, v->list, "Files");
    zui_w(ui, v->list)->label[0] = 0;
    zui_set_text(ui, v->list, "This folder is empty");
    v->none = zui_empty(ui, v->list_pane, ZUI_ICON_FOLDER, "This folder is empty", "New folder");
    zui_set_size(ui, v->none, ZUI_SIZE_FLEX, 1);

    v->detail = zui_panel(ui, s->body, ZUI_COL);
    zui_set_size(ui, v->detail, ZUI_SIZE_FIXED, 320);
    v->back = zui_button(ui, v->detail, "Back", ZUI_BTN_GHOST);
    v->d_name = zui_label(ui, v->detail, "");
    zui_set_font(ui, v->d_name, ZUI_TEXT_TITLE, ZUI_TEXT);
    v->d_kind = zui_label(ui, v->detail, "");
    zui_set_font(ui, v->d_kind, ZUI_TEXT_LABEL, ZUI_MUTED);
    v->d_size = zui_label(ui, v->detail, "");
    zui_set_font(ui, v->d_size, ZUI_TEXT_MONO, ZUI_TEXT_2);
    v->name_field = zui_field(ui, v->detail, v->name_buf, sizeof v->name_buf, false);
    zui_set_name(ui, v->name_field, "Name");
    zui_id_t acts = zui_box(ui, v->detail, ZUI_COL);
    zui_set_pad(ui, acts, 0, ZUI_S2);
    v->open = zui_button(ui, acts, "Open", ZUI_BTN_PRIMARY);
    v->rename = zui_button(ui, acts, "Rename", ZUI_BTN_SECONDARY);
    v->trash = zui_button(ui, acts, "Move to Trash", ZUI_BTN_SECONDARY);
    v->put_back = zui_button(ui, acts, "Put back", ZUI_BTN_SECONDARY);
    burrow_view_sync(v);
}

void burrow_view_sync(burrow_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    burrow_t *b = v->m;
    bool trash = burrow_in_trash(b);
    char line[ZUI_LABEL_MAX];
    zv_cat(line, sizeof line, b->space[0] ? b->space : "Home", trash ? " \xC2\xB7 Trash" : " / ");
    if (!trash) zv_cat(line, sizeof line, line, b->dir);
    zui_set_text(ui, v->path, line);
    zui_list_set_rows(ui, v->list, (int32_t) b->n);
    zui_list_select(ui, v->list, b->sel);
    zui_set_disabled(ui, v->up, b->dir[0] == 0);
    zui_set_hidden(ui, v->empty, !trash);
    zui_set_hidden(ui, v->new_dir, trash);
    zui_set_hidden(ui, v->new_text, trash);
    zui_set_text(ui, v->trash_view, trash ? "Files" : "Trash");
    bool none = b->n == 0; /* W12 */
    zui_set_hidden(ui, v->list, none);
    zui_set_hidden(ui, v->none, !none);
    zui_widget_t *nw = zui_w(ui, v->none);
    if (nw) {
        zui_set_text(ui, nw->first, trash ? "The Trash is empty" : "This folder is empty");
        zui_set_hidden(ui, nw->last, trash); /* New folder, not in the Trash */
    }

    char st[ZUI_LABEL_MAX];
    uint32_t n = zv_u64(st, sizeof st, b->n);
    zv_cat(st + n, sizeof st - n, b->n == 1 ? " item" : " items",
           b->truncated ? " (more not shown)" : 0);
    zv_shell_status(&v->sh, st);

    bool have = b->sel >= 0 && (uint32_t) b->sel < b->n;
    const burrow_entry_t *e = have ? &b->e[b->sel] : 0;
    zui_set_text(ui, v->d_name, e ? e->name : "Nothing selected");
    zui_set_text(ui, v->d_kind, e ? KIND[e->kind < 6 ? e->kind : 5] : "");
    char sz[32] = "";
    if (e && !e->dir) zv_bytes(sz, sizeof sz, e->size);
    zui_set_text(ui, v->d_size, sz);
    zui_set_hidden(ui, v->open, !e || trash);
    zui_set_hidden(ui, v->rename, !e || trash);
    zui_set_hidden(ui, v->trash, !e);
    zui_set_text(ui, v->trash, trash ? "Delete for good" : "Move to Trash");
    zui_w(ui, v->trash)->style = trash ? ZUI_BTN_DANGER : ZUI_BTN_SECONDARY;
    zui_set_hidden(ui, v->put_back, !e || !trash);
    zui_set_hidden(ui, v->name_field, !v->naming);
    zv_shell_held(&v->sh, &b->held);

    /* V3: one pane at a time on a phone */
    bool compact = zv_compact(&v->sh);
    zui_set_hidden(ui, v->back, !compact);
    zui_set_hidden(ui, v->list_pane, compact && v->show_detail);
    zui_set_hidden(ui, v->detail, compact && !v->show_detail);
    zui_set_size(ui, v->detail, compact ? ZUI_SIZE_FLEX : ZUI_SIZE_FIXED, compact ? 1 : 320);
}

static void start_naming(burrow_view_t *v, uint8_t what)
{
    zui_ui_t *ui = v->sh.ui;
    v->naming = true;
    v->name_for = what;
    uint32_t n = 0;
    if (what == 0 && v->m->sel >= 0)
        n = zv_cat(v->name_buf, sizeof v->name_buf, v->m->e[v->m->sel].name, 0);
    else
        v->name_buf[0] = 0;
    zui_w(ui, v->name_field)->u.field.placeholder[0] = 0;
    zv_cat(zui_w(ui, v->name_field)->u.field.placeholder, ZUI_LABEL_MAX,
           what == 1   ? "New folder name"
           : what == 2 ? "New file name"
                       : "New name",
           0);
    v->show_detail = true;
    burrow_view_sync(v);
    zui_field_set(ui, v->name_field, n);
    zui_field_select(ui, v->name_field, 0, n);
    zui_focus(ui, v->name_field, true);
}

/* A folder is entered; a file goes to the host's open_fn. Returns the
 * model's result for the error line. */
static int open_sel(burrow_view_t *v, int32_t i)
{
    burrow_t *b = v->m;
    if (i < 0 || (uint32_t) i >= b->n) return ZXM_OK;
    if (b->e[i].dir) {
        v->show_detail = false;
        return burrow_enter(b, (uint32_t) i);
    }
    char path[BURROW_PATH_MAX + 1];
    int rc = burrow_path(b, (uint32_t) i, path, sizeof path);
    if (rc == ZXM_OK && v->open_fn) v->open_fn(v->open_ctx, b->e[i].kind, b->space, path);
    return rc;
}

bool burrow_view_action(burrow_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    burrow_t *b = v->m;
    zv_shell_t *s = &v->sh;
    zui_ui_t *ui = s->ui;
    int rc = ZXM_OK;
    bool mine = true;
    if (w == v->list && a == ZUI_ACT_SELECT) {
        rc = burrow_select(b, arg);
        v->naming = false;
        if (zv_compact(s) && arg >= 0) v->show_detail = true;
    } else if (w == v->list && a == ZUI_ACT_OPEN) {
        burrow_select(b, arg);
        rc = open_sel(v, arg);
    } else if (w == v->list && a == ZUI_ACT_CONTEXT && arg >= 0) {
        static const char *const in_files[] = {"Open", "Rename", "-", "Move to Trash"};
        static const char *const in_trash[] = {"Put back", "-", "Delete for good"};
        zui_widget_t *lw = zui_w(ui, v->list);
        int32_t pt = lw->u.list.row_pt                       ? lw->u.list.row_pt
                     : ui->screen.input == ZUI_INPUT_POINTER ? 28
                                                             : ZUI_TOUCH_MIN;
        int32_t y = lw->r.y + (arg - lw->u.list.top + 1) * zui_px(&ui->screen, pt);
        if (burrow_in_trash(b))
            zui_menu_open(ui, v->list, lw->r.x + zui_px(&ui->screen, 24), y, in_trash, 0, 3);
        else
            zui_menu_open(ui, v->list, lw->r.x + zui_px(&ui->screen, 24), y, in_files, 0, 4);
    } else if (w == v->list && a == ZUI_ACT_PICK) {
        bool trash = burrow_in_trash(b);
        if (!trash && arg == 0)
            rc = open_sel(v, b->sel);
        else if (!trash && arg == 1)
            start_naming(v, 0);
        else if (b->sel >= 0 && ((!trash && arg == 3) || (trash && arg == 2)))
            rc = burrow_trash(b, (uint32_t) b->sel);
        else if (trash && arg == 0 && b->sel >= 0)
            rc = burrow_put_back(b, (uint32_t) b->sel, "");
        if (arg == 1 && !trash) return true;
    } else if (w == v->up && a == ZUI_ACT_PRESS) {
        rc = burrow_up(b);
    } else if (w == v->trash_view && a == ZUI_ACT_PRESS) {
        rc = burrow_open(b, burrow_in_trash(b) ? "" : ".Trash");
    } else if (w == v->empty && a == ZUI_ACT_PRESS) {
        uint32_t removed = 0;
        rc = burrow_empty_trash(b, &removed);
    } else if ((w == v->new_dir || w == v->none) && a == ZUI_ACT_PRESS) {
        start_naming(v, 1);
        return true;
    } else if (w == v->new_text && a == ZUI_ACT_PRESS) {
        start_naming(v, 2);
        return true;
    } else if (w == v->rename && a == ZUI_ACT_PRESS) {
        start_naming(v, 0);
        return true;
    } else if (w == v->name_field && a == ZUI_ACT_SUBMIT) {
        if (!v->name_buf[0]) return true;
        if (v->name_for == 1)
            rc = burrow_mkdir(b, v->name_buf);
        else if (v->name_for == 2)
            rc = burrow_new_text(b, v->name_buf);
        else if (b->sel >= 0)
            rc = burrow_rename(b, (uint32_t) b->sel, v->name_buf);
        if (rc == ZXM_OK) v->naming = false;
    } else if (w == v->open && a == ZUI_ACT_PRESS) {
        rc = open_sel(v, b->sel);
    } else if (w == v->trash && a == ZUI_ACT_PRESS && b->sel >= 0) {
        rc = burrow_trash(b, (uint32_t) b->sel);
    } else if (w == v->put_back && a == ZUI_ACT_PRESS && b->sel >= 0) {
        rc = burrow_put_back(b, (uint32_t) b->sel, "");
    } else if (w == v->back && a == ZUI_ACT_PRESS) {
        v->show_detail = false;
        v->naming = false;
    } else if (w == s->root && a == ZUI_ACT_PICK) {
        rc = burrow_ai(b, arg);
    } else if (w == s->held && a == ZUI_ACT_ACCEPT) {
        rc = burrow_ai_accept(b);
    } else if (w == s->held && a == ZUI_ACT_REJECT) {
        rc = burrow_ai_reject(b);
    } else {
        mine = false;
    }
    if (mine) {
        zv_shell_result(s, rc, b->link);
        burrow_view_sync(v);
    }
    return mine;
}
