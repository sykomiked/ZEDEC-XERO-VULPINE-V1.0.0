/*
 * ARM32 linker script for ZEDEC XERO pqOS
 * Target: qemu-system-arm -M virt (Cortex-A15)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
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
