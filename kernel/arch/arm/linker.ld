/* linker.ld — Linker script for ARM (QEMU virt machine)
 * Kernel loads at 0x40000000 (DRAM base on virt machine)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text ALIGN(4K) : {
        *(.text.boot)
        *(.vectors)
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.ARM.exidx)
        *(.ARM.extab)
    }
}
