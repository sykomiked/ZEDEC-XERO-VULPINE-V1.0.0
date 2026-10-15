/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This screen shows an agreement and reports whether a person
 * accepted it. It does not provide legal advice or decide whether an
 * agreement is enforceable anywhere.
 */
/* zui_terms.c — see zui_terms.h. */
#include "zui_terms.h"

#include <string.h>

#include "zui_tokens.h"

/* T2: a codepoint's advance in quarter pixels, rounded up so a measured line
 * never draws wider than the box. Never 0, so every line can wrap. */
static uint16_t measure_q2(void *ctx, uint32_t cp)
{
    const zui_text_t *f = (const zui_text_t *) ctx;
    char u[5] = {0};
    int32_t q8;
    if (cp < 0x80) {
        u[0] = (char) cp;
    } else if (cp < 0x800) {
        u[0] = (char) (0xC0 | (cp >> 6));
        u[1] = (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        u[0] = (char) (0xE0 | (cp >> 12));
        u[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        u[2] = (char) (0x80 | (cp & 0x3F));
    } else {
        u[0] = (char) (0xF0 | (cp >> 18));
        u[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
        u[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
        u[3] = (char) (0x80 | (cp & 0x3F));
    }
    q8 = zui_text_width_q8(f, u);
    q8 = (q8 + 63) >> 6;
    if (q8 < 1) q8 = 1;
    return (uint16_t) (q8 > 0xFFFF ? 0xFFFF : q8);
}

static const char *const DEFAULT_SUMMARY = "The short version";
static const char *const TOGGLE_OPEN = "Contents";
static const char *const TOGGLE_CLOSE = "Back to the text";

static bool wide(const zui_terms_t *t)
{
    return t->lay.toggle.w == 0;
}

static bool text_shown(const zui_terms_t *t)
{
    return wide(t) || !t->list_open;
}

static bool list_shown(const zui_terms_t *t)
{
    return wide(t) || t->list_open;
}

static void layout(zui_terms_t *t)
{
    const zui_screen_t *sc = &t->screen;
    zui_terms_layout_t *L = &t->lay;
    zui_rect_t a = t->area;
    int32_t gut = zui_px(sc, sc->px_w < (uint32_t) zui_px(sc, 600) ? ZUI_S4 : ZUI_S6);
    int32_t btn_h = zui_px(sc, ZUI_TOUCH_MIN);
    int32_t btn_w = zui_px(sc, 140);
    int32_t asc = t->font ? zui_text_ascent(t->font) : 0;
    int32_t desc = t->font ? zui_text_descent(t->font) : 0;
    int32_t bar_w = zui_px(sc, ZUI_S2);
    int32_t gap = zui_px(sc, ZUI_S3);
    int32_t list_w = zui_px(sc, ZUI_TERMS_LIST_PT);
    int32_t top, bottom, maxw;
    bool is_wide;

    L->pad = zui_px(sc, ZUI_S5);
    L->line_h = asc - desc + zui_px(sc, ZUI_S1) + zui_px(sc, 2);
    if (L->line_h < 1) L->line_h = 1;
    L->entry_h = L->line_h + zui_px(sc, ZUI_S2);

    is_wide = a.w - 2 * gut >= zui_px(sc, ZUI_TERMS_WIDE_PT);
    maxw = zui_px(sc, ZUI_TERMS_MAX_PT);
    if (is_wide) maxw += list_w + L->pad;
    L->card.w = a.w - 2 * gut;
    if (L->card.w > maxw) L->card.w = maxw;
    L->card.h = a.h - 2 * gut;
    L->card.x = a.x + (a.w - L->card.w) / 2;
    L->card.y = a.y + gut;

    /* bottom up: buttons, hint, then the text (and the list) take the rest */
    L->agree.w = btn_w;
    L->agree.h = btn_h;
    L->agree.x = L->card.x + L->card.w - L->pad - btn_w;
    L->agree.y = L->card.y + L->card.h - L->pad - btn_h;
    L->decline = L->agree;
    L->decline.x = L->agree.x - gap - btn_w;
    if (L->decline.x < L->card.x + L->pad) { /* phone: two equal halves */
        int32_t half = (L->card.w - 2 * L->pad - gap) / 2;
        L->decline.x = L->card.x + L->pad;
        L->decline.w = half;
        L->agree.x = L->decline.x + half + gap;
        L->agree.w = half;
    }
    L->hint.x = L->card.x + L->pad;
    L->hint.w = L->card.w - 2 * L->pad;
    L->hint.h = L->line_h;
    L->hint.y = L->agree.y - gap - L->hint.h;

    top = L->card.y + L->pad;
    bottom = L->hint.y - gap;
    memset(&L->toggle, 0, sizeof L->toggle);
    if (is_wide) {
        L->contents.x = L->card.x + L->pad;
        L->contents.y = top;
        L->contents.w = list_w;
        L->contents.h = bottom - top;
        L->text.x = L->contents.x + list_w + L->pad;
    } else {
        L->toggle.x = L->card.x + L->pad;
        L->toggle.y = top;
        L->toggle.w = L->card.w - 2 * L->pad;
        L->toggle.h = btn_h;
        top += btn_h + gap;
        L->text.x = L->card.x + L->pad;
    }
    L->text.y = top;
    L->text.w = L->card.x + L->card.w - L->pad - bar_w - gap - L->text.x;
    L->text.h = bottom - top;
    L->bar.x = L->text.x + L->text.w + gap;
    L->bar.y = L->text.y;
    L->bar.w = bar_w;
    L->bar.h = L->text.h;
    if (!is_wide) { /* the list opens over the text and its scroll bar */
        L->contents = L->text;
        L->contents.w = L->bar.x + L->bar.w - L->text.x;
    }
}

/* Lay out and give the reader's width (quarter pixels) and rows. */
static int fit(zui_terms_t *t, uint16_t *w_q2, uint16_t *rows_out)
{
    int32_t rows, w;
    layout(t);
    rows = t->lay.text.h / t->lay.line_h;
    w = (t->lay.text.w - zui_px(&t->screen, ZUI_S3)) * 4; /* room for the key bar */
    if (rows < 1 || w < 8 || t->lay.agree.w < 1 || t->lay.contents.h < 2 * t->lay.entry_h)
        return TERMS_ERR_ARG;
    *w_q2 = (uint16_t) (w > 0xFFFF ? 0xFFFF : w);
    *rows_out = (uint16_t) (rows > 0xFFFF ? 0xFFFF : rows);
    return TERMS_OK;
}

/* ---------------------------------------------------------------- contents */

uint32_t zui_terms_entries(const zui_terms_t *t)
{
    return t && t->r ? (uint32_t) t->r->nsections + 1u : 0;
}

uint32_t zui_terms_current(const zui_terms_t *t)
{
    uint16_t sec, k;
    if (!t || !t->r || !t->r->nlines) return 0;
    sec = t->r->lines[t->r->top].section;
    for (k = 0; sec && k < t->r->nsections; k++)
        if (t->r->section_no[k] == sec) return (uint32_t) k + 1u;
    return 0;
}

static uint32_t list_rows(const zui_terms_t *t)
{
    int32_t v = t->lay.contents.h / t->lay.entry_h;
    return v < 2 ? 2u : (uint32_t) v;
}

static void list_clamp(zui_terms_t *t)
{
    uint32_t n = zui_terms_entries(t), vis = list_rows(t);
    uint32_t max = n - 1 > vis - 1 ? (n - 1) - (vis - 1) : 0;
    if (t->list_sel >= n) t->list_sel = (uint16_t) (n ? n - 1 : 0);
    if (t->list_top > max) t->list_top = (uint16_t) max;
}

static void list_show(zui_terms_t *t, uint32_t i)
{
    uint32_t vis = list_rows(t);
    if (i > 0) {
        if (i - 1 < t->list_top) t->list_top = (uint16_t) (i - 1);
        if (i - 1 >= t->list_top + vis - 1) t->list_top = (uint16_t) (i - 1 - (vis - 2));
    }
    list_clamp(t);
}

/* Entry shown in row `row` of the list (row 0 is the pinned summary). */
static int32_t entry_of_row(const zui_terms_t *t, uint32_t row)
{
    uint32_t e = row == 0 ? 0 : 1u + t->list_top + row - 1;
    return row < list_rows(t) && e < zui_terms_entries(t) ? (int32_t) e : -1;
}

static int32_t entry_at(const zui_terms_t *t, int32_t x, int32_t y)
{
    const zui_rect_t *c = &t->lay.contents;
    if (!list_shown(t) || x < c->x || y < c->y || x >= c->x + c->w || y >= c->y + c->h) return -1;
    return entry_of_row(t, (uint32_t) ((y - c->y) / t->lay.entry_h));
}

int zui_terms_go(zui_terms_t *t, uint32_t i)
{
    int rc = TERMS_OK;
    if (!t || !t->r || i >= zui_terms_entries(t) || t->result != ZUI_TERMS_OPEN)
        return TERMS_ERR_ARG;
    if (i == 0)
        terms_scroll(t->r, -(int32_t) t->r->top, TERMS_VIA_SECTION);
    else
        rc = terms_goto_section(t->r, t->r->section_no[i - 1]);
    t->wheel_px = 0;
    t->list_sel = (uint16_t) i;
    list_show(t, i);
    if (!wide(t)) { /* narrow: back to the text, where the reading is */
        t->list_open = false;
        t->focus = ZUI_TERMS_FOCUS_TEXT;
    }
    return rc;
}

static void list_open(zui_terms_t *t, bool open)
{
    t->list_open = open;
    if (open) {
        t->list_sel = (uint16_t) zui_terms_current(t);
        list_show(t, t->list_sel);
        t->focus = ZUI_TERMS_FOCUS_CONTENTS;
    }
}

/* ------------------------------------------------------------------- open */

int zui_terms_open(zui_terms_t *t, terms_reader_t *r, const terms_doc_t *doc,
                   const zui_text_t *font, const zui_screen_t *screen, zui_rect_t area)
{
    uint16_t w = 0, rows = 0;
    int rc;
    if (!t || !r || !doc || !font || !screen) return TERMS_ERR_ARG;
    memset(t, 0, sizeof *t);
    t->r = r;
    t->font = font;
    t->screen = *screen;
    t->area = area;
    t->entry_down = -1;
    t->summary = DEFAULT_SUMMARY;
    rc = fit(t, &w, &rows);
    if (rc) return rc;
    rc = terms_reader_init_ex(r, doc, w, rows, measure_q2, (void *) font);
    if (!rc) list_clamp(t);
    return rc;
}

int zui_terms_resize(zui_terms_t *t, const zui_screen_t *screen, zui_rect_t area)
{
    zui_screen_t os;
    zui_rect_t oa;
    uint16_t w = 0, rows = 0;
    int rc;
    if (!t || !t->r || !screen) return TERMS_ERR_ARG;
    os = t->screen;
    oa = t->area;
    t->screen = *screen;
    t->area = area;
    rc = fit(t, &w, &rows);
    if (!rc) rc = terms_reader_resize(t->r, w, rows);
    if (rc) { /* too small: keep the old layout */
        t->screen = os;
        t->area = oa;
        layout(t);
    }
    if (wide(t)) t->list_open = false;
    list_clamp(t);
    return rc;
}

/* ------------------------------------------------------------------ input */

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static int8_t button_at(const zui_terms_t *t, int32_t x, int32_t y)
{
    if (inside(t->lay.decline, x, y)) return 1;
    if (inside(t->lay.agree, x, y)) return 2;
    if (!wide(t) && inside(t->lay.toggle, x, y)) return 3;
    return 0;
}

static void press(zui_terms_t *t, int8_t which)
{
    if (t->result != ZUI_TERMS_OPEN) return;
    if (which == 1) {
        terms_decline(t->r);
        t->result = ZUI_TERMS_DECLINED;
    } else if (which == 2 && terms_can_accept(t->r)) {
        t->result = ZUI_TERMS_ACCEPTED;
    } else if (which == 3) {
        list_open(t, !t->list_open);
    }
}

static void scroll_px(zui_terms_t *t, int32_t dy)
{
    int32_t lines;
    t->wheel_px += dy;
    lines = t->wheel_px / t->lay.line_h;
    t->wheel_px -= lines * t->lay.line_h;
    if (lines) terms_scroll(t->r, lines, TERMS_VIA_SCROLL);
}

static void focus_step(zui_terms_t *t, bool back)
{
    static const zui_terms_focus_t order[4] = {ZUI_TERMS_FOCUS_CONTENTS, ZUI_TERMS_FOCUS_TEXT,
                                               ZUI_TERMS_FOCUS_DECLINE, ZUI_TERMS_FOCUS_AGREE};
    int i;
    for (i = 0; i < 4 && order[i] != t->focus; i++) {
    }
    i = back ? (i + 3) % 4 : (i + 1) % 4;
    t->focus = order[i];
    if (t->focus != ZUI_TERMS_FOCUS_CONTENTS) t->list_open = false;
}

static bool list_key(zui_terms_t *t, const zui_event_t *e)
{
    uint32_t n = zui_terms_entries(t), step = list_rows(t) - 1;
    int32_t sel = t->list_sel;
    switch (e->key) {
    case ZUI_KEY_UP:
        sel--;
        break;
    case ZUI_KEY_DOWN:
        sel++;
        break;
    case ZUI_KEY_PAGE_UP:
        sel -= (int32_t) step;
        break;
    case ZUI_KEY_PAGE_DOWN:
        sel += (int32_t) step;
        break;
    case ZUI_KEY_HOME:
        sel = 0;
        break;
    case ZUI_KEY_END:
        sel = (int32_t) n - 1;
        break;
    case ZUI_KEY_ENTER:
    case ZUI_KEY_SPACE:
        zui_terms_go(t, t->list_sel);
        return true;
    case ZUI_KEY_ESCAPE:
        if (wide(t)) return false;
        t->list_open = false;
        return true;
    default:
        return false;
    }
    if (sel < 0) sel = 0;
    if (sel >= (int32_t) n) sel = (int32_t) n - 1;
    t->list_sel = (uint16_t) sel;
    list_show(t, (uint32_t) sel);
    return true;
}

static bool key(zui_terms_t *t, const zui_event_t *e)
{
    if (e->key == ZUI_KEY_TAB) {
        focus_step(t, (e->mods & ZUI_MOD_SHIFT) != 0);
        return true;
    }
    if (t->focus == ZUI_TERMS_FOCUS_CONTENTS) {
        if (list_shown(t)) return list_key(t, e);
        if (e->key == ZUI_KEY_ENTER || e->key == ZUI_KEY_SPACE) {
            list_open(t, true);
            return true;
        }
        return false;
    }
    switch (e->key) {
    case ZUI_KEY_ENTER:
    case ZUI_KEY_SPACE:
        if (t->focus == ZUI_TERMS_FOCUS_DECLINE)
            press(t, 1);
        else if (t->focus == ZUI_TERMS_FOCUS_AGREE)
            press(t, 2);
        else if (e->key == ZUI_KEY_SPACE)
            terms_page(t->r, (e->mods & ZUI_MOD_SHIFT) ? -1 : 1);
        return true;
    case ZUI_KEY_UP:
        terms_scroll(t->r, -1, TERMS_VIA_KEYBOARD);
        return true;
    case ZUI_KEY_DOWN:
        terms_scroll(t->r, 1, TERMS_VIA_KEYBOARD);
        return true;
    case ZUI_KEY_PAGE_UP:
        terms_page(t->r, -1);
        return true;
    case ZUI_KEY_PAGE_DOWN:
        terms_page(t->r, 1);
        return true;
    case ZUI_KEY_HOME:
        terms_home(t->r);
        return true;
    case ZUI_KEY_END:
        terms_end(t->r);
        return true;
    default:
        return false;
    }
}

bool zui_terms_event(zui_terms_t *t, const zui_event_t *e)
{
    if (!t || !t->r || !e || t->result != ZUI_TERMS_OPEN) return false;
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
        return key(t, e);
    case ZUI_EV_SCROLL:
        if (list_shown(t) && inside(t->lay.contents, e->x, e->y)) {
            if (e->dy > 0)
                t->list_top++;
            else if (e->dy < 0 && t->list_top)
                t->list_top--;
            list_clamp(t);
        } else if (text_shown(t)) {
            scroll_px(t, e->dy);
        }
        return true;
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_TOUCH_BEGIN:
        t->pressed = button_at(t, e->x, e->y);
        t->entry_down = -1;
        if (t->pressed) {
            t->focus = (zui_terms_focus_t) t->pressed;
        } else if ((t->entry_down = (int16_t) entry_at(t, e->x, e->y)) >= 0) {
            t->focus = ZUI_TERMS_FOCUS_CONTENTS;
            t->list_sel = (uint16_t) t->entry_down;
        } else if (text_shown(t) && inside(t->lay.bar, e->x, e->y)) { /* track click pages */
            terms_view_t v = terms_view(t->r);
            int32_t thumb_y = t->lay.bar.y + (int32_t) ((int64_t) t->lay.bar.h * v.scroll256 *
                                                        (256 - v.thumb256) / 65536);
            terms_scroll(t->r, e->y < thumb_y ? -(int32_t) t->r->rows : (int32_t) t->r->rows,
                         TERMS_VIA_SCROLL);
        } else if (text_shown(t) && inside(t->lay.text, e->x, e->y)) {
            t->dragging = true;
            t->drag_y = e->y;
            t->focus = ZUI_TERMS_FOCUS_TEXT;
        }
        return true;
    case ZUI_EV_PTR_MOVE:
    case ZUI_EV_TOUCH_MOVE:
        if (t->dragging) {
            scroll_px(t, t->drag_y - e->y); /* content follows the finger */
            t->drag_y = e->y;
        }
        return true;
    case ZUI_EV_TOUCH_CANCEL:
        t->pressed = 0;
        t->entry_down = -1;
        t->dragging = false;
        return true;
    case ZUI_EV_PTR_UP:
    case ZUI_EV_TOUCH_END:
        if (t->pressed && button_at(t, e->x, e->y) == t->pressed)
            press(t, t->pressed);
        else if (t->entry_down >= 0 && entry_at(t, e->x, e->y) == t->entry_down)
            zui_terms_go(t, (uint32_t) t->entry_down);
        t->pressed = 0;
        t->entry_down = -1;
        t->dragging = false;
        return true;
    case ZUI_EV_RESIZE: {
        zui_screen_t sc = t->screen;
        zui_rect_t a = {0, 0, e->x, e->y};
        sc.px_w = (uint32_t) e->x;
        sc.px_h = (uint32_t) e->y;
        zui_terms_resize(t, &sc, a);
        return true;
    }
    case ZUI_EV_SCALE: {
        zui_screen_t sc = t->screen;
        sc.scale_q8 = (uint32_t) e->x;
        zui_terms_resize(t, &sc, t->area);
        return true;
    }
    default:
        return false;
    }
}

void zui_terms_spoke(zui_terms_t *t, uint32_t off)
{
    if (t && t->r) terms_reader_spoke(t->r, off);
}

/* ---------------------------------------------------------------- drawing */

/* T7: every pair below is checked for contrast by test_zui_terms. */
static const char *const HINT[] = {"Read to the end to continue.", "You've reached the end.",
                                   "You declined. Nothing was recorded."};

static void label(zui_surface_t *s, const zui_terms_t *t, zui_rect_t r, uint32_t rgb,
                  const char *txt)
{
    int32_t w = zui_text_width(t->font, txt);
    int32_t asc = zui_text_ascent(t->font), desc = zui_text_descent(t->font);
    int32_t base = r.y + (r.h - (asc - desc)) / 2 + asc;
    zui_text_fit(s, t->font, r.x + (r.w > w ? (r.w - w) / 2 : 0), base, r.w, rgb, txt);
}

static void focus_ring(zui_surface_t *s, const zui_terms_t *t, zui_rect_t r)
{
    int32_t o = zui_px(&t->screen, 3);
    zui_rect_t f = {r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o};
    zui_stroke_rrect(s, f, zui_px(&t->screen, ZUI_R_CONTROL) + o, zui_px(&t->screen, 2),
                     ZUI_GOLD_GLOW, 255);
}

static void draw_text(zui_surface_t *s, const zui_terms_t *t, terms_view_t v)
{
    const zui_terms_layout_t *L = &t->lay;
    int32_t asc = zui_text_ascent(t->font), keybar = zui_px(&t->screen, 3);
    uint32_t i;
    char line[1024];

    zui_clip(s, L->text);
    for (i = 0; i < v.count; i++) {
        const terms_line_t *ln = &t->r->lines[v.first + i];
        int32_t y = L->text.y + (int32_t) i * L->line_h;
        int32_t x = L->text.x + zui_px(&t->screen, ZUI_S3);
        uint32_t ink = ZUI_TEXT_2;
        if (ln->key && ln->kind != TERMS_LINE_BLANK) {
            zui_rect_t b = {L->text.x, y, keybar, L->line_h};
            zui_fill(s, b, ZUI_GOLD);
            ink = ZUI_TEXT;
        }
        if (ln->kind == TERMS_LINE_TITLE || ln->kind == TERMS_LINE_HEADING) ink = ZUI_TEXT;
        if (ln->kind == TERMS_LINE_BLANK) continue;
        terms_line_text(t->r, v.first + i, line, sizeof line);
        zui_text(s, t->font, x, y + asc + zui_px(&t->screen, 2), ink, line);
    }
    zui_unclip(s);
    if (t->focus == ZUI_TERMS_FOCUS_TEXT && t->result == ZUI_TERMS_OPEN) focus_ring(s, t, L->text);

    /* the scroll bar: the thumb is 3:1 against its track (1.4.11) */
    zui_fill_rrect(s, L->bar, L->bar.w / 2, ZUI_HAIRLINE, 255);
    {
        zui_rect_t th = L->bar;
        th.h = (int32_t) ((int64_t) L->bar.h * v.thumb256 / 256);
        if (th.h < L->bar.w) th.h = L->bar.w;
        th.y = L->bar.y + (int32_t) ((int64_t) (L->bar.h - th.h) * v.scroll256 / 256);
        zui_fill_rrect(s, th, L->bar.w / 2, ZUI_MUTED, 255);
    }
}

/* A section's whole heading, joined back from its wrapped lines. */
static void heading_text(const terms_reader_t *r, uint16_t k, char *out, size_t cap)
{
    uint32_t j = r->section_line[k];
    size_t n = 0;
    out[0] = 0;
    for (; j < r->nlines && r->lines[j].kind == TERMS_LINE_HEADING && n + 2 < cap; j++) {
        if (n) out[n++] = ' ';
        terms_line_text(r, j, out + n, cap - n);
        while (out[n]) n++;
        if (j + 1 < r->nlines && r->lines[j + 1].section != r->lines[j].section) break;
    }
}

static void draw_list(zui_surface_t *s, const zui_terms_t *t)
{
    const zui_terms_layout_t *L = &t->lay;
    int32_t asc = zui_text_ascent(t->font), desc = zui_text_descent(t->font);
    int32_t keybar = zui_px(&t->screen, 3), inset = zui_px(&t->screen, ZUI_S3);
    int32_t ctl = zui_px(&t->screen, ZUI_R_CONTROL);
    uint32_t row, vis = list_rows(t), cur = zui_terms_current(t);
    char line[256];

    if (wide(t)) { /* a hairline between the list and the text */
        zui_rect_t sep = {L->contents.x + L->contents.w + L->pad / 2, L->contents.y, 1,
                          L->contents.h};
        zui_fill(s, sep, ZUI_HAIRLINE);
    }
    for (row = 0; row < vis; row++) {
        int32_t e = entry_of_row(t, row);
        zui_rect_t r = {L->contents.x, L->contents.y + (int32_t) row * L->entry_h, L->contents.w,
                        L->entry_h};
        int32_t base = r.y + (r.h - (asc - desc)) / 2 + asc;
        uint32_t ink = ZUI_TEXT_2;
        const char *txt = line;
        if (e < 0) break;
        if ((uint32_t) e == cur) {
            zui_fill_rrect(s, r, ctl, ZUI_RAISED, 255);
            ink = ZUI_TEXT;
        }
        if (e == 0) {
            txt = t->summary ? t->summary : DEFAULT_SUMMARY;
            ink = ZUI_GOLD;
        } else {
            uint16_t k = (uint16_t) (e - 1);
            heading_text(t->r, k, line, sizeof line);
            if (t->r->section_key[k]) {
                zui_rect_t b = {r.x, r.y + zui_px(&t->screen, 2), keybar,
                                r.h - 2 * zui_px(&t->screen, 2)};
                zui_fill(s, b, ZUI_GOLD);
            }
        }
        zui_text_fit(s, t->font, r.x + inset, base, r.w - 2 * inset, ink, txt);
        if (row == 0) { /* the pinned entry stays above the ones that scroll */
            zui_rect_t hl = {r.x, r.y + r.h - 1, r.w, 1};
            zui_fill(s, hl, ZUI_HAIRLINE);
        }
        if (t->focus == ZUI_TERMS_FOCUS_CONTENTS && t->result == ZUI_TERMS_OPEN &&
            (uint32_t) e == t->list_sel)
            focus_ring(s, t, r);
    }
}

void zui_terms_draw(zui_surface_t *s, const zui_terms_t *t)
{
    const zui_terms_layout_t *L;
    terms_view_t v;
    int32_t asc, rad, ctl;

    if (!s || !t || !t->r || !t->font) return;
    L = &t->lay;
    v = terms_view(t->r);
    asc = zui_text_ascent(t->font);
    rad = zui_px(&t->screen, ZUI_R_PANEL);
    ctl = zui_px(&t->screen, ZUI_R_CONTROL);

    zui_fill(s, t->area, ZUI_VOID);
    zui_fill_rrect(s, L->card, rad, ZUI_PANEL, 255);
    zui_stroke_rrect(s, L->card, rad, 1, ZUI_GOLD, 255);

    if (text_shown(t)) draw_text(s, t, v);
    if (list_shown(t)) draw_list(s, t);
    if (!wide(t)) {
        zui_stroke_rrect(s, L->toggle, ctl, 1, ZUI_FAINT, 255);
        label(s, t, L->toggle, ZUI_TEXT, t->list_open ? TOGGLE_CLOSE : TOGGLE_OPEN);
        if (t->focus == ZUI_TERMS_FOCUS_CONTENTS && !t->list_open && t->result == ZUI_TERMS_OPEN)
            focus_ring(s, t, L->toggle);
    }

    /* hint */
    zui_text_fit(s, t->font, L->hint.x, L->hint.y + asc, L->hint.w,
                 v.hint == TERMS_HINT_READY ? ZUI_OK : ZUI_MUTED, HINT[v.hint]);

    /* buttons: outlines 3:1, labels 4.5:1, disabled included */
    zui_stroke_rrect(s, L->decline, ctl, 1, ZUI_FAINT, 255);
    label(s, t, L->decline, ZUI_TEXT, "Decline");
    if (v.can_accept) {
        zui_fill_rrect(s, L->agree, ctl, ZUI_GOLD, 255);
        label(s, t, L->agree, ZUI_VOID, "I agree");
    } else {
        zui_fill_rrect(s, L->agree, ctl, ZUI_RAISED, 255);
        label(s, t, L->agree, ZUI_MUTED, "I agree");
    }
    if (t->result == ZUI_TERMS_OPEN) {
        if (t->focus == ZUI_TERMS_FOCUS_DECLINE) focus_ring(s, t, L->decline);
        if (t->focus == ZUI_TERMS_FOCUS_AGREE) focus_ring(s, t, L->agree);
    }
}
