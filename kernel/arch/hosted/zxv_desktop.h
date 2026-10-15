/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_desktop.h — the ZXV desktop in a window on a hosted system (macOS,
 * Linux, Windows): the shared desktop chrome (kernel/src/zui/zui_desk.h) on
 * a zui present layer (zui_present_host.h), with the built-in apps' native
 * screens (kernel/src/zui/views) in its windows. One look everywhere: the
 * #002911 ground, carved panels, light green ASCW Golden text and one gold
 * accent. Everything on screen is drawn by zui as vector shapes and text;
 * there is no browser and no bitmap.
 *
 *   HD1 ONE LOOP. zxv_desktop_step polls the present layer, hands every
 *       event to the desk (zui_desk_event), acts on what the desk reports
 *       (open or raise an app's window, show Spaces, go back to the
 *       desktop), lets the window manager animate, repaints and presents.
 *       The caller owns the loop: a terminal program calls it with a wait,
 *       an AppKit shell from a 60 Hz timer with wait 0.
 *   HD2 APP WINDOWS. Each app on the rail opens in a zui_wm window whose
 *       client owns its own zui_ui_t, sized to the window's content and laid
 *       out again when the window changes size. The app's model is loaded
 *       when the window opens and its real progress is fed to the launch
 *       screen; a model call that fails shows in the view's own error line.
 *   HD3 ZXVD. Models talk to zxvd over its control socket (zxv_rpc_call,
 *       macos/zxv_rpc_client.h) at cfg.rpc, else $ZXV_RPC. With no socket
 *       the link's send fails (-1) and every view says so in words; nothing
 *       crashes. The bar's status says "zxvd connected" or "zxvd not
 *       running", checked every two seconds. Calls are blocking, bounded by
 *       a timeout: the models are synchronous by design (zxm.h M1).
 *   HD4 SPACES. The bar's Spaces switch shows the five spaces (Home,
 *       Office, Team, Cloud top, Park) as carved cards; choosing one returns
 *       to the desktop with that space chosen.
 *   HD5 HEADLESS. cfg.headless draws into the desktop's own surface with no
 *       present layer, and events come only from zxv_desktop_feed: tests
 *       render the real desktop without a display.
 */
#ifndef ZXV_DESKTOP_H
#define ZXV_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_desk.h"
#include "zui_present.h"
#include "zui_widget.h"

typedef struct {
    const char *backend; /* "metal", "x11", ... ; NULL: the first that opens */
    const char *rpc;     /* zxvd's socket; NULL: $ZXV_RPC; "" none   */
    const char *font;    /* TrueType file; NULL: $ZXV_FONT, then the repo's
                            assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf  */
    const char *mono;    /* NULL: $ZXV_MONO, then ASCW-Golden-Mono.ttf beside
                            the font, then the font itself                   */
    const char *title;   /* the window's title; NULL: "ZXV"          */
    int32_t w_pt, h_pt;  /* the window, points; 0: 1180 x 780         */
    bool headless;       /* HD5: no present layer                     */
    uint32_t scale_q8;   /* headless only: pixels per point x256 (0: 256) */
    /* The system clipboard for every app window's fields (zui_ui_t's
     * clip_set / clip_get); NULL keeps zui's own, inside the desktop. */
    bool (*clip_set)(void *ctx, const char *s, uint32_t n);
    uint32_t (*clip_get)(void *ctx, char *out, uint32_t cap);
    void *clip_ctx;
} zxv_desktop_cfg_t;

typedef struct zxv_desktop zxv_desktop_t;

/* Open the window (or the headless surface). NULL when no present layer
 * opened or memory ran out; *why (may be NULL) then says which. */
zxv_desktop_t *zxv_desktop_open(const zxv_desktop_cfg_t *cfg, const char **why);
/* HD1: one frame. Returns 1 while running, 0 when the person closed the
 * window, -1 when the window or the device went away. */
int zxv_desktop_step(zxv_desktop_t *d, uint32_t wait_ms);
void zxv_desktop_close(zxv_desktop_t *d);

/* A short line in the bar's status in place of the connection's (the
 * shell's own news: "Restarting the swarm"); NULL or "" goes back to it. */
void zxv_desktop_note(zxv_desktop_t *d, const char *text);
/* Use another socket (NULL or "" none) and check it now. */
void zxv_desktop_set_rpc(zxv_desktop_t *d, const char *path);
bool zxv_desktop_connected(const zxv_desktop_t *d);

/* The apps on the rail, in order. */
uint32_t zxv_desktop_apps(const zxv_desktop_t *d);
const char *zxv_desktop_app_name(const zxv_desktop_t *d, uint32_t app);
/* Open or raise an app's window, as pressing its tile does; the window id. */
uint32_t zxv_desktop_launch(zxv_desktop_t *d, uint32_t app);

/* The widget tree of an app's open window (NULL when it has none). */
zui_ui_t *zxv_desktop_app_ui(zxv_desktop_t *d, uint32_t app);

/* Tests and tools: queue an event (screen pixels) for the next step, the
 * surface last drawn, the desk itself, and the present layer (NULL when
 * headless). */
void zxv_desktop_feed(zxv_desktop_t *d, const zui_event_t *e);
const zui_surface_t *zxv_desktop_surface(const zxv_desktop_t *d);
zui_desk_t *zxv_desktop_desk(zxv_desktop_t *d);
zui_present_t *zxv_desktop_present(zxv_desktop_t *d);
/* HD4: the chosen space (0 Home .. 4 Park) and whether Spaces is showing. */
uint32_t zxv_desktop_space(const zxv_desktop_t *d);
bool zxv_desktop_in_spaces(const zxv_desktop_t *d);
/* HD4: where space i's card is on screen while Spaces shows (w 0 if not). */
zui_rect_t zxv_desktop_space_card(const zxv_desktop_t *d, uint32_t i);

#endif /* ZXV_DESKTOP_H */
