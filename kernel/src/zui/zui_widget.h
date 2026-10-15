/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_widget.h — the ZXV widget set: a retained tree of controls, laid out,
 * painted and driven by zui events, with no allocation.
 *
 * An app view builds its screen once as a tree (boxes holding labels,
 * buttons, lists, text fields, scroll areas), keeps the widget ids, and from
 * then on only changes state (a label's text, a list's row count, a button's
 * enabled flag) and answers actions. zui works out layout, focus, hit
 * testing, what changed and what to repaint.
 *
 *   W1 NO ALLOCATION. Widgets live in a fixed pool inside zui_ui_t; labels
 *      are copied into the widget (ZUI_LABEL_MAX bytes); a text field edits
 *      a buffer the caller owns; a list asks the caller for each visible row.
 *   W2 ONE ACCENT. A ZUI_BTN_PRIMARY button is the screen's one gold element;
 *      everything else is carved from the green ground (zui_tokens.h).
 *   W3 KEYBOARD FIRST. Every control is reachable with Tab / Shift-Tab, and
 *      on a remote with the arrow keys (nearest control in that direction).
 *      The focused control always shows a gold focus ring when focus came
 *      from the keyboard. Enter or Space activates; Escape closes the top
 *      menu or palette.
 *   W4 AI IS HELD. Swarm output appears only in a ZUI_W_HELD block: a dashed
 *      gold edge at the glass (S0), who made it and what it cost, and Accept
 *      / Reject. The widget never applies anything itself; the app's model
 *      does on ZUI_ACT_ACCEPT (zxm.h M3).
 *   W5 ASK PALETTE. Cmd-K (Ctrl-K elsewhere), or a long press on touch,
 *      opens the Ask palette over any screen: a query field and the app's
 *      declared AI actions, filtered as the person types. Picking one sends
 *      ZUI_ACT_PICK with the action's index; the answer comes back held.
 *   W6 ACCESSIBLE. Every widget has a role and a name; zui_a11y_text writes
 *      the tree as the screen-reader sees it, and the present layer maps it
 *      to the platform's accessibility API.
 *   W7 DAMAGE. A change marks only that widget's rectangle; zui_ui_paint
 *      repaints the damaged parts and hands the same rectangles to the
 *      present layer (zui_present.h P1).
 *   W8 CHOICES. A toggle (a switch), a check box, radio buttons (the radios
 *      of one box are one group), a slider and tabs (or a segmented control)
 *      work by pointer, touch, keyboard, switch access and voice: Enter or
 *      Space flips a toggle or check box and picks a radio or a tab; arrows
 *      move within a radio group or a tab list (the choice follows) and step
 *      a slider (Page Up/Down ten steps, Home/End the ends; a scroll wheel
 *      steps too). Each says what changed with ZUI_ACT_CHANGE (arg = the new
 *      value: 0/1 for a toggle or check box, the slider's value) or
 *      ZUI_ACT_SELECT (arg = the index of the radio or tab chosen). Only the
 *      chosen radio and the chosen tab are in the Tab order (roving focus).
 *   W9 DIALOGS. zui_dialog builds a modal dialog (a sheet along the bottom on
 *      a compact screen) that is hidden until zui_dialog_open. While one is
 *      open a scrim covers the screen behind it, focus stays inside it (Tab
 *      wraps within it, nothing behind can be clicked, focused or reached by
 *      switch access), Escape closes it with ZUI_ACT_DISMISS on the dialog,
 *      and closing puts focus back where it was.
 *   W10 TOOLTIPS. zui_set_tooltip gives any widget a one-line tip, shown
 *      ZUI_TIP_MS after the pointer rests on it or keyboard focus reaches it,
 *      under it (above when there is no room), gone on any press, key or
 *      scroll. It fades in over ZUI_MOTION_MS, or is simply there under
 *      reduce_motion. A screen reader reads it as the widget's description.
 *   W11 OVERFLOW SHOWS. A list, scroll area or text area holding more than
 *      fits draws a scroll indicator (a track and a thumb at 3:1 against the
 *      ground) and a fade at each edge with more beyond it; pressing the
 *      track pages toward the press. zui_a11y_text says how much is hidden.
 *   W12 EMPTY STATES. zui_empty is the one empty-state component: an
 *      engraved vector icon, one line, and an optional primary action,
 *      centred in the space it is given (ZUI_ACT_PRESS on the zui_empty id).
 *   W13 COLOURS BY ROLE (zui_palette.h C6): zui_ui_set_colors chooses the
 *      set; dark (NULL) is pixel for pixel the zui_tokens.h screen.
 *   W14 CARET ORDER. With zui_ui_t.caret set (zui_caret.h: the UAX #9 one),
 *      a text field lays each line out in visual order: the caret, the
 *      selection, clicks and Left/Right follow what is seen in right-to-left
 *      and mixed text, while the buffer and every edit stay in logical
 *      order. NULL (the default) is left-to-right only.
 *
 * Sizes are in points; zui converts with the screen's scale. Integer only.
 */
#ifndef ZXV_ZUI_WIDGET_H
#define ZXV_ZUI_WIDGET_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_layout.h"
#include "zui_palette.h"
#include "zui_present.h"

#ifndef ZUI_WIDGETS
#    define ZUI_WIDGETS 512u
#endif
#define ZUI_LABEL_MAX   96u /* bytes of a widget's own text, with the NUL */
#define ZUI_NAME_MAX    48u /* bytes of an accessible name override        */
#define ZUI_MENU_ITEMS  24u
#define ZUI_PALETTE_MAX 32u
#define ZUI_PREEDIT_MAX 64u

typedef int32_t zui_id_t; /* a widget's index in the pool; -1 = none */
#define ZUI_NONE (-1)

typedef enum {
    ZUI_W_BOX = 0,   /* a container: a row or a column of children   */
    ZUI_W_LABEL,     /* text that is read, not edited                 */
    ZUI_W_BUTTON,    /* activates on click, tap, Enter or Space       */
    ZUI_W_LIST,      /* rows the caller supplies; selection; scrolls  */
    ZUI_W_FIELD,     /* editable text, one line or many               */
    ZUI_W_SCROLL,    /* a column taller than its box, scrolled        */
    ZUI_W_HELD,      /* W4: an AI answer waiting for accept / reject  */
    ZUI_W_PROGRESS,  /* the ZXV bar (zui_progress.h)                  */
    ZUI_W_CANVAS,    /* the app paints it (a picture, a chart, faces) */
    ZUI_W_SEPARATOR, /* an engraved hairline                          */
    ZUI_W_TOGGLE,    /* W8: an on/off switch with its label           */
    ZUI_W_CHECK,     /* W8: a check box (off, on, mixed)              */
    ZUI_W_RADIO,     /* W8: one of the radios in its box              */
    ZUI_W_SLIDER,    /* W8: a value in [min, max] by steps            */
    ZUI_W_TABS,      /* W8: a row of tabs or segments (holds ZUI_W_TAB) */
    ZUI_W_TAB,       /* W8: one tab of a ZUI_W_TABS                    */
    ZUI_W_DIALOG,    /* W9: a modal dialog or sheet                   */
    ZUI_W_EMPTY      /* W12: an empty state: icon, line, action        */
} zui_kind_t;

/* W12: the empty state's engraved icon. */
typedef enum {
    ZUI_ICON_FOLDER = 0, /* nothing in a folder         */
    ZUI_ICON_PICTURE,    /* nothing to look at          */
    ZUI_ICON_CONSOLE,    /* nothing called yet          */
    ZUI_ICON_BELL,       /* no notifications            */
    ZUI_ICON_SEARCH      /* nothing matched             */
} zui_icon_t;

#define ZUI_TIP_MS 500u /* W10: rest this long before a tooltip shows */

typedef enum { ZUI_COL = 0, ZUI_ROW } zui_dir_t;

/* How a widget takes space along its parent's main axis. */
typedef enum {
    ZUI_SIZE_FIT = 0, /* its own natural size                       */
    ZUI_SIZE_FIXED,   /* `size` points                               */
    ZUI_SIZE_FLEX     /* a share (`size` = weight) of what is left   */
} zui_sizing_t;

typedef enum {
    ZUI_BTN_SECONDARY = 0, /* carved, light green text      */
    ZUI_BTN_PRIMARY,       /* W2: the gold one              */
    ZUI_BTN_GHOST,         /* text only, for toolbars       */
    ZUI_BTN_DANGER         /* alert outline (Empty Trash)   */
} zui_btn_style_t;

typedef enum {
    ZUI_TEXT_BODY = 0, /* UI text                       */
    ZUI_TEXT_LABEL,    /* captions, column heads         */
    ZUI_TEXT_TITLE,    /* a pane's title                 */
    ZUI_TEXT_MONO      /* numbers, paths, code, tokens   */
} zui_font_role_t;

/* What a widget tells the app. */
typedef enum {
    ZUI_ACT_NONE = 0,
    ZUI_ACT_PRESS,   /* a button: arg 0                              */
    ZUI_ACT_SELECT,  /* a list: arg = the newly selected row (-1 none) */
    ZUI_ACT_OPEN,    /* a list: arg = row, by double click or Enter    */
    ZUI_ACT_CHANGE,  /* a field's text changed                         */
    ZUI_ACT_SUBMIT,  /* Enter in a one-line field, Cmd-Enter in a text area */
    ZUI_ACT_CONTEXT, /* right click, long press, or the Menu key: arg = row for a list */
    ZUI_ACT_PICK,    /* a menu item or palette action: arg = its index */
    ZUI_ACT_ACCEPT,  /* W4: the held answer's Accept                   */
    ZUI_ACT_REJECT,  /* W4: its Reject                                 */
    ZUI_ACT_DISMISS, /* a menu or palette closed with nothing picked   */
    ZUI_ACT_ASK      /* W5: the palette's query submitted as a free question */
} zui_action_t;

typedef struct zui_ui zui_ui_t;

typedef void (*zui_on_fn)(zui_ui_t *ui, zui_id_t w, zui_action_t act, int32_t arg, void *ctx);

/* A list row: write its text (and an optional right-aligned detail, e.g. a
 * size or a token count) for row `row`. lead_rgb != 0 draws a small mark in
 * that colour before the text (a file kind, an agent's mood). */
typedef struct {
    char text[ZUI_LABEL_MAX];
    char detail[32];
    uint32_t lead_rgb;
    bool muted; /* drawn faint (a withdrawn item: S-) */
} zui_row_t;
typedef void (*zui_row_fn)(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out,
                           void *ctx);

/* A text field's edit, for models that must check each one (Quillon keeps
 * its text valid UTF-8 and drops held answers on edit). Replace bytes
 * [start, end) with s[0..n); return false to refuse. NULL: the field edits
 * its buffer in place. */
typedef bool (*zui_edit_fn)(void *ctx, uint32_t start, uint32_t end, const char *s, uint32_t n);

/* A canvas widget paints itself. */
typedef void (*zui_paint_fn)(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx);

typedef struct {
    char *buf; /* the caller's text, NUL-terminated              */
    uint32_t cap;
    uint32_t len;
    uint32_t caret, anchor; /* selection is [min, max) of the two      */
    int32_t scroll_x, scroll_y;
    int32_t want_x; /* the column up/down keeps, in px (-1 = none) */
    bool multiline, readonly, secret;
    zui_edit_fn edit;
    void *edit_ctx;
    char preedit[ZUI_PREEDIT_MAX + 1]; /* I4: shown at the caret, not in buf */
    char placeholder[ZUI_LABEL_MAX];
} zui_field_t;

typedef struct {
    int32_t rows, sel, top; /* top = first visible row */
    int32_t row_pt;         /* row height in points (0 = from the input) */
    zui_row_fn row;
    void *row_ctx;
} zui_list_t;

typedef struct {
    zui_kind_t kind;
    zui_id_t parent, first, last, next;
    zui_rect_t r; /* laid out, device pixels */
    /* layout */
    zui_dir_t dir;
    zui_sizing_t sizing;
    int32_t size;     /* points (FIXED) or weight (FLEX)          */
    int32_t pad, gap; /* points                                    */
    int32_t min_pt;   /* minimum along the main axis, points       */
    /* state */
    bool hidden, disabled, focusable;
    bool panel; /* a box drawn as a carved panel             */
    zui_btn_style_t style;
    zui_font_role_t font;
    uint32_t color; /* a label's colour (0 = the text token)  */
    char label[ZUI_LABEL_MAX];
    char name[ZUI_NAME_MAX]; /* W6: accessible name when not the label */
    char shortcut[16];       /* shown on a button ("⌘S")               */
    char tip[ZUI_NAME_MAX];  /* W10: the tooltip ("" none)             */
    bool wrap;               /* a row that wraps onto more lines        */
    int32_t tag;             /* the app's own number                   */
    union {
        zui_field_t field;
        zui_list_t list;
        struct {
            int32_t offset, content_h;
        } scroll;
        struct {
            uint32_t p;   /* 0..256 or ZUI_BAR_UNKNOWN */
            uint8_t mode; /* zui_bar_mode_t            */
        } bar;
        struct {
            zui_paint_fn paint;
            void *ctx;
        } canvas;
        struct {
            uint8_t state; /* W8: 0 off, 1 on, 2 mixed (check box only) */
        } check;
        struct {
            int32_t value, min, max, step; /* W8: the slider */
        } range;
        struct {
            int32_t sel;    /* W8: the chosen tab                   */
            bool segmented; /* carved segments instead of tabs     */
        } tabs;
        struct {
            zui_id_t back; /* W9: focus to restore on close */
            bool sheet;    /* laid out as a bottom sheet      */
        } dialog;
        struct {
            uint8_t icon; /* W12: zui_icon_t */
        } empty;
        struct {
            char agent[32];
            uint64_t tokens;
            bool exact;
            uint8_t state; /* ZXM_HELD_* numbering: 1 pending 2 accepted 3 rejected */
        } held;
    } u;
} zui_widget_t;

typedef struct {
    zui_text_t body, label, title, mono;
} zui_theme_t;

/* W14: one line of a field in visual order. c[] is in logical order; each
 * character has its bytes [a, b), its box [x0, x1) in 1/256 px from the
 * line's start, whether it runs right to left, and the code point to draw
 * (mirrored brackets in right-to-left runs). */
#define ZUI_CARET_MAX 256u
typedef struct {
    uint32_t n;
    bool rtl; /* the paragraph runs right to left (the line aligns right) */
    int32_t width;
    struct {
        uint32_t a, b;
        int32_t x0, x1;
        bool rtl;
        uint32_t cp;
    } c[ZUI_CARET_MAX];
} zui_caret_line_t;
/* The width in 1/256 px of bytes [a, b) of s as the widget measures them. */
typedef int32_t (*zui_measure_fn)(const zui_text_t *t, const char *s, uint32_t a, uint32_t b);
/* W14: lay out bytes [a, b) of s (len bytes in all; the paragraph is
 * found around [a, b)). False: draw this line left to right. */
typedef bool (*zui_caret_fn)(const zui_text_t *t, zui_measure_fn measure, const char *s,
                             uint32_t len, uint32_t a, uint32_t b, zui_caret_line_t *out);

/* The modal layer: one menu or the palette at a time. */
typedef struct {
    bool open;
    bool palette;
    zui_id_t owner; /* the widget the menu belongs to (gets PICK) */
    zui_rect_t r;
    int32_t hot; /* highlighted item (in the filtered list for the palette) */
    uint32_t n;
    char item[ZUI_MENU_ITEMS][ZUI_LABEL_MAX];
    char key[ZUI_MENU_ITEMS][16];
    bool off[ZUI_MENU_ITEMS];
} zui_menu_t;

typedef struct {
    const char *name, *agent; /* zxm_ai_action_t's fields */
    uint32_t est_tokens;
} zui_palette_item_t;

typedef struct {
    bool open;
    zui_id_t owner; /* who hears PICK / ASK (usually the view's root) */
    zui_palette_item_t item[ZUI_PALETTE_MAX];
    uint32_t n;
    int32_t match[ZUI_PALETTE_MAX]; /* filtered: indexes into item */
    uint32_t n_match;
    int32_t hot;
    char query[256];
    zui_field_t q;
    zui_rect_t r;
    char context[ZUI_LABEL_MAX]; /* what the palette will send ("3 files selected") */
} zui_palette_t;

struct zui_ui {
    zui_widget_t w[ZUI_WIDGETS];
    uint32_t n;
    zui_id_t root, focus, hover, pressed;
    bool focus_visible; /* W3: the last focus move came from the keyboard */
    zui_screen_t screen;
    zui_plan_t plan;
    zui_theme_t theme;
    zui_damage_t damage;
    zui_gesture_state_t gesture;
    zui_menu_t menu;
    zui_palette_t palette;
    zui_on_fn on;
    void *on_ctx;
    bool layout_dirty;
    bool reduce_motion;
    uint32_t now_ms;
    uint32_t caret_ms; /* caret blink phase start */
    int32_t drag_list;
    /* Clipboard, supplied by the present layer (NULL: an internal one). */
    bool (*clip_set)(void *ctx, const char *s, uint32_t n);
    uint32_t (*clip_get)(void *ctx, char *out, uint32_t cap);
    void *clip_ctx;
    char clip_local[1024];
    uint32_t clip_local_len;
    uint32_t overflow; /* widgets refused because the pool was full */
    /* zui_palette.h C6: the colour set painted with (NULL: dark, zui_tokens.h) */
    const zui_colors_t *colors;
    zui_id_t modal;     /* W9: the open dialog, ZUI_NONE for none   */
    zui_id_t tip_for;   /* W10: the widget the tip waits on or shows */
    uint32_t tip_since; /* when it started waiting                   */
    bool tip_kbd;       /* it came from keyboard focus               */
    zui_rect_t tip_r;   /* where it was drawn (damage)               */
    zui_caret_fn caret; /* W14: NULL left to right                   */
};

/* ---- building --------------------------------------------------------- */

void zui_ui_init(zui_ui_t *ui, const zui_screen_t *screen, const zui_theme_t *theme);
void zui_ui_on(zui_ui_t *ui, zui_on_fn on, void *ctx);
/* zui_palette.h C6: paint with this colour set from now on (NULL: dark).
 * Borrowed, not copied. Every colour the widgets draw is a role in it; a
 * colour an app stored as a zui_tokens.h value (zui_set_font, a row's
 * lead_rgb) names its role and follows the set too. Repaints everything. */
void zui_ui_set_colors(zui_ui_t *ui, const zui_colors_t *colors);
uint32_t zui_ui_color(const zui_ui_t *ui, zui_color_role_t role); /* for canvases */
/* Remove every widget (a view switching screens rebuilds its tree). */
void zui_ui_clear(zui_ui_t *ui);

/* Each returns the new widget's id, or ZUI_NONE when the pool is full (the
 * builders accept ZUI_NONE as a parent and do nothing, so a full pool never
 * crashes a view). parent ZUI_NONE with an empty tree makes the root. */
zui_id_t zui_box(zui_ui_t *ui, zui_id_t parent, zui_dir_t dir);
zui_id_t zui_panel(zui_ui_t *ui, zui_id_t parent, zui_dir_t dir); /* a carved box */
zui_id_t zui_label(zui_ui_t *ui, zui_id_t parent, const char *text);
zui_id_t zui_button(zui_ui_t *ui, zui_id_t parent, const char *text, zui_btn_style_t style);
zui_id_t zui_list(zui_ui_t *ui, zui_id_t parent, zui_row_fn row, void *ctx);
zui_id_t zui_field(zui_ui_t *ui, zui_id_t parent, char *buf, uint32_t cap, bool multiline);
zui_id_t zui_scroll(zui_ui_t *ui, zui_id_t parent);
zui_id_t zui_held(zui_ui_t *ui, zui_id_t parent); /* W4: builds its text and two buttons */
zui_id_t zui_progress(zui_ui_t *ui, zui_id_t parent);
zui_id_t zui_canvas(zui_ui_t *ui, zui_id_t parent, zui_paint_fn paint, void *ctx);
zui_id_t zui_separator(zui_ui_t *ui, zui_id_t parent);
/* W8 */
zui_id_t zui_toggle(zui_ui_t *ui, zui_id_t parent, const char *text, bool on);
zui_id_t zui_check(zui_ui_t *ui, zui_id_t parent, const char *text, bool on);
zui_id_t zui_radio(zui_ui_t *ui, zui_id_t parent, const char *text, bool on);
zui_id_t zui_slider(zui_ui_t *ui, zui_id_t parent, int32_t min, int32_t max, int32_t step,
                    int32_t value);
/* Tabs (segmented false) or a segmented control, with n labels; sel chosen. */
zui_id_t zui_tabs(zui_ui_t *ui, zui_id_t parent, const char *const *labels, uint32_t n, int32_t sel,
                  bool segmented);
/* W9: a dialog over the whole screen with this title; add its content and
 * buttons as children of the id returned. Hidden until opened. */
zui_id_t zui_dialog(zui_ui_t *ui, const char *title);
void zui_dialog_open(zui_ui_t *ui, zui_id_t dialog);
void zui_dialog_close(zui_ui_t *ui, zui_id_t dialog);
/* W12: action NULL draws no button. The button is the empty state's last
 * child; a press on it reaches the app as ZUI_ACT_PRESS on the empty id. */
zui_id_t zui_empty(zui_ui_t *ui, zui_id_t parent, zui_icon_t icon, const char *line,
                   const char *action);

zui_widget_t *zui_w(zui_ui_t *ui, zui_id_t id); /* NULL for ZUI_NONE or out of range */

/* ---- state (each marks the widget's damage when it changes) ---------- */

void zui_set_text(zui_ui_t *ui, zui_id_t id, const char *text);
void zui_set_name(zui_ui_t *ui, zui_id_t id, const char *name);
void zui_set_hidden(zui_ui_t *ui, zui_id_t id, bool hidden);
void zui_set_disabled(zui_ui_t *ui, zui_id_t id, bool disabled);
void zui_set_size(zui_ui_t *ui, zui_id_t id, zui_sizing_t sizing, int32_t size);
void zui_set_pad(zui_ui_t *ui, zui_id_t id, int32_t pad, int32_t gap);
void zui_set_font(zui_ui_t *ui, zui_id_t id, zui_font_role_t role, uint32_t rgb);
void zui_set_shortcut(zui_ui_t *ui, zui_id_t id, const char *keys);
void zui_list_set_rows(zui_ui_t *ui, zui_id_t list, int32_t rows); /* keeps sel in range */
void zui_list_select(zui_ui_t *ui, zui_id_t list, int32_t row);    /* scrolls it into view */
void zui_field_set(zui_ui_t *ui, zui_id_t field, uint32_t len);    /* after the app edits buf */
void zui_field_select(zui_ui_t *ui, zui_id_t field, uint32_t anchor, uint32_t caret);
void zui_field_selection(const zui_ui_t *ui, zui_id_t field, uint32_t *start, uint32_t *end);
void zui_held_set(zui_ui_t *ui, zui_id_t held, const char *agent, uint64_t tokens, bool exact,
                  const char *text, uint8_t state);
void zui_progress_set(zui_ui_t *ui, zui_id_t bar, uint32_t p, uint8_t mode);
void zui_set_tooltip(zui_ui_t *ui, zui_id_t id, const char *tip); /* W10 */
void zui_set_wrap(zui_ui_t *ui, zui_id_t row, bool wrap);         /* a row of buttons wraps */
/* W8: a toggle's, check box's or radio's state (2 = mixed, check boxes
 * only); choosing a radio clears the others of its box. */
void zui_set_checked(zui_ui_t *ui, zui_id_t id, uint8_t state);
uint8_t zui_checked(const zui_ui_t *ui, zui_id_t id);
void zui_slider_set(zui_ui_t *ui, zui_id_t slider, int32_t value); /* clamped, on a step */
int32_t zui_slider_value(const zui_ui_t *ui, zui_id_t slider);
void zui_tabs_select(zui_ui_t *ui, zui_id_t tabs, int32_t sel);
/* W8: the index of a radio among its box's radios, or of a tab. */
int32_t zui_choice_index(const zui_ui_t *ui, zui_id_t id);
void zui_invalidate(zui_ui_t *ui, zui_id_t id); /* repaint (a canvas whose data changed) */
void zui_relayout(zui_ui_t *ui);                /* after a resize or a structural change */

/* ---- focus ------------------------------------------------------------ */

void zui_focus(zui_ui_t *ui, zui_id_t id, bool from_keyboard);
zui_id_t zui_focus_next(zui_ui_t *ui, bool backward); /* W3: Tab order */
/* W3: the nearest focusable widget in a direction (ZUI_KEY_LEFT..DOWN). */
zui_id_t zui_focus_toward(zui_ui_t *ui, uint16_t arrow);

/* ---- menus and the Ask palette ----------------------------------------- */

/* Open a menu for `owner` at (x, y) device pixels. items may contain "-" for
 * a separator; keys (may be NULL) are the shortcuts shown at the right. */
void zui_menu_open(zui_ui_t *ui, zui_id_t owner, int32_t x, int32_t y, const char *const *items,
                   const char *const *keys, uint32_t n);
void zui_menu_disable(zui_ui_t *ui, uint32_t item, bool off);
void zui_menu_close(zui_ui_t *ui);

/* W5: the palette lists `items` (an app's zxm_ai_action_t table, copied as
 * pointers: the table must be static). context says what it will act on. */
void zui_palette_set(zui_ui_t *ui, zui_id_t owner, const zui_palette_item_t *items, uint32_t n);
void zui_palette_open(zui_ui_t *ui, const char *context);
void zui_palette_close(zui_ui_t *ui);
/* Case-insensitive subsequence match: "smz" finds "Summarize". */
bool zui_palette_matches(const char *query, const char *name);

/* ---- running ------------------------------------------------------------ */

/* Handle one event; returns true when it was used. */
bool zui_ui_event(zui_ui_t *ui, const zui_event_t *e);
/* Lay out if needed and repaint what is damaged into s; the damage is left
 * in ui->damage for the present layer, then cleared by the next paint. */
void zui_ui_paint(zui_ui_t *ui, zui_surface_t *s);
/* One frame: poll p into q, handle every event, paint, present. Returns
 * false when the window closed. */
bool zui_ui_frame(zui_ui_t *ui, zui_present_t *p, zui_queue_t *q, zui_surface_t *s,
                  uint32_t wait_ms);

zui_id_t zui_hit(const zui_ui_t *ui, int32_t x, int32_t y); /* deepest widget at a point */
/* The list row at a point, or -1. */
int32_t zui_list_row_at(const zui_ui_t *ui, zui_id_t list, int32_t x, int32_t y);
/* W11: the scroll indicator's track of a list, scroll area or text area
 * (all zero when everything fits). */
zui_rect_t zui_scroll_track(const zui_ui_t *ui, zui_id_t id);
/* W10: the tooltip on screen now (r all zero and NULL when none). */
const char *zui_tooltip_shown(const zui_ui_t *ui, zui_rect_t *r);
/* W14: a field's caret as drawn: x, y of its top and its height, in px. */
void zui_field_caret_rect(const zui_ui_t *ui, zui_id_t field, zui_rect_t *out);

/* W6: the tree as a screen reader reads it, one widget per line, indented
 * by depth: "button \"Save\" focused", "list \"Files\" 12 rows, row 3 selected".
 * Returns the length written (cut to fit cap, always NUL-terminated). */
uint32_t zui_a11y_text(const zui_ui_t *ui, char *out, uint32_t cap);
const char *zui_role_name(zui_kind_t k);

#endif /* ZXV_ZUI_WIDGET_H */
