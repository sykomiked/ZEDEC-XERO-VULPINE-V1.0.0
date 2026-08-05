/* boot.s — ARM entry point for VOVINA SHAKINA
 * QEMU virt machine loads the kernel at 0x40000000 (DRAM_BASE).
 * We set up the stack and call kernel_main.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
.section .text.boot
.global _start
_start:
    /* Set up stack pointer (1MB into RAM) */
    ldr sp, =_stack_top

    /* Clear BSS */
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
clear_bss:
    cmp r0, r1
    bge bss_done
    str r2, [r0], #4
    b clear_bss
bss_done:

    /* Call kernel_main(0, 0) — no multiboot info on ARM */
    mov r0, #0
    mov r1, #0
    bl kernel_main

    /* If kernel_main returns, halt forever */
hang:
    wfi
    b hang

/* ---- Exception vector table ---- */
.section .vectors
.global vector_table
vector_table:
    b exception_undef      /* 0: Undefined instruction */
    b exception_swi        /* 1: Software interrupt (SVC) */
    b exception_prefetch   /* 2: Prefetch abort */
    b exception_data       /* 3: Data abort */
    b exception_unused     /* 4: Unused */
    b exception_irq        /* 5: IRQ */
    b exception_fiq        /* 6: FIQ */

/* ---- Exception handlers ---- */
.section .text
exception_undef:
    sub sp, sp, #72
    stmia sp, {r0-r12}
    add r0, sp, #52
    str lr, [r0], #4
    str pc, [r0], #4
    mrs r1, spsr
    str r1, [r0], #4
    mov r0, sp
    mov r1, #0
    bl exception_handler
    ldmia sp, {r0-r12}
    add sp, sp, #72
    subs pc, lr, #0

exception_swi:
    sub sp, sp, #72
    stmia sp, {r0-r12}
    add r0, sp, #52
    str lr, [r0], #4
    str pc, [r0], #4
    mrs r1, spsr
    str r1, [r0], #4
    mov r0, sp
    mov r1, #1
    bl exception_handler
    ldmia sp, {r0-r12}
    add sp, sp, #72
    subs pc, lr, #0

exception_prefetch:
    sub sp, sp, #72
    stmia sp, {r0-r12}
    add r0, sp, #52
    str lr, [r0], #4
    str pc, [r0], #4
    mrs r1, spsr
    str r1, [r0], #4
    mov r0, sp
    mov r1, #2
    bl exception_handler
    ldmia sp, {r0-r12}
    add sp, sp, #72
    subs pc, lr, #4

exception_data:
    sub sp, sp, #72
    stmia sp, {r0-r12}
    add r0, sp, #52
    str lr, [r0], #4
    str pc, [r0], #4
    mrs r1, spsr
    str r1, [r0], #4
    mov r0, sp
    mov r1, #3
    mrc p15, 0, r2, c5, c0, 0  /* Read DFSR */
    bl exception_handler
    ldmia sp, {r0-r12}
    add sp, sp, #72
    subs pc, lr, #8

exception_unused:
    b exception_unused

exception_irq:
    sub sp, sp, #72
    stmia sp, {r0-r12}
    add r0, sp, #52
    str lr, [r0], #4
    str pc, [r0], #4
    mrs r1, spsr
    str r1, [r0], #4
    mov r0, sp
    mov r1, #5
    bl irq_handler_c
    ldmia sp, {r0-r12}
    add sp, sp, #72
    subs pc, lr, #0

exception_fiq:
    b exception_fiq

/* ---- Stack space ---- */
.section .bss
.align 12
_stack_bottom:
    .skip 1048576          /* 1MB stack */
_stack_top:
