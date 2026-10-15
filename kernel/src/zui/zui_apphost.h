/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_apphost.h — the desktop session's app opener: every app opens on its
 * own launch screen, and the screen moves only as fast as the real load.
 *
 * The owner's rule: loading bars track real progress, never faked, and each
 * app's begin / middle / end sounds play at the speed of the real load. The
 * pieces exist: zui_launch_theme draws and sounds the screen, zxm_open
 * receives an app from its recipe through zxvd. This is what joins them for
 * a desktop session.
 *
 *   AH1 ATTACH. zui_ah_init attaches the launch screens to the window
 *       manager (zui_launch_theme_attach, so every window, however it was
 *       opened, gets its app's screen and sounds) and keeps the zxvd link
 *       the opens use. It chains the window manager's on_window hook the
 *       same way the launch screens do.
 *   AH2 OPEN FROM A RECIPE. zui_ah_open opens the app's window on its launch
 *       screen (not warm), then starts a zxm_open_t on the recipe. Every
 *       answer goes through a three-line adapter onto zui_launch_theme_load
 *       for that window: the start's 0 of N, each step's verified blocks,
 *       and DONE only when the app zxvd built is the one the recipe named.
 *   AH3 PUMP. zui_ah_pump steps every running open once; the desktop calls it
 *       once a frame, beside zui_wm_tick and zui_ls_pump. Nothing is asked of
 *       zxvd between frames, so a frame never waits on more than one step
 *       per opening app.
 *   AH4 READY. An open that finishes tells done(ok = true) with the path the
 *       app was written to; the window stays and its doors open on the END
 *       cue, as on every launch.
 *   AH5 FAILED. An open that fails (a refused recipe, a broken answer, a
 *       sender not on the trust list) tells done(ok = false) with why, in
 *       zxvd's words where it gave them, and closes the window: the file at
 *       `out` is never offered to run. The screen never plays its end.
 *   AH6 CLOSED WHILE LOADING. Closing a window whose app is still loading
 *       cancels the receive (fab.receive.cancel), so zxvd holds no job for
 *       a window nobody has; done(ok = false) says it was closed.
 *   AH7 WARM. zui_ah_open_warm opens an app that is already on disk: no
 *       fetch, the screen plays only its end (zui_launch_theme.h LT1).
 *
 * At most ZUI_WM_MAX opens at once (one per window). Integer only, no
 * allocation: the caller owns the struct, the window manager, the link. */
#ifndef ZXV_ZUI_APPHOST_H
#define ZXV_ZUI_APPHOST_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_launch_theme.h"
#include "zxm_open.h"

#define ZUI_AH_PATH_MAX 256u

/* AH4/AH5/AH6: an open ended. ok: the app is at `out`; else why says what
 * went wrong. Called once per zui_ah_open that returned a window. */
typedef void (*zui_ah_done_fn)(void *ctx, uint32_t window, bool ok, const char *out,
                               const char *why);

typedef struct zui_apphost zui_apphost_t;

typedef struct {
    zui_apphost_t *host;
    uint32_t window; /* 0 = free */
    zxm_open_t open;
    char out[ZUI_AH_PATH_MAX];
} zui_ah_slot_t;

struct zui_apphost {
    zui_wm_t *wm;
    zui_launch_theme_t lt;
    zxm_link_t *link;
    zui_ah_done_fn done;
    void *done_ctx;
    zui_ah_slot_t slot[ZUI_WM_MAX];
    void (*prev_on_window)(zui_wm_t *wm, uint32_t id, bool open, void *ctx);
    void *prev_on_window_ctx;
};

/* AH1. shell, prefs as zui_launch_theme_attach; link may be NULL for a
 * session that only opens warm apps. */
void zui_ah_init(zui_apphost_t *h, zui_wm_t *wm, const zui_lt_shell_t *shell,
                 const zxl_prefs_t *prefs, zxm_link_t *link, zui_ah_done_fn done, void *ctx);

/* AH2. app, title, launch_name, system_app, client as zui_launch_theme_open;
 * recipe, out, trust, ntrust, airgap, step as zxm_open_start. Returns the
 * window id, or 0 when nothing was opened (no free window, no link, a bad
 * argument: done is not called). A start zxvd refuses returns the window
 * id after AH5 has already run, so done has been told. */
uint32_t zui_ah_open(zui_apphost_t *h, const char *app, const char *title, const char *launch_name,
                     bool system_app, const zui_wm_client_t *client, const char *recipe,
                     const char *out, const char *const *trust, uint32_t ntrust, bool airgap,
                     uint32_t step);

/* AH7. Returns the window id, or 0. */
uint32_t zui_ah_open_warm(zui_apphost_t *h, const char *app, const char *title,
                          const char *launch_name, bool system_app, const zui_wm_client_t *client);

/* AH3. Returns how many opens are still loading. */
uint32_t zui_ah_pump(zui_apphost_t *h);

/* Whether window id is still loading. */
bool zui_ah_loading(const zui_apphost_t *h, uint32_t window);

/* The launch screens, for zui_launch_theme_prefs and zui_launch_theme_of. */
static inline zui_launch_theme_t *zui_ah_theme(zui_apphost_t *h)
{
    return &h->lt;
}

#endif /* ZXV_ZUI_APPHOST_H */
