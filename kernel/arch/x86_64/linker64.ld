/* linker64.ld — x86-64 multiboot2 kernel linker script
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* RX */
    data PT_LOAD FLAGS(6);   /* RW */
}

SECTIONS
{
    /DISCARD/ : {
        *(.note.*)
        *(.comment)
    }

    /* Multiboot2 headers must be in the first 32KB and aligned. */
    . = 0x100000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.data.*)
    } :data

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /* Stack grows down from 0x200000; first 2MB are identity-mapped. */
    __stack_top = 0x200000;
}
