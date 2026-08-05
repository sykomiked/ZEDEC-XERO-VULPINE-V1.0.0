/* boot.s — RISC-V 64-bit entry point for VOVINA SHAKINA
 *
 * QEMU virt machine loads the kernel at 0x80200000 in M-mode.
 * We set up the stack, initialize BSS, configure mtvec, and
 * call kernel_main_riscv.
 *
 * Target: qemu-system-riscv64 -M virt -cpu rv64 -m 256M
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

.section .text.boot
.global _start
_start:
    /* Read hart ID; if not hart 0, park */
    csrr t0, mhartid
    bnez t0, park

    /* Set up stack */
    la sp, __stack_top

    /* Clear BSS */
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bgeu t0, t1, bss_done
    sd zero, 0(t0)
    addi t0, t0, 8
    j clear_bss
bss_done:

    /* Set up trap vector (direct mode) */
    la t0, trap_vector
    csrw mtvec, t0

    /* Call kernel_main_riscv */
    call kernel_main_riscv

park:
    wfi
    j park

/* Trap handler */
.align 2
trap_vector:
    /* Save registers */
    addi sp, sp, -256
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    sd t0, 24(sp)
    sd t1, 32(sp)
    sd t2, 40(sp)
    sd a0, 48(sp)
    sd a1, 56(sp)
    sd a2, 64(sp)
    sd a3, 72(sp)
    sd a4, 80(sp)
    sd a5, 88(sp)
    sd a6, 96(sp)
    sd a7, 104(sp)

    /* Read mcause */
    csrr a0, mcause
    /* Call trap handler C function */
    call riscv_trap_handler

    /* Restore registers */
    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    ld t0, 24(sp)
    ld t1, 32(sp)
    ld t2, 40(sp)
    ld a0, 48(sp)
    ld a1, 56(sp)
    ld a2, 64(sp)
    ld a3, 72(sp)
    ld a4, 80(sp)
    ld a5, 88(sp)
    ld a6, 96(sp)
    ld a7, 104(sp)
    addi sp, sp, 256

    /* Return from trap */
    mret
