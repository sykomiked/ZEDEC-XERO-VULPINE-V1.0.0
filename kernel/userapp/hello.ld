/* hello.ld — link the ZXV sample user app at the EL0 code base (0x10000).
 * Keeps all PT_LOAD segments inside the loader's [0x1000, 0x200000)
 * window and page-aligns the RX/RW boundary so W^X holds per segment. */
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);   /* R E */
    data PT_LOAD FLAGS(6);   /* R W */
}
SECTIONS {
    . = 0x10000;
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(0x1000);
    .data : { *(.data*) } :data
    .bss  : { *(.bss*) *(COMMON) } :data
}
