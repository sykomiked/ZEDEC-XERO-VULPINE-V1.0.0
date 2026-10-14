/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_gesture.h — touchpad gestures from raw multi-finger contacts.
 *
 * A touchpad reports where each finger is, frame by frame. This turns those
 * frames into what the person meant: move the pointer, click, scroll, zoom,
 * rotate, switch apps, show every window. (Single-finger tap, double tap
 * and long press on a touch screen are zui_input.h I3; this is the
 * touchpad's own recogniser, named zui_tpad_*.)
 *
 *   G1 CONTACTS. Each frame lists the fingers down now, with a stable id
 *      per finger and its contact size in pixels; a finger missing from a
 *      frame has lifted. Time is the frame's milliseconds, never a clock.
 *   G2 PALMS. A contact larger than palm_px when it lands is a palm and is
 *      ignored until it lifts, however small it later reads.
 *   G3 ONE FINGER moves the pointer (scaled by speed_q8). A tap (down and
 *      up within tap_ms, no finger moving more than tap_slop_px) clicks
 *      when tap_to_click is on: one finger = primary, two = secondary,
 *      three = middle. Fingers may land and lift a frame or two apart.
 *   G4 TWO FINGERS lock into the first of: zoom (their distance changes by
 *      pinch_px; scale_q8 = distance now / distance at the start x256),
 *      rotate (their angle turns by rotate_deg; angle in whole degrees,
 *      clockwise positive), or scroll (they move together by scroll_px).
 *      Each runs BEGIN, UPDATE..., END. Scroll deltas follow zui_input.h
 *      (+dy = content up); with natural on, content follows the fingers.
 *   G5 INERTIA. Lifting both fingers while scrolling fast keeps the scroll
 *      going (phase INERTIA) from zui_tpad_tick, slowing by decay_q8 every
 *      16 ms, until it is under a quarter pixel a frame: then END. Touching
 *      again stops it (END at once). All integer, fixed-point q8.
 *   G6 THREE FINGERS swipe once per touch when their centre moves swipe_px:
 *      left or right switch apps (ZUI_TPAD_SWIPE_LEFT = next app, RIGHT =
 *      previous), up shows all windows, down shows the desktop. FOUR
 *      FINGERS pinching in or out by pinch_px fire once per touch.
 *   G7 A CHANGE OF FINGER COUNT ends the running gesture and starts
 *      looking again from where the fingers are; a tap is no longer
 *      possible after any gesture locked.
 *   G8 zui_tpad_events turns pointer moves, clicks and scrolls into
 *      ordinary zui_event_t; the rest are for the window manager. No
 *      allocation, integer only, deterministic.
 */
#ifndef ZXV_ZUI_GESTURE_H
#define ZXV_ZUI_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_input.h"

#define ZUI_TPAD_CONTACTS 10u
#define ZUI_TPAD_EV_MAX   4u /* events one frame or tick can produce */
#define ZUI_TPAD_FRAME_MS 16u

typedef struct {
    uint16_t id;
    int32_t x, y;
    int32_t size; /* the contact's long axis, pixels */
} zui_tpad_contact_t;

typedef enum {
    ZUI_TPAD_NONE = 0,
    ZUI_TPAD_MOVE,            /* dx, dy pointer motion; x, y the new pointer */
    ZUI_TPAD_CLICK,           /* G3                                        */
    ZUI_TPAD_SECONDARY_CLICK, /* two-finger tap                            */
    ZUI_TPAD_MIDDLE_CLICK,    /* three-finger tap                          */
    ZUI_TPAD_SCROLL,          /* G4, G5: dx, dy content delta, phase       */
    ZUI_TPAD_ZOOM,            /* scale_q8, phase                           */
    ZUI_TPAD_ROTATE,          /* angle (degrees), phase                    */
    ZUI_TPAD_SWIPE_LEFT,      /* G6: next app                              */
    ZUI_TPAD_SWIPE_RIGHT,     /* previous app                              */
    ZUI_TPAD_SWIPE_UP,        /* show all windows                          */
    ZUI_TPAD_SWIPE_DOWN,      /* show the desktop                          */
    ZUI_TPAD_PINCH4_IN,
    ZUI_TPAD_PINCH4_OUT
} zui_tpad_kind_t;

#define ZUI_TPAD_APP_NEXT    ZUI_TPAD_SWIPE_LEFT
#define ZUI_TPAD_APP_PREV    ZUI_TPAD_SWIPE_RIGHT
#define ZUI_TPAD_ALL_WINDOWS ZUI_TPAD_SWIPE_UP

typedef enum {
    ZUI_TPAD_BEGIN = 0,
    ZUI_TPAD_UPDATE,
    ZUI_TPAD_END,
    ZUI_TPAD_INERTIA
} zui_tpad_phase_t;

typedef struct {
    uint8_t kind;  /* zui_tpad_kind_t  */
    uint8_t phase; /* zui_tpad_phase_t */
    uint8_t fingers;
    int32_t x, y;   /* centre of the fingers (the pointer for MOVE/clicks) */
    int32_t dx, dy; /* MOVE: pointer delta; SCROLL: content delta          */
    int32_t scale_q8;
    int32_t angle;
    uint32_t ms;
} zui_tpad_ev_t;

typedef struct {
    int32_t tap_slop_px; /* G3 */
    uint32_t tap_ms;
    int32_t scroll_px; /* G4 */
    int32_t pinch_px;
    int32_t rotate_deg;
    int32_t swipe_px; /* G6 */
    int32_t palm_px;  /* G2 */
    int32_t speed_q8; /* pointer speed, 256 = 1:1 */
    bool natural;     /* content follows the fingers */
    bool tap_to_click;
    int32_t inertia_min_q8; /* G5: least speed to coast, px per frame x256 */
    int32_t decay_q8;       /* speed kept per frame, x256                  */
} zui_tpad_cfg_t;

typedef struct {
    uint16_t id;
    bool palm;
    int32_t x, y;   /* now                          */
    int32_t sx, sy; /* where it landed (tap slop)    */
} zui_tpad_finger_t;

typedef struct {
    zui_tpad_cfg_t cfg;
    zui_tpad_finger_t f[ZUI_TPAD_CONTACTS];
    uint32_t nf;
    uint8_t state; /* internal */
    uint8_t fingers, max_fingers;
    bool tap_ok;
    uint32_t t0;
    int32_t cx0, cy0;   /* centre when the count last changed */
    int32_t lcx, lcy;   /* centre last frame                  */
    int32_t dist0;      /* two fingers: distance at the start */
    int32_t ldist;      /* two fingers: distance last frame   */
    int32_t ang0, lang; /* angle at the start, last frame     */
    int32_t ang_acc;
    int32_t spread0; /* four fingers: mean distance from the centre */
    int32_t rem_x, rem_y;
    /* G5 */
    int32_t vx_q8, vy_q8;
    uint32_t last_scroll_ms;
    bool coasting;
    uint32_t coast_ms;
    int32_t acc_x_q8, acc_y_q8;
    /* the pointer, for G8 */
    int32_t px, py, bound_w, bound_h;
} zui_tpad_t;

void zui_tpad_init(zui_tpad_t *t);
void zui_tpad_defaults(zui_tpad_cfg_t *c);
/* The pointer's range for MOVE (0..w-1, 0..h-1); 0 x 0 = unbounded. */
void zui_tpad_set_bounds(zui_tpad_t *t, int32_t w, int32_t h, int32_t px, int32_t py);
/* G1: one frame of contacts. Writes up to ZUI_TPAD_EV_MAX gestures. */
uint32_t zui_tpad_frame(zui_tpad_t *t, const zui_tpad_contact_t *c, uint32_t n, uint32_t ms,
                        zui_tpad_ev_t *out, uint32_t cap);
/* G5: time passing with no frame. */
uint32_t zui_tpad_tick(zui_tpad_t *t, uint32_t ms, zui_tpad_ev_t *out, uint32_t cap);
/* G8: the ordinary events for one gesture (0..2). */
uint32_t zui_tpad_events(const zui_tpad_t *t, const zui_tpad_ev_t *g, zui_event_t *out,
                         uint32_t cap);

/* Integer geometry, exposed for tests and other recognisers. */
int32_t zui_tpad_atan2_deg(int32_t y, int32_t x); /* 0..359, clockwise from +x (y down) */
int32_t zui_tpad_isqrt(int64_t v);

#endif /* ZXV_ZUI_GESTURE_H */
