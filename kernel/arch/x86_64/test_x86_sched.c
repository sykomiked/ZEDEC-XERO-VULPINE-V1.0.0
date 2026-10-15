/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_x86_sched.c: host tests for the portable x86-64 scheduler logic
 * (x86_sched.c): slot reuse, round robin, quantum expiry, sleep and wake,
 * exit verdicts, and the release hook that returns a process's frames. */
#include "x86_userspace.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* Stands in for x86_userspace.c's frame release: counts and clears. */
static int g_released;
void x86_proc_release_space(user_proc_t *p)
{
    g_released++;
    p->pml4 = 0;
}

static uint64_t g_fake_l4[MAX_USER_PROCS][1];

/* What proc_create_from_elf does to a slot, without memory. */
static uint32_t spawn(proc_scheduler_t *ps, uint32_t quantum)
{
    uint32_t s = proc_take_slot(ps);
    if (s >= MAX_USER_PROCS) return s;
    user_proc_t *p = &ps->procs[s];
    p->pml4 = g_fake_l4[s];
    p->pid = ps->next_pid++;
    p->state = PROC_READY;
    p->quantum_default = p->quantum_ticks = quantum;
    p->exit_verdict = LPRES_STATE_NEITHER;
    p->exit_code = 0;
    ps->num_procs++;
    return s;
}

static proc_scheduler_t ps;

static void test_slots(void)
{
    proc_sched_init(&ps);
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) CHECK(spawn(&ps, 2) == i);
    CHECK(ps.num_procs == MAX_USER_PROCS);
    CHECK(proc_take_slot(&ps) == MAX_USER_PROCS); /* all live: no slot */

    /* an exit frees the address space at once and keeps the slot readable */
    g_released = 0;
    proc_terminate(&ps, ps.procs[5].pid, 7);
    CHECK(g_released == 1 && ps.procs[5].pml4 == 0);
    CHECK(ps.procs[5].state == PROC_TERMINATED && ps.procs[5].exit_code == 7);
    CHECK(ps.procs[5].exit_verdict == LPRES_STATE_TRUE);
    CHECK(proc_get(&ps, ps.procs[5].pid) == &ps.procs[5]);
    CHECK(ps.num_procs == MAX_USER_PROCS - 1);
    /* a second end changes nothing */
    proc_end(&ps, &ps.procs[5], -1, LPRES_STATE_FALSE);
    CHECK(ps.procs[5].exit_code == 7 && ps.procs[5].exit_verdict == LPRES_STATE_TRUE);
    CHECK(ps.num_procs == MAX_USER_PROCS - 1 && g_released == 1);

    /* the TERMINATED slot is reaped for the next process */
    CHECK(spawn(&ps, 2) == 5);
    CHECK(ps.procs[5].pid == MAX_USER_PROCS + 1);

    /* an UNUSED slot is preferred over a TERMINATED one */
    proc_end(&ps, &ps.procs[9], -1, LPRES_STATE_FALSE);
    proc_abandon(&ps.procs[20], 20);
    CHECK(ps.procs[20].state == PROC_UNUSED && ps.procs[20].pml4 == 0);
    CHECK(proc_take_slot(&ps) == 20);
    CHECK(ps.procs[9].exit_verdict == LPRES_STATE_FALSE);
    CHECK(proc_get(&ps, 0) == 0);
}

static void test_round_robin(void)
{
    proc_sched_init(&ps);
    uint32_t a = spawn(&ps, 2), b = spawn(&ps, 2), c = spawn(&ps, 2);
    CHECK(a == 0 && b == 1 && c == 2);
    ps.current_pid = MAX_USER_PROCS - 1; /* nothing has run yet */
    CHECK(proc_current(&ps) == 0);
    CHECK(proc_pick_next(&ps) == a);
    CHECK(proc_current(&ps) == &ps.procs[a]);
    CHECK(proc_pick_next(&ps) == a); /* RUNNING stays */

    /* quantum 2: the first tick charges, the second expires */
    proc_on_tick(&ps);
    CHECK(ps.procs[a].state == PROC_RUNNING && ps.procs[a].quantum_ticks == 1);
    proc_on_tick(&ps);
    CHECK(ps.procs[a].state == PROC_READY && ps.procs[a].quantum_ticks == 2);
    CHECK(ps.procs[a].cpu_time_ticks == 2 && ps.ticks == 2);
    uint32_t sw = ps.total_context_switches;
    CHECK(proc_pick_next(&ps) == b);
    CHECK(ps.total_context_switches == sw + 1);
    proc_on_tick(&ps);
    proc_on_tick(&ps);
    CHECK(proc_pick_next(&ps) == c);
    proc_on_tick(&ps);
    proc_on_tick(&ps);
    CHECK(proc_pick_next(&ps) == a); /* wraps */

    /* a lone READY process picks itself without a context switch */
    proc_end(&ps, &ps.procs[b], 0, LPRES_STATE_TRUE);
    proc_end(&ps, &ps.procs[c], 0, LPRES_STATE_TRUE);
    ps.procs[a].state = PROC_READY; /* yield */
    sw = ps.total_context_switches;
    CHECK(proc_pick_next(&ps) == a && ps.total_context_switches == sw);

    /* the process ends: nothing to run, nothing live */
    proc_end(&ps, &ps.procs[a], 0, LPRES_STATE_TRUE);
    CHECK(proc_pick_next(&ps) == MAX_USER_PROCS);
    CHECK(!proc_any_live(&ps));
    CHECK(ps.num_procs == 0);
}

static void test_sleep(void)
{
    proc_sched_init(&ps);
    uint32_t a = spawn(&ps, 5), b = spawn(&ps, 5);
    ps.current_pid = MAX_USER_PROCS - 1;
    CHECK(proc_pick_next(&ps) == a);
    CHECK(proc_ms_to_ticks(0) == 0);
    CHECK(proc_ms_to_ticks(1) == 1); /* rounds up */
    CHECK(proc_ms_to_ticks(10) == 1);
    CHECK(proc_ms_to_ticks(20) == 2);
    CHECK(proc_ms_to_ticks(25) == 3);
    proc_sleep(&ps, &ps.procs[a], proc_ms_to_ticks(20));
    CHECK(ps.procs[a].state == PROC_SLEEPING);
    CHECK(proc_pick_next(&ps) == b);
    proc_on_tick(&ps);
    CHECK(ps.procs[a].state == PROC_SLEEPING);
    proc_on_tick(&ps);
    CHECK(ps.procs[a].state == PROC_READY); /* woke at its tick */
    CHECK(proc_pick_next(&ps) == b);        /* b still has quantum */

    /* only sleepers left: nothing runnable, but still live (idle, not done) */
    proc_end(&ps, &ps.procs[b], 0, LPRES_STATE_TRUE);
    CHECK(proc_pick_next(&ps) == a);
    proc_sleep(&ps, &ps.procs[a], 3);
    CHECK(proc_pick_next(&ps) == MAX_USER_PROCS);
    CHECK(proc_any_live(&ps));
    proc_on_tick(&ps); /* idle ticks: nobody RUNNING, the clock still moves */
    proc_on_tick(&ps);
    CHECK(proc_pick_next(&ps) == MAX_USER_PROCS);
    proc_on_tick(&ps);
    CHECK(proc_pick_next(&ps) == a);

    /* sleep 0 is a yield */
    proc_sleep(&ps, &ps.procs[a], 0);
    CHECK(ps.procs[a].state == PROC_READY);
}

int main(void)
{
    test_slots();
    test_round_robin();
    test_sleep();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
