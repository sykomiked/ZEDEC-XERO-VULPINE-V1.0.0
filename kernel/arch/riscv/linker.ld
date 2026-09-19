/* linker.ld — SHARED RISC-V linker script for VOVINA SHAKINA (rv64 AND rv32)
 *
 * Used by BOTH build_system/Makefile.riscv (rv64, lp64d, boot.s) and
 * build_system/Makefile.riscv32 (rv32, ilp32d, boot_rv32.s). It is deliberately
 * XLEN-agnostic: no .quad, no >4 GiB address, no 8-byte-only assumption.
 *
 * Target: qemu-system-riscv64|riscv32 -M virt
 * Load address 0x80200000 — this is NOT a taste decision. QEMU's virt machine
 * puts DRAM at 0x80000000 and the SBI firmware (OpenSBI) claims the first 2 MiB
 * of it, so 0x80000000 + 0x200000 is the first address a supervisor payload may
 * own.
 *
 * _start MUST BE AT 0x80200000 EXACTLY — DO NOT rely on the ELF entry being read.
 * This script used to say "OpenSBI reads the real entry from the ELF header, so
 * the +0x24 offset the build-id note introduces is carried for us." That is TRUE
 * on QEMU 6.2 and FALSE on QEMU 11: MEASURED 2026-08-11, QEMU 11.0.2 transfers
 * control to the LOAD BASE (0x80200000), not to the ELF entry. With an ALLOCATED
 * .note.gnu.build-id sitting at the base, the hart executed 0x24 bytes of note
 * DATA as instructions and died silently right after the OpenSBI banner — no
 * output, no trap message. Proof: patching `j _start` over the note at 0x80200000
 * made the identical kernel reach [BOOT_OK] on QEMU 11.
 *
 * So the note is DISCARDED below and an ASSERT pins _start to the base. Both
 * loaders then agree, because entry == base and there is nothing to disagree
 * about. Do not "clean up" the /DISCARD/ rule: it is load-bearing, and the
 * failure it prevents is silent on exactly the newest toolchain a reader will try.
 *
 * SMALL-DATA SECTIONS ARE NOT OPTIONAL HERE. GCC emits .sdata/.sbss/.srodata for
 * both RISC-V ABIs. If this script does not name them, ld ORPHAN-PLACES .sbss
 * after __bss_end, and the clear_bss loop in boot.s / boot_rv32.s — which walks
 * only [__bss_start, __bss_end) — never zeroes them. Measured before this fix:
 * .sbss landed at 0x89e2b000 while __bss_end was 0x89e1a690, stranding 39 live
 * globals including riscv_timer.c's timer_cb FUNCTION POINTER. QEMU hid it by
 * handing over zeroed DRAM; a warm reset or real hardware would not, and the
 * failure mode is an indirect call through garbage at the first timer tick.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;

    /* Nothing may be ALLOCATED ahead of .text — see the header. An allocated
     * build-id note here is executed as instructions by any loader that jumps to
     * the load base instead of the ELF entry. Discard rather than relocate: the
     * note carries no runtime value for a freestanding S-mode payload. (Belt and
     * braces: the Makefiles also pass -Wl,--build-id=none, so the note normally
     * never exists; this rule catches a toolchain that emits it anyway.) */
    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.note.ABI-tag)
        *(.comment)
    }

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        /* MODULE DECLARATIONS (kernel/include/zxv_decl.h) — same explicit
         * placement as kernel/arch/arm64/linker.ld:70-74, and it must be here
         * for BOTH widths: this one script serves rv64 (lp64d) and rv32
         * (ilp32d). The section holds POINTERS, not records, so the stride is
         * sizeof(void *) by construction — 8 on rv64, 4 on rv32 — and
         * zxv_decl_count() divides by exactly that. Nothing here depends on
         * XLEN, which is why ALIGN(8) at the start is safe on rv32 too: the
         * inputs are 4-aligned and pack contiguously after it.
         *
         * PLACED BY HAND, NOT LEFT AN ORPHAN. This script's own header records
         * what orphan placement did to .sbss here (stranded past __bss_end,
         * never zeroed, 39 live globals including a function pointer), and the
         * arm64 script records an orphan landing over .text and QEMU refusing
         * the kernel. Two independent bites of the same bug; do not take a
         * third by relying on ld's heuristics.
         *
         * ASSIGNED, not PROVIDEd — the C side declares these markers WEAK so an
         * arch that never placed the section still links, and PROVIDE only
         * defines a symbol when something references it, which is exactly the
         * question a weak undefined reference cannot answer reliably.
         *
         * KEEP() is inert without --gc-sections (this link does not pass it)
         * and is written anyway, so adding it later cannot silently empty the
         * graph. */
        . = ALIGN(8);
        __zxv_decl_start = .;
        KEEP(*(.rodata.zxv_decl))
        __zxv_decl_end = .;

        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    .bss : {
        /* Align both ends to 16: rv64's clear loop stores 8 bytes at a time and
         * rv32's stores 4, so the widest stride must divide the range exactly or
         * the loop overruns past __bss_end / stops short of it. */
        . = ALIGN(16);
        __bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.dynsbss)
        *(.scommon)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16);
    . = . + 0x10000;
    __stack_top = .;

    . = ALIGN(4096);
    __kernel_end = .;

    /* Loud beats silent: a regression here is a wild store loop, not a warning. */
    ASSERT(__bss_end >= __bss_start, "riscv linker.ld: bss range inverted")
    ASSERT(__stack_top > __bss_end,  "riscv linker.ld: stack overlaps bss")
    ASSERT((__bss_start & 15) == 0,  "riscv linker.ld: __bss_start not 16-aligned")
    ASSERT((__bss_end   & 15) == 0,  "riscv linker.ld: __bss_end not 16-aligned")
}
