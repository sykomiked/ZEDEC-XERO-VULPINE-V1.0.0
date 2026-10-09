/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_budget.h — co-processor tier 3: the open-system budget that decides how
 * hard the accelerator may be driven, and whether a result may be stored.
 * See docs/COPROCESSOR_TIERS.md.
 *
 *   B1  HEADROOM.  h_t = eta ln N - delta Q_t - C_t (Q16): the largest
 *       one-step increment the open-system ledger Q_{t+1} = (1 - delta) Q_t
 *       + eta S_t - C_t can make (surplus.h, surplus_max_increment). Q_t is
 *       stored capacity, S_t the surplus achieved this cycle (for example
 *       the mean F(u) the gate kept), C_t the cost.
 *   B2  COST FROM HARDWARE.  C_t comes from a host callback that reports
 *       any of: power (milliwatts), stall cycles out of total cycles, and
 *       thermal headroom (milli-degrees below the throttle point). Each
 *       present metric adds its weight times a normalised load:
 *         w_watts * min(P / P_ref, 4)  +  w_stall * stall / total
 *         + w_temp * max(0, 1 - headroom / headroom_ref)
 *       Absent metrics (or no callback, or a callback that fails) add
 *       nothing. The mapping is linear and its weights are configuration,
 *       not physics: they have to be calibrated per machine.
 *   B3  CONTROLLER.  A ladder of `levels` throttle levels. Level 0 runs at
 *       batch_max and ctx_max; each level halves the batch (down to
 *       batch_min) and steps the context cap linearly down to ctx_min; the
 *       last level also turns the accelerator off (tier 2 idle, the
 *       integer engine carries on). Each step predicts h_{t+1} = h_t +
 *       (h_t - h_{t-1}) (only when both were measured at the current level,
 *       so the controller's own last step is not read as a trend); if h_t
 *       or the prediction is below h_low it
 *       throttles one level (two when h_t < 0). It relaxes one level only
 *       after `hold` consecutive cycles with h_t and the prediction above
 *       h_high. h_low < h_high is the hysteresis band, so a level does not
 *       flip back and forth on noise. A band narrower than one level's cost
 *       step would still make a limit cycle, so the controller also learns
 *       each level's RELIEF: the largest drop in C_t seen within `hold`
 *       cycles after throttling past it. It relaxes past a level only when
 *       h_t minus that relief stays at or above the middle of the band.
 *   B4  STORAGE INEQUALITY.  A result may be committed to the ledger only
 *       when delta Q_t + C_t <= eta F(u): what it adds pays for the decay
 *       and the cost. ct_budget_commit checks it and calls the caller's
 *       commit function (the pay / swarm triple ledger, wired by the
 *       caller) only when it holds; this module never touches a ledger.
 * Freestanding: no libc, no malloc, no floating point, no 64-bit division.
 */
#ifndef CT_BUDGET_H
#define CT_BUDGET_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

#define CT_M_WATTS 1u
#define CT_M_STALL 2u
#define CT_M_TEMP  4u

typedef struct {
    uint32_t present;      /* CT_M_* bits */
    uint32_t watts_mw;     /* power draw */
    uint64_t stall_cycles; /* of total_cycles */
    uint64_t total_cycles;
    int32_t headroom_mc; /* milli-degrees C below the throttle point (may be negative) */
} ct_metrics_t;

/* Fill m (start from present = 0); return false if nothing could be read. */
typedef bool (*ct_metrics_fn)(void *ctx, ct_metrics_t *m);

#define CT_BUDGET_MAX_LEVELS 16u

typedef struct {
    zt_fx eta, delta; /* Q16; delta in [0, 1] */
    uint32_t N;       /* ISF block count: eta ln N is the surplus ceiling */
    zt_fx w_watts, w_stall, w_temp;
    uint32_t watts_ref_mw;
    int32_t headroom_ref_mc;
    zt_fx h_low, h_high; /* Q16, h_low < h_high */
    uint32_t hold;       /* cycles above h_high before relaxing one level */
    uint32_t levels;     /* 2..CT_BUDGET_MAX_LEVELS; the last one is accelerator off */
    uint32_t batch_max, batch_min, ctx_max, ctx_min;
    int64_t Q0; /* Q16 */
} ct_budget_cfg_t;

typedef struct {
    ct_budget_cfg_t cfg;
    int64_t Q; /* stored capacity, Q16 */
    zt_fx lnN; /* ln N, Q16 */
    zt_fx C;   /* last cost */
    zt_fx h;   /* last headroom */
    zt_fx h_prev;
    zt_fx h_min; /* lowest h seen */
    uint32_t level, above, tick;
    /* outputs for the tier-2 dispatcher */
    uint32_t batch, ctx;
    bool accel_on;
    zt_fx relief[CT_BUDGET_MAX_LEVELS]; /* cost drop seen after throttling past level l */
    zt_fx c_at_change;
    uint32_t since_change, from_level;
    /* counts */
    uint32_t changes, reversals, last_dir; /* last_dir: 0 none, 1 throttle, 2 relax */
    uint64_t commits, refusals;
} ct_budget_t;

/* Returns false (b untouched) for a bad configuration. */
bool ct_budget_init(ct_budget_t *b, const ct_budget_cfg_t *cfg);
/* C_t from one metrics sample (B2), Q16, saturating. */
zt_fx ct_cost(const ct_budget_cfg_t *cfg, const ct_metrics_t *m);
/* h for a given cost at the current Q (B1). */
zt_fx ct_budget_headroom(const ct_budget_t *b, zt_fx C);
/* One cycle: read the metrics (fn may be null), compute C_t and h_t, move
 * the throttle level and the outputs, then advance Q with this cycle's
 * surplus S (Q16). Returns h_t. */
zt_fx ct_budget_step(ct_budget_t *b, ct_metrics_fn fn, void *ctx, zt_fx S);

/* B4 */
bool ct_storage_ok(const ct_budget_t *b, zt_fx F);
typedef int32_t (*ct_commit_fn)(void *ctx, const void *rec, uint32_t len);
enum { CT_COMMIT_OK = 0, CT_COMMIT_REFUSED = 1, CT_COMMIT_EARG = -1 };
/* CT_COMMIT_REFUSED when the inequality fails (fn not called), fn's result
 * otherwise (0 = committed). */
int32_t ct_budget_commit(ct_budget_t *b, zt_fx F, ct_commit_fn fn, void *ctx, const void *rec,
                         uint32_t len);

#endif /* CT_BUDGET_H */
