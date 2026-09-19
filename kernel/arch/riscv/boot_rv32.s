/* boot_rv32.s — RISC-V 32-bit entry point for VOVINA SHAKINA
 *
 * Same structure as boot.s (rv64) but uses 32-bit sw/lw instead of sd/ld.
 * Booted by OpenSBI in S-mode (qemu-system-riscv32 -M virt):
 * OpenSBI passes the hart id in a0 and the DTB pointer in a1, and we run in
 * SUPERVISOR mode — so we use the S-mode CSRs (stvec/scause/sret), NOT the
 * machine CSRs (mhartid/mtvec/mcause/mret), which trap illegally in S-mode.
 *
 * NOTE ON FIRMWARE: qemu-system-riscv32 -M virt loads an SBI firmware BEFORE
 * this file exists in memory. If the host has no rv32 OpenSBI, QEMU aborts with
 * 97 bytes of "Unable to load the RISC-V firmware ..." and not one instruction
 * here ever runs. See build_system/riscv32_sbi.sh, which detects/builds it.
 *
 * Target: qemu-system-riscv32 -M virt
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

.section .text.boot
.global _start
_start:
    /* Hart id is in a0 (from OpenSBI); if not hart 0, park */
    bnez a0, park

    /* Preserve what the FIRMWARE told us about this machine across the BSS
     * clear (a0 = hart id, a1 = device-tree blob). s0/s1 are callee-saved and
     * nothing has run yet, so they survive the loop below. This is the handle
     * the kernel needs to ASK the hardware what it is instead of assuming. */
    mv s0, a0
    mv s1, a1

    /* Set up stack */
    la sp, __stack_top

    /* Clear BSS (32-bit stores).
     * linker.ld 16-aligns both ends, so this stride divides the range exactly. */
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bgeu t0, t1, bss_done
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss
bss_done:

    /* Publish the firmware handoff AFTER the BSS clear, or it would be erased. */
    la t0, riscv_boot_hart
    sw s0, 0(t0)
    la t0, riscv_dtb_addr
    sw s1, 0(t0)

    /* Enable the FPU before any C runs.
     * -mabi=ilp32d makes hardware double-precision MANDATORY: the image holds
     * 2331 FP instructions, and plain struct copies compile to fld/fsd. If
     * sstatus.FS is Off (00) the first one raises Illegal Instruction. Today it
     * only works because OpenSBI happens to leave FS enabled — that is an
     * inherited assumption, not a guarantee, so state it ourselves.
     * csrs can only SET bit 13, so FS ends as Initial(01) or Dirty(11), never
     * Off. On a core without F/D the field is read-only zero and this is a nop. */
    li   t0, 0x2000              /* sstatus.FS = Initial (bits 14:13 = 01) */
    csrs sstatus, t0

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
    /* Save the caller-saved registers (32-bit).
     * t3-t6 (x28-x31) are caller-saved too and CAN be live in the interrupted
     * code; riscv_timer_handler tail-calls an arbitrary callback, so leaving
     * them out silently corrupts whatever we interrupted. */
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
    sw t3, 56(sp)
    sw t4, 60(sp)
    sw t5, 64(sp)
    sw t6, 68(sp)

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
    lw t3, 56(sp)
    lw t4, 60(sp)
    lw t5, 64(sp)
    lw t6, 68(sp)
    addi sp, sp, 128

    /* Return from trap (S-mode) */
    sret
