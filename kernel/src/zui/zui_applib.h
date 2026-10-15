/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_applib.h — the app library: every one of the 407 apps with its vector
 * icon, and the card that tells what an app is. The bare-metal shell and the
 * hosted desktop both show it from the launcher's first tile.
 *
 *   AL1 THE HEADER: the title and how many apps are shown, the layer switch
 *       (All, Layer 1, Layer 2, Layer 3, each with its count) and the search
 *       field. Typing always searches (name, category, what it does); Escape
 *       clears the search, and on an empty search asks to close.
 *   AL2 THE GRID: one tile per app, its icon above its name, as many columns
 *       as fit, scrolled by wheel, drag-free keys (arrows, Page Up/Down, Home,
 *       End) and kept so the focused tile is always in view. The tile under
 *       the pointer and the focused tile get a gold ring; a thin indicator
 *       shows where the view is when the grid is taller than the screen.
 *   AL3 Choosing a tile (click, or Enter on the focused one) is reported as
 *       ZUI_APPLIB_OPEN with the app's 0-based catalogue index; the owner
 *       opens the app's window when it has one, or the card (AL5).
 *   AL4 NOTHING MATCHES: a clear empty state (a magnifier, one line naming the
 *       search, and a Clear search button), never a blank grid.
 *   AL5 THE CARD: a large icon, the name, the layer and category, a status
 *       (Ready or Not built yet), what it does, what it is built from, the
 *       pillar languages each part runs in, and how it keeps contradictions.
 *   AL6 Icons drawn once per size into the caller's cache (optional), so a
 *       scrolled grid redraws in copies, not in vector passes. Integer only,
 *       freestanding, no allocation; sizes are points scaled by scale_q8.
 */
#ifndef ZXV_ZUI_APPLIB_H
#define ZXV_ZUI_APPLIB_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_apps.h"
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_vec.h"

#define ZUI_APPLIB_QUERY   48u
#define ZUI_APPLIB_TILE_W  112 /* points */
#define ZUI_APPLIB_TILE_H  120
#define ZUI_APPLIB_ICON    64
#define ZUI_APPLIB_GAP     8
#define ZUI_APPLIB_FIELD_W 280

typedef enum { ZUI_APPLIB_NONE = 0, ZUI_APPLIB_OPEN, ZUI_APPLIB_CLOSE } zui_applib_act_t;

typedef struct {
    const zui_text_t *headline, *title, *body, *label, *caption;
} zui_applib_faces_t;

/* AL6: n slots of side*side pixels; a slot holds one icon at one size,
 * already drawn on the grid's ground. */
typedef struct {
    uint32_t *px;
    uint32_t slots, side;
    int32_t tag[64]; /* app index per slot (-1 empty); slots <= 64 */
    int32_t tag_size[64];
} zui_applib_cache_t;

typedef struct {
    const zui_apps_t *apps;
    zui_vec_work_t *work;
    zui_applib_cache_t *cache; /* may be null */
    zui_applib_faces_t f;
    uint32_t scale_q8;
    zui_rect_t box; /* where it draws, screen pixels */
    uint32_t layer; /* 0 all, 1..3 */
    char query[ZUI_APPLIB_QUERY];
    uint32_t qlen;
    uint16_t shown[ZUI_APPS_MAX]; /* catalogue indices that match */
    uint32_t nshown;
    int32_t scroll; /* pixels from the top of the grid */
    int32_t hover, focus, press; /* positions in shown[], -1 none */
    int32_t press_seg;           /* layer switch pressed, -1 none */
    bool press_clear;
    zui_applib_act_t act;
    int32_t act_app;
} zui_applib_t;

void zui_applib_init(zui_applib_t *l, const zui_apps_t *apps, zui_vec_work_t *work,
                     zui_applib_cache_t *cache, const zui_applib_faces_t *faces,
                     uint32_t scale_q8);
void zui_applib_place(zui_applib_t *l, zui_rect_t box, uint32_t scale_q8);
void zui_applib_set_layer(zui_applib_t *l, uint32_t layer);
void zui_applib_set_query(zui_applib_t *l, const char *q);
void zui_applib_cache_init(zui_applib_cache_t *c, uint32_t *px, uint32_t slots, uint32_t side);

/* geometry, screen pixels */
zui_rect_t zui_applib_segment(const zui_applib_t *l, uint32_t layer); /* 0..3 */
zui_rect_t zui_applib_field(const zui_applib_t *l);
zui_rect_t zui_applib_clear_btn(const zui_applib_t *l); /* the field's x, or Clear search */
zui_rect_t zui_applib_grid(const zui_applib_t *l);
uint32_t zui_applib_columns(const zui_applib_t *l);
zui_rect_t zui_applib_tile(const zui_applib_t *l, uint32_t pos); /* scrolled */
int32_t zui_applib_max_scroll(const zui_applib_t *l);

/* One event; true when the library used it. l->act says what to do. */
bool zui_applib_event(zui_applib_t *l, const zui_event_t *e);
void zui_applib_draw(zui_applib_t *l, zui_surface_t *s);

/* AL5: the card for app at catalogue index i, filling box. built says
 * whether the app has a window to open (Ready, with an Open button). */
void zui_applib_card(zui_surface_t *s, zui_vec_work_t *w, const zui_applib_faces_t *f,
                     uint32_t scale_q8, const zui_app_t *app, bool built, zui_rect_t box);
/* where the card's Open button is (zero size when not built) */
zui_rect_t zui_applib_card_open(uint32_t scale_q8, bool built, zui_rect_t box);

#endif /* ZXV_ZUI_APPLIB_H */
