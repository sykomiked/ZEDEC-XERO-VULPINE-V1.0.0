/*
 * ARM32 boot entry point for ZEDEC XERO pqOS
 * Entry: 0x10000 (versatilepb)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 */
.section .text.boot
.global _start

_start:
    /* Set up stack */
    ldr sp, =_stack_top

    /* Clear BSS */
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
clear_bss:
    cmp r0, r1
    strlo r2, [r0], #4
    blo clear_bss

    /* Disable interrupts initially */
    cpsid if

    /* Call kernel_main_arm32 */
    bl kernel_main_arm32

    /* If we return, hang */
hang:
    wfi
    b hang

.section .text
.align 2
