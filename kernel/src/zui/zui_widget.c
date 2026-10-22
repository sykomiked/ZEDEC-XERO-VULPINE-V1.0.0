/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_widget.c — the widget set (zui_widget.h): building, layout, painting,
 * events, focus, menus, the Ask palette and the accessibility text. */
#include "zui_widget.h"
#include "zui_progress.h"
#include "zui_tokens.h"

/* C6 (zui_palette.h): every colour by role, in the active set */
#define C(role) zui_color(ui->colors, ZUI_C_##role)

/* ---- small helpers ------------------------------------------------------ */

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t n = s ? zui_utf8_fit(s, slen(s), cap - 1u) : 0;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
}

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void mset(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void mmove(char *d, const char *s, uint32_t n)
{
    if (d < s)
        for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    else
        for (uint32_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static zui_rect_t rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    zui_rect_t r = {x, y, w, h};
    return r;
}

static zui_rect_t inset(zui_rect_t r, int32_t d)
{
    return rect(r.x + d, r.y + d, imax(0, r.w - 2 * d), imax(0, r.h - 2 * d));
}

static bool overlap(zui_rect_t a, zui_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static int32_t PX(const zui_ui_t *ui, int32_t pt)
{
    return zui_px(&ui->screen, pt);
}

/* ---- text metrics (a missing font measures as a plain monospace) -------- */

static const zui_text_t *face(const zui_ui_t *ui, zui_font_role_t r)
{
    switch (r) {
    case ZUI_TEXT_LABEL:
        return &ui->theme.label;
    case ZUI_TEXT_TITLE:
        return &ui->theme.title;
    case ZUI_TEXT_MONO:
        return &ui->theme.mono;
    default:
        return &ui->theme.body;
    }
}

static int32_t fsize(const zui_text_t *t)
{
    return t->size_px ? (int32_t) t->size_px : 14;
}

static int32_t ascent(const zui_text_t *t)
{
    return t->font ? zui_text_ascent(t) : (fsize(t) * 4 + 4) / 5;
}

/* Cap height in px: the top of "H" from the font's glyph table, or about
 * seven tenths of the ascent without a font. Button labels centre on it. */
static int32_t cap_h(const zui_text_t *t)
{
    const ttf_font_t *f = t->font;
    uint32_t g = f && f->valid && f->units_per_em ? ttf_glyph_index(f, 'H') : 0;
    uint32_t esz = f && f->loca_long ? 4u : 2u;
    if (g && g < f->num_glyphs && f->loca_off + (g + 2u) * esz <= f->len) {
        const uint8_t *p = f->data + f->loca_off + g * esz;
        uint32_t off, end;
        if (f->loca_long) {
            off = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
            end = ((uint32_t) p[4] << 24) | ((uint32_t) p[5] << 16) | ((uint32_t) p[6] << 8) | p[7];
        } else {
            off = (((uint32_t) p[0] << 8) | p[1]) * 2u;
            end = (((uint32_t) p[2] << 8) | p[3]) * 2u;
        }
        if (end >= off + 10u && off + 10u <= f->glyf_len && f->glyf_off + off + 10u <= f->len) {
            const uint8_t *h = f->data + f->glyf_off + off;
            int32_t ymax = (int16_t) (((uint32_t) h[8] << 8) | h[9]);
            if (ymax > 0)
                return (int32_t) (((int64_t) ymax * fsize(t) + f->units_per_em / 2u) /
                                  f->units_per_em);
        }
    }
    return (ascent(t) * 72 + 50) / 100;
}

/* The baseline that centres a line's capitals in a box of height h at y. */
static int32_t cap_base(const zui_text_t *t, int32_t y, int32_t h)
{
    return y + (h + cap_h(t) + 1) / 2;
}

static int32_t line_h(const zui_text_t *t)
{
    int32_t d = t->font ? -zui_text_descent(t) : (fsize(t) + 4) / 5;
    return ascent(t) + d + (fsize(t) + 3) / 4;
}

/* Advance of bytes [a, b) of s, in 1/256 px. */
static int32_t span_q8(const zui_text_t *t, const char *s, uint32_t a, uint32_t b)
{
    if (!t->font) {
        int32_t n = 0;
        for (uint32_t i = a; i < b; i++)
            if (((uint8_t) s[i] & 0xC0u) != 0x80u) n += s[i] == '\t' ? 4 : 1;
        return n * fsize(t) * 160; /* 5/8 em */
    }
    int32_t w = 0;
    char tmp[64];
    while (a < b) {
        uint32_t n = 0;
        while (a < b && n + 4u < sizeof tmp) {
            if (s[a] == '\t') {
                for (int k = 0; k < 4; k++) tmp[n++] = ' ';
                a++;
                continue;
            }
            uint32_t e = zui_utf8_next(s, b, a);
            while (a < e) tmp[n++] = s[a++];
        }
        tmp[n] = 0;
        w += zui_text_width_q8(t, tmp);
    }
    return w;
}

static int32_t text_w(const zui_text_t *t, const char *s)
{
    return (span_q8(t, s, 0, slen(s)) + 255) >> 8;
}

/* Draw bytes [a, b) of s at pen x (px). Returns the pen x after. */
static int32_t draw_span(zui_surface_t *sf, const zui_text_t *t, int32_t x, int32_t base,
                         uint32_t rgb, const char *s, uint32_t a, uint32_t b)
{
    char tmp[128];
    while (a < b) {
        uint32_t n = 0;
        while (a < b && n + 8u < sizeof tmp) {
            if (s[a] == '\t') {
                for (int k = 0; k < 4; k++) tmp[n++] = ' ';
                a++;
                continue;
            }
            if (s[a] == '\n' || s[a] == '\r') {
                a++;
                continue;
            }
            uint32_t e = zui_utf8_next(s, b, a);
            while (a < e) tmp[n++] = s[a++];
        }
        tmp[n] = 0;
        if (t->font && t->cache)
            x = zui_text(sf, t, x, base, rgb, tmp);
        else
            x += (span_q8(t, tmp, 0, n) + 255) >> 8;
    }
    return x;
}

/* ---- the pool ----------------------------------------------------------- */

zui_widget_t *zui_w(zui_ui_t *ui, zui_id_t id)
{
    if (!ui || id < 0 || (uint32_t) id >= ui->n) return 0;
    return &ui->w[id];
}

static const zui_widget_t *cw(const zui_ui_t *ui, zui_id_t id)
{
    if (!ui || id < 0 || (uint32_t) id >= ui->n) return 0;
    return &ui->w[id];
}

void zui_ui_clear(zui_ui_t *ui)
{
    ui->n = 0;
    ui->root = ui->focus = ui->hover = ui->pressed = ZUI_NONE;
    ui->drag_list = ZUI_NONE;
    ui->modal = ui->tip_for = ZUI_NONE;
    ui->tip_r = rect(0, 0, 0, 0);
    ui->menu.open = false;
    ui->palette.open = false;
    ui->layout_dirty = true;
    ui->overflow = 0;
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}

void zui_ui_init(zui_ui_t *ui, const zui_screen_t *screen, const zui_theme_t *theme)
{
    mset(ui, sizeof *ui);
    ui->screen = *screen;
    if (!ui->screen.scale_q8) ui->screen.scale_q8 = 256;
    if (theme) ui->theme = *theme;
    zui_plan(&ui->screen, &ui->plan);
    zui_gesture_init(&ui->gesture, zui_px(&ui->screen, ZUI_TAP_SLOP_PX));
    ui->palette.owner = ZUI_NONE;
    ui->menu.owner = ZUI_NONE;
    zui_ui_clear(ui);
}

void zui_ui_set_colors(zui_ui_t *ui, const zui_colors_t *colors)
{
    if (ui->colors == colors) return;
    ui->colors = colors;
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}

uint32_t zui_ui_color(const zui_ui_t *ui, zui_color_role_t role)
{
    return zui_color(ui ? ui->colors : 0, role);
}

void zui_ui_on(zui_ui_t *ui, zui_on_fn on, void *ctx)
{
    ui->on = on;
    ui->on_ctx = ctx;
}

static void emit(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg)
{
    if (ui->on) ui->on(ui, w, a, arg, ui->on_ctx);
}

static zui_id_t add(zui_ui_t *ui, zui_id_t parent, zui_kind_t k)
{
    if (parent == ZUI_NONE && ui->root != ZUI_NONE) return ZUI_NONE;
    if (parent != ZUI_NONE && !cw(ui, parent)) return ZUI_NONE;
    if (ui->n >= ZUI_WIDGETS) {
        ui->overflow++;
        return ZUI_NONE;
    }
    zui_id_t id = (zui_id_t) ui->n++;
    zui_widget_t *w = &ui->w[id];
    mset(w, sizeof *w);
    w->kind = k;
    w->parent = parent;
    w->first = w->last = w->next = ZUI_NONE;
    w->font = ZUI_TEXT_BODY;
    if (parent == ZUI_NONE) {
        ui->root = id;
        w->sizing = ZUI_SIZE_FLEX;
        w->size = 1;
    } else {
        zui_widget_t *p = &ui->w[parent];
        if (p->last == ZUI_NONE)
            p->first = id;
        else
            ui->w[p->last].next = id;
        p->last = id;
    }
    ui->layout_dirty = true;
    return id;
}

zui_id_t zui_box(zui_ui_t *ui, zui_id_t parent, zui_dir_t dir)
{
    zui_id_t id = add(ui, parent, ZUI_W_BOX);
    if (id != ZUI_NONE) ui->w[id].dir = dir;
    return id;
}

zui_id_t zui_panel(zui_ui_t *ui, zui_id_t parent, zui_dir_t dir)
{
    zui_id_t id = zui_box(ui, parent, dir);
    if (id != ZUI_NONE) {
        ui->w[id].panel = true;
        ui->w[id].pad = ZUI_S3;
        ui->w[id].gap = ZUI_S2;
    }
    return id;
}

zui_id_t zui_label(zui_ui_t *ui, zui_id_t parent, const char *text)
{
    zui_id_t id = add(ui, parent, ZUI_W_LABEL);
    if (id != ZUI_NONE) scopy(ui->w[id].label, ZUI_LABEL_MAX, text);
    return id;
}

zui_id_t zui_button(zui_ui_t *ui, zui_id_t parent, const char *text, zui_btn_style_t style)
{
    zui_id_t id = add(ui, parent, ZUI_W_BUTTON);
    if (id != ZUI_NONE) {
        scopy(ui->w[id].label, ZUI_LABEL_MAX, text);
        ui->w[id].style = style;
        ui->w[id].focusable = true;
    }
    return id;
}

zui_id_t zui_list(zui_ui_t *ui, zui_id_t parent, zui_row_fn row, void *ctx)
{
    zui_id_t id = add(ui, parent, ZUI_W_LIST);
    if (id != ZUI_NONE) {
        zui_widget_t *w = &ui->w[id];
        w->focusable = true;
        w->sizing = ZUI_SIZE_FLEX;
        w->size = 1;
        w->u.list.sel = -1;
        w->u.list.row = row;
        w->u.list.row_ctx = ctx;
    }
    return id;
}

zui_id_t zui_field(zui_ui_t *ui, zui_id_t parent, char *buf, uint32_t cap, bool multiline)
{
    zui_id_t id = add(ui, parent, ZUI_W_FIELD);
    if (id != ZUI_NONE) {
        zui_widget_t *w = &ui->w[id];
        zui_field_t *f = &w->u.field;
        w->focusable = true;
        f->buf = buf;
        f->cap = cap;
        f->len = buf ? zui_utf8_fit(buf, slen(buf), cap ? cap - 1u : 0) : 0;
        f->caret = f->anchor = f->len;
        f->want_x = -1;
        f->multiline = multiline;
        if (multiline) {
            w->sizing = ZUI_SIZE_FLEX;
            w->size = 1;
        }
    }
    return id;
}

zui_id_t zui_scroll(zui_ui_t *ui, zui_id_t parent)
{
    zui_id_t id = add(ui, parent, ZUI_W_SCROLL);
    if (id != ZUI_NONE) {
        ui->w[id].dir = ZUI_COL;
        ui->w[id].sizing = ZUI_SIZE_FLEX;
        ui->w[id].size = 1;
    }
    return id;
}

zui_id_t zui_held(zui_ui_t *ui, zui_id_t parent)
{
    zui_id_t id = add(ui, parent, ZUI_W_HELD);
    if (id == ZUI_NONE) return id;
    zui_widget_t *w = &ui->w[id];
    w->dir = ZUI_COL;
    w->pad = ZUI_S3;
    w->gap = ZUI_S2;
    w->hidden = true; /* until there is an answer */
    zui_id_t meta = zui_label(ui, id, "");
    zui_set_font(ui, meta, ZUI_TEXT_LABEL, ZUI_GOLD);
    zui_id_t body = zui_field(ui, id, 0, 0, true);
    zui_widget_t *b = zui_w(ui, body);
    if (b) {
        b->u.field.readonly = true;
        b->sizing = ZUI_SIZE_FIT;
        b->min_pt = 40;
    }
    zui_id_t row = zui_box(ui, id, ZUI_ROW);
    if (row != ZUI_NONE) ui->w[row].gap = ZUI_S2;
    zui_id_t acc = zui_button(ui, row, "Accept", ZUI_BTN_PRIMARY);
    zui_id_t rej = zui_button(ui, row, "Reject", ZUI_BTN_SECONDARY);
    zui_set_shortcut(ui, acc, "\xE2\x8C\x98\xE2\x86\xA9"); /* ⌘↩ */
    (void) rej;
    return id;
}

zui_id_t zui_progress(zui_ui_t *ui, zui_id_t parent)
{
    zui_id_t id = add(ui, parent, ZUI_W_PROGRESS);
    if (id != ZUI_NONE) {
        ui->w[id].u.bar.p = ZUI_BAR_UNKNOWN;
        ui->w[id].u.bar.mode = ZUI_BAR_RELEASE;
    }
    return id;
}

zui_id_t zui_canvas(zui_ui_t *ui, zui_id_t parent, zui_paint_fn paint, void *ctx)
{
    zui_id_t id = add(ui, parent, ZUI_W_CANVAS);
    if (id != ZUI_NONE) {
        ui->w[id].u.canvas.paint = paint;
        ui->w[id].u.canvas.ctx = ctx;
        ui->w[id].sizing = ZUI_SIZE_FLEX;
        ui->w[id].size = 1;
    }
    return id;
}

zui_id_t zui_separator(zui_ui_t *ui, zui_id_t parent)
{
    return add(ui, parent, ZUI_W_SEPARATOR);
}

/* ---- W8: choices ------------------------------------------------------------ */

static zui_id_t choice(zui_ui_t *ui, zui_id_t parent, zui_kind_t k, const char *text, bool on)
{
    zui_id_t id = add(ui, parent, k);
    if (id != ZUI_NONE) {
        scopy(ui->w[id].label, ZUI_LABEL_MAX, text);
        ui->w[id].focusable = true;
        ui->w[id].u.check.state = on ? 1u : 0u;
    }
    return id;
}

zui_id_t zui_toggle(zui_ui_t *ui, zui_id_t parent, const char *text, bool on)
{
    return choice(ui, parent, ZUI_W_TOGGLE, text, on);
}

zui_id_t zui_check(zui_ui_t *ui, zui_id_t parent, const char *text, bool on)
{
    return choice(ui, parent, ZUI_W_CHECK, text, on);
}

zui_id_t zui_radio(zui_ui_t *ui, zui_id_t parent, const char *text, bool on)
{
    zui_id_t id = choice(ui, parent, ZUI_W_RADIO, text, false);
    if (on) zui_set_checked(ui, id, 1);
    return id;
}

zui_id_t zui_slider(zui_ui_t *ui, zui_id_t parent, int32_t min, int32_t max, int32_t step,
                    int32_t value)
{
    zui_id_t id = add(ui, parent, ZUI_W_SLIDER);
    if (id != ZUI_NONE) {
        zui_widget_t *w = &ui->w[id];
        w->focusable = true;
        w->u.range.min = min;
        w->u.range.max = imax(min, max);
        w->u.range.step = imax(1, step);
        w->u.range.value = min;
        zui_slider_set(ui, id, value);
    }
    return id;
}

zui_id_t zui_tabs(zui_ui_t *ui, zui_id_t parent, const char *const *labels, uint32_t n, int32_t sel,
                  bool segmented)
{
    zui_id_t id = add(ui, parent, ZUI_W_TABS);
    if (id == ZUI_NONE) return id;
    zui_widget_t *w = &ui->w[id];
    w->dir = ZUI_ROW;
    w->gap = segmented ? 0 : ZUI_S1;
    w->pad = segmented ? 2 : 0;
    w->u.tabs.segmented = segmented;
    for (uint32_t i = 0; i < n; i++) {
        zui_id_t t = add(ui, id, ZUI_W_TAB);
        if (t == ZUI_NONE) break;
        scopy(ui->w[t].label, ZUI_LABEL_MAX, labels[i]);
        ui->w[t].focusable = true;
    }
    ui->w[id].u.tabs.sel = -1;
    zui_tabs_select(ui, id, sel);
    return id;
}

/* ---- W9: dialogs ------------------------------------------------------------- */

zui_id_t zui_dialog(zui_ui_t *ui, const char *title)
{
    if (ui->root == ZUI_NONE) return ZUI_NONE;
    zui_id_t id = add(ui, ui->root, ZUI_W_DIALOG);
    if (id == ZUI_NONE) return id;
    zui_widget_t *w = &ui->w[id];
    w->dir = ZUI_COL;
    w->pad = ZUI_S5;
    w->gap = ZUI_S3;
    w->hidden = true;
    w->u.dialog.back = ZUI_NONE;
    scopy(w->label, ZUI_LABEL_MAX, title);
    scopy(w->name, ZUI_NAME_MAX, title);
    if (title && title[0]) {
        zui_id_t t = zui_label(ui, id, title);
        zui_set_font(ui, t, ZUI_TEXT_TITLE, 0);
    }
    return id;
}

static bool within(const zui_ui_t *ui, zui_id_t id, zui_id_t top)
{
    for (; id != ZUI_NONE; id = ui->w[id].parent)
        if (id == top) return true;
    return false;
}

static bool can_focus(const zui_ui_t *ui, zui_id_t id);
static zui_id_t preorder_next(const zui_ui_t *ui, zui_id_t id);
static void tip_arm(zui_ui_t *ui, zui_id_t id, bool kbd);

void zui_dialog_open(zui_ui_t *ui, zui_id_t id)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_DIALOG || ui->modal == id) return;
    if (ui->modal != ZUI_NONE) zui_dialog_close(ui, ui->modal); /* one at a time */
    zui_menu_close(ui);
    zui_palette_close(ui);
    w->u.dialog.back = ui->focus;
    w->u.dialog.sheet = ui->plan.cls == ZUI_COMPACT;
    w->hidden = false;
    ui->modal = id;
    ui->layout_dirty = true;
    bool kbd = ui->focus_visible;
    ui->focus = ZUI_NONE;
    /* W9: focus moves in, to its first control */
    for (zui_id_t c = ui->w[id].first; c != ZUI_NONE && within(ui, c, id); c = preorder_next(ui, c))
        if (can_focus(ui, c)) {
            zui_focus(ui, c, kbd);
            break;
        }
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}

void zui_dialog_close(zui_ui_t *ui, zui_id_t id)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_DIALOG || w->hidden) return;
    w->hidden = true;
    if (ui->modal == id) ui->modal = ZUI_NONE;
    ui->layout_dirty = true;
    zui_id_t back = w->u.dialog.back;
    w->u.dialog.back = ZUI_NONE;
    if (ui->focus != ZUI_NONE && within(ui, ui->focus, id)) ui->focus = ZUI_NONE;
    if (back != ZUI_NONE && cw(ui, back) && !within(ui, back, id))
        zui_focus(ui, back, ui->focus_visible);
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}

/* ---- W12: the empty state ------------------------------------------------------ */

zui_id_t zui_empty(zui_ui_t *ui, zui_id_t parent, zui_icon_t icon, const char *line,
                   const char *action)
{
    zui_id_t id = add(ui, parent, ZUI_W_EMPTY);
    if (id == ZUI_NONE) return id;
    zui_widget_t *w = &ui->w[id];
    w->dir = ZUI_COL;
    w->pad = ZUI_S5;
    w->gap = ZUI_S3;
    w->sizing = ZUI_SIZE_FLEX;
    w->size = 1;
    w->u.empty.icon = (uint8_t) icon;
    scopy(w->label, ZUI_LABEL_MAX, line);
    zui_id_t l = zui_label(ui, id, line);
    zui_set_font(ui, l, ZUI_TEXT_BODY, ZUI_TEXT_2);
    if (action && action[0]) zui_button(ui, id, action, ZUI_BTN_PRIMARY);
    return id;
}

/* ---- damage ------------------------------------------------------------- */

void zui_invalidate(zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = cw(ui, id);
    if (!w) return;
    /* the focus ring (two tones, W3) sits just outside a widget */
    zui_damage_add(&ui->damage, inset(w->r, -PX(ui, 4)));
}

void zui_relayout(zui_ui_t *ui)
{
    ui->layout_dirty = true;
}

static void dirty_layout(zui_ui_t *ui, zui_id_t id)
{
    zui_invalidate(ui, id);
    ui->layout_dirty = true;
}

void zui_set_text(zui_ui_t *ui, zui_id_t id, const char *text)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    char tmp[ZUI_LABEL_MAX];
    scopy(tmp, sizeof tmp, text);
    if (seq(tmp, w->label)) return;
    scopy(w->label, ZUI_LABEL_MAX, tmp);
    if (w->sizing == ZUI_SIZE_FIT)
        dirty_layout(ui, id);
    else
        zui_invalidate(ui, id);
}

void zui_set_name(zui_ui_t *ui, zui_id_t id, const char *name)
{
    zui_widget_t *w = zui_w(ui, id);
    if (w) scopy(w->name, ZUI_NAME_MAX, name);
}

void zui_set_hidden(zui_ui_t *ui, zui_id_t id, bool hidden)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->hidden == hidden) return;
    w->hidden = hidden;
    dirty_layout(ui, w->parent != ZUI_NONE ? w->parent : id);
    if (hidden && ui->focus != ZUI_NONE) {
        /* focus inside a hidden widget moves on */
        for (zui_id_t f = ui->focus; f != ZUI_NONE; f = ui->w[f].parent)
            if (f == id) {
                ui->focus = ZUI_NONE;
                break;
            }
    }
}

void zui_set_disabled(zui_ui_t *ui, zui_id_t id, bool disabled)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->disabled == disabled) return;
    w->disabled = disabled;
    if (disabled && ui->focus == id) ui->focus = ZUI_NONE;
    zui_invalidate(ui, id);
}

void zui_set_size(zui_ui_t *ui, zui_id_t id, zui_sizing_t sizing, int32_t size)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    w->sizing = sizing;
    w->size = size;
    ui->layout_dirty = true;
}

void zui_set_pad(zui_ui_t *ui, zui_id_t id, int32_t pad, int32_t gap)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    w->pad = pad;
    w->gap = gap;
    ui->layout_dirty = true;
}

void zui_set_font(zui_ui_t *ui, zui_id_t id, zui_font_role_t role, uint32_t rgb)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    w->font = role;
    w->color = rgb;
    dirty_layout(ui, id);
}

void zui_set_shortcut(zui_ui_t *ui, zui_id_t id, const char *keys)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    scopy(w->shortcut, sizeof w->shortcut, keys);
    dirty_layout(ui, id);
}

void zui_set_tooltip(zui_ui_t *ui, zui_id_t id, const char *tip)
{
    zui_widget_t *w = zui_w(ui, id);
    if (w) scopy(w->tip, sizeof w->tip, tip);
}

void zui_set_wrap(zui_ui_t *ui, zui_id_t id, bool wrap)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w) return;
    w->wrap = wrap;
    ui->layout_dirty = true;
}

void zui_set_checked(zui_ui_t *ui, zui_id_t id, uint8_t state)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || (w->kind != ZUI_W_TOGGLE && w->kind != ZUI_W_CHECK && w->kind != ZUI_W_RADIO)) return;
    if (state > (w->kind == ZUI_W_CHECK ? 2u : 1u)) state = 1;
    if (w->kind == ZUI_W_RADIO && state && w->parent != ZUI_NONE)
        for (zui_id_t c = ui->w[w->parent].first; c != ZUI_NONE; c = ui->w[c].next)
            if (c != id && ui->w[c].kind == ZUI_W_RADIO && ui->w[c].u.check.state) {
                ui->w[c].u.check.state = 0;
                zui_invalidate(ui, c);
            }
    if (w->u.check.state == state) return;
    w->u.check.state = state;
    zui_invalidate(ui, id);
}

uint8_t zui_checked(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = cw(ui, id);
    return w && (w->kind == ZUI_W_TOGGLE || w->kind == ZUI_W_CHECK || w->kind == ZUI_W_RADIO)
               ? w->u.check.state
               : 0u;
}

void zui_slider_set(zui_ui_t *ui, zui_id_t id, int32_t v)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_SLIDER) return;
    int32_t lo = w->u.range.min, hi = w->u.range.max, st = imax(1, w->u.range.step);
    v = imax(lo, imin(v, hi));
    int64_t k = ((int64_t) v - lo + st / 2) / st; /* to the nearest step */
    int64_t on = lo + k * st;
    v = on > hi ? hi : (int32_t) on;
    if (w->u.range.value == v) return;
    w->u.range.value = v;
    zui_invalidate(ui, id);
}

int32_t zui_slider_value(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = cw(ui, id);
    return w && w->kind == ZUI_W_SLIDER ? w->u.range.value : 0;
}

void zui_tabs_select(zui_ui_t *ui, zui_id_t id, int32_t sel)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_TABS) return;
    int32_t n = 0;
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) n++;
    sel = n ? imax(0, imin(sel, n - 1)) : -1;
    if (w->u.tabs.sel == sel) return;
    w->u.tabs.sel = sel;
    zui_invalidate(ui, id);
}

int32_t zui_choice_index(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = cw(ui, id);
    if (!w || w->parent == ZUI_NONE || (w->kind != ZUI_W_RADIO && w->kind != ZUI_W_TAB)) return -1;
    int32_t i = 0;
    for (zui_id_t c = ui->w[w->parent].first; c != ZUI_NONE && c != id; c = ui->w[c].next)
        if (ui->w[c].kind == w->kind) i++;
    return i;
}

/* ---- lists ---------------------------------------------------------------- */

static int32_t row_px(const zui_ui_t *ui, const zui_widget_t *w)
{
    int32_t pt = w->u.list.row_pt;
    if (!pt) pt = ui->screen.input == ZUI_INPUT_POINTER ? 28 : ZUI_TOUCH_MIN;
    return imax(1, PX(ui, pt));
}

static int32_t list_visible(const zui_ui_t *ui, const zui_widget_t *w)
{
    return imax(1, w->r.h / row_px(ui, w));
}

static void list_reveal(zui_ui_t *ui, zui_widget_t *w)
{
    zui_list_t *l = &w->u.list;
    int32_t vis = list_visible(ui, w);
    if (l->sel >= 0) {
        if (l->sel < l->top) l->top = l->sel;
        if (l->sel >= l->top + vis) l->top = l->sel - vis + 1;
    }
    l->top = imax(0, imin(l->top, imax(0, l->rows - vis)));
}

void zui_list_set_rows(zui_ui_t *ui, zui_id_t id, int32_t rows)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_LIST) return;
    zui_list_t *l = &w->u.list;
    l->rows = imax(0, rows);
    if (l->sel >= l->rows) l->sel = l->rows - 1;
    list_reveal(ui, w);
    zui_invalidate(ui, id);
}

void zui_list_select(zui_ui_t *ui, zui_id_t id, int32_t row)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_LIST) return;
    zui_list_t *l = &w->u.list;
    if (row < -1) row = -1;
    if (row >= l->rows) row = l->rows - 1;
    l->sel = row;
    list_reveal(ui, w);
    zui_invalidate(ui, id);
}

int32_t zui_list_row_at(const zui_ui_t *ui, zui_id_t id, int32_t x, int32_t y)
{
    const zui_widget_t *w = cw(ui, id);
    if (!w || w->kind != ZUI_W_LIST || !inside(w->r, x, y)) return -1;
    int32_t row = w->u.list.top + (y - w->r.y) / row_px(ui, w);
    return row < w->u.list.rows ? row : -1;
}

/* ---- fields: lines, caret, editing --------------------------------------- */

typedef struct {
    uint32_t a, b; /* bytes drawn on this line */
    uint32_t next; /* where the next line starts */
} line_t;

/* The visual line starting at `at`, wrapped to width px (0 = no wrap). */
static line_t line_at(const zui_text_t *t, const zui_field_t *f, uint32_t at, int32_t width)
{
    line_t L = {at, at, at};
    const char *s = f->buf;
    uint32_t n = f->len;
    if (!f->multiline) {
        L.b = L.next = n;
        return L;
    }
    int32_t lim = width > 0 ? width * 256 : 0x7FFFFFFF;
    int32_t w = 0;
    uint32_t last_space = 0;
    bool have_space = false;
    uint32_t i = at;
    while (i < n && s[i] != '\n') {
        uint32_t e = zui_utf8_next(s, n, i);
        int32_t a = span_q8(t, s, i, e);
        if (w + a > lim && i > at) {
            if (have_space) {
                L.b = last_space;
                L.next = last_space + 1;
            } else {
                L.b = L.next = i;
            }
            return L;
        }
        if (s[i] == ' ') {
            last_space = i;
            have_space = true;
        }
        w += a;
        i = e;
    }
    L.b = i;
    L.next = i < n ? i + 1 : n + 1; /* past the newline; n + 1 = no more lines */
    return L;
}

static int32_t field_text_w(const zui_ui_t *ui, const zui_widget_t *w)
{
    return imax(1, w->r.w - 2 * PX(ui, ZUI_S2));
}

/* ---- W14: a line in visual order ------------------------------------------- */

static zui_caret_line_t VLINE; /* scratch: one line at a time, as zui paints */

/* Lay out line L of field w through ui->caret; false (the left-to-right
 * path) when there is no hook, it declines, or nothing on the line runs
 * right to left. *off is where the line starts (it aligns right in a
 * right-to-left paragraph), 1/256 px from the text's left. */
static bool vline(const zui_ui_t *ui, const zui_widget_t *w, line_t L, int32_t *off)
{
    const zui_field_t *f = &w->u.field;
    if (!ui->caret || !f->buf || f->secret) return false;
    if (!ui->caret(face(ui, w->font), span_q8, f->buf, f->len, L.a, L.b, &VLINE)) return false;
    bool any = VLINE.rtl;
    for (uint32_t i = 0; i < VLINE.n && !any; i++) any = VLINE.c[i].rtl;
    if (!any) return false;
    *off = VLINE.rtl ? imax(0, field_text_w(ui, w) * 256 - VLINE.width) : 0;
    return true;
}

/* The caret at logical position pos: after the character before it (on
 * its trailing side), else before the character at it. */
static int32_t vcaret(uint32_t pos)
{
    const zui_caret_line_t *v = &VLINE;
    for (uint32_t i = 0; i < v->n; i++)
        if (v->c[i].b == pos) return v->c[i].rtl ? v->c[i].x0 : v->c[i].x1;
    for (uint32_t i = 0; i < v->n; i++)
        if (v->c[i].a == pos) return v->c[i].rtl ? v->c[i].x1 : v->c[i].x0;
    return v->rtl ? 0 : v->width;
}

/* The logical position nearest x (1/256 px along the line). */
static uint32_t vpos(int32_t x, uint32_t empty)
{
    const zui_caret_line_t *v = &VLINE;
    if (!v->n) return empty;
    uint32_t lo = 0, hi = 0;
    for (uint32_t i = 0; i < v->n; i++) {
        if (x >= v->c[i].x0 && x < v->c[i].x1) {
            bool left = x < (v->c[i].x0 + v->c[i].x1) / 2;
            return left == v->c[i].rtl ? v->c[i].b : v->c[i].a;
        }
        if (v->c[i].x0 < v->c[lo].x0) lo = i;
        if (v->c[i].x1 > v->c[hi].x1) hi = i;
    }
    if (x < v->c[lo].x0) return v->c[lo].rtl ? v->c[lo].b : v->c[lo].a;
    return v->c[hi].rtl ? v->c[hi].a : v->c[hi].b;
}

/* One step left (dir < 0) or right on screen; UINT32_MAX at the line's end. */
static uint32_t vstep(uint32_t pos, int32_t dir)
{
    const zui_caret_line_t *v = &VLINE;
    int32_t cur = vcaret(pos), bx = 0;
    uint32_t best = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < v->n; i++)
        for (int k = 0; k < 2; k++) {
            uint32_t p = k ? v->c[i].b : v->c[i].a;
            int32_t x = vcaret(p);
            if (dir < 0 ? x < cur : x > cur) {
                if (best == 0xFFFFFFFFu || (dir < 0 ? x > bx : x < bx)) {
                    best = p;
                    bx = x;
                }
            }
        }
    return best;
}

/* The line holding byte position pos, and its index. */
static line_t line_of(const zui_ui_t *ui, const zui_widget_t *w, uint32_t pos, int32_t *li)
{
    const zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t width = field_text_w(ui, w);
    uint32_t at = 0;
    *li = 0;
    for (;;) {
        line_t L = line_at(t, f, at, width);
        if (pos <= L.b || pos < L.next || L.next > f->len) return L; /* as caret_xy */
        at = L.next;
        (*li)++;
    }
}

/* Line index and x (px from the text's left) of byte position pos. */
static void caret_xy(const zui_ui_t *ui, const zui_widget_t *w, uint32_t pos, int32_t *line,
                     int32_t *x)
{
    const zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t width = field_text_w(ui, w);
    uint32_t at = 0;
    int32_t li = 0;
    for (;;) {
        line_t L = line_at(t, f, at, width);
        bool here = pos <= L.b || L.next > f->len;
        if (here || pos < L.next) {
            int32_t off;
            *line = li;
            if (vline(ui, w, L, &off)) { /* W14 */
                *x = (off + vcaret(pos > L.b ? L.b : pos) + 128) >> 8;
            } else if (here) {
                uint32_t to = pos < L.a ? L.a : pos > L.b ? L.b : pos;
                *x = (span_q8(t, f->buf, L.a, to) + 128) >> 8;
            } else { /* on the space a wrap swallowed */
                *x = (span_q8(t, f->buf, L.a, L.b) + 128) >> 8;
            }
            return;
        }
        at = L.next;
        li++;
    }
}

/* The byte position nearest x on line `line`. */
static uint32_t pos_at(const zui_ui_t *ui, const zui_widget_t *w, int32_t line, int32_t x)
{
    const zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t width = field_text_w(ui, w);
    uint32_t at = 0;
    line_t L = line_at(t, f, 0, width);
    for (int32_t li = 0; li < line && L.next <= f->len; li++) {
        at = L.next;
        L = line_at(t, f, at, width);
    }
    int32_t off;
    if (vline(ui, w, L, &off)) return vpos(x * 256 - off, L.a); /* W14 */
    int32_t pen = 0;
    uint32_t i = L.a;
    while (i < L.b) {
        uint32_t e = zui_utf8_next(f->buf, f->len, i);
        int32_t a = span_q8(t, f->buf, i, e);
        if ((int64_t) x * 256 < (int64_t) pen + a / 2) return i;
        pen += a;
        i = e;
    }
    return L.b;
}

static int32_t field_lines(const zui_ui_t *ui, const zui_widget_t *w)
{
    const zui_field_t *f = &w->u.field;
    if (!f->multiline) return 1;
    const zui_text_t *t = face(ui, w->font);
    int32_t width = field_text_w(ui, w), n = 1;
    uint32_t at = 0;
    for (;;) {
        line_t L = line_at(t, f, at, width);
        if (L.next > f->len) return n;
        at = L.next;
        n++;
    }
}

static void field_reveal(zui_ui_t *ui, zui_widget_t *w)
{
    zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t line, x;
    caret_xy(ui, w, f->caret, &line, &x);
    int32_t lh = line_h(t), inner_w = field_text_w(ui, w);
    int32_t inner_h = imax(lh, w->r.h - 2 * PX(ui, ZUI_S1));
    if (f->multiline) {
        f->scroll_x = 0;
        if (line * lh < f->scroll_y) f->scroll_y = line * lh;
        if ((line + 1) * lh > f->scroll_y + inner_h) f->scroll_y = (line + 1) * lh - inner_h;
    } else {
        f->scroll_y = 0;
        if (x < f->scroll_x) f->scroll_x = x;
        if (x > f->scroll_x + inner_w - 2) f->scroll_x = x - inner_w + 2;
        if (f->scroll_x < 0) f->scroll_x = 0;
    }
}

void zui_field_set(zui_ui_t *ui, zui_id_t id, uint32_t len)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_FIELD) return;
    zui_field_t *f = &w->u.field;
    if (!f->buf) len = 0;
    if (f->cap && len >= f->cap) len = f->cap - 1u;
    f->len = len;
    if (f->buf && f->cap) f->buf[len] = 0;
    if (f->caret > len) f->caret = len;
    if (f->anchor > len) f->anchor = len;
    f->want_x = -1;
    zui_invalidate(ui, id);
    if (w->sizing == ZUI_SIZE_FIT) ui->layout_dirty = true;
}

void zui_field_select(zui_ui_t *ui, zui_id_t id, uint32_t anchor, uint32_t caret)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_FIELD) return;
    zui_field_t *f = &w->u.field;
    f->anchor = anchor > f->len ? f->len : anchor;
    f->caret = caret > f->len ? f->len : caret;
    /* never split a character */
    while (f->anchor > 0 && f->anchor < f->len && ((uint8_t) f->buf[f->anchor] & 0xC0u) == 0x80u)
        f->anchor--;
    while (f->caret > 0 && f->caret < f->len && ((uint8_t) f->buf[f->caret] & 0xC0u) == 0x80u)
        f->caret--;
    field_reveal(ui, w);
    zui_invalidate(ui, id);
}

void zui_field_selection(const zui_ui_t *ui, zui_id_t id, uint32_t *start, uint32_t *end)
{
    const zui_widget_t *w = cw(ui, id);
    if (!w || w->kind != ZUI_W_FIELD) {
        *start = *end = 0;
        return;
    }
    const zui_field_t *f = &w->u.field;
    *start = f->anchor < f->caret ? f->anchor : f->caret;
    *end = f->anchor < f->caret ? f->caret : f->anchor;
}

/* Replace [a, b) with s[0..n). */
static bool field_replace(zui_ui_t *ui, zui_id_t id, uint32_t a, uint32_t b, const char *s,
                          uint32_t n)
{
    zui_widget_t *w = &ui->w[id];
    zui_field_t *f = &w->u.field;
    if (f->readonly || !f->buf) return false;
    if (!f->multiline) { /* one line: newlines become spaces, inserted as typed */
        for (uint32_t i = 0; i < n; i++)
            if (s[i] == '\n' || s[i] == '\r') return false;
    }
    if (f->edit) {
        uint32_t before = f->len;
        if (!f->edit(f->edit_ctx, a, b, s, n)) return false;
        /* the model owns the buffer; its new length is wherever the NUL is */
        f->len = slen(f->buf);
        (void) before;
    } else {
        if (f->len - (b - a) + n + 1u > f->cap) {
            n = zui_utf8_fit(s, n, f->cap - 1u - (f->len - (b - a)));
            if (n == 0 && a == b) return false;
        }
        mmove(f->buf + a + n, f->buf + b, f->len - b);
        for (uint32_t i = 0; i < n; i++) f->buf[a + i] = s[i];
        f->len = f->len - (b - a) + n;
        f->buf[f->len] = 0;
    }
    f->caret = f->anchor = a + n > f->len ? f->len : a + n;
    f->want_x = -1;
    field_reveal(ui, w);
    zui_invalidate(ui, id);
    if (w->sizing == ZUI_SIZE_FIT) ui->layout_dirty = true;
    emit(ui, id, ZUI_ACT_CHANGE, 0);
    return true;
}

static void clip_put(zui_ui_t *ui, const char *s, uint32_t n)
{
    if (ui->clip_set && ui->clip_set(ui->clip_ctx, s, n)) return;
    n = zui_utf8_fit(s, n, sizeof ui->clip_local - 1u);
    for (uint32_t i = 0; i < n; i++) ui->clip_local[i] = s[i];
    ui->clip_local[n] = 0;
    ui->clip_local_len = n;
}

static uint32_t clip_take(zui_ui_t *ui, char *out, uint32_t cap)
{
    if (ui->clip_get) return ui->clip_get(ui->clip_ctx, out, cap);
    uint32_t n = ui->clip_local_len < cap ? ui->clip_local_len : cap;
    for (uint32_t i = 0; i < n; i++) out[i] = ui->clip_local[i];
    return n;
}

static bool is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           ((uint8_t) c & 0x80u);
}

static uint32_t word_left(const zui_field_t *f, uint32_t p)
{
    while (p > 0 && !is_word(f->buf[p - 1])) p--;
    while (p > 0 && is_word(f->buf[p - 1])) p--;
    return p;
}

static uint32_t word_right(const zui_field_t *f, uint32_t p)
{
    while (p < f->len && !is_word(f->buf[p])) p++;
    while (p < f->len && is_word(f->buf[p])) p++;
    return p;
}

static bool field_key(zui_ui_t *ui, zui_id_t id, const zui_event_t *e)
{
    zui_widget_t *w = &ui->w[id];
    zui_field_t *f = &w->u.field;
    bool shift = (e->mods & ZUI_MOD_SHIFT) != 0;
    bool cmd = (e->mods & ZUI_MOD_CMD) != 0;
    bool word = (e->mods & (ZUI_MOD_ALT | ZUI_MOD_CTRL)) != 0 && !(e->mods & ZUI_MOD_META);
    uint32_t s0, s1;
    zui_field_selection(ui, id, &s0, &s1);
    uint32_t p = f->caret;
    int32_t line, x;
    switch (e->key) {
    case ZUI_KEY_LEFT:
    case ZUI_KEY_RIGHT: {
        bool left = e->key == ZUI_KEY_LEFT;
        int32_t li, off;
        line_t L = line_of(ui, w, p, &li);
        if (!cmd && !word && (shift || s0 == s1) && vline(ui, w, L, &off)) {
            /* W14: the caret moves the way it looks */
            uint32_t q = vstep(p, left ? -1 : 1);
            bool fwd = left == VLINE.rtl; /* toward the next line */
            if (q == 0xFFFFFFFFu)
                q = fwd ? (L.next <= f->len ? L.next : f->len)
                        : (L.a ? zui_utf8_prev(f->buf, L.a) : 0);
            p = q;
        } else if (left) {
            if (!shift && s0 != s1)
                p = s0;
            else
                p = cmd && !word ? 0 : word ? word_left(f, p) : zui_utf8_prev(f->buf, p);
        } else {
            if (!shift && s0 != s1)
                p = s1;
            else
                p = cmd && !word ? f->len
                    : word       ? word_right(f, p)
                                 : zui_utf8_next(f->buf, f->len, p);
        }
        break;
    }
    case ZUI_KEY_HOME:
    case ZUI_KEY_END:
        if (!f->multiline || cmd) {
            p = e->key == ZUI_KEY_HOME ? 0 : f->len;
        } else {
            caret_xy(ui, w, p, &line, &x);
            p = pos_at(ui, w, line, e->key == ZUI_KEY_HOME ? 0 : 0x3FFFFFFF);
        }
        break;
    case ZUI_KEY_UP:
    case ZUI_KEY_DOWN:
    case ZUI_KEY_PAGE_UP:
    case ZUI_KEY_PAGE_DOWN: {
        if (!f->multiline) return false; /* focus navigation takes it */
        caret_xy(ui, w, p, &line, &x);
        if (f->want_x < 0) f->want_x = x;
        int32_t step = 1;
        if (e->key == ZUI_KEY_PAGE_UP || e->key == ZUI_KEY_PAGE_DOWN)
            step = imax(1, w->r.h / line_h(face(ui, w->font)) - 1);
        int32_t target =
            e->key == ZUI_KEY_UP || e->key == ZUI_KEY_PAGE_UP ? line - step : line + step;
        int32_t last = field_lines(ui, w) - 1;
        int32_t keep = f->want_x;
        if (target < 0)
            p = 0;
        else if (target > last)
            p = f->len;
        else
            p = pos_at(ui, w, target, keep);
        f->caret = p;
        if (!shift) f->anchor = p;
        f->want_x = keep;
        field_reveal(ui, w);
        zui_invalidate(ui, id);
        return true;
    }
    case ZUI_KEY_BACKSPACE:
    case ZUI_KEY_DELETE:
        if (f->readonly) return true;
        if (s0 == s1) {
            if (e->key == ZUI_KEY_BACKSPACE)
                s0 = word ? word_left(f, s0) : zui_utf8_prev(f->buf, s0);
            else
                s1 = word ? word_right(f, s1) : zui_utf8_next(f->buf, f->len, s1);
        }
        if (s0 != s1) field_replace(ui, id, s0, s1, "", 0);
        return true;
    case ZUI_KEY_ENTER:
        if (f->multiline && !cmd) {
            if (!f->readonly) field_replace(ui, id, s0, s1, "\n", 1);
            return true;
        }
        emit(ui, id, ZUI_ACT_SUBMIT, 0); /* one line, or Cmd-Enter in a text area */
        return true;
    case ZUI_KEY_TAB:
        if (f->multiline && !f->readonly && !shift && !cmd && !(e->mods & ZUI_MOD_CTRL)) {
            field_replace(ui, id, s0, s1, "\t", 1);
            return true;
        }
        return false;
    default:
        if (cmd && e->key == 'a') {
            f->anchor = 0;
            f->caret = f->len;
            zui_invalidate(ui, id);
            return true;
        }
        if (cmd && (e->key == 'c' || e->key == 'x')) {
            if (s0 != s1 && !f->secret) {
                clip_put(ui, f->buf + s0, s1 - s0);
                if (e->key == 'x' && !f->readonly) field_replace(ui, id, s0, s1, "", 0);
            }
            return true;
        }
        if (cmd && e->key == 'v') {
            char tmp[1024];
            uint32_t n = clip_take(ui, tmp, sizeof tmp);
            if (n && !f->readonly) {
                if (!f->multiline)
                    for (uint32_t i = 0; i < n; i++)
                        if (tmp[i] == '\n' || tmp[i] == '\r') tmp[i] = ' ';
                field_replace(ui, id, s0, s1, tmp, zui_utf8_fit(tmp, n, n));
            }
            return true;
        }
        return false;
    }
    f->caret = p;
    if (!shift) f->anchor = p;
    f->want_x = -1;
    field_reveal(ui, w);
    zui_invalidate(ui, id);
    return true;
}

/* ---- intrinsic size ------------------------------------------------------- */

static void natural(zui_ui_t *ui, zui_id_t id, int32_t *nw, int32_t *nh);

static int32_t btn_pad(const zui_ui_t *ui)
{
    return PX(ui, ZUI_S3);
}

/* A row of buttons only (or one marked to wrap) wraps onto more lines when
 * they do not fit, instead of running past the edge (compact screens). */
static bool row_wraps(const zui_ui_t *ui, const zui_widget_t *w)
{
    if (w->dir != ZUI_ROW || w->kind != ZUI_W_BOX) return false;
    if (w->wrap) return true;
    int32_t n = 0;
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        const zui_widget_t *cc = &ui->w[c];
        if (cc->hidden) continue;
        if (cc->kind != ZUI_W_BUTTON || cc->sizing != ZUI_SIZE_FIT) return false;
        n++;
    }
    return n >= 2;
}

static void layout(zui_ui_t *ui, zui_id_t id, zui_rect_t r);

/* Children of a wrapping row, line by line inside `in`; places them when
 * `place`. Returns the height the lines take. */
static int32_t wrap_flow(zui_ui_t *ui, zui_id_t id, zui_rect_t in, bool place)
{
    zui_widget_t *w = &ui->w[id];
    int32_t x = 0, y = 0, lh = 0, gap = PX(ui, w->gap);
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        zui_widget_t *cc = &ui->w[c];
        if (cc->hidden) {
            cc->r = rect(0, 0, 0, 0);
            continue;
        }
        int32_t nw, nh;
        natural(ui, c, &nw, &nh);
        if (cc->sizing == ZUI_SIZE_FIXED) nw = PX(ui, cc->size);
        nw = imin(nw, in.w);
        if (x > 0 && x + nw > in.w) {
            y += lh + gap;
            x = 0;
            lh = 0;
        }
        if (place) layout(ui, c, rect(in.x + x, in.y + y, nw, nh));
        x += nw + gap;
        lh = imax(lh, nh);
    }
    return y + lh;
}

static bool in_flow(const zui_widget_t *c)
{
    return !c->hidden && c->kind != ZUI_W_DIALOG; /* W9: dialogs lie over the screen */
}

/* Widgets whose natural height depends on the width they are given. */
static bool width_first(const zui_ui_t *ui, const zui_widget_t *c)
{
    return (c->kind == ZUI_W_FIELD && c->u.field.multiline) || row_wraps(ui, c);
}

static void natural(zui_ui_t *ui, zui_id_t id, int32_t *nw, int32_t *nh)
{
    zui_widget_t *w = &ui->w[id];
    const zui_text_t *t = face(ui, w->font);
    int32_t W = 0, H = 0;
    switch (w->kind) {
    case ZUI_W_LABEL:
        W = text_w(t, w->label);
        H = line_h(t);
        break;
    case ZUI_W_BUTTON:
        W = text_w(t, w->label) + 2 * btn_pad(ui);
        if (w->shortcut[0]) W += PX(ui, ZUI_S2) + text_w(&ui->theme.label, w->shortcut);
        H = imax(ui->plan.target_px, line_h(t) + PX(ui, ZUI_S2));
        W = imax(W, H);
        break;
    case ZUI_W_FIELD: {
        int32_t lines = 1;
        if (w->u.field.multiline && w->r.w > 0) lines = field_lines(ui, w);
        H = lines * line_h(t) + 2 * PX(ui, ZUI_S1);
        H = imax(H, ui->plan.target_px);
        W = PX(ui, 160);
        break;
    }
    case ZUI_W_LIST:
        H = row_px(ui, w) * imin(imax(w->u.list.rows, 1), 6);
        W = PX(ui, 160);
        break;
    case ZUI_W_PROGRESS:
        H = PX(ui, 3);
        W = PX(ui, 80);
        break;
    case ZUI_W_SEPARATOR:
        H = imax(1, PX(ui, 1));
        W = 1;
        break;
    case ZUI_W_CANVAS:
        H = PX(ui, 48);
        W = PX(ui, 48);
        break;
    case ZUI_W_TOGGLE:
        W = PX(ui, 36) + PX(ui, ZUI_S2) + text_w(t, w->label);
        H = imax(ui->plan.target_px, imax(PX(ui, 22), line_h(t)));
        break;
    case ZUI_W_CHECK:
    case ZUI_W_RADIO:
        W = PX(ui, 18) + PX(ui, ZUI_S2) + text_w(t, w->label);
        H = imax(ui->plan.target_px, imax(PX(ui, 18), line_h(t)));
        break;
    case ZUI_W_SLIDER:
        W = PX(ui, 160);
        H = imax(ui->plan.target_px, PX(ui, 24));
        break;
    case ZUI_W_TAB:
        W = text_w(t, w->label) + 2 * btn_pad(ui);
        H = imax(ui->plan.target_px, line_h(t) + PX(ui, ZUI_S2));
        break;
    default: {
        /* boxes, panels, scroll areas, held blocks: their children */
        int32_t pad = PX(ui, w->pad), gap = PX(ui, w->gap), k = 0;
        for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
            if (!in_flow(&ui->w[c])) continue;
            int32_t cw_, ch_;
            if (w->dir != ZUI_ROW && width_first(ui, &ui->w[c]) && w->r.w > 0)
                ui->w[c].r.w = imax(0, w->r.w - 2 * pad);
            natural(ui, c, &cw_, &ch_);
            const zui_widget_t *cc = &ui->w[c];
            if (cc->sizing == ZUI_SIZE_FIXED) {
                if (w->dir == ZUI_ROW)
                    cw_ = PX(ui, cc->size);
                else
                    ch_ = PX(ui, cc->size);
            }
            if (w->dir == ZUI_ROW) {
                W += cw_ + (k ? gap : 0);
                H = imax(H, ch_);
            } else {
                H += ch_ + (k ? gap : 0);
                W = imax(W, cw_);
            }
            k++;
        }
        W += 2 * pad;
        H += 2 * pad;
        if (w->kind == ZUI_W_EMPTY) { /* W12: the icon above */
            H += PX(ui, 48) + (k ? gap : 0);
            W = imax(W, PX(ui, 48) + 2 * pad);
        }
        if (row_wraps(ui, w) && w->r.w > 0 && W > w->r.w) {
            H = wrap_flow(ui, id, inset(rect(0, 0, w->r.w, 0x3FFFFFFF), pad), false) + 2 * pad;
            W = w->r.w;
        }
        break;
    }
    }
    int32_t mn = PX(ui, w->min_pt);
    if (w->parent != ZUI_NONE && ui->w[w->parent].dir == ZUI_ROW)
        W = imax(W, mn);
    else
        H = imax(H, mn);
    *nw = W;
    *nh = H;
}

/* ---- layout --------------------------------------------------------------- */

/* W12: the line and the action centred under the icon, the block centred. */
static void layout_empty(zui_ui_t *ui, zui_id_t id, zui_rect_t in)
{
    zui_widget_t *w = &ui->w[id];
    int32_t gap = PX(ui, w->gap), total = PX(ui, 48);
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        int32_t nw, nh;
        if (!in_flow(&ui->w[c])) continue;
        natural(ui, c, &nw, &nh);
        total += gap + nh;
    }
    int32_t y = in.y + imax(0, (in.h - total) / 2) + PX(ui, 48) + gap;
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        int32_t nw, nh;
        if (!in_flow(&ui->w[c])) {
            ui->w[c].r = rect(0, 0, 0, 0);
            continue;
        }
        natural(ui, c, &nw, &nh);
        nw = imin(nw, in.w);
        layout(ui, c, rect(in.x + (in.w - nw) / 2, y, nw, nh));
        y += nh + gap;
    }
}

static void layout(zui_ui_t *ui, zui_id_t id, zui_rect_t r)
{
    zui_widget_t *w = &ui->w[id];
    w->r = r;
    if (w->kind == ZUI_W_LIST) list_reveal(ui, w);
    if (w->first == ZUI_NONE) return;
    int32_t pad = PX(ui, w->pad), gap = PX(ui, w->gap);
    zui_rect_t in = inset(r, pad);
    if (w->kind == ZUI_W_HELD) in = inset(r, pad + PX(ui, 2));
    if (w->kind == ZUI_W_EMPTY) {
        layout_empty(ui, id, in);
        return;
    }
    if (row_wraps(ui, w)) {
        int32_t total = 0, k = 0;
        for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
            if (!in_flow(&ui->w[c])) continue;
            int32_t nw, nh;
            natural(ui, c, &nw, &nh);
            total +=
                (ui->w[c].sizing == ZUI_SIZE_FIXED ? PX(ui, ui->w[c].size) : nw) + (k++ ? gap : 0);
        }
        if (total > in.w) {
            wrap_flow(ui, id, in, true);
            return;
        }
    }
    bool row = w->dir == ZUI_ROW && w->kind != ZUI_W_SCROLL;
    int32_t avail = row ? in.w : in.h;
    int32_t fixed = 0, weight = 0, count = 0;
    /* first pass: what the fixed and natural children take */
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        zui_widget_t *cc = &ui->w[c];
        if (!in_flow(cc)) continue;
        if (count) fixed += gap;
        count++;
        if (cc->sizing == ZUI_SIZE_FLEX && w->kind != ZUI_W_SCROLL) {
            weight += imax(1, cc->size);
            fixed += PX(ui, cc->min_pt);
            continue;
        }
        int32_t nw, nh;
        if (width_first(ui, cc) && !row) cc->r.w = in.w;
        natural(ui, c, &nw, &nh);
        fixed += cc->sizing == ZUI_SIZE_FIXED ? PX(ui, cc->size) : row ? nw : nh;
    }
    int32_t left = imax(0, avail - fixed);
    int32_t pos = row ? in.x : in.y;
    if (w->kind == ZUI_W_SCROLL) pos -= w->u.scroll.offset;
    int32_t given = 0, wsum = 0;
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        zui_widget_t *cc = &ui->w[c];
        if (cc->kind == ZUI_W_DIALOG) continue;
        if (cc->hidden) {
            cc->r = rect(0, 0, 0, 0);
            continue;
        }
        int32_t len;
        if (cc->sizing == ZUI_SIZE_FLEX && w->kind != ZUI_W_SCROLL) {
            wsum += imax(1, cc->size);
            /* hand out the remainder exactly: the last flex child ends flush */
            int32_t upto = weight ? (int32_t) (((int64_t) left * wsum) / weight) : 0;
            len = upto - given + PX(ui, cc->min_pt);
            given = upto;
        } else if (cc->sizing == ZUI_SIZE_FIXED) {
            len = PX(ui, cc->size);
        } else {
            int32_t nw, nh;
            if (width_first(ui, cc) && !row) cc->r.w = in.w;
            natural(ui, c, &nw, &nh);
            len = row ? nw : nh;
        }
        zui_rect_t cr = row ? rect(pos, in.y, len, in.h) : rect(in.x, pos, in.w, len);
        layout(ui, c, cr);
        pos += len + gap;
    }
    if (w->kind == ZUI_W_SCROLL) {
        int32_t content = pos - gap - (in.y - w->u.scroll.offset) + pad;
        w->u.scroll.content_h = imax(0, content);
        int32_t maxo = imax(0, w->u.scroll.content_h - r.h);
        if (w->u.scroll.offset > maxo) {
            w->u.scroll.offset = maxo;
            layout(ui, id, r);
        }
    }
}

/* W9: the open dialog, centred (or a sheet along the bottom on a phone). */
static void layout_modal(zui_ui_t *ui)
{
    zui_widget_t *w = &ui->w[ui->modal];
    int32_t W = (int32_t) ui->screen.px_w, H = (int32_t) ui->screen.px_h, g = ui->plan.gutter_px;
    w->u.dialog.sheet = ui->plan.cls == ZUI_COMPACT;
    int32_t maxw = w->u.dialog.sheet ? W : imax(0, imin(W - 2 * g, PX(ui, 480)));
    int32_t nw, nh;
    w->r.w = maxw;
    natural(ui, ui->modal, &nw, &nh);
    int32_t dw = w->u.dialog.sheet ? W : imin(maxw, imax(nw, PX(ui, 320)));
    w->r.w = dw;
    natural(ui, ui->modal, &nw, &nh);
    int32_t dh = imin(nh, imax(0, H - 2 * g));
    zui_rect_t r =
        w->u.dialog.sheet ? rect(0, H - dh, W, dh) : rect((W - dw) / 2, (H - dh) * 2 / 5, dw, dh);
    layout(ui, ui->modal, r);
}

static void do_layout(zui_ui_t *ui)
{
    if (!ui->layout_dirty || ui->root == ZUI_NONE) return;
    ui->layout_dirty = false;
    layout(ui, ui->root, rect(0, 0, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h));
    if (ui->modal != ZUI_NONE && cw(ui, ui->modal)) layout_modal(ui);
    zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
}

/* ---- visibility ----------------------------------------------------------- */

static bool shown(const zui_ui_t *ui, zui_id_t id)
{
    for (; id != ZUI_NONE; id = ui->w[id].parent)
        if (ui->w[id].hidden) return false;
    return true;
}

/* The rectangle a widget is actually seen in (clipped by scroll areas). */
static zui_rect_t seen(const zui_ui_t *ui, zui_id_t id)
{
    zui_rect_t r = ui->w[id].r;
    for (zui_id_t p = ui->w[id].parent; p != ZUI_NONE; p = ui->w[p].parent) {
        if (ui->w[p].kind != ZUI_W_SCROLL) continue;
        zui_rect_t c = ui->w[p].r;
        int32_t x0 = imax(r.x, c.x), y0 = imax(r.y, c.y);
        int32_t x1 = imin(r.x + r.w, c.x + c.w), y1 = imin(r.y + r.h, c.y + c.h);
        r = rect(x0, y0, imax(0, x1 - x0), imax(0, y1 - y0));
    }
    return r;
}

static bool can_focus(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    if (!w->focusable || w->disabled || !shown(ui, id)) return false;
    if (ui->modal != ZUI_NONE && !within(ui, id, ui->modal)) return false; /* W9: the trap */
    if (w->kind == ZUI_W_FIELD && w->u.field.readonly && w->parent != ZUI_NONE &&
        ui->w[w->parent].kind == ZUI_W_HELD)
        return false; /* the held text is read with its buttons */
    /* laid out; a row scrolled out of view still counts (focus scrolls to it),
     * and so does a widget just shown, before the next layout gives it a rect */
    return ui->layout_dirty || (w->r.w > 0 && w->r.h > 0);
}

/* ---- focus ------------------------------------------------------------------ */

static void scroll_into_view(zui_ui_t *ui, zui_id_t id)
{
    for (zui_id_t p = ui->w[id].parent; p != ZUI_NONE; p = ui->w[p].parent) {
        zui_widget_t *s = &ui->w[p];
        if (s->kind != ZUI_W_SCROLL) continue;
        zui_rect_t r = ui->w[id].r;
        int32_t o = s->u.scroll.offset;
        if (r.y < s->r.y)
            o -= s->r.y - r.y;
        else if (r.y + r.h > s->r.y + s->r.h)
            o += (r.y + r.h) - (s->r.y + s->r.h);
        if (o != s->u.scroll.offset) {
            s->u.scroll.offset = imax(0, o);
            layout(ui, p, s->r);
            zui_invalidate(ui, p);
        }
    }
}

void zui_focus(zui_ui_t *ui, zui_id_t id, bool from_keyboard)
{
    if (id != ZUI_NONE && (!cw(ui, id) || !can_focus(ui, id))) return;
    if (ui->focus == id && ui->focus_visible == from_keyboard) return;
    if (ui->focus != ZUI_NONE) zui_invalidate(ui, ui->focus);
    ui->focus = id;
    ui->focus_visible = from_keyboard;
    if (id != ZUI_NONE) {
        if (from_keyboard) scroll_into_view(ui, id);
        zui_invalidate(ui, id);
        ui->caret_ms = ui->now_ms;
        if (from_keyboard) tip_arm(ui, id, true); /* W10: focus shows a tip too */
    }
}

/* The widget after id in tree (pre-)order, or ZUI_NONE at the end. */
static zui_id_t preorder_next(const zui_ui_t *ui, zui_id_t id)
{
    if (ui->w[id].first != ZUI_NONE) return ui->w[id].first;
    for (; id != ZUI_NONE; id = ui->w[id].parent)
        if (ui->w[id].next != ZUI_NONE) return ui->w[id].next;
    return ZUI_NONE;
}

/* W8: roving focus. Of a radio group only the chosen radio (the first when
 * none is) and of a tab list only the chosen tab take Tab-order focus. */
static bool roving_out(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    if (w->parent == ZUI_NONE) return false;
    if (w->kind == ZUI_W_TAB) return zui_choice_index(ui, id) != ui->w[w->parent].u.tabs.sel;
    if (w->kind != ZUI_W_RADIO || w->u.check.state) return false;
    zui_id_t first = ZUI_NONE;
    for (zui_id_t c = ui->w[w->parent].first; c != ZUI_NONE; c = ui->w[c].next) {
        if (ui->w[c].kind != ZUI_W_RADIO || !can_focus(ui, c)) continue;
        if (ui->w[c].u.check.state) return true;
        if (first == ZUI_NONE) first = c;
    }
    return first != id;
}

/* W3: the next (or previous) focusable widget in tree order, wrapping. */
zui_id_t zui_focus_next(zui_ui_t *ui, bool backward)
{
    do_layout(ui);
    if (ui->root == ZUI_NONE) return ZUI_NONE;
    zui_id_t first = ZUI_NONE, last = ZUI_NONE, before = ZUI_NONE, after = ZUI_NONE;
    bool seen_focus = false;
    for (zui_id_t id = ui->root; id != ZUI_NONE; id = preorder_next(ui, id)) {
        if (id == ui->focus) {
            seen_focus = true;
            continue;
        }
        if (!can_focus(ui, id) || roving_out(ui, id)) continue;
        if (first == ZUI_NONE) first = id;
        last = id;
        if (!seen_focus) before = id;
        if (seen_focus && after == ZUI_NONE) after = id;
    }
    zui_id_t to;
    if (!seen_focus)
        to = backward ? last : first;
    else if (backward)
        to = before != ZUI_NONE ? before : last;
    else
        to = after != ZUI_NONE ? after : first;
    if (to == ZUI_NONE) return ui->focus;
    zui_focus(ui, to, true);
    return ui->focus;
}

zui_id_t zui_focus_toward(zui_ui_t *ui, uint16_t arrow)
{
    do_layout(ui);
    if (ui->focus == ZUI_NONE) return zui_focus_next(ui, false);
    zui_rect_t a = ui->w[ui->focus].r;
    int32_t ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    zui_id_t best = ZUI_NONE;
    int64_t best_score = 0;
    for (uint32_t i = 0; i < ui->n; i++) {
        zui_id_t id = (zui_id_t) i;
        if (id == ui->focus || !can_focus(ui, id) || roving_out(ui, id)) continue;
        zui_rect_t b = ui->w[id].r;
        int32_t bx = b.x + b.w / 2, by = b.y + b.h / 2;
        int32_t along, across;
        switch (arrow) {
        case ZUI_KEY_LEFT:
            along = ax - bx;
            across = by - ay;
            if (b.x + b.w > a.x) continue;
            break;
        case ZUI_KEY_RIGHT:
            along = bx - ax;
            across = by - ay;
            if (b.x < a.x + a.w) continue;
            break;
        case ZUI_KEY_UP:
            along = ay - by;
            across = bx - ax;
            if (b.y + b.h > a.y) continue;
            break;
        case ZUI_KEY_DOWN:
            along = by - ay;
            across = bx - ax;
            if (b.y < a.y + a.h) continue;
            break;
        default:
            return ui->focus;
        }
        if (along <= 0) continue;
        /* straight ahead beats diagonal: sideways distance counts double */
        int64_t score = (int64_t) along + 2 * (int64_t) iabs(across);
        if (best == ZUI_NONE || score < best_score) {
            best = id;
            best_score = score;
        }
    }
    if (best != ZUI_NONE) zui_focus(ui, best, true);
    return ui->focus;
}

/* ---- hit testing ------------------------------------------------------------ */

static zui_id_t hit_in(const zui_ui_t *ui, zui_id_t id, int32_t x, int32_t y)
{
    const zui_widget_t *w = &ui->w[id];
    if (w->hidden || !inside(w->r, x, y)) return ZUI_NONE;
    zui_id_t found = ZUI_NONE;
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) {
        if (ui->w[c].kind == ZUI_W_DIALOG) continue; /* W9: only as the modal layer */
        zui_id_t h = hit_in(ui, c, x, y);
        if (h != ZUI_NONE) found = h; /* later siblings paint on top */
    }
    return found != ZUI_NONE ? found : id;
}

zui_id_t zui_hit(const zui_ui_t *ui, int32_t x, int32_t y)
{
    if (ui->root == ZUI_NONE) return ZUI_NONE;
    if (ui->modal != ZUI_NONE) { /* W9: the scrim takes every press outside */
        zui_id_t h = hit_in(ui, ui->modal, x, y);
        return h != ZUI_NONE ? h : ui->modal;
    }
    return hit_in(ui, ui->root, x, y);
}

static zui_id_t held_of(const zui_ui_t *ui, zui_id_t id)
{
    /* the held block an Accept / Reject button belongs to */
    const zui_widget_t *w = cw(ui, id);
    if (!w || w->kind != ZUI_W_BUTTON || w->parent == ZUI_NONE) return ZUI_NONE;
    zui_id_t row = w->parent, held = ui->w[row].parent;
    if (held == ZUI_NONE || ui->w[held].kind != ZUI_W_HELD) return ZUI_NONE;
    return held;
}

static void press(zui_ui_t *ui, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    if (w->disabled) return;
    zui_id_t held = held_of(ui, id);
    if (held != ZUI_NONE) {
        bool accept = ui->w[held].first != ZUI_NONE && id == ui->w[ui->w[held].last].first;
        emit(ui, held, accept ? ZUI_ACT_ACCEPT : ZUI_ACT_REJECT, 0);
        return;
    }
    if (w->parent != ZUI_NONE && ui->w[w->parent].kind == ZUI_W_EMPTY) { /* W12 */
        emit(ui, w->parent, ZUI_ACT_PRESS, 0);
        return;
    }
    emit(ui, id, ZUI_ACT_PRESS, 0);
}

/* ---- W8: choosing ---------------------------------------------------------- */

static bool is_choice(zui_kind_t k)
{
    return k == ZUI_W_TOGGLE || k == ZUI_W_CHECK || k == ZUI_W_RADIO || k == ZUI_W_TAB;
}

/* A press, Enter or Space on a button or a choice. */
static void activate(zui_ui_t *ui, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    if (w->disabled) return;
    switch (w->kind) {
    case ZUI_W_BUTTON:
        press(ui, id);
        break;
    case ZUI_W_TOGGLE:
    case ZUI_W_CHECK: {
        uint8_t to = w->u.check.state == 1u ? 0u : 1u; /* mixed turns on */
        zui_set_checked(ui, id, to);
        emit(ui, id, ZUI_ACT_CHANGE, to);
        break;
    }
    case ZUI_W_RADIO:
        if (!w->u.check.state) {
            zui_set_checked(ui, id, 1);
            emit(ui, id, ZUI_ACT_SELECT, zui_choice_index(ui, id));
        }
        break;
    case ZUI_W_TAB: {
        int32_t i = zui_choice_index(ui, id);
        if (w->parent != ZUI_NONE && ui->w[w->parent].u.tabs.sel != i) {
            zui_tabs_select(ui, w->parent, i);
            emit(ui, w->parent, ZUI_ACT_SELECT, i);
        }
        break;
    }
    default:
        break;
    }
}

/* Arrows in a radio group or a tab list: the next one of the same kind
 * (wrapping, skipping disabled ones) takes focus and is chosen. */
static bool choice_step(zui_ui_t *ui, zui_id_t id, int32_t dir, bool to_end)
{
    zui_widget_t *w = &ui->w[id];
    if (w->parent == ZUI_NONE) return false;
    zui_id_t sib[ZUI_MENU_ITEMS * 2];
    int32_t n = 0, at = -1;
    for (zui_id_t c = ui->w[w->parent].first;
         c != ZUI_NONE && n < (int32_t) (sizeof sib / sizeof sib[0]); c = ui->w[c].next)
        if (ui->w[c].kind == w->kind && (c == id || can_focus(ui, c))) {
            if (c == id) at = n;
            sib[n++] = c;
        }
    if (n < 2 || at < 0) return true;
    int32_t to = to_end ? (dir < 0 ? 0 : n - 1) : (at + dir + n) % n;
    zui_focus(ui, sib[to], true);
    activate(ui, sib[to]);
    return true;
}

static int32_t slider_x0(const zui_ui_t *ui, const zui_widget_t *w)
{
    return w->r.x + PX(ui, 10);
}

static int32_t slider_len(const zui_ui_t *ui, const zui_widget_t *w)
{
    return imax(1, w->r.w - 2 * PX(ui, 10));
}

static void slider_to(zui_ui_t *ui, zui_id_t id, int32_t v)
{
    int32_t was = ui->w[id].u.range.value;
    zui_slider_set(ui, id, v);
    if (ui->w[id].u.range.value != was) emit(ui, id, ZUI_ACT_CHANGE, ui->w[id].u.range.value);
}

static void slider_at(zui_ui_t *ui, zui_id_t id, int32_t x)
{
    const zui_widget_t *w = &ui->w[id];
    int32_t len = slider_len(ui, w), d = imax(0, imin(x - slider_x0(ui, w), len));
    int64_t span = (int64_t) w->u.range.max - w->u.range.min;
    slider_to(ui, id, w->u.range.min + (int32_t) ((span * d + len / 2) / len));
}

static bool choice_key(zui_ui_t *ui, zui_id_t id, const zui_event_t *e)
{
    zui_widget_t *w = &ui->w[id];
    bool space = e->key == ZUI_KEY_SPACE || e->key == ZUI_KEY_ENTER;
    switch (w->kind) {
    case ZUI_W_TOGGLE:
    case ZUI_W_CHECK:
        if (!space) return false;
        activate(ui, id);
        return true;
    case ZUI_W_RADIO:
    case ZUI_W_TAB: {
        if (space) {
            activate(ui, id);
            return true;
        }
        bool tab = w->kind == ZUI_W_TAB;
        if (e->key == ZUI_KEY_LEFT || (!tab && e->key == ZUI_KEY_UP))
            return choice_step(ui, id, -1, false);
        if (e->key == ZUI_KEY_RIGHT || (!tab && e->key == ZUI_KEY_DOWN))
            return choice_step(ui, id, 1, false);
        if (e->key == ZUI_KEY_HOME || e->key == ZUI_KEY_END)
            return choice_step(ui, id, e->key == ZUI_KEY_HOME ? -1 : 1, true);
        return false;
    }
    case ZUI_W_SLIDER: {
        int32_t st = imax(1, w->u.range.step), v = w->u.range.value;
        bool vert = ui->screen.input != ZUI_INPUT_REMOTE; /* a remote's up/down move focus */
        switch (e->key) {
        case ZUI_KEY_LEFT:
            v -= st;
            break;
        case ZUI_KEY_RIGHT:
            v += st;
            break;
        case ZUI_KEY_DOWN:
            if (!vert) return false;
            v -= st;
            break;
        case ZUI_KEY_UP:
            if (!vert) return false;
            v += st;
            break;
        case ZUI_KEY_PAGE_DOWN:
            v -= 10 * st;
            break;
        case ZUI_KEY_PAGE_UP:
            v += 10 * st;
            break;
        case ZUI_KEY_HOME:
            v = w->u.range.min;
            break;
        case ZUI_KEY_END:
            v = w->u.range.max;
            break;
        default:
            return false;
        }
        slider_to(ui, id, v);
        return true;
    }
    default:
        return false;
    }
}

/* ---- the held block -------------------------------------------------------- */

void zui_held_set(zui_ui_t *ui, zui_id_t id, const char *agent, uint64_t tokens, bool exact,
                  const char *text, uint8_t state)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_HELD) return;
    scopy(w->u.held.agent, sizeof w->u.held.agent, agent);
    w->u.held.tokens = tokens;
    w->u.held.exact = exact;
    w->u.held.state = state;
    /* "companion · 120 tokens" or "~120 tokens (estimate)" */
    char meta[ZUI_LABEL_MAX];
    uint32_t n = 0;
    const char *head = state == 2   ? "Accepted \xC2\xB7 "
                       : state == 3 ? "Rejected \xC2\xB7 "
                                    : "Held \xC2\xB7 ";
    for (const char *p = head; *p && n < sizeof meta - 1; p++) meta[n++] = *p;
    for (const char *p = w->u.held.agent; *p && n < sizeof meta - 1; p++) meta[n++] = *p;
    const char *mid = " \xC2\xB7 ";
    for (const char *p = mid; *p && n < sizeof meta - 1; p++) meta[n++] = *p;
    if (!exact && n < sizeof meta - 1) meta[n++] = '~';
    char num[24];
    uint32_t k = 0;
    uint64_t v = tokens;
    do {
        num[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < sizeof num);
    while (k && n < sizeof meta - 1) meta[n++] = num[--k];
    const char *tail = exact ? " tokens" : " tokens (estimate)";
    for (const char *p = tail; *p && n < sizeof meta - 1; p++) meta[n++] = *p;
    meta[n] = 0;
    zui_id_t lab = w->first;
    zui_id_t body = lab != ZUI_NONE ? ui->w[lab].next : ZUI_NONE;
    zui_id_t row = body != ZUI_NONE ? ui->w[body].next : ZUI_NONE;
    zui_set_text(ui, lab, meta);
    zui_widget_t *b = zui_w(ui, body);
    if (b) {
        b->u.field.buf = (char *) (uintptr_t) text; /* read only: never written */
        b->u.field.cap = text ? slen(text) + 1u : 0;
        b->u.field.len = text ? slen(text) : 0;
        b->u.field.caret = b->u.field.anchor = 0;
        b->u.field.scroll_y = 0;
    }
    /* accept / reject only while pending */
    zui_set_hidden(ui, row, state != 1);
    w->hidden = !text || state == 0;
    ui->layout_dirty = true;
    zui_invalidate(ui, id);
}

void zui_progress_set(zui_ui_t *ui, zui_id_t id, uint32_t p, uint8_t mode)
{
    zui_widget_t *w = zui_w(ui, id);
    if (!w || w->kind != ZUI_W_PROGRESS) return;
    if (p != ZUI_BAR_UNKNOWN && p > 256u) p = 256u;
    if (w->u.bar.p == p && w->u.bar.mode == mode) return;
    w->u.bar.p = p;
    w->u.bar.mode = mode;
    zui_invalidate(ui, id);
}

/* ---- menus and the palette -------------------------------------------------- */

static int32_t menu_row(const zui_ui_t *ui)
{
    return PX(ui, ui->screen.input == ZUI_INPUT_POINTER ? 26 : ZUI_TOUCH_MIN);
}

void zui_menu_open(zui_ui_t *ui, zui_id_t owner, int32_t x, int32_t y, const char *const *items,
                   const char *const *keys, uint32_t n)
{
    zui_menu_t *m = &ui->menu;
    if (n > ZUI_MENU_ITEMS) n = ZUI_MENU_ITEMS;
    m->open = true;
    m->palette = false;
    m->owner = owner;
    m->n = n;
    m->hot = -1;
    int32_t wmax = PX(ui, 160);
    for (uint32_t i = 0; i < n; i++) {
        scopy(m->item[i], ZUI_LABEL_MAX, items[i]);
        scopy(m->key[i], sizeof m->key[i], keys ? keys[i] : "");
        m->off[i] = seq(m->item[i], "-");
        int32_t iw = text_w(&ui->theme.body, m->item[i]) + PX(ui, ZUI_S5) +
                     text_w(&ui->theme.label, m->key[i]) + 2 * PX(ui, ZUI_S3);
        wmax = imax(wmax, iw);
    }
    for (uint32_t i = 0; i < n && m->hot < 0; i++)
        if (!m->off[i]) m->hot = (int32_t) i;
    int32_t h = (int32_t) n * menu_row(ui) + 2 * PX(ui, ZUI_S1);
    int32_t W = (int32_t) ui->screen.px_w, H = (int32_t) ui->screen.px_h;
    if (x + wmax > W) x = imax(0, W - wmax);
    if (y + h > H) y = imax(0, H - h);
    m->r = rect(x, y, wmax, h);
    zui_damage_add(&ui->damage, inset(m->r, -PX(ui, 4)));
}

void zui_menu_disable(zui_ui_t *ui, uint32_t item, bool off)
{
    if (item < ui->menu.n) ui->menu.off[item] = off || seq(ui->menu.item[item], "-");
    if (ui->menu.hot == (int32_t) item && off) ui->menu.hot = -1;
}

void zui_menu_close(zui_ui_t *ui)
{
    if (!ui->menu.open) return;
    ui->menu.open = false;
    zui_damage_add(&ui->damage, inset(ui->menu.r, -PX(ui, 4)));
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

bool zui_palette_matches(const char *q, const char *name)
{
    if (!q) return true;
    const char *p = name ? name : "";
    for (; *q; q++) {
        if (*q == ' ') continue;
        while (*p && lower(*p) != lower(*q)) p++;
        if (!*p) return false;
        p++;
    }
    return true;
}

static void palette_filter(zui_ui_t *ui)
{
    zui_palette_t *pl = &ui->palette;
    pl->n_match = 0;
    for (uint32_t i = 0; i < pl->n; i++)
        if (zui_palette_matches(pl->query, pl->item[i].name))
            pl->match[pl->n_match++] = (int32_t) i;
    pl->hot = pl->n_match ? 0 : -1;
    zui_damage_add(&ui->damage, inset(pl->r, -PX(ui, 4)));
}

void zui_palette_set(zui_ui_t *ui, zui_id_t owner, const zui_palette_item_t *items, uint32_t n)
{
    zui_palette_t *pl = &ui->palette;
    if (n > ZUI_PALETTE_MAX) n = ZUI_PALETTE_MAX;
    pl->owner = owner;
    pl->n = n;
    for (uint32_t i = 0; i < n; i++) pl->item[i] = items[i];
    if (pl->open) palette_filter(ui);
}

static void palette_rect(zui_ui_t *ui)
{
    int32_t W = (int32_t) ui->screen.px_w, H = (int32_t) ui->screen.px_h;
    int32_t w = imin(W - 2 * ui->plan.gutter_px, PX(ui, 560));
    int32_t rows = (int32_t) (ui->palette.n ? ui->palette.n : 1);
    int32_t h = PX(ui, 44) + rows * menu_row(ui) + PX(ui, 28) + 2 * PX(ui, ZUI_S2);
    h = imin(h, H - 2 * ui->plan.gutter_px);
    ui->palette.r = rect((W - w) / 2, imin(PX(ui, 96), H / 6), imax(0, w), imax(0, h));
}

void zui_palette_open(zui_ui_t *ui, const char *context)
{
    zui_palette_t *pl = &ui->palette;
    zui_menu_close(ui);
    pl->open = true;
    pl->query[0] = 0;
    mset(&pl->q, sizeof pl->q);
    pl->q.buf = pl->query;
    pl->q.cap = sizeof pl->query;
    pl->q.want_x = -1;
    scopy(pl->context, sizeof pl->context, context);
    palette_rect(ui);
    palette_filter(ui);
}

void zui_palette_close(zui_ui_t *ui)
{
    if (!ui->palette.open) return;
    ui->palette.open = false;
    zui_damage_add(&ui->damage, inset(ui->palette.r, -PX(ui, 4)));
}

static void palette_edit(zui_ui_t *ui, uint32_t a, uint32_t b, const char *s, uint32_t n)
{
    zui_field_t *f = &ui->palette.q;
    if (f->len - (b - a) + n + 1u > f->cap)
        n = zui_utf8_fit(s, n, f->cap - 1u - (f->len - (b - a)));
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '\n' || s[i] == '\r') return;
    mmove(f->buf + a + n, f->buf + b, f->len - b);
    for (uint32_t i = 0; i < n; i++) f->buf[a + i] = s[i];
    f->len = f->len - (b - a) + n;
    f->buf[f->len] = 0;
    f->caret = f->anchor = a + n;
    palette_filter(ui);
}

static bool palette_event(zui_ui_t *ui, const zui_event_t *e)
{
    zui_palette_t *pl = &ui->palette;
    zui_field_t *f = &pl->q;
    switch (e->type) {
    case ZUI_EV_TEXT:
        palette_edit(ui, f->caret, f->caret, e->text, slen(e->text));
        return true;
    case ZUI_EV_PREEDIT:
        return true;
    case ZUI_EV_KEY_DOWN:
        switch (e->key) {
        case ZUI_KEY_ESCAPE:
        case ZUI_KEY_BACK:
            zui_palette_close(ui);
            emit(ui, pl->owner, ZUI_ACT_DISMISS, 0);
            return true;
        case ZUI_KEY_UP:
            if (pl->n_match)
                pl->hot = (pl->hot + (int32_t) pl->n_match - 1) % (int32_t) pl->n_match;
            zui_damage_add(&ui->damage, pl->r);
            return true;
        case ZUI_KEY_DOWN:
        case ZUI_KEY_TAB:
            if (pl->n_match) pl->hot = (pl->hot + 1) % (int32_t) pl->n_match;
            zui_damage_add(&ui->damage, pl->r);
            return true;
        case ZUI_KEY_ENTER: {
            zui_id_t owner = pl->owner;
            if (pl->hot >= 0 && pl->n_match) {
                int32_t pick = pl->match[pl->hot];
                zui_palette_close(ui);
                emit(ui, owner, ZUI_ACT_PICK, pick);
            } else if (f->len) {
                zui_palette_close(ui);
                emit(ui, owner, ZUI_ACT_ASK, 0); /* the query is in ui->palette.query */
            }
            return true;
        }
        case ZUI_KEY_BACKSPACE:
            if (f->caret) palette_edit(ui, zui_utf8_prev(f->buf, f->caret), f->caret, "", 0);
            return true;
        case ZUI_KEY_LEFT:
            f->caret = f->anchor = zui_utf8_prev(f->buf, f->caret);
            break;
        case ZUI_KEY_RIGHT:
            f->caret = f->anchor = zui_utf8_next(f->buf, f->len, f->caret);
            break;
        default:
            if ((e->mods & ZUI_MOD_CMD) && e->key == 'k') {
                zui_palette_close(ui);
                emit(ui, pl->owner, ZUI_ACT_DISMISS, 0);
            }
            return true;
        }
        zui_damage_add(&ui->damage, pl->r);
        return true;
    case ZUI_EV_PTR_DOWN:
        if (!inside(pl->r, e->x, e->y)) {
            zui_palette_close(ui);
            emit(ui, pl->owner, ZUI_ACT_DISMISS, 0);
            return true;
        }
        {
            int32_t top = pl->r.y + PX(ui, 44) + PX(ui, ZUI_S2);
            int32_t i = (e->y - top) / menu_row(ui);
            if (e->y >= top && i >= 0 && i < (int32_t) pl->n_match) {
                zui_id_t owner = pl->owner;
                int32_t pick = pl->match[i];
                zui_palette_close(ui);
                emit(ui, owner, ZUI_ACT_PICK, pick);
            }
        }
        return true;
    case ZUI_EV_PTR_MOVE: {
        int32_t top = pl->r.y + PX(ui, 44) + PX(ui, ZUI_S2);
        int32_t i = (e->y - top) / menu_row(ui);
        if (inside(pl->r, e->x, e->y) && e->y >= top && i < (int32_t) pl->n_match && i != pl->hot) {
            pl->hot = i;
            zui_damage_add(&ui->damage, pl->r);
        }
        return true;
    }
    default:
        return e->type != ZUI_EV_TICK && e->type != ZUI_EV_RESIZE && e->type != ZUI_EV_SCALE;
    }
}

static bool menu_event(zui_ui_t *ui, const zui_event_t *e)
{
    zui_menu_t *m = &ui->menu;
    int32_t rh = menu_row(ui);
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
        switch (e->key) {
        case ZUI_KEY_ESCAPE:
        case ZUI_KEY_BACK:
            zui_menu_close(ui);
            emit(ui, m->owner, ZUI_ACT_DISMISS, 0);
            return true;
        case ZUI_KEY_UP:
        case ZUI_KEY_DOWN: {
            int32_t d = e->key == ZUI_KEY_UP ? -1 : 1, h = m->hot;
            for (uint32_t k = 0; k < m->n; k++) {
                h = (h + d + (int32_t) m->n) % (int32_t) m->n;
                if (h >= 0 && !m->off[h]) break;
            }
            if (h >= 0 && !m->off[h]) m->hot = h;
            zui_damage_add(&ui->damage, m->r);
            return true;
        }
        case ZUI_KEY_ENTER:
        case ZUI_KEY_SPACE:
            if (m->hot >= 0 && !m->off[m->hot]) {
                int32_t pick = m->hot;
                zui_menu_close(ui);
                emit(ui, m->owner, ZUI_ACT_PICK, pick);
            }
            return true;
        default:
            return true;
        }
    case ZUI_EV_PTR_MOVE:
        if (inside(m->r, e->x, e->y)) {
            int32_t i = (e->y - m->r.y - PX(ui, ZUI_S1)) / rh;
            if (i >= 0 && i < (int32_t) m->n && !m->off[i] && i != m->hot) {
                m->hot = i;
                zui_damage_add(&ui->damage, m->r);
            }
        }
        return true;
    case ZUI_EV_PTR_DOWN:
        return true; /* act on release, like a native menu */
    case ZUI_EV_PTR_UP:
        if (!inside(m->r, e->x, e->y)) {
            zui_menu_close(ui);
            emit(ui, m->owner, ZUI_ACT_DISMISS, 0);
            return true;
        } else {
            int32_t i = (e->y - m->r.y - PX(ui, ZUI_S1)) / rh;
            if (i >= 0 && i < (int32_t) m->n && !m->off[i]) {
                zui_menu_close(ui);
                emit(ui, m->owner, ZUI_ACT_PICK, i);
            }
        }
        return true;
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT:
    case ZUI_EV_SCROLL:
        return true;
    default:
        return false;
    }
}

/* ---- W11: scroll indicators --------------------------------------------------- */

static int32_t field_lines(const zui_ui_t *ui, const zui_widget_t *w);

/* How far a list, scroll area or text area can scroll, where it is, and
 * how much of it shows (all in the same unit). False when it all fits. */
static bool scroll_state(const zui_ui_t *ui, const zui_widget_t *w, int32_t *total, int32_t *at,
                         int32_t *view)
{
    if (w->kind == ZUI_W_LIST) {
        *view = list_visible(ui, w);
        *total = w->u.list.rows;
        *at = w->u.list.top;
    } else if (w->kind == ZUI_W_SCROLL) {
        *view = w->r.h;
        *total = w->u.scroll.content_h;
        *at = w->u.scroll.offset;
    } else if (w->kind == ZUI_W_FIELD && w->u.field.multiline) {
        *view = imax(1, w->r.h - 2 * PX(ui, ZUI_S1));
        *total = field_lines(ui, w) * line_h(face(ui, w->font));
        *at = w->u.field.scroll_y;
    } else {
        return false;
    }
    return *total > *view && w->r.h > PX(ui, 16);
}

zui_rect_t zui_scroll_track(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = cw(ui, id);
    int32_t total, at, view;
    if (!w || w->hidden || !scroll_state(ui, w, &total, &at, &view)) return rect(0, 0, 0, 0);
    int32_t m = PX(ui, 3);
    return rect(w->r.x + w->r.w - PX(ui, 8), w->r.y + m, PX(ui, 8), imax(0, w->r.h - 2 * m));
}

static zui_rect_t scroll_thumb(const zui_ui_t *ui, const zui_widget_t *w, zui_rect_t tr)
{
    int32_t total, at, view;
    scroll_state(ui, w, &total, &at, &view);
    int32_t th = imax(PX(ui, 16), (int32_t) (((int64_t) tr.h * view) / imax(1, total)));
    th = imin(th, tr.h);
    int32_t ty = tr.y + (int32_t) (((int64_t) (tr.h - th) * at) / imax(1, total - view));
    return rect(tr.x + tr.w - PX(ui, 5), ty, imax(2, PX(ui, 3)), th);
}

/* The colour a widget sits on (for its edge fades). */
static uint32_t ground_of(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    if (w->kind == ZUI_W_FIELD && !(w->parent != ZUI_NONE && ui->w[w->parent].kind == ZUI_W_HELD))
        return C(RAISED);
    for (zui_id_t p = w->parent; p != ZUI_NONE; p = ui->w[p].parent) {
        const zui_widget_t *pw = &ui->w[p];
        if ((pw->kind == ZUI_W_BOX && pw->panel) || pw->kind == ZUI_W_HELD ||
            pw->kind == ZUI_W_DIALOG)
            return C(PANEL);
    }
    return C(VOID);
}

/* W11: a track and a thumb at the right, and a fade where more is hidden. */
static void paint_scroll(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    zui_rect_t tr = zui_scroll_track(ui, id);
    if (!tr.w) return;
    int32_t total, at, view;
    scroll_state(ui, w, &total, &at, &view);
    uint32_t g = ground_of(ui, id);
    int32_t fh = imin(PX(ui, 16), w->r.h / 4), steps = 8;
    zui_rect_t keep = s->clip;
    zui_clip(s, w->r);
    for (int32_t i = 0; i < steps && fh > 0; i++) {
        int32_t y0 = i * fh / steps, y1 = (i + 1) * fh / steps;
        uint32_t a = (uint32_t) (200 - i * 200 / steps);
        if (at > 0) zui_fill_a(s, rect(w->r.x, w->r.y + y0, w->r.w, y1 - y0), g, a);
        if (at < total - view)
            zui_fill_a(s, rect(w->r.x, w->r.y + w->r.h - y1, w->r.w, y1 - y0), g, a);
    }
    zui_fill(s, rect(tr.x + tr.w - PX(ui, 4), tr.y, imax(1, PX(ui, 1)), tr.h), C(HAIRLINE));
    zui_rect_t th = scroll_thumb(ui, w, tr);
    zui_fill_rrect(s, th, th.w / 2, C(MUTED), 255);
    s->clip = keep;
}

/* ---- W10: tooltips ------------------------------------------------------------ */

static bool tip_due(const zui_ui_t *ui)
{
    const zui_widget_t *w = cw(ui, ui->tip_for);
    if (!w || !w->tip[0] || !shown(ui, ui->tip_for)) return false;
    if (ui->modal != ZUI_NONE && !within(ui, ui->tip_for, ui->modal)) return false;
    return ui->now_ms - ui->tip_since >= ZUI_TIP_MS;
}

static zui_rect_t tip_rect(const zui_ui_t *ui)
{
    const zui_widget_t *w = &ui->w[ui->tip_for];
    const zui_text_t *t = &ui->theme.label;
    int32_t pad = PX(ui, ZUI_S2), h = line_h(t) + 2 * PX(ui, ZUI_S1);
    int32_t W = (int32_t) ui->screen.px_w, H = (int32_t) ui->screen.px_h;
    int32_t tw = imin(text_w(t, w->tip) + 2 * pad, imax(0, W - 2 * PX(ui, ZUI_S2)));
    zui_rect_t r = seen(ui, ui->tip_for);
    int32_t x = imax(PX(ui, ZUI_S2), imin(r.x + (r.w - tw) / 2, W - tw - PX(ui, ZUI_S2)));
    int32_t y = r.y + r.h + PX(ui, 6);
    if (y + h > H) y = r.y - h - PX(ui, 6); /* no room below: above */
    return rect(x, imax(0, y), tw, h);
}

static void tip_hide(zui_ui_t *ui)
{
    if (ui->tip_r.w) zui_damage_add(&ui->damage, inset(ui->tip_r, -PX(ui, 2)));
    ui->tip_r = rect(0, 0, 0, 0);
    ui->tip_for = ZUI_NONE;
    ui->tip_kbd = false;
}

static void tip_arm(zui_ui_t *ui, zui_id_t id, bool kbd)
{
    if (id == ui->tip_for) return;
    tip_hide(ui);
    if (id == ZUI_NONE || !ui->w[id].tip[0]) return;
    ui->tip_for = id;
    ui->tip_since = ui->now_ms;
    ui->tip_kbd = kbd;
}

/* 0 before it shows; then up to 255 over ZUI_MOTION_MS, or 255 at once
 * under reduce motion. */
static uint32_t tip_alpha(const zui_ui_t *ui)
{
    if (!tip_due(ui)) return 0;
    uint32_t t = ui->now_ms - ui->tip_since - ZUI_TIP_MS;
    if (ui->reduce_motion || t >= ZUI_MOTION_MS) return 255;
    return 40u + t * 215u / ZUI_MOTION_MS;
}

const char *zui_tooltip_shown(const zui_ui_t *ui, zui_rect_t *r)
{
    if (!tip_alpha(ui)) {
        if (r) *r = rect(0, 0, 0, 0);
        return 0;
    }
    if (r) *r = tip_rect(ui);
    return ui->w[ui->tip_for].tip;
}

static void paint_tip(zui_ui_t *ui, zui_surface_t *s)
{
    uint32_t a = tip_alpha(ui);
    if (!a) return;
    const zui_text_t *t = &ui->theme.label;
    zui_rect_t r = tip_rect(ui);
    ui->tip_r = r;
    zui_fill_rrect(s, r, PX(ui, ZUI_R_CONTROL), C(RAISED), a);
    zui_stroke_rrect(s, r, PX(ui, ZUI_R_CONTROL), imax(1, PX(ui, 1)), C(MUTED), a);
    zui_text_a(s, t, r.x + PX(ui, ZUI_S2), cap_base(t, r.y, r.h), C(TEXT), a,
               ui->w[ui->tip_for].tip);
}

/* ---- events --------------------------------------------------------------- */

static zui_id_t scroller_of(const zui_ui_t *ui, zui_id_t id)
{
    for (; id != ZUI_NONE; id = ui->w[id].parent) {
        const zui_widget_t *w = &ui->w[id];
        if (w->kind == ZUI_W_SCROLL || w->kind == ZUI_W_LIST ||
            (w->kind == ZUI_W_FIELD && w->u.field.multiline))
            return id;
    }
    return ZUI_NONE;
}

static bool scroll_by(zui_ui_t *ui, zui_id_t id, int32_t dy)
{
    zui_widget_t *w = &ui->w[id];
    if (w->kind == ZUI_W_LIST) {
        int32_t rh = row_px(ui, w), rows = dy / rh;
        if (!rows) rows = dy > 0 ? 1 : dy < 0 ? -1 : 0;
        int32_t vis = list_visible(ui, w);
        int32_t top = imax(0, imin(w->u.list.top + rows, imax(0, w->u.list.rows - vis)));
        if (top == w->u.list.top) return false;
        w->u.list.top = top;
    } else if (w->kind == ZUI_W_SCROLL) {
        int32_t maxo = imax(0, w->u.scroll.content_h - w->r.h);
        int32_t o = imax(0, imin(w->u.scroll.offset + dy, maxo));
        if (o == w->u.scroll.offset) return false;
        w->u.scroll.offset = o;
        layout(ui, id, w->r);
    } else {
        zui_field_t *f = &w->u.field;
        int32_t lh = line_h(face(ui, w->font));
        int32_t maxo = imax(0, field_lines(ui, w) * lh - (w->r.h - 2 * PX(ui, ZUI_S1)));
        int32_t o = imax(0, imin(f->scroll_y + dy, maxo));
        if (o == f->scroll_y) return false;
        f->scroll_y = o;
    }
    zui_invalidate(ui, id);
    return true;
}

static void field_click(zui_ui_t *ui, zui_id_t id, int32_t x, int32_t y, bool extend)
{
    zui_widget_t *w = &ui->w[id];
    zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t lh = line_h(t);
    int32_t tx = x - (w->r.x + PX(ui, ZUI_S2)) + f->scroll_x;
    int32_t line = f->multiline ? (y - (w->r.y + PX(ui, ZUI_S1)) + f->scroll_y) / imax(1, lh) : 0;
    if (line < 0) line = 0;
    uint32_t p = pos_at(ui, w, line, tx);
    f->caret = p;
    if (!extend) f->anchor = p;
    f->want_x = -1;
    zui_invalidate(ui, id);
}

static bool list_key(zui_ui_t *ui, zui_id_t id, const zui_event_t *e)
{
    zui_widget_t *w = &ui->w[id];
    zui_list_t *l = &w->u.list;
    int32_t s = l->sel, vis = list_visible(ui, w);
    switch (e->key) {
    case ZUI_KEY_UP:
        s = s < 0 ? l->rows - 1 : s - 1;
        break;
    case ZUI_KEY_DOWN:
        s = s < 0 ? 0 : s + 1;
        break;
    case ZUI_KEY_PAGE_UP:
        s = s < 0 ? 0 : s - vis;
        break;
    case ZUI_KEY_PAGE_DOWN:
        s = s < 0 ? 0 : s + vis;
        break;
    case ZUI_KEY_HOME:
        s = 0;
        break;
    case ZUI_KEY_END:
        s = l->rows - 1;
        break;
    case ZUI_KEY_ENTER:
        if (l->sel >= 0) emit(ui, id, ZUI_ACT_OPEN, l->sel);
        return true;
    default:
        return false;
    }
    if (!l->rows) return true;
    s = imax(0, imin(s, l->rows - 1));
    if (s == l->sel) return true;
    zui_list_select(ui, id, s);
    emit(ui, id, ZUI_ACT_SELECT, s);
    return true;
}

static bool key_event(zui_ui_t *ui, const zui_event_t *e)
{
    zui_id_t f = ui->focus;
    zui_widget_t *w = cw(ui, f) ? &ui->w[f] : 0;
    bool cmd = (e->mods & ZUI_MOD_CMD) != 0;
    /* W9: Escape (or Back) closes the open dialog, wherever focus is in it */
    if (ui->modal != ZUI_NONE && (e->key == ZUI_KEY_ESCAPE || e->key == ZUI_KEY_BACK)) {
        zui_id_t d = ui->modal;
        zui_dialog_close(ui, d);
        emit(ui, d, ZUI_ACT_DISMISS, 0);
        return true;
    }
    /* W5: the palette, from anywhere (not over a dialog) */
    if (cmd && e->key == 'k' && ui->modal == ZUI_NONE) {
        zui_palette_open(ui, "");
        return true;
    }
    /* W4: Cmd-Enter accepts the held answer nearest the focus, else the only one */
    if (cmd && e->key == ZUI_KEY_ENTER) {
        zui_id_t held = ZUI_NONE;
        for (zui_id_t p = f; p != ZUI_NONE && held == ZUI_NONE; p = ui->w[p].parent)
            if (ui->w[p].kind == ZUI_W_HELD) held = p;
        if (held == ZUI_NONE)
            for (uint32_t i = 0; i < ui->n; i++)
                if (ui->w[i].kind == ZUI_W_HELD && shown(ui, (zui_id_t) i) &&
                    ui->w[i].u.held.state == 1)
                    held = (zui_id_t) i;
        if (held != ZUI_NONE && ui->w[held].u.held.state == 1) {
            emit(ui, held, ZUI_ACT_ACCEPT, 0);
            return true;
        }
    }
    if (w && w->kind == ZUI_W_FIELD && field_key(ui, f, e)) return true;
    if (w && w->kind == ZUI_W_LIST && list_key(ui, f, e)) return true;
    if (w && !w->disabled && choice_key(ui, f, e)) return true;
    switch (e->key) {
    case ZUI_KEY_TAB:
        zui_focus_next(ui, (e->mods & ZUI_MOD_SHIFT) != 0);
        return true;
    case ZUI_KEY_LEFT:
    case ZUI_KEY_RIGHT:
    case ZUI_KEY_UP:
    case ZUI_KEY_DOWN:
        zui_focus_toward(ui, e->key);
        return true;
    case ZUI_KEY_ENTER:
    case ZUI_KEY_SPACE:
        if (w && w->kind == ZUI_W_BUTTON) {
            press(ui, f);
            return true;
        }
        return false;
    case ZUI_KEY_MENU:
        if (w) {
            emit(ui, f, ZUI_ACT_CONTEXT, w->kind == ZUI_W_LIST ? w->u.list.sel : 0);
            return true;
        }
        return false;
    default:
        break;
    }
    /* a button's shortcut: "⌘S" style labels are not parsed; views map keys */
    return false;
}

bool zui_ui_event(zui_ui_t *ui, const zui_event_t *e)
{
    ui->now_ms = e->ms ? e->ms : ui->now_ms;
    if (e->type == ZUI_EV_RESIZE || e->type == ZUI_EV_SCALE) {
        if (e->type == ZUI_EV_RESIZE) {
            ui->screen.px_w = (uint32_t) imax(0, e->x);
            ui->screen.px_h = (uint32_t) imax(0, e->y);
        } else if (e->x > 0) {
            ui->screen.scale_q8 = (uint32_t) e->x;
        }
        zui_plan(&ui->screen, &ui->plan);
        ui->layout_dirty = true;
        if (ui->palette.open) palette_rect(ui);
        zui_menu_close(ui);
        return true;
    }
    if (e->type == ZUI_EV_TOUCH_BEGIN && ui->screen.input == ZUI_INPUT_POINTER) {
        ui->screen.input = ZUI_INPUT_TOUCH; /* touch targets grow on first touch */
        zui_plan(&ui->screen, &ui->plan);
        ui->layout_dirty = true;
    }
    do_layout(ui);
    /* W10: any press, key or scroll takes the tooltip down */
    if (e->type == ZUI_EV_KEY_DOWN || e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_SCROLL ||
        e->type == ZUI_EV_TOUCH_BEGIN || e->type == ZUI_EV_TEXT || e->type == ZUI_EV_FOCUS_OUT)
        tip_hide(ui);
    zui_gesture_t g = zui_gesture_feed(&ui->gesture, e);
    if (ui->palette.open) return palette_event(ui, e);
    if (ui->menu.open && menu_event(ui, e)) return true;
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
        return key_event(ui, e);
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT: {
        zui_widget_t *w = zui_w(ui, ui->focus);
        if (!w || w->kind != ZUI_W_FIELD || w->u.field.readonly) return false;
        zui_field_t *f = &w->u.field;
        if (e->type == ZUI_EV_PREEDIT) {
            scopy(f->preedit, sizeof f->preedit, e->text);
            zui_invalidate(ui, ui->focus);
            return true;
        }
        f->preedit[0] = 0;
        uint32_t s0, s1;
        zui_field_selection(ui, ui->focus, &s0, &s1);
        field_replace(ui, ui->focus, s0, s1, e->text, slen(e->text));
        return true;
    }
    case ZUI_EV_PTR_DOWN: {
        zui_id_t h = zui_hit(ui, e->x, e->y);
        if (h == ZUI_NONE) return false;
        if (h == ui->modal && !inside(ui->w[h].r, e->x, e->y)) return true; /* W9: the scrim */
        {
            /* W11: a press on a scroll track pages toward it */
            zui_id_t sc = scroller_of(ui, h);
            zui_rect_t tr = sc != ZUI_NONE ? zui_scroll_track(ui, sc) : rect(0, 0, 0, 0);
            if (tr.w && inside(tr, e->x, e->y) && e->button != ZUI_BTN_RIGHT) {
                zui_rect_t th = scroll_thumb(ui, &ui->w[sc], tr);
                int32_t page = ui->w[sc].kind == ZUI_W_LIST
                                   ? (list_visible(ui, &ui->w[sc]) - 1) * row_px(ui, &ui->w[sc])
                                   : ui->w[sc].r.h * 7 / 8;
                if (e->y < th.y) scroll_by(ui, sc, -imax(1, page));
                if (e->y >= th.y + th.h) scroll_by(ui, sc, imax(1, page));
                return true;
            }
        }
        if (e->button == ZUI_BTN_RIGHT) {
            int32_t row = ui->w[h].kind == ZUI_W_LIST ? zui_list_row_at(ui, h, e->x, e->y) : 0;
            if (ui->w[h].kind == ZUI_W_LIST && row >= 0 && row != ui->w[h].u.list.sel) {
                zui_list_select(ui, h, row);
                emit(ui, h, ZUI_ACT_SELECT, row);
            }
            emit(ui, h, ZUI_ACT_CONTEXT, row);
            return true;
        }
        zui_widget_t *w = &ui->w[h];
        if (w->disabled) return true;
        ui->pressed = h;
        if (can_focus(ui, h)) zui_focus(ui, h, false);
        if (w->kind == ZUI_W_BUTTON || is_choice(w->kind)) zui_invalidate(ui, h);
        if (w->kind == ZUI_W_SLIDER) {
            slider_at(ui, h, e->x);
            ui->drag_list = h;
        }
        if (w->kind == ZUI_W_FIELD) {
            field_click(ui, h, e->x, e->y, (e->mods & ZUI_MOD_SHIFT) != 0);
            ui->drag_list = h;
        }
        if (w->kind == ZUI_W_LIST) {
            int32_t row = zui_list_row_at(ui, h, e->x, e->y);
            if (row >= 0 && row != w->u.list.sel) {
                zui_list_select(ui, h, row);
                emit(ui, h, ZUI_ACT_SELECT, row);
            }
        }
        return true;
    }
    case ZUI_EV_PTR_MOVE: {
        zui_id_t h = zui_hit(ui, e->x, e->y);
        if (h != ui->hover) {
            if (ui->hover != ZUI_NONE &&
                (ui->w[ui->hover].kind == ZUI_W_BUTTON || is_choice(ui->w[ui->hover].kind)))
                zui_invalidate(ui, ui->hover);
            if (h != ZUI_NONE && (ui->w[h].kind == ZUI_W_BUTTON || is_choice(ui->w[h].kind)))
                zui_invalidate(ui, h);
            ui->hover = h;
            if (h != ZUI_NONE && ui->w[h].tip[0])
                tip_arm(ui, h, false); /* W10: rests from now */
            else if (!ui->tip_kbd)
                tip_hide(ui);
        }
        if (ui->drag_list != ZUI_NONE && (e->button & ZUI_BTN_LEFT) &&
            ui->w[ui->drag_list].kind == ZUI_W_FIELD) {
            field_click(ui, ui->drag_list, e->x, e->y, true);
            return true;
        }
        if (ui->drag_list != ZUI_NONE && (e->button & ZUI_BTN_LEFT) &&
            ui->w[ui->drag_list].kind == ZUI_W_SLIDER) {
            slider_at(ui, ui->drag_list, e->x);
            return true;
        }
        if (g == ZUI_GESTURE_DRAG && ui->pressed != ZUI_NONE &&
            ui->screen.input != ZUI_INPUT_POINTER) {
            zui_id_t s = scroller_of(ui, ui->pressed);
            if (s != ZUI_NONE) ui->pressed = ZUI_NONE; /* a drag cancels the press */
        }
        if (g == ZUI_GESTURE_LONG_PRESS) goto long_press;
        return false;
    }
    case ZUI_EV_TICK:
        if (tip_alpha(ui)) { /* W10: it shows, or fades in */
            zui_rect_t tr = tip_rect(ui);
            if (tr.x != ui->tip_r.x || tr.y != ui->tip_r.y || tr.w != ui->tip_r.w ||
                ui->now_ms - ui->tip_since < ZUI_TIP_MS + ZUI_MOTION_MS + 40u)
                zui_damage_add(&ui->damage, inset(tr, -PX(ui, 2)));
        }
        if (g == ZUI_GESTURE_LONG_PRESS) goto long_press;
        return false;
    case ZUI_EV_PTR_UP: {
        zui_id_t was = ui->pressed;
        ui->pressed = ZUI_NONE;
        ui->drag_list = ZUI_NONE;
        if (g == ZUI_GESTURE_LONG_PRESS) goto long_press;
        if (was == ZUI_NONE) return false;
        zui_widget_t *w = &ui->w[was];
        if (w->kind == ZUI_W_BUTTON || is_choice(w->kind)) {
            zui_invalidate(ui, was);
            if (inside(w->r, e->x, e->y)) activate(ui, was);
            return true;
        }
        if (w->kind == ZUI_W_LIST && g == ZUI_GESTURE_DOUBLE_TAP) {
            int32_t row = zui_list_row_at(ui, was, e->x, e->y);
            if (row >= 0) emit(ui, was, ZUI_ACT_OPEN, row);
            return true;
        }
        if (w->kind == ZUI_W_LIST && g == ZUI_GESTURE_TAP &&
            ui->screen.input != ZUI_INPUT_POINTER) {
            /* on touch one tap opens, as phones do */
            int32_t row = zui_list_row_at(ui, was, e->x, e->y);
            if (row >= 0) emit(ui, was, ZUI_ACT_OPEN, row);
            return true;
        }
        return true;
    }
    case ZUI_EV_SCROLL: {
        zui_id_t h = zui_hit(ui, e->x, e->y);
        if (h != ZUI_NONE && ui->w[h].kind == ZUI_W_SLIDER && !ui->w[h].disabled && e->dy) {
            /* W8: the wheel steps a slider; up is more */
            int32_t st = imax(1, ui->w[h].u.range.step);
            slider_to(ui, h, ui->w[h].u.range.value + (e->dy < 0 ? st : -st));
            return true;
        }
        zui_id_t s = scroller_of(ui, h);
        return s != ZUI_NONE && scroll_by(ui, s, e->dy);
    }
    case ZUI_EV_FOCUS_OUT:
        ui->pressed = ZUI_NONE;
        ui->drag_list = ZUI_NONE;
        return false;
    default:
        return false;
    }
long_press: {
    /* I3: a long press is the touch right click; on empty ground it is Ask */
    zui_id_t h = zui_hit(ui, ui->gesture.x0, ui->gesture.y0);
    ui->pressed = ZUI_NONE;
    if (ui->modal != ZUI_NONE && (h == ZUI_NONE || h == ui->modal)) return true;
    if (h == ZUI_NONE || h == ui->root || ui->w[h].kind == ZUI_W_BOX ||
        ui->w[h].kind == ZUI_W_LABEL) {
        zui_palette_open(ui, "");
        return true;
    }
    int32_t row =
        ui->w[h].kind == ZUI_W_LIST ? zui_list_row_at(ui, h, ui->gesture.x0, ui->gesture.y0) : 0;
    emit(ui, h, ZUI_ACT_CONTEXT, row);
    return true;
}
}

/* ---- painting ---------------------------------------------------------------- */

/* W3: two tones, so the ring shows on a gold button too: a ground-coloured
 * gap hugging the control, the gold ring outside it. */
static void paint_focus(zui_ui_t *ui, zui_surface_t *s, zui_rect_t r)
{
    int32_t d = imax(1, PX(ui, 2)), rad = PX(ui, ZUI_R_CONTROL);
    zui_stroke_rrect(s, inset(r, -d), rad + d, d, C(VOID), 255);
    zui_stroke_rrect(s, inset(r, -2 * d), rad + 2 * d, d, C(GOLD), 255);
}

/* A thick segment from (x0, y0) to (x1, y1), stamped (icons, check marks). */
static void seg(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t th,
                uint32_t rgb, uint32_t a)
{
    int32_t n = imax(iabs(x1 - x0), iabs(y1 - y0));
    for (int32_t i = 0; i <= n; i++) {
        int32_t x = x0 + (n ? (x1 - x0) * i / n : 0), y = y0 + (n ? (y1 - y0) * i / n : 0);
        zui_fill_a(s, rect(x - th / 2, y - th / 2, th, th), rgb, a);
    }
}

/* W12: an icon's strokes in one colour; u is the icon's unit (1/12 of it). */
static void icon_strokes(zui_ui_t *ui, zui_surface_t *s, zui_icon_t icon, zui_rect_t b, int32_t dx,
                         int32_t dy, uint32_t rgb, uint32_t a)
{
    int32_t u = imax(1, b.w / 12), th = imax(1, PX(ui, 2)), x = b.x + dx, y = b.y + dy;
    switch (icon) {
    case ZUI_ICON_PICTURE:
        zui_stroke_rrect(s, rect(x + u, y + 2 * u, 10 * u, 8 * u), u, th, rgb, a);
        seg(s, x + 2 * u, y + 9 * u, x + 5 * u, y + 5 * u, th, rgb, a);
        seg(s, x + 5 * u, y + 5 * u, x + 7 * u, y + 8 * u, th, rgb, a);
        seg(s, x + 7 * u, y + 8 * u, x + 8 * u, y + 7 * u, th, rgb, a);
        seg(s, x + 8 * u, y + 7 * u, x + 10 * u, y + 9 * u, th, rgb, a);
        zui_stroke_rrect(s, rect(x + 8 * u, y + 3 * u + u / 2, 2 * u, 2 * u), u, th, rgb, a);
        break;
    case ZUI_ICON_CONSOLE:
        zui_stroke_rrect(s, rect(x + u, y + 2 * u, 10 * u, 8 * u), u, th, rgb, a);
        seg(s, x + 3 * u, y + 4 * u, x + 5 * u, y + 6 * u, th, rgb, a);
        seg(s, x + 5 * u, y + 6 * u, x + 3 * u, y + 8 * u, th, rgb, a);
        zui_fill_a(s, rect(x + 6 * u, y + 8 * u - th / 2, 3 * u, th), rgb, a);
        break;
    case ZUI_ICON_BELL:
        zui_stroke_rrect(s, rect(x + 3 * u, y + 2 * u, 6 * u, 8 * u), 3 * u, th, rgb, a);
        zui_fill_a(s, rect(x + 2 * u, y + 8 * u, 8 * u, th), rgb, a);
        zui_fill_rrect(s, rect(x + 5 * u, y + 10 * u, 2 * u, u + u / 2), u, rgb, a);
        break;
    case ZUI_ICON_SEARCH:
        zui_stroke_rrect(s, rect(x + 2 * u, y + 2 * u, 6 * u, 6 * u), 3 * u, th, rgb, a);
        seg(s, x + 7 * u, y + 7 * u, x + 10 * u, y + 10 * u, th + th / 2, rgb, a);
        break;
    default: /* a folder */
        zui_stroke_rrect(s, rect(x + u, y + 4 * u, 10 * u, 6 * u), u, th, rgb, a);
        seg(s, x + u, y + 4 * u, x + u, y + 3 * u, th, rgb, a);
        seg(s, x + u, y + 3 * u, x + 4 * u, y + 3 * u, th, rgb, a);
        seg(s, x + 4 * u, y + 3 * u, x + 5 * u, y + 4 * u, th, rgb, a);
        break;
    }
}

/* Engraved: the shade above, the light lip below, then the line itself. */
static void paint_icon(zui_ui_t *ui, zui_surface_t *s, zui_icon_t icon, zui_rect_t b)
{
    icon_strokes(ui, s, icon, b, 0, -1, C(ENGRAVE_LO), 255);
    icon_strokes(ui, s, icon, b, 0, 1, C(ENGRAVE_HI), 40);
    icon_strokes(ui, s, icon, b, 0, 0, C(MUTED), 255);
}

static void paint_button(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    const zui_text_t *t = face(ui, w->font);
    zui_rect_t r = w->r;
    int32_t rad = PX(ui, ZUI_R_CONTROL);
    bool hot = ui->hover == id, down = ui->pressed == id && hot;
    uint32_t fg = C(TEXT);
    switch (w->style) {
    case ZUI_BTN_PRIMARY:
        zui_fill_rrect(s, r, rad, down ? C(GOLD) : hot ? C(GOLD_HI) : C(GOLD), 255);
        fg = C(VOID);
        break;
    case ZUI_BTN_DANGER:
        zui_fill_rrect(s, r, rad, down ? C(RAISED) : C(PANEL), 255);
        zui_stroke_rrect(s, r, rad, imax(1, PX(ui, 1)), C(ALERT), 255);
        fg = C(ALERT);
        break;
    case ZUI_BTN_GHOST:
        if (hot || down) zui_fill_rrect(s, r, rad, C(RAISED), down ? 255 : 160);
        fg = C(TEXT_2);
        break;
    default:
        zui_fill_rrect(s, r, rad, down ? C(HAIRLINE) : hot ? C(RAISED) : C(PANEL), 255);
        zui_engrave(s, r, rad, C(ENGRAVE_LO), C(ENGRAVE_HI), 40);
        break;
    }
    if (w->disabled) fg = C(FAINT);
    int32_t tw = text_w(t, w->label);
    int32_t kw = w->shortcut[0] ? text_w(&ui->theme.label, w->shortcut) + PX(ui, ZUI_S2) : 0;
    int32_t x = r.x + imax(0, (r.w - tw - kw) / 2);
    int32_t base = cap_base(t, r.y, r.h); /* capitals centred, not the ascent */
    zui_text_fit(s, t, x, base, r.w - 2 * PX(ui, ZUI_S2), fg, w->label);
    if (kw)
        zui_text(s, &ui->theme.label, x + tw + PX(ui, ZUI_S2), base,
                 w->style == ZUI_BTN_PRIMARY ? C(HAIRLINE) : C(MUTED), w->shortcut);
}

static void paint_list(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    zui_list_t *l = &w->u.list;
    const zui_text_t *t = face(ui, w->font);
    const zui_text_t *tl = &ui->theme.label;
    int32_t rh = row_px(ui, w), pad = PX(ui, ZUI_S3);
    /* the lead-mark column is kept on every row once one visible row has a
     * mark, so the text of every row starts at the same x */
    bool leads = false;
    for (int32_t i = 0; i < list_visible(ui, w) + 1 && !leads && l->row; i++) {
        if (l->top + i >= l->rows) break;
        zui_row_t r;
        mset(&r, sizeof r);
        l->row(ui, id, l->top + i, &r, l->row_ctx);
        leads = r.lead_rgb != 0;
    }
    zui_clip(s, w->r);
    for (int32_t i = 0; i < list_visible(ui, w) + 1; i++) {
        int32_t row = l->top + i;
        if (row >= l->rows) break;
        zui_rect_t rr = rect(w->r.x, w->r.y + i * rh, w->r.w, rh);
        bool sel = row == l->sel;
        if (sel) zui_fill_rrect(s, inset(rr, PX(ui, 1)), PX(ui, ZUI_R_CONTROL), C(RAISED), 255);
        zui_row_t r;
        mset(&r, sizeof r);
        if (l->row) l->row(ui, id, row, &r, l->row_ctx);
        int32_t x = rr.x + pad;
        int32_t base = cap_base(t, rr.y, rh);
        if (leads) {
            int32_t d = PX(ui, 8);
            if (r.lead_rgb)
                zui_fill_rrect(s, rect(x, rr.y + (rh - d) / 2, d, d), d / 2,
                               zui_color_remap(ui->colors, r.lead_rgb), 255);
            x += d + PX(ui, ZUI_S2);
        }
        /* the detail never takes more than two fifths of the row (phones) */
        int32_t dt = r.detail[0] ? imin(text_w(tl, r.detail), rr.w * 2 / 5) : 0;
        int32_t dw = dt ? dt + PX(ui, ZUI_S3) : 0;
        uint32_t fg = r.muted ? C(FAINT) : sel ? C(TEXT) : C(TEXT_2);
        zui_text_fit(s, t, x, base, rr.x + rr.w - pad - dw - x, fg, r.text);
        if (dw) zui_text_fit(s, tl, rr.x + rr.w - pad - dt, base, dt, C(MUTED), r.detail);
        if (sel && ui->focus == id && ui->focus_visible)
            zui_fill(s, rect(rr.x, rr.y + PX(ui, 4), imax(1, PX(ui, 2)), rh - PX(ui, 8)), C(GOLD));
    }
    if (!l->rows && w->label[0])
        zui_text(s, t, w->r.x + pad, w->r.y + pad + ascent(t), C(MUTED), w->label);
    zui_unclip(s);
    paint_scroll(ui, s, id); /* W11 */
}

static void paint_field(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    bool in_held = w->parent != ZUI_NONE && ui->w[w->parent].kind == ZUI_W_HELD;
    int32_t rad = PX(ui, ZUI_R_CONTROL);
    if (!in_held) {
        zui_fill_rrect(s, w->r, rad, C(RAISED), 255);
        zui_engrave(s, w->r, rad, C(ENGRAVE_LO), C(ENGRAVE_HI), 50);
    }
    zui_rect_t in =
        rect(w->r.x + PX(ui, ZUI_S2), w->r.y + PX(ui, ZUI_S1), imax(0, w->r.w - 2 * PX(ui, ZUI_S2)),
             imax(0, w->r.h - 2 * PX(ui, ZUI_S1)));
    zui_clip(s, in);
    int32_t lh = line_h(t), asc = ascent(t);
    int32_t y0 = f->multiline ? in.y - f->scroll_y : in.y + (in.h - lh) / 2;
    uint32_t s0, s1;
    zui_field_selection(ui, id, &s0, &s1);
    bool focused = ui->focus == id && !f->readonly;
    if (!f->len && f->placeholder[0] && !f->preedit[0]) {
        zui_text(s, t, in.x, y0 + asc + (lh - asc) / 4, C(FAINT), f->placeholder);
    } else if (f->buf) {
        uint32_t at = 0;
        int32_t li = 0;
        int32_t width = field_text_w(ui, w);
        for (;;) {
            line_t L = line_at(t, f, at, width);
            int32_t ly = y0 + li * lh;
            if (ly > in.y + in.h) break;
            int32_t off;
            if (ly + lh >= in.y && vline(ui, w, L, &off)) {
                /* W14: each character at its place in visual order */
                int32_t x = (in.x - f->scroll_x) * 256 + off, base = ly + asc + (lh - asc) / 4;
                uint32_t ink = w->color ? zui_color_remap(ui->colors, w->color) : C(TEXT);
                for (uint32_t i = 0; i < VLINE.n; i++) {
                    int32_t xa = (x + VLINE.c[i].x0) >> 8, xb = (x + VLINE.c[i].x1) >> 8;
                    if (s1 > s0 && VLINE.c[i].a >= s0 && VLINE.c[i].b <= s1)
                        zui_fill_a(s, rect(xa, ly, imax(1, xb - xa), lh), C(GOLD), 70);
                    char g[8];
                    uint32_t cp = VLINE.c[i].cp, gn = 0;
                    if (cp == '\t' || cp < 0x20u) continue;
                    if (cp < 0x80u) {
                        g[gn++] = (char) cp;
                    } else if (cp < 0x800u) {
                        g[gn++] = (char) (0xC0u | (cp >> 6));
                        g[gn++] = (char) (0x80u | (cp & 0x3Fu));
                    } else if (cp < 0x10000u) {
                        g[gn++] = (char) (0xE0u | (cp >> 12));
                        g[gn++] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
                        g[gn++] = (char) (0x80u | (cp & 0x3Fu));
                    } else {
                        g[gn++] = (char) (0xF0u | (cp >> 18));
                        g[gn++] = (char) (0x80u | ((cp >> 12) & 0x3Fu));
                        g[gn++] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
                        g[gn++] = (char) (0x80u | (cp & 0x3Fu));
                    }
                    g[gn] = 0;
                    if (t->font && t->cache) zui_text(s, t, xa, base, ink, g);
                }
            } else if (ly + lh >= in.y) {
                int32_t x = in.x - f->scroll_x, base = ly + asc + (lh - asc) / 4;
                /* selection behind the text */
                if (s1 > s0 && s1 > L.a && s0 <= L.b) {
                    uint32_t a = s0 > L.a ? s0 : L.a, b = s1 < L.b ? s1 : L.b;
                    int32_t xa = x + ((span_q8(t, f->buf, L.a, a) + 128) >> 8);
                    int32_t xb = x + ((span_q8(t, f->buf, L.a, b) + 128) >> 8);
                    if (s1 > L.b && L.next <= f->len) xb += PX(ui, 4); /* the newline */
                    zui_fill_a(s, rect(xa, ly, imax(1, xb - xa), lh), C(GOLD), 70);
                }
                if (f->secret) {
                    int32_t d = imax(2, fsize(t) / 3), n = 0;
                    for (uint32_t i = L.a; i < L.b; i = zui_utf8_next(f->buf, f->len, i)) n++;
                    for (int32_t i = 0; i < n; i++)
                        zui_fill_rrect(s, rect(x + i * (d + d), base - d - d / 2, d, d), d / 2,
                                       C(TEXT), 255);
                } else {
                    draw_span(s, t, x, base,
                              w->color ? zui_color_remap(ui->colors, w->color) : C(TEXT), f->buf,
                              L.a, L.b);
                }
            }
            if (L.next > f->len) break;
            at = L.next;
            li++;
        }
    }
    if (focused) {
        int32_t line, x;
        caret_xy(ui, w, f->caret, &line, &x);
        int32_t cx = in.x - f->scroll_x + x, cy = y0 + line * lh;
        if (f->preedit[0]) { /* I4: the composition, underlined, at the caret */
            int32_t pw = text_w(t, f->preedit);
            zui_fill(s, rect(cx, cy, pw, lh), C(PANEL));
            draw_span(s, t, cx, cy + asc + (lh - asc) / 4, C(GOLD_GLOW), f->preedit, 0,
                      slen(f->preedit));
            zui_fill(s, rect(cx, cy + lh - imax(1, PX(ui, 1)), pw, imax(1, PX(ui, 1))), C(GOLD));
            cx += pw;
        }
        bool on = ui->reduce_motion || ((ui->now_ms - ui->caret_ms) / 530u) % 2u == 0;
        if (on) zui_fill(s, rect(cx, cy + PX(ui, 1), imax(1, PX(ui, 2)), lh - PX(ui, 2)), C(GOLD));
    }
    zui_unclip(s);
    if (f->multiline && !in_held) paint_scroll(ui, s, id); /* W11 */
}

void zui_field_caret_rect(const zui_ui_t *ui, zui_id_t id, zui_rect_t *out)
{
    const zui_widget_t *w = cw(ui, id);
    *out = rect(0, 0, 0, 0);
    if (!w || w->kind != ZUI_W_FIELD) return;
    const zui_field_t *f = &w->u.field;
    const zui_text_t *t = face(ui, w->font);
    int32_t lh = line_h(t), line, x;
    int32_t inx = w->r.x + PX(ui, ZUI_S2), iny = w->r.y + PX(ui, ZUI_S1);
    int32_t inh = imax(0, w->r.h - 2 * PX(ui, ZUI_S1));
    int32_t y0 = f->multiline ? iny - f->scroll_y : iny + (inh - lh) / 2;
    caret_xy(ui, w, f->caret, &line, &x);
    *out =
        rect(inx - f->scroll_x + x, y0 + line * lh + PX(ui, 1), imax(1, PX(ui, 2)), lh - PX(ui, 2));
}

static void paint_held(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    int32_t rad = PX(ui, ZUI_R_PANEL);
    /* S0: at the glass, a dashed gold edge, the panel step behind it */
    zui_fill_rrect(s, w->r, rad, C(PANEL), 255);
    if (w->u.held.state == 1)
        zui_dash_rrect(s, w->r, rad, imax(1, PX(ui, 2)), PX(ui, 6), C(GOLD));
    else
        zui_stroke_rrect(s, w->r, rad, imax(1, PX(ui, 1)), w->u.held.state == 2 ? C(OK) : C(FAINT),
                         255);
}

/* W8: a toggle, check box or radio: the control at the left, its label. */
static void paint_choice(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    const zui_text_t *t = face(ui, w->font);
    zui_rect_t r = w->r;
    bool on = w->u.check.state != 0, hot = ui->hover == id, off = w->disabled;
    int32_t cy = r.y + r.h / 2, x = r.x, lw = imax(1, PX(ui, 2));
    uint32_t edge = off ? C(FAINT) : C(MUTED), fill = off ? C(FAINT) : C(TEXT_2);
    if (w->kind == ZUI_W_TOGGLE) {
        int32_t tw = PX(ui, 36), th = PX(ui, 20), k = th - 2 * PX(ui, 3);
        zui_rect_t tr = rect(x, cy - th / 2, tw, th);
        if (on) {
            zui_fill_rrect(s, tr, th / 2, fill, 255);
        } else {
            zui_fill_rrect(s, tr, th / 2, hot ? C(RAISED) : C(PANEL), 255);
            zui_stroke_rrect(s, tr, th / 2, imax(1, PX(ui, 1)), edge, 255);
        }
        int32_t kx = on ? tr.x + tw - PX(ui, 3) - k : tr.x + PX(ui, 3);
        zui_fill_rrect(s, rect(kx, cy - k / 2, k, k), k / 2, on ? C(VOID) : edge, 255);
        x += tw + PX(ui, ZUI_S2);
    } else {
        int32_t b = PX(ui, 18);
        zui_rect_t br = rect(x, cy - b / 2, b, b);
        int32_t rad = w->kind == ZUI_W_RADIO ? b / 2 : PX(ui, 4);
        if (on && w->kind == ZUI_W_CHECK) {
            zui_fill_rrect(s, br, rad, fill, 255);
            if (w->u.check.state == 2) {
                zui_fill(s, rect(br.x + b / 4, cy - lw / 2, b / 2, lw), C(VOID));
            } else {
                seg(s, br.x + b / 4, cy, br.x + b * 5 / 12, cy + b / 5, lw, C(VOID), 255);
                seg(s, br.x + b * 5 / 12, cy + b / 5, br.x + b * 3 / 4, cy - b / 4, lw, C(VOID),
                    255);
            }
        } else {
            zui_fill_rrect(s, br, rad, hot ? C(RAISED) : C(PANEL), 255);
            zui_stroke_rrect(s, br, rad, imax(1, PX(ui, on ? 2 : 1)), on ? fill : edge, 255);
            if (on) {
                int32_t d = b / 2;
                zui_fill_rrect(s, rect(br.x + (b - d) / 2, cy - d / 2, d, d), d / 2, fill, 255);
            }
        }
        x += b + PX(ui, ZUI_S2);
    }
    zui_text_fit(s, t, x, cap_base(t, r.y, r.h), r.x + r.w - x, off ? C(FAINT) : C(TEXT_2),
                 w->label);
}

/* W8: an engraved track, the part up to the value lit, a knob. */
static void paint_slider(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    int32_t x0 = slider_x0(ui, w), len = slider_len(ui, w), cy = w->r.y + w->r.h / 2;
    int32_t th = imax(2, PX(ui, 4)), k = PX(ui, 18);
    int64_t span = (int64_t) w->u.range.max - w->u.range.min;
    int32_t at = span ? (int32_t) (((int64_t) w->u.range.value - w->u.range.min) * len / span) : 0;
    zui_rect_t tr = rect(x0, cy - th / 2, len, th);
    zui_fill_rrect(s, tr, th / 2, C(RAISED), 255);
    zui_engrave(s, tr, th / 2, C(ENGRAVE_LO), C(ENGRAVE_HI), 60u);
    uint32_t lit = w->disabled ? C(FAINT) : C(TEXT_2);
    if (at > 0) zui_fill_rrect(s, rect(x0, cy - th / 2, at, th), th / 2, lit, 255);
    zui_rect_t kr = rect(x0 + at - k / 2, cy - k / 2, k, k);
    zui_fill_rrect(s, kr, k / 2, w->disabled ? C(FAINT) : C(TEXT), 255);
    zui_stroke_rrect(s, kr, k / 2, imax(1, PX(ui, 1)), C(ENGRAVE_LO), 255);
}

/* W8: a tab (an underline when chosen) or a segment (a raised step). */
static void paint_tab(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    const zui_text_t *t = face(ui, w->font);
    zui_rect_t r = w->r;
    const zui_widget_t *p = w->parent != ZUI_NONE ? &ui->w[w->parent] : 0;
    bool sel = p && p->u.tabs.sel == zui_choice_index(ui, id), seg_ = p && p->u.tabs.segmented;
    bool hot = ui->hover == id;
    if (seg_ && sel) {
        zui_fill_rrect(s, r, PX(ui, ZUI_R_CONTROL), C(RAISED), 255);
        zui_engrave(s, r, PX(ui, ZUI_R_CONTROL), C(ENGRAVE_LO), C(ENGRAVE_HI), 50u);
    } else if (hot && !w->disabled) {
        zui_fill_rrect(s, r, PX(ui, ZUI_R_CONTROL), C(RAISED), 120);
    }
    if (!seg_ && sel)
        zui_fill(s,
                 rect(r.x + PX(ui, ZUI_S2), r.y + r.h - imax(2, PX(ui, 2)),
                      imax(0, r.w - 2 * PX(ui, ZUI_S2)), imax(2, PX(ui, 2))),
                 C(TEXT));
    int32_t tw = text_w(t, w->label);
    uint32_t fg = w->disabled ? C(FAINT) : sel ? C(TEXT) : C(TEXT_2);
    zui_text_fit(s, t, r.x + imax(0, (r.w - tw) / 2), cap_base(t, r.y, r.h), r.w, fg, w->label);
}

static void paint_one(zui_ui_t *ui, zui_surface_t *s, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    const zui_text_t *t = face(ui, w->font);
    switch (w->kind) {
    case ZUI_W_BOX:
        if (w->panel) {
            zui_fill_rrect(s, w->r, PX(ui, ZUI_R_PANEL), C(PANEL), 255);
            zui_engrave(s, w->r, PX(ui, ZUI_R_PANEL), C(ENGRAVE_LO), C(ENGRAVE_HI), 36);
        } else if (id == ui->root) {
            zui_fill(s, w->r, C(VOID));
        }
        break;
    case ZUI_W_LABEL: {
        uint32_t fg = w->color                    ? zui_color_remap(ui->colors, w->color)
                      : w->font == ZUI_TEXT_LABEL ? C(MUTED)
                      : w->font == ZUI_TEXT_TITLE ? C(TEXT)
                                                  : C(TEXT_2);
        if (w->disabled) fg = C(FAINT);
        int32_t base = w->r.y + (w->r.h - line_h(t)) / 2 + ascent(t);
        zui_text_fit(s, t, w->r.x, base, w->r.w, fg, w->label);
        break;
    }
    case ZUI_W_BUTTON:
        paint_button(ui, s, id);
        break;
    case ZUI_W_LIST:
        paint_list(ui, s, id);
        break;
    case ZUI_W_FIELD:
        paint_field(ui, s, id);
        break;
    case ZUI_W_HELD:
        paint_held(ui, s, id);
        break;
    case ZUI_W_PROGRESS:
        zui_bar_draw_in(s, w->r, (zui_bar_mode_t) w->u.bar.mode, w->u.bar.p, ui->now_ms / 16u, 0, 0,
                        ui->colors);
        break;
    case ZUI_W_CANVAS:
        if (w->u.canvas.paint) {
            zui_clip(s, w->r);
            w->u.canvas.paint(ui, id, s, w->r, w->u.canvas.ctx);
            zui_unclip(s);
        }
        break;
    case ZUI_W_SEPARATOR:
        zui_hline(s, w->r.x, w->r.y + w->r.h / 2, w->r.w, imax(1, PX(ui, 1)), C(HAIRLINE));
        break;
    case ZUI_W_SCROLL:
        break;
    case ZUI_W_TOGGLE:
    case ZUI_W_CHECK:
    case ZUI_W_RADIO:
        paint_choice(ui, s, id);
        break;
    case ZUI_W_SLIDER:
        paint_slider(ui, s, id);
        break;
    case ZUI_W_TABS:
        if (w->u.tabs.segmented) {
            zui_fill_rrect(s, w->r, PX(ui, ZUI_R_CONTROL) + 2, C(PANEL), 255);
            zui_engrave(s, w->r, PX(ui, ZUI_R_CONTROL) + 2, C(ENGRAVE_LO), C(ENGRAVE_HI), 40u);
        } else {
            zui_hline(s, w->r.x, w->r.y + w->r.h - imax(1, PX(ui, 1)), w->r.w, imax(1, PX(ui, 1)),
                      C(HAIRLINE));
        }
        break;
    case ZUI_W_TAB:
        paint_tab(ui, s, id);
        break;
    case ZUI_W_DIALOG: {
        int32_t rad = PX(ui, ZUI_R_PANEL);
        zui_fill_rrect(s, w->r, rad, C(PANEL), 255);
        if (w->u.dialog.sheet) /* square at the bottom edge */
            zui_fill(s, rect(w->r.x, w->r.y + w->r.h - rad, w->r.w, rad), C(PANEL));
        zui_engrave(s, w->r, rad, C(ENGRAVE_LO), C(ENGRAVE_HI), 60u);
        zui_stroke_rrect(s, w->r, rad, imax(1, PX(ui, 1)), C(HAIRLINE), 255);
        break;
    }
    case ZUI_W_EMPTY:
        if (w->first != ZUI_NONE) {
            int32_t d = PX(ui, 48);
            zui_rect_t b =
                rect(w->r.x + (w->r.w - d) / 2, ui->w[w->first].r.y - PX(ui, w->gap) - d, d, d);
            paint_icon(ui, s, (zui_icon_t) w->u.empty.icon, b);
        }
        break;
    }
}

static void paint_tree(zui_ui_t *ui, zui_surface_t *s, zui_id_t id, zui_rect_t dmg)
{
    zui_widget_t *w = &ui->w[id];
    if (w->hidden || !overlap(inset(w->r, -PX(ui, 4)), dmg)) return;
    paint_one(ui, s, id);
    bool clip = w->kind == ZUI_W_SCROLL;
    zui_rect_t keep = s->clip;
    if (clip) zui_clip(s, w->r);
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next)
        if (ui->w[c].kind != ZUI_W_DIALOG) paint_tree(ui, s, c, dmg); /* W9: painted last */
    if (clip) s->clip = keep;
    if (w->kind == ZUI_W_SCROLL) paint_scroll(ui, s, id); /* W11 */
    if (ui->focus == id && ui->focus_visible && w->kind != ZUI_W_LIST)
        paint_focus(ui, s, seen(ui, id));
}

static void paint_menu(zui_ui_t *ui, zui_surface_t *s)
{
    zui_menu_t *m = &ui->menu;
    int32_t rh = menu_row(ui), pad = PX(ui, ZUI_S3);
    zui_fill_rrect(s, m->r, PX(ui, ZUI_R_PANEL), C(RAISED), 255);
    zui_stroke_rrect(s, m->r, PX(ui, ZUI_R_PANEL), imax(1, PX(ui, 1)), C(EDGE), 255);
    const zui_text_t *t = &ui->theme.body;
    for (uint32_t i = 0; i < m->n; i++) {
        zui_rect_t rr = rect(m->r.x, m->r.y + PX(ui, ZUI_S1) + (int32_t) i * rh, m->r.w, rh);
        if (seq(m->item[i], "-")) {
            zui_hline(s, rr.x + pad, rr.y + rh / 2, rr.w - 2 * pad, imax(1, PX(ui, 1)),
                      C(HAIRLINE));
            continue;
        }
        if ((int32_t) i == m->hot)
            zui_fill_rrect(s, inset(rr, PX(ui, 2)), PX(ui, ZUI_R_CONTROL), C(HAIRLINE), 255);
        int32_t base = rr.y + (rh + ascent(t) - (line_h(t) - ascent(t)) / 2) / 2;
        zui_text(s, t, rr.x + pad, base, m->off[i] ? C(FAINT) : C(TEXT), m->item[i]);
        if (m->key[i][0])
            zui_text(s, &ui->theme.label, rr.x + rr.w - pad - text_w(&ui->theme.label, m->key[i]),
                     base, C(MUTED), m->key[i]);
    }
}

static void paint_palette(zui_ui_t *ui, zui_surface_t *s)
{
    zui_palette_t *pl = &ui->palette;
    zui_rect_t r = pl->r;
    int32_t pad = PX(ui, ZUI_S3), rh = menu_row(ui);
    /* the screen behind recedes a step */
    zui_fill_a(s, rect(0, 0, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h), C(VOID), 150);
    zui_fill_rrect(s, r, PX(ui, ZUI_R_PANEL), C(PANEL), 255);
    zui_stroke_rrect(s, r, PX(ui, ZUI_R_PANEL), imax(1, PX(ui, 1)), C(GOLD), 255);
    const zui_text_t *t = &ui->theme.body, *tl = &ui->theme.label;
    zui_rect_t q = rect(r.x + pad, r.y + pad, r.w - 2 * pad, PX(ui, 44) - pad);
    zui_fill_rrect(s, q, PX(ui, ZUI_R_CONTROL), C(RAISED), 255);
    int32_t base = q.y + (q.h + ascent(t) - (line_h(t) - ascent(t)) / 2) / 2;
    int32_t x = q.x + PX(ui, ZUI_S2);
    if (pl->q.len) {
        int32_t end = draw_span(s, t, x, base, C(TEXT), pl->query, 0, pl->q.caret);
        draw_span(s, t, end, base, C(TEXT), pl->query, pl->q.caret, pl->q.len);
        zui_fill(s, rect(end, q.y + PX(ui, 6), imax(1, PX(ui, 2)), q.h - PX(ui, 12)), C(GOLD));
    } else {
        zui_fill(s, rect(x, q.y + PX(ui, 6), imax(1, PX(ui, 2)), q.h - PX(ui, 12)), C(GOLD));
        zui_text(s, t, x + PX(ui, 6), base, C(FAINT), "Ask the swarm, or pick an action");
    }
    int32_t top = r.y + PX(ui, 44) + PX(ui, ZUI_S2);
    for (uint32_t i = 0; i < pl->n_match; i++) {
        zui_rect_t rr =
            rect(r.x + PX(ui, ZUI_S1), top + (int32_t) i * rh, r.w - 2 * PX(ui, ZUI_S1), rh);
        if (rr.y + rh > r.y + r.h - PX(ui, 28)) break;
        const zui_palette_item_t *it = &pl->item[pl->match[i]];
        if ((int32_t) i == pl->hot) zui_fill_rrect(s, rr, PX(ui, ZUI_R_CONTROL), C(RAISED), 255);
        int32_t b = rr.y + (rh + ascent(t) - (line_h(t) - ascent(t)) / 2) / 2;
        zui_text(s, t, rr.x + pad, b, (int32_t) i == pl->hot ? C(TEXT) : C(TEXT_2), it->name);
        char meta[64];
        uint32_t n = 0;
        for (const char *p = it->agent ? it->agent : ""; *p && n < 40; p++) meta[n++] = *p;
        const char *mid = " \xC2\xB7 ~";
        for (const char *p = mid; *p; p++) meta[n++] = *p;
        char num[12];
        uint32_t k = 0, v = it->est_tokens;
        do {
            num[k++] = (char) ('0' + v % 10u);
            v /= 10u;
        } while (v && k < sizeof num);
        while (k) meta[n++] = num[--k];
        meta[n] = 0;
        zui_text(s, tl, rr.x + rr.w - pad - text_w(tl, meta), b, C(MUTED), meta);
    }
    if (!pl->n_match && pl->q.len)
        zui_text(s, t, r.x + pad, top + ascent(t) + PX(ui, 6), C(TEXT_2),
                 "Enter asks the swarm this question");
    /* W4 said where it is read: answers come back held */
    const char *foot =
        pl->context[0] ? pl->context : "Answers arrive held. Nothing changes until you accept.";
    zui_text_fit(s, tl, r.x + pad, r.y + r.h - PX(ui, 10), r.w - 2 * pad, C(MUTED), foot);
}

void zui_ui_paint(zui_ui_t *ui, zui_surface_t *s)
{
    do_layout(ui);
    if (ui->root == ZUI_NONE || !ui->damage.n) return;
    if (ui->modal != ZUI_NONE) /* W9: the scrim covers the screen: repaint it whole */
        zui_damage_all(&ui->damage, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
    for (uint32_t i = 0; i < ui->damage.n; i++) {
        zui_rect_t d = ui->damage.r[i];
        zui_clip(s, d);
        zui_fill(s, d, C(VOID));
        paint_tree(ui, s, ui->root, d);
        if (ui->modal != ZUI_NONE) {
            zui_clip(s, d);
            zui_fill_a(s, d, C(VOID), 170);
            paint_tree(ui, s, ui->modal, d);
            zui_clip(s, d);
        }
        if (ui->menu.open) paint_menu(ui, s);
        zui_clip(s, d);
        paint_tip(ui, s);
        zui_unclip(s);
    }
    if (ui->palette.open) {
        /* the dimmer covers the screen, so a palette frame repaints all of it */
        zui_unclip(s);
        zui_rect_t all = rect(0, 0, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h);
        zui_damage_all(&ui->damage, all.w, all.h);
        zui_fill(s, all, C(VOID));
        paint_tree(ui, s, ui->root, all);
        paint_palette(ui, s);
    }
}

bool zui_ui_frame(zui_ui_t *ui, zui_present_t *p, zui_queue_t *q, zui_surface_t *s,
                  uint32_t wait_ms)
{
    int got = p->ops->poll(p, q, wait_ms);
    if (got < 0) return false;
    zui_event_t e;
    bool alive = true;
    while (zui_pop(q, &e)) {
        if (e.type == ZUI_EV_CLOSE) alive = false;
        if (e.type == ZUI_EV_RESIZE || e.type == ZUI_EV_SCALE) p->screen = ui->screen;
        zui_ui_event(ui, &e);
        if (e.type == ZUI_EV_RESIZE || e.type == ZUI_EV_SCALE) p->screen = ui->screen;
    }
    zui_event_t tick;
    mset(&tick, sizeof tick);
    tick.type = ZUI_EV_TICK;
    tick.ms = p->ops->now_ms(p);
    zui_ui_event(ui, &tick);
    /* the caret blinks: repaint the focused field on each phase change */
    if (cw(ui, ui->focus) && ui->w[ui->focus].kind == ZUI_W_FIELD) zui_invalidate(ui, ui->focus);
    if (s->w != (int32_t) ui->screen.px_w || s->h != (int32_t) ui->screen.px_h) return alive;
    zui_ui_paint(ui, s);
    if (ui->damage.n) p->ops->present(p, s, &ui->damage);
    zui_damage_clear(&ui->damage);
    return alive;
}

/* ---- accessibility ------------------------------------------------------------ */

const char *zui_role_name(zui_kind_t k)
{
    switch (k) {
    case ZUI_W_BOX:
        return "group";
    case ZUI_W_LABEL:
        return "text";
    case ZUI_W_BUTTON:
        return "button";
    case ZUI_W_LIST:
        return "list";
    case ZUI_W_FIELD:
        return "textbox";
    case ZUI_W_SCROLL:
        return "scrollarea";
    case ZUI_W_HELD:
        return "suggestion";
    case ZUI_W_PROGRESS:
        return "progressbar";
    case ZUI_W_CANVAS:
        return "image";
    case ZUI_W_SEPARATOR:
        return "separator";
    case ZUI_W_TOGGLE:
        return "switch";
    case ZUI_W_CHECK:
        return "checkbox";
    case ZUI_W_RADIO:
        return "radio";
    case ZUI_W_SLIDER:
        return "slider";
    case ZUI_W_TABS:
        return "tablist";
    case ZUI_W_TAB:
        return "tab";
    case ZUI_W_DIALOG:
        return "dialog";
    case ZUI_W_EMPTY:
        return "note";
    }
    return "group";
}

typedef struct {
    char *out;
    uint32_t cap, n;
} sb_t;

static void sb_s(sb_t *b, const char *s)
{
    while (s && *s && b->n + 1u < b->cap) b->out[b->n++] = *s++;
    if (b->cap) b->out[b->n < b->cap ? b->n : b->cap - 1u] = 0;
}

static void sb_u(sb_t *b, int64_t v)
{
    char num[24];
    uint32_t k = 0;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) (-(v + 1)) + 1u : (uint64_t) v;
    do {
        num[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < sizeof num);
    if (neg) sb_s(b, "-");
    char one[2] = {0, 0};
    while (k) {
        one[0] = num[--k];
        sb_s(b, one);
    }
}

static void a11y(const zui_ui_t *ui, zui_id_t id, int depth, sb_t *b)
{
    const zui_widget_t *w = &ui->w[id];
    if (w->hidden) return;
    bool plain_box = w->kind == ZUI_W_BOX && !w->panel && !w->name[0];
    if (!plain_box && w->kind != ZUI_W_SEPARATOR) {
        for (int i = 0; i < depth; i++) sb_s(b, "  ");
        sb_s(b, zui_role_name(w->kind));
        const char *name = w->name[0] ? w->name : w->label;
        if (name[0]) {
            sb_s(b, " \"");
            sb_s(b, name);
            sb_s(b, "\"");
        }
        if (w->kind == ZUI_W_LIST) {
            sb_s(b, " ");
            sb_u(b, w->u.list.rows);
            sb_s(b, " rows");
            if (w->u.list.sel >= 0) {
                zui_row_t r;
                mset(&r, sizeof r);
                if (w->u.list.row) w->u.list.row(ui, id, w->u.list.sel, &r, w->u.list.row_ctx);
                sb_s(b, ", row ");
                sb_u(b, w->u.list.sel + 1);
                sb_s(b, " selected \"");
                sb_s(b, r.text);
                sb_s(b, "\"");
            }
        }
        if (w->kind == ZUI_W_FIELD) {
            if (w->u.field.readonly) sb_s(b, " readonly");
            sb_s(b, " ");
            sb_u(b, w->u.field.len);
            sb_s(b, " bytes");
        }
        if (w->kind == ZUI_W_HELD) {
            sb_s(b, w->u.held.state == 1   ? " pending"
                    : w->u.held.state == 2 ? " accepted"
                                           : " rejected");
            sb_s(b, " by ");
            sb_s(b, w->u.held.agent);
        }
        if (w->kind == ZUI_W_TOGGLE) sb_s(b, w->u.check.state ? " on" : " off");
        if (w->kind == ZUI_W_CHECK)
            sb_s(b, w->u.check.state == 2 ? " mixed"
                    : w->u.check.state    ? " checked"
                                          : " not checked");
        if (w->kind == ZUI_W_RADIO) sb_s(b, w->u.check.state ? " selected" : " not selected");
        if (w->kind == ZUI_W_TAB && w->parent != ZUI_NONE &&
            ui->w[w->parent].u.tabs.sel == zui_choice_index(ui, id))
            sb_s(b, " selected");
        if (w->kind == ZUI_W_SLIDER) {
            sb_s(b, " value ");
            sb_u(b, w->u.range.value);
            sb_s(b, " of ");
            sb_u(b, w->u.range.min);
            sb_s(b, "..");
            sb_u(b, w->u.range.max);
        }
        if (w->kind == ZUI_W_DIALOG) sb_s(b, " modal");
        {
            int32_t total, at, view; /* W11 */
            if (scroll_state(ui, w, &total, &at, &view)) {
                int32_t below = total - view - at;
                bool rows = w->kind == ZUI_W_LIST;
                if (at > 0) {
                    sb_s(b, ", ");
                    sb_u(b, at);
                    sb_s(b, !rows ? " px" : at == 1 ? " row" : " rows");
                    sb_s(b, " above");
                }
                if (below > 0) {
                    sb_s(b, ", ");
                    sb_u(b, below);
                    sb_s(b, !rows ? " px" : below == 1 ? " row" : " rows");
                    sb_s(b, " below");
                }
            }
        }
        if (w->kind == ZUI_W_PROGRESS) {
            if (w->u.bar.p == ZUI_BAR_UNKNOWN) {
                sb_s(b, " busy");
            } else {
                sb_s(b, " ");
                sb_u(b, (int64_t) ((w->u.bar.p * 100u + 128u) / 256u));
                sb_s(b, "%");
            }
        }
        if (w->shortcut[0]) {
            sb_s(b, " shortcut ");
            sb_s(b, w->shortcut);
        }
        if (w->tip[0]) {
            sb_s(b, " tip \"");
            sb_s(b, w->tip);
            sb_s(b, "\"");
        }
        if (w->disabled) sb_s(b, " disabled");
        if (ui->focus == id) sb_s(b, " focused");
        sb_s(b, "\n");
        depth++;
    }
    for (zui_id_t c = w->first; c != ZUI_NONE; c = ui->w[c].next) a11y(ui, c, depth, b);
}

uint32_t zui_a11y_text(const zui_ui_t *ui, char *out, uint32_t cap)
{
    sb_t b = {out, cap, 0};
    if (cap) out[0] = 0;
    if (ui->root != ZUI_NONE) a11y(ui, ui->root, 0, &b);
    if (ui->menu.open) {
        sb_s(&b, "menu\n");
        for (uint32_t i = 0; i < ui->menu.n; i++) {
            if (seq(ui->menu.item[i], "-")) continue;
            sb_s(&b, "  menuitem \"");
            sb_s(&b, ui->menu.item[i]);
            sb_s(&b, "\"");
            if (ui->menu.off[i]) sb_s(&b, " disabled");
            if ((int32_t) i == ui->menu.hot) sb_s(&b, " focused");
            sb_s(&b, "\n");
        }
    }
    if (ui->palette.open) {
        sb_s(&b, "dialog \"Ask\"\n  textbox \"");
        sb_s(&b, ui->palette.query);
        sb_s(&b, "\" focused\n");
        for (uint32_t i = 0; i < ui->palette.n_match; i++) {
            sb_s(&b, "  option \"");
            sb_s(&b, ui->palette.item[ui->palette.match[i]].name);
            sb_s(&b, "\"");
            if ((int32_t) i == ui->palette.hot) sb_s(&b, " selected");
            sb_s(&b, "\n");
        }
    }
    return b.n;
}
