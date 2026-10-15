/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_desk.h — the ZXV desktop's own chrome, shared by every place the OS
 * shows a desktop: the bare-metal shell (kernel/src/desktop/zxv_shell.c,
 * on the UEFI GOP or ramfb framebuffer) and the hosted desktop window
 * (kernel/arch/hosted/zxv_desktop.c, on Metal, Vulkan, Direct3D or X11).
 * One look on every screen: the #002911 ground, carved panels, light green
 * text and one gold accent (zui_tokens.h).
 *
 *   D1 THE TOP BAR runs across the screen: the gold ZXV mark and name at the
 *      left, the Desktop / Spaces switch in the centre, a short status line
 *      at the right. Pressing Spaces (or Tab on the bare-metal shell) is
 *      reported as ZUI_DESK_SPACES; the owner decides what Spaces shows.
 *   D2 THE LAUNCHER RAIL runs down the left edge: one carved tile per app,
 *      its mark (one or two letters, drawn in the system face, so it stays
 *      vector) and its name under it. A tile whose app has a window open has
 *      a gold dot; the app in front has a gold ring. Pressing a tile is
 *      reported as ZUI_DESK_LAUNCH with the app's index; the owner opens or
 *      focuses its window.
 *   D3 WINDOWS are zui_wm's (zui_wm.h): the desk gives the window manager
 *      the screen right of the rail and below the bar, translating events
 *      and drawing through a sub-surface, so windows, the strip of open apps
 *      and the switcher behave as they do everywhere else.
 *   D4 Integer only, freestanding, deterministic: the same events give the
 *      same pixels. Sizes are points scaled by scale_q8.
 */
#ifndef ZXV_ZUI_DESK_H
#define ZXV_ZUI_DESK_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_wm.h"

#define ZUI_DESK_APPS   16u
#define ZUI_DESK_BAR_PT 40 /* the top bar's height                 */
#define ZUI_DESK_RAIL_PT 84 /* the launcher rail's width            */
#define ZUI_DESK_TILE_PT 52 /* one launcher tile (square)           */
#define ZUI_DESK_STATUS 96u

typedef struct {
    const char *name; /* shown under the tile and in the strip  */
    const char *mark; /* one or two letters drawn in the tile   */
} zui_desk_app_t;

typedef enum { ZUI_DESK_NONE = 0, ZUI_DESK_LAUNCH, ZUI_DESK_SPACES, ZUI_DESK_HOME } zui_desk_act_t;

/* The faces the desk draws with (NULL draws no text there). */
typedef struct {
    const zui_text_t *title;   /* the wordmark, ZUI_T_TITLE        */
    const zui_text_t *body;    /* the switch, ZUI_T_BODY           */
    const zui_text_t *caption; /* tile names and status, CAPTION   */
    const zui_text_t *mark;    /* letters in a tile, HEADLINE      */
} zui_desk_faces_t;

typedef struct {
    int32_t w, h;      /* the screen, pixels  */
    uint32_t scale_q8; /* pixels per point x256 */
    zui_desk_faces_t f;
    zui_desk_app_t app[ZUI_DESK_APPS];
    uint32_t napps;
    uint32_t win_of[ZUI_DESK_APPS]; /* the app's window id in wm, 0 none */
    bool spaces;                    /* the switch shows Spaces chosen  */
    int32_t hover;                  /* tile under the pointer, -1 none */
    int32_t press;                  /* tile pressed and not yet let go */
    char status[ZUI_DESK_STATUS];
    zui_wm_t wm;
    /* what the last zui_desk_event did */
    zui_desk_act_t act;
    int32_t act_app;
} zui_desk_t;

void zui_desk_init(zui_desk_t *d, int32_t w, int32_t h, uint32_t scale_q8, bool touch,
                   const zui_desk_faces_t *faces);
void zui_desk_resize(zui_desk_t *d, int32_t w, int32_t h, uint32_t scale_q8);
/* Add an app to the rail; returns its index, or -1 when the rail is full. */
int32_t zui_desk_add(zui_desk_t *d, const char *name, const char *mark);
void zui_desk_status(zui_desk_t *d, const char *text);

int32_t zui_desk_pt(const zui_desk_t *d, int32_t pt); /* points to pixels */
zui_rect_t zui_desk_bar(const zui_desk_t *d);
zui_rect_t zui_desk_rail(const zui_desk_t *d);
zui_rect_t zui_desk_area(const zui_desk_t *d); /* where the windows live */
zui_rect_t zui_desk_tile(const zui_desk_t *d, uint32_t i);
zui_rect_t zui_desk_switch(const zui_desk_t *d, bool spaces); /* one half of the D1 switch */

/* D2: open `app`'s window, or bring it to the front when it has one. The
 * client paints the content. Returns the window id (0 when none fit). */
uint32_t zui_desk_open(zui_desk_t *d, uint32_t app, const char *title, const zui_wm_client_t *c);
int32_t zui_desk_front_app(const zui_desk_t *d); /* the app in front, -1 none */

/* Take one event (screen pixels). Returns true when the desk or a window
 * used it; d->act says whether the owner has something to do. */
bool zui_desk_event(zui_desk_t *d, const zui_event_t *e);
void zui_desk_tick(zui_desk_t *d, uint32_t ms);
/* Draw everything: ground, bar, rail and windows. */
void zui_desk_draw(zui_desk_t *d, zui_surface_t *s, uint32_t tick);
/* Draw only the bar (a full-screen mode such as Spaces keeps the bar). */
void zui_desk_draw_bar(zui_desk_t *d, zui_surface_t *s);

#endif /* ZXV_ZUI_DESK_H */
