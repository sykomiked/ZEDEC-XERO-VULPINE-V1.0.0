/* syscall.h — System Call Interface
 * int 0x80 dispatch with eax=syscall number, ebx/ecx/edx/esi/edi=args.
 * M5-axiomatic syscalls carry phase/omega metadata.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "../idt/idt.h"

#define SYSCALL_EXIT       0
#define SYSCALL_WRITE      1
#define SYSCALL_READ       2
#define SYSCALL_OPEN       3
#define SYSCALL_CLOSE      4
#define SYSCALL_BRK        5
#define SYSCALL_GETPID     6
#define SYSCALL_FORK       7
#define SYSCALL_EXEC       8
#define SYSCALL_SLEEP      9
#define SYSCALL_YIELD      10
#define SYSCALL_MKDIR      11
#define SYSCALL_LISTDIR    12
#define SYSCALL_SOCKET     13
#define SYSCALL_CONNECT    14
#define SYSCALL_SEND       15
#define SYSCALL_RECV       16
#define SYSCALL_BIND       17
#define SYSCALL_LISTEN     18
#define SYSCALL_ACCEPT     19
#define SYSCALL_GUI_CREATE_WIN  20
#define SYSCALL_GUI_ADD_WIDGET  21
#define SYSCALL_GUI_RENDER      22
#define SYSCALL_M5_EXECUTE      23
#define SYSCALL_M5_QUERY        24
#define SYSCALL_GET_TIME        25
#define SYSCALL_GET_MEMINFO     26

#define MAX_SYSCALLS 32

typedef int32_t (*syscall_handler_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3,
                                      uint32_t arg4, uint32_t arg5);

typedef struct syscall_table {
    syscall_handler_t handlers[MAX_SYSCALLS];
    uint32_t num_registered;
} syscall_table_t;

void syscall_init(syscall_table_t *table);
void syscall_register(syscall_table_t *table, uint32_t num, syscall_handler_t handler);
int32_t syscall_dispatch(registers_t *regs);
void syscall_handler(registers_t *regs);

/* Convenience wrappers */
int32_t sys_write(int32_t fd, const void *buf, uint32_t len);
int32_t sys_read(int32_t fd, void *buf, uint32_t len);
int32_t sys_exit(int32_t code);
int32_t sys_getpid(void);
int32_t sys_sleep(uint32_t ms);
int32_t sys_yield(void);
int32_t sys_get_time(void);
int32_t sys_get_meminfo(uint32_t *total, uint32_t *used);

#endif
