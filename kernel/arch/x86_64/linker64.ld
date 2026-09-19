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
        /* MODULE DECLARATIONS (kernel/include/zxv_decl.h) — same explicit
         * placement as kernel/arch/arm64/linker.ld:70-74.
         *
         * Each ZXV_DECLARE emits one pointer here and zxv_decl_boot_gate()
         * registers every one of them. PLACED BY HAND, NOT LEFT AN ORPHAN: an
         * unplaced orphan section has already cost this project a boot (the
         * arm64 script's /DISCARD/ comment records .note.gnu.build-id landing
         * at VMA 0x40080000 over .text under the RW PHDR, and QEMU refusing the
         * kernel). Explicit markers are deterministic; ld's __start_/__stop_
         * symbols are not even generated for a name containing dots.
         *
         * ASSIGNED, not PROVIDEd: the C side declares __zxv_decl_start/_end
         * WEAK so an arch that never placed the section still links, and PROVIDE
         * only defines a symbol when something references it — precisely the
         * case a weak undefined reference makes unreliable.
         *
         * KEEP() costs nothing here (this link does not pass --gc-sections) and
         * is written anyway so adding --gc-sections later cannot silently empty
         * the graph. The records are const, so they belong in the RX :text
         * segment with the rest of .rodata — no new PT_LOAD to get wrong. */
        . = ALIGN(8);
        __zxv_decl_start = .;
        KEEP(*(.rodata.zxv_decl))
        __zxv_decl_end = .;

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
        *(.bss.user)      /* ring-3 user code+stack, kept < 2 MB (4 KB-page region) */
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /* Kernel stack sits ABOVE the (now large) BSS, not at a fixed 2MB — the full
     * subsystem set's static tables push .bss well past 2MB. */
    . = . + 0x80000;          /* 512 KB kernel stack */
    __stack_top = .;
}
