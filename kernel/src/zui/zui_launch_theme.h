/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_launch_theme.h — every app's launch screen, live, in its window.
 *
 * The owner (2026-10-12): each app opens with the boot screen itself, inside
 * its own window: the boot's same five logos on the tiles (36N9, ACOTO, the
 * ASCW crest in the centre, SMIC, CIS), the app's own icon at the top centre,
 * and the app's own begin / middle / end sound sequence. It is a loading
 * screen, not a recording: the app's real load drives the picture AND the
 * sounds, so a fast app opens in a blink and a slow one takes as long as it
 * really loads.
 *
 * This ties three things together:
 *   zui_wm      the window, its launch screen (zui_launch_draw) and its cues
 *   zxl_launch  the app's theme (zxl_theme_pick / zxl_theme_for), the sound
 *               setting, and which sound plays when
 *   the shell   draws the pictures (the app's icon, the five logos) and
 *               plays the sounds, through the callbacks below
 *
 *   LT1 OPEN. zui_launch_theme_open opens the app's window with the five
 *       logos as its parts and the app's icon as its mark. The theme is the
 *       manifest's "launch <name>" when it names one, else the app's own
 *       entry, else group 1's. A window that is already loaded (warm) plays
 *       only the end.
 *   LT2 BEGIN plays when the window opens.
 *   LT3 TICKS play when a tile visibly lights: the screen tells this module
 *       through the tile's draw, so the sound and the gold happen together at
 *       whatever speed the real load moves the bar.
 *   LT4 END plays when the app is ready (the window manager's END cue, at
 *       progress 256), never before. A window that gathered off screen lights
 *       its remaining tiles silently instead of bursting ticks.
 *   LT5 The sound setting (sound.app_launch), mute, focus, screen reader and
 *       volume are zxl_prefs_t's rules; play is only called for audible cues.
 *   LT6 Icon colours: system apps' icons are held to the palette (gold on
 *       #002911); an app a person made keeps its own (zxl_icon_ink).
 *   LT7 Other callers keep working: this chains the window manager's
 *       on_window, cue and launch_draw hooks that were set before attaching.
 *   LT8 REAL LOAD. An app opened from a recipe loads by fetching its
 *       blocks (zxvd fab.receive.start, then fab.receive.step until phase
 *       "done"; zxvd_fab.h B13). Its opener hands each answer to
 *       zui_launch_theme_load: the verified blocks fill the tiles (outer
 *       pair first, the centre last) and the bar never runs ahead of them;
 *       once every block is in, the mark lights and the bar above it
 *       breathes while the app is built; only "done" (the target CID
 *       matched) opens the doors and plays the end. A failed step or a
 *       cancel never does: the screen stays where the work stopped, and the
 *       opener says why and closes the window. A warm window takes none of
 *       this: it plays only its end.
 *
 * Integer only, no allocation. */
#ifndef ZXV_ZUI_LAUNCH_THEME_H
#define ZXV_ZUI_LAUNCH_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_wm.h"
#include "zxl_launch.h"

#define ZUI_LT_LOGOS 5 /* the boot's five logos, left to right */
enum { ZUI_LT_36N9 = 0, ZUI_LT_ACOTO, ZUI_LT_ASCW, ZUI_LT_SMIC, ZUI_LT_CIS };

typedef struct {
    /* the app's icon at the top centre, lit 0..256, in `ink` (LT6) */
    void (*icon)(void *ctx, const char *app, zxl_icon_ink_t ink, zui_surface_t *s, zui_rect_t box,
                 uint32_t lit);
    /* one of the five logos (ZUI_LT_*), lit 0..256 */
    void (*logo)(void *ctx, uint32_t which, zui_surface_t *s, zui_rect_t box, uint32_t lit);
    /* play one cue: ev->kind is BEGIN, TICK (ev->note, a MIDI note) or END;
     * ev->gain_q8 is the volume after the setting (LT5) */
    void (*play)(void *ctx, uint32_t window, const zxl_theme_t *theme, const zxl_event_t *ev);
    void *ctx;
} zui_lt_shell_t;

typedef struct zui_launch_theme zui_launch_theme_t;

typedef struct {
    zui_launch_theme_t *owner;
    uint32_t slot, part;
} zui_lt_ref_t;

typedef struct {
    uint32_t id; /* window id, 0 = free */
    bool used, system_app;
    char app[ZUI_WM_NAME_MAX];
    const zxl_theme_t *theme;
    zxl_launch_t launch;
    zui_lt_ref_t ref[ZUI_LT_LOGOS + 1]; /* [0] the icon, [1 + i] logo i */
} zui_lt_slot_t;

struct zui_launch_theme {
    zui_wm_t *wm;
    zui_lt_shell_t shell;
    zxl_prefs_t prefs;
    zui_lt_slot_t slot[ZUI_WM_MAX];
    uint32_t pending; /* slot being opened, ZUI_WM_MAX = none */
    /* the hooks that were there before (LT7) */
    void (*prev_on_window)(zui_wm_t *wm, uint32_t id, bool open, void *ctx);
    void *prev_on_window_ctx;
    void (*prev_cue)(zui_wm_t *wm, uint32_t id, int cue, void *ctx);
    void *prev_cue_ctx;
    zui_wm_launch_fn prev_launch_draw;
    void *prev_launch_ctx;
};

/* Attach to a window manager. prefs NULL = the defaults (sounds on). */
void zui_launch_theme_attach(zui_launch_theme_t *t, zui_wm_t *wm, const zui_lt_shell_t *shell,
                             const zxl_prefs_t *prefs);

/* The sound setting changed (Keystone > Sound): applies to launches opened
 * from now on. */
void zui_launch_theme_prefs(zui_launch_theme_t *t, const zxl_prefs_t *prefs);

/* LT1: open `app` in a window. launch_name is the manifest's "launch <name>"
 * ("" or NULL: the app's own). system_app holds its icon to the palette;
 * warm opens an app that is already loaded. Returns the window id, or 0. */
uint32_t zui_launch_theme_open(zui_launch_theme_t *t, const char *app, const char *title,
                               const char *launch_name, bool system_app, bool warm,
                               const zui_wm_client_t *client);

/* LT8: one fab.receive.start or fab.receive.step answer for window id. */
enum { ZUI_LT_FETCH = 0, ZUI_LT_DONE, ZUI_LT_FAILED };
/* phase: ZUI_LT_FETCH ("fetch", and the start's answer), ZUI_LT_DONE
 * ("done"), ZUI_LT_FAILED (an error answer, or the job cancelled). Returns
 * 0, or -1 for an unknown window, a warm one, or one that already ended or
 * failed (nothing changes). */
int zui_launch_theme_load(zui_launch_theme_t *t, uint32_t id, uint32_t phase, uint32_t blocks_done,
                          uint32_t blocks_total);

/* The theme a window is using, or NULL. */
const zxl_theme_t *zui_launch_theme_of(const zui_launch_theme_t *t, uint32_t id);

#endif /* ZXV_ZUI_LAUNCH_THEME_H */
