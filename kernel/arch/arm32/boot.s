/*
 * ARM32 boot entry point for ZEDEC XERO pqOS
 * Entry: 0x40000000 (qemu-system-arm -M virt RAM base)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: Apache-2.0
 */
.arch armv7-a
.fpu vfpv3-d16
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

    /* Enable the VFP/NEON unit before any C runs. The kernel is built
     * hard-float (-mfpu=vfpv3-d16 -mfloat-abi=hard), so the first
     * floating-point instruction would otherwise trap as Undefined
     * (ESR EC=0x7) with no vector table installed → infinite fault loop.
     * 1) CPACR: grant CP10/CP11 (VFP) full access at PL1.
     * 2) FPEXC.EN (bit 30): enable VFP data-processing. */
    mrc p15, 0, r0, c1, c0, 2      /* read CPACR */
    orr r0, r0, #(0xf << 20)       /* cp10 + cp11 = full access */
    mcr p15, 0, r0, c1, c0, 2      /* write CPACR */
    isb
    mov r0, #(1 << 30)             /* FPEXC.EN */
    vmsr fpexc, r0

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
