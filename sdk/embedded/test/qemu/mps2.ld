/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mps2.ld — QEMU mps2-an385 (Cortex-M3) / mps2-an386 (Cortex-M4): code in
 * SSRAM1 at 0, data, heap and stack in the 4 MB at 0x20000000. Test only. */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(reset_handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
        __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array)) __fini_array_end = .;
    } > CODE
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > CODE
    __etext = .;
    .data : AT(__etext) { __data_start__ = .; *(.data*) . = ALIGN(4); __data_end__ = .; } > RAM
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; } > RAM
    end = .;
    __end__ = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __HeapLimit = __stack_top - 0x10000;
}
