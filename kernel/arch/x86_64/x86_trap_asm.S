/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_trap_asm.S: one entry stub per IDT vector, the common trap path, the
 * resume path into a saved frame, and a setjmp/longjmp pair for the kernel.
 *
 * Every stub leaves the same frame (x86_irq.h x86_frame_t): the CPU's
 * rip/cs/rflags/rsp/ss, then error (pushed as 0 where the CPU pushes none),
 * vector, and the fifteen general registers. x86_trap() may rewrite the frame
 * (a context switch copies the next process's frame over it), and the common
 * return path restores whatever the frame then holds and IRETQs. */

.code64
.text

/* Exceptions that push an error code: 8, 10-14, 17, 21, 29, 30. */
.macro TRAP_NOERR n
    .balign 16
    pushq $0
    pushq $\n
    jmp   x86_trap_common
.endm
.macro TRAP_ERR n
    .balign 16
    pushq $\n
    jmp   x86_trap_common
.endm

/* 256 stubs, 16 bytes apart: the IDT entry for vector v is
 * x86_trap_stubs + 16 * v. The longest stub (pushq $0, pushq $imm32,
 * jmp rel32) is 12 bytes, so the stride always holds. */
    .balign 16
.global x86_trap_stubs
x86_trap_stubs:
    TRAP_NOERR 0
    TRAP_NOERR 1
    TRAP_NOERR 2
    TRAP_NOERR 3
    TRAP_NOERR 4
    TRAP_NOERR 5
    TRAP_NOERR 6
    TRAP_NOERR 7
    TRAP_ERR   8
    TRAP_NOERR 9
    TRAP_ERR   10
    TRAP_ERR   11
    TRAP_ERR   12
    TRAP_ERR   13
    TRAP_ERR   14
    TRAP_NOERR 15
    TRAP_NOERR 16
    TRAP_ERR   17
    TRAP_NOERR 18
    TRAP_NOERR 19
    TRAP_NOERR 20
    TRAP_ERR   21
    TRAP_NOERR 22
    TRAP_NOERR 23
    TRAP_NOERR 24
    TRAP_NOERR 25
    TRAP_NOERR 26
    TRAP_NOERR 27
    TRAP_NOERR 28
    TRAP_ERR   29
    TRAP_ERR   30
    TRAP_NOERR 31
    .set vec, 32
    .rept 224
    TRAP_NOERR vec
    .set vec, vec + 1
    .endr
    .balign 16
.global x86_trap_stubs_end
x86_trap_stubs_end:

x86_trap_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, %rdi
    cld
    call  x86_trap
x86_trap_pop:
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rbp
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rbx
    popq  %rax
    addq  $16, %rsp           /* vector, error */
    iretq

/* void x86_frame_resume(const x86_frame_t *f): the frame becomes the stack
 * for the pops and the IRETQ. Interrupts are off, so nothing else pushes onto
 * it in between. */
.global x86_frame_resume
x86_frame_resume:
    movq  %rdi, %rsp
    jmp   x86_trap_pop

/* int x86_kctx_save(x86_kctx_t *k): 0 now, 1 when x86_kctx_restore(k) runs.
 * Layout: rbx, rbp, r12, r13, r14, r15, rsp, rip. */
.global x86_kctx_save
x86_kctx_save:
    movq  %rbx,  0(%rdi)
    movq  %rbp,  8(%rdi)
    movq  %r12, 16(%rdi)
    movq  %r13, 24(%rdi)
    movq  %r14, 32(%rdi)
    movq  %r15, 40(%rdi)
    leaq  8(%rsp), %rax       /* the caller's rsp after this call returns */
    movq  %rax, 48(%rdi)
    movq  (%rsp), %rax        /* return address */
    movq  %rax, 56(%rdi)
    xorl  %eax, %eax
    ret

.global x86_kctx_restore
x86_kctx_restore:
    movq   0(%rdi), %rbx
    movq   8(%rdi), %rbp
    movq  16(%rdi), %r12
    movq  24(%rdi), %r13
    movq  32(%rdi), %r14
    movq  40(%rdi), %r15
    movq  48(%rdi), %rsp
    movl  $1, %eax
    jmpq  *56(%rdi)

.section .note.GNU-stack, "", @progbits
