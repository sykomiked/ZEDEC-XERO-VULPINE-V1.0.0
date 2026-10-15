/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This screen shows an agreement and reports whether a person
 * accepted it. It does not provide legal advice or decide whether an
 * agreement is enforceable anywhere.
 */
/* zui_terms.h — the first-boot agreement screen.
 *
 * After a person picks their seat and unlocks it, the shell asks
 * terms_log_status (../terms/terms.h). When the answer is not CURRENT it
 * opens this screen before the desktop; zui_login knows nothing about it.
 * The logic (wrapping, the read gate, [KEY] clauses, the acceptance record)
 * is the terms module's; this file lays it out, draws it and turns input
 * into terms_* calls.
 *
 *   T1 LOOK. The #002911 ground; one carved ZUI_PANEL card with a gold edge,
 *      as wide as reads well (ZUI_TERMS_MAX_PT) and narrower on a phone. In
 *      it: the text in ZUI_TEXT with headings brighter, a gold bar beside
 *      every line of a [KEY] clause, a scroll bar, the hint line, and two
 *      buttons. "I agree" is gold with ZUI_VOID text once it may be pressed
 *      and a flat raised step before; "Decline" is an outlined button.
 *   T2 TEXT MEASURED BY THE REAL FONT. Lines wrap by the font's advances
 *      (quarter pixels), so what is drawn is what was measured.
 *   T3 EVERY ROUTE READS. Wheel and touch drag scroll (TERMS_VIA_SCROLL);
 *      Up/Down, Page Up/Down, Space, Home and End move by keyboard
 *      (TERMS_VIA_KEYBOARD); a click on the scroll bar track pages. Tab and
 *      Shift-Tab move focus between the text, Decline and I agree; Enter or
 *      Space on a button presses it. A screen reader reports its progress
 *      with zui_terms_spoke. There is no timer.
 *   T4 PRESSING. A pointer press counts when it goes down and up on the same
 *      button. "I agree" does nothing until the end has been shown; then
 *      the result is ACCEPTED and the shell calls terms_accept and appends
 *      the record. "Decline" gives DECLINED and records nothing.
 *   T5 RESIZE keeps the place and the read state (terms_reader_resize).
 *   T6 CONTENTS. A list of the sections sits beside the text on a wide
 *      screen, and behind a "Contents" button on a narrow one. Its first
 *      entry, the short version (the summary at the top), is pinned and never
 *      scrolls away; [KEY] sections carry the same gold bar as their text;
 *      the section being read is lit. Choosing an entry jumps there
 *      (TERMS_VIA_SECTION) so a person can go straight to the clauses that
 *      limit liability or decide disputes. The read gate is unchanged: the
 *      end still has to be shown before "I agree" works.
 *   T7 CONTRAST. Every text colour on the card is at least 4.5:1 against
 *      what it is drawn on (WCAG 2.1 AA, 1.4.3), and the outlines a person
 *      needs to find a control (Decline, the scroll thumb) at least 3:1
 *      (1.4.11). The disabled "I agree" label is held to 4.5:1 as well,
 *      though WCAG exempts disabled controls. test_zui_terms checks each
 *      pair.
 *
 * Integer only, no allocation; the caller owns the reader (it is large, so
 * keep it static) and the fonts. */
#ifndef ZXV_ZUI_TERMS_H
#define ZXV_ZUI_TERMS_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_layout.h"
#include "../terms/terms.h"

#define ZUI_TERMS_MAX_PT  720 /* widest text column, points */
#define ZUI_TERMS_LIST_PT 272 /* contents column beside it, points */
#define ZUI_TERMS_WIDE_PT 880 /* a card this wide (points) shows the list beside the text */

typedef enum { ZUI_TERMS_OPEN = 0, ZUI_TERMS_ACCEPTED, ZUI_TERMS_DECLINED } zui_terms_result_t;

typedef enum {
    ZUI_TERMS_FOCUS_TEXT = 0,
    ZUI_TERMS_FOCUS_DECLINE,
    ZUI_TERMS_FOCUS_AGREE,
    ZUI_TERMS_FOCUS_CONTENTS
} zui_terms_focus_t;

/* Where things are (device pixels). Tab order: contents, text, Decline,
 * I agree. */
typedef struct {
    zui_rect_t card, text, bar, hint, decline, agree;
    zui_rect_t contents; /* the section list (wide), or where it opens (narrow) */
    zui_rect_t toggle;   /* narrow only: the "Contents" button; w == 0 when wide */
    int32_t line_h, pad;
    int32_t entry_h; /* one contents row */
} zui_terms_layout_t;

typedef struct {
    terms_reader_t *r;
    const zui_text_t *font;
    zui_screen_t screen;
    zui_rect_t area;
    zui_terms_layout_t lay;
    zui_terms_result_t result;
    zui_terms_focus_t focus;
    int32_t wheel_px; /* wheel pixels not yet a whole line */
    int32_t drag_y;   /* last touch/pointer y while dragging the text */
    int8_t pressed;   /* under a pointer press: 0 none, 1 decline, 2 agree, 3 contents */
    bool dragging;
    bool list_open;      /* narrow: the contents list shows instead of the text */
    int16_t entry_down;  /* contents entry under a pointer press, -1 none */
    uint16_t list_top;   /* first scrolled entry, below the pinned one */
    uint16_t list_sel;   /* keyboard selection in the contents list */
    const char *summary; /* label of the pinned first entry; the shell may translate it */
} zui_terms_t;

/* Lay out in `area` for `screen` and start reading `doc`. Returns
 * TERMS_OK or a terms error (TERMS_ERR_ARG when the area is too small). */
int zui_terms_open(zui_terms_t *t, terms_reader_t *r, const terms_doc_t *doc,
                   const zui_text_t *font, const zui_screen_t *screen, zui_rect_t area);
/* T5: the window or text size changed. */
int zui_terms_resize(zui_terms_t *t, const zui_screen_t *screen, zui_rect_t area);
/* T3, T4. Returns true when the event was used. */
bool zui_terms_event(zui_terms_t *t, const zui_event_t *e);
/* T6: entries in the contents list (the pinned summary plus one per
 * numbered section), and the one holding the top visible line. */
uint32_t zui_terms_entries(const zui_terms_t *t);
uint32_t zui_terms_current(const zui_terms_t *t);
/* T6: jump to entry `i` (0 = the short version) as a section-list read. */
int zui_terms_go(zui_terms_t *t, uint32_t i);
/* T3: a screen reader has spoken up to byte `off` of the text. */
void zui_terms_spoke(zui_terms_t *t, uint32_t off);
void zui_terms_draw(zui_surface_t *s, const zui_terms_t *t);
static inline zui_terms_result_t zui_terms_result(const zui_terms_t *t)
{
    return t->result;
}

#endif /* ZXV_ZUI_TERMS_H */
