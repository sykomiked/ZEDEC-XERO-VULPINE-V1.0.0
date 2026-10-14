/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_fleet.h — mission checks, fleet task allocation and route
 * deconfliction for civilian robots and drones (roadmap S.4, planning
 * part; the ledger settlement of payload handoffs is not built here).
 *
 * WHAT IT DOES
 *   Mission purposes are a closed list: delivery/logistics, mapping and
 *   survey, infrastructure inspection, disaster response / search-and-
 *   rescue support, agriculture monitoring. Anything else is refused.
 *
 *   Path check (sp_path_check). A path is up to SP_MAX_WP waypoints
 *   (x, y, z in mm, time in ms since the mission epoch), moving in straight
 *   lines between them. It is REFUSED (never priced, never overridable) if:
 *     - the vehicle is not declared for the mission's purpose;
 *     - any waypoint is outside the altitude band (the band is convex in z,
 *       so straight segments between valid waypoints stay inside);
 *     - it leaves the keep-in operating area: every waypoint must be
 *       strictly inside (ON the boundary counts as outside — the cautious
 *       reading), no segment may touch the boundary and every segment must
 *       keep at least clearance_mm from it;
 *     - it enters, touches or comes closer than clearance_mm to an ACTIVE
 *       keep-out zone. A protected-habitat zone is active only inside its
 *       window [active_from_ms, active_until_ms) (absolute time, e.g. a
 *       nesting season) unless always_active; a segment is checked against
 *       it when the segment's absolute time span overlaps the window;
 *     - near an active protected-habitat zone (closer than noise_buffer_mm)
 *       the vehicle's declared noise level exceeds the zone's ceiling;
 *     - its energy estimate exceeds the vehicle's usable energy or the
 *       mission's energy budget (both in mWh = Wh * 1000).
 *   Zones are vertical columns over all altitudes (conservative).
 *   Energy estimate (all rounded UP):
 *     ceil(sum_len_mm * rate_dist / 10^6) + ceil(sum_climb_mm * rate_climb
 *     / 1000) + ceil(duration_ms * rate_time / 3600000)
 *   with segment lengths themselves rounded up to whole mm.
 *
 *   Allocation (sp_allocate). One task per vehicle. A pair (vehicle, task)
 *   is feasible if the vehicle is declared for the task's purpose, the
 *   payload fits, the round trip home -> pickup -> dropoff -> home fits the
 *   vehicle's energy and, if a mission is given, the route passes
 *   sp_path_check. Cost = w_dist * ceil(route_mm / 1000) + w_energy *
 *   energy_mwh. An exact integer Hungarian algorithm on a padded square
 *   matrix (infeasible and dummy entries cost BIG > n * max cost) gives an
 *   assignment that first MAXIMISES the number of assigned tasks and then
 *   MINIMISES total cost. Unassigned tasks are reported with a reason.
 *
 *   Deconfliction (sp_deconflict). Two vehicles lose separation when their
 *   3D distance is STRICTLY less than sep_mm at some instant at which both
 *   are between their first and last waypoint. For each pair of segments
 *   whose time spans overlap, the squared distance is a quadratic in t;
 *   its minimum over the overlap is compared with sep^2 exactly, in 512-bit
 *   integer arithmetic (no floats, no sampling). The advisory plan keeps
 *   higher-priority vehicles on schedule and delays the departure of lower
 *   priority ones in whole steps of step_ms (up to max_steps), re-running the
 *   full mission check at each delay (a delay can move a path into a
 *   nesting season). A vehicle with no conflict-free delay is UNRESOLVED and
 *   is not scheduled at all, so later vehicles need not avoid it.
 *
 *   Every plan output has advisory = 1 and requires_human_approval = 1.
 *   There is no actuation API in this module.
 *
 * HONEST LIMITS
 *   Coordination at human timescales only; real-time control and collision
 *   avoidance stay on each vehicle's own autopilot and safety layer, and a
 *   person approves every plan. Straight-line motion between waypoints is a
 *   model: wind, GNSS error and the autopilot's path-following error are not
 *   included, so sep_mm and clearance_mm must carry those buffers. The
 *   energy model is linear and ignores payload mass, wind and battery
 *   ageing. The greedy delay plan is not optimal (a different order could
 *   need smaller delays). Times are limited to [0, 2^31) ms (about 24.8
 *   days) after the mission epoch; coordinates to +-2^29 mm.
 */
#ifndef ZXV_SP_FLEET_H
#define ZXV_SP_FLEET_H

#include <stdint.h>
#include "sp_geom.h"

typedef enum {
    SP_PURPOSE_DELIVERY = 1,          /* delivery and logistics */
    SP_PURPOSE_SURVEY = 2,            /* mapping and survey */
    SP_PURPOSE_INSPECTION = 3,        /* infrastructure inspection */
    SP_PURPOSE_DISASTER_RESPONSE = 4, /* disaster response / SAR support */
    SP_PURPOSE_AGRICULTURE = 5,       /* agriculture monitoring */
} sp_purpose_t;

#define SP_PURPOSE_BIT(p) (1u << (unsigned) (p))
#define SP_PURPOSE_ALL    0x3Eu /* bits 1..5 */
int sp_purpose_valid(int p);

typedef enum {
    SP_VEH_MULTIROTOR = 1,
    SP_VEH_FIXED_WING = 2,
    SP_VEH_VTOL = 3,
    SP_VEH_GROUND = 4, /* ground robot */
} sp_vehicle_class_t;

#define SP_TIME_MAX      ((int64_t) 1 << 31) /* ms after the mission epoch */
#define SP_MAX_WP        16
#define SP_MAX_ZONES     8
#define SP_MAX_VEHICLES  16
#define SP_MAX_TASKS     16
#define SP_MAX_CONFLICTS 64
#define SP_HUNG_MAX      16
#define SP_COST_MAX      ((int64_t) 1 << 40) /* per-pair allocation cost */
#define SP_HUNG_COST_MAX ((int64_t) 1 << 46) /* entries of sp_hungarian */
#define SP_RATE_MAX      ((int64_t) 1000000000)
#define SP_SPEED_MAX     ((int64_t) 200000) /* mm/s (200 m/s) */
#define SP_ENERGY_MAX    ((int64_t) 1 << 50)
#define SP_NOISE_MAX_DB  200
#define SP_STEPS_MAX     100000u

typedef struct {
    sp_vec3 p;
    int64_t t_ms;
} sp_waypoint;

typedef struct {
    uint32_t n; /* 2..SP_MAX_WP, times strictly increasing */
    sp_waypoint wp[SP_MAX_WP];
} sp_path;

typedef enum {
    SP_ZONE_KEEP_OUT = 1,          /* e.g. airport, hospital roof, crowd */
    SP_ZONE_PROTECTED_HABITAT = 2, /* wildlife / habitat, seasonal window */
} sp_zone_kind_t;

typedef struct {
    sp_zone_kind_t kind;
    sp_poly area;
    int always_active;                       /* 1: ignore the window */
    int64_t active_from_ms, active_until_ms; /* absolute ms, [from, until) */
    int64_t noise_buffer_mm;                 /* protected zones: noise ring */
    int32_t noise_ceiling_db;                /* protected zones: max dB(A) */
} sp_zone;

typedef struct {
    sp_purpose_t purpose;
    int64_t epoch_ms; /* absolute time of t = 0, >= 0 */
    int has_keep_in;
    sp_poly keep_in; /* operating area */
    sp_alt_band band;
    int64_t clearance_mm;      /* from keep-in edge and keep-out zones */
    int64_t energy_budget_mwh; /* per path, hard limit */
    uint32_t n_zones;
    sp_zone zones[SP_MAX_ZONES];
} sp_mission;

typedef struct {
    uint32_t id;
    sp_vehicle_class_t vclass;
    uint32_t purpose_mask; /* SP_PURPOSE_BIT(...) | ... */
    sp_vec3 home;
    int64_t payload_capacity_g;
    int64_t energy_budget_mwh;    /* usable energy, mWh */
    int64_t rate_dist_mwh_per_km; /* per km of 3D travel */
    int64_t rate_climb_mwh_per_m; /* per m of ascent */
    int64_t rate_time_mwh_per_h;  /* per hour airborne/active */
    int64_t cruise_speed_mm_s;
    int32_t noise_db; /* declared dB(A) at the reference distance */
    int32_t priority; /* higher keeps its schedule in deconfliction */
} sp_vehicle;

typedef enum {
    SP_REFUSE_NONE = 0,
    SP_REFUSE_PURPOSE = 1,
    SP_REFUSE_ALTITUDE = 2,
    SP_REFUSE_KEEP_IN = 3,
    SP_REFUSE_KEEP_OUT = 4,
    SP_REFUSE_PROTECTED = 5,
    SP_REFUSE_NOISE = 6,
    SP_REFUSE_ENERGY = 7,
} sp_refuse_t;

typedef struct {
    int ok; /* 1 accepted (advisory), 0 refused */
    sp_refuse_t reason;
    uint32_t index;     /* waypoint (altitude) or segment index */
    int32_t zone;       /* zone index or -1 */
    int64_t energy_mwh; /* estimate, when computed */
} sp_check;

int sp_path_validate(const sp_path *p, int64_t delay_ms);
int sp_mission_validate(const sp_mission *m);
int sp_vehicle_validate(const sp_vehicle *v);
int sp_path_energy(const sp_vehicle *v, const sp_path *p, int64_t *mwh);
/* Checks the path shifted later by delay_ms (>= 0). */
int sp_path_check(const sp_mission *m, const sp_vehicle *v, const sp_path *p, int64_t delay_ms,
                  sp_check *out);

/* ---------- allocation ---------- */
typedef struct {
    uint32_t id;
    sp_purpose_t purpose;
    sp_vec3 pickup;  /* or survey start */
    sp_vec3 dropoff; /* or survey end */
    int64_t payload_g;
} sp_task;

typedef enum {
    SP_UA_ASSIGNED = 0,
    SP_UA_PURPOSE = 1,     /* no vehicle is declared for this purpose */
    SP_UA_PAYLOAD = 2,     /* no such vehicle can carry the payload */
    SP_UA_ENERGY = 3,      /* the round trip exceeds every such vehicle */
    SP_UA_ROUTE = 4,       /* the route is refused by the mission check */
    SP_UA_OUTNUMBERED = 5, /* feasible, but the optimum used the vehicles */
} sp_unassigned_t;

typedef struct {
    int64_t w_dist_per_m;      /* 0..2^20 */
    int64_t w_energy_per_mwh;  /* 0..2^20 */
    const sp_mission *mission; /* optional: route check */
    int64_t depart_ms;
} sp_alloc_cfg;

typedef struct {
    int advisory;                /* always 1 */
    int requires_human_approval; /* always 1 */
    uint32_t n_vehicles, n_tasks, n_assigned;
    int32_t task_vehicle[SP_MAX_TASKS]; /* vehicle index or -1 */
    sp_unassigned_t task_status[SP_MAX_TASKS];
    int64_t task_cost[SP_MAX_TASKS]; /* cost of the chosen pair, else 0 */
    int32_t vehicle_task[SP_MAX_VEHICLES];
    int64_t total_cost;
} sp_alloc_result;

/* home@depart -> pickup -> dropoff -> home, each leg ceil(len*1000/speed)
 * ms (at least 1 ms). */
int sp_route_build(const sp_vehicle *v, const sp_task *t, int64_t depart_ms, sp_path *out);
/* *status = SP_UA_ASSIGNED when feasible (then *cost is set). */
int sp_alloc_pair(const sp_vehicle *v, const sp_task *t, const sp_alloc_cfg *cfg,
                  sp_unassigned_t *status, int64_t *cost);
/* Exact min-cost perfect matching of an n x n matrix, entries in
 * [0, SP_HUNG_COST_MAX]. row_to_col[i] = column of row i. */
int sp_hungarian(uint32_t n, const int64_t cost[SP_HUNG_MAX][SP_HUNG_MAX], int32_t *row_to_col,
                 int64_t *total);
int sp_allocate(const sp_vehicle *v, uint32_t nv, const sp_task *t, uint32_t nt,
                const sp_alloc_cfg *cfg, sp_alloc_result *out);

/* ---------- deconfliction ---------- */
typedef struct {
    int64_t sep_mm;     /* 1..2^30, loss iff distance < sep */
    int64_t step_ms;    /* 1..SP_TIME_MAX */
    uint32_t max_steps; /* 0..SP_STEPS_MAX */
} sp_deconf_cfg;

typedef struct {
    uint32_t a, b;           /* vehicle indices */
    uint32_t seg_a, seg_b;   /* segment indices */
    int64_t t_from, t_until; /* overlap window of those segments (ms) */
} sp_conflict;

typedef enum {
    SP_DC_CLEARED = 1,    /* conflict-free with delay_ms (may be 0) */
    SP_DC_UNRESOLVED = 2, /* no conflict-free delay within max_steps */
    SP_DC_REFUSED = 3,    /* path refused by the mission check */
} sp_dc_status_t;

typedef struct {
    int advisory;                /* always 1 */
    int requires_human_approval; /* always 1 */
    uint32_t n;
    sp_dc_status_t status[SP_MAX_VEHICLES];
    sp_refuse_t refuse[SP_MAX_VEHICLES];
    int64_t delay_ms[SP_MAX_VEHICLES];
    uint32_t n_cleared, n_unresolved, n_refused;
    uint32_t n_conflicts_initial; /* vehicle pairs in conflict as given */
    uint32_t n_conflicts_stored;  /* <= SP_MAX_CONFLICTS */
    sp_conflict conflicts[SP_MAX_CONFLICTS];
} sp_deconf_plan;

/* Loss of separation between segment a0->a1 (shifted by da) and b0->b1
 * (shifted by db). *loss = 1 if the distance is < sep at some common
 * instant. t_from, t_until = overlap window (when it exists). */
int sp_seg_conflict(const sp_waypoint *a0, const sp_waypoint *a1, int64_t da, const sp_waypoint *b0,
                    const sp_waypoint *b1, int64_t db, int64_t sep, int *loss, int64_t *t_from,
                    int64_t *t_until);
/* First conflicting segment pair of two paths; first may be NULL. */
int sp_paths_conflict(const sp_path *a, int64_t da, const sp_path *b, int64_t db, int64_t sep,
                      int *loss, sp_conflict *first);
int sp_deconflict(const sp_mission *m, const sp_vehicle *veh, const sp_path *paths, uint32_t n,
                  const sp_deconf_cfg *cfg, sp_deconf_plan *out);

#endif /* ZXV_SP_FLEET_H */
