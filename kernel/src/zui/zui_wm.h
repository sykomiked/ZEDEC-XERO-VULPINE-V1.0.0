/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_wm.h — windows in the app space: several apps open at once, each in
 * its own window, and a way to switch between them.
 *
 * The window manager owns geometry, stacking, focus, the window frames,
 * the dock (or the strip) of open apps and the switcher. What is inside a window belongs
 * to the app (a zui view): the manager asks it to paint into its content
 * rectangle and hands it the events meant for it.
 *
 *   WM1 WINDOWS stack: the last one raised is on top and has the keyboard.
 *       Up to ZUI_WM_MAX are open at once.
 *   WM2 EACH APP LAUNCHES IN ITS WINDOW. A new window shows the boot
 *       sequence screen (zui_launch_draw) inside its frame, with the app's
 *       own mark large at the top centre where the boot screen has the fox,
 *       and its parts in the row. zui_wm_progress feeds it the app's real
 *       load progress (0..256, as zxapp_load reports it): the lower bar
 *       gathers and lights the parts, the mark lights, the upper bar
 *       releases. At 256 the screen opens like the boot vault's doors,
 *       from the centre out, onto the app. Nothing is faked: a window that
 *       gets no progress keeps gathering. A theme can draw the screen
 *       instead (zui_wm_t.launch_draw), the same state passed in.
 *       Each launch has three sound cues, told to zui_wm_t.cue once each,
 *       on real events only: BEGIN when the window opens, MIDDLE when the
 *       parts are all in (progress reaches 128, the mark lights), END when
 *       the load is done and the doors open. The launch theme plays them
 *       (per app, switchable off in settings); the manager makes no sound.
 *   WM3 MOVE AND RESIZE. Drag the title bar to move, an edge or corner to
 *       resize (never below ZUI_WM_MIN_W x ZUI_WM_MIN_H, and the title bar
 *       never leaves the screen). Double-clicking the title, or its
 *       maximise button, fills the work area and back. The title bar has
 *       minimise, maximise and close.
 *   WM4 PHONES. On a compact screen every window fills the work area and
 *       does not move or resize; the dock, strip and switcher still work.
 *   WM5 SWITCHING. Cmd-Tab shows the switcher: open apps, most recently
 *       used first, with the next one chosen. Tab and Shift-Tab move the
 *       choice; letting go of Cmd, or Enter, switches; Esc stays. Cmd-W
 *       closes the window in front, Cmd-M minimises it.
 *   WM6 THE STRIP (with zui_wm_use_dock(wm, false); the dock of WM9 is the
 *       default) along the bottom shows every open window in the order it
 *       was opened. Pressing one brings it to the front (restoring it when
 *       minimised); pressing the one in front minimises it. A window still
 *       launching shows its gathering bar there too.
 *   WM7 EVENTS. zui_wm_event takes every event. What is not the manager's
 *       goes to an app with coordinates inside its content: pointer events
 *       to the window under the pointer (raising it), keys and text to the
 *       window in front. A launching window gets none.
 *   WM8 Integer only and deterministic: the same events and ticks give the
 *       same pixels. Colours are zui_tokens.h; frames are engraved; the one
 *       gold element is the window in front's mark in the strip (WM9: the
 *       dock's active slot).
 *   WM9 THE DOCK. By default the astrolabe dock (zui_dock.h) stands in
 *       place of the strip (zui_wm_use_dock switches): a seal in a corner
 *       that blooms into rings of the open apps (most recently used first)
 *       and the pinned ones (pin them on zui_wm_t.dock). The work area is
 *       then the whole screen; only a seal in a top corner takes a band of
 *       its height off the top (so title bars stay clear of it), and on a
 *       phone a band of its height off the bottom. Choosing an open app
 *       raises it (restoring it when minimised); choosing the one in front
 *       minimises it; choosing a pinned app that is not open asks
 *       launch_app (NULL: zui_wm_open with no client), which opens its
 *       window on the launch screen of WM2. The dock sees events first, but
 *       not while the switcher is up or a window is being dragged.
 *       zui_wm_activity sets what an app's pip shows; a launching window
 *       shows BUSY and its gathering bar. zui_wm_dock_pick and
 *       zui_wm_launch do what choosing does without a pointer (switch
 *       access, voice control). */
#ifndef ZXV_ZUI_WM_H
#define ZXV_ZUI_WM_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_dock.h"
#include "zui_input.h"
#include "zui_progress.h"

#define ZUI_WM_MAX      16u
#define ZUI_WM_TITLE    32 /* title bar height, pointer screens       */
#define ZUI_WM_TITLE_T  44 /* and touch screens                       */
#define ZUI_WM_STRIP    48 /* the strip of open apps                  */
#define ZUI_WM_GRIP     6  /* how far outside an edge a resize grabs */
#define ZUI_WM_MIN_W    320
#define ZUI_WM_MIN_H    200
#define ZUI_WM_OPEN_MS  360 /* the doors opening (WM2)              */
#define ZUI_WM_NAME_MAX 48u

typedef enum {
    ZUI_WIN_LAUNCHING = 0, /* the launch screen, gathering (WM2) */
    ZUI_WIN_OPENING,       /* loaded: the doors open             */
    ZUI_WIN_OPEN
} zui_win_phase_t;

/* What lives inside a window (a zui view). paint draws into `content`;
 * event gets an event whose x, y are already relative to content and
 * returns whether it used it; closed is told the window went away. */
typedef struct {
    void (*paint)(void *ctx, zui_surface_t *s, zui_rect_t content, uint32_t tick);
    bool (*event)(void *ctx, const zui_event_t *e, zui_rect_t content);
    void (*closed)(void *ctx);
    void *ctx;
} zui_wm_client_t;

typedef struct {
    uint32_t id; /* 0 = a free slot */
    char app[ZUI_WM_NAME_MAX];
    char title[ZUI_WM_NAME_MAX];
    zui_rect_t r, restore; /* the frame; where it goes back to when un-maximised */
    bool maximised, minimised;
    zui_win_phase_t phase;
    uint32_t progress;  /* 0..256 (WM2) */
    uint32_t opened_ms; /* time spent opening, 0..ZUI_WM_OPEN_MS */
    uint32_t order;     /* when it was opened, for the strip */
    uint8_t cued;       /* the cues already told (1 << ZUI_CUE_*) */
    uint8_t activity;   /* WM9: zui_dock_act_t, for the dock's pip */
    zui_launch_t launch;
    zui_wm_client_t client;
} zui_wm_window_t;

typedef struct zui_wm zui_wm_t;
enum { ZUI_CUE_BEGIN = 0, ZUI_CUE_MIDDLE, ZUI_CUE_END }; /* WM2: a launch's sounds */
/* WM2: a theme's launch screen, drawn in place of zui_launch_draw. */
typedef void (*zui_wm_launch_fn)(zui_surface_t *s, zui_rect_t area, const zui_wm_window_t *w,
                                 zui_launch_state_t st, uint32_t tick, void *ctx);

struct zui_wm {
    int32_t w, h; /* the screen, pixels */
    bool compact, touch;
    zui_wm_window_t win[ZUI_WM_MAX];
    uint32_t z[ZUI_WM_MAX], nz;   /* window slots, bottom to top (WM1) */
    uint32_t mru[ZUI_WM_MAX], nm; /* window ids, most recently used first */
    uint32_t next_id, opened;
    /* a pointer drag in progress */
    uint8_t drag;  /* 0 none, 1 move, 2 resize, 3 an app's */
    uint8_t edges; /* resize: 1 left, 2 right, 4 top, 8 bottom */
    uint32_t drag_id;
    int32_t dx0, dy0;
    zui_rect_t r0;
    uint32_t last_title_ms, last_title_id; /* double-click on a title */
    /* the switcher (WM5) */
    bool switching;
    uint32_t pick;          /* index into mru */
    const zui_text_t *text; /* for titles; NULL draws none */
    zui_wm_launch_fn launch_draw;
    void *launch_ctx;
    /* told when a window opens or closes (the launch theme, the shell) */
    void (*on_window)(zui_wm_t *wm, uint32_t id, bool open, void *ctx);
    void *on_window_ctx;
    /* WM2: a launch reached a sound cue (ZUI_CUE_*); NULL plays nothing */
    void (*cue)(zui_wm_t *wm, uint32_t id, int cue, void *ctx);
    void *cue_ctx;
    /* WM9: the dock in place of the strip */
    bool use_dock;
    zui_dock_t dock;
    uint32_t (*launch_app)(zui_wm_t *wm, const char *app, const char *title, void *ctx);
    void *launch_app_ctx;
};

void zui_wm_init(zui_wm_t *wm, int32_t w, int32_t h, bool touch);
void zui_wm_resize(zui_wm_t *wm, int32_t w, int32_t h);
/* WM2: open a window for `app` showing `launch` (copied; NULL: no mark).
 * Returns its id, or 0 when ZUI_WM_MAX are open. */
uint32_t zui_wm_open(zui_wm_t *wm, const char *app, const char *title, const zui_launch_t *launch,
                     const zui_wm_client_t *client);
void zui_wm_progress(zui_wm_t *wm, uint32_t id, uint32_t done256);
bool zui_wm_close(zui_wm_t *wm, uint32_t id);
bool zui_wm_focus(zui_wm_t *wm, uint32_t id); /* raise, restoring if minimised */
bool zui_wm_minimise(zui_wm_t *wm, uint32_t id);
bool zui_wm_maximise(zui_wm_t *wm, uint32_t id, bool on);
bool zui_wm_move(zui_wm_t *wm, uint32_t id, zui_rect_t r); /* WM3 limits apply */
uint32_t zui_wm_front(const zui_wm_t *wm); /* the window with the keyboard, 0 none */
zui_wm_window_t *zui_wm_get(zui_wm_t *wm, uint32_t id);
zui_rect_t zui_wm_work(const zui_wm_t *wm); /* the screen above the strip (WM9: or the dock) */
zui_rect_t zui_wm_content(const zui_wm_t *wm, const zui_wm_window_t *w);
/* Which part of the screen is at (x, y): the window id (0 none) and where
 * on it: 0 content, 1 title, 2 minimise, 3 maximise, 4 close, 5 an edge
 * (with *edges), 6 the strip (with *id the strip's window), 7 nothing,
 * 8 the dock (WM9; with *id the window of the slot there, 0 for none). */
int zui_wm_hit(const zui_wm_t *wm, int32_t x, int32_t y, uint32_t *id, uint8_t *edges);
/* The windows in the strip, in opening order; returns how many (also
 * when the dock is in use). */
uint32_t zui_wm_strip(const zui_wm_t *wm, uint32_t *ids, uint32_t cap);
zui_rect_t zui_wm_strip_slot(const zui_wm_t *wm, uint32_t i, uint32_t n);

bool zui_wm_event(zui_wm_t *wm, const zui_event_t *e); /* WM7 */
void zui_wm_tick(zui_wm_t *wm, uint32_t ms);           /* WM2: the doors */
void zui_wm_draw(zui_wm_t *wm, zui_surface_t *s, uint32_t tick);
/* WM9 */
void zui_wm_use_dock(zui_wm_t *wm, bool on);
bool zui_wm_activity(zui_wm_t *wm, uint32_t id, zui_dock_act_t a);
/* WM9: choose the dock's item (zui_wm_t.dock.item[item]) as a press on its
 * slot would, whether or not the dock is open; it closes. For switch access
 * and voice control. */
bool zui_wm_dock_pick(zui_wm_t *wm, uint32_t item);
/* Raise app's most recent window, else launch it as the dock does (launch_app,
 * else zui_wm_open). Returns the window id, 0 when none could open. */
uint32_t zui_wm_launch(zui_wm_t *wm, const char *app, const char *title);

#endif /* ZXV_ZUI_WM_H */
