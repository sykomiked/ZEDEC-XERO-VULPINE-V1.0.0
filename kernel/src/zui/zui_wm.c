/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_wm.c — windows in the app space (zui_wm.h). */
#include "zui_wm.h"
#include "zui_motion.h"
#include "zui_tokens.h"

/* zui_palette.h C6: every colour by role, in the active set */
#define C(role) zui_color(wm->colors, ZUI_C_##role)

#define DOUBLE_MS 400u /* two presses on a title this close: maximise */

enum { DRAG_NONE = 0, DRAG_MOVE, DRAG_RESIZE, DRAG_APP };
enum { E_LEFT = 1, E_RIGHT = 2, E_TOP = 4, E_BOTTOM = 8 };
enum {
    HIT_CONTENT = 0,
    HIT_TITLE,
    HIT_MIN,
    HIT_MAX,
    HIT_CLOSE,
    HIT_EDGE,
    HIT_STRIP,
    HIT_NONE,
    HIT_DOCK
};

/* WM10: a 1x size at the chrome's scale */
int32_t zui_wm_px(const zui_wm_t *wm, int32_t v)
{
    uint32_t q = wm->scale_q8 ? wm->scale_q8 : 256u;
    return (int32_t) (((int64_t) v * q + 128) >> 8);
}
#define S(v) zui_wm_px(wm, (v))

static int32_t title_h(const zui_wm_t *wm)
{
    return S(wm->touch ? ZUI_WM_TITLE_T : ZUI_WM_TITLE);
}

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static zui_rect_t meet(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
    int32_t x1 = imin(a.x + a.w, b.x + b.w), y1 = imin(a.y + a.h, b.y + b.h);
    zui_rect_t r = {x0, y0, imax(0, x1 - x0), imax(0, y1 - y0)};
    return r;
}

static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    d[i] = 0;
}

zui_rect_t zui_wm_strip_rect(const zui_wm_t *wm)
{
    int32_t t = imax(0, wm->strip_size);
    switch (wm->strip_pos) {
    case 1:
        return (zui_rect_t){0, 0, imin(t, wm->w), wm->h};
    case 2:
        return (zui_rect_t){imax(0, wm->w - t), 0, imin(t, wm->w), wm->h};
    case 3:
        return (zui_rect_t){0, 0, wm->w, imin(t, wm->h)};
    default:
        return (zui_rect_t){0, imax(0, wm->h - t), wm->w, imin(t, wm->h)};
    }
}

zui_rect_t zui_wm_work(const zui_wm_t *wm)
{
    zui_rect_t s = zui_wm_strip_rect(wm), r = {0, 0, wm->w, wm->h};
    switch (wm->strip_pos) { /* WM6: the screen beside the strip */
    case 1:
        r.x = s.w, r.w = wm->w - s.w;
        break;
    case 2:
        r.w = wm->w - s.w;
        break;
    case 3:
        r.y = s.h, r.h = wm->h - s.h;
        break;
    default:
        r.h = imax(0, wm->h - s.h);
    }
    if (wm->use_dock) { /* WM9: only the seal's band, and only where it would cover */
        const zui_dock_t *d = &wm->dock;
        bool top =
            !d->compact && (d->corner == ZUI_DOCK_TOP_LEFT || d->corner == ZUI_DOCK_TOP_RIGHT);
        int32_t band = top || d->compact ? d->seal : 0;
        r = (zui_rect_t){0, top ? band : 0, wm->w, imax(0, wm->h - band)};
    }
    return r;
}

zui_rect_t zui_wm_content(const zui_wm_t *wm, const zui_wm_window_t *w)
{
    int32_t th = title_h(wm);
    int32_t b = S(1); /* the border */
    zui_rect_t c = {w->r.x + b, w->r.y + th, imax(0, w->r.w - 2 * b), imax(0, w->r.h - th - b)};
    return c;
}

zui_wm_window_t *zui_wm_get(zui_wm_t *wm, uint32_t id)
{
    for (uint32_t i = 0; id && i < ZUI_WM_MAX; i++)
        if (wm->win[i].id == id) return &wm->win[i];
    return 0;
}

static const zui_wm_window_t *cget(const zui_wm_t *wm, uint32_t id)
{
    return zui_wm_get((zui_wm_t *) (uintptr_t) wm, id);
}

static uint32_t slot_of(const zui_wm_t *wm, uint32_t id)
{
    return (uint32_t) (cget(wm, id) - wm->win);
}

void zui_wm_init(zui_wm_t *wm, int32_t w, int32_t h, bool touch)
{
    uint8_t *z = (uint8_t *) wm;
    for (uint32_t i = 0; i < sizeof *wm; i++) z[i] = 0;
    wm->touch = touch;
    wm->next_id = 1;
    wm->strip_size = ZUI_WM_STRIP; /* WM6: along the bottom until placed */
    wm->scale_q8 = 256u;           /* WM10: 1x */
    zui_dock_init(&wm->dock, w, h, touch);
    wm->use_dock = true; /* WM9: the dock unless the strip is asked for */
    zui_wm_resize(wm, w, h);
}

/* WM3: at least the minimum, at most the work area; the title bar stays
 * on screen with a grabbable stretch of it. */
static zui_rect_t limit(const zui_wm_t *wm, zui_rect_t r)
{
    zui_rect_t a = zui_wm_work(wm);
    int32_t th = title_h(wm), keep = S(64);
    r.w = imin(imax(r.w, S(ZUI_WM_MIN_W)), a.w);
    r.h = imin(imax(r.h, S(ZUI_WM_MIN_H)), a.h);
    r.x = imin(imax(r.x, a.x - r.w + keep), a.x + a.w - keep);
    r.y = imin(imax(r.y, a.y), a.y + a.h - th);
    return r;
}

static void fit(zui_wm_t *wm, zui_wm_window_t *w)
{
    if (wm->compact || w->maximised)
        w->r = zui_wm_work(wm);
    else
        w->r = limit(wm, w->r);
}

void zui_wm_resize(zui_wm_t *wm, int32_t w, int32_t h)
{
    wm->w = imax(w, 1);
    wm->h = imax(h, ZUI_WM_STRIP + 1);
    wm->compact = w < 600;
    zui_dock_resize(&wm->dock, wm->w, wm->h, wm->compact);
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++)
        if (wm->win[i].id) fit(wm, &wm->win[i]);
}

static void mru_front(zui_wm_t *wm, uint32_t id)
{
    uint32_t i = 0;
    while (i < wm->nm && wm->mru[i] != id) i++;
    if (i == wm->nm) {
        if (wm->nm >= ZUI_WM_MAX) return;
        wm->nm++;
    }
    for (; i > 0; i--) wm->mru[i] = wm->mru[i - 1];
    wm->mru[0] = id;
}

static void mru_drop(zui_wm_t *wm, uint32_t id)
{
    uint32_t j = 0;
    for (uint32_t i = 0; i < wm->nm; i++)
        if (wm->mru[i] != id) wm->mru[j++] = wm->mru[i];
    wm->nm = j;
}

/* WM9: the dock shows the open windows, most recently used first. */
static void sync(zui_wm_t *wm)
{
    if (!wm->use_dock) return;
    zui_dock_item_t it[ZUI_WM_MAX];
    uint32_t n = 0, front = zui_wm_front(wm);
    for (uint32_t i = 0; i < wm->nm; i++) {
        const zui_wm_window_t *w = cget(wm, wm->mru[i]);
        if (!w) continue;
        zui_dock_item_t *d = &it[n++];
        uint8_t *z = (uint8_t *) d;
        for (uint32_t k = 0; k < sizeof *d; k++) z[k] = 0;
        copy(d->app, sizeof d->app, w->app);
        copy(d->title, sizeof d->title, w->title);
        d->win = w->id;
        d->front = w->id == front;
        d->minimised = w->minimised;
        d->activity = w->phase != ZUI_WIN_OPEN ? (uint8_t) ZUI_DOCK_BUSY : w->activity;
        if (w->phase == ZUI_WIN_LAUNCHING)
            d->progress = w->progress ? (uint32_t) imin((int32_t) w->progress, 255) : 1u;
    }
    zui_dock_set_open(&wm->dock, it, n);
}

static void raise(zui_wm_t *wm, uint32_t slot)
{
    uint32_t i = 0;
    while (i < wm->nz && wm->z[i] != slot) i++;
    if (i == wm->nz) return;
    for (; i + 1u < wm->nz; i++) wm->z[i] = wm->z[i + 1u];
    wm->z[wm->nz - 1u] = slot;
}

uint32_t zui_wm_front(const zui_wm_t *wm)
{
    for (uint32_t i = wm->nz; i > 0; i--) {
        const zui_wm_window_t *w = &wm->win[wm->z[i - 1u]];
        if (!w->minimised) return w->id;
    }
    return 0;
}

static void cue(zui_wm_t *wm, zui_wm_window_t *w, int which)
{
    if (w->cued & (1u << which)) return;
    w->cued |= (uint8_t) (1u << which);
    if (wm->cue) wm->cue(wm, w->id, which, wm->cue_ctx);
}

uint32_t zui_wm_open(zui_wm_t *wm, const char *app, const char *title, const zui_launch_t *launch,
                     const zui_wm_client_t *client)
{
    uint32_t s = 0;
    while (s < ZUI_WM_MAX && wm->win[s].id) s++;
    if (s == ZUI_WM_MAX) return 0;
    zui_wm_window_t *w = &wm->win[s];
    uint8_t *z = (uint8_t *) w;
    for (uint32_t i = 0; i < sizeof *w; i++) z[i] = 0;
    w->id = wm->next_id++;
    if (!wm->next_id) wm->next_id = 1;
    copy(w->app, sizeof w->app, app);
    copy(w->title, sizeof w->title, title && title[0] ? title : app);
    if (launch) w->launch = *launch;
    if (client) w->client = *client;
    w->order = wm->opened++;
    zui_rect_t a = zui_wm_work(wm); /* cascade from the upper left */
    int32_t step = (int32_t) (w->order % 8u) * S(32);
    zui_rect_t r = {a.x + a.w / 8 + step, a.y + a.h / 12 + step, imin(a.w * 3 / 4, S(960)),
                    imin(a.h * 3 / 4, S(680))};
    w->r = w->restore = r;
    fit(wm, w);
    w->restore = w->r;
    wm->z[wm->nz++] = s;
    mru_front(wm, w->id);
    if (wm->on_window) wm->on_window(wm, w->id, true, wm->on_window_ctx);
    cue(wm, w, ZUI_CUE_BEGIN);
    sync(wm);
    return w->id;
}

void zui_wm_progress(zui_wm_t *wm, uint32_t id, uint32_t done256)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w || w->phase != ZUI_WIN_LAUNCHING) return;
    w->progress = done256 > 256u ? 256u : done256;
    if (w->progress >= 128u) cue(wm, w, ZUI_CUE_MIDDLE); /* the parts are in */
    if (w->progress == 256u) {
        cue(wm, w, ZUI_CUE_END);
        w->phase = ZUI_WIN_OPENING; /* WM2: the doors */
    }
    sync(wm);
}

bool zui_wm_close(zui_wm_t *wm, uint32_t id)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w) return false;
    uint32_t s = slot_of(wm, id), j = 0;
    for (uint32_t i = 0; i < wm->nz; i++)
        if (wm->z[i] != s) wm->z[j++] = wm->z[i];
    wm->nz = j;
    mru_drop(wm, id);
    if (wm->drag_id == id) wm->drag = DRAG_NONE;
    zui_wm_client_t c = w->client;
    w->id = 0;
    if (c.closed) c.closed(c.ctx);
    if (wm->on_window) wm->on_window(wm, id, false, wm->on_window_ctx);
    if (wm->switching && wm->pick >= wm->nm) wm->pick = 0;
    sync(wm);
    return true;
}

bool zui_wm_focus(zui_wm_t *wm, uint32_t id)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w) return false;
    w->minimised = false;
    raise(wm, slot_of(wm, id));
    mru_front(wm, id);
    sync(wm);
    return true;
}

bool zui_wm_minimise(zui_wm_t *wm, uint32_t id)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w) return false;
    w->minimised = true;
    if (wm->drag_id == id) wm->drag = DRAG_NONE;
    mru_drop(wm, id); /* out of the way: the window now in front comes first */
    wm->mru[wm->nm++] = id;
    sync(wm);
    return true;
}

bool zui_wm_maximise(zui_wm_t *wm, uint32_t id, bool on)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w || wm->compact) return false;
    if (on && !w->maximised) w->restore = w->r;
    if (!on && w->maximised) w->r = w->restore;
    w->maximised = on;
    fit(wm, w);
    return true;
}

bool zui_wm_move(zui_wm_t *wm, uint32_t id, zui_rect_t r)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w || wm->compact || w->maximised) return false;
    w->r = limit(wm, r);
    return true;
}

uint32_t zui_wm_strip(const zui_wm_t *wm, uint32_t *ids, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) {
        if (!wm->win[i].id) continue;
        uint32_t k = n < cap ? n : cap; /* insert by opening order */
        if (ids && n < cap) {
            while (k > 0 && cget(wm, ids[k - 1u])->order > wm->win[i].order) {
                ids[k] = ids[k - 1u];
                k--;
            }
            ids[k] = wm->win[i].id;
        }
        n++;
    }
    return n;
}

zui_rect_t zui_wm_strip_slot(const zui_wm_t *wm, uint32_t i, uint32_t n)
{
    zui_rect_t b = zui_wm_strip_rect(wm);
    int32_t pad = S(ZUI_S2), g = S(ZUI_S1);
    if (wm->strip_pos == 1 || wm->strip_pos == 2) { /* WM6: down a side edge */
        int32_t room = b.h - 2 * pad, sh = S(40);
        sh = n ? imin(sh, (room - (int32_t) (n - 1u) * g) / (int32_t) n) : 0;
        int32_t total = (int32_t) n * sh + (int32_t) (n ? n - 1u : 0u) * g;
        zui_rect_t r = {b.x + g, b.y + (b.h - total) / 2 + (int32_t) i * (sh + g),
                        imax(0, b.w - 2 * g), imax(0, sh)};
        return r;
    }
    int32_t room = b.w - 2 * pad;
    int32_t sw = n ? imin(S(180), (room - (int32_t) (n - 1u) * g) / (int32_t) n) : 0;
    int32_t total = (int32_t) n * sw + (int32_t) (n ? n - 1u : 0u) * g;
    zui_rect_t r = {(wm->w - total) / 2 + (int32_t) i * (sw + g), b.y + g, sw,
                    imax(0, b.h - 2 * g)};
    return r;
}

void zui_wm_set_scale(zui_wm_t *wm, uint32_t scale_q8)
{
    uint32_t old = wm->scale_q8 ? wm->scale_q8 : 256u;
    uint32_t q = scale_q8 < 128u ? 128u : scale_q8 > 1024u ? 1024u : scale_q8;
    wm->scale_q8 = q;
    wm->strip_size = (int32_t) (((int64_t) wm->strip_size * q + old / 2u) / old);
    zui_dock_t *d = &wm->dock; /* the dock has no scale of its own */
    int32_t seal = (int32_t) (((int64_t) d->seal * q + old / 2u) / old);
    d->icon = S(d->touch ? ZUI_DOCK_ICON_T : ZUI_DOCK_ICON);
    zui_dock_place(d, d->corner, seal, d->autohide);
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) /* the work area changed */
        if (wm->win[i].id) fit(wm, &wm->win[i]);
}

void zui_wm_strip_place(zui_wm_t *wm, int pos, int32_t size)
{
    wm->strip_pos = (uint8_t) (pos >= 0 && pos <= 3 ? pos : 0);
    wm->strip_size = imax(0, size);
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) /* the work area changed */
        if (wm->win[i].id) fit(wm, &wm->win[i]);
}

/* The title bar's buttons, right to left: close, maximise, minimise. */
static zui_rect_t button(const zui_wm_t *wm, const zui_wm_window_t *w, int which)
{
    int32_t th = title_h(wm), k = which == HIT_CLOSE ? 1 : which == HIT_MAX ? 2 : 3;
    if (wm->compact && which == HIT_MIN) k = 2; /* no maximise on a phone */
    zui_rect_t r = {w->r.x + w->r.w - k * th, w->r.y, th, th};
    return r;
}

int zui_wm_hit(const zui_wm_t *wm, int32_t x, int32_t y, uint32_t *id, uint8_t *edges)
{
    if (id) *id = 0;
    if (edges) *edges = 0;
    if (wm->use_dock) { /* WM9: the dock is above the windows */
        int32_t slot;
        zui_dock_slot_t sl;
        int dh = zui_dock_hit(&wm->dock, x, y, &slot);
        if (dh != ZUI_DOCK_HIT_NONE) {
            if (id && dh == ZUI_DOCK_HIT_SLOT && zui_dock_slot(&wm->dock, slot, &sl))
                *id = wm->dock.item[sl.item].win;
            return HIT_DOCK;
        }
    } else if (inside(zui_wm_strip_rect(wm), x, y)) {
        uint32_t ids[ZUI_WM_MAX];
        uint32_t n = zui_wm_strip(wm, ids, ZUI_WM_MAX);
        for (uint32_t i = 0; i < n; i++)
            if (inside(zui_wm_strip_slot(wm, i, n), x, y)) {
                if (id) *id = ids[i];
                return HIT_STRIP;
            }
        return HIT_NONE;
    }
    int32_t g = S(ZUI_WM_GRIP), th = title_h(wm);
    for (uint32_t i = wm->nz; i > 0; i--) {
        const zui_wm_window_t *w = &wm->win[wm->z[i - 1u]];
        if (w->minimised) continue;
        zui_rect_t r = w->r;
        bool fixed = wm->compact || w->maximised;
        zui_rect_t grab = fixed ? r : (zui_rect_t){r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g};
        if (!inside(grab, x, y)) continue;
        if (id) *id = w->id;
        if (!fixed) {
            uint8_t e = 0;
            if (x < r.x + g) e |= E_LEFT;
            if (x >= r.x + r.w - g) e |= E_RIGHT;
            if (y < r.y) e |= E_TOP;
            if (y >= r.y + r.h - g) e |= E_BOTTOM;
            if (e) {
                if (edges) *edges = e;
                return HIT_EDGE;
            }
        }
        if (y < r.y + th) {
            if (inside(button(wm, w, HIT_CLOSE), x, y)) return HIT_CLOSE;
            if (!wm->compact && inside(button(wm, w, HIT_MAX), x, y)) return HIT_MAX;
            if (inside(button(wm, w, HIT_MIN), x, y)) return HIT_MIN;
            return HIT_TITLE;
        }
        return HIT_CONTENT;
    }
    return HIT_NONE;
}

/* WM7: an event for an app, relative to its content. */
static bool forward(zui_wm_t *wm, uint32_t id, const zui_event_t *e)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w || w->phase != ZUI_WIN_OPEN || w->minimised || !w->client.event) return false;
    zui_rect_t c = zui_wm_content(wm, w);
    zui_event_t t = *e;
    bool ptr = e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_PTR_MOVE ||
               e->type == ZUI_EV_PTR_UP || e->type == ZUI_EV_SCROLL ||
               (e->type >= ZUI_EV_TOUCH_BEGIN && e->type <= ZUI_EV_TOUCH_CANCEL);
    if (ptr) {
        t.x -= c.x;
        t.y -= c.y;
    }
    return w->client.event(w->client.ctx, &t, c);
}

static void switch_to_pick(zui_wm_t *wm)
{
    if (wm->pick < wm->nm) zui_wm_focus(wm, wm->mru[wm->pick]);
    wm->switching = false;
}

/* WM5 */
static bool key(zui_wm_t *wm, const zui_event_t *e)
{
    bool cmd = (e->mods & ZUI_MOD_CMD) != 0;
    if (wm->switching) {
        if (e->type == ZUI_EV_KEY_DOWN && e->key == ZUI_KEY_TAB && wm->nm) {
            uint32_t n = wm->nm;
            wm->pick = (e->mods & ZUI_MOD_SHIFT) ? (wm->pick + n - 1u) % n : (wm->pick + 1u) % n;
        } else if (e->type == ZUI_EV_KEY_DOWN && e->key == ZUI_KEY_ESCAPE) {
            wm->switching = false;
        } else if ((e->type == ZUI_EV_KEY_DOWN && e->key == ZUI_KEY_ENTER) ||
                   (e->type == ZUI_EV_KEY_UP && !cmd)) {
            switch_to_pick(wm);
        }
        return true; /* the switcher has the keyboard while it is up */
    }
    if (e->type == ZUI_EV_KEY_DOWN && cmd) {
        uint32_t f = zui_wm_front(wm);
        if (e->key == ZUI_KEY_TAB) {
            if (!wm->nm) return true;
            wm->switching = true;
            wm->pick = wm->nm < 2u ? 0u : (e->mods & ZUI_MOD_SHIFT) ? wm->nm - 1u : 1u;
            return true;
        }
        if (e->key == 'w' || e->key == 'W') return f ? zui_wm_close(wm, f) : true;
        if (e->key == 'm' || e->key == 'M') return f ? zui_wm_minimise(wm, f) : true;
    }
    return forward(wm, zui_wm_front(wm), e);
}

static bool press(zui_wm_t *wm, const zui_event_t *e)
{
    uint32_t id;
    uint8_t edges;
    int h = zui_wm_hit(wm, e->x, e->y, &id, &edges);
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (h == HIT_NONE) return false;
    if (h == HIT_STRIP) { /* WM6 */
        if (id == zui_wm_front(wm))
            zui_wm_minimise(wm, id);
        else
            zui_wm_focus(wm, id);
        return true;
    }
    zui_wm_focus(wm, id);
    if (h == HIT_CLOSE) return zui_wm_close(wm, id);
    if (h == HIT_MIN) return zui_wm_minimise(wm, id);
    if (h == HIT_MAX) return zui_wm_maximise(wm, id, !w->maximised) || true;
    if (h == HIT_TITLE) {
        bool twice = wm->last_title_id == id && e->ms - wm->last_title_ms < DOUBLE_MS;
        wm->last_title_id = twice ? 0 : id;
        wm->last_title_ms = e->ms;
        if (twice) return zui_wm_maximise(wm, id, !w->maximised) || true;
        if (!wm->compact && !w->maximised) {
            wm->drag = DRAG_MOVE;
            wm->drag_id = id;
            wm->dx0 = e->x;
            wm->dy0 = e->y;
            wm->r0 = w->r;
        }
        return true;
    }
    if (h == HIT_EDGE) {
        wm->drag = DRAG_RESIZE;
        wm->edges = edges;
        wm->drag_id = id;
        wm->dx0 = e->x;
        wm->dy0 = e->y;
        wm->r0 = w->r;
        return true;
    }
    wm->drag = DRAG_APP; /* the app keeps the pointer until it lets go */
    wm->drag_id = id;
    forward(wm, id, e);
    return true;
}

static void drag_to(zui_wm_t *wm, int32_t x, int32_t y)
{
    zui_rect_t r = wm->r0;
    int32_t dx = x - wm->dx0, dy = y - wm->dy0;
    if (wm->drag == DRAG_MOVE) {
        r.x += dx;
        r.y += dy;
    } else {
        int32_t mw = S(ZUI_WM_MIN_W), mh = S(ZUI_WM_MIN_H);
        if (wm->edges & E_RIGHT) r.w = imax(mw, r.w + dx);
        if (wm->edges & E_BOTTOM) r.h = imax(mh, r.h + dy);
        if (wm->edges & E_LEFT) { /* the right edge stays where it was */
            int32_t nw = imax(mw, r.w - dx);
            r.x += r.w - nw;
            r.w = nw;
        }
        if (wm->edges & E_TOP) {
            int32_t nh = imax(mh, r.h - dy);
            r.y += r.h - nh;
            r.h = nh;
        }
    }
    zui_wm_move(wm, wm->drag_id, r);
}

/* WM9: what choosing an app in the dock does. */
static uint32_t launch(zui_wm_t *wm, const char *app_in, const char *title_in)
{
    char app[ZUI_DOCK_NAME], title[ZUI_DOCK_NAME]; /* the names may live in the dock */
    copy(app, sizeof app, app_in);
    copy(title, sizeof title, title_in && title_in[0] ? title_in : app_in);
    if (wm->launch_app) return wm->launch_app(wm, app, title, wm->launch_app_ctx);
    return zui_wm_open(wm, app, title, 0, 0);
}

static bool chose(zui_wm_t *wm, const zui_dock_item_t *it)
{
    if (!it) return true;
    if (it->win && zui_wm_get(wm, it->win)) {
        if (it->win == zui_wm_front(wm)) return zui_wm_minimise(wm, it->win);
        return zui_wm_focus(wm, it->win);
    }
    launch(wm, it->app, it->title);
    return true;
}

static bool dock_chose(zui_wm_t *wm)
{
    return chose(wm, zui_dock_chosen(&wm->dock));
}

bool zui_wm_dock_pick(zui_wm_t *wm, uint32_t item)
{
    if (!wm->use_dock || item >= wm->dock.nitem) return false;
    zui_dock_bloom(&wm->dock, false, false);
    return chose(wm, &wm->dock.item[item]);
}

uint32_t zui_wm_launch(zui_wm_t *wm, const char *app, const char *title)
{
    if (!app || !app[0]) return 0;
    for (uint32_t i = 0; i < wm->nm; i++) { /* already open: the most recent window */
        const zui_wm_window_t *w = cget(wm, wm->mru[i]);
        bool same = w != 0;
        for (uint32_t k = 0; same && (w->app[k] || app[k]); k++) same = w->app[k] == app[k];
        if (same) {
            zui_wm_focus(wm, w->id);
            return w->id;
        }
    }
    return launch(wm, app, title);
}

bool zui_wm_event(zui_wm_t *wm, const zui_event_t *e)
{
    if (wm->use_dock && !wm->switching && wm->drag == DRAG_NONE) {
        int r = zui_dock_event(&wm->dock, e);
        if (r == ZUI_DOCK_CHOSE) return dock_chose(wm);
        if (r == ZUI_DOCK_USED) return true;
    }
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
    case ZUI_EV_KEY_UP:
        return key(wm, e);
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT:
        return wm->switching || forward(wm, zui_wm_front(wm), e);
    case ZUI_EV_RESIZE:
        zui_wm_resize(wm, e->x, e->y);
        return true;
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_TOUCH_BEGIN:
        if (wm->switching) {
            wm->switching = false;
            return true;
        }
        return press(wm, e);
    case ZUI_EV_PTR_MOVE:
    case ZUI_EV_TOUCH_MOVE:
        if (wm->drag == DRAG_MOVE || wm->drag == DRAG_RESIZE) {
            drag_to(wm, e->x, e->y);
            return true;
        }
        if (wm->drag == DRAG_APP) return forward(wm, wm->drag_id, e);
        {
            uint32_t id;
            return zui_wm_hit(wm, e->x, e->y, &id, 0) == HIT_CONTENT && forward(wm, id, e);
        }
    case ZUI_EV_PTR_UP:
    case ZUI_EV_TOUCH_END:
    case ZUI_EV_TOUCH_CANCEL: {
        uint8_t d = wm->drag;
        uint32_t id = wm->drag_id;
        wm->drag = DRAG_NONE;
        if (d == DRAG_APP) return forward(wm, id, e);
        return d != DRAG_NONE;
    }
    case ZUI_EV_SCROLL: {
        uint32_t id;
        return zui_wm_hit(wm, e->x, e->y, &id, 0) == HIT_CONTENT && forward(wm, id, e);
    }
    default:
        return false;
    }
}

void zui_wm_tick(zui_wm_t *wm, uint32_t ms)
{
    if (wm->use_dock) zui_dock_tick(&wm->dock, ms);
    bool opened = false;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) {
        zui_wm_window_t *w = &wm->win[i];
        if (!w->id || w->phase != ZUI_WIN_OPENING) continue;
        w->opened_ms += zui_motion_step(ms, ZUI_WM_OPEN_MS); /* MO: reduced motion */
        if (w->opened_ms >= ZUI_WM_OPEN_MS) {
            w->opened_ms = ZUI_WM_OPEN_MS;
            w->phase = ZUI_WIN_OPEN;
            opened = true;
        }
    }
    if (opened) sync(wm);
}

/* ---- drawing ------------------------------------------------------------------ */

/* WM2: the real progress, as the boot screen's three phases. */
static zui_launch_state_t state_of(uint32_t p)
{
    zui_launch_state_t st;
    st.gather = p >= 128u ? 256u : p * 2u;
    st.light = p <= 128u ? 0u : (p - 128u) * 4u > 256u ? 256u : (p - 128u) * 4u;
    st.release = p <= 192u ? 0u : (p - 192u) * 4u;
    st.open = 0; /* set it: zui_launch_draw reads it once release is full */
    return st;
}

static void launch_screen(zui_wm_t *wm, zui_surface_t *s, zui_rect_t area, zui_rect_t clip,
                          const zui_wm_window_t *w, uint32_t tick)
{
    zui_rect_t c = meet(clip, (zui_rect_t){0, 0, s->w, s->h});
    if (c.w <= 0 || c.h <= 0) return;
    zui_clip(s, c);
    zui_launch_state_t st = state_of(w->progress);
    if (wm->launch_draw)
        wm->launch_draw(s, area, w, st, tick, wm->launch_ctx);
    else
        zui_launch_draw(s, area, 256, &w->launch, st, tick);
    zui_unclip(s);
}

static void glyph_x(zui_surface_t *s, zui_rect_t b, uint32_t rgb, int32_t k)
{
    int32_t n = b.h / 3, x0 = b.x + (b.w - n) / 2, y0 = b.y + (b.h - n) / 2;
    for (int32_t i = 0; i < n; i++) {
        zui_fill(s, (zui_rect_t){x0 + i, y0 + i, k, k}, rgb);
        zui_fill(s, (zui_rect_t){x0 + n - 1 - i, y0 + i, k, k}, rgb);
    }
}

static void frame(zui_wm_t *wm, zui_surface_t *s, const zui_wm_window_t *w, bool front,
                  uint32_t tick)
{
    int32_t th = title_h(wm), rad = (wm->compact || w->maximised) ? 0 : S(ZUI_R_PANEL);
    int32_t k = S(2), hl = S(1); /* marks' strokes, the hairline */
    zui_rect_t r = w->r;
    zui_fill_rrect(s, r, rad, C(VOID), 255);
    zui_fill_rrect(s, (zui_rect_t){r.x, r.y, r.w, th + rad}, rad, C(PANEL), 255);
    zui_fill(s, (zui_rect_t){r.x, r.y + th, r.w, rad}, C(VOID));
    zui_hline(s, r.x, r.y + th - hl, r.w, hl, C(HAIRLINE));
    zui_engrave(s, r, rad, C(ENGRAVE_LO), C(ENGRAVE_HI), front ? 90u : 40u);
    uint32_t ink = front ? C(TEXT) : C(MUTED);
    if (wm->text && wm->text->font) {
        int32_t base = r.y + (th + zui_text_ascent(wm->text) + zui_text_descent(wm->text)) / 2;
        int32_t room = r.w - 3 * th - 2 * S(ZUI_S3);
        if (room > 0) zui_text_fit(s, wm->text, r.x + S(ZUI_S3), base, room, ink, w->title);
    }
    zui_rect_t b = button(wm, w, HIT_MIN); /* minimise: a bar */
    zui_hline(s, b.x + b.w / 3, b.y + b.h / 2, b.w / 3, k, C(MUTED));
    if (!wm->compact) { /* maximise: a frame */
        b = button(wm, w, HIT_MAX);
        int32_t m = b.h / 3;
        zui_stroke_rrect(s, (zui_rect_t){b.x + (b.w - m) / 2, b.y + (b.h - m) / 2, m, m}, k, k,
                         C(MUTED), 255);
    }
    glyph_x(s, button(wm, w, HIT_CLOSE), C(MUTED), k);
    zui_rect_t c = zui_wm_content(wm, w);
    if (w->phase == ZUI_WIN_LAUNCHING) {
        launch_screen(wm, s, c, c, w, tick);
        return;
    }
    if (w->client.paint) {
        zui_clip(s, meet(c, (zui_rect_t){0, 0, s->w, s->h}));
        w->client.paint(w->client.ctx, s, c, tick);
        zui_unclip(s);
    }
    if (w->phase == ZUI_WIN_OPENING) { /* the doors part from the centre */
        int32_t half = c.w / 2;
        int32_t d = zui_motion_travel(half, w->opened_ms, ZUI_WM_OPEN_MS);
        zui_rect_t left = {c.x, c.y, half - d, c.h};
        zui_rect_t right = {c.x + half + d, c.y, c.w - half - d, c.h};
        launch_screen(wm, s, (zui_rect_t){c.x - d, c.y, c.w, c.h}, left, w, tick);
        launch_screen(wm, s, (zui_rect_t){c.x + d, c.y, c.w, c.h}, right, w, tick);
    }
}

static void strip(zui_wm_t *wm, zui_surface_t *s, uint32_t tick)
{
    zui_rect_t bar = zui_wm_strip_rect(wm);
    if (!bar.w || !bar.h) return;
    int32_t hl = S(1), rc = S(ZUI_R_CONTROL), p2 = S(ZUI_S2), p3 = S(ZUI_S3);
    zui_fill(s, bar, C(PANEL));
    switch (wm->strip_pos) { /* the hairline on the edge facing the windows */
    case 1:
        zui_vline(s, bar.x + bar.w - hl, 0, wm->h, hl, C(HAIRLINE));
        break;
    case 2:
        zui_vline(s, bar.x, 0, wm->h, hl, C(HAIRLINE));
        break;
    case 3:
        zui_hline(s, 0, bar.y + bar.h - hl, wm->w, hl, C(HAIRLINE));
        break;
    default:
        zui_hline(s, 0, bar.y, wm->w, hl, C(HAIRLINE));
    }
    uint32_t ids[ZUI_WM_MAX];
    uint32_t n = zui_wm_strip(wm, ids, ZUI_WM_MAX), front = zui_wm_front(wm);
    for (uint32_t i = 0; i < n; i++) {
        const zui_wm_window_t *w = cget(wm, ids[i]);
        zui_rect_t r = zui_wm_strip_slot(wm, i, n);
        if (ids[i] == front) zui_fill_rrect(s, r, rc, C(RAISED), 255);
        zui_engrave(s, r, rc, C(ENGRAVE_LO), C(ENGRAVE_HI), 50u);
        if (wm->text && wm->text->font) {
            int32_t base = r.y + (r.h + zui_text_ascent(wm->text) + zui_text_descent(wm->text)) / 2;
            zui_text_fit(s, wm->text, r.x + p2, base, r.w - 2 * p2,
                         w->minimised ? C(FAINT) : C(TEXT_2), w->title);
        }
        if (ids[i] == front) /* WM8: the one gold element */
            zui_hline(s, r.x + p3, r.y + r.h - S(3), r.w - 2 * p3, S(2), C(GOLD));
        if (w->phase == ZUI_WIN_LAUNCHING)
            zui_bar_draw_in(s, (zui_rect_t){r.x + p2, r.y + r.h - S(9), r.w - 2 * p2, S(2)},
                            ZUI_BAR_GATHER, w->progress, tick, 0, 0, wm->colors);
    }
}

static void switcher(zui_wm_t *wm, zui_surface_t *s)
{
    if (!wm->switching || !wm->nm) return;
    int32_t tile = S(160), th = S(72), gap = S(ZUI_S3), n = (int32_t) wm->nm;
    int32_t rp = S(ZUI_R_PANEL), rc = S(ZUI_R_CONTROL);
    int32_t per = imax(1, imin(n, (wm->w - 4 * gap) / (tile + gap)));
    int32_t rows = (n + per - 1) / per;
    int32_t pw = per * (tile + gap) + gap, ph = rows * (th + gap) + gap;
    zui_rect_t a = zui_wm_work(wm);
    zui_rect_t p = {(wm->w - pw) / 2, a.y + (a.h - ph) / 2, pw, ph};
    zui_fill_rrect(s, p, rp, C(PANEL), 245);
    zui_engrave(s, p, rp, C(ENGRAVE_LO), C(ENGRAVE_HI), 90u);
    for (int32_t i = 0; i < n; i++) {
        const zui_wm_window_t *w = cget(wm, wm->mru[i]);
        zui_rect_t t = {p.x + gap + (i % per) * (tile + gap), p.y + gap + (i / per) * (th + gap),
                        tile, th};
        bool chosen = (uint32_t) i == wm->pick;
        zui_fill_rrect(s, t, rc, chosen ? C(RAISED) : C(VOID), 255);
        if (chosen)
            zui_stroke_rrect(s, t, rc, S(2), C(GOLD), 255);
        else
            zui_engrave(s, t, rc, C(ENGRAVE_LO), C(ENGRAVE_HI), 50u);
        if (wm->text && wm->text->font)
            zui_text_fit(s, wm->text, t.x + gap, t.y + t.h / 2 + S(5), t.w - 2 * gap,
                         chosen ? C(TEXT) : C(TEXT_2), w->title);
    }
}

void zui_wm_draw(zui_wm_t *wm, zui_surface_t *s, uint32_t tick)
{
    zui_unclip(s);
    zui_fill(s, wm->use_dock ? (zui_rect_t){0, 0, wm->w, wm->h} : zui_wm_work(wm), C(VOID));
    uint32_t front = zui_wm_front(wm);
    for (uint32_t i = 0; i < wm->nz; i++) {
        const zui_wm_window_t *w = &wm->win[wm->z[i]];
        if (!w->minimised) frame(wm, s, w, w->id == front, tick);
    }
    if (wm->use_dock) {
        if (!wm->dock.text) wm->dock.text = wm->text;
        if (!wm->dock.colors) wm->dock.colors = wm->colors;
        zui_dock_draw(&wm->dock, s);
    } else {
        strip(wm, s, tick);
    }
    switcher(wm, s);
}

void zui_wm_use_dock(zui_wm_t *wm, bool on)
{
    wm->use_dock = on;
    zui_dock_bloom(&wm->dock, false, false);
    sync(wm);
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) /* the work area changed */
        if (wm->win[i].id) fit(wm, &wm->win[i]);
}

bool zui_wm_activity(zui_wm_t *wm, uint32_t id, zui_dock_act_t a)
{
    zui_wm_window_t *w = zui_wm_get(wm, id);
    if (!w || a > ZUI_DOCK_ASK) return false;
    w->activity = (uint8_t) a;
    sync(wm);
    return true;
}
