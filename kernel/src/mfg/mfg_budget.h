/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_budget.h — F.10 nature boundaries as hard budgets, plus a minimal
 * F.6 machine-time admission queue.
 *
 * WHAT IT DOES
 *   Each site has, per epoch (a shift, a day, a month: the caller decides),
 *   a hard limit for five resources: energy (Wh), water (mL), material (g),
 *   waste (g) and emissions (g CO2e). Consumption comes ONLY from meter
 *   readings the caller supplies (cumulative use so far this epoch). Per
 *   resource the meter state is four-valued:
 *     NEITHER  no reading yet / meter offline  -> jobs needing it WAIT
 *     TRUE     a reading is held
 *     BOTH     contradictory readings (the cumulative value went
 *              backwards)                       -> jobs needing it WAIT
 *                                                  until a person clears it
 *   A job declares its needs. It is admitted only if, for every resource it
 *   needs (need > 0), the meter is TRUE and
 *       measured_use + reserved + need <= limit
 *   exactly ("exactly at the limit" is allowed; one unit over waits).
 *   Admission reserves the need. The budget check takes NO price and NO
 *   bid: the function signature has nowhere to put one, so no bid can buy
 *   past a boundary. A breach makes the job wait; it is never priced.
 *
 *   Minimal F.6 queue: jobs carry an owner, an integer bid and machine time
 *   (minutes). mfg_queue_run admits, for one epoch, the highest-bid job
 *   (ties: earlier submission) that passes the budget and fits the
 *   remaining machine time, subject to a cooperative no-monopoly rule: an
 *   owner may not hold more than share_num/share_den (default 1/3) of the
 *   epoch's machine-time capacity while another job that is within its own
 *   owner's share is admissible. The share is measured against the epoch's
 *   CAPACITY, not the running admitted total (a share of the running total
 *   would block every owner's first job). When every admissible job is over
 *   its owner's share, the highest bid among them is admitted so the
 *   machine does not sit idle. All arithmetic is exact integer.
 *
 * HONEST LIMITS
 *   - Limits are set by the operator per site (from published ecological
 *     thresholds and local permits); this code does not derive them.
 *   - Only measured numbers are reported as use. Reservations are declared
 *     needs, kept separately; after a finished job's consumption shows in
 *     the meter reading, call mfg_site_release, or it is counted twice
 *     (conservative, never optimistic).
 *   - Meter readings are trusted as given (signing and logging of readings
 *     belongs to F.1). No market, payment or settlement happens here; F.6 is
 *     only the admission order. Not a scheduler for machine motion.
 *
 * Freestanding, integer-only, bounded, no allocation, no libc.
 */
#ifndef ZXV_MFG_BUDGET_H
#define ZXV_MFG_BUDGET_H

#include "mfg_common.h"

typedef enum {
    MFG_RES_ENERGY_WH = 0,
    MFG_RES_WATER_ML = 1,
    MFG_RES_MATERIAL_G = 2,
    MFG_RES_WASTE_G = 3,
    MFG_RES_EMISSIONS_G = 4,
    MFG_RES_N = 5
} mfg_resource_t;

#define MFG_BUDGET_MAX  1000000000000000LL /* 1e15 per resource */
#define MFG_Q_MAX_JOBS  64u
#define MFG_Q_TIME_MAX  1000000000LL /* minutes */
#define MFG_Q_BID_MAX   1000000000000000LL
#define MFG_Q_SHARE_MAX 1000000LL /* bound on share numerator/denominator */
#define MFG_SHARE_NUM   1
#define MFG_SHARE_DEN   3

typedef enum { MFG_ADMIT = 0, MFG_WAIT = 1 } mfg_admit_t;

typedef struct {
    uint64_t epoch;
    int64_t limit[MFG_RES_N];
    int64_t used[MFG_RES_N];     /* last measured cumulative use this epoch */
    int64_t reserved[MFG_RES_N]; /* declared needs of admitted jobs */
    uint8_t meter[MFG_RES_N];    /* mfg_tv_t */
    uint8_t init;
} mfg_site_t;

/* limits[r] in [0, MFG_BUDGET_MAX]. All meters start NEITHER. */
mfg_status_t mfg_site_init(mfg_site_t *s, uint64_t epoch, const int64_t limits[MFG_RES_N]);
/* Start a later epoch (epoch must increase): use, reservations, meters reset. */
mfg_status_t mfg_site_new_epoch(mfg_site_t *s, uint64_t epoch);
/* A meter reading of cumulative use this epoch. A reading lower than the
 * previous one makes the meter BOTH (returned as MFG_OK; the state shows). */
mfg_status_t mfg_site_meter(mfg_site_t *s, uint32_t r, int64_t reading);
/* Meter offline or reset by a person: state NEITHER, baseline cleared. */
mfg_status_t mfg_site_meter_clear(mfg_site_t *s, uint32_t r);
/* Check needs (each in [0, MFG_BUDGET_MAX]). per_res may be NULL; else it
 * gets MFG_RES_N states: TRUE fits or not needed, FALSE would exceed,
 * NEITHER no meter, BOTH contradictory meter. */
mfg_status_t mfg_site_check(const mfg_site_t *s, const int64_t need[MFG_RES_N],
                            mfg_tv_t per_res[MFG_RES_N], mfg_admit_t *decision);
/* Check and, if admitted, reserve the needs. */
mfg_status_t mfg_site_reserve(mfg_site_t *s, const int64_t need[MFG_RES_N],
                              mfg_tv_t per_res[MFG_RES_N], mfg_admit_t *decision);
/* Drop a reservation (its use is now in the meter reading, or the job was
 * cancelled). MFG_ERR_RANGE if more than reserved. */
mfg_status_t mfg_site_release(mfg_site_t *s, const int64_t need[MFG_RES_N]);

typedef enum { MFG_JOB_PENDING = 0, MFG_JOB_ADMITTED = 1 } mfg_job_state_t;
typedef enum { MFG_WAIT_NONE = 0, MFG_WAIT_BUDGET = 1, MFG_WAIT_CAPACITY = 2 } mfg_wait_t;

typedef struct {
    uint32_t id;
    uint32_t owner;
    int64_t bid;     /* integer bid for the machine time, >= 0 */
    int64_t minutes; /* machine time, >= 1 */
    int64_t need[MFG_RES_N];
    uint32_t seq;                 /* submission order */
    uint8_t state;                /* mfg_job_state_t */
    uint8_t wait;                 /* mfg_wait_t after the last run */
    uint8_t res_state[MFG_RES_N]; /* mfg_tv_t from the last budget check */
} mfg_job_t;

typedef struct {
    uint32_t njob;
    uint32_t next_seq;
    mfg_job_t job[MFG_Q_MAX_JOBS];
} mfg_queue_t;

mfg_status_t mfg_queue_init(mfg_queue_t *q);
mfg_status_t mfg_queue_add(mfg_queue_t *q, uint32_t id, uint32_t owner, int64_t bid,
                           int64_t minutes, const int64_t need[MFG_RES_N]);
/* Remove a job (finished or withdrawn). Its reservation is NOT released
 * here: call mfg_site_release when the meter shows its use. */
mfg_status_t mfg_queue_remove(mfg_queue_t *q, uint32_t id);
/* Admit pending jobs for one epoch of capacity_min machine minutes. order
 * receives admitted job ids in admission order (the first max_order of
 * them; *nadmitted counts all, so it may exceed max_order); jobs
 * already ADMITTED are ignored. share_num/share_den in (0, 1]. */
mfg_status_t mfg_queue_run(mfg_queue_t *q, mfg_site_t *s, int64_t capacity_min, int64_t share_num,
                           int64_t share_den, uint32_t *order, uint32_t max_order,
                           uint32_t *nadmitted);

#endif /* ZXV_MFG_BUDGET_H */
