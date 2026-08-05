/*
 * ARM32 linker script for ZEDEC XERO pqOS
 * Target: versatilepb (Cortex-A9)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 */
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00010000, LENGTH = 128M
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
