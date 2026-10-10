/* boot.s — RISC-V 64-bit entry point for VOVINA SHAKINA
 *
 * Booted by OpenSBI in S-mode (qemu-system-riscv64 -M virt, default -bios).
 * OpenSBI passes the hart id in a0 and the DTB pointer in a1. We use the
 * SUPERVISOR CSRs (stvec/scause), NOT the machine CSRs (mhartid/mtvec/mcause),
 * which are illegal in S-mode.
 *
 * The rv32 sibling is boot_rv32.s; keep the two in step.
 *
 * Target: qemu-system-riscv64 -M virt
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: Apache-2.0
 */

.section .text.boot
.global _start
_start:
    /* Hart id is in a0 (from OpenSBI); if not hart 0, park */
    bnez a0, park

    /* Preserve what the FIRMWARE told us about this machine across the BSS
     * clear (a0 = hart id, a1 = device-tree blob). s0/s1 are callee-saved and
     * nothing has run yet, so they survive the loop below. */
    mv s0, a0
    mv s1, a1

    /* Set up stack */
    la sp, __stack_top

    /* Clear BSS.
     * linker.ld 16-aligns both ends, so this 8-byte stride divides the range
     * exactly and cannot overrun __bss_end. */
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bgeu t0, t1, bss_done
    sd zero, 0(t0)
    addi t0, t0, 8
    j clear_bss
bss_done:

    /* Publish the firmware handoff AFTER the BSS clear, or it would be erased. */
    la t0, riscv_boot_hart
    sd s0, 0(t0)
    la t0, riscv_dtb_addr
    sd s1, 0(t0)

    /* Enable the FPU before any C runs. The lp64d ABI makes hardware
     * double-precision mandatory even for struct copies; if sstatus.FS is Off
     * the first fld raises Illegal Instruction. csrs can only SET bit 13, so FS
     * ends Initial(01) or Dirty(11), never Off — and it is a nop on a core
     * without F/D. Do not inherit this from the firmware. */
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
    /* Save the caller-saved registers.
     * t3-t6 (x28-x31) are caller-saved too and CAN be live in the interrupted
     * code; riscv_timer_handler tail-calls an arbitrary callback, so leaving
     * them out silently corrupts whatever we interrupted. */
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
    sd t3, 112(sp)
    sd t4, 120(sp)
    sd t5, 128(sp)
    sd t6, 136(sp)

    /* Read scause (S-mode) */
    csrr a0, scause
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
    ld t3, 112(sp)
    ld t4, 120(sp)
    ld t5, 128(sp)
    ld t6, 136(sp)
    addi sp, sp, 256

    /* Return from trap */
    sret
