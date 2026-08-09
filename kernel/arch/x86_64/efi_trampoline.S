/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* efi_trampoline.S — long mode -> 32-bit protected mode handoff for the x86-64
 * EFI stub. UEFI runs us in 64-bit long mode with an identity page map; our
 * Multiboot1 kernel wants 32-bit protected mode at _entry32 (which rebuilds its
 * own long-mode paging). Standard "reverse of entering long mode" sequence:
 *   1. lgdt our own GDT (with a 32-bit code+data pair) — built rip-relative so
 *      the image needs no PE relocations.
 *   2. far-return into a 32-bit *compatibility* code segment (L=0, D=1).
 *   3. clear CR0.PG  -> deactivates long mode (EFER.LMA -> 0).
 *   4. clear EFER.LME, clear CR4.PAE/PGE -> pure legacy 32-bit protected mode.
 *   5. jmp to _entry32 (identity: linear == physical once paging is off).
 * Entry (SysV): %rdi = kernel _entry32 physical address (< 4 GiB). */

.section .text
.balign 16
.code64
.globl x86_efi_handoff
.hidden x86_efi_handoff        /* hidden => C calls it PC-relative, not via the GOT */
x86_efi_handoff:
    cli
    /* Build a GDTR that points at our runtime GDT (rip-relative => PIC). */
    lea     gdt_base(%rip), %rax
    lea     gdtr(%rip), %rdx
    movw    $(gdt_end - gdt_base - 1), (%rdx)
    movq    %rax, 2(%rdx)
    lgdt    (%rdx)
    /* Preserve the kernel entry across the mode switch (GPRs survive it). */
    movq    %rdi, %rbx
    /* Far-return into the 32-bit compatibility code segment (selector 0x08).
     * Push order is [CS][offset] so lretq pops RIP=offset then CS=0x08. */
    lea     compat32(%rip), %rax
    pushq   $0x08
    pushq   %rax
    lretq

.code32
.balign 16
compat32:
    /* Reload data/stack selectors with the 32-bit data descriptor (0x10). */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    /* Disable paging -> leave long mode (clears EFER.LMA). Next fetch is
     * identity (linear == physical), so execution continues seamlessly. */
    movl    %cr0, %eax
    andl    $0x7FFFFFFF, %eax
    movl    %eax, %cr0
    /* Clear EFER.LME (MSR 0xC0000080, bit 8). */
    movl    $0xC0000080, %ecx
    rdmsr
    andl    $0xFFFFFEFF, %eax
    wrmsr
    /* Clear CR4.PAE (bit 5) and PGE (bit 7) -> plain legacy protected mode. */
    movl    %cr4, %eax
    andl    $0xFFFFFF5F, %eax
    movl    %eax, %cr4
    /* Enter the kernel. entry (< 4 GiB) is in ebx; CS base is 0 (flat). */
    jmp     *%ebx

.section .data
.balign 16
gdt_base:
    .quad 0x0000000000000000     /* null */
    .quad 0x00CF9A000000FFFF     /* 0x08: 32-bit code, base 0, limit 4G, G=1 D=1 */
    .quad 0x00CF92000000FFFF     /* 0x10: 32-bit data, base 0, limit 4G, G=1 D=1 */
gdt_end:
.balign 16
gdtr:
    .word 0                      /* limit (filled at runtime) */
    .quad 0                      /* base  (filled at runtime) */
