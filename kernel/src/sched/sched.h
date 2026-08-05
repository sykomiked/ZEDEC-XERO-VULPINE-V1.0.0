/* sched.h — Process Scheduler: Task structures, context switching
 * M5-axiomatic aware: each task carries phase/omega metadata.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include "../idt/idt.h"

#define MAX_TASKS       64
#define TASK_NAME_LEN   32
#define KERNEL_STACK_SIZE 8192

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_SLEEPING = 4,
    TASK_TERMINATED = 5
} task_state_t;

typedef enum {
    TASK_KERNEL = 0,
    TASK_USER = 1,
    TASK_M5_AXIOMATIC = 2
} task_type_t;

typedef struct task {
    uint32_t id;
    char name[TASK_NAME_LEN];
    task_state_t state;
    task_type_t type;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t cr3;
    uint32_t stack[KERNEL_STACK_SIZE / 4];
    uint32_t sleep_until;
    int32_t exit_code;
    uint32_t parent_id;

    /* M5 Axiomatic metadata */
    uint32_t omega;
    uint32_t phase;
    uint32_t collapse_count;
    uint32_t cpu_time_ms;
    uint32_t priority;
} task_t;

typedef struct scheduler {
    task_t tasks[MAX_TASKS];
    uint32_t num_tasks;
    uint32_t current_task;
    uint32_t next_pid;
    uint64_t ticks;
    bool initialized;
} scheduler_t;

void sched_init(scheduler_t *sched);
int32_t sched_create_task(scheduler_t *sched, const char *name, task_type_t type,
                           void (*entry_point)(void), uint32_t priority);
void sched_yield(scheduler_t *sched);
void sched_switch(scheduler_t *sched);
void sched_block(scheduler_t *sched, uint32_t task_id);
void sched_unblock(scheduler_t *sched, uint32_t task_id);
void sched_sleep(scheduler_t *sched, uint32_t task_id, uint32_t ms);
void sched_terminate(scheduler_t *sched, uint32_t task_id, int32_t exit_code);
void sched_tick(scheduler_t *sched);
task_t *sched_current(scheduler_t *sched);
task_t *sched_get_task(scheduler_t *sched, uint32_t task_id);
void sched_list_tasks(scheduler_t *sched, void (*print)(void *ctx, const char *s), void *ctx);

#endif
