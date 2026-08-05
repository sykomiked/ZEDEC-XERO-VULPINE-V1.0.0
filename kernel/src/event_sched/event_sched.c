/* event_sched.c — Event-Driven Scheduler implementation
 *
 * See event_sched.h for design rationale. Follows the same conventions
 * as count_house.c / porter_house.c: SR_* fixed-point macros, M5
 * coordinate mirroring, coverage computed inline.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "event_sched.h"
#include <string.h>

void ev_sched_init(ev_scheduler_t *evs) {
    if (!evs) return;
    memset(evs, 0, sizeof(*evs));
    evs->num_tasks = 0;
    evs->current_task = 0;
    evs->next_id = 1;
    evs->idle = true;
    evs->idle_ratio = SR_ONE; /* start fully idle */

    evs->m5.omega = 0;
    evs->m5.chi = 0;
    evs->m5.phi = SR_ZERO;

    ev_sched_update_coverage(evs);
}

int32_t ev_sched_create_task(ev_scheduler_t *evs, const char *name,
                               uint32_t event_budget, uint32_t priority) {
    if (!evs) return -1;

    /* Find unused slot */
    uint32_t slot = EV_MAX_TASKS;
    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        if (evs->tasks[i].state == EV_TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot >= EV_MAX_TASKS) return -1;

    ev_task_t *task = &evs->tasks[slot];
    memset(task, 0, sizeof(*task));
    task->id = evs->next_id++;
    task->state = EV_TASK_READY;
    task->event_budget = (event_budget > EV_MAX_BUDGET) ? EV_MAX_BUDGET : event_budget;
    task->events_remaining = task->event_budget;
    task->priority = (priority > 1000) ? 1000 : priority;
    task->pending_events = 0;
    task->cycle_cost = SR_ZERO;
    task->total_cycle_cost = SR_ZERO;
    task->total_events_dispatched = 0;

    /* Copy name */
    uint32_t i;
    for (i = 0; i + 1 < EV_MAX_NAME_LEN && name && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[i] = '\0';

    /* M5: omega = task ID, chi = priority, phi = budget */
    task->m5.omega = task->id;
    task->m5.chi = task->priority;
    task->m5.phi = SR_FROM_INT((int64_t)task->event_budget);

    evs->num_tasks++;
    return (int32_t)task->id;
}

int32_t ev_sched_post_events(ev_scheduler_t *evs, uint32_t task_id,
                               uint32_t event_count) {
    if (!evs) return -1;
    ev_task_t *task = ev_sched_get_task(evs, task_id);
    if (!task) return -1;

    task->pending_events += event_count;

    /* Wake task if it was idle or blocked */
    if (task->state == EV_TASK_IDLE || task->state == EV_TASK_BLOCKED) {
        task->state = EV_TASK_READY;
        task->events_remaining = task->event_budget;
    }

    evs->idle = false;
    return 0;
}

void ev_sched_charge_cycles(ev_scheduler_t *evs, surplus_real_t cost) {
    if (!evs) return;
    ev_task_t *task = ev_sched_current(evs);
    if (!task) return;

    task->cycle_cost = SR_ADD(task->cycle_cost, cost);
    task->total_cycle_cost = SR_ADD(task->total_cycle_cost, cost);
}

int32_t ev_sched_dispatch(ev_scheduler_t *evs) {
    if (!evs) return -1;

    /* Find highest-priority task with pending events */
    uint32_t best = EV_MAX_TASKS;
    uint32_t best_score = 0;

    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        ev_task_t *t = &evs->tasks[i];
        if (t->state != EV_TASK_READY) continue;
        if (t->pending_events == 0) continue;
        if (t->events_remaining == 0) continue;

        /* Priority score: priority + pending_events weighting */
        uint32_t score = t->priority + (t->pending_events > 100 ? 100 : t->pending_events);
        if (best >= EV_MAX_TASKS || score > best_score) {
            best_score = score;
            best = i;
        }
    }

    if (best >= EV_MAX_TASKS) {
        /* No ready task with pending events — enter tickless idle */
        evs->idle = true;
        evs->total_idle_cycles++;
        ev_sched_update_coverage(evs);
        return -1;
    }

    ev_task_t *task = &evs->tasks[best];
    task->state = EV_TASK_RUNNING;
    evs->current_task = best;

    /* Dispatch up to min(events_remaining, pending_events) events */
    uint32_t to_dispatch = task->events_remaining;
    if (to_dispatch > task->pending_events) {
        to_dispatch = task->pending_events;
    }

    task->pending_events -= to_dispatch;
    task->events_remaining -= to_dispatch;
    task->total_events_dispatched += to_dispatch;
    task->m5.omega = (uint32_t)(task->total_events_dispatched & 0xFFFFFFFF);

    /* Reset per-dispatch cycle cost */
    task->cycle_cost = SR_ZERO;

    /* If budget exhausted or no more pending events, transition state */
    if (task->pending_events == 0) {
        task->state = (task->events_remaining == 0) ? EV_TASK_IDLE : EV_TASK_BLOCKED;
    } else if (task->events_remaining == 0) {
        task->state = EV_TASK_IDLE;
    } else {
        task->state = EV_TASK_READY;
    }

    evs->total_dispatches++;
    evs->total_events_handled += to_dispatch;
    evs->idle = false;

    ev_sched_update_coverage(evs);
    return (int32_t)task->id;
}

bool ev_sched_check_idle(ev_scheduler_t *evs) {
    if (!evs) return true;
    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        ev_task_t *t = &evs->tasks[i];
        if (t->state == EV_TASK_READY && t->pending_events > 0 && t->events_remaining > 0) {
            evs->idle = false;
            return false;
        }
    }
    evs->idle = true;
    evs->total_idle_cycles++;
    return true;
}

void ev_sched_replenish(ev_scheduler_t *evs) {
    if (!evs) return;
    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        ev_task_t *t = &evs->tasks[i];
        if (t->state == EV_TASK_IDLE) {
            t->events_remaining = t->event_budget;
            if (t->pending_events > 0) {
                t->state = EV_TASK_READY;
            }
        }
    }
    evs->idle = false;
}

void ev_sched_block(ev_scheduler_t *evs, uint32_t task_id) {
    if (!evs) return;
    ev_task_t *task = ev_sched_get_task(evs, task_id);
    if (task) task->state = EV_TASK_BLOCKED;
}

void ev_sched_unblock(ev_scheduler_t *evs, uint32_t task_id, uint32_t event_count) {
    if (!evs) return;
    ev_sched_post_events(evs, task_id, event_count);
}

void ev_sched_terminate(ev_scheduler_t *evs, uint32_t task_id) {
    if (!evs) return;
    ev_task_t *task = ev_sched_get_task(evs, task_id);
    if (task) {
        task->state = EV_TASK_TERMINATED;
        if (evs->num_tasks > 0) evs->num_tasks--;
    }
}

ev_task_t *ev_sched_current(ev_scheduler_t *evs) {
    if (!evs || evs->num_tasks == 0) return NULL;
    if (evs->current_task >= EV_MAX_TASKS) return NULL;
    ev_task_t *t = &evs->tasks[evs->current_task];
    if (t->state == EV_TASK_UNUSED || t->state == EV_TASK_TERMINATED) return NULL;
    return t;
}

ev_task_t *ev_sched_get_task(ev_scheduler_t *evs, uint32_t task_id) {
    if (!evs) return NULL;
    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        if (evs->tasks[i].id == task_id &&
            evs->tasks[i].state != EV_TASK_UNUSED &&
            evs->tasks[i].state != EV_TASK_TERMINATED) {
            return &evs->tasks[i];
        }
    }
    return NULL;
}

surplus_real_t ev_sched_update_coverage(ev_scheduler_t *evs) {
    if (!evs) return SR_ZERO;

    /* r: dispatch ratio = dispatches / (dispatches + idle_cycles).
     * A scheduler that's always idle has r=0; one that's always
     * dispatching has r=1. */
    uint64_t total = evs->total_dispatches + evs->total_idle_cycles;
    if (total == 0) {
        evs->m5.r = SR_ZERO;
    } else {
        evs->m5.r = SR_DIV(SR_FROM_INT((int64_t)evs->total_dispatches),
                            SR_FROM_INT((int64_t)total));
    }

    /* ell: fraction of tasks that are not IDLE/UNUSED/TERMINATED --
     * a scheduler where every task is idle is not really scheduling. */
    uint32_t active = 0;
    uint32_t total_tasks = 0;
    for (uint32_t i = 0; i < EV_MAX_TASKS; i++) {
        if (evs->tasks[i].state == EV_TASK_UNUSED) continue;
        total_tasks++;
        if (evs->tasks[i].state == EV_TASK_READY ||
            evs->tasks[i].state == EV_TASK_RUNNING ||
            evs->tasks[i].state == EV_TASK_BLOCKED) {
            active++;
        }
    }
    evs->m5.ell = (total_tasks == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)active), SR_FROM_INT((int64_t)total_tasks));

    /* idle_ratio: complement of r */
    evs->idle_ratio = SR_SUB(SR_ONE, evs->m5.r);
    if (SR_CMP(evs->idle_ratio, SR_ZERO) < 0) evs->idle_ratio = SR_ZERO;

    /* Coverage hyperbola */
    surplus_real_t product = SR_MUL(evs->m5.r, evs->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    evs->coverage_ratio = SR_DIV(product, floor);

    return evs->coverage_ratio;
}
