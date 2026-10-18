/* chg_tick.h — the Fibonacci tick: the nine seeds handshake and report on a
 * ladder, not on a timer.
 *
 * The mesh does not run on a fixed period. It runs on the Fibonacci ladder --
 * 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 -- which is the same spine the
 * lattice uses (F6 = 8 and F12 = 144 are the anchors). Each rung is a
 * handshake-and-report cycle: the Vinea sessions refresh (carrying the
 * SCP-144 lattice binding inside the session layer), the mesh coordinator
 * exchanges state, the telemetry counters go out, and CISEX and SMIC are
 * stepped. The interval between rungs grows with the ladder, so the traffic
 * is constant in the steady state and never a storm: early rungs are dense
 * (a booting seed finds its peers fast), later rungs are sparse (a settled
 * mesh costs almost nothing).
 *
 * Exact integers only; the clock is the caller's (any monotonic ms source),
 * so this module is testable without a machine.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_CHG_TICK_H
#define ZXV_CHG_TICK_H

#include <stdint.h>
#include <stdbool.h>
#include "chiglet.h"
#include "chg_mesh.h"
#include "ipfs_pin.h"

/* The ladder: F1..F12 = 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144. The rung
 * index selects the interval, so the traffic thins as the mesh settles. */
#ifndef CHG_TICK_RUNGS
#define CHG_TICK_RUNGS 12u
#endif

/* The telemetry one rung reports: the counters a peer (or an operator, or
 * the 36n9 state plane) reads to see this seed's contribution. */
typedef struct {
    uint32_t rung;        /* which ladder position this report is for (1..12) */
    uint32_t interval_ms; /* the rung's interval, the ladder value * the base */
    uint32_t peers;       /* live peer sessions this rung */
    uint32_t pinned;      /* confirmed pins (from ipfs_pin_progress_t) */
    uint32_t unconfirmed; /* pins that could not be confirmed on-node */
    uint32_t served;      /* serve requests this seed has sent (all rungs) */
    uint32_t refused;     /* transport ops that returned false */
    uint32_t cisex;       /* the CISEX index size this rung */
    uint32_t smic;        /* the SMIC state-plane counter this rung */
} chg_tick_report_t;

/* The telemetry sink: the caller binds it to the door's /telemetry, to the
 * Vinea state exchange, or to both. One call per rung. */
typedef struct {
    void *ctx;
    bool (*report)(void *ctx, const chg_tick_report_t *r);
    /* step the content index and the state plane for this rung; both may be
     * NULL, and the report then carries the last known values */
    uint32_t (*cisex_step)(void *ctx);
    uint32_t (*smic_step)(void *ctx);
} chg_tick_sink_t;

typedef struct {
    chg_tick_sink_t sink;
    uint32_t        rung;        /* the current ladder position (0 = not started) */
    uint64_t        due_ms;      /* the next rung's due time */
    uint32_t        base_ms;     /* the ladder's unit: interval = F * base_ms */
    uint32_t        rungs_run;   /* how many rungs have fired (all time) */
    uint32_t        reports;     /* how many reports the sink accepted */
    uint32_t        cisex_last;  /* the last values, carried in the report */
    uint32_t        smic_last;
} chg_tick_t;

/* Bind the sink. `base_ms` is the ladder's unit: rung 1 is 1 * base_ms,
 * rung 12 is 144 * base_ms. 0 selects 1000 (one second per unit, so the
 * ladder spans 1s .. 144s). */
void chg_tick_init(chg_tick_t *t, const chg_tick_sink_t *sink, uint32_t base_ms);

/* The ladder value for a rung (1..12): F1=1, F2=1, F3=2 ... F12=144. Returns
 * 0 for a rung outside the ladder. */
uint32_t chg_tick_fib(uint32_t rung);

/* Advance to the next rung when `now_ms` reaches it. Returns true when a
 * rung fired (the caller then runs the mesh cycle for that rung). The ladder
 * CLIMBS: each fired rung advances the position, and after the last rung the
 * ladder holds at F12 (the anchor) rather than wrapping to a storm. */
bool chg_tick_due(chg_tick_t *t, uint64_t now_ms);

/* Build and send the report for the rung that just fired. `p` and `m` are
 * this seed's own state; both may be NULL and the report then carries zeros
 * for their fields. Returns true when the sink accepted it. */
bool chg_tick_report(chg_tick_t *t, const ipfs_pin_progress_t *p, const chg_mesh_t *m);

#endif /* ZXV_CHG_TICK_H */
