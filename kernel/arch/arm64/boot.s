/*
 * boot.s — AArch64 (ARM64) entry point for VOVINA SHAKINA
 * 
 * QEMU virt machine loads the kernel at EL2 (hypervisor level).
 * We drop to EL1 (kernel space), set up VBAR_EL1, configure the
 * stack, initialize BSS, and call kernel_main.
 *
 * Target: qemu-system-aarch64 -M virt -cpu cortex-a53 -m 256M
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

.section .text.boot
.global _start
_start:
    /* ---- Step 1: Drop from EL2 to EL1 ---- */
    /* Check current exception level */
    mrs x0, CurrentEL
    lsr x0, x0, #2          /* Extract EL bits */

    /* If at EL2, configure and drop to EL1 */
    cmp x0, #2
    b.ne 1f

    /* Configure HCR_EL2 for EL1 preparation */
    /* Initialize SCTLR_EL1 before dropping */
    movz x0, #0x0833
    movk x0, #0x30d5, lsl #16
    msr SCTLR_EL1, x0

    /* Set up HCR_EL2 — allow EL1 to run */
    movz x0, #0x8000, lsl #16  /* RW: EL1 is AArch64 */
    msr HCR_EL2, x0

    /* Configure CNTHCTL_EL2 — give EL1 access to physical timer */
    mov x0, #0x3            /* EL1PCTEN | EL1PCEN */
    msr CNTHCTL_EL2, x0

    /* Set up the virtual timer offset to 0 */
    mov x0, #0
    msr CNTVOFF_EL2, x0

    /* Set up SPSR_EL2 for the ERET to EL1 */
    mov x0, #0x3c5          /* EL1h, IRQ masked, AArch64 */
    msr SPSR_EL2, x0

    /* Set up the return address — our EL1 entry point */
    adr x0, el1_entry
    msr ELR_EL2, x0

    /* ERET — drop to EL1 */
    eret

1:
    /* Already at EL1 or lower — proceed directly */
    b el1_entry

/* ---- EL1 Entry Point ----
 * Reached at EL1 with the MMU OFF, whether QEMU -kernel loaded us here directly
 * or the EFI stub (BOOTAA64.EFI) copied us to 0x40080000 and branched MMU-off. */
el1_entry:
    /* ---- Step 2: Set up VBAR_EL1 (Vector Base Address) ---- */
    adr x0, vector_table
    msr VBAR_EL1, x0
    isb

    /* ---- Step 2b: Enable FP/SIMD access (CPACR_EL1.FPEN = 0b11) ----
     * Required before any C code runs: -O2 auto-vectorizes plain
     * integer loops (e.g. page-table zeroing) using NEON registers,
     * which trap with ESR EC=0x07 if FP/SIMD access isn't enabled. */
    mrs x0, CPACR_EL1
    orr x0, x0, #(3 << 20)
    msr CPACR_EL1, x0
    isb

    /* ---- Step 3: Set up stack pointer ---- */
    ldr x0, =_stack_top
    mov sp, x0

    /* ---- Step 4: Clear BSS ---- */
    ldr x0, =_bss_start
    ldr x1, =_bss_end
    mov x2, #0
clear_bss:
    cmp x0, x1
    b.ge bss_done
    str x2, [x0], #8
    b clear_bss
bss_done:

    /* ---- Step 5: Call kernel_main(0, 0) ---- */
    mov x0, #0              /* magic = 0 (no multiboot on ARM) */
    mov x1, #0              /* mbi = NULL */
    bl kernel_main

    /* If kernel_main returns, halt forever */
hang:
    wfi
    b hang

/* ---- AArch64 Exception Vector Table ---- */
/* Each entry is 128 bytes (0x80) apart. We use simple branches. */
.section .text
.align 11                   /* VBAR must be 2KB-aligned */
.global vector_table
vector_table:
    /* Current EL with SP_EL0 (0x000-0x1FF) */
    b sync_handler_curr_el0
    .align 7
    b irq_handler_curr_el0
    .align 7
    b fiq_handler_curr_el0
    .align 7
    b serror_handler_curr_el0
    .align 7

    /* Current EL with SP_ELx (0x200-0x3FF) */
    b sync_handler_curr_elx
    .align 7
    b irq_handler_curr_elx
    .align 7
    b fiq_handler_curr_elx
    .align 7
    b serror_handler_curr_elx
    .align 7

    /* Lower EL, AArch64 (0x400-0x5FF) */
    b sync_handler_lower_aarch64
    .align 7
    b irq_handler_lower_aarch64
    .align 7
    b fiq_handler_lower_aarch64
    .align 7
    b serror_handler_lower_aarch64
    .align 7

    /* Lower EL, AArch32 (0x600-0x7FF) */
    b sync_handler_lower_aarch32
    .align 7
    b irq_handler_lower_aarch32
    .align 7
    b fiq_handler_lower_aarch32
    .align 7
    b serror_handler_lower_aarch32
    .align 7

/* ---- Exception Handlers ---- */
/* Each handler saves registers, calls C handler, restores */

.macro SAVE_REGS
    sub sp, sp, #272        /* 31 regs × 8 + 16 for alignment */
    str x30,     [sp, #240]
    stp x28, x29, [sp, #224]
    stp x26, x27, [sp, #208]
    stp x24, x25, [sp, #192]
    stp x22, x23, [sp, #176]
    stp x20, x21, [sp, #160]
    stp x18, x19, [sp, #144]
    stp x16, x17, [sp, #128]
    stp x14, x15, [sp, #112]
    stp x12, x13, [sp, #96]
    stp x10, x11, [sp, #80]
    stp x8,  x9,  [sp, #64]
    stp x6,  x7,  [sp, #48]
    stp x4,  x5,  [sp, #32]
    stp x2,  x3,  [sp, #16]
    stp x0,  x1,  [sp]
.endm

.macro RESTORE_REGS
    ldp x0,  x1,  [sp]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]
    add sp, sp, #272
.endm

/* IRQ handler at current EL with SP_ELx — this is the main timer/UART IRQ path */
irq_handler_curr_elx:
    SAVE_REGS
    mov x0, sp
    mov x1, #5              /* IRQ exception type */
    bl irq_handler_c
    RESTORE_REGS
    eret

/* Synchronous exception at current EL with SP_ELx */
sync_handler_curr_elx:
    SAVE_REGS
    mov x0, sp
    mov x1, #0              /* Sync exception type */
    mrs x2, ESR_EL1         /* Get exception syndrome */
    bl exception_handler
    RESTORE_REGS
    eret

/* Stub handlers for other vectors */
sync_handler_curr_el0:
    b sync_handler_curr_el0
irq_handler_curr_el0:
    b irq_handler_curr_el0
fiq_handler_curr_el0:
    b fiq_handler_curr_el0
serror_handler_curr_el0:
    b serror_handler_curr_el0
fiq_handler_curr_elx:
    b fiq_handler_curr_elx
serror_handler_curr_elx:
    b serror_handler_curr_elx

/* ---- Lower EL (EL0) AArch64 exception handlers ---- */
/* These fire when an EL0 process triggers an exception.
 * They save the EL0 context, call the C dispatcher, and ERET back. */

/* SVC or fault from EL0 — dispatch to el0_sync_handler_c */
sync_handler_lower_aarch64:
    SAVE_REGS
    mov x0, sp              /* saved register frame (x0-x30) */
    mov x1, #0              /* sync exception type */
    mrs x2, ESR_EL1         /* exception syndrome */
    mrs x3, ELR_EL1         /* saved PC (for fault reporting) */
    mrs x4, SPSR_EL1        /* saved PSTATE */
    bl el0_sync_handler_c
    /* If the handler returns, it has set up ELR/SPSR for ERET
     * (either back to EL0 or to the next process). Restore and ERET. */
    RESTORE_REGS
    eret

/* Timer IRQ from EL0 — dispatch to el0_irq_handler_c */
irq_handler_lower_aarch64:
    SAVE_REGS
    mov x0, sp              /* saved register frame (x0-x30) */
    bl el0_irq_handler_c
    /* Handler has set up ELR/SPSR/TTBR0 for return to EL0 */
    RESTORE_REGS
    eret

fiq_handler_lower_aarch64:
    b fiq_handler_lower_aarch64
serror_handler_lower_aarch64:
    SAVE_REGS
    mov x0, sp
    mov x1, #3              /* serror type */
    mrs x2, ESR_EL1
    bl exception_handler    /* serror from EL0 is fatal — halt */
    RESTORE_REGS
    eret
sync_handler_lower_aarch32:
    b sync_handler_lower_aarch32
irq_handler_lower_aarch32:
    b irq_handler_lower_aarch32
fiq_handler_lower_aarch32:
    b fiq_handler_lower_aarch32
serror_handler_lower_aarch32:
    b serror_handler_lower_aarch32

/* ---- Stack space ---- */
.section .bss
.align 12
_stack_bottom:
    .skip 1048576           /* 1MB stack */
_stack_top:
