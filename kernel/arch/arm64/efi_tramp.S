/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* efi_tramp.S — relocatable copy-and-jump trampoline for the arm64 EFI stub.
 *
 * WHY THIS EXISTS: AAVMF loads BOOTAA64.EFI into low RAM, which overlaps the
 * kernel's fixed link address 0x40080000. Copying the kernel there directly
 * overwrote the stub's OWN executing code (and the embedded blob) mid-loop —
 * the CPU then fetched stub bytes at 0x40080000 and took an Undefined
 * Instruction. So the stub stages the kernel into firmware-allocated memory,
 * copies THIS routine to its own scratch page, and jumps to it. Running from
 * that page, the trampoline touches neither the EFI image nor the blob.
 *
 * Must be fully position-independent and self-contained: no literal pools that
 * reference outside [_tramp_start.._tramp_end), no calls, no globals.
 *
 * Entry (AAPCS): x0 = staging src, x1 = dest (0x40080000), x2 = byte count.
 * Runs with the MMU still on (firmware identity map); disables it before the
 * final branch so the kernel enters exactly as it does on the -kernel path. */

.section .text
.balign 16
.globl _tramp_start
.hidden _tramp_start
_tramp_start:
    mov     x3, x1                  /* keep dest for the branch */

    /* ---- copy staging -> dest, 8 bytes at a time then the tail ---- */
    mov     x4, xzr
1:  add     x5, x4, #8
    cmp     x5, x2
    b.hi    2f
    ldr     x6, [x0, x4]
    str     x6, [x1, x4]
    mov     x4, x5
    b       1b
2:  cmp     x4, x2
    b.hs    3f
    ldrb    w6, [x0, x4]
    strb    w6, [x1, x4]
    add     x4, x4, #1
    b       2b
3:

    /* ---- clean D-cache to PoC over the copied range, invalidate I-cache ----
     * Done BEFORE turning caches off: once SCTLR.C is clear, stores would not
     * be pushed out and the MMU-off kernel could fetch stale lines. Start one
     * 64KB below dest so the boot-handoff record is cleaned too. */
    sub     x7, x3, #0x10000
    add     x8, x3, x2
4:  dc      cvac, x7
    add     x7, x7, #64
    cmp     x7, x8
    b.lo    4b
    dsb     sy
    ic      iallu
    dsb     sy
    isb

    /* ---- MMU + caches off, then enter the kernel (EL1, MMU off) ---- */
    mrs     x0, sctlr_el1
    bic     x0, x0, #1              /* M:  MMU off     */
    bic     x0, x0, #4              /* C:  D-cache off */
    bic     x0, x0, #4096           /* I:  I-cache off */
    msr     sctlr_el1, x0
    isb
    br      x3

.balign 16
.globl _tramp_end
.hidden _tramp_end
_tramp_end:
