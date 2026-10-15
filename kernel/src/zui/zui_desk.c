/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_desk.c — the desktop chrome every ZXV screen shares. See zui_desk.h. */
#include "zui_desk.h"
#include "zui_tokens.h"

static void scopy(char *d, const char *s, uint32_t cap)
{
    uint32_t i = 0;
    if (!cap) return;
    for (; s && s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}

int32_t zui_desk_pt(const zui_desk_t *d, int32_t pt)
{
    return (int32_t) (((int64_t) pt * (int64_t) d->scale_q8 + 128) >> 8);
}

zui_rect_t zui_desk_bar(const zui_desk_t *d)
{
    return (zui_rect_t){0, 0, d->w, zui_desk_pt(d, ZUI_DESK_BAR_PT)};
}

zui_rect_t zui_desk_rail(const zui_desk_t *d)
{
    int32_t top = zui_desk_pt(d, ZUI_DESK_BAR_PT);
    return (zui_rect_t){0, top, zui_desk_pt(d, ZUI_DESK_RAIL_PT), d->h - top};
}

zui_rect_t zui_desk_area(const zui_desk_t *d)
{
    zui_rect_t rail = zui_desk_rail(d);
    return (zui_rect_t){rail.w, rail.y, d->w - rail.w, rail.h};
}

/* the rail's slots: 0 is Apps, app i is slot i + 1 */
static zui_rect_t slot(const zui_desk_t *d, uint32_t k)
{
    zui_rect_t rail = zui_desk_rail(d);
    int32_t t = zui_desk_pt(d, ZUI_DESK_TILE_PT);
    int32_t step = t + zui_desk_pt(d, ZUI_S5); /* the name sits in the gap */
    return (zui_rect_t){(rail.w - t) / 2, rail.y + zui_desk_pt(d, ZUI_S4) + (int32_t) k * step, t,
                        t};
}

zui_rect_t zui_desk_tile(const zui_desk_t *d, uint32_t i)
{
    return slot(d, i + 1u);
}

zui_rect_t zui_desk_apps_tile(const zui_desk_t *d)
{
    return slot(d, 0);
}

zui_rect_t zui_desk_switch(const zui_desk_t *d, bool spaces)
{
    int32_t hw = zui_desk_pt(d, 96), h = zui_desk_pt(d, 28);
    zui_rect_t bar = zui_desk_bar(d);
    int32_t x0 = d->w / 2 - hw, y = (bar.h - h) / 2;
    return (zui_rect_t){spaces ? x0 + hw : x0, y, hw, h};
}

static void on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    zui_desk_t *d = (zui_desk_t *) ctx;
    (void) wm;
    if (open) return;
    for (uint32_t i = 0; i < d->napps; i++)
        if (d->win_of[i] == id) d->win_of[i] = 0;
}

static void wm_setup(zui_desk_t *d, bool touch)
{
    zui_rect_t a = zui_desk_area(d);
    zui_wm_init(&d->wm, a.w, a.h, touch);
    /* The rail is the desk's launcher, so the window manager keeps its strip
     * for now; moving the rail onto the astrolabe dock (WM9) is a follow-up. */
    zui_wm_use_dock(&d->wm, false);
    d->wm.text = d->f.body;
    d->wm.on_window = on_window;
    d->wm.on_window_ctx = d;
}

void zui_desk_init(zui_desk_t *d, int32_t w, int32_t h, uint32_t scale_q8, bool touch,
                   const zui_desk_faces_t *faces)
{
    uint8_t *p = (uint8_t *) d;
    for (uint32_t i = 0; i < sizeof *d; i++) p[i] = 0;
    d->w = w;
    d->h = h;
    d->scale_q8 = scale_q8 ? scale_q8 : 256u;
    if (faces) d->f = *faces;
    d->hover = -1;
    d->press = -1;
    d->act_app = -1;
    wm_setup(d, touch);
}

void zui_desk_resize(zui_desk_t *d, int32_t w, int32_t h, uint32_t scale_q8)
{
    d->w = w;
    d->h = h;
    if (scale_q8) d->scale_q8 = scale_q8;
    zui_rect_t a = zui_desk_area(d);
    zui_wm_resize(&d->wm, a.w, a.h);
}

int32_t zui_desk_add(zui_desk_t *d, const char *name, const char *mark)
{
    if (d->napps >= ZUI_DESK_APPS) return -1;
    d->app[d->napps].name = name;
    d->app[d->napps].mark = mark;
    d->app[d->napps].icon = 0;
    d->app[d->napps].icon_len = 0;
    d->win_of[d->napps] = 0;
    return (int32_t) d->napps++;
}

void zui_desk_status(zui_desk_t *d, const char *text)
{
    scopy(d->status, text, sizeof d->status);
}

void zui_desk_icon(zui_desk_t *d, uint32_t app, const uint8_t *zxvg, uint32_t len)
{
    if (app >= d->napps) return;
    bool ok = zxvg && len && zui_zxvg_check(zxvg, len) == 0;
    d->app[app].icon = ok ? zxvg : 0;
    d->app[app].icon_len = ok ? len : 0;
}

void zui_desk_use_work(zui_desk_t *d, zui_vec_work_t *work)
{
    d->work = work;
}

uint32_t zui_desk_open(zui_desk_t *d, uint32_t app, const char *title, const zui_wm_client_t *c)
{
    if (app >= d->napps) return 0;
    if (d->win_of[app] && zui_wm_get(&d->wm, d->win_of[app])) {
        zui_wm_focus(&d->wm, d->win_of[app]);
        return d->win_of[app];
    }
    uint32_t id = zui_wm_open(&d->wm, d->app[app].name, title ? title : d->app[app].name,
                              (const zui_launch_t *) 0, c);
    d->win_of[app] = id;
    return id;
}

int32_t zui_desk_front_app(const zui_desk_t *d)
{
    uint32_t f = zui_wm_front(&d->wm);
    if (!f) return -1;
    for (uint32_t i = 0; i < d->napps; i++)
        if (d->win_of[i] == f) return (int32_t) i;
    return -1;
}

static bool in(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static int32_t tile_at(const zui_desk_t *d, int32_t x, int32_t y)
{
    int32_t name = zui_desk_pt(d, ZUI_S5) - zui_desk_pt(d, ZUI_S1); /* the name counts */
    zui_rect_t a = zui_desk_apps_tile(d);
    a.h += name;
    if (in(a, x, y)) return ZUI_DESK_APPS_TILE;
    for (uint32_t i = 0; i < d->napps; i++) {
        zui_rect_t t = zui_desk_tile(d, i);
        t.h += name;
        if (in(t, x, y)) return (int32_t) i;
    }
    return -1;
}

bool zui_desk_event(zui_desk_t *d, const zui_event_t *e)
{
    d->act = ZUI_DESK_NONE;
    d->act_app = -1;
    bool ptr = e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_PTR_MOVE || e->type == ZUI_EV_PTR_UP;
    zui_rect_t a = zui_desk_area(d);
    /* a drag that began on a window keeps going to the window manager */
    bool wm_drag = d->wm.drag != 0;
    if (ptr && !wm_drag && !in(a, e->x, e->y)) {
        int32_t t = tile_at(d, e->x, e->y);
        if (e->type == ZUI_EV_PTR_MOVE) {
            d->hover = t;
            return t != -1;
        }
        if (e->type == ZUI_EV_PTR_DOWN && e->button == ZUI_BTN_LEFT) {
            d->press = t;
            if (t != -1) return true;
            if (in(zui_desk_switch(d, true), e->x, e->y)) {
                d->act = ZUI_DESK_SPACES;
                return true;
            }
            if (in(zui_desk_switch(d, false), e->x, e->y)) {
                d->act = ZUI_DESK_HOME;
                return true;
            }
            return in(zui_desk_bar(d), e->x, e->y) || in(zui_desk_rail(d), e->x, e->y);
        }
        if (e->type == ZUI_EV_PTR_UP) {
            bool used = d->press != -1;
            if (t == ZUI_DESK_APPS_TILE && t == d->press) d->act = ZUI_DESK_LIBRARY;
            if (t >= 0 && t == d->press) {
                d->act = ZUI_DESK_LAUNCH;
                d->act_app = t;
            }
            d->press = -1;
            return used;
        }
        return false;
    }
    if (ptr && in(a, e->x, e->y)) d->hover = -1;
    zui_event_t w = *e;
    if (ptr || e->type == ZUI_EV_SCROLL) {
        w.x -= a.x;
        w.y -= a.y;
    }
    return zui_wm_event(&d->wm, &w);
}

void zui_desk_tick(zui_desk_t *d, uint32_t ms)
{
    zui_wm_tick(&d->wm, ms);
}

/* ---- drawing ------------------------------------------------------------ */

static int32_t text_mid(const zui_text_t *t, zui_rect_t r)
{
    /* the baseline that centres the cap height in r */
    int32_t asc = zui_text_ascent(t);
    int32_t cap = (asc * 7) / 10;
    return r.y + (r.h + cap) / 2;
}

static void text_centre(zui_surface_t *s, const zui_text_t *t, zui_rect_t r, uint32_t rgb,
                        const char *str)
{
    if (!t || !t->font || !str) return;
    int32_t w = zui_text_width(t, str);
    zui_text(s, t, r.x + (r.w - w) / 2, text_mid(t, r), rgb, str);
}

/* The ZXV mark: a gold disc engraved with a ring, drawn as vector shapes. */
static void mark(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r)
{
    zui_rect_t o = {cx - r, cy - r, 2 * r, 2 * r};
    zui_fill_rrect(s, o, r, ZUI_GOLD, 255);
    int32_t ri = (r * 5) / 8;
    zui_rect_t in_r = {cx - ri, cy - ri, 2 * ri, 2 * ri};
    zui_stroke_rrect(s, in_r, ri, r / 6 > 1 ? r / 6 : 1, ZUI_VOID, 255);
    int32_t rd = r / 4 > 1 ? r / 4 : 1;
    zui_fill_rrect(s, (zui_rect_t){cx - rd, cy - rd, 2 * rd, 2 * rd}, rd, ZUI_VOID, 255);
}

void zui_desk_draw_bar(zui_desk_t *d, zui_surface_t *s)
{
    zui_rect_t bar = zui_desk_bar(d);
    zui_fill(s, bar, ZUI_PANEL);
    zui_hline(s, 0, bar.y + bar.h - 1, bar.w, 1, ZUI_HAIRLINE);
    int32_t r = zui_desk_pt(d, 10);
    int32_t cx = zui_desk_pt(d, ZUI_S4) + r;
    mark(s, cx, bar.h / 2, r);
    if (d->f.title && d->f.title->font)
        zui_text(s, d->f.title, cx + r + zui_desk_pt(d, ZUI_S2), text_mid(d->f.title, bar),
                 ZUI_TEXT, "ZXV");
    /* D1: the switch, carved into the bar; the chosen half is raised */
    zui_rect_t l = zui_desk_switch(d, false), rr = zui_desk_switch(d, true);
    zui_rect_t whole = {l.x, l.y, l.w + rr.w, l.h};
    int32_t rad = zui_desk_pt(d, ZUI_R_CONTROL);
    zui_fill_rrect(s, whole, rad, ZUI_VOID, 255);
    zui_engrave(s, whole, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
    zui_rect_t on = d->spaces ? rr : l;
    int32_t inset = zui_desk_pt(d, 2);
    zui_fill_rrect(s, (zui_rect_t){on.x + inset, on.y + inset, on.w - 2 * inset, on.h - 2 * inset},
                   rad - inset, ZUI_RAISED, 255);
    text_centre(s, d->f.body, l, d->spaces ? ZUI_MUTED : ZUI_TEXT, "Desktop");
    text_centre(s, d->f.body, rr, d->spaces ? ZUI_TEXT : ZUI_MUTED, "Spaces");
    if (d->status[0] && d->f.caption && d->f.caption->font) {
        int32_t w = zui_text_width(d->f.caption, d->status);
        zui_text(s, d->f.caption, d->w - zui_desk_pt(d, ZUI_S4) - w, text_mid(d->f.caption, bar),
                 ZUI_MUTED, d->status);
    }
}

static void tile_name(zui_desk_t *d, zui_surface_t *s, zui_rect_t t, const char *n, bool hot)
{
    if (!d->f.caption || !d->f.caption->font) return;
    zui_rect_t r = zui_desk_rail(d);
    int32_t max = r.w - zui_desk_pt(d, ZUI_S2);
    int32_t w = zui_text_width(d->f.caption, n);
    int32_t x = w > max ? zui_desk_pt(d, ZUI_S1) : (r.w - w) / 2;
    zui_text_fit(s, d->f.caption, x, t.y + t.h + zui_desk_pt(d, 14), max,
                 hot ? ZUI_TEXT : ZUI_MUTED, n);
}

/* D2: the Apps tile, nine carved squares (gold while the library is open) */
static void apps_tile(zui_desk_t *d, zui_surface_t *s)
{
    zui_rect_t t = zui_desk_apps_tile(d);
    int32_t rad = zui_desk_pt(d, ZUI_R_PANEL);
    bool hot = d->hover == ZUI_DESK_APPS_TILE || d->press == ZUI_DESK_APPS_TILE;
    zui_fill_rrect(s, t, rad, hot ? ZUI_RAISED : ZUI_VOID, 255);
    zui_engrave(s, t, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 46);
    if (d->library) {
        int32_t g = zui_desk_pt(d, 2);
        zui_stroke_rrect(s, (zui_rect_t){t.x - g, t.y - g, t.w + 2 * g, t.h + 2 * g}, rad + g, g,
                         ZUI_GOLD, 255);
    }
    int32_t cell = t.w / 7, gap = (t.w - 3 * cell) / 4;
    uint32_t ink = d->library ? ZUI_GOLD : ZUI_TEXT_2;
    for (int32_t k = 0; k < 9; k++) {
        zui_rect_t c = {t.x + gap + (k % 3) * (cell + gap), t.y + gap + (k / 3) * (cell + gap),
                        cell, cell};
        zui_fill_rrect(s, c, cell / 4, ink, 255);
    }
    tile_name(d, s, t, "Apps", hot);
}

static void rail(zui_desk_t *d, zui_surface_t *s)
{
    zui_rect_t r = zui_desk_rail(d);
    zui_fill(s, r, ZUI_PANEL);
    zui_vline(s, r.x + r.w - 1, r.y, r.h, 1, ZUI_HAIRLINE);
    apps_tile(d, s);
    int32_t front = zui_desk_front_app(d);
    int32_t rad = zui_desk_pt(d, ZUI_R_PANEL);
    for (uint32_t i = 0; i < d->napps; i++) {
        zui_rect_t t = zui_desk_tile(d, i);
        if (t.y + t.h > r.y + r.h) break;
        bool hot = d->hover == (int32_t) i || d->press == (int32_t) i;
        zui_fill_rrect(s, t, rad, hot ? ZUI_RAISED : ZUI_VOID, 255);
        zui_engrave(s, t, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 46);
        if (front == (int32_t) i) {
            int32_t g = zui_desk_pt(d, 2);
            zui_stroke_rrect(s, (zui_rect_t){t.x - g, t.y - g, t.w + 2 * g, t.h + 2 * g}, rad + g,
                             g, ZUI_GOLD, 255);
        }
        if (d->app[i].icon && d->work) { /* D2: the app's own vector icon */
            int32_t in2 = zui_desk_pt(d, 6);
            zui_rect_t ib = {t.x + in2, t.y + in2, t.w - 2 * in2, t.h - 2 * in2};
            zui_vec_opts_t o = {0};
            zui_vec_draw_zxvg(s, d->work, d->app[i].icon, d->app[i].icon_len, ib, &o);
        } else {
            text_centre(s, d->f.mark, t, front == (int32_t) i ? ZUI_GOLD : ZUI_TEXT_2,
                        d->app[i].mark);
        }
        if (d->win_of[i]) { /* running: a gold dot at the rail's edge */
            int32_t dr = zui_desk_pt(d, 2);
            int32_t dy = t.y + t.h / 2;
            zui_fill_rrect(s, (zui_rect_t){zui_desk_pt(d, 3), dy - dr, 2 * dr, 2 * dr}, dr,
                           ZUI_GOLD, 255);
        }
        tile_name(d, s, t, d->app[i].name, hot);
    }
}

void zui_desk_draw(zui_desk_t *d, zui_surface_t *s, uint32_t tick)
{
    zui_unclip(s);
    zui_fill(s, (zui_rect_t){0, 0, d->w, d->h}, ZUI_VOID);
    zui_rect_t a = zui_desk_area(d);
    if (a.w > 0 && a.h > 0) {
        zui_surface_t sub;
        zui_surface_init(&sub, s->px + (int64_t) a.y * s->stride + a.x, a.w, a.h, s->stride);
        zui_wm_draw(&d->wm, &sub, tick);
    }
    zui_unclip(s);
    rail(d, s);
    zui_desk_draw_bar(d, s);
}
