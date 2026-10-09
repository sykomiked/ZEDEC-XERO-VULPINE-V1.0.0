/* test_event_sched.c — Event-Driven Scheduler tests
 *
 * Tests task creation, event posting, dispatch with event budgets,
 * PMU cycle cost charging, tickless idle detection, budget replenishment,
 * block/unblock, and M5 coverage computation.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "event_sched.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

int main(void) {
    /* ===== init defaults ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        assert(evs.num_tasks == 0);
        assert(evs.next_id == 1);
        assert(evs.idle == true);
        assert(evs.total_dispatches == 0);
        assert(evs.total_idle_cycles == 0);
        assert(feq(evs.idle_ratio, 1.0, 1e-9)); /* start fully idle */
    }

    /* ===== task creation ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id1 = ev_sched_create_task(&evs, "net-rx", 50, 800);
        assert(id1 > 0);
        int32_t id2 = ev_sched_create_task(&evs, "count-house", 100, 900);
        assert(id2 > 0 && id2 != id1);
        assert(evs.num_tasks == 2);

        ev_task_t *t1 = ev_sched_get_task(&evs, (uint32_t)id1);
        assert(t1 != NULL);
        assert(strcmp(t1->name, "net-rx") == 0);
        assert(t1->event_budget == 50);
        assert(t1->priority == 800);
        assert(t1->state == EV_TASK_READY);
        assert(t1->events_remaining == 50);
    }

    /* ===== event posting wakes idle tasks ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "test", 10, 500);
        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);

        /* Manually set to IDLE to test waking */
        t->state = EV_TASK_IDLE;
        t->events_remaining = 0;

        assert(ev_sched_post_events(&evs, (uint32_t)id, 5) == 0);
        assert(t->pending_events == 5);
        assert(t->state == EV_TASK_READY);
        assert(t->events_remaining == 10); /* replenished on wake */
        assert(evs.idle == false);
    }

    /* ===== dispatch: highest priority with pending events wins ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t low = ev_sched_create_task(&evs, "low", 10, 100);
        int32_t high = ev_sched_create_task(&evs, "high", 10, 900);

        ev_sched_post_events(&evs, (uint32_t)low, 3);
        ev_sched_post_events(&evs, (uint32_t)high, 3);

        int32_t dispatched = ev_sched_dispatch(&evs);
        assert(dispatched == high); /* higher priority dispatched first */
        assert(evs.total_dispatches == 1);
        assert(evs.total_events_handled == 3);

        ev_task_t *t_high = ev_sched_get_task(&evs, (uint32_t)high);
        assert(t_high->pending_events == 0);
        assert(t_high->total_events_dispatched == 3);
    }

    /* ===== event budget limits dispatch ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "budget-test", 5, 500);
        ev_sched_post_events(&evs, (uint32_t)id, 20); /* 20 pending, budget 5 */

        int32_t dispatched = ev_sched_dispatch(&evs);
        assert(dispatched == id);

        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);
        assert(t->total_events_dispatched == 5); /* only 5 dispatched (budget) */
        assert(t->pending_events == 15); /* 15 still pending */
        assert(t->events_remaining == 0); /* budget exhausted */
        assert(t->state == EV_TASK_IDLE); /* budget exhausted -> idle */
    }

    /* ===== tickless idle: no pending events -> WFI ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        ev_sched_create_task(&evs, "idle-test", 10, 500);
        /* No events posted */

        int32_t dispatched = ev_sched_dispatch(&evs);
        assert(dispatched == -1); /* no task ready */
        assert(evs.idle == true);
        assert(evs.total_idle_cycles == 1);
    }

    /* ===== budget replenishment ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "replenish", 5, 500);
        ev_sched_post_events(&evs, (uint32_t)id, 20);

        /* Dispatch exhausts budget (5 events) */
        ev_sched_dispatch(&evs);
        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);
        assert(t->events_remaining == 0);
        assert(t->state == EV_TASK_IDLE);
        assert(t->pending_events == 15);

        /* Replenish */
        ev_sched_replenish(&evs);
        assert(t->events_remaining == 5); /* budget restored */
        assert(t->state == EV_TASK_READY); /* has pending events -> ready */

        /* Second dispatch */
        ev_sched_dispatch(&evs);
        assert(t->total_events_dispatched == 10);
        assert(t->pending_events == 10);
    }

    /* ===== PMU cycle cost charging ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "cost-test", 10, 500);
        ev_sched_post_events(&evs, (uint32_t)id, 3);
        ev_sched_dispatch(&evs);

        /* Simulate event handler charging cycles */
        ev_sched_charge_cycles(&evs, SR_FROM_INT(42));
        ev_sched_charge_cycles(&evs, SR_FROM_INT(8));

        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);
        assert(feq(t->total_cycle_cost, SR_FROM_INT(50), 1e-9));
    }

    /* ===== block / unblock ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "block-test", 10, 500);
        ev_sched_post_events(&evs, (uint32_t)id, 5);

        ev_sched_block(&evs, (uint32_t)id);
        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);
        assert(t->state == EV_TASK_BLOCKED);

        /* Dispatch should skip blocked task */
        int32_t dispatched = ev_sched_dispatch(&evs);
        assert(dispatched == -1); /* blocked task can't be dispatched */

        /* Unblock with events */
        ev_sched_unblock(&evs, (uint32_t)id, 3);
        assert(t->state == EV_TASK_READY);
        assert(t->pending_events == 8); /* 5 original + 3 new */

        dispatched = ev_sched_dispatch(&evs);
        assert(dispatched == id);
    }

    /* ===== coverage computation ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        ev_sched_create_task(&evs, "cov-1", 10, 500);
        ev_sched_create_task(&evs, "cov-2", 10, 600);

        /* Post events and dispatch one */
        ev_sched_post_events(&evs, 1, 5);
        ev_sched_dispatch(&evs);

        /* Now dispatch again with no pending events -> idle */
        ev_sched_dispatch(&evs);
        assert(evs.total_idle_cycles == 1);

        surplus_real_t cov = ev_sched_update_coverage(&evs);
        assert(evs.total_dispatches == 1);
        /* r = 1/(1+1) = 0.5, ell = 2/2 = 1.0 (both active) */
        assert(feq(evs.m5.r, 0.5, 1e-9));
        assert(feq(evs.m5.ell, 1.0, 1e-9));
        double expected_cov = (0.5 * 1.0) / 1.8;
        assert(feq(cov, expected_cov, 1e-9));
    }

    /* ===== terminate ===== */
    {
        ev_scheduler_t evs;
        ev_sched_init(&evs);
        int32_t id = ev_sched_create_task(&evs, "term-test", 10, 500);
        ev_sched_terminate(&evs, (uint32_t)id);
        ev_task_t *t = ev_sched_get_task(&evs, (uint32_t)id);
        assert(t == NULL); /* terminated tasks can't be retrieved */
        assert(evs.num_tasks == 0);
    }

    printf("All Event-Driven Scheduler tests passed\n");
    return 0;
}
