/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_fusion.c — four-valued sensor fusion (roadmap S.2). See sp_fusion.h
 * for the exact rules.
 *
 * HONEST LIMITS: advisory only; consistency of claimed tolerances is not
 * truth. Freestanding, integer-only, no allocation.
 */
#include "sp_fusion.h"

static int sp_fu_in_range(int64_t v)
{
    return v >= -SP_COORD_MAX && v <= SP_COORD_MAX;
}

int sp_fuse_policy_validate(const sp_fuse_policy *p)
{
    if (!p) return SP_ERR_NULL;
    if (p->min_healthy < 1 || p->min_healthy > SP_FUSE_MAX) return SP_ERR_RANGE;
    if (p->cruise_speed_mm_s < 0 || p->slow_speed_mm_s < 0 || p->conflict_speed_mm_s < 0)
        return SP_ERR_RANGE;
    if (p->hold_dist_mm < 0 || p->hold_dist_mm > SP_COORD_MAX || p->slow_dist_mm < 0 ||
        p->slow_dist_mm > SP_COORD_MAX || p->base_margin_mm < 0 ||
        p->base_margin_mm > SP_COORD_MAX || p->conflict_extra_mm < 0 ||
        p->conflict_extra_mm > SP_COORD_MAX)
        return SP_ERR_RANGE;
    if (p->slow_speed_mm_s > p->cruise_speed_mm_s || p->conflict_speed_mm_s > p->slow_speed_mm_s ||
        p->hold_dist_mm > p->slow_dist_mm)
        return SP_ERR_PARAM;
    if (p->hold_on_conflict != 0 && p->hold_on_conflict != 1) return SP_ERR_PARAM;
    return SP_OK;
}

int sp_fuse_interval(const sp_reading *r, uint32_t n, uint32_t min_healthy, sp_fused *out)
{
    uint32_t i, healthy;
    int64_t max_lo = 0, min_hi = 0, min_lo = 0, max_hi = 0;
    if (!out || (!r && n > 0)) return SP_ERR_NULL;
    if (n > SP_FUSE_MAX) return SP_ERR_CAPACITY;
    if (min_healthy < 1 || min_healthy > SP_FUSE_MAX) return SP_ERR_RANGE;
    for (i = 0; i < n; i++) {
        if (!sp_fu_in_range(r[i].value_mm) || r[i].tol_mm < 0 || r[i].tol_mm > SP_COORD_MAX)
            return SP_ERR_RANGE;
        if (r[i].health != SP_HEALTH_FAULT && r[i].health != SP_HEALTH_OK &&
            r[i].health != SP_HEALTH_DEGRADED)
            return SP_ERR_RANGE;
    }
    out->n_ok = out->n_degraded = out->n_fault = 0;
    out->lo = out->hi = out->cons_lo = out->cons_hi = 0;
    for (i = 0; i < n; i++) {
        /* |value| + tol <= 2^30: no overflow possible */
        int64_t lo = r[i].value_mm - r[i].tol_mm, hi = r[i].value_mm + r[i].tol_mm;
        if (r[i].health == SP_HEALTH_FAULT) {
            out->n_fault++;
            continue;
        }
        healthy = out->n_ok + out->n_degraded;
        if (healthy == 0) {
            max_lo = min_lo = lo;
            min_hi = max_hi = hi;
        } else {
            if (lo > max_lo) max_lo = lo;
            if (lo < min_lo) min_lo = lo;
            if (hi < min_hi) min_hi = hi;
            if (hi > max_hi) max_hi = hi;
        }
        if (r[i].health == SP_HEALTH_OK)
            out->n_ok++;
        else
            out->n_degraded++;
    }
    healthy = out->n_ok + out->n_degraded;
    if (healthy > 0) {
        out->cons_lo = min_lo;
        out->cons_hi = max_hi;
    }
    if (healthy < min_healthy) {
        out->state = SP_STATE_NEITHER;
        return SP_OK;
    }
    if (max_lo <= min_hi) {
        out->state = SP_STATE_TRUE;
        out->lo = max_lo;
        out->hi = min_hi;
    } else {
        out->state = SP_STATE_BOTH;
    }
    return SP_OK;
}

int sp_fuse_range(const sp_reading *r, uint32_t n, const sp_fuse_policy *p, sp_advice *out)
{
    int st;
    if (!p || !out) return SP_ERR_NULL;
    if ((st = sp_fuse_policy_validate(p)) != SP_OK) return st;
    if ((st = sp_fuse_interval(r, n, p->min_healthy, &out->fused)) != SP_OK) return st;
    out->advisory = 1;
    out->requires_human_approval = 1;
    out->state = out->fused.state;
    switch (out->fused.state) {
    case SP_STATE_TRUE:
        out->distance_used_mm = out->fused.lo;
        out->margin_mm = p->base_margin_mm;
        if (out->fused.lo <= p->hold_dist_mm) {
            out->action = SP_ACT_HOLD;
            out->speed_cap_mm_s = 0;
        } else if (out->fused.lo <= p->slow_dist_mm || out->fused.n_fault > 0 ||
                   out->fused.n_degraded > 0) {
            out->action = SP_ACT_SLOW;
            out->speed_cap_mm_s = p->slow_speed_mm_s;
        } else {
            out->action = SP_ACT_PROCEED;
            out->speed_cap_mm_s = p->cruise_speed_mm_s;
        }
        break;
    case SP_STATE_BOTH:
        out->distance_used_mm = out->fused.cons_lo;
        out->margin_mm = p->base_margin_mm + p->conflict_extra_mm; /* <= 2^30 */
        if (p->hold_on_conflict || out->fused.cons_lo <= p->hold_dist_mm + p->conflict_extra_mm) {
            out->action = SP_ACT_HOLD;
            out->speed_cap_mm_s = 0;
        } else {
            out->action = SP_ACT_SLOW;
            out->speed_cap_mm_s = p->conflict_speed_mm_s;
        }
        break;
    default: /* NEITHER: missing evidence */
        out->distance_used_mm = 0;
        out->margin_mm = p->base_margin_mm + p->conflict_extra_mm;
        out->action = SP_ACT_HOLD;
        out->speed_cap_mm_s = 0;
        break;
    }
    return SP_OK;
}

int sp_fuse_position(const sp_pos_reading *r, uint32_t n, uint32_t min_healthy, sp_pos_fused *out)
{
    sp_reading ax[SP_FUSE_MAX];
    sp_fused f[3];
    uint32_t i;
    int k, st, any_neither = 0, any_both = 0;
    if (!out || (!r && n > 0)) return SP_ERR_NULL;
    if (n > SP_FUSE_MAX) return SP_ERR_CAPACITY;
    for (k = 0; k < 3; k++) {
        for (i = 0; i < n; i++) {
            ax[i].value_mm = k == 0 ? r[i].pos.x : (k == 1 ? r[i].pos.y : r[i].pos.z);
            ax[i].tol_mm = r[i].tol_mm;
            ax[i].health = r[i].health;
        }
        if ((st = sp_fuse_interval(ax, n, min_healthy, &f[k])) != SP_OK) return st;
        if (f[k].state == SP_STATE_NEITHER) any_neither = 1;
        if (f[k].state == SP_STATE_BOTH) any_both = 1;
    }
    out->lo.x = out->lo.y = out->lo.z = 0;
    out->hi.x = out->hi.y = out->hi.z = 0;
    if (any_neither) {
        out->state = SP_STATE_NEITHER;
    } else if (any_both) {
        out->state = SP_STATE_BOTH;
    } else {
        out->state = SP_STATE_TRUE;
        out->lo.x = f[0].lo;
        out->hi.x = f[0].hi;
        out->lo.y = f[1].lo;
        out->hi.y = f[1].hi;
        out->lo.z = f[2].lo;
        out->hi.z = f[2].hi;
    }
    return SP_OK;
}
