/*
 * ARM32 linker script for ZEDEC XERO pqOS
 * Target: versatilepb (Cortex-A9)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 */
ENTRY(_start)

/* 256M holds the full subsystem set's static data (the emu/MegaROM/game_universe
 * tables are large); the previous 128M overflowed .bss. Boot on the server with
 * qemu-system-arm -m 512M. NOTE: ORIGIN here is the versatilepb base; a QEMU
 * `virt` boot needs the base moved to 0x40000000 (arch bring-up follow-up). */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x00010000, LENGTH = 256M
}

SECTIONS
{
    . = 0x00010000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    } > RAM

    .rodata : {
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
