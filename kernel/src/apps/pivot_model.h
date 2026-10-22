/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pivot_model.h — Pivot, switch and eye control, as a model (zxm.h): a
 * control profile, a ring of gaze samples measured against a target (dwell
 * to click), and a scan stepped by time (one switch) or by hand (two
 * switches). The rules are pivot_rules.h (PV1-PV5), the same ones the app's
 * Eve, KRON, Sutra, Rust and MYRA blocks run; the camera's readiness is
 * Pinhole's (pinhole_rules.h PH1) and a row then column scan lays its items
 * out in Hollow's grid (hollow_rules.h HR1, HR2). No drawing, no
 * allocation, integers only.
 *
 *   PM1 THE PROFILE is held here: zxvd has no method that keeps it. It is
 *       set only when it fits PV2 and PV3, and only on the owner's
 *       confirmed word (ZXM_E_STATE without it, nothing changed). A new
 *       profile ends any scan, run of samples or held action waiting.
 *   PM2 GAZE: zxvd has no gaze or camera-frame method, so the view hands
 *       each sample (x, y in pixels, t in ms) with what the camera
 *       reported. Samples are taken only while the camera is ready (PH1),
 *       never going back in time, into a bounded ring: a full ring is
 *       refused (backpressure, ZXM_E_STATE) until a dwell is measured.
 *   PM3 DWELL: the run is the samples since the gaze was last outside the
 *       target (earlier ones are dropped, selected or not); the target is selected when
 *       PV1 holds over it: its least edge margin and how long it lasted
 *       past the dwell time. A target narrower than the profile's side is
 *       refused. A selected run is spent.
 *   PM4 HELD ACTIONS (pay, grant, trash): a select never fires one; it
 *       arms it (PV4 with confirm 0). Only a second select on the confirm
 *       target fires it (PV4 with confirm 1). Any other select, a cancel
 *       or a new profile disarms it. In a scan, an armed item turns the
 *       scan into two items, 0 cancel (lit first) and 1 confirm.
 *   PM5 SCAN: one switch selects what a timed scan lights (PV5 over the ms
 *       since the scan or its phase began); two switches step by hand
 *       (switch 1) and select (switch 2), the item lit after k presses
 *       being PV5 at k * step. Order 1 scans the rows of Hollow's grid,
 *       then the items of the row chosen. A scan past PIVOT_ELAPSED_MAX is
 *       refused (ZXM_E_STATE): the view starts it again.
 *   PM6 THE CLOCK is zxvd's (sys.info time_ms); a scan starts and a press
 *       is timed by it. The _at forms take the time from the caller.
 *   PM7 Every action checks its arguments and state before it changes
 *       anything: a refusal leaves the model as it was. */
#ifndef PIVOT_MODEL_H
#define PIVOT_MODEL_H

#include "pinhole_rules.h"
#include "pivot_rules.h"
#include "zxm.h"

#define PIVOT_SAMPLES 512u /* 5 s of gaze at 90 samples a second, with room */

enum { PIVOT_WAIT = 0, PIVOT_ARMED = 1, PIVOT_FIRE = 2, PIVOT_CANCEL = 3 };
enum { PIVOT_ORDER_LINEAR = 0, PIVOT_ORDER_ROWCOL = 1 };
enum { PIVOT_SWITCH_1 = 1, PIVOT_SWITCH_2 = 2 };

typedef struct {
    int32_t x, y;
    int64_t t_ms;
} pivot_sample_t;

typedef struct {
    int32_t left, top, width, height;
} pivot_rect_t;

typedef struct {
    uint8_t number;   /* 1..99, the profile's record (PV3) */
    uint8_t switches; /* 1 or 2 */
    uint8_t order;    /* PIVOT_ORDER_* */
    uint16_t dwell_ms, step_ms, side_px;
} pivot_profile_t;

typedef struct {
    zxm_link_t *link;
    pivot_profile_t prof;
    bool camera; /* PH1, as the camera last reported */
    pivot_sample_t ring[PIVOT_SAMPLES];
    uint32_t head, tail; /* head - tail samples held */
    int32_t armed;       /* the held target or item waiting for confirm; -1 none */
    /* the scan */
    bool scanning, confirming;
    uint8_t phase; /* order 1: 0 rows, 1 the items of `row` */
    uint32_t items, row, presses;
    uint64_t held_mask; /* bit i: item i is a held action */
    int64_t start_ms;
    int64_t clock_ms; /* zxvd's clock at the last read; 0 none */
    uint32_t fired;   /* actions fired */
} pivot_t;

void pivot_init(pivot_t *p, zxm_link_t *link);
/* PM1 */
int pivot_set_profile(pivot_t *p, const pivot_profile_t *pr, bool owner_confirmed);
/* PM2: the camera's report (flags as PH1 takes them), then samples. */
int pivot_camera(pivot_t *p, int64_t sensor, int64_t mode, int64_t buffer);
int pivot_gaze(pivot_t *p, int32_t x, int32_t y, int64_t t_ms);
uint32_t pivot_samples(const pivot_t *p);
/* PM3, PM4: *outcome PIVOT_WAIT, _ARMED or _FIRE; target 0.. names it. */
int pivot_dwell(pivot_t *p, int32_t target, const pivot_rect_t *r, bool held, int *outcome);
/* PM4: a dwell on the confirm target; *outcome PIVOT_WAIT or _FIRE with
 * *target the armed one. ZXM_E_STATE when nothing is armed. */
int pivot_confirm(pivot_t *p, const pivot_rect_t *r, int *outcome, int32_t *target);
void pivot_cancel(pivot_t *p);
/* PM6 */
int pivot_clock(pivot_t *p, int64_t *now_ms);
/* PM5: items 1..PIVOT_ITEMS_MAX, held_mask bit i for a held item i. */
int pivot_scan_start(pivot_t *p, uint32_t items, uint64_t held_mask);
int pivot_scan_start_at(pivot_t *p, uint32_t items, uint64_t held_mask, int64_t now_ms);
/* What is lit now: *lit is a row (order 1, phase 0), an item, or 0 cancel
 * and 1 confirm while confirming; *count is how many there are to light. */
int pivot_scan_lit(const pivot_t *p, int64_t now_ms, int32_t *lit, uint32_t *count);
/* A press of switch sw: with one switch, switch 1 selects; with two,
 * switch 1 steps and switch 2 selects. *outcome PIVOT_WAIT (stepped, or a row chosen), _ARMED,
 * _FIRE or _CANCEL, with *item the item it names (-1 none). */
int pivot_switch(pivot_t *p, uint32_t sw, int *outcome, int32_t *item);
int pivot_switch_at(pivot_t *p, uint32_t sw, int64_t now_ms, int *outcome, int32_t *item);

#endif
