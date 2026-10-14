/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_switch.h — switch access: the whole screen driven by one or two
 * switches (a button, a sip-and-puff tube, a head switch, a blink sensor).
 *
 * The highlight moves over what can be used; the person presses a switch
 * when it is on what they want. Everything a pointer or a keyboard can reach
 * is reachable this way.
 *
 *   SW1 TARGETS. The focusable widgets of a zui_ui_t (shown, enabled, laid
 *       out), or the items of its open menu when one is open, plus, when a
 *       zui_wm_t is attached, every window that is not minimised and every
 *       slot of the strip. They are collected again each time a scan starts,
 *       so a screen that changed is scanned as it is now.
 *   SW2 GROUPS THEN ITEMS. Targets are grouped into rows (or columns, or one
 *       list), in reading order; the windows and the strip are a group each.
 *       The highlight steps through the groups; selecting a group steps
 *       through its items; after the last item the group itself is offered
 *       once more, and selecting it goes back to the groups. A group of one
 *       is selected straight through. One group is scanned as items.
 *   SW3 ONE SWITCH (AUTO). The highlight moves by itself every interval_ms
 *       (the first stop of a scan waits first_ms longer); SELECT chooses.
 *       After `loops` passes with no choice, item scanning returns to the
 *       groups and group scanning rests (IDLE) until SELECT starts it again.
 *   SW4 TWO SWITCHES (STEP). NEXT moves the highlight, SELECT chooses;
 *       nothing moves by itself. (In AUTO, a NEXT switch steps early.)
 *   SW5 DWELL. A switch counts when it has been held dwell_ms (0: at once),
 *       so a tremor or a bump does not choose; a press within debounce_ms of
 *       the last one that counted is ignored. While a switch is held the
 *       highlight does not move.
 *   SW6 POINT SCAN. For anything that is not a target, the last group is the
 *       point scan: a vertical line sweeps across (x), SELECT fixes it, a
 *       horizontal line sweeps down (y), SELECT fixes the point. The action
 *       goes to that point as pointer events.
 *   SW7 ACTION MENU. After choosing a target (or a point), a small menu is
 *       scanned the same way: Activate, Long press, Scroll up, Scroll down,
 *       Back, Cancel. With the menu off, choosing activates at once. Its
 *       words come from a table a locale can replace.
 *   SW8 ACTIONS, NOT MAGIC. A choice becomes a zui_sw_action_t given to the
 *       on_act callback; with none, zui_switch_apply performs it: activate
 *       focuses the widget and presses Enter (a field just takes focus);
 *       long press sends the Menu key (a context menu); scroll sends a
 *       scroll event; back sends the Back key; windows are raised, strip
 *       slots pressed, points clicked. Nothing else is done on its own.
 *   SW9 THE HIGHLIGHT is engraved into the ground with a gold edge: it is
 *       the screen's one active element. The fixed line of a point scan is
 *       light green, the sweeping one gold.
 *
 * Driven only by switch events and ticks of elapsed ms: deterministic.
 * Integer only, no allocation.
 */
#ifndef ZXV_ZUI_SWITCH_H
#define ZXV_ZUI_SWITCH_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_widget.h"
#include "zui_wm.h"

#ifndef ZUI_SW_TARGETS
#    define ZUI_SW_TARGETS 128u
#endif
#ifndef ZUI_SW_GROUPS
#    define ZUI_SW_GROUPS 48u
#endif

typedef enum { ZUI_SW_AUTO = 0, ZUI_SW_STEP } zui_sw_mode_t;
typedef enum { ZUI_SW_ROWS = 0, ZUI_SW_COLUMNS, ZUI_SW_LINEAR } zui_sw_grouping_t;
typedef enum { ZUI_SW_SELECT = 0, ZUI_SW_NEXT, ZUI_SW_SWITCHES } zui_sw_switch_t;

typedef enum {
    ZUI_SW_OFF = 0, /* not scanning; switches do nothing      */
    ZUI_SW_IDLE,    /* SW3: resting; SELECT starts a scan     */
    ZUI_SW_GROUP,   /* the highlight is on a group            */
    ZUI_SW_ITEM,    /* on an item of the chosen group         */
    ZUI_SW_MENU,    /* SW7: on an action menu entry           */
    ZUI_SW_POINT_X, /* SW6: the vertical line sweeps          */
    ZUI_SW_POINT_Y  /* SW6: the horizontal line sweeps        */
} zui_sw_state_t;

typedef enum {
    ZUI_SW_T_WIDGET = 0, /* id: a widget of the ui                 */
    ZUI_SW_T_MENU_ITEM,  /* id: an item of the ui's open menu      */
    ZUI_SW_T_WINDOW,     /* id: a zui_wm window id                 */
    ZUI_SW_T_STRIP,      /* id: the window id of a strip slot      */
    ZUI_SW_T_POINT       /* a point chosen by the point scan       */
} zui_sw_tkind_t;

typedef struct {
    uint8_t kind; /* zui_sw_tkind_t */
    int32_t id;
    zui_rect_t r; /* screen pixels */
} zui_sw_target_t;

typedef enum {
    ZUI_SW_ACT_ACTIVATE = 0,
    ZUI_SW_ACT_LONG_PRESS,
    ZUI_SW_ACT_SCROLL_UP,
    ZUI_SW_ACT_SCROLL_DOWN,
    ZUI_SW_ACT_BACK,
    ZUI_SW_ACT_CANCEL, /* the menu's way out: nothing is done */
    ZUI_SW_ACT_COUNT
} zui_sw_verb_t;

typedef struct {
    uint8_t verb; /* zui_sw_verb_t */
    zui_sw_target_t target;
    int32_t x, y; /* screen pixels: the target's centre, or the point */
} zui_sw_action_t;

typedef struct zui_switch zui_switch_t;
typedef void (*zui_sw_act_fn)(zui_switch_t *sw, const zui_sw_action_t *a, void *ctx);

typedef struct {
    zui_sw_mode_t mode;
    zui_sw_grouping_t grouping;
    uint32_t interval_ms;          /* SW3: 100..20000                         */
    uint32_t first_ms;             /* SW3: extra wait on a scan's first stop  */
    uint32_t dwell_ms;             /* SW5: 0..10000                           */
    uint32_t debounce_ms;          /* SW5                                      */
    uint8_t loops;                 /* SW3: 1..20                              */
    bool menu;                     /* SW7: offer the action menu              */
    bool point;                    /* SW6: offer the point scan               */
    uint8_t menu_mask;             /* SW7: which verbs the menu offers (1 << verb) */
    uint32_t point_ms;             /* SW6: ms between steps of the line       */
    int32_t point_step;            /* SW6: px a step (0: 1/64 of the area)    */
    uint16_t key_select, key_next; /* keys that act as the switches (0: none) */
} zui_sw_config_t;

typedef struct {
    zui_rect_t r;
    uint16_t first, n; /* targets [first, first + n) */
    bool point;        /* SW6: the point-scan group  */
} zui_sw_group_t;

struct zui_switch {
    zui_sw_config_t cfg;
    zui_sw_state_t state;
    zui_ui_t *ui;
    int32_t ui_x, ui_y; /* where the ui's (0, 0) is on screen */
    zui_wm_t *wm;
    zui_rect_t area; /* the screen, for the point scan */
    zui_sw_target_t t[ZUI_SW_TARGETS];
    uint32_t nt;
    zui_sw_group_t g[ZUI_SW_GROUPS];
    uint32_t ng;
    uint32_t group, item, hot; /* where the highlight is (hot: the menu)  */
    uint32_t passes;
    uint32_t elapsed;      /* ms since the highlight last moved   */
    uint32_t now_ms;       /* the sum of the ticks                */
    uint32_t last_fire_ms; /* SW5: debounce                       */
    bool fired_once;
    bool held[ZUI_SW_SWITCHES], fired[ZUI_SW_SWITCHES];
    uint32_t hold_ms[ZUI_SW_SWITCHES];
    zui_sw_target_t chosen; /* SW7: what the menu acts on */
    int32_t px, py;         /* SW6 */
    uint8_t menu_verbs[ZUI_SW_ACT_COUNT], n_menu;
    const char *const *words; /* SW7: ZUI_SW_ACT_COUNT labels; NULL = English */
    zui_sw_act_fn on_act;
    void *act_ctx;
    void (*on_move)(zui_switch_t *sw, void *ctx); /* the highlight moved (a reader speaks it) */
    void *move_ctx;
    uint32_t actions; /* actions performed or handed on */
    uint32_t moves;
};

/* AUTO, rows, 1000 ms a stop, 500 ms more on the first, no dwell, 100 ms
 * debounce, 3 loops, the menu with every verb, the point scan at 50 ms a step;
 * Space is SELECT and Enter is NEXT. */
void zui_sw_config_default(zui_sw_config_t *c);
void zui_switch_init(zui_switch_t *sw, const zui_sw_config_t *cfg);   /* NULL: the defaults */
void zui_switch_config(zui_switch_t *sw, const zui_sw_config_t *cfg); /* clamped */
/* What is scanned: a ui whose (0, 0) is at (x, y) on screen, and/or a window
 * manager. Either may be NULL. The point-scan area defaults to the wm's
 * screen, else the ui's. */
void zui_switch_attach(zui_switch_t *sw, zui_ui_t *ui, int32_t x, int32_t y, zui_wm_t *wm);
void zui_switch_set_area(zui_switch_t *sw, zui_rect_t area);
void zui_switch_on_act(zui_switch_t *sw, zui_sw_act_fn fn, void *ctx);
void zui_switch_set_words(zui_switch_t *sw, const char *const *words); /* SW7 locale */
const char *zui_switch_word(const zui_switch_t *sw, zui_sw_verb_t v);

/* SW1: collect the targets and start at the first group. */
void zui_switch_start(zui_switch_t *sw);
void zui_switch_stop(zui_switch_t *sw);

/* SW5: a switch went down or up. */
void zui_switch_press(zui_switch_t *sw, zui_sw_switch_t which, bool down);
/* The configured keys as switches; returns true when the event was used. */
bool zui_switch_event(zui_switch_t *sw, const zui_event_t *e);
/* SW3, SW5: ms of time passed. */
void zui_switch_tick(zui_switch_t *sw, uint32_t ms);

/* Where the highlight is now (screen pixels); false when there is none.
 * For the point scan it is the sweeping line. */
bool zui_switch_highlight(const zui_switch_t *sw, zui_rect_t *r);
/* SW7: the menu's box, and the verb under the highlight. */
zui_rect_t zui_switch_menu_rect(const zui_switch_t *sw);
zui_sw_verb_t zui_switch_menu_verb(const zui_switch_t *sw);

/* SW8: perform an action on the attached ui / wm. Returns whether it was used. */
bool zui_switch_apply(zui_switch_t *sw, const zui_sw_action_t *a);

/* SW9: draw the highlight (and the menu, with text when given) over s. */
void zui_switch_draw(const zui_switch_t *sw, zui_surface_t *s, const zui_text_t *text);

#endif /* ZXV_ZUI_SWITCH_H */
