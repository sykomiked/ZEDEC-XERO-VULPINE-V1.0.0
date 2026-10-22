/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pivot_rules.h — Pivot's rules in C, the reference its Eve, KRON, Sutra,
 * Rust (WebAssembly) and MYRA blocks are checked against (blocks/pivot).
 * Pure integer functions, no state: the model (pivot_model.c) and the
 * language test (tests/pivot_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. A refused
 * number answers PIVOT_REFUSED (-1), which no answer is.
 *
 *   PV1 DWELL SELECT (dwell_select): a target is selected by the eyes when
 *       every gaze sample of the run lies inside it and the run lasted at
 *       least the dwell time. It is given as margins, so each field stands
 *       against its own range: edge is the least distance, in pixels, of
 *       any sample from the target's edge (pivot_sample_edge, below 0 when
 *       a sample fell outside), 0..PIVOT_EDGE_MAX; over is how long the
 *       run lasted past the dwell time, in ms, 0..PIVOT_OVER_MAX (below 0:
 *       not yet); dwell is the dwell time PIVOT_DWELL_MIN..PIVOT_DWELL_MAX.
 *       A margin past its range is no measurement: never a select.
 *   PV2 LIMITS (control_limits_ok): dwell PIVOT_DWELL_MIN..PIVOT_DWELL_MAX
 *       ms, scan step PIVOT_STEP_MIN..PIVOT_STEP_MAX ms, the smallest
 *       target side PIVOT_SIDE_MIN..PIVOT_SIDE_MAX px, switches 1..2 and
 *       scan order 0 (one item after another) or 1 (row, then column),
 *       each against its own range.
 *   PV3 A PROFILE RECORD of ^ZXPIVOT holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/pivot/profile.myra):
 *       profile 2, switches 1, dwell 4, step 5, order 1, side 5.
 *   PV4 FIRES (dwell_fires): a select fires its action when the action is
 *       plain (held 0), or when it is held (held 1: pay, grant, trash) and
 *       a second select on the confirm target followed (confirm 1). A
 *       first select of a held action never fires it. A flag that is no
 *       flag (not 0 or 1) is never read as yes.
 *   PV5 SCAN ITEM (scan_item): the item lit at a moment of a scan that
 *       steps every `step` ms over `items` items from the first:
 *       floor(elapsed / step) mod items; elapsed 0..PIVOT_ELAPSED_MAX ms,
 *       step PIVOT_STEP_MIN..PIVOT_STEP_MAX, items 1..PIVOT_ITEMS_MAX.
 *       Anything else is refused. Two switches step by hand: the item lit
 *       after k presses is scan_item(k * step, step, items). */
#ifndef PIVOT_RULES_H
#define PIVOT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "hollow_rules.h"

#define PIVOT_REFUSED     (-1)
#define PIVOT_DWELL_MIN   200
#define PIVOT_DWELL_MAX   5000
#define PIVOT_STEP_MIN    300
#define PIVOT_STEP_MAX    10000
#define PIVOT_SIDE_MIN    44              /* the smallest target a finger or an eye hits */
#define PIVOT_SIDE_MAX    HOLLOW_PX_MAX   /* the widest screen side, 16384 */
#define PIVOT_EDGE_MAX    8191            /* the deepest point of the widest target */
#define PIVOT_OVER_MAX    86400000        /* a day, in ms */
#define PIVOT_ELAPSED_MAX 999999999       /* ms: past 11 days a scan is no scan */
#define PIVOT_ITEMS_MAX   HOLLOW_TILE_MAX /* 64: an 8 by 8 grid */

/* The least distance of a sample at (x, y) from the edge of a target
 * left..left+width-1 by top..top+height-1; below 0 when it is outside. */
static inline int64_t pivot_sample_edge(int64_t x, int64_t y, int64_t left, int64_t top,
                                        int64_t width, int64_t height)
{
    int64_t m = x - left, r = left + width - 1 - x, t = y - top, b = top + height - 1 - y;
    if (r < m) m = r;
    if (t < m) m = t;
    if (b < m) m = b;
    return m;
}

/* PV1 */
static inline bool pivot_dwell_select(int64_t edge, int64_t over, int64_t dwell)
{
    return edge >= 0 && edge <= PIVOT_EDGE_MAX && over >= 0 && over <= PIVOT_OVER_MAX &&
           dwell >= PIVOT_DWELL_MIN && dwell <= PIVOT_DWELL_MAX;
}

/* PV2 */
static inline bool pivot_control_limits_ok(int64_t dwell, int64_t step, int64_t side,
                                           int64_t switches, int64_t order)
{
    return dwell >= PIVOT_DWELL_MIN && dwell <= PIVOT_DWELL_MAX && step >= PIVOT_STEP_MIN &&
           step <= PIVOT_STEP_MAX && side >= PIVOT_SIDE_MIN && side <= PIVOT_SIDE_MAX &&
           switches >= 1 && switches <= 2 && order >= 0 && order <= 1;
}

/* PV3: does x have at most `digits` digits, sign aside? */
static inline bool pivot_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool pivot_profile_record_ok(int64_t profile, int64_t switches, int64_t dwell,
                                           int64_t step, int64_t order, int64_t side)
{
    return pivot_digits_fit(profile, 2) && pivot_digits_fit(switches, 1) &&
           pivot_digits_fit(dwell, 4) && pivot_digits_fit(step, 5) && pivot_digits_fit(order, 1) &&
           pivot_digits_fit(side, 5);
}

/* PV4 */
static inline bool pivot_dwell_fires(int64_t held, int64_t confirm)
{
    return held == 0 || (held == 1 && confirm == 1);
}

/* PV5 */
static inline int64_t pivot_scan_item(int64_t elapsed, int64_t step, int64_t items)
{
    if (elapsed < 0 || elapsed > PIVOT_ELAPSED_MAX || step < PIVOT_STEP_MIN ||
        step > PIVOT_STEP_MAX || items < 1 || items > PIVOT_ITEMS_MAX)
        return PIVOT_REFUSED;
    return (elapsed / step) % items;
}

#endif
