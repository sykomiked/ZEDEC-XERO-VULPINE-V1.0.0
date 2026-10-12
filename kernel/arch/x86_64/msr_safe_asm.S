/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* msr_safe_asm.S — RDMSR/WRMSR that survive #GP, via the exception-fixup table.
 *
 * Each instruction that may fault gets an entry in .rodata.zxv_extable pairing
 * its address with a recovery label. If RDMSR/WRMSR raises #GP (MSR not
 * implemented, reserved bits set), x86_exc_common (ring3_asm.S) finds the RIP
 * in the table, rewrites the saved RIP to the recovery label and IRETQs; the
 * recovery code returns -1 to the C caller (msr_safe.c).
 *
 * Both are leaf functions that keep nothing below RSP, so the interrupt frame
 * the CPU pushes on the fault (same-privilege, on the current stack) cannot
 * clobber live data. */

.macro X86_EXTABLE_ENTRY insn, fixup
    .pushsection .rodata.zxv_extable, "a"
    .balign 8
    .quad \insn, \fixup
    .popsection
.endm

.code64
.text

/* int x86_rdmsr_raw_safe(uint32_t msr [edi], uint64_t *val [rsi])
 * 0 and *val written on success; -1 and *val untouched on #GP. */
.global x86_rdmsr_raw_safe
.type x86_rdmsr_raw_safe, @function
x86_rdmsr_raw_safe:
    movl    %edi, %ecx
.global x86_rdmsr_insn         /* exported only so the boot self-test can */
x86_rdmsr_insn:                 /* confirm the table entry is registered    */
    rdmsr
    shlq    $32, %rdx
    movl    %eax, %eax
    orq     %rdx, %rax
    movq    %rax, (%rsi)
    xorl    %eax, %eax
    ret
.Lrdmsr_fixup:
    movl    $-1, %eax
    ret
    X86_EXTABLE_ENTRY x86_rdmsr_insn, .Lrdmsr_fixup
.size x86_rdmsr_raw_safe, . - x86_rdmsr_raw_safe

/* int x86_wrmsr_raw_safe(uint32_t msr [edi], uint64_t val [rsi])
 * 0 on success; -1 on #GP (the MSR keeps its old value). */
.global x86_wrmsr_raw_safe
.type x86_wrmsr_raw_safe, @function
x86_wrmsr_raw_safe:
    movl    %edi, %ecx
    movl    %esi, %eax
    movq    %rsi, %rdx
    shrq    $32, %rdx
.global x86_wrmsr_insn
x86_wrmsr_insn:
    wrmsr
    xorl    %eax, %eax
    ret
.Lwrmsr_fixup:
    movl    $-1, %eax
    ret
    X86_EXTABLE_ENTRY x86_wrmsr_insn, .Lwrmsr_fixup
.size x86_wrmsr_raw_safe, . - x86_wrmsr_raw_safe
