/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_workspace.h — a person's workspace, their way: saved sets of open
 * windows, the panel, the keyboard shortcuts, the theme, text size, motion
 * and density. Everything is a key in the settings store (zui_settings.h),
 * so it layers, locks, syncs to watchers and saves as text like any other
 * setting.
 *
 *   WS1 KEYS. zui_ws_schema registers workspace.*, panel.*, shortcut.* and
 *       theme.* once per store. A zui_workspace_t reads through its view
 *       (seat, user) and writes to the user's layer (the seat's layer for a
 *       view with no user, such as the login screen). An admin can lock any
 *       of these keys in SYSTEM, DEVICE or SEAT (zui_settings.h S4); a write
 *       the lock refuses comes back as ZUI_SET_E_LOCKED.
 *   WS2 SAVED WORKSPACES. zui_ws_save keeps the open windows of a zui_wm_t
 *       under a name: in opening order, each with its app, its frame (where
 *       it goes back to when maximised) relative to the work area, whether
 *       it is maximised or minimised, and which window is in front. At most
 *       ZUI_WS_SAVED names and ZUI_WS_WINS windows each (the first ones
 *       opened; the count saved is returned). Saving under a name that
 *       exists replaces it.
 *   WS3 RESTORE ON ANY SCREEN. zui_ws_restore opens the saved apps in order
 *       on a zui_wm_t of any size. On a screen that is not compact each frame
 *       is scaled from the saved work area to this one, then clamped: at
 *       least ZUI_WM_MIN_W x ZUI_WM_MIN_H, at most the work area, wholly
 *       inside it; maximised and minimised come back, and the front window
 *       is raised last. On a compact screen (zui_wm.h WM4) the apps just
 *       open in order and the front one is raised. An open callback supplies
 *       each app's client and launch screen, or says the app is gone (it is
 *       skipped).
 *   WS4 THE PANEL is the strip of open and pinned apps: its edge (bottom,
 *       left, right, top), its size (ZUI_WS_PANEL_MIN..MAX points in steps
 *       of 4), auto-hide, and up to ZUI_WS_PINNED pinned apps in the order
 *       the person chose (no app twice). zui_ws_work gives the screen
 *       left for windows.
 *   WS5 SHORTCUTS map actions to key chords ("cmd+shift+l"). The system ones
 *       (switch apps, close, minimise, maximise, lock now, Ask, new window,
 *       quit) are bound by default. A chord is one key with modifiers in the
 *       order cmd, ctrl, alt, shift, meta; a key that types text needs cmd,
 *       ctrl, alt or meta. Binding a chord another action holds is refused
 *       and names that action; "" unbinds, except lock now, which must stay
 *       bound. Reset puts back the default (refused, naming the holder, if
 *       someone else has taken it). A chord in the store that does not parse
 *       counts as the default.
 *   WS6 MATCHING. zui_ws_match turns a key-down event into an action. The
 *       platform's real shortcut key (Ctrl, or Meta where Meta is the
 *       shortcut key: cmd_is_meta) arrives with ZUI_MOD_CMD (zui_input.h);
 *       the one CMD stands for is not counted again, so Ctrl-W and Cmd-W
 *       both match "cmd+w". Letters match either case.
 *   WS7 THEMES KEEP THE COLOUR RULES. A theme picks one of the three
 *       zui_palette.h sets (dark, light, high contrast) and one accent from
 *       a fixed list: the set's own gold, or amber, jade, sky, rose, coral
 *       (made for dark grounds; coral is too soft for high contrast) or
 *       bronze, pine, ocean, plum (made for light). The accent and its
 *       hover shade must read as text on the void, panel and raised colours
 *       (so the ground-coloured label on an accent button reads too: the
 *       same pair): 4.5:1, or 7:1 in high contrast (zui_palette.h C5). A
 *       pair that fails is refused; a failing pair found in the store
 *       (written by hand, or one part changed under a lock) shows the set's
 *       own gold. A part a lock below already holds at the value asked for
 *       is left alone; held at another value, the theme is refused and
 *       nothing is written. The focus ring is a colour role that must reach
 *       3:1 on the void and panel colours.
 *   WS8 COMFORT. Text scale (80..200 percent, steps of 10), reduced motion
 *       (every transition 0 ms) and density (compact 3/4, regular, roomy
 *       5/4 of the spacing). Touch and pointer targets never shrink below
 *       ZUI_TOUCH_MIN and ZUI_POINTER_MIN, whatever the density.
 *   WS9 Integer only, no allocation, no libc; everything persists through
 *       the settings store and round-trips through its text (S6). */
#ifndef ZXV_ZUI_WORKSPACE_H
#define ZXV_ZUI_WORKSPACE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_palette.h"
#include "zui_settings.h"
#include "zui_wm.h"

#define ZUI_WS_SAVED     4u /* saved workspaces                    */
#define ZUI_WS_WINS      8u /* windows kept in one                 */
#define ZUI_WS_PINNED    8u /* pinned apps in the panel            */
#define ZUI_WS_PANEL_MIN 32
#define ZUI_WS_PANEL_MAX 128
#define ZUI_WS_CHORD_MAX 40u /* "cmd+ctrl+alt+shift+meta+backspace" fits */

enum {
    ZUI_WS_E_CONFLICT = -20, /* WS5: another action has that chord     */
    ZUI_WS_E_CONTRAST = -21, /* WS7: fails the colour rules            */
    ZUI_WS_E_NOTFOUND = -22  /* no saved workspace by that name        */
};

typedef enum {
    ZUI_PANEL_BOTTOM = 0,
    ZUI_PANEL_LEFT,
    ZUI_PANEL_RIGHT,
    ZUI_PANEL_TOP
} zui_panel_pos_t;

typedef enum { ZUI_DENSITY_COMPACT = 0, ZUI_DENSITY_REGULAR, ZUI_DENSITY_ROOMY } zui_density_t;

typedef enum {
    ZUI_ACT_SWITCH_NEXT = 0, /* cmd+tab        */
    ZUI_ACT_SWITCH_PREV,     /* cmd+shift+tab  */
    ZUI_ACT_CLOSE,           /* cmd+w          */
    ZUI_ACT_MINIMISE,        /* cmd+m          */
    ZUI_ACT_MAXIMISE,        /* cmd+ctrl+f     */
    ZUI_ACT_LOCK_NOW,        /* cmd+shift+l    */
    ZUI_ACT_ASK,             /* cmd+k          */
    ZUI_ACT_NEW_WINDOW,      /* cmd+n          */
    ZUI_ACT_QUIT,            /* cmd+q          */
    ZUI_ACT_COUNT
} zui_ws_action_t;

typedef enum {
    ZUI_ACCENT_GOLD = 0, /* the set's own */
    ZUI_ACCENT_AMBER,
    ZUI_ACCENT_JADE,
    ZUI_ACCENT_SKY,
    ZUI_ACCENT_ROSE,
    ZUI_ACCENT_CORAL,
    ZUI_ACCENT_BRONZE,
    ZUI_ACCENT_PINE,
    ZUI_ACCENT_OCEAN,
    ZUI_ACCENT_PLUM,
    ZUI_ACCENT_COUNT
} zui_accent_t;

typedef struct {
    uint8_t mods; /* ZUI_MOD_* (CMD, CTRL, ALT, SHIFT, META)         */
    uint16_t key; /* ZUI_KEY_* or lower-case ASCII; 0 = unbound      */
} zui_chord_t;

typedef struct {
    zui_settings_t *st;
    zui_set_view_t view;   /* reads                                  */
    zui_set_layer_t layer; /* writes (WS1)                           */
    zui_set_view_t scope;
    bool cmd_is_meta; /* WS6: CMD stands for Meta here, not Ctrl */
} zui_workspace_t;

typedef struct {
    zui_panel_pos_t pos;
    int32_t size; /* in the screen's units, as zui_wm_t */
    bool autohide;
    uint32_t npinned;
    char pinned[ZUI_WS_PINNED][ZUI_SET_STR_MAX];
} zui_ws_panel_t;

typedef struct {
    zui_scheme_t scheme;
    zui_accent_t accent;
    zui_color_role_t focus;
    int32_t text_scale; /* percent */
    bool reduced_motion;
    zui_density_t density;
} zui_ws_theme_t;

/* WS3: fill in the client and launch screen for app; false skips it. */
typedef bool (*zui_ws_open_fn)(void *ctx, const char *app, zui_wm_client_t *client,
                               zui_launch_t *launch);

int zui_ws_schema(zui_settings_t *st); /* WS1: ZUI_SET_OK, or the error */
/* view.user 0 writes to the seat layer of view.seat (WS1). */
int zui_ws_init(zui_workspace_t *ws, zui_settings_t *st, zui_set_view_t view);

/* ---- WS2, WS3 ---------------------------------------------------------- */
/* Returns the number of windows saved, or a negative error. */
int zui_ws_save(zui_workspace_t *ws, const zui_wm_t *wm, const char *name);
/* Returns the number of windows opened, or a negative error. */
int zui_ws_restore(zui_workspace_t *ws, zui_wm_t *wm, const char *name, zui_ws_open_fn open,
                   void *ctx);
int zui_ws_forget(zui_workspace_t *ws, const char *name);
/* The saved names, in slot order; returns how many. */
uint32_t zui_ws_names(zui_workspace_t *ws, char names[][ZUI_SET_STR_MAX], uint32_t cap);

/* ---- WS4 --------------------------------------------------------------- */
void zui_ws_panel(zui_workspace_t *ws, zui_ws_panel_t *out);
int zui_ws_set_panel(zui_workspace_t *ws, zui_panel_pos_t pos, int32_t size, bool autohide);
/* Pin app at position at (past the end: last); an app already pinned moves
 * there. ZUI_SET_E_FULL when ZUI_WS_PINNED are pinned. */
int zui_ws_pin(zui_workspace_t *ws, const char *app, uint32_t at);
int zui_ws_unpin(zui_workspace_t *ws, const char *app);
int zui_ws_pins_reset(zui_workspace_t *ws); /* back to the layers below */
/* The panel's rectangle on a w x h screen (all zero when auto-hidden) and
 * the rest of the screen, for windows. */
zui_rect_t zui_ws_panel_rect(zui_workspace_t *ws, int32_t w, int32_t h);
zui_rect_t zui_ws_work(zui_workspace_t *ws, int32_t w, int32_t h);

/* ---- WS5, WS6 ---------------------------------------------------------- */
const char *zui_ws_action_name(zui_ws_action_t a); /* "switch-next", ... "lock-now" */
int zui_ws_action(const char *name);               /* the action, or -1 */
bool zui_chord_parse(const char *text, zui_chord_t *out);
int zui_chord_format(zui_chord_t c, char *buf, uint32_t cap); /* length, or ZUI_SET_E_SPACE */
zui_chord_t zui_ws_default_chord(zui_ws_action_t a);
zui_chord_t zui_ws_binding(zui_workspace_t *ws, zui_ws_action_t a);
/* On ZUI_WS_E_CONFLICT *holder (when not NULL) is the action holding it. */
int zui_ws_bind(zui_workspace_t *ws, zui_ws_action_t a, const char *chord, int *holder);
int zui_ws_bind_reset(zui_workspace_t *ws, zui_ws_action_t a, int *holder);
int zui_ws_bind_reset_all(zui_workspace_t *ws);
/* Two actions that share a chord (from text loaded by hand): true and
 * the pair, lowest first. */
bool zui_ws_conflict(zui_workspace_t *ws, int *a, int *b);
int zui_ws_match(zui_workspace_t *ws, const zui_event_t *e); /* the action, or -1 */

/* ---- WS7, WS8 ---------------------------------------------------------- */
const char *zui_accent_name(zui_accent_t a); /* "gold", "amber", ... */
/* ZUI_SET_OK when the pair keeps the rules, else ZUI_WS_E_CONTRAST */
int zui_ws_theme_check(zui_scheme_t scheme, zui_accent_t accent);
int zui_ws_focus_check(zui_scheme_t scheme, zui_color_role_t focus);
int zui_ws_set_theme(zui_workspace_t *ws, zui_scheme_t scheme, zui_accent_t accent);
int zui_ws_set_focus(zui_workspace_t *ws, zui_color_role_t focus);
int zui_ws_set_comfort(zui_workspace_t *ws, int32_t text_scale, bool reduced_motion,
                       zui_density_t density);
void zui_ws_theme(zui_workspace_t *ws, zui_ws_theme_t *out); /* in effect, after WS7 */
void zui_ws_colors(zui_workspace_t *ws, zui_colors_t *out);  /* the set with the accent */
int32_t zui_ws_text_pt(zui_workspace_t *ws, int32_t pt);     /* WS8 */
int32_t zui_ws_space(zui_workspace_t *ws, int32_t pt);
int32_t zui_ws_target(zui_workspace_t *ws, int32_t pt, bool touch);
uint32_t zui_ws_motion_ms(zui_workspace_t *ws, uint32_t ms);

#endif /* ZXV_ZUI_WORKSPACE_H */
