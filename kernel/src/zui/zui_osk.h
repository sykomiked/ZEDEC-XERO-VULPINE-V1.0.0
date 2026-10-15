/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_osk.h — the on-screen keyboard: typing with a finger.
 *
 * A keyboard drawn on the screen, built from the same layout a hardware
 * keyboard uses (zui_keymap), that sends the same events a hardware keyboard
 * sends. Apps cannot tell the two apart.
 *
 *   O1 FROM THE LAYOUT. The letters page takes the layout's letter keys row
 *      by row (D, C, B; a letter on the digit row whose shift is not a digit
 *      joins the top row, as ذ does on Arabic). Shift shows their capitals.
 *      Symbols and numbers are fixed pages. Key widths are in quarter units
 *      (a letter key is 4); a row is centred, or its Shift / Backspace or
 *      space bar stretch to the widest row.
 *   O2 PAGES: letters, shift (one capital, then back), caps lock (shift
 *      tapped twice within ZUI_DOUBLE_MS), symbols, numbers.
 *   O3 TARGETS. Every row is at least ZUI_TOUCH_MIN points tall. A tap is
 *      matched to the key whose slot holds it; slots cover the gaps and the
 *      row ends, so no tap falls between keys. When the rect is too narrow
 *      for 44-point-wide keys, `cramped` says so (slots still cover all).
 *      The keyboard sits at the bottom of its rect; zui_osk_min_height says
 *      how tall a rect it needs.
 *   O4 SAME EVENTS. A key sends on release (sliding to another key before
 *      lifting picks that one): ZUI_EV_KEY_DOWN, ZUI_EV_TEXT for a
 *      character, ZUI_EV_KEY_UP, with ZUI_MOD_SHIFT on the shift page. Enter
 *      and Backspace are keys only. Shift and page keys send nothing.
 *   O5 REPEAT. Holding a character key, space or Backspace for
 *      repeat_delay_ms sends KEY_DOWN (and TEXT), then repeats every
 *      repeat_ms (repeat = 1) while held; release sends KEY_UP. Time comes
 *      from the events (TICK while nothing else happens), never a clock.
 *   O6 POINTER OR TOUCH. Feed pointer events or touch events. Once a touch
 *      has been seen, pointer events are ignored, since the first finger
 *      also arrives as the pointer (zui_input.h I2). A new finger while a
 *      key is held sends the held key first (fast two-thumb typing).
 *   O7 DRAWN carved into the ground: #002911 behind, keys as engraved caps
 *      of the first carved step, light green labels, and the gold accent on
 *      the Enter key only. Integer only, no allocation, deterministic.
 */
#ifndef ZXV_ZUI_OSK_H
#define ZXV_ZUI_OSK_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_palette.h"
#include "zui_input.h"
#include "zui_keymap.h"

typedef enum {
    ZUI_OSK_LETTERS = 0,
    ZUI_OSK_SHIFT,
    ZUI_OSK_SYMBOLS,
    ZUI_OSK_NUMBERS,
    ZUI_OSK_PAGES
} zui_osk_page_t;

typedef enum {
    ZUI_OSKK_CHAR = 0,
    ZUI_OSKK_SPACE,
    ZUI_OSKK_ENTER,
    ZUI_OSKK_BACKSPACE,
    ZUI_OSKK_SHIFT,
    ZUI_OSKK_PAGE
} zui_osk_kind_t;

#define ZUI_OSK_KEYS_MAX 56u
#define ZUI_OSK_ROWS     4u
#define ZUI_OSK_ROW_KEYS 15u /* most keys in one row */
#define ZUI_OSK_EV_MAX   8u  /* a sensible out[] size for zui_osk_feed */

typedef struct {
    uint8_t kind; /* zui_osk_kind_t                               */
    uint8_t row;
    uint8_t page_to; /* ZUI_OSKK_PAGE: where it goes                 */
    int8_t pos;      /* the layout position, -1 for fixed keys       */
    uint16_t qx, qw; /* left edge and width in quarter units         */
    uint16_t key;    /* the key code it sends                        */
    char label[16];  /* drawn                                        */
    char text[16];   /* sent as TEXT (characters only)               */
    zui_rect_t r;    /* the cap as drawn, pixels                     */
    zui_rect_t hit;  /* O3: the slot it answers to, pixels           */
} zui_osk_key_t;

typedef struct {
    const zui_keymap_t *km;
    uint8_t page; /* zui_osk_page_t */
    bool caps_lock;
    zui_osk_key_t keys[ZUI_OSK_KEYS_MAX];
    uint32_t nkeys;
    uint32_t nrows;
    uint16_t grid_q; /* quarter units across the widest row          */
    /* layout */
    zui_rect_t rect;
    uint32_t scale_q8;
    int32_t row_h, touch_px, top;
    bool cramped;
    /* the press in progress */
    int32_t pressed; /* key index, -1 none */
    bool touch_seen, by_touch;
    uint16_t touch_id;
    bool repeating;
    uint32_t down_ms, next_ms;
    uint32_t shift_ms; /* when Shift was last tapped                  */
    bool shift_tapped;
    uint32_t repeat_delay_ms, repeat_ms;
    const zui_colors_t *colors; /* zui_palette.h C6: the set painted with; NULL dark */
} zui_osk_t;

void zui_osk_init(zui_osk_t *o, const zui_keymap_t *km);
void zui_osk_set_keymap(zui_osk_t *o, const zui_keymap_t *km);
void zui_osk_set_page(zui_osk_t *o, uint8_t page);
/* Place the keys in rect for a panel scale (pixels per point x256). */
void zui_osk_layout(zui_osk_t *o, zui_rect_t rect, uint32_t scale_q8);
int32_t zui_osk_min_height(const zui_osk_t *o, uint32_t scale_q8);
int32_t zui_osk_hit(const zui_osk_t *o, int32_t x, int32_t y); /* key index or -1 */
/* O4..O6: one input event in, keyboard events out. Returns the count. */
uint32_t zui_osk_feed(zui_osk_t *o, const zui_event_t *e, zui_event_t *out, uint32_t cap);
/* The same, pushed onto a queue. */
uint32_t zui_osk_feed_queue(zui_osk_t *o, const zui_event_t *e, zui_queue_t *q);
/* O7: draw into the surface; t may be 0 (no labels). */
void zui_osk_paint(const zui_osk_t *o, zui_surface_t *s, const zui_text_t *t);

#endif /* ZXV_ZUI_OSK_H */
