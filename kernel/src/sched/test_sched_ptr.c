/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_sched_ptr.c -- the scheduler keeps whole pointers in its task context.
 *
 * task_t used to hold esp/ebp/eip/cr3 and the prepared stack as uint32_t, so on
 * a 64-bit image every address above 4 GB lost its top half. This stores an
 * entry address above 4 GB through sched_create_task() and reads it back from
 * the task record and from the prepared stack frame, and checks that esp/ebp
 * point inside the task's own stack.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sched.h"

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

static void real_entry(void) {}

static scheduler_t g_sched;

static void check_task(const task_t *t, uintptr_t want, const char *what)
{
    uintptr_t lo = (uintptr_t) &t->stack[0];
    uintptr_t hi = (uintptr_t) &t->stack[KERNEL_STACK_WORDS];
    char msg[128];
    snprintf(msg, sizeof msg, "%s: eip holds the whole entry address", what);
    CHECK(t->eip == want, msg);
    snprintf(msg, sizeof msg, "%s: prepared frame slot 0 holds the whole entry address", what);
    CHECK(t->stack[KERNEL_STACK_WORDS - 3] == want, msg);
    snprintf(msg, sizeof msg, "%s: esp points inside the task's own stack", what);
    CHECK(t->esp >= lo && t->esp < hi, msg);
    snprintf(msg, sizeof msg, "%s: esp addresses the prepared frame", what);
    CHECK(t->esp == (uintptr_t) &t->stack[KERNEL_STACK_WORDS - 3], msg);
    snprintf(msg, sizeof msg, "%s: ebp == esp at creation", what);
    CHECK(t->ebp == t->esp, msg);
    snprintf(msg, sizeof msg, "%s: flags word is 0x202", what);
    CHECK(t->stack[KERNEL_STACK_WORDS - 1] == 0x202u, msg);
}

int main(void)
{
    printf("=== scheduler pointer width ===\n");
    CHECK(sizeof(((task_t *) 0)->esp) == sizeof(void *), "esp is pointer-sized");
    CHECK(sizeof(((task_t *) 0)->eip) == sizeof(void *), "eip is pointer-sized");
    CHECK(sizeof(((task_t *) 0)->cr3) == sizeof(void *), "cr3 is pointer-sized");
    CHECK(sizeof(((task_t *) 0)->stack) == KERNEL_STACK_SIZE,
          "stack is still KERNEL_STACK_SIZE bytes");

    sched_init(&g_sched);

    /* A real function pointer, wherever the host put it. */
    int32_t id = sched_create_task(&g_sched, "real", TASK_KERNEL, real_entry, 1);
    CHECK(id > 0, "create task with a real entry");
    task_t *t = sched_get_task(&g_sched, (uint32_t) id);
    CHECK(t != 0, "task found by id");
    if (t) check_task(t, (uintptr_t) real_entry, "real entry");

#if UINTPTR_MAX > 0xFFFFFFFFu
    /* An address above 4 GB. It is never called: the scheduler only records it. */
    const uintptr_t high = (uintptr_t) 0x123456789ULL;
    void (*high_entry)(void) = (void (*)(void)) high;
    id = sched_create_task(&g_sched, "high", TASK_KERNEL, high_entry, 2);
    CHECK(id > 0, "create task with an entry above 4 GB");
    t = sched_get_task(&g_sched, (uint32_t) id);
    CHECK(t != 0, "high task found by id");
    if (t) {
        check_task(t, high, "entry above 4 GB");
        CHECK((t->eip >> 32) == 0x1u, "top half of the entry address survived");
        CHECK((uint32_t) t->eip == 0x23456789u, "bottom half of the entry address survived");
    }
    /* A stack-resident pointer above 4 GB too: every stack slot is a full word. */
    if (t) {
        t->stack[0] = (uintptr_t) 0xFEDCBA9876543210ULL;
        CHECK(t->stack[0] == (uintptr_t) 0xFEDCBA9876543210ULL, "stack word keeps all 64 bits");
    }
#else
    printf("  (32-bit host: the above-4GB case does not apply)\n");
#endif

    /* Switching between the tasks must not disturb the recorded context. */
    task_t *a = &g_sched.tasks[0];
    uintptr_t a_eip = a->eip, a_esp = a->esp;
    sched_switch(&g_sched);
    sched_switch(&g_sched);
    CHECK(a->eip == a_eip && a->esp == a_esp, "context fields unchanged across sched_switch");

    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
