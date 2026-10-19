/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_magnifier.h — screen zoom for people who need the screen larger:
 * the whole screen, or a lens that follows them, with optional colour filters.
 *
 * zui paints each frame into a zui_surface_t (zui_canvas.h). The magnifier
 * reads that finished surface and writes the zoomed view into a second one
 * that the present layer shows instead. Widgets never know it is there.
 *
 *   M1 TWO VIEWS. ZUI_MAG_FULL zooms the whole screen around a viewport that
 *      pans; ZUI_MAG_LENS keeps the screen at 1x and draws a zoomed lens,
 *      ringed in gold, around the point of interest.
 *   M2 INTEGER ZOOM. zoom is Q8 (256 = 1x) from 1x to 16x, snapped to quarter
 *      steps (64). zui_mag_zoom_step moves one quarter; a zoom change keeps
 *      the centre of the view where it was.
 *   M3 FOLLOW. The view follows keyboard focus, the text caret and the
 *      pointer, each switched on in `follow`. Focus and caret rectangles are
 *      brought fully into view (centred when they were out of it; their top
 *      left when they are larger than the view); the pointer pushes the view
 *      only when it reaches an edge, so the view stays calm while reading.
 *   M4 SAMPLING. Nearest neighbour keeps pixel edges hard (text, icons);
 *      bilinear smooths, with integer weights (Q8), sampling at pixel
 *      centres, so 1x is an exact copy under either.
 *   M5 COLOUR FILTERS. Invert, grayscale, and simulations of the three
 *      colour-vision deficiencies (protan, deutan, tritan) as Q12 integer
 *      matrices applied in linear light (zui_srgb_lut.h). Every matrix row
 *      sums to exactly 1.0, so white stays white and greys stay grey.
 *   M6 BOTH WAYS. zui_mag_to_source maps a screen point (a click on the
 *      zoomed view) back to the real surface, and zui_mag_to_screen maps a
 *      real point to where it is shown, so input still lands on the right
 *      widget.
 *   M7 KEYS. Meta+Alt with '=' zooms in, '-' out, '0' back to 1x, 'm' swaps
 *      full and lens, 'i' turns inversion on and off, 'f' cycles the filters.
 *      zui_mag_event returns whether it used the event.
 *
 * Integer only, no allocation, deterministic; the source and destination
 * surfaces belong to the caller and must not share pixels.
 */
#ifndef ZXV_ZUI_MAGNIFIER_H
#define ZXV_ZUI_MAGNIFIER_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"

#define ZUI_MAG_MIN_Q8  256  /* 1x                 */
#define ZUI_MAG_MAX_Q8  4096 /* 16x                */
#define ZUI_MAG_STEP_Q8 64   /* a quarter step     */

typedef enum { ZUI_MAG_FULL = 0, ZUI_MAG_LENS } zui_mag_mode_t;
typedef enum { ZUI_MAG_NEAREST = 0, ZUI_MAG_BILINEAR } zui_mag_sample_t;

/* M3: what the view follows (a mask). */
enum { ZUI_MAG_FOLLOW_FOCUS = 1u, ZUI_MAG_FOLLOW_CARET = 2u, ZUI_MAG_FOLLOW_POINTER = 4u };

/* M5 */
typedef enum {
    ZUI_FILTER_NONE = 0,
    ZUI_FILTER_INVERT,
    ZUI_FILTER_GRAYSCALE,
    ZUI_FILTER_PROTAN, /* red-blind simulation   */
    ZUI_FILTER_DEUTAN, /* green-blind simulation */
    ZUI_FILTER_TRITAN, /* blue-blind simulation  */
    ZUI_FILTER_COUNT
} zui_filter_t;

typedef struct {
    zui_mag_mode_t mode;
    zui_mag_sample_t sample;
    zui_filter_t filter;
    uint32_t follow; /* ZUI_MAG_FOLLOW_* */
    int32_t zoom_q8; /* M2 */
    int32_t screen_w, screen_h;
    /* FULL: the viewport's top-left in source pixels, Q8 */
    int64_t vx_q8, vy_q8;
    /* LENS: its size on screen and the source point it is centred on */
    int32_t lens_w, lens_h;
    int32_t lens_x, lens_y;
    int32_t ring_px; /* the lens's gold ring, pixels (0 = none) */
} zui_magnifier_t;

/* Full screen, 2x, nearest, no filter, following focus, caret and pointer;
 * a 1/3-screen lens. */
void zui_mag_init(zui_magnifier_t *m, int32_t screen_w, int32_t screen_h);
void zui_mag_resize(zui_magnifier_t *m, int32_t screen_w, int32_t screen_h);

/* M2: set (snapped to a quarter step and clamped to 1x..16x), or move by
 * `steps` quarter steps. Both keep the view's centre. Return the new zoom. */
int32_t zui_mag_set_zoom(zui_magnifier_t *m, int32_t zoom_q8);
int32_t zui_mag_zoom_step(zui_magnifier_t *m, int32_t steps);

/* The source rectangle the full-screen view shows (whole pixels, rounded
 * outward). */
zui_rect_t zui_mag_view(const zui_magnifier_t *m);
/* Centre the view (or the lens) on a source point. */
void zui_mag_center(zui_magnifier_t *m, int32_t x, int32_t y);

/* M3: report where focus, the caret or the pointer is, in source pixels. Each
 * is ignored unless its follow bit is set. Return true when the view moved. */
bool zui_mag_follow_focus(zui_magnifier_t *m, zui_rect_t r);
bool zui_mag_follow_caret(zui_magnifier_t *m, zui_rect_t r);
bool zui_mag_follow_pointer(zui_magnifier_t *m, int32_t x, int32_t y);

/* M6: map points. to_source returns false for a point outside the screen. */
bool zui_mag_to_source(const zui_magnifier_t *m, int32_t sx, int32_t sy, int32_t *x, int32_t *y);
/* to_screen writes where a source point is shown: inside the lens when it
 * falls there, else at 1x. Returns false when it is not shown at all (off the
 * full-screen view, or covered by the lens). */
bool zui_mag_to_screen(const zui_magnifier_t *m, int32_t x, int32_t y, int32_t *sx, int32_t *sy);

/* Draw the view of src into dst (inside dst's clip). Returns false (and
 * draws nothing) when either surface is empty or they share pixels. */
bool zui_mag_render(const zui_magnifier_t *m, const zui_surface_t *src, zui_surface_t *dst);

/* M5: one pixel, or a rectangle of a surface in place (clipped). */
uint32_t zui_filter_px(zui_filter_t f, uint32_t rgb);
void zui_filter_rect(zui_surface_t *s, zui_rect_t r, zui_filter_t f);
const char *zui_filter_name(zui_filter_t f);

/* M7 */
bool zui_mag_event(zui_magnifier_t *m, const zui_event_t *e);

#endif /* ZXV_ZUI_MAGNIFIER_H */
