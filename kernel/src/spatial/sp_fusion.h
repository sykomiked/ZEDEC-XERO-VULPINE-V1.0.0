/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_fusion.h — four-valued sensor fusion with cautious advisories (S.2).
 *
 * WHAT IT DOES
 *   Several sources (LiDAR, radar, ultrasonic, stereo camera, GNSS axis,
 *   ...) each report value +- tolerance (mm) and a health flag. Each
 *   healthy report is the closed interval [value - tol, value + tol].
 *   The fusion rule is exact and has no voting:
 *     - fewer healthy sources than the policy minimum  -> NEITHER
 *       (missing evidence: HOLD, speed cap 0);
 *     - all healthy intervals share at least one point (max lo <= min hi,
 *       touching counts)                               -> TRUE, and the
 *       fused interval is their intersection [max lo, min hi];
 *     - otherwise the sources contradict each other    -> BOTH. No
 *       majority vote, no averaging: the cautious reading wins. For an
 *       obstacle range that is the SMALLEST lower bound of any healthy
 *       source; the margin is widened and speed is capped, or the advisory
 *       is HOLD.
 *   FALSE is never produced by the fusion rule; the value is reserved so the
 *   numbering stays identical to lpres_state_t.
 *
 *   Range advisory (sp_fuse_range), d = the distance used:
 *     NEITHER                         -> HOLD, cap 0, margin base+extra
 *     BOTH, d = min healthy lower bound:
 *       hold_on_conflict or d <= hold_dist + conflict_extra
 *                                     -> HOLD, cap 0
 *       else                          -> SLOW, cap conflict_speed
 *       margin = base + conflict_extra
 *     TRUE, d = fused lower bound:
 *       d <= hold_dist                -> HOLD, cap 0
 *       d <= slow_dist, or any source faulted or degraded
 *                                     -> SLOW, cap slow_speed
 *       else                          -> PROCEED, cap cruise_speed
 *       margin = base
 *   Degraded sources are used (their interval counts) but can never yield
 *   PROCEED. A faulted source is ignored but its absence caps at SLOW.
 *
 * HONEST LIMITS
 *   This is ADVISORY output for the platform's own safety layer and the
 *   person supervising it. It actuates nothing and is not in any control
 *   loop. Interval agreement only says the sources are CONSISTENT within
 *   the tolerances they claim; two sources that are wrong the same way
 *   (e.g. both blinded) agree and still read TRUE. Tolerances must come
 *   from the sensor's calibration, not from this code. Smoke, rain and
 *   multipath are not modelled; they show up only as disagreement.
 */
#ifndef ZXV_SP_FUSION_H
#define ZXV_SP_FUSION_H

#include <stdint.h>
#include "sp_geom.h"

/* Same numeric values as lpres_state_t in kernel/src/lpres/lpres.h
 * (redefined here so this module does not pull lpres headers in). */
typedef enum {
    SP_STATE_NEITHER = 0, /* no usable evidence */
    SP_STATE_TRUE = 1,    /* sources agree */
    SP_STATE_FALSE = 2,   /* reserved: not produced by the fusion rule */
    SP_STATE_BOTH = 3,    /* sources contradict each other */
} sp_state_t;

typedef enum {
    SP_HEALTH_FAULT = 0, /* source reports itself failed or stale */
    SP_HEALTH_OK = 1,
    SP_HEALTH_DEGRADED = 2, /* usable, but never enough for PROCEED */
} sp_health_t;

typedef enum {
    SP_ACT_HOLD = 0,
    SP_ACT_SLOW = 1,
    SP_ACT_PROCEED = 2,
} sp_action_t;

#define SP_FUSE_MAX 16 /* sources per fusion call */

typedef struct {
    int64_t value_mm; /* |value| <= SP_COORD_MAX */
    int64_t tol_mm;   /* 0 <= tol <= SP_COORD_MAX */
    sp_health_t health;
} sp_reading;

typedef struct {
    sp_state_t state;
    int64_t lo, hi;           /* fused interval, valid only when TRUE */
    int64_t cons_lo, cons_hi; /* min lower / max upper bound of all healthy
                                 sources (valid when n_healthy > 0) */
    uint32_t n_ok, n_degraded, n_fault;
} sp_fused;

typedef struct {
    int64_t cruise_speed_mm_s;   /* PROCEED cap */
    int64_t slow_speed_mm_s;     /* SLOW cap, <= cruise */
    int64_t conflict_speed_mm_s; /* SLOW cap under BOTH, <= slow */
    int64_t hold_dist_mm;        /* hold at or below this distance */
    int64_t slow_dist_mm;        /* slow at or below this, >= hold_dist */
    int64_t base_margin_mm;
    int64_t conflict_extra_mm; /* margin widening under BOTH / NEITHER */
    uint32_t min_healthy;      /* 1..SP_FUSE_MAX */
    int hold_on_conflict;      /* 1: BOTH always means HOLD */
} sp_fuse_policy;

typedef struct {
    sp_state_t state;
    sp_action_t action;
    int64_t speed_cap_mm_s;
    int64_t margin_mm;
    int64_t distance_used_mm; /* the (cautious) distance the rule used */
    sp_fused fused;
    int advisory;                /* always 1 */
    int requires_human_approval; /* always 1 */
} sp_advice;

int sp_fuse_policy_validate(const sp_fuse_policy *p);
/* Generic 1D fusion (positions or ranges). */
int sp_fuse_interval(const sp_reading *r, uint32_t n, uint32_t min_healthy, sp_fused *out);
/* Obstacle-range fusion plus the cautious advisory above. */
int sp_fuse_range(const sp_reading *r, uint32_t n, const sp_fuse_policy *p, sp_advice *out);

/* Position fusion, axis by axis with one tolerance per source. The 3D
 * state is NEITHER if any axis is NEITHER, else BOTH if any axis is BOTH,
 * else TRUE with the fused box [lo, hi]. */
typedef struct {
    sp_vec3 pos;
    int64_t tol_mm;
    sp_health_t health;
} sp_pos_reading;

typedef struct {
    sp_state_t state;
    sp_vec3 lo, hi; /* valid when TRUE */
} sp_pos_fused;

int sp_fuse_position(const sp_pos_reading *r, uint32_t n, uint32_t min_healthy, sp_pos_fused *out);

#endif /* ZXV_SP_FUSION_H */
