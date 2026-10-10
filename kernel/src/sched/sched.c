/* sched.c — Process Scheduler Implementation
 * Round-robin with M5 priority weighting. Task records carry esp/ebp/eip and
 * a prepared stack, but sched_switch() only picks the next task: no register
 * save/restore is performed here (see the note in sched_switch).
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "sched.h"
#include "../phase_coord/phase_coordinator.h"
#include "../rmag/rmag_core.h"
#include "../lpres/lpres_core.h"
#include "../../include/m5_types.h"

static __attribute__((unused)) int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
/* Bounded: task names are TASK_NAME_LEN bytes; a longer name is cut, never
 * written past the field (it used to overrun into the task's state). */
static void str_copy(char *d, const char *s)
{
    int i = 0;
    while (s && s[i] && i < TASK_NAME_LEN - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

void sched_init(scheduler_t *sched) {
    sched->num_tasks = 0;
    sched->current_task = 0;
    sched->next_pid = 1;
    sched->ticks = 0;
    sched->initialized = true;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        sched->tasks[i].state = TASK_UNUSED;
        sched->tasks[i].id = 0;
    }
}

int32_t sched_create_task(scheduler_t *sched, const char *name, task_type_t type,
                           void (*entry_point)(void), uint32_t priority) {
    if (sched->num_tasks >= MAX_TASKS) return -1;

    /* A terminated task's slot is free again (num_tasks already dropped it).
     * With no free slot the old code fell through to slot 0 and overwrote
     * whatever task lived there. */
    uint32_t slot = MAX_TASKS;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (sched->tasks[i].state == TASK_UNUSED || sched->tasks[i].state == TASK_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_TASKS) return -1;

    task_t *task = &sched->tasks[slot];
    if (sched->next_pid == 0) sched->next_pid = 1; /* id 0 means "no task" */
    task->id = sched->next_pid++;
    str_copy(task->name, name);
    task->state = TASK_READY;
    task->type = type;
    task->parent_id = 0;
    task->sleep_until = 0;
    task->exit_code = 0;
    task->priority = priority;
    task->omega = 0;
    task->phase = 0;
    task->collapse_count = 0;
    task->cpu_time_ms = 0;

    /* Set up initial stack: entry_point at top, then a return to terminate.
     * Three machine words, so a 64-bit entry address is stored whole. */
    uintptr_t stack_top = (uintptr_t) &task->stack[KERNEL_STACK_WORDS];
    task->esp = stack_top - 3u * sizeof(uintptr_t);
    task->ebp = task->esp;
    task->eip = (uintptr_t) entry_point;
    task->cr3 = 0; /* no per-task address space yet */

    /* Stack layout: [entry_point] [sched_terminate_addr] [flags] */
    uintptr_t *sp = &task->stack[KERNEL_STACK_WORDS - 3u];
    sp[0] = task->eip;
    sp[1] = 0;  /* return address placeholder */
    sp[2] = 0x202; /* EFLAGS with IF set */

    sched->num_tasks++;
    return (int32_t)task->id;
}

void sched_switch(scheduler_t *sched) {
    if (sched->num_tasks == 0) return;

    task_t *curr = &sched->tasks[sched->current_task];
    if (curr->state == TASK_RUNNING)
        curr->state = TASK_READY;

    /* Find next ready task with highest priority (M5 omega-weighted) */
    uint32_t best = sched->current_task;
    uint32_t best_score = 0;
    for (uint32_t i = 1; i <= MAX_TASKS; i++) {
        uint32_t idx = (sched->current_task + i) % MAX_TASKS;
        task_t *t = &sched->tasks[idx];
        if (t->state == TASK_READY) {
            /* M5 priority: omega + priority weighting */
            uint32_t score = t->priority + (t->omega % 8) + 1;
            if (score > best_score || best == sched->current_task) {
                best_score = score;
                best = idx;
            }
        }
    }

    if (sched->tasks[best].state != TASK_READY) return;

    sched->current_task = best;
    sched->tasks[best].state = TASK_RUNNING;
    sched->tasks[best].omega++;

    /* Context switch would happen here via asm:
     * save current esp/ebp/eip, load new ones
     * For now, this is a cooperative stub
     */
}

void sched_yield(scheduler_t *sched) {
    sched_switch(sched);
}

void sched_tick(scheduler_t *sched) {
    sched->ticks++;

    /* M5 Phase Coordinator: verify coverage hyperbola before scheduling */
    /* r * l >= 1.8 — RMAG magnitude * LPRES presence must hold */
    task_t *curr = sched_current(sched);
    if (curr) {
        rational_t r = rmag_get_quota((ordinal_t)curr->id);
        trit_t ell = lpres_get_presence((ordinal_t)curr->id);
        if (m5_coverage_cmp(r, ell, 9, 5) < 0) { /* r * ell < 1.8, exact */
            /* Coverage violation: task lacks sufficient attestation.
             * Do NOT schedule this task — yield instead. */
            curr->collapse_count++;
            sched_switch(sched);
            return;
        }
        curr->phase = m5_coverage_permille(r, ell);
    }

    if (sched->ticks % 10 == 0) {
        sched_switch(sched);
    }

    /* Wake sleeping tasks — LPRES attestation required for wakeup */
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (sched->tasks[i].state == TASK_SLEEPING &&
            sched->ticks >= sched->tasks[i].sleep_until) {
            trit_t presence = lpres_get_presence((ordinal_t)sched->tasks[i].id);
            if (presence != TRIT_FALSE) {
                sched->tasks[i].state = TASK_READY;
            }
        }
    }
}

void sched_block(scheduler_t *sched, uint32_t task_id) {
    task_t *t = sched_get_task(sched, task_id);
    if (t) t->state = TASK_BLOCKED;
}

void sched_unblock(scheduler_t *sched, uint32_t task_id) {
    task_t *t = sched_get_task(sched, task_id);
    if (t && t->state == TASK_BLOCKED) t->state = TASK_READY;
}

void sched_sleep(scheduler_t *sched, uint32_t task_id, uint32_t ms) {
    task_t *t = sched_get_task(sched, task_id);
    if (t) {
        t->state = TASK_SLEEPING;
        /* 64-bit like ticks: the old uint32_t cast wrapped after 2^32 ticks,
         * and a wrapped deadline compares as already passed (instant wake). */
        t->sleep_until = sched->ticks + ms / 10u;
    }
}

void sched_terminate(scheduler_t *sched, uint32_t task_id, int32_t exit_code) {
    task_t *t = sched_get_task(sched, task_id);
    if (t && t->state != TASK_TERMINATED) {
        t->state = TASK_TERMINATED;
        t->exit_code = exit_code;
        sched->num_tasks--;
        /* current_task is a slot index, not a task id */
        if (&sched->tasks[sched->current_task] == t) sched_switch(sched);
    }
}

task_t *sched_current(scheduler_t *sched) {
    if (sched->num_tasks == 0) return 0;
    return &sched->tasks[sched->current_task];
}

task_t *sched_get_task(scheduler_t *sched, uint32_t task_id) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (sched->tasks[i].id == task_id && sched->tasks[i].state != TASK_UNUSED)
            return &sched->tasks[i];
    }
    return 0;
}

/* Append a decimal number to buf at position *pos (no NUL). */
static void list_put_dec(char *buf, uint32_t *pos, uint64_t val) {
    char tmp[20];
    int ti = 0;
    if (val == 0) tmp[ti++] = '0';
    while (val > 0) { tmp[ti++] = (char)('0' + (val % 10)); val /= 10; }
    while (ti > 0) buf[(*pos)++] = tmp[--ti];
}

static const char *task_state_name(task_state_t st) {
    switch (st) {
        case TASK_READY:      return "READY   ";
        case TASK_RUNNING:    return "RUNNING ";
        case TASK_BLOCKED:    return "BLOCKED ";
        case TASK_SLEEPING:   return "SLEEPING";
        case TASK_TERMINATED: return "TERM    ";
        default:              return "UNKNOWN ";
    }
}

void sched_list_tasks(scheduler_t *sched, void (*print)(void *ctx, const char *s), void *ctx) {
    if (!sched || !print) return;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        const task_t *t = &sched->tasks[i];
        if (t->state == TASK_UNUSED) continue;

        char row[96];
        uint32_t pos = 0;
        list_put_dec(row, &pos, t->id);
        while (pos < 5) row[pos++] = ' ';
        const char *st = task_state_name(t->state);
        for (uint32_t j = 0; st[j]; j++) row[pos++] = st[j];
        row[pos++] = ' '; row[pos++] = ' ';
        for (uint32_t j = 0; t->name[j] && j < TASK_NAME_LEN; j++)
            row[pos++] = t->name[j];
        while (pos < 34) row[pos++] = ' ';
        row[pos++] = 'p'; row[pos++] = 'r'; row[pos++] = 'i'; row[pos++] = 'o';
        row[pos++] = '=';
        list_put_dec(row, &pos, t->priority);
        row[pos++] = '\n';
        row[pos] = '\0';
        print(ctx, row);
    }
}
