/* linker.ld — RISC-V 64-bit linker script for VOVINA SHAKINA
 *
 * Target: qemu-system-riscv64 -M virt
 * Load address: 0x80200000 (QEMU virt kernel load address)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    . = . + 0x10000;
    __stack_top = .;

    . = ALIGN(4096);
    __kernel_end = .;
}
