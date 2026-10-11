/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* startup.c — the smallest Cortex-M start-up for the QEMU target test:
 * a vector table, copy .data, zero .bss, run constructors, call main,
 * report through semihosting (newlib rdimon). Test only. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t __etext, __data_start__, __data_end__, __bss_start__, __bss_end__, __stack_top;
extern int main(void);
extern void __libc_init_array(void);
extern void initialise_monitor_handles(void);

void reset_handler(void);
static void fault_handler(void)
{
    exit(99);
}

__attribute__((section(".vectors"), used)) static void (*const vectors[16])(void) = {
    (void (*)(void)) & __stack_top, reset_handler, fault_handler, fault_handler, fault_handler,
    fault_handler, fault_handler, 0, 0, 0, 0, fault_handler, fault_handler, 0, fault_handler,
    fault_handler};

void reset_handler(void)
{
    memcpy(&__data_start__, &__etext, (size_t) ((char *) &__data_end__ - (char *) &__data_start__));
    memset(&__bss_start__, 0, (size_t) ((char *) &__bss_end__ - (char *) &__bss_start__));
    initialise_monitor_handles();
    __libc_init_array();
    exit(main());
}

/* -nostartfiles: no crti/crtn, so __libc_init_array's hooks live here. */
void _init(void);
void _fini(void);
void _init(void)
{
}
void _fini(void)
{
}
