/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ratchet_model.h — Ratchet, maintenance and repair, as a model (zxm.h): a
 * machine's parts tree, each part's readings against its safe operating
 * limits, faults diagnosed from them, wear and hours to the next service,
 * maintenance scheduled as tasks and a repair walked through one step at a
 * time, with the job's parts and labour costed. Composed of its parts:
 * Tidewatch's sampler reads sys.info, Treadle's rules time and budget the
 * tasks, Homestead's rules say whether restoring a device's power would be
 * allowed and total a device's energy, and Mockingbird's rules judge the
 * self-check after a repair and a device's record on the machine's map.
 *
 *   RM1 THE PARTS TREE. A part has a number (1..999999), the part it is
 *       fitted to (0 for the machine itself, else a part already kept), a
 *       kind and a name; it is kept only when RR7 says its node fits and,
 *       when it has a place on the machine's map, mockingbird.emu_device_ok
 *       says its record fits. Added on the owner's word.
 *   RM2 LIMITS. Each part has a safe operating range lo..hi for each
 *       measure, at most RATCHET_MARGIN_MAX wide, set on the owner's word.
 *   RM3 READINGS. ratchet_refresh reads this machine (Tidewatch's sample of
 *       sys.info, sys.disk, power.status) into the parts of each machine
 *       kind; the person types the rest (vibration, a part's temperature).
 *       Each reading with a limit goes on the sensor grid (RR6), bounded:
 *       a full grid refuses the reading (backpressure), never overwrites.
 *   RM4 DIAGNOSIS (RR8). A part is what its measures say, joined
 *       (lpres_kjoin): FALSE is a fault and raises a repair task, BOTH (its
 *       measures disagree) raises an inspection task; neither is ever
 *       dropped or passed as sound. The machine is lpres_conjoin across
 *       its parts.
 *   RM5 WEAR AND SERVICE. A part's run log (hours, load share) gives its
 *       wear (RR3) and the hours left to its next service (RR4).
 *   RM6 MAINTENANCE TASKS. A task (inspect, service, repair) runs every
 *       `period` minutes from minute `start` (treadle.next_run), its run
 *       must fit the owner's budget (treadle.budget_ok) and its record
 *       fit (treadle.task_ok). A raised task waits for the owner's word;
 *       a scheduled one is the owner's act (owner_confirmed).
 *   RM7 THE REPAIR WALK (RR2). One step at a time; every step that touches
 *       the machine begins with "Isolate the power", and no step moves on
 *       without the person's confirmation (person_confirmed): without it
 *       ZXM_E_STATE and nothing changes. The self-check after a repair is
 *       mockingbird.boot_passed; a failed one sends the walk back to
 *       isolating the power. Restoring the power is a command the person
 *       gives at the switch: Ratchet only shows whether
 *       homestead.device_cmd_ok would allow it, and never sends it.
 *   RM8 THE JOB'S COST (RR5): the sum of its lines in minor units, exact;
 *       advice for the owner, nothing is charged or posted.
 *
 * zxvd has no method for another machine's readings, for a vibration or
 * part-temperature sensor, or for posting a notice: those readings come
 * from the person, and a notice is held in `notice` for the view. No
 * drawing, no allocation, integers only. */
#ifndef RATCHET_MODEL_H
#define RATCHET_MODEL_H

#include "ratchet_rules.h"
#include "tidewatch_model.h"
#include "zxm.h"

#define RATCHET_PARTS      16u
#define RATCHET_NAME_MAX   31u
#define RATCHET_GRID_ROWS  64u
#define RATCHET_RUNS       64u
#define RATCHET_TASKS      8u
#define RATCHET_METERS     8u
#define RATCHET_NOTICE_MAX 255u
#define RATCHET_PART_MAX   INT64_C(999999)
#define RATCHET_MACHINE    INT64_C(1) /* this machine's number in the tree */

/* What a part is (RR7's kind): the machine kinds are read by ratchet_refresh */
enum {
    RATCHET_KIND_MACHINE = 1, /* the machine itself */
    RATCHET_KIND_CPU = 2,     /* load share, from sys.info */
    RATCHET_KIND_MEMORY = 3,  /* memory used, from sys.info */
    RATCHET_KIND_DISK = 4,    /* the Hoard's volume used, from sys.disk */
    RATCHET_KIND_BATTERY = 5, /* temperature, from power.status */
    RATCHET_KIND_POWER = 6,   /* draw and meter faults, from power.status */
    RATCHET_KIND_OTHER = 7,   /* a part the person reads by hand (a fan, a motor) */
};

/* Measures, each in its own integer unit */
enum {
    RATCHET_M_TEMP = 0,      /* tenths of a degree Celsius */
    RATCHET_M_VIBRATION = 1, /* hundredths of a millimetre per second */
    RATCHET_M_LOAD = 2,      /* hundredths of a percent of the machine's CPUs */
    RATCHET_M_USED = 3,      /* hundredths of a percent of a capacity */
    RATCHET_M_DRAW = 4,      /* milliwatts */
    RATCHET_M_FAULTS = 5,    /* power meter faults counted */
    RATCHET_MEASURES = 6,
};

enum { RATCHET_TASK_INSPECT = 1, RATCHET_TASK_SERVICE = 2, RATCHET_TASK_REPAIR = 3 };

/* A part's place on the machine's map (Mockingbird's device record) */
typedef struct {
    int64_t base, size, irq;
} ratchet_map_t;

typedef struct {
    int64_t part, parent, kind, hours;
    char name[RATCHET_NAME_MAX + 1];
    bool has_limit[RATCHET_MEASURES];
    int64_t lo[RATCHET_MEASURES], hi[RATCHET_MEASURES];
    bool has_reading[RATCHET_MEASURES];
    int64_t reading[RATCHET_MEASURES];
    lpres_state_t says[RATCHET_MEASURES]; /* RR8, per measure read against a limit */
    lpres_state_t state;                  /* RR8, the last diagnosis */
} ratchet_part_t;

typedef struct {
    int64_t part, hours, load;
} ratchet_run_t;

typedef struct {
    int64_t part, kind, start, period, cost;
    int64_t next; /* minutes from today's midnight (treadle.next_run) */
    bool approved;
} ratchet_task_t;

typedef struct {
    bool open;
    uint32_t part; /* its place in the tree */
    int64_t fault, isolated, fixed, tested;
    int64_t step;          /* RR2 */
    bool restore_allowed;  /* homestead.device_cmd_ok, shown only */
    uint32_t failed_stage; /* the self-check's first failed stage, or 5 */
} ratchet_walk_t;

typedef struct {
    zxm_link_t *link;
    tidewatch_t tide; /* RM3: sys.info through Tidewatch's sampler */
    ratchet_part_t p[RATCHET_PARTS];
    uint32_t np;
    int64_t grid_sensor[RATCHET_GRID_ROWS], grid_hour[RATCHET_GRID_ROWS],
        grid_fault[RATCHET_GRID_ROWS];
    uint32_t ngrid;
    ratchet_run_t run[RATCHET_RUNS];
    uint32_t nrun;
    ratchet_task_t task[RATCHET_TASKS];
    uint32_t ntask;
    uint32_t waiting; /* raised tasks that found no free slot (backpressure) */
    ratchet_walk_t walk;
    int64_t meter_domain[RATCHET_METERS], meter_uj[RATCHET_METERS];
    uint32_t nmeters;
    int64_t hour; /* hours since 1970 of the last machine reading */
    bool have;
    char notice[RATCHET_NOTICE_MAX + 1];
} ratchet_t;

void ratchet_init(ratchet_t *r, zxm_link_t *link);

/* RM1: map NULL for a part with no place on the machine's map. */
int ratchet_add_part(ratchet_t *r, int64_t part, int64_t parent, int64_t kind, const char *name,
                     const ratchet_map_t *map, bool owner_confirmed);
/* The place of part in the tree, or -1. */
int32_t ratchet_find(const ratchet_t *r, int64_t part);
/* RM2 */
int ratchet_set_limit(ratchet_t *r, int64_t part, uint32_t measure, int64_t lo, int64_t hi,
                      bool owner_confirmed);

/* RM3: read this machine into the parts of the machine kinds. */
int ratchet_refresh(ratchet_t *r);
/* RM3: a reading the person took by hand, at hour (hours since 1970). */
int ratchet_reading(ratchet_t *r, int64_t part, uint32_t measure, int64_t value, int64_t hour);

/* RM4: diagnose one part; *state its four-valued answer. */
int ratchet_diagnose(ratchet_t *r, int64_t part, lpres_state_t *state);
/* RM4: every part with a reading diagnosed; the machine's state. */
lpres_state_t ratchet_machine_state(ratchet_t *r);

/* RM5 */
int ratchet_log_run(ratchet_t *r, int64_t part, int64_t hours, int64_t load);
int ratchet_wear_of(const ratchet_t *r, int64_t part, int64_t *worn);
int ratchet_service_due(const ratchet_t *r, int64_t part, int64_t interval, int64_t load,
                        int64_t *num, int64_t *den);

/* RM6: schedule a task at now (minute of the day, 0..1439) with a run of
 * `cost` tokens leaving `left` of the owner's budget; *next its next run.
 * ratchet_approve_task gives the owner's word on a raised one. */
int ratchet_schedule(ratchet_t *r, int64_t part, int64_t kind, int64_t start, int64_t period,
                     int64_t now, int64_t cost, int64_t left, bool owner_confirmed, int64_t *next);
int ratchet_approve_task(ratchet_t *r, uint32_t idx, int64_t now, bool owner_confirmed);
/* The task for part of kind, or -1. */
int32_t ratchet_task_of(const ratchet_t *r, int64_t part, int64_t kind);

/* RM7: open the walk for a part from its last diagnosis. */
int ratchet_walk_start(ratchet_t *r, int64_t part);
/* RM7: the current step's words for the person. */
const char *ratchet_step_text(int64_t step);
/* RM7: the person confirms the current step is done; selfcheck holds the
 * five stage codes of the self-check (rom, loader, kernel, drivers, apps)
 * for step 3, NULL otherwise. */
int ratchet_walk_confirm(ratchet_t *r, bool person_confirmed, const int64_t *selfcheck);

/* RM8: n lines of a job; *total their cost in minor units. */
int ratchet_job_cost(const int64_t *qty, const int64_t *price, const int64_t *minutes,
                     const int64_t *rate, uint32_t n, int64_t *total);
/* RM3: the sensor grid's cells (RR6), one per row kept, into cell (cap). */
int ratchet_grid(const ratchet_t *r, int64_t *cell, uint32_t cap, uint32_t *n);
/* The lifetime energy of the power meters of one domain, in microjoules
 * (homestead.device_energy over the meters read by ratchet_refresh). */
int ratchet_domain_energy(const ratchet_t *r, int64_t domain, int64_t *uj);

#endif
