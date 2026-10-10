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

    /* Install the exception vector table (VBAR needs 32-byte alignment and
     * SCTLR.V=0, the reset value on -M virt). Without it an abort or an
     * undefined instruction jumps to address 0 (flash on virt) and the boot
     * just stops printing; with it the fault is reported as [FAULT] on the
     * serial log, which is what CI greps for. */
    ldr r0, =arm32_vectors
    mcr p15, 0, r0, c12, c0, 0
    isb

    /* Call kernel_main_arm32 */
    bl kernel_main_arm32

    /* If we return, hang */
hang:
    wfi
    b hang

/* Exception vectors. Every entry is fatal: the kernel never enables IRQ/FIQ
 * and makes no SVC calls, so reaching any of them means something faulted.
 * Each stub passes its vector number (r0) and the exception-mode LR (r1) to
 * arm32_fault_report() (kernel_main_arm32.c), which prints [FAULT]. The
 * banked SP of the exception mode is never set up, so the common path loads
 * a dedicated fault stack before calling C, and parks if C returns. */
.align 5
.global arm32_vectors
arm32_vectors:
    b .                     /* 0 reset (not taken through VBAR) */
    b vec_und               /* 1 undefined instruction */
    b vec_svc               /* 2 supervisor call */
    b vec_pabt              /* 3 prefetch abort */
    b vec_dabt              /* 4 data abort */
    b .                     /* 5 reserved */
    b vec_irq               /* 6 IRQ */
    b vec_fiq               /* 7 FIQ */

vec_und:
    mov r0, #1
    b vec_common
vec_svc:
    mov r0, #2
    b vec_common
vec_pabt:
    mov r0, #3
    b vec_common
vec_dabt:
    mov r0, #4
    b vec_common
vec_irq:
    mov r0, #6
    b vec_common
vec_fiq:
    mov r0, #7
vec_common:
    mov r1, lr
    ldr sp, =arm32_fault_stack_top
    bl arm32_fault_report
fault_hang:
    wfi
    b fault_hang

.section .bss
.align 3
arm32_fault_stack:
    .space 4096
arm32_fault_stack_top:

.section .text
.align 2
