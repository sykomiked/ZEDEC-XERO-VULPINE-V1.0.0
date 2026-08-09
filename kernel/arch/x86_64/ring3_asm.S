/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ring3_asm.S — x86-64 ring-3 entry + int 0x80 syscall ISR + exception stub.
 * The C side (ring3.c) builds the GDT/TSS/IDT and page mappings; this file is the
 * privilege-transition machinery, mirroring the arm64 EL0 eret/SVC path. */

.code64
.text

/* ---- globals shared with ring3.c ---- */
.section .bss
.balign 8
saved_kernel_rsp:  .quad 0        /* main kernel RSP to return to on SYS_EXIT   */
sc_num:            .quad 0        /* syscall number scratch                     */
sc_arg:            .quad 0        /* syscall arg0 scratch                       */
.global x86_last_exc
x86_last_exc:      .quad 0        /* vector of the last CPU exception (or -1)   */

.text

/* void x86_ring3_enter(uint64_t user_rip [rdi], uint64_t user_rsp [rsi]).
 * Saves the kernel context, then IRETQs down to ring 3. Returns here (as a normal
 * C call return) only when the user does SYS_EXIT (via x86_ring3_return). */
.global x86_ring3_enter
x86_ring3_enter:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    movq %rsp, saved_kernel_rsp(%rip)

    /* Build the IRETQ frame: SS, RSP, RFLAGS, CS, RIP (user, DPL=3). */
    movq $0x23, %rax          /* user data selector (udata | RPL 3) */
    pushq %rax                /* SS   */
    pushq %rsi                /* RSP  (user stack top) */
    pushfq
    popq %rax
    orq  $0x200, %rax         /* IF = 1 so the user runs with interrupts on */
    pushq %rax                /* RFLAGS */
    movq $0x1b, %rax          /* user code selector (ucode | RPL 3) */
    pushq %rax                /* CS   */
    pushq %rdi                /* RIP  (user entry) */

    /* user data segment registers */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    iretq

/* Reached from the ISR on SYS_EXIT: drop the user frame, restore kernel context. */
.global x86_ring3_return
x86_ring3_return:
    movq saved_kernel_rsp(%rip), %rsp
    movw $0x10, %ax           /* kernel data segment */
    movw %ax, %ds
    movw %ax, %es
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret

/* int 0x80 handler. Enters at ring 0 on the TSS.RSP0 interrupt stack with the CPU
 * having pushed SS,RSP,RFLAGS,CS,RIP. rax = syscall number, rdi = arg0. */
.global x86_syscall_isr
x86_syscall_isr:
    cmpq $2, %rax             /* SYS_EXIT -> back to the kernel */
    je   x86_ring3_return

    movq %rax, sc_num(%rip)
    movq %rdi, sc_arg(%rip)
    /* preserve the caller-saved regs across the C dispatch + IRETQ back to user */
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %r8
    push %r9
    push %r10
    push %r11
    movq sc_num(%rip), %rdi
    movq sc_arg(%rip), %rsi
    call x86_syscall_dispatch
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    iretq                     /* return to ring 3, continue the user program */

/* Generic exception stub. During the ring-3 self-test (g_ring3_active=1) a fault
 * unwinds to the kernel like SYS_EXIT so the test reports failure. Otherwise (a
 * fault in ordinary kernel code) it reports and halts — a clean diagnostic stop
 * instead of a silent triple-fault reboot. The interrupt frame top holds RIP. */
.global x86_exc_isr
x86_exc_isr:
    movq $-1, x86_last_exc(%rip)
    movq g_ring3_active(%rip), %rax
    testq %rax, %rax
    jnz   x86_ring3_return          /* fault inside the ring-3 test: recover */
    movq (%rsp), %rdi               /* first frame word (RIP or error code) */
    call x86_exc_report
1:  cli
    hlt
    jmp  1b
