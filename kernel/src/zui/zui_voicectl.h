/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_voicectl.h — voice control: the screen used by saying what to do.
 *
 * The speech recognizer is somewhere else (on the device, or behind the
 * speech service); it hands this module what the person said, as UTF-8
 * text. This module turns a phrase into an action on the zui widgets or the
 * windows, or types it into the focused field.
 *
 *   VC1 PHRASES ARE A TABLE. Every command is a pattern in a
 *       zui_vc_locale_t: plain words, with at most one slot, "%s" (any
 *       words) or "%n" (a number, in digits or the locale's number words),
 *       anywhere in it, so "click %s" and "%sをクリック" both work. Matching
 *       folds ASCII case, squeezes spaces and ignores closing punctuation.
 *       English (zui_vc_locale_en) is built in; zui_vc_add_locale puts up to
 *       ZUI_VC_LOCALES tables in front of it, tried in order.
 *   VC2 NAMES. "click / tap / press <name>" finds the shown, enabled,
 *       focusable widgets and the visible list rows whose name (accessible
 *       name, else label, else placeholder; a row's text) is the name, else
 *       starts with it as a word, else contains it. One match is acted on;
 *       several are numbered on screen (VC3) and the phrase is AMBIGUOUS.
 *   VC3 NUMBERS. "show numbers" numbers every actionable thing on screen;
 *       "<n>", "tap <n>" or "number <n>" acts on n. The numbers stay until
 *       "hide numbers" or "cancel" (and are counted again after each
 *       action); the numbers of a disambiguation go after one is chosen.
 *   VC4 GRID. "show grid" lays a 3 x 3 grid over the screen; "<n>" zooms into
 *       cell n (a 3 x 3 grid inside it), until a cell is under
 *       3 x ZUI_VC_GRID_MIN px, when it clicks its centre; "tap <n>" clicks
 *       the centre of cell n at once. "hide grid" or "cancel" ends it.
 *   VC5 THE REST. "scroll up / down" (the focused widget, else the middle
 *       of the screen), "go back", "open <app>" (from the app list given),
 *       "switch to <app>" (an open window by app or title), "close window"
 *       (the one in front), "type <text>" (into the focused field, never
 *       pressing Enter), "select <word>" (in the focused field), "start /
 *       stop dictation".
 *   VC6 DICTATION. AUTO (the default): a phrase that is no command goes
 *       into the focused editable field as text. ON: everything but "stop
 *       dictation" is text. OFF: only commands.
 *   VC7 ACTIONS, NEVER AI OUTPUT. A command becomes a zui_vc_action_t given
 *       to on_act (with none, zui_vc_apply performs it). Nothing is run on
 *       its own: the only input is the person's own recognized speech; text
 *       is typed, never submitted; and the Accept and Reject of a held AI
 *       answer (zui_widget.h W4) answer only to their exact name or number,
 *       never to a partial match.
 *   VC8 THE DOCK (zui_wm.h WM9, the default in place of the strip). Its
 *       seal is a target named "Apps" (choosing it opens or closes the
 *       dock) and every app on it is a target named by its title: on its
 *       slot while the dock is open, on the seal while it is closed.
 *       Choosing one does what pressing its slot does (zui_wm_dock_pick).
 *       "show numbers", or a name several apps share, opens the dock so its
 *       slots are numbered where they are; the dock closes again when the
 *       numbers go. "open <app>" also knows the dock's apps (raising an
 *       open one, launching a pinned one: zui_wm_launch, also for a name
 *       from the app list when no on_act launcher is set); "switch to
 *       <app>" is by window as before.
 *
 * Integer only, no allocation, deterministic.
 */
#ifndef ZXV_ZUI_VOICECTL_H
#define ZXV_ZUI_VOICECTL_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_widget.h"
#include "zui_wm.h"

#ifndef ZUI_VC_TARGETS
#    define ZUI_VC_TARGETS 160u
#endif
#define ZUI_VC_TEXT     256u /* bytes of a phrase, with the NUL       */
#define ZUI_VC_NAME     64u  /* bytes of a target's name, with the NUL */
#define ZUI_VC_LOCALES  4u
#define ZUI_VC_GRID_MIN 16 /* VC4: px */

typedef enum {
    ZUI_VC_CMD_CLICK = 0,  /* %s: a name                         */
    ZUI_VC_CMD_TAP_NUMBER, /* %n                                  */
    ZUI_VC_CMD_NUMBER,     /* %n: zoom the grid, or act on n      */
    ZUI_VC_CMD_SHOW_NUMBERS,
    ZUI_VC_CMD_HIDE_NUMBERS,
    ZUI_VC_CMD_SHOW_GRID,
    ZUI_VC_CMD_HIDE_GRID,
    ZUI_VC_CMD_CANCEL,
    ZUI_VC_CMD_SCROLL_UP,
    ZUI_VC_CMD_SCROLL_DOWN,
    ZUI_VC_CMD_BACK,
    ZUI_VC_CMD_OPEN,   /* %s: an app        */
    ZUI_VC_CMD_SWITCH, /* %s: an open app   */
    ZUI_VC_CMD_CLOSE_WINDOW,
    ZUI_VC_CMD_TYPE,   /* %s: text          */
    ZUI_VC_CMD_SELECT, /* %s: a word        */
    ZUI_VC_CMD_DICTATE_ON,
    ZUI_VC_CMD_DICTATE_OFF,
    ZUI_VC_CMD_COUNT
} zui_vc_cmd_t;

typedef struct {
    uint8_t cmd;         /* zui_vc_cmd_t                              */
    const char *pattern; /* VC1: lower-case words, one %s or %n slot */
} zui_vc_phrase_t;

/* VC1: a language's phrases. numbers[i] is the word for i (may be NULL). */
typedef struct {
    const char *tag; /* "en", "fr", "ja" ... */
    const zui_vc_phrase_t *phrases;
    uint32_t n_phrases;
    const char *const *numbers;
    uint32_t n_numbers;
} zui_vc_locale_t;

extern const zui_vc_locale_t zui_vc_locale_en;

typedef enum { ZUI_VC_DICT_AUTO = 0, ZUI_VC_DICT_ON, ZUI_VC_DICT_OFF } zui_vc_dict_t;
typedef enum { ZUI_VC_NONE = 0, ZUI_VC_NUMBERS, ZUI_VC_PICK, ZUI_VC_GRID } zui_vc_overlay_t;

typedef enum {
    ZUI_VC_T_WIDGET = 0, /* id: a widget                      */
    ZUI_VC_T_ROW,        /* id: a list, row: its row           */
    ZUI_VC_T_STRIP,      /* id: a window, by its strip slot    */
    ZUI_VC_T_DOCK        /* id: a dock item (-1 the seal), row: its slot or -1 */
} zui_vc_tkind_t;

typedef struct {
    uint8_t kind; /* zui_vc_tkind_t */
    bool held;    /* VC7: part of a held AI answer */
    int32_t id, row;
    zui_rect_t r; /* screen pixels */
    char name[ZUI_VC_NAME];
} zui_vc_target_t;

typedef enum {
    ZUI_VC_ACT_ACTIVATE = 0, /* target                                    */
    ZUI_VC_ACT_CLICK_AT,     /* x, y                                      */
    ZUI_VC_ACT_SCROLL,       /* x, y, dy (+ = see further down)           */
    ZUI_VC_ACT_BACK,
    ZUI_VC_ACT_OPEN_APP,     /* app (index, -1 = not in the list), text   */
    ZUI_VC_ACT_SWITCH_TO,    /* window                                    */
    ZUI_VC_ACT_CLOSE_WINDOW, /* window                                    */
    ZUI_VC_ACT_TYPE,         /* widget (the field), text                  */
    ZUI_VC_ACT_SELECT_TEXT   /* widget, start, end (bytes)                */
} zui_vc_act_kind_t;

typedef struct {
    uint8_t kind; /* zui_vc_act_kind_t */
    zui_vc_target_t target;
    zui_id_t widget;
    uint32_t window;
    int32_t app;
    int32_t x, y, dy;
    uint32_t start, end;
    char text[ZUI_VC_TEXT];
} zui_vc_action_t;

typedef enum {
    ZUI_VC_DONE = 0,  /* an action was made                                */
    ZUI_VC_SHOWN,     /* an overlay was shown, changed or hidden           */
    ZUI_VC_AMBIGUOUS, /* VC2: several matched; they are numbered           */
    ZUI_VC_NO_MATCH,  /* a command, but nothing it could act on            */
    ZUI_VC_DICTATED,  /* VC6: typed into the focused field                 */
    ZUI_VC_UNKNOWN,   /* not a command, and nowhere to type it             */
    ZUI_VC_EMPTY      /* nothing was said                                  */
} zui_vc_result_t;

typedef struct zui_voicectl zui_voicectl_t;
typedef void (*zui_vc_act_fn)(zui_voicectl_t *vc, const zui_vc_action_t *a, void *ctx);

struct zui_voicectl {
    zui_ui_t *ui;
    int32_t ui_x, ui_y; /* where the ui's (0, 0) is on screen */
    zui_wm_t *wm;
    zui_rect_t area; /* the screen: the grid, the middle for scrolling */
    const zui_vc_locale_t *loc[ZUI_VC_LOCALES];
    uint32_t n_loc;
    const char *const *apps; /* VC5: names "open" knows */
    uint32_t n_apps;
    zui_vc_dict_t dictation;
    zui_vc_overlay_t overlay;
    zui_vc_target_t t[ZUI_VC_TARGETS];
    uint32_t nt;
    uint16_t pick[ZUI_VC_TARGETS]; /* VC3: number n is t[pick[n - 1]] */
    uint32_t n_pick;
    zui_rect_t grid; /* VC4 */
    uint32_t grid_depth;
    zui_vc_act_fn on_act;
    void *act_ctx;
    zui_vc_action_t last;
    uint32_t actions;
    uint32_t clock;  /* ms stamped on the events it sends */
    bool dock_shown; /* VC8: the numbers opened the dock  */
    char raw[ZUI_VC_TEXT], norm[ZUI_VC_TEXT], arg[ZUI_VC_TEXT], arg_raw[ZUI_VC_TEXT];
};

void zui_vc_init(zui_voicectl_t *vc);
/* What is controlled: a ui whose (0, 0) is at (x, y) on screen, and/or a
 * window manager; either may be NULL. The area defaults to the wm's screen,
 * else the ui's. */
void zui_vc_attach(zui_voicectl_t *vc, zui_ui_t *ui, int32_t x, int32_t y, zui_wm_t *wm);
void zui_vc_set_area(zui_voicectl_t *vc, zui_rect_t area);
/* VC1: tried before the ones already added, and before English. */
bool zui_vc_add_locale(zui_voicectl_t *vc, const zui_vc_locale_t *loc);
void zui_vc_clear_locales(zui_voicectl_t *vc);
void zui_vc_set_apps(zui_voicectl_t *vc, const char *const *names, uint32_t n);
void zui_vc_set_dictation(zui_voicectl_t *vc, zui_vc_dict_t mode);
void zui_vc_on_act(zui_voicectl_t *vc, zui_vc_act_fn fn, void *ctx);

/* The one entry point: what the person said. */
zui_vc_result_t zui_vc_hear(zui_voicectl_t *vc, const char *utf8);

/* VC1: which command a phrase is (ZUI_VC_CMD_COUNT: none) and its slot's
 * text, without doing anything. */
zui_vc_cmd_t zui_vc_parse(zui_voicectl_t *vc, const char *utf8, char *arg, uint32_t cap);
/* Collect what can be acted on again (hear does this itself). */
uint32_t zui_vc_collect(zui_voicectl_t *vc);
/* VC3: the target shown as number n (1-based), or NULL. */
const zui_vc_target_t *zui_vc_numbered(const zui_voicectl_t *vc, uint32_t n);
/* VC4: grid cell n (1..9) of the current grid. */
zui_rect_t zui_vc_grid_cell(const zui_voicectl_t *vc, uint32_t n);

/* VC7: perform an action on the attached ui / wm; false when not used. */
bool zui_vc_apply(zui_voicectl_t *vc, const zui_vc_action_t *a);

/* Draw the numbers or the grid over s (numbers need text to show digits). */
void zui_vc_draw(const zui_voicectl_t *vc, zui_surface_t *s, const zui_text_t *text);

#endif /* ZXV_ZUI_VOICECTL_H */
