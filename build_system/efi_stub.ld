ENTRY(efi_main)
SECTIONS {
  . = 0;
  .head : { KEEP(*(.head)) }
  . = 0x1000;
  .text : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) }
  . = ALIGN(0x1000);
  __image_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
