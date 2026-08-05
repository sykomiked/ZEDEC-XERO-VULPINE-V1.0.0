/* linker_arm64.ld — Linker script for VOVINA SHAKINA ARM64 kernel
 *
 * Target: QEMU virt machine. Loaded at 0x40080000.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    .text : ALIGN(4096)
    {
        *(.text.boot)
        *(.text)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata)
    }

    .data : ALIGN(4096)
    {
        *(.data)
    }

    . = ALIGN(4096);
    _bss_start = .;
    .bss : ALIGN(4096)
    {
        *(.bss)
        *(COMMON)
    }
    _bss_end = .;

    . = ALIGN(4096);
    _stack_bottom = .;
    . += 65536;
    _stack_top = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
    }
}
