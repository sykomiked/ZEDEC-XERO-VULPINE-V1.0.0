/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_apphost.c — the desktop session's app opener (see zui_apphost.h). */
#include "zui_apphost.h"

_Static_assert((int) ZXM_OPEN_FETCH == (int) ZUI_LT_FETCH &&
                   (int) ZXM_OPEN_DONE == (int) ZUI_LT_DONE &&
                   (int) ZXM_OPEN_FAILED == (int) ZUI_LT_FAILED,
               "the opener's phases are the launch screen's (zxm_open.h, LT8)");

static void copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    for (; src && src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = 0;
}

static zui_ah_slot_t *slot_of(zui_apphost_t *h, uint32_t window)
{
    if (!window) return 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++)
        if (h->slot[i].window == window) return &h->slot[i];
    return 0;
}

/* AH2: the three-line adapter, one answer onto the window's screen */
static void on_load(void *ctx, uint32_t phase, uint32_t blocks_done, uint32_t blocks_total)
{
    zui_ah_slot_t *sl = ctx;
    zui_launch_theme_load(&sl->host->lt, sl->window, phase, blocks_done, blocks_total);
}

/* AH4/AH5/AH6: the open ended; the slot is freed before anything is told or
 * closed, so the close below cannot come back to it */
static void end(zui_apphost_t *h, zui_ah_slot_t *sl, bool ok, const char *why, bool close)
{
    uint32_t window = sl->window;
    sl->window = 0;
    if (close) zui_wm_close(h->wm, window);
    if (h->done) h->done(h->done_ctx, window, ok, ok ? sl->out : 0, ok ? 0 : why);
}

/* AH6: chained ahead of the launch screens, so a cancel still reaches the
 * window's screen before it lets the window go */
static void on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    zui_apphost_t *h = ctx;
    zui_ah_slot_t *sl = open ? 0 : slot_of(h, id);
    if (sl && sl->open.state == ZXM_OPEN_RUNNING) {
        zxm_open_cancel(&sl->open);
        end(h, sl, false, "the window was closed while the app was loading", false);
    }
    if (h->prev_on_window) h->prev_on_window(wm, id, open, h->prev_on_window_ctx);
}

void zui_ah_init(zui_apphost_t *h, zui_wm_t *wm, const zui_lt_shell_t *shell,
                 const zxl_prefs_t *prefs, zxm_link_t *link, zui_ah_done_fn done, void *ctx)
{
    if (!h || !wm) return;
    uint8_t *b = (uint8_t *) h;
    for (uint32_t i = 0; i < sizeof *h; i++) b[i] = 0;
    h->wm = wm;
    h->link = link;
    h->done = done;
    h->done_ctx = ctx;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) h->slot[i].host = h;
    zui_launch_theme_attach(&h->lt, wm, shell, prefs);
    h->prev_on_window = wm->on_window;
    h->prev_on_window_ctx = wm->on_window_ctx;
    wm->on_window = on_window;
    wm->on_window_ctx = h;
}

uint32_t zui_ah_open(zui_apphost_t *h, const char *app, const char *title, const char *launch_name,
                     bool system_app, const zui_wm_client_t *client, const char *recipe,
                     const char *out, const char *const *trust, uint32_t ntrust, bool airgap,
                     uint32_t step)
{
    if (!h || !h->wm || !h->link || !recipe || !recipe[0] || !out || !out[0]) return 0;
    zui_ah_slot_t *sl = 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX && !sl; i++)
        if (!h->slot[i].window) sl = &h->slot[i];
    if (!sl) return 0;
    uint32_t window =
        zui_launch_theme_open(&h->lt, app, title, launch_name, system_app, false, client);
    if (!window) return 0;
    sl->window = window;
    copy(sl->out, sizeof sl->out, out);
    int rc =
        zxm_open_start(&sl->open, h->link, recipe, out, trust, ntrust, airgap, step, on_load, sl);
    if (rc == ZXM_E_ARG) { /* nothing was asked: as if never opened */
        sl->window = 0;
        zui_wm_close(h->wm, window);
        return 0;
    }
    if (rc != ZXM_OK) end(h, sl, false, sl->open.why, true);
    return window;
}

uint32_t zui_ah_open_warm(zui_apphost_t *h, const char *app, const char *title,
                          const char *launch_name, bool system_app, const zui_wm_client_t *client)
{
    if (!h || !h->wm) return 0;
    return zui_launch_theme_open(&h->lt, app, title, launch_name, system_app, true, client);
}

uint32_t zui_ah_pump(zui_apphost_t *h)
{
    if (!h) return 0;
    uint32_t loading = 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) {
        zui_ah_slot_t *sl = &h->slot[i];
        if (!sl->window) continue;
        uint32_t st = zxm_open_pump(&sl->open);
        if (st == ZXM_OPEN_RUNNING)
            loading++;
        else if (st == ZXM_OPEN_FINISHED)
            end(h, sl, true, 0, false);
        else
            end(h, sl, false, sl->open.why, true);
    }
    return loading;
}

bool zui_ah_loading(const zui_apphost_t *h, uint32_t window)
{
    return h && slot_of((zui_apphost_t *) h, window) != 0;
}
