/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_progress.h — the ZXV loading bar and the launch screen built on it.
 *
 * The owner made this the one progress style for the whole ecosystem (boot,
 * app launch, any wait): a symmetric bar, never one-sided.
 *
 *   GATHER   both ends fill toward the centre (things coming together)
 *   RELEASE  the centre fills out to both edges (the thing now running)
 *
 * A 3 px hairline track, gold fill, a bright head dot at each moving end, no
 * percentage inside the bar. Items placed along a gathering bar light from dim
 * to full gold as a head passes their centre; the centre item lights when the
 * halves meet. Unknown progress never fakes a number: the heads breathe slowly
 * at the centre instead.
 *
 * The launch screen is the boot sequence made general: the app's mark sits dim
 * at the top, its parts (models, agents, documents it is opening) sit in a row
 * on carved tiles, the bottom bar gathers and lights them, the mark lights and
 * warms, and the bar under the mark releases with the rest of the load. The
 * boot splash is this screen with the ZXV fox as the mark and the houses as
 * the parts.
 *
 * Integer only, deterministic: same inputs -> same pixels.
 */
#ifndef ZXV_ZUI_PROGRESS_H
#define ZXV_ZUI_PROGRESS_H

#include <stdint.h>
#include "zui_canvas.h"

typedef enum { ZUI_BAR_GATHER = 0, ZUI_BAR_RELEASE } zui_bar_mode_t;

#define ZUI_BAR_UNKNOWN 0xFFFFFFFFu   /* progress not known: breathe, no number */

/* Draw the bar across `track` (its height is the bar's thickness). p is 0..256
 * or ZUI_BAR_UNKNOWN; tick animates the unknown state. The head x positions are
 * written to *head_l / *head_r when not NULL (equal to the centre when full). */
void zui_bar_draw(zui_surface_t *s, zui_rect_t track, zui_bar_mode_t mode,
                  uint32_t p, uint32_t tick, int32_t *head_l, int32_t *head_r);

/* How lit (0..256) an item centred at item_cx is on a gathering bar whose heads
 * are at head_l / head_r, with the bar centre at mid. Items left of centre light
 * as the left head passes them, right of centre as the right head does, and the
 * centre item when the heads meet. ramp_px softens the switch-on. */
uint32_t zui_bar_lit(int32_t item_cx, int32_t mid, int32_t head_l, int32_t head_r,
                     int32_t ramp_px);

/* ---- the launch screen ------------------------------------------------- */
#define ZUI_LAUNCH_MAX_PARTS 7

/* Draws one part (its own art) at a lit level 0..256 inside box. */
typedef void (*zui_part_fn)(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx);

typedef struct {
    zui_part_fn mark;                         /* the app's mark (top)          */
    zui_part_fn parts[ZUI_LAUNCH_MAX_PARTS];  /* the row; the middle is centre */
    void       *ctx[ZUI_LAUNCH_MAX_PARTS + 1];/* ctx[0] mark, ctx[1+i] part i  */
    uint32_t    n;                            /* odd is best: a true centre    */
} zui_launch_t;

/* Phases of one launch, each 0..256:
 *   gather   the bottom bar runs in, lighting the parts
 *   light    the mark lights and warms (a brief flash at the middle)
 *   release  the bar under the mark runs out with the rest of the load, or
 *            ZUI_BAR_UNKNOWN while the remaining time is unknown */
typedef struct { uint32_t gather, light, release; } zui_launch_state_t;

/* Lay out and draw the launch screen into `area` (fills it with the ground). */
void zui_launch_draw(zui_surface_t *s, zui_rect_t area, int32_t scale_q8,
                     const zui_launch_t *l, zui_launch_state_t st, uint32_t tick);

#endif /* ZXV_ZUI_PROGRESS_H */
