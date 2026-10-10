/*
 * ARM32 linker script for ZEDEC XERO pqOS
 * Target: qemu-system-arm -M virt (Cortex-A15)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: Apache-2.0
 */
ENTRY(_start)

/* The `virt` machine places RAM at 0x40000000; QEMU's -kernel loader honors the
 * ELF's physical address and enters at _start. 256M holds the full subsystem
 * set's static data (the emu/MegaROM/game_universe tables are large); the old
 * 128M overflowed .bss. Boot with qemu-system-arm -M virt -cpu cortex-a15 -m 512M. */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 256M
}

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    } > RAM

    .rodata : {
        /* MODULE DECLARATIONS (kernel/include/zxv_decl.h) — same explicit
         * placement as kernel/arch/arm64/linker.ld:70-74.
         *
         * Each ZXV_DECLARE emits one POINTER here (not a record), so the stride
         * is sizeof(void *) = 4 on this target and zxv_decl_count() divides by
         * exactly that. ALIGN(8) at the start is harmless: the 4-aligned inputs
         * pack contiguously after it, so __zxv_decl_end - __zxv_decl_start stays
         * an exact multiple of 4.
         *
         * PLACED BY HAND, NOT LEFT AN ORPHAN. The arm64 script records an
         * orphan section landing at VMA 0x40080000 over .text under the wrong
         * PHDR and QEMU refusing the kernel; this script loads at the same
         * 0x40000000 base off the same `virt` machine. Explicit markers are
         * deterministic; ld mints no __start_/__stop_ symbols for a section
         * name containing dots anyway.
         *
         * ASSIGNED, not PROVIDEd: the C side declares these markers WEAK so an
         * arch that never placed the section still links, and PROVIDE defines a
         * symbol only when something references it — the one case a weak
         * undefined reference makes unreliable.
         *
         * KEEP() is inert without --gc-sections (this link does not pass it),
         * written anyway so adding it later cannot silently empty the graph. */
        . = ALIGN(8);
        __zxv_decl_start = .;
        KEEP(*(.rodata.zxv_decl))
        __zxv_decl_end = .;

        *(.rodata)
        *(.rodata.*)
    } > RAM

    .data : {
        *(.data)
        *(.data.*)
    } > RAM

    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    . = ALIGN(8);
    . = . + 0x10000; /* 64KB stack */
    _stack_top = .;
}
