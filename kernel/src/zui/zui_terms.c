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

static void layout(zui_terms_t *t)
{
    const zui_screen_t *sc = &t->screen;
    zui_terms_layout_t *L = &t->lay;
    zui_rect_t a = t->area;
    int32_t gut = zui_px(sc, sc->px_w < (uint32_t) zui_px(sc, 600) ? ZUI_S4 : ZUI_S6);
    int32_t maxw = zui_px(sc, ZUI_TERMS_MAX_PT);
    int32_t btn_h = zui_px(sc, ZUI_TOUCH_MIN);
    int32_t btn_w = zui_px(sc, 140);
    int32_t asc = t->font ? zui_text_ascent(t->font) : 0;
    int32_t desc = t->font ? zui_text_descent(t->font) : 0;
    int32_t bar_w = zui_px(sc, ZUI_S2);
    int32_t gap = zui_px(sc, ZUI_S3);

    L->pad = zui_px(sc, ZUI_S5);
    L->line_h = asc - desc + zui_px(sc, ZUI_S1) + zui_px(sc, 2);
    if (L->line_h < 1) L->line_h = 1;

    L->card.w = a.w - 2 * gut;
    if (L->card.w > maxw) L->card.w = maxw;
    L->card.h = a.h - 2 * gut;
    L->card.x = a.x + (a.w - L->card.w) / 2;
    L->card.y = a.y + gut;

    /* bottom up: buttons, hint, then the text takes the rest */
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

    L->text.x = L->card.x + L->pad;
    L->text.y = L->card.y + L->pad;
    L->text.w = L->card.w - 2 * L->pad - bar_w - gap;
    L->text.h = L->hint.y - gap - L->text.y;
    L->bar.x = L->text.x + L->text.w + gap;
    L->bar.y = L->text.y;
    L->bar.w = bar_w;
    L->bar.h = L->text.h;
}

/* Lay out and give the reader's width (quarter pixels) and rows. */
static int fit(zui_terms_t *t, uint16_t *w_q2, uint16_t *rows_out)
{
    int32_t rows, w;
    layout(t);
    rows = t->lay.text.h / t->lay.line_h;
    w = (t->lay.text.w - zui_px(&t->screen, ZUI_S3)) * 4; /* room for the key bar */
    if (rows < 1 || w < 8 || t->lay.agree.w < 1) return TERMS_ERR_ARG;
    *w_q2 = (uint16_t) (w > 0xFFFF ? 0xFFFF : w);
    *rows_out = (uint16_t) (rows > 0xFFFF ? 0xFFFF : rows);
    return TERMS_OK;
}

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
    rc = fit(t, &w, &rows);
    if (rc) return rc;
    return terms_reader_init_ex(r, doc, w, rows, measure_q2, (void *) font);
}

int zui_terms_resize(zui_terms_t *t, const zui_screen_t *screen, zui_rect_t area)
{
    zui_screen_t os;
    zui_rect_t oa;
    int rc;
    if (!t || !t->r || !screen) return TERMS_ERR_ARG;
    os = t->screen;
    oa = t->area;
    t->screen = *screen;
    t->area = area;
    uint16_t w = 0, rows = 0;
    rc = fit(t, &w, &rows);
    if (!rc) rc = terms_reader_resize(t->r, w, rows);
    if (rc) { /* too small: keep the old layout */
        t->screen = os;
        t->area = oa;
        layout(t);
    }
    return rc;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static int8_t button_at(const zui_terms_t *t, int32_t x, int32_t y)
{
    if (inside(t->lay.decline, x, y)) return 1;
    if (inside(t->lay.agree, x, y)) return 2;
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

static bool key(zui_terms_t *t, const zui_event_t *e)
{
    switch (e->key) {
    case ZUI_KEY_TAB:
        if (e->mods & ZUI_MOD_SHIFT)
            t->focus = (zui_terms_focus_t) ((t->focus + 2) % 3);
        else
            t->focus = (zui_terms_focus_t) ((t->focus + 1) % 3);
        return true;
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
        scroll_px(t, e->dy);
        return true;
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_TOUCH_BEGIN:
        t->pressed = button_at(t, e->x, e->y);
        if (t->pressed) {
            t->focus = (zui_terms_focus_t) t->pressed;
        } else if (inside(t->lay.bar, e->x, e->y)) { /* track click pages */
            terms_view_t v = terms_view(t->r);
            int32_t thumb_y = t->lay.bar.y + (int32_t) ((int64_t) t->lay.bar.h * v.scroll256 *
                                                        (256 - v.thumb256) / 65536);
            terms_scroll(t->r, e->y < thumb_y ? -(int32_t) t->r->rows : (int32_t) t->r->rows,
                         TERMS_VIA_SCROLL);
        } else if (inside(t->lay.text, e->x, e->y)) {
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
        t->dragging = false;
        return true;
    case ZUI_EV_PTR_UP:
    case ZUI_EV_TOUCH_END:
        if (t->pressed && button_at(t, e->x, e->y) == t->pressed) press(t, t->pressed);
        t->pressed = 0;
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

void zui_terms_draw(zui_surface_t *s, const zui_terms_t *t)
{
    const zui_terms_layout_t *L;
    terms_view_t v;
    int32_t asc, rad, keybar, ctl;
    uint32_t i;
    char line[1024];

    if (!s || !t || !t->r || !t->font) return;
    L = &t->lay;
    v = terms_view(t->r);
    asc = zui_text_ascent(t->font);
    rad = zui_px(&t->screen, ZUI_R_PANEL);
    ctl = zui_px(&t->screen, ZUI_R_CONTROL);
    keybar = zui_px(&t->screen, 3);

    zui_fill(s, t->area, ZUI_VOID);
    zui_fill_rrect(s, L->card, rad, ZUI_PANEL, 255);
    zui_stroke_rrect(s, L->card, rad, 1, ZUI_GOLD, 255);

    /* the text */
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

    /* the scroll bar */
    zui_fill_rrect(s, L->bar, L->bar.w / 2, ZUI_HAIRLINE, 255);
    {
        zui_rect_t th = L->bar;
        th.h = (int32_t) ((int64_t) L->bar.h * v.thumb256 / 256);
        if (th.h < L->bar.w) th.h = L->bar.w;
        th.y = L->bar.y + (int32_t) ((int64_t) (L->bar.h - th.h) * v.scroll256 / 256);
        zui_fill_rrect(s, th, L->bar.w / 2, ZUI_EDGE, 255);
    }

    /* hint */
    {
        zui_rect_t h = L->hint;
        int32_t base = h.y + asc;
        zui_text_fit(s, t->font, h.x, base, h.w, v.hint == TERMS_HINT_READY ? ZUI_OK : ZUI_MUTED,
                     HINT[v.hint]);
    }

    /* buttons */
    zui_stroke_rrect(s, L->decline, ctl, 1, ZUI_EDGE, 255);
    label(s, t, L->decline, ZUI_TEXT, "Decline");
    if (v.can_accept) {
        zui_fill_rrect(s, L->agree, ctl, ZUI_GOLD, 255);
        label(s, t, L->agree, ZUI_VOID, "I agree");
    } else {
        zui_fill_rrect(s, L->agree, ctl, ZUI_RAISED, 255);
        label(s, t, L->agree, ZUI_FAINT, "I agree");
    }
    if (t->result == ZUI_TERMS_OPEN) {
        if (t->focus == ZUI_TERMS_FOCUS_DECLINE) focus_ring(s, t, L->decline);
        if (t->focus == ZUI_TERMS_FOCUS_AGREE) focus_ring(s, t, L->agree);
    }
}
