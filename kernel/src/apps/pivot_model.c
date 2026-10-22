/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pivot_model.c — Pivot (see pivot_model.h). */
#include <string.h>

#include "pivot_model.h"

static const pivot_profile_t PIVOT_DEFAULT = {.number = 1,
                                              .switches = 1,
                                              .order = PIVOT_ORDER_LINEAR,
                                              .dwell_ms = 1000,
                                              .step_ms = 1000,
                                              .side_px = PIVOT_SIDE_MIN};

static void scan_stop(pivot_t *p)
{
    p->scanning = p->confirming = false;
    p->phase = 0;
    p->items = p->row = p->presses = 0;
    p->held_mask = 0;
    p->start_ms = 0;
}

void pivot_init(pivot_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    p->prof = PIVOT_DEFAULT;
    p->armed = -1;
}

int pivot_set_profile(pivot_t *p, const pivot_profile_t *pr, bool owner_confirmed)
{
    if (!pr || pr->number < 1 ||
        !pivot_control_limits_ok(pr->dwell_ms, pr->step_ms, pr->side_px, pr->switches, pr->order) ||
        !pivot_profile_record_ok(pr->number, pr->switches, pr->dwell_ms, pr->step_ms, pr->order,
                                 pr->side_px))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* PM1: set on the owner's word only */
    p->prof = *pr;
    p->tail = p->head;
    p->armed = -1;
    scan_stop(p);
    return ZXM_OK;
}

int pivot_camera(pivot_t *p, int64_t sensor, int64_t mode, int64_t buffer)
{
    bool ready = pinhole_camera_ready(sensor, mode, buffer);
    if (!ready) p->tail = p->head; /* samples from a camera that stopped are stale */
    p->camera = ready;
    return ZXM_OK;
}

uint32_t pivot_samples(const pivot_t *p)
{
    return p->head - p->tail;
}

int pivot_gaze(pivot_t *p, int32_t x, int32_t y, int64_t t_ms)
{
    if (t_ms < 0) return ZXM_E_ARG;
    if (p->head != p->tail && t_ms < p->ring[(p->head - 1u) % PIVOT_SAMPLES].t_ms) return ZXM_E_ARG;
    if (!p->camera) return ZXM_E_STATE;
    if (p->head - p->tail >= PIVOT_SAMPLES) return ZXM_E_STATE; /* backpressure */
    pivot_sample_t *s = &p->ring[p->head % PIVOT_SAMPLES];
    s->x = x;
    s->y = y;
    s->t_ms = t_ms;
    p->head++;
    return ZXM_OK;
}

static bool rect_ok(const pivot_t *p, const pivot_rect_t *r)
{
    return r && r->width >= p->prof.side_px && r->height >= p->prof.side_px &&
           r->width <= PIVOT_SIDE_MAX && r->height <= PIVOT_SIDE_MAX;
}

/* PM3: drop the samples up to the last one outside r, then measure the
 * run left against PV1. True when it selects r; the run is then spent. */
static bool run_selects(pivot_t *p, const pivot_rect_t *r)
{
    for (uint32_t i = p->head; i != p->tail; i--) {
        const pivot_sample_t *s = &p->ring[(i - 1u) % PIVOT_SAMPLES];
        if (pivot_sample_edge(s->x, s->y, r->left, r->top, r->width, r->height) < 0) {
            p->tail = i;
            break;
        }
    }
    if (p->head == p->tail) return false;
    int64_t edge = PIVOT_EDGE_MAX;
    for (uint32_t i = p->tail; i != p->head; i++) {
        const pivot_sample_t *s = &p->ring[i % PIVOT_SAMPLES];
        int64_t e = pivot_sample_edge(s->x, s->y, r->left, r->top, r->width, r->height);
        if (e < edge) edge = e;
    }
    const pivot_sample_t *first = &p->ring[p->tail % PIVOT_SAMPLES];
    const pivot_sample_t *last = &p->ring[(p->head - 1u) % PIVOT_SAMPLES];
    int64_t over = last->t_ms - first->t_ms - p->prof.dwell_ms;
    if (!pivot_dwell_select(edge, over, p->prof.dwell_ms)) return false;
    p->tail = p->head;
    return true;
}

int pivot_dwell(pivot_t *p, int32_t target, const pivot_rect_t *r, bool held, int *outcome)
{
    if (target < 0 || !rect_ok(p, r) || !outcome) return ZXM_E_ARG;
    *outcome = PIVOT_WAIT;
    if (!run_selects(p, r)) return ZXM_OK;
    p->armed = -1; /* any other select disarms (PM4) */
    if (pivot_dwell_fires(held ? 1 : 0, 0)) {
        p->fired++;
        *outcome = PIVOT_FIRE;
    } else {
        p->armed = target;
        *outcome = PIVOT_ARMED;
    }
    return ZXM_OK;
}

int pivot_confirm(pivot_t *p, const pivot_rect_t *r, int *outcome, int32_t *target)
{
    if (!rect_ok(p, r) || !outcome || !target) return ZXM_E_ARG;
    if (p->armed < 0) return ZXM_E_STATE;
    *outcome = PIVOT_WAIT;
    *target = -1;
    if (!run_selects(p, r) || !pivot_dwell_fires(1, 1)) return ZXM_OK;
    *target = p->armed;
    p->armed = -1;
    p->fired++;
    *outcome = PIVOT_FIRE;
    return ZXM_OK;
}

void pivot_cancel(pivot_t *p)
{
    p->armed = -1;
    if (p->confirming) {
        p->confirming = false;
        p->phase = 0;
        p->presses = 0;
    }
}

int pivot_clock(pivot_t *p, int64_t *now_ms)
{
    zxm_link_t *l = p->link;
    int64_t t;
    if (!now_ms) return ZXM_E_ARG;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", &t) || t <= 0) return ZXM_E_PROTOCOL;
    p->clock_ms = t;
    *now_ms = t;
    return ZXM_OK;
}

int pivot_scan_start_at(pivot_t *p, uint32_t items, uint64_t held_mask, int64_t now_ms)
{
    if (items < 1 || items > PIVOT_ITEMS_MAX || now_ms < 0) return ZXM_E_ARG;
    if (items < 64u && (held_mask >> items)) return ZXM_E_ARG; /* a held item past the last */
    scan_stop(p);
    p->armed = -1;
    p->scanning = true;
    p->items = items;
    p->held_mask = held_mask;
    p->start_ms = now_ms;
    return ZXM_OK;
}

int pivot_scan_start(pivot_t *p, uint32_t items, uint64_t held_mask)
{
    int64_t now;
    if (items < 1 || items > PIVOT_ITEMS_MAX) return ZXM_E_ARG;
    int rc = pivot_clock(p, &now);
    if (rc) return rc;
    return pivot_scan_start_at(p, items, held_mask, now);
}

/* How many the scan lights in turn now (PM4, PM5); 0 when it cannot. */
static uint32_t scan_count(const pivot_t *p)
{
    if (p->confirming) return 2u;
    if (p->prof.order != PIVOT_ORDER_ROWCOL) return p->items;
    int64_t cols = hollow_tile_cols(p->items), rows = hollow_tile_rows(p->items);
    if (cols < 1 || rows < 1) return 0;
    if (p->phase == 0) return (uint32_t) rows;
    int64_t left = (int64_t) p->items - (int64_t) p->row * cols;
    if (left < 1) return 0;
    return (uint32_t) (left < cols ? left : cols);
}

int pivot_scan_lit(const pivot_t *p, int64_t now_ms, int32_t *lit, uint32_t *count)
{
    if (!lit || !count) return ZXM_E_ARG;
    if (!p->scanning) return ZXM_E_STATE;
    if (now_ms < p->start_ms) return ZXM_E_ARG;
    uint32_t n = scan_count(p);
    if (!n) return ZXM_E_STATE;
    int64_t elapsed =
        p->prof.switches == 2 ? (int64_t) p->presses * p->prof.step_ms : now_ms - p->start_ms;
    int64_t k = pivot_scan_item(elapsed, p->prof.step_ms, n);
    if (k < 0) return ZXM_E_STATE; /* run past PIVOT_ELAPSED_MAX: start again */
    *lit = (int32_t) k;
    *count = n;
    return ZXM_OK;
}

static void phase_restart(pivot_t *p, int64_t now_ms)
{
    p->phase = 0;
    p->row = 0;
    p->presses = 0;
    p->start_ms = now_ms;
}

int pivot_switch_at(pivot_t *p, uint32_t sw, int64_t now_ms, int *outcome, int32_t *item)
{
    int32_t k;
    uint32_t n;
    if (!outcome || !item) return ZXM_E_ARG;
    if (p->prof.switches == 2 ? (sw != PIVOT_SWITCH_1 && sw != PIVOT_SWITCH_2)
                              : sw != PIVOT_SWITCH_1)
        return ZXM_E_ARG; /* one switch: its only switch selects */
    int rc = pivot_scan_lit(p, now_ms, &k, &n);
    if (rc) return rc;
    *outcome = PIVOT_WAIT;
    *item = -1;
    if (p->prof.switches == 2 && sw == PIVOT_SWITCH_1) {
        p->presses = (p->presses + 1u) % n;
        return ZXM_OK;
    }
    if (p->confirming) {
        bool fire = k == 1 && pivot_dwell_fires(1, 1);
        *item = p->armed;
        *outcome = fire ? PIVOT_FIRE : PIVOT_CANCEL;
        p->fired += fire ? 1u : 0u;
        p->armed = -1;
        p->confirming = false;
        phase_restart(p, now_ms);
        return ZXM_OK;
    }
    if (p->prof.order == PIVOT_ORDER_ROWCOL && p->phase == 0) {
        p->phase = 1;
        p->row = (uint32_t) k;
        p->presses = 0;
        p->start_ms = now_ms;
        return ZXM_OK;
    }
    int64_t at = k;
    if (p->prof.order == PIVOT_ORDER_ROWCOL) at += (int64_t) p->row * hollow_tile_cols(p->items);
    bool held = (p->held_mask >> at) & 1u;
    *item = (int32_t) at;
    if (pivot_dwell_fires(held ? 1 : 0, 0)) {
        p->fired++;
        p->armed = -1;
        *outcome = PIVOT_FIRE;
        phase_restart(p, now_ms);
    } else {
        p->armed = (int32_t) at;
        p->confirming = true; /* PM4: 0 cancel, lit first, then 1 confirm */
        p->presses = 0;
        p->start_ms = now_ms;
        *outcome = PIVOT_ARMED;
    }
    return ZXM_OK;
}

int pivot_switch(pivot_t *p, uint32_t sw, int *outcome, int32_t *item)
{
    int64_t now;
    if (!outcome || !item) return ZXM_E_ARG;
    if (!p->scanning) return ZXM_E_STATE;
    int rc = pivot_clock(p, &now);
    if (rc) return rc;
    return pivot_switch_at(p, sw, now, outcome, item);
}
