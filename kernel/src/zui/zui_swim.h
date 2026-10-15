/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_swim.h — the chiglet swimming over the screen.
 *
 * The chiglet lives on a transparent layer above every window. When its
 * companion has something to offer about a window or a part of one, it swims
 * there, fading as it passes through other windows as if it went into the
 * glass and out again, and waits beside the place it can help with. Nothing
 * happens until the person taps it. It is the companion drawn by
 * zui_chiglet_draw from its own genes, moved by zui_motion, set at its depth
 * by zui_depth, and it offers only what the companion gate (companion.h
 * cmp_decide) let through.
 *
 *   SW1 THE LAYER. zui_swim_draw paints into whatever surface it is given,
 *       after the windows (the top layer). Only the chiglet's body, and its
 *       bubble while open, take a pointer: zui_swim_hit is false everywhere
 *       else, so every other click goes to the window below. It never takes
 *       the keyboard and never raises or focuses a window. The shell calls
 *       it right after zui_wm_draw (zui_wm has no overlay plane; present
 *       blits the finished frame). With the depth pixel filter on, it draws
 *       before the filter's whole-frame pass and stamps zui_swim_extent
 *       into the filter's plane at zui_swim_depth, so it lifts like the
 *       rest of the screen instead of sitting flat on top.
 *   SW2 OFFERS COME FROM THE GATE. zui_swim_offer takes the gate's decision:
 *       PROCEED and ASK may be offered; WAIT, REFUSE, a quarantined decision
 *       or one the gate did not log (seq 0) are not. An offer is a target
 *       rectangle, the proposal's digest and a short text (at most
 *       ZUI_SWIM_TEXT - 1 bytes). One offer at a time: a new one replaces a
 *       waiting one, never an open one.
 *   SW3 A SUGGESTION UNTIL ACCEPTED. Offering runs nothing. A tap on the body
 *       opens the bubble (the only way the text appears); the bubble's
 *       Accept hands the offer's digest back to the caller, who runs it
 *       through its own gate. Not now dismisses it; Pin keeps the chiglet in
 *       its corner. The chiglet never speaks or opens by itself.
 *   SW4 DISMISS, PIN, AWAY. Not now drops the offer and refuses new ones for
 *       ZUI_SWIM_SNOOZE_MS. Pinned, the chiglet rests at the work area's
 *       bottom-right corner, never swims, and shows a gold dot when an offer
 *       waits. Away hides it and refuses every offer until it is turned off.
 *   SW5 MOTION. Swimming is ambient motion like an icon's (zui_kinetic.h
 *       KI2), not a transition: a curve from where it is to beside the
 *       target, ZUI_SWIM_TRAVEL_MS long, eased, with a side-to-side wave that
 *       is zero at both ends. While it waits it bobs with a 4000 ms period.
 *       Fades in and out last ZUI_SWIM_FADE_MS (under ZUI_MOTION_MS). With
 *       reduce motion (zui_motion.h MO3) nothing travels, bobs or waves: the
 *       chiglet fades in beside the target (FADE) or is simply there (CUT).
 *   SW6 THROUGH THE GLASS. While its centre is inside a window that is not
 *       the target's, it is drawn at ZUI_SWIM_THROUGH_A and at depth
 *       -ZUI_SWIM_DEPTH_PX (behind the glass, zui_depth S-); beside its
 *       target, waiting, it sits at the glass (z 0, S0: held, undecided).
 *       Alpha moves toward its goal at most 255 per ZUI_SWIM_FADE_MS. Reduce
 *       transparency (MO6) draws it opaque whenever it shows.
 *   SW7 WHERE IT WAITS. Beside the target: to the right if there is room in
 *       the work area, else to the left, else above, else inside the
 *       target's top-right corner; always wholly inside the work area.
 *   SW8 TIME. zui_swim_tick takes the caller's monotonic millisecond frame
 *       clock; differences are modulo 2^32 and a clock that steps back
 *       counts as no time. The same calls with the same clock give the same
 *       positions, alphas and pixels.
 *   SW9 WORDS. zui_swim_describe writes what a screen reader says about it
 *       ("Your chiglet has a suggestion. Open it to read it."), and
 *       zui_swim_open opens the bubble as a tap would, for the keyboard and
 *       switch access the shell maps to it.
 *
 * Integer only, no allocation (the two scratch drawings live in the struct),
 * no libc, deterministic. Not thread safe: call it from the UI thread.
 */
#ifndef ZXV_ZUI_SWIM_H
#define ZXV_ZUI_SWIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../companion/companion.h"
#include "zui_canvas.h"
#include "zui_chiglet.h"
#include "zui_depth.h"
#include "zui_layout.h"

#define ZUI_SWIM_R         24u    /* body radius, pixels                  */
#define ZUI_SWIM_BOX       112    /* scratch side: the drawing's extent    */
#define ZUI_SWIM_TEXT      160u   /* SW2: offer text buffer, bytes         */
#define ZUI_SWIM_WINDOWS   32u    /* SW6: windows it can swim through      */
#define ZUI_SWIM_TRAVEL_MS 1400u  /* SW5 */
#define ZUI_SWIM_BOB_MS    4000u  /* SW5 */
#define ZUI_SWIM_FADE_MS   160u   /* SW5, SW6 */
#define ZUI_SWIM_SNOOZE_MS 60000u /* SW4 */
#define ZUI_SWIM_THROUGH_A 90u    /* SW6 */
#define ZUI_SWIM_DEPTH_PX  12     /* SW6 */
#define ZUI_SWIM_WAVE_PX   18     /* SW5 */
#define ZUI_SWIM_GAP       8      /* SW7: space between target and body    */
#define ZUI_SWIM_BUBBLE_W  260    /* the bubble                            */
#define ZUI_SWIM_BUBBLE_H  96
#define ZUI_SWIM_BUTTON_H  24

typedef enum {
    ZUI_SWIM_HIDDEN = 0, /* nothing on the layer                      */
    ZUI_SWIM_ARRIVING,   /* swimming to beside the target             */
    ZUI_SWIM_WAITING,    /* beside the target, the offer held         */
    ZUI_SWIM_OPEN,       /* tapped: the bubble shows the text         */
    ZUI_SWIM_LEAVING,    /* fading out                                */
    ZUI_SWIM_PINNED      /* resting in the corner (SW4)               */
} zui_swim_state_t;

typedef enum {
    ZUI_SWIM_NONE = 0, /* the tap was not on the chiglet: pass it through */
    ZUI_SWIM_OPENED,   /* the bubble opened                               */
    ZUI_SWIM_CLOSED,   /* a tap on the body closed the bubble             */
    ZUI_SWIM_ACCEPTED, /* Accept: the digest is handed back                */
    ZUI_SWIM_DISMISSED,
    ZUI_SWIM_PIN,  /* pinned                                          */
    ZUI_SWIM_UNPIN /* unpinned                                        */
} zui_swim_act_t;

typedef struct {
    zui_rect_t work; /* the screen area it may use              */
    zui_chiglet_t look;
    zui_swim_state_t state;
    bool pinned, away;
    /* the offer (SW2) */
    bool has_offer;
    zui_rect_t target;
    uint8_t digest[CMP_HASH];
    char text[ZUI_SWIM_TEXT];
    /* motion (SW5, SW8), positions in 1/256 px */
    int32_t x, y;           /* where it is drawn (centre)            */
    int32_t from_x, from_y; /* where this swim started               */
    int32_t to_x, to_y;     /* where it waits                        */
    uint32_t now, started;  /* frame clock, ms                       */
    bool clock_set;
    uint32_t alpha; /* 0..255                                */
    uint32_t snooze_until;
    bool snoozing;
    /* the windows it swims through (SW6) */
    zui_rect_t win[ZUI_SWIM_WINDOWS];
    uint32_t nwin;
    /* scratch drawings on black and on white (alpha recovery) */
    uint32_t on_black[ZUI_SWIM_BOX * ZUI_SWIM_BOX];
    uint32_t on_white[ZUI_SWIM_BOX * ZUI_SWIM_BOX];
} zui_swim_t;

/* Hidden, with this look, using the work area; the first tick sets the clock. */
void zui_swim_init(zui_swim_t *sw, zui_rect_t work, const zui_chiglet_t *look);
void zui_swim_set_look(zui_swim_t *sw, const zui_chiglet_t *look);
void zui_swim_set_work(zui_swim_t *sw, zui_rect_t work);
/* SW6: the windows, any order; their union is what it swims through. */
void zui_swim_set_windows(zui_swim_t *sw, const zui_rect_t *r, uint32_t n);

/* SW2: true when offered. */
bool zui_swim_offer(zui_swim_t *sw, zui_rect_t target, const cmp_decision_t *d,
                    const uint8_t digest[CMP_HASH], const char *text);
/* The offer is no longer wanted (the caller's state moved on): leave. */
void zui_swim_withdraw(zui_swim_t *sw);

void zui_swim_tick(zui_swim_t *sw, uint32_t now_ms); /* SW8 */

/* SW1: true only on the body, and on the bubble while it is open. */
bool zui_swim_hit(const zui_swim_t *sw, int32_t x, int32_t y);
/* SW3: a tap at (x, y); digest_out (may be NULL) gets the digest on ACCEPTED. */
zui_swim_act_t zui_swim_tap(zui_swim_t *sw, int32_t x, int32_t y, uint8_t digest_out[CMP_HASH]);
zui_swim_act_t zui_swim_open(zui_swim_t *sw); /* SW9 */
void zui_swim_pin(zui_swim_t *sw, bool on);   /* SW4 */
void zui_swim_away(zui_swim_t *sw, bool on);  /* SW4 */

/* SW1, SW6: paint the layer. text may be NULL (the bubble then has no
 * words, only its buttons' outlines); depth may be NULL (flat); eye is -1, 0
 * or +1 as zui_layer_offset takes it. */
void zui_swim_draw(zui_swim_t *sw, zui_surface_t *s, const zui_text_t *text,
                   const zui_depth_t *depth, int eye);

/* Readers. */
zui_swim_state_t zui_swim_state(const zui_swim_t *sw);
void zui_swim_pos(const zui_swim_t *sw, int32_t *x, int32_t *y); /* centre, px */
uint32_t zui_swim_alpha(const zui_swim_t *sw);                   /* as drawn, 0..255 */
int32_t zui_swim_depth(const zui_swim_t *sw);                    /* SW6: z, px */
/* SW1: what this frame paints (body box, and the bubble while open). */
zui_rect_t zui_swim_extent(const zui_swim_t *sw);
zui_rect_t zui_swim_bubble(const zui_swim_t *sw); /* where the bubble is */
/* The bubble's three buttons: 0 Accept, 1 Not now, 2 Pin. */
zui_rect_t zui_swim_button(const zui_swim_t *sw, uint32_t i);
size_t zui_swim_describe(const zui_swim_t *sw, char *buf, size_t cap); /* SW9 */

#endif /* ZXV_ZUI_SWIM_H */
