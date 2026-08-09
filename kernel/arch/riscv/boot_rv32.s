/* boot_rv32.s — RISC-V 32-bit entry point for VOVINA SHAKINA
 *
 * Same structure as boot.s (rv64) but uses 32-bit sw/lw instead of sd/ld.
 * Booted by OpenSBI in S-mode (qemu-system-riscv32 -M virt, default -bios):
 * OpenSBI passes the hart id in a0 and the DTB pointer in a1, and we run in
 * SUPERVISOR mode — so we use the S-mode CSRs (stvec/scause/sret), NOT the
 * machine CSRs (mhartid/mtvec/mcause/mret), which trap illegally in S-mode.
 *
 * Target: qemu-system-riscv32 -M virt -m 256M
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

.section .text.boot
.global _start
_start:
    /* Hart id is in a0 (from OpenSBI); if not hart 0, park */
    bnez a0, park

    /* Set up stack */
    la sp, __stack_top

    /* Clear BSS (32-bit stores) */
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bgeu t0, t1, bss_done
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss
bss_done:

    /* Set up the S-mode trap vector (direct mode) */
    la t0, trap_vector
    csrw stvec, t0

    /* Call kernel_main_riscv */
    call kernel_main_riscv

park:
    wfi
    j park

/* Trap handler */
.align 2
trap_vector:
    /* Save registers (32-bit) */
    addi sp, sp, -128
    sw ra, 0(sp)
    sw gp, 4(sp)
    sw tp, 8(sp)
    sw t0, 12(sp)
    sw t1, 16(sp)
    sw t2, 20(sp)
    sw a0, 24(sp)
    sw a1, 28(sp)
    sw a2, 32(sp)
    sw a3, 36(sp)
    sw a4, 40(sp)
    sw a5, 44(sp)
    sw a6, 48(sp)
    sw a7, 52(sp)

    /* Read scause (S-mode) */
    csrr a0, scause
    /* Call trap handler C function */
    call riscv_trap_handler

    /* Restore registers (32-bit) */
    lw ra, 0(sp)
    lw gp, 4(sp)
    lw tp, 8(sp)
    lw t0, 12(sp)
    lw t1, 16(sp)
    lw t2, 20(sp)
    lw a0, 24(sp)
    lw a1, 28(sp)
    lw a2, 32(sp)
    lw a3, 36(sp)
    lw a4, 40(sp)
    lw a5, 44(sp)
    lw a6, 48(sp)
    lw a7, 52(sp)
    addi sp, sp, 128

    /* Return from trap (S-mode) */
    sret
