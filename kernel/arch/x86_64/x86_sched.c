/* x86_sched.c: portable x86-64 process scheduler logic (no asm)
 *
 * The decisions the runtime makes about slots and time: which slot a new
 * process gets, what a tick does, who runs next, when a sleeper wakes. It
 * compiles and runs on the host (test_x86_sched.c); the page tables, FXSAVE
 * and the trap path that act on these decisions are in x86_userspace.c.
 * Mirrors src/el0_userspace/el0_sched.c for arm64, with the same semantics
 * for proc_take_slot, proc_abandon and proc_terminate.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "x86_userspace.h"

/* Defined by x86_userspace.c; absent in the host test, where processes own
 * no memory. Frees every frame of the process's address space. */
__attribute__((weak)) void x86_proc_release_space(user_proc_t *p);

static void release_space(user_proc_t *p)
{
    if (p->pml4 && x86_proc_release_space) x86_proc_release_space(p);
    p->pml4 = 0;
}

void proc_sched_init(proc_scheduler_t *ps)
{
    ps->num_procs = 0;
    ps->current_pid = 0;
    ps->next_pid = 1;
    ps->ticks = 0;
    ps->initialized = true;
    ps->total_context_switches = 0;
    ps->total_preemptions = 0;
    ps->total_syscalls = 0;
    ps->total_faults = 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        ps->procs[i].state = PROC_UNUSED;
        ps->procs[i].pid = 0;
        ps->procs[i].pml4 = 0;
    }
}

user_proc_t *proc_current(proc_scheduler_t *ps)
{
    if (!ps || ps->current_pid >= MAX_USER_PROCS) return 0;
    if (ps->procs[ps->current_pid].state == PROC_RUNNING) return &ps->procs[ps->current_pid];
    return 0;
}

user_proc_t *proc_get(proc_scheduler_t *ps, uint32_t pid)
{
    if (!ps || pid == 0) return 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++)
        if (ps->procs[i].pid == pid && ps->procs[i].state != PROC_UNUSED) return &ps->procs[i];
    return 0;
}

uint32_t proc_take_slot(proc_scheduler_t *ps)
{
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++)
        if (ps->procs[i].state == PROC_UNUSED) return i;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].state == PROC_TERMINATED) {
            release_space(&ps->procs[i]); /* already released at exit; harmless */
            ps->procs[i].state = PROC_UNUSED;
            ps->procs[i].pid = 0;
            return i;
        }
    }
    return MAX_USER_PROCS;
}

void proc_abandon(user_proc_t *proc, uint32_t slot)
{
    (void) slot;
    release_space(proc);
    proc->state = PROC_UNUSED;
    proc->pid = 0;
}

void proc_end(proc_scheduler_t *ps, user_proc_t *p, int32_t exit_code, lpres_state_t verdict)
{
    if (!ps || !p || p->state == PROC_UNUSED || p->state == PROC_TERMINATED) return;
    p->state = PROC_TERMINATED;
    p->exit_code = exit_code;
    p->exit_verdict = verdict;
    if (ps->num_procs) ps->num_procs--;
    release_space(p);
}

void proc_terminate(proc_scheduler_t *ps, uint32_t pid, int32_t exit_code)
{
    proc_end(ps, proc_get(ps, pid), exit_code, LPRES_STATE_TRUE);
}

uint32_t proc_find_next_ready(proc_scheduler_t *ps)
{
    if (!ps) return MAX_USER_PROCS;
    for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
        uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
        if (ps->procs[idx].state == PROC_READY) return idx;
    }
    return MAX_USER_PROCS;
}

void proc_wake_eligible(proc_scheduler_t *ps)
{
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        user_proc_t *p = &ps->procs[i];
        if (p->state == PROC_SLEEPING && ps->ticks >= p->sleep_until_tick) p->state = PROC_READY;
    }
}

void proc_on_tick(proc_scheduler_t *ps)
{
    ps->ticks++;
    proc_wake_eligible(ps);
    user_proc_t *curr = proc_current(ps);
    if (!curr) return;
    curr->cpu_time_ticks++;
    if (curr->quantum_ticks) curr->quantum_ticks--;
    if (curr->quantum_ticks == 0) {
        curr->state = PROC_READY;
        curr->quantum_ticks = curr->quantum_default;
    }
}

uint32_t proc_pick_next(proc_scheduler_t *ps)
{
    if (proc_current(ps)) return ps->current_pid;
    uint32_t idx = proc_find_next_ready(ps);
    if (idx >= MAX_USER_PROCS) return MAX_USER_PROCS;
    if (idx != ps->current_pid) ps->total_context_switches++;
    ps->current_pid = idx;
    ps->procs[idx].state = PROC_RUNNING;
    if (ps->procs[idx].quantum_ticks == 0)
        ps->procs[idx].quantum_ticks = ps->procs[idx].quantum_default;
    return idx;
}

bool proc_any_live(proc_scheduler_t *ps)
{
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        proc_state_t st = ps->procs[i].state;
        if (st == PROC_READY || st == PROC_RUNNING || st == PROC_SLEEPING || st == PROC_BLOCKED)
            return true;
    }
    return false;
}

uint64_t proc_ms_to_ticks(uint64_t ms)
{
    return (ms * X86_HZ + 999u) / 1000u;
}

void proc_sleep(proc_scheduler_t *ps, user_proc_t *p, uint64_t n)
{
    if (n == 0) {
        p->state = PROC_READY;
        return;
    }
    p->state = PROC_SLEEPING;
    p->sleep_until_tick = ps->ticks + n;
}
