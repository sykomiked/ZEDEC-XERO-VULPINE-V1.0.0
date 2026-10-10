/* el0_sched.c — Portable process scheduler logic (no ARM64 inline asm)
 *
 * This file contains the scheduler logic that can be compiled and
 * tested on any platform. The ARM64-specific EL0 entry/exit and
 * page table manipulation live in el0_userspace.c (ARM64 only).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "el0_userspace.h"
#include <string.h>

/* Weak references to ARM64-specific functions. These are defined in
 * el0_userspace.c (ARM64 only). When building host-side tests, they
 * are absent and the weak defaults (NULL) are used. */
__attribute__((weak)) void proc_pt_init(void);
__attribute__((weak)) void proc_pt_free(uint64_t *l0_table);

void proc_sched_init(proc_scheduler_t *ps) {
    ps->num_procs = 0;
    ps->current_pid = 0;
    ps->next_pid = 1;
    ps->ticks = 0;
    ps->initialized = true;
    ps->total_context_switches = 0;
    ps->total_preemptions = 0;
    ps->total_syscalls = 0;

    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        ps->procs[i].state = PROC_UNUSED;
        ps->procs[i].pid = 0;
        ps->procs[i].l0_table = 0;
    }

    if (proc_pt_init)
        proc_pt_init();
}

user_proc_t *proc_current(proc_scheduler_t *ps) {
    if (!ps || ps->num_procs == 0) return 0;
    if (ps->procs[ps->current_pid].state == PROC_RUNNING)
        return &ps->procs[ps->current_pid];
    return 0;
}

user_proc_t *proc_get(proc_scheduler_t *ps, uint32_t pid) {
    if (!ps) return 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].pid == pid && ps->procs[i].state != PROC_UNUSED)
            return &ps->procs[i];
    }
    return 0;
}

void proc_terminate(proc_scheduler_t *ps, uint32_t pid, int32_t exit_code) {
    if (!ps) return;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].pid == pid && ps->procs[i].state != PROC_UNUSED) {
            /* Terminating twice must not decrement num_procs twice. */
            if (ps->procs[i].state == PROC_TERMINATED) return;
            ps->procs[i].state = PROC_TERMINATED;
            ps->procs[i].exit_code = exit_code;
            ps->num_procs--;
            if (proc_pt_free && ps->procs[i].l0_table)
                proc_pt_free(ps->procs[i].l0_table);
            return;
        }
    }
}

/* Find the next READY process in round-robin order from current_pid */
uint32_t proc_find_next_ready(proc_scheduler_t *ps) {
    if (!ps) return MAX_USER_PROCS;
    for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
        uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
        if (ps->procs[idx].state == PROC_READY)
            return idx;
    }
    return MAX_USER_PROCS;  /* none found */
}

/* Tick the scheduler: decrement quantum, preempt if expired.
 * Returns true if a context switch is needed (new process selected). */
bool proc_sched_tick_logic(proc_scheduler_t *ps, cpu_context_t *ctx) {
    if (!ps || !ctx) return false;
    ps->ticks++;

    user_proc_t *curr = proc_current(ps);
    if (!curr) return false;

    /* Save context */
    for (int i = 0; i < 31; i++)
        curr->ctx.x[i] = ctx->x[i];
    curr->ctx.sp = ctx->sp;
    curr->ctx.pc = ctx->pc;
    curr->ctx.pstate = ctx->pstate;

    curr->cpu_time_ticks++;
    curr->quantum_ticks--;

    if (curr->quantum_ticks == 0) {
        curr->state = PROC_READY;
        curr->quantum_ticks = curr->quantum_default;
        ps->total_preemptions++;

        uint32_t next = proc_find_next_ready(ps);
        if (next < MAX_USER_PROCS && next != ps->current_pid) {
            ps->current_pid = next;
            ps->procs[next].state = PROC_RUNNING;
            ps->total_context_switches++;
            return true;
        } else {
            /* Same process continues */
            curr->state = PROC_RUNNING;
            return false;
        }
    }
    return false;
}
