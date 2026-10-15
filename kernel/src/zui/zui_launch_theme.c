/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_launch_theme.c — every app's launch screen, live, in its window. See
 * zui_launch_theme.h. */
#include "zui_launch_theme.h"

#define NONE ZUI_WM_MAX

static void copy_name(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    if (s)
        for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static zui_lt_slot_t *slot_for(zui_launch_theme_t *t, uint32_t id)
{
    if (!id) return 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++)
        if (t->slot[i].used && t->slot[i].id == id) return &t->slot[i];
    return 0;
}

static uint32_t free_slot(const zui_launch_theme_t *t)
{
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++)
        if (!t->slot[i].used) return i;
    return NONE;
}

/* Hand every waiting audible cue to the shell (LT5: silent ones are dropped). */
static void drain(zui_launch_theme_t *t, zui_lt_slot_t *sl)
{
    zxl_event_t ev;
    while (zxl_launch_next(&sl->launch, &ev))
        if (ev.audible && ev.kind != ZXL_EV_LIGHT && t->shell.play)
            t->shell.play(t->shell.ctx, sl->id, sl->theme, &ev);
}

/* ---- the pictures: the icon on top, the five logos on the tiles ---------- */
static void draw_icon(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    zui_lt_ref_t *r = ctx;
    zui_launch_theme_t *t = r->owner;
    zui_lt_slot_t *sl = &t->slot[r->slot];
    if (t->shell.icon)
        t->shell.icon(t->shell.ctx, sl->app, zxl_icon_ink(sl->system_app), s, box, lit);
}

static void draw_logo(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    zui_lt_ref_t *r = ctx;
    zui_launch_theme_t *t = r->owner;
    zui_lt_slot_t *sl = &t->slot[r->slot];
    if (t->shell.logo) t->shell.logo(t->shell.ctx, r->part, s, box, lit);
    /* LT3: the tick sounds as the tile's gold comes up */
    if (sl->used && sl->id && lit >= 128u && zxl_launch_lit(&sl->launch, r->part) == 0)
        drain(t, sl);
}

/* ---- window manager hooks ------------------------------------------------ */
static void start(zui_launch_theme_t *t, uint32_t k, uint32_t id, const char *app,
                  const zxl_theme_t *theme, bool system_app, bool warm)
{
    zui_lt_slot_t *sl = &t->slot[k];
    sl->used = true;
    sl->id = id;
    sl->system_app = system_app;
    copy_name(sl->app, sizeof sl->app, app);
    sl->theme = theme ? theme : zxl_theme_for(app, 1);
    zxl_launch_init(&sl->launch, sl->theme, ZUI_LT_LOGOS, &t->prefs, warm ? 1 : 0);
}

static void on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    zui_launch_theme_t *t = ctx;
    if (open) {
        if (t->pending != NONE) {
            t->slot[t->pending].id = id; /* zui_launch_theme_open's window */
        } else {
            /* opened by someone else: its sounds still follow the app (LT7) */
            uint32_t k = free_slot(t);
            zui_wm_window_t *w = zui_wm_get(wm, id);
            if (k != NONE && w) start(t, k, id, w->app, 0, false, false);
        }
    } else {
        zui_lt_slot_t *sl = slot_for(t, id);
        if (sl) sl->used = false;
    }
    if (t->prev_on_window) t->prev_on_window(wm, id, open, t->prev_on_window_ctx);
}

static void on_cue(zui_wm_t *wm, uint32_t id, int cue, void *ctx)
{
    zui_launch_theme_t *t = ctx;
    zui_lt_slot_t *sl = slot_for(t, id);
    if (sl) {
        if (cue == ZUI_CUE_END) zxl_launch_finish(&sl->launch); /* LT4 */
        /* BEGIN plays what zxl_launch_init queued (LT2); MIDDLE is the mark
         * lighting, which is silent: the ticks already told it */
        drain(t, sl);
    }
    if (t->prev_cue) t->prev_cue(wm, id, cue, t->prev_cue_ctx);
}

/* LT8: while the app is built after its blocks are in, the bar above the
 * mark breathes: the time left is not known, so no number is shown */
static void launch_draw(zui_surface_t *s, zui_rect_t area, const zui_wm_window_t *w,
                        zui_launch_state_t st, uint32_t tick, void *ctx)
{
    zui_launch_theme_t *t = ctx;
    zui_lt_slot_t *sl = slot_for(t, w->id);
    if (sl && sl->launch.fed && sl->launch.rest == ZXL_UNKNOWN && st.light >= 256u)
        st.release = ZUI_BAR_UNKNOWN;
    if (t->prev_launch_draw)
        t->prev_launch_draw(s, area, w, st, tick, t->prev_launch_ctx);
    else
        zui_launch_draw(s, area, 256, &w->launch, st, tick);
}

/* ---- public ---------------------------------------------------------------- */
void zui_launch_theme_attach(zui_launch_theme_t *t, zui_wm_t *wm, const zui_lt_shell_t *shell,
                             const zxl_prefs_t *prefs)
{
    if (!t || !wm) return;
    uint8_t *b = (uint8_t *) t;
    for (uint32_t i = 0; i < sizeof *t; i++) b[i] = 0;
    t->wm = wm;
    if (shell) t->shell = *shell;
    t->prefs = prefs ? *prefs : zxl_prefs_default();
    t->pending = NONE;
    t->prev_on_window = wm->on_window;
    t->prev_on_window_ctx = wm->on_window_ctx;
    t->prev_cue = wm->cue;
    t->prev_cue_ctx = wm->cue_ctx;
    wm->on_window = on_window;
    wm->on_window_ctx = t;
    wm->cue = on_cue;
    wm->cue_ctx = t;
    t->prev_launch_draw = wm->launch_draw;
    t->prev_launch_ctx = wm->launch_ctx;
    wm->launch_draw = launch_draw;
    wm->launch_ctx = t;
}

void zui_launch_theme_prefs(zui_launch_theme_t *t, const zxl_prefs_t *prefs)
{
    if (t && prefs) t->prefs = *prefs;
}

uint32_t zui_launch_theme_open(zui_launch_theme_t *t, const char *app, const char *title,
                               const char *launch_name, bool system_app, bool warm,
                               const zui_wm_client_t *client)
{
    if (!t || !t->wm || !app) return 0;
    uint32_t k = free_slot(t);
    if (k == NONE) return 0;
    const zxl_theme_t *theme = 0;
    if (launch_name && launch_name[0]) theme = zxl_theme_pick(launch_name);
    start(t, k, 0, app, theme, system_app, warm);

    zui_lt_slot_t *sl = &t->slot[k];
    zui_launch_t l;
    uint8_t *b = (uint8_t *) &l;
    for (uint32_t i = 0; i < sizeof l; i++) b[i] = 0;
    for (uint32_t i = 0; i <= ZUI_LT_LOGOS; i++) {
        sl->ref[i].owner = t;
        sl->ref[i].slot = k;
        sl->ref[i].part = i ? i - 1u : 0u;
        l.ctx[i] = &sl->ref[i];
    }
    l.mark = draw_icon;
    for (uint32_t i = 0; i < ZUI_LT_LOGOS; i++) l.parts[i] = draw_logo;
    l.n = ZUI_LT_LOGOS;

    t->pending = k; /* on_window binds the new id to this slot */
    uint32_t id = zui_wm_open(t->wm, app, title, &l, client);
    t->pending = NONE;
    if (!id) {
        sl->used = false;
        return 0;
    }
    if (warm) zui_wm_progress(t->wm, id, 256u); /* already loaded: straight to the doors */
    return id;
}

int zui_launch_theme_load(zui_launch_theme_t *t, uint32_t id, uint32_t phase, uint32_t blocks_done,
                          uint32_t blocks_total)
{
    zui_lt_slot_t *sl = t ? slot_for(t, id) : 0;
    if (!sl) return -1;
    zxl_launch_t *l = &sl->launch;
    if (phase == ZUI_LT_FAILED) { /* never the end */
        if (l->warm || l->failed || l->phase == ZXL_PH_OPEN) return -1;
        zxl_launch_fail(l);
        return 0;
    }
    if (zxl_launch_blocks(l, phase == ZUI_LT_DONE ? blocks_total : blocks_done, blocks_total))
        return -1;
    drain(t, sl);
    /* the bar moves only with verified blocks; only "done" reaches 256,
     * where the window manager's END cue finishes the launch (LT4) */
    uint32_t p = zxl_launch_screen_p(l);
    zui_wm_progress(t->wm, id, phase == ZUI_LT_DONE ? 256u : p > 255u ? 255u : p);
    return 0;
}

const zxl_theme_t *zui_launch_theme_of(const zui_launch_theme_t *t, uint32_t id)
{
    if (!t) return 0;
    zui_lt_slot_t *sl = slot_for((zui_launch_theme_t *) t, id);
    return sl ? sl->theme : 0;
}
