/* event_sched.h — Event-Driven Scheduler for ZXV
 *
 * A native ZXV scheduler that replaces the round-robin sched.c with
 * an event-budget model: each task receives a finite per-dispatch
 * event budget. When the budget is exhausted (or the task has no
 * pending events), the task yields. When no task has pending events,
 * the scheduler enters tickless WFI idle instead of spinning.
 *
 * Cost model: each event is charged a PMU-cycle cost proxy (surplus_real_t),
 * not wall-clock time. This decouples scheduling fairness from clock
 * speed and ties it to actual work done, measured in M5-coordinate
 * space (omega = event count, chi = cycle cost, phi = phase budget).
 *
 * Architecture: this is NOT a POSIX/Linux scheduler. There are no
 * signals, no POSIX timers, no clock_gettime. Tasks are ZXV-native
 * event handlers identified by 168-bit node IDs, dispatched by the
 * M5 phase coordinator.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef EVENT_SCHED_H
#define EVENT_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define EV_MAX_TASKS        64
#define EV_MAX_NAME_LEN     32
#define EV_DEFAULT_BUDGET   100    /* default events per dispatch */
#define EV_MAX_BUDGET       1000   /* hard cap on per-task event budget */
#define EV_IDLE_THRESHOLD   0     /* if no pending events, enter WFI */

/* ===== Task States ===== */

typedef enum {
    EV_TASK_UNUSED    = 0,
    EV_TASK_READY     = 1,  /* has pending events, eligible for dispatch */
    EV_TASK_RUNNING   = 2,  /* currently being dispatched */
    EV_TASK_BLOCKED   = 3,  /* waiting on an external event (I/O, timer) */
    EV_TASK_IDLE      = 4,  /* budget exhausted, waiting for replenishment */
    EV_TASK_TERMINATED = 5
} ev_task_state_t;

/* ===== Event-Driven Task ===== */

typedef struct ev_task {
    uint32_t id;
    char name[EV_MAX_NAME_LEN];
    ev_task_state_t state;

    /* Event budget model */
    uint32_t event_budget;          /* max events per dispatch cycle */
    uint32_t events_remaining;      /* events left in current dispatch */
    uint64_t total_events_dispatched; /* lifetime counter */

    /* PMU-cycle cost proxy */
    surplus_real_t cycle_cost;      /* accumulated cost this dispatch */
    surplus_real_t total_cycle_cost; /* lifetime cost */

    /* M5 coordinates */
    m5_coords_t m5;                 /* omega=events, chi=cost, phi=budget */
    surplus_real_t coverage_ratio;

    /* Priority (0-1000, same scale as Count House trust weight) */
    uint32_t priority;

    /* Pending event count (set by event source, consumed by scheduler) */
    uint32_t pending_events;
} ev_task_t;

/* ===== Event-Driven Scheduler ===== */

typedef struct ev_scheduler {
    ev_task_t tasks[EV_MAX_TASKS];
    uint32_t num_tasks;
    uint32_t current_task;
    uint32_t next_id;

    /* Global stats */
    uint64_t total_dispatches;      /* total task dispatches */
    uint64_t total_idle_cycles;     /* times WFI was entered */
    uint64_t total_events_handled;  /* total events processed across all tasks */

    /* Tickless idle support */
    bool idle;                      /* true when no tasks have pending events */
    surplus_real_t idle_ratio;      /* idle_cycles / total_cycles */

    /* M5 coordinates for the scheduler itself */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} ev_scheduler_t;

/* ===== API ===== */

void ev_sched_init(ev_scheduler_t *evs);

/* Create a task with a given event budget and priority.
 * Returns task ID on success, -1 if capacity exceeded. */
int32_t ev_sched_create_task(ev_scheduler_t *evs, const char *name,
                               uint32_t event_budget, uint32_t priority);

/* Post events to a task (marks it READY if it was IDLE/BLOCKED).
 * Returns 0 on success, -1 if task not found. */
int32_t ev_sched_post_events(ev_scheduler_t *evs, uint32_t task_id,
                               uint32_t event_count);

/* Charge cycle cost to the current task during dispatch.
 * Called by event handlers to account for PMU cycles consumed. */
void ev_sched_charge_cycles(ev_scheduler_t *evs, surplus_real_t cost);

/* Dispatch one round: pick the highest-priority task with pending
 * events, dispatch up to its event_budget events, then yield.
 * Returns the task ID that was dispatched, or -1 if no task was
 * ready (caller should enter WFI idle). */
int32_t ev_sched_dispatch(ev_scheduler_t *evs);

/* Check if any task has pending events. If not, the scheduler
 * enters tickless idle. Returns true if idle (no pending work). */
bool ev_sched_check_idle(ev_scheduler_t *evs);

/* Replenish event budgets for all IDLE tasks (called at the start
 * of each new dispatch epoch / phase coordinator tick). */
void ev_sched_replenish(ev_scheduler_t *evs);

/* Block a task (waiting for external event). */
void ev_sched_block(ev_scheduler_t *evs, uint32_t task_id);

/* Unblock a task and post events to it. */
void ev_sched_unblock(ev_scheduler_t *evs, uint32_t task_id, uint32_t event_count);

/* Terminate a task. */
void ev_sched_terminate(ev_scheduler_t *evs, uint32_t task_id);

/* Get task by ID. Returns NULL if not found. */
ev_task_t *ev_sched_get_task(ev_scheduler_t *evs, uint32_t task_id);

/* Get currently running task. Returns NULL if none. */
ev_task_t *ev_sched_current(ev_scheduler_t *evs);

/* Update M5 coverage for the scheduler. */
surplus_real_t ev_sched_update_coverage(ev_scheduler_t *evs);

#endif /* EVENT_SCHED_H */
