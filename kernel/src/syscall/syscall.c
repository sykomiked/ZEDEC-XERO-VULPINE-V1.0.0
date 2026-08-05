/* syscall.c — System Call Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "syscall.h"
#include "../timer/timer.h"

static syscall_table_t g_syscalls;

void syscall_init(syscall_table_t *table) {
    table->num_registered = 0;
    for (uint32_t i = 0; i < MAX_SYSCALLS; i++)
        table->handlers[i] = 0;
    g_syscalls = *table;
}

void syscall_register(syscall_table_t *table, uint32_t num, syscall_handler_t handler) {
    if (num < MAX_SYSCALLS) {
        table->handlers[num] = handler;
        if (num + 1 > table->num_registered)
            table->num_registered = num + 1;
    }
}

int32_t syscall_dispatch(registers_t *regs) {
    uint32_t num = regs->eax;
    if (num >= MAX_SYSCALLS || !g_syscalls.handlers[num])
        return -1;
    return g_syscalls.handlers[num](regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
}

void syscall_handler(registers_t *regs) {
    int32_t ret = syscall_dispatch(regs);
    regs->eax = (uint32_t)ret;
}

/* Stub implementations */
int32_t sys_write(int32_t fd, const void *buf, uint32_t len) {
    (void)fd; (void)buf; (void)len;
    return (int32_t)len;
}

int32_t sys_read(int32_t fd, void *buf, uint32_t len) {
    (void)fd; (void)buf; (void)len;
    return 0;
}

int32_t sys_exit(int32_t code) { return code; }
int32_t sys_getpid(void) { return 1; }
int32_t sys_sleep(uint32_t ms) { (void)ms; return 0; }
int32_t sys_yield(void) { return 0; }
int32_t sys_get_time(void) { return (int32_t)timer_get_ticks(); }
int32_t sys_get_meminfo(uint32_t *total, uint32_t *used) { *total = 0; *used = 0; return 0; }
