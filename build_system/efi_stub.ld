ENTRY(efi_main)
SECTIONS {
  . = 0;
  .head : { KEEP(*(.head)) }
  . = 0x1000;
  .text : {
    *(.text*) *(.rodata*) *(.data*)
    . = ALIGN(16);
    KEEP(*(.kernel_blob))     /* the embedded kernel_arm64.bin (symbols in the .S) */
    *(.bss*) *(COMMON)
  }
  . = ALIGN(0x1000);
  __image_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
