/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_progress.h — the ZXV loading bar and the launch screen built on it.
 *
 * The owner's loading screen for the ecosystem: the OS boot, opening an app,
 * and connecting to a remote instance or space (the Park, a Cloud top, a
 * hall). It does not replace an ordinary progress bar for byte-counted work
 * such as downloads, copies or installs; those keep a plain one-sided bar.
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
 * Two owner rules (2026-10-12):
 *   - Progress is real. Every phase value comes from actual loading work
 *     (modules verified, models mapped, agents started); nothing is timed for
 *     show. When the remaining work is unknown, pass ZUI_BAR_UNKNOWN.
 *   - Sound (chime, coins, gong) belongs to the OS boot only. App launches
 *     reuse this screen silently; this module never makes sound.
 *
 * When the load is done the screen opens like a vault door (`open`): bolts
 * that locked across the centre seam while the top bar filled throw back, and
 * the whole screen splits down the middle, each half sliding to its edge to
 * reveal what was loaded. The caller draws the loaded view first and passes
 * it as `behind`.
 *
 * Integer only, deterministic: same inputs -> same pixels.
 */
#ifndef ZXV_ZUI_PROGRESS_H
#define ZXV_ZUI_PROGRESS_H

#include <stdint.h>
#include "zui_canvas.h"
#include "zui_depth.h"

typedef enum { ZUI_BAR_GATHER = 0, ZUI_BAR_RELEASE } zui_bar_mode_t;

#define ZUI_BAR_UNKNOWN 0xFFFFFFFFu /* progress not known: breathe, no number */

/* Draw the bar across `track` (its height is the bar's thickness). p is 0..256
 * or ZUI_BAR_UNKNOWN; tick animates the unknown state. The head x positions are
 * written to *head_l / *head_r when not NULL (equal to the centre when full). */
void zui_bar_draw(zui_surface_t *s, zui_rect_t track, zui_bar_mode_t mode, uint32_t p,
                  uint32_t tick, int32_t *head_l, int32_t *head_r);

/* How lit (0..256) an item centred at item_cx is on a gathering bar whose heads
 * are at head_l / head_r, with the bar centre at mid. Items left of centre light
 * as the left head passes them, right of centre as the right head does, and the
 * centre item when the heads meet. ramp_px softens the switch-on. */
uint32_t zui_bar_lit(int32_t item_cx, int32_t mid, int32_t head_l, int32_t head_r, int32_t ramp_px);

/* ---- the launch screen ------------------------------------------------- */
#define ZUI_LAUNCH_MAX_PARTS 7

/* Draws one part (its own art) at a lit level 0..256 inside box. */
typedef void (*zui_part_fn)(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx);

typedef struct {
    zui_part_fn mark;                        /* the app's mark (top)          */
    zui_part_fn parts[ZUI_LAUNCH_MAX_PARTS]; /* the row; the middle is centre */
    void *ctx[ZUI_LAUNCH_MAX_PARTS + 1];     /* ctx[0] mark, ctx[1+i] part i  */
    uint32_t n;                              /* odd is best: a true centre    */
    const zui_depth_t *depth;                /* FOIL: tiles and mark shimmer; may be NULL */
} zui_launch_t;

/* Phases of one launch, each 0..256:
 *   gather   the bottom bar runs in, lighting the parts
 *   light    the mark lights and warms (a brief flash at the middle)
 *   release  the bar under the mark runs out with the rest of the load, or
 *            ZUI_BAR_UNKNOWN while the remaining time is unknown */
typedef struct {
    uint32_t gather, light, release, open;
} zui_launch_state_t;
/*   open     0..256 once release is full: the bolts throw back (first quarter),
 *            then the two halves slide apart to the edges */

#define ZUI_VAULT_BOLTS 5

/* Lay out and draw the launch screen into `area` (fills it with the ground). */
void zui_launch_draw(zui_surface_t *s, zui_rect_t area, int32_t scale_q8, const zui_launch_t *l,
                     zui_launch_state_t st, uint32_t tick);

/* The vault opening. Draw the finished face with zui_launch_draw (it draws the
 * bolts: bolt i locks once release passes (i+1)*256/6, and they throw back
 * over the first quarter of `open`), draw the loaded view into `behind`, then
 * call this: it copies `behind` into area and lays the face over it split at
 * the centre, each half moved out toward its edge by a smootherstep of the
 * rest of `open`. face and behind must be at least area's size; their pixel
 * (0,0) maps to area's top-left. */
void zui_vault_draw(zui_surface_t *s, zui_rect_t area, int32_t scale_q8, const zui_surface_t *face,
                    const zui_surface_t *behind, uint32_t open);

#endif /* ZXV_ZUI_PROGRESS_H */
