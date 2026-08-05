/* linker.ld — AArch64 linker script for VOVINA SHAKINA
 *
 * Target: qemu-system-aarch64 -M virt
 * Load address: 0x40080000 (QEMU virt kernel load address)
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
    /* GCC/ld auto-generate a .note.gnu.build-id section that this
     * script does not otherwise place. As an orphan section it was
     * landing at VMA 0x40080000 (overlapping the start of .text)
     * while being attributed to the RW :data PHDR -- the actual root
     * cause of overlapping PT_LOAD segments (QEMU: "ROM regions
     * overlapping"). It has no purpose in a freestanding kernel. */
    /DISCARD/ : {
        *(.note.*)
        *(.comment)
    }

    /* QEMU virt machine loads kernel at 0x40080000 */
    . = 0x40080000;

    .text : {
        /* Boot code first */
        *(.text.boot)
        /* Then everything else */
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    /* Align to the next page before the RW segment so its virtual
     * address advances in lockstep with the file offset that ld's
     * PT_LOAD segment split already applies at this RX->RW boundary
     * (0x10000 matches the Align this toolchain reports on the
     * generated program headers -- see readelf -l). Without this,
     * both PT_LOAD segments report the same VirtAddr and QEMU refuses
     * to load the kernel ("ROM regions overlapping"). */
    . = ALIGN(0x10000);

    .data : {
        *(.data)
        *(.data.*)
    } :data

    /* BSS — zeroed by boot.s */
    .bss : {
        __bss_start = .;
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
        _bss_end = .;
    } :data

    /* Stack */
    . = ALIGN(16);
    . = . + 0x10000; /* 64KB stack */
    __stack_top = .;

    /* Page-aligned end */
    . = ALIGN(4096);
    __kernel_end = .;
}
