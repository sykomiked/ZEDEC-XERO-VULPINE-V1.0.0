/* boot.s — Multiboot entry point for VOVINA SHAKINA
 * GRUB loads this file, sets up the multiboot header, and calls kernel_main
 */
/* boot.s — Multiboot1 entry point for VOVINA SHAKINA
 * GRUB2 loads in 32-bit protected mode. We stay in 32-bit.
 * The C code is compiled with -m32 — wait, no 32-bit libs.
 * Alternative: use multiboot2 with 64-bit long mode switch.
 * Simplest: use cross-compiler or just use the host gcc in 64-bit
 * with a multiboot1 header and let GRUB handle the transition.
 */
.section .multiboot
.align 4
.long 0x1BADB002
.long 0x00000000
.long -(0x1BADB002 + 0)

.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:

.section .text
.global _start
.code32
_start:
    movl $(stack_top), %esp
    pushl %ebx
    pushl %eax
    call kernel_main
    cli
hang:
    hlt
    jmp hang
