; boot.asm — x86-64 multiboot1 long-mode entry (NASM syntax)
;
; Author: H.M. Michael-Laurence: Curzi (c)
; License: SEL-3.3

BITS 32

%define MB_MAGIC     0x1BADB002
%define MB_FLAGS     0x00010000          ; use header address fields
%define MB_CHECKSUM  -(MB_MAGIC + MB_FLAGS)

; Linker-provided symbols.
EXTERN __bss_start
EXTERN __bss_end
EXTERN __stack_top
EXTERN kernel_main_x86_64

SECTION .text.boot progbits alloc exec write align=8

global _start
_start:
    ; Multiboot1 header with address fields.
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd _start                       ; header_addr
    dd 0x100000                     ; load_addr
    dd 0                            ; load_end_addr (0 = whole file)
    dd 0                            ; bss_end_addr
    dd _entry32                     ; entry_addr

; Temporary identity page tables (4-level). 2MB identity map.
align 0x1000
pt_l4:  resb 4096
pt_l3:  resb 4096
pt_l2:  resb 4096
pt_l1:  resb 4096

align 16
gdt64:
    dq 0x0000000000000000           ; null
    dq 0x00209A0000000000           ; 64-bit kernel code (execute/read, L=1)
    dq 0x0000920000000000           ; 64-bit kernel data
gdt64_ptr:
    dw (gdt64_ptr - gdt64 - 1)
    dd gdt64

; Far jump target for the 32-to-64-bit transition.
align 8
far_jmp_target:
    dd _entry64
    dw 0x08

; 32-bit entry point from multiboot1 loader.
align 16
global _entry32
_entry32:
    cli

    ; Zero BSS.
    mov  edi, __bss_start
    mov  ecx, __bss_end
    sub  ecx, edi
    shr  ecx, 2
    xor  eax, eax
    rep  stosd

    ; Set up 2MB identity page tables.
    mov  edi, pt_l1
    mov  eax, 0x03                  ; P | RW
    mov  ecx, 512
.pt_l1_loop:
    mov  [edi], eax
    add  edi, 8
    add  eax, 0x1000
    loop .pt_l1_loop

    mov  edi, pt_l2
    mov  eax, pt_l1
    or   eax, 0x03
    mov  [edi], eax

    mov  edi, pt_l3
    mov  eax, pt_l2
    or   eax, 0x03
    mov  [edi], eax

    mov  edi, pt_l4
    mov  eax, pt_l3
    or   eax, 0x03
    mov  [edi], eax

    ; Load PML4 into CR3.
    mov  eax, pt_l4
    mov  cr3, eax

    ; Enable PAE and PGE.
    mov  eax, cr4
    or   eax, 0x20                  ; PAE
    or   eax, 0x80                  ; PGE
    mov  cr4, eax

    ; Enable long mode (LME) in EFER.
    mov  ecx, 0xC0000080
    rdmsr
    or   eax, 0x100                 ; LME
    wrmsr

    ; Enable paging.
    mov  eax, cr0
    or   eax, 0x80000000
    mov  cr0, eax

    ; Load the 64-bit GDT and far-jump to long mode.
    mov  eax, gdt64_ptr
    lgdt [eax]
    jmp  far [rel far_jmp_target]   ; 32-bit far jump to 64-bit CS

; 64-bit kernel entry.
BITS 64
global _entry64
_entry64:
    ; Load 64-bit data segments.
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Set up a temporary stack.
    mov  rsp, __stack_top

    ; Enable SSE — the SysV x86-64 ABI + gcc emit SSE (movdqa/xmm) for struct
    ; copies and doubles; without this the first such instruction #UDs. Clear
    ; CR0.EM (bit 2), set CR0.MP (bit 1); set CR4.OSFXSR (bit 9) + OSXMMEXCPT (10).
    mov  rax, cr0
    and  ax, 0xFFFB
    or   ax, 0x0002
    mov  cr0, rax
    mov  rax, cr4
    or   ax, 0x0600
    mov  cr4, rax

    ; Save multiboot1 magic and info pointer.
    mov  edi, eax
    mov  rsi, rbx

    ; Jump to C.
    call kernel_main_x86_64

    ; Should not return.
    cli
.hang:
    hlt
    jmp  .hang
