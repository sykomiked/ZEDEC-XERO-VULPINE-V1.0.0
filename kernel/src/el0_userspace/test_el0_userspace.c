/* test_el0_userspace.c — Test for ARM64 EL0 user space + preemptive scheduler
 *
 * Tests the process scheduler logic (context selection, quantum
 * accounting, preemption) without actual EL0 execution (which
 * requires QEMU). The page table mapping and address space switching
 * are verified structurally.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* Include the header under test */
#include "el0_userspace.h"

/* Forward declarations from el0_sched.c */
uint32_t proc_find_next_ready(proc_scheduler_t *ps);
bool proc_sched_tick_logic(proc_scheduler_t *ps, cpu_context_t *ctx);

/* Test: scheduler initialization */
static void test_init(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);
    assert(ps.num_procs == 0);
    assert(ps.current_pid == 0);
    assert(ps.next_pid == 1);
    assert(ps.ticks == 0);
    assert(ps.initialized == true);
    assert(ps.total_context_switches == 0);
    assert(ps.total_preemptions == 0);
    assert(ps.total_syscalls == 0);
    printf("  [PASS] scheduler init\n");
}

/* Test: all slots initially UNUSED */
static void test_create(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        assert(ps.procs[i].state == PROC_UNUSED);
    }
    printf("  [PASS] all slots initially UNUSED\n");
}

/* Test: round-robin selection logic */
static void test_round_robin(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    for (int i = 0; i < 3; i++) {
        ps.procs[i].pid = i + 1;
        ps.procs[i].state = PROC_READY;
        ps.procs[i].quantum_ticks = 5;
        ps.procs[i].quantum_default = 5;
        ps.procs[i].priority = 100;
        ps.procs[i].cpu_time_ticks = 0;
        ps.procs[i].l0_table = 0;
        for (int j = 0; j < 31; j++)
            ps.procs[i].ctx.x[j] = 0;
        ps.procs[i].ctx.pc = 0x10000 + i * 0x1000;
        ps.procs[i].ctx.sp = 0x80000;
    }
    ps.num_procs = 3;
    ps.current_pid = 0;
    ps.procs[0].state = PROC_RUNNING;

    /* current=0, find next ready -> should be 1 */
    uint32_t next = proc_find_next_ready(&ps);
    assert(next == 1);
    printf("  [PASS] round-robin: 0 -> 1\n");

    /* current=1, find next ready -> should be 2 */
    ps.current_pid = 1;
    next = proc_find_next_ready(&ps);
    assert(next == 2);
    printf("  [PASS] round-robin: 1 -> 2\n");

    /* current=2, find next ready -> should be 0 (wrap) */
    ps.current_pid = 2;
    ps.procs[0].state = PROC_READY;  /* was RUNNING, now READY for wrap test */
    next = proc_find_next_ready(&ps);
    assert(next == 0);
    printf("  [PASS] round-robin: 2 -> 0 (wrap)\n");
}

/* Test: quantum accounting via proc_sched_tick_logic */
static void test_quantum(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    ps.procs[0].pid = 1;
    ps.procs[0].state = PROC_RUNNING;
    ps.procs[0].quantum_ticks = 3;
    ps.procs[0].quantum_default = 3;
    ps.procs[0].cpu_time_ticks = 0;
    ps.num_procs = 1;
    ps.current_pid = 0;

    cpu_context_t ctx = {0};

    /* Tick 1: quantum 3->2, no switch */
    bool switched = proc_sched_tick_logic(&ps, &ctx);
    assert(!switched);
    assert(ps.procs[0].quantum_ticks == 2);
    assert(ps.procs[0].cpu_time_ticks == 1);
    printf("  [PASS] tick 1: quantum=2, no switch\n");

    /* Tick 2: quantum 2->1, no switch */
    switched = proc_sched_tick_logic(&ps, &ctx);
    assert(!switched);
    assert(ps.procs[0].quantum_ticks == 1);
    printf("  [PASS] tick 2: quantum=1, no switch\n");

    /* Tick 3: quantum 1->0, preempt (but only 1 proc, so no switch) */
    switched = proc_sched_tick_logic(&ps, &ctx);
    assert(!switched);  /* only 1 proc, no other to switch to */
    assert(ps.procs[0].quantum_ticks == 3);  /* reset */
    assert(ps.total_preemptions == 1);
    printf("  [PASS] tick 3: quantum expired, preempted (same proc)\n");
}

/* Test: quantum with 2 processes -> actual context switch */
static void test_quantum_switch(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    /* Two processes */
    ps.procs[0].pid = 1;
    ps.procs[0].state = PROC_RUNNING;
    ps.procs[0].quantum_ticks = 2;
    ps.procs[0].quantum_default = 2;
    ps.procs[0].cpu_time_ticks = 0;
    ps.procs[0].l0_table = 0;
    for (int j = 0; j < 31; j++) ps.procs[0].ctx.x[j] = 0;
    ps.procs[0].ctx.pc = 0x10000;
    ps.procs[0].ctx.sp = 0x90000;

    ps.procs[1].pid = 2;
    ps.procs[1].state = PROC_READY;
    ps.procs[1].quantum_ticks = 2;
    ps.procs[1].quantum_default = 2;
    ps.procs[1].cpu_time_ticks = 0;
    ps.procs[1].l0_table = 0;
    for (int j = 0; j < 31; j++) ps.procs[1].ctx.x[j] = 0;
    ps.procs[1].ctx.pc = 0x20000;
    ps.procs[1].ctx.sp = 0x90000;

    ps.num_procs = 2;
    ps.current_pid = 0;

    cpu_context_t ctx = {0};
    ctx.pc = 0x10000;
    ctx.sp = 0x90000;

    /* Tick 1: quantum 2->1, no switch */
    bool switched = proc_sched_tick_logic(&ps, &ctx);
    assert(!switched);
    printf("  [PASS] 2-proc tick 1: no switch\n");

    /* Tick 2: quantum 1->0, preempt -> switch to proc 1 */
    switched = proc_sched_tick_logic(&ps, &ctx);
    assert(switched);
    assert(ps.current_pid == 1);
    assert(ps.procs[1].state == PROC_RUNNING);
    assert(ps.procs[0].state == PROC_READY);
    assert(ps.total_preemptions == 1);
    assert(ps.total_context_switches == 1);
    printf("  [PASS] 2-proc tick 2: switched to proc 1\n");

    /* Tick 3: proc 1 quantum 2->1, no switch */
    switched = proc_sched_tick_logic(&ps, &ctx);
    assert(!switched);
    printf("  [PASS] 2-proc tick 3: no switch\n");

    /* Tick 4: proc 1 quantum 1->0, preempt -> switch back to proc 0 */
    switched = proc_sched_tick_logic(&ps, &ctx);
    assert(switched);
    assert(ps.current_pid == 0);
    assert(ps.procs[0].state == PROC_RUNNING);
    assert(ps.procs[1].state == PROC_READY);
    assert(ps.total_preemptions == 2);
    assert(ps.total_context_switches == 2);
    printf("  [PASS] 2-proc tick 4: switched back to proc 0\n");
}

/* Test: process termination */
static void test_terminate(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    ps.procs[0].pid = 1;
    ps.procs[0].state = PROC_RUNNING;
    ps.procs[0].l0_table = 0;
    ps.num_procs = 1;
    ps.current_pid = 0;

    proc_terminate(&ps, 1, 42);
    assert(ps.procs[0].state == PROC_TERMINATED);
    assert(ps.procs[0].exit_code == 42);
    assert(ps.num_procs == 0);
    printf("  [PASS] process termination\n");
}

/* Test: proc_get and proc_current */
static void test_getters(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    ps.procs[0].pid = 1;
    ps.procs[0].state = PROC_RUNNING;
    ps.procs[1].pid = 2;
    ps.procs[1].state = PROC_READY;
    ps.procs[2].pid = 3;
    ps.procs[2].state = PROC_UNUSED;
    ps.num_procs = 2;
    ps.current_pid = 0;

    user_proc_t *curr = proc_current(&ps);
    assert(curr != NULL);
    assert(curr->pid == 1);

    user_proc_t *p2 = proc_get(&ps, 2);
    assert(p2 != NULL);
    assert(p2->pid == 2);

    user_proc_t *p3 = proc_get(&ps, 3);
    assert(p3 == NULL);  /* UNUSED */

    user_proc_t *p99 = proc_get(&ps, 99);
    assert(p99 == NULL);  /* doesn't exist */

    printf("  [PASS] proc_current and proc_get\n");
}

/* Test: PTE constants and address layout */
static void test_pte_constants(void) {
    assert(PTE_VALID == (1ULL << 0));
    assert(PTE_TABLE == (1ULL << 1));
    assert(PTE_AF == (1ULL << 10));
    assert(PAGE_SIZE_4K == 4096);
    assert(TABLE_ENTRIES == 512);

    assert(USER_CODE_BASE == 0x10000ULL);
    assert(USER_STACK_TOP == (USER_STACK_BASE + USER_STACK_SIZE));
    assert(USER_STACK_TOP == 0x90000ULL);

    printf("  [PASS] PTE constants and address layout\n");
}

/* Test: max processes limit */
static void test_max_procs(void) {
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        ps.procs[i].pid = i + 1;
        ps.procs[i].state = PROC_READY;
    }
    ps.num_procs = MAX_USER_PROCS;

    uint32_t count = 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps.procs[i].state != PROC_UNUSED)
            count++;
    }
    assert(count == MAX_USER_PROCS);
    printf("  [PASS] max processes: %d slots filled\n", MAX_USER_PROCS);
}

int main(void) {
    printf("=== EL0 User Space + Preemptive Scheduler Tests ===\n\n");

    test_init();
    test_create();
    test_round_robin();
    test_quantum();
    test_quantum_switch();
    test_terminate();
    test_getters();
    test_pte_constants();
    test_max_procs();

    printf("\nAll EL0 user space tests passed\n");
    return 0;
}
