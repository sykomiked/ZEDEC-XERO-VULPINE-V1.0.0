/* linker.ld — AArch64 linker script for VOVINA SHAKINA
 *
 * Target: qemu-system-aarch64 -M virt
 * Load address: 0x40080000 (QEMU virt kernel load address)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: Apache-2.0
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
    _text_start = .;            /* start of the immutable core (.text + .rodata) */

    .text : {
        /* Boot code first */
        *(.text.boot)
        /* Then everything else */
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        /* MODULE DECLARATIONS (kernel/include/zxv_decl.h).
         *
         * Each ZXV_DECLARE emits one pointer here, and the boot walk registers
         * every one of them into modbind. KEEP() is the retention lever:
         * --gc-sections drops any section nothing references, and nothing
         * references these by name -- that is the whole design, the modules are
         * found rather than listed.
         *
         * PLACED EXPLICITLY, AND NOT AS AN ORPHAN. See the /DISCARD/ comment
         * above: an unplaced orphan section in this very script landed at VMA
         * 0x40080000 over the start of .text, was attributed to the RW :data
         * PHDR, and QEMU refused the kernel with "ROM regions overlapping".
         * Explicit markers here are deterministic; ld's auto-generated
         * __start_/__stop_ symbols would require leaving the section orphaned
         * (and would not be generated at all for a name containing dots).
         *
         * The markers are ASSIGNED, not PROVIDEd. PROVIDE only defines a symbol
         * when something references it, and the C side declares these WEAK (so
         * an arch that never placed the section still links) -- a weak
         * undefined reference is exactly the case where "is it referenced?"
         * stops being reliable. An unconditional assignment cannot half-resolve
         * into a walk that silently finds nothing.
         *
         * The records are const and never written, so they belong in the RX
         * :text segment with the rest of .rodata -- no new PT_LOAD to get
         * wrong. The `.rodata.` prefix also means any linker script WITHOUT
         * this block still absorbs them through its own *(.rodata.*) wildcard,
         * so an arch that has not opted in gets inert bytes, not a bad image.  */
        . = ALIGN(8);
        __zxv_decl_start = .;
        KEEP(*(.rodata.zxv_decl))
        __zxv_decl_end = .;

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
    _data_start = .;            /* start of volatile state (.data/.bss/stack) — RW */

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
