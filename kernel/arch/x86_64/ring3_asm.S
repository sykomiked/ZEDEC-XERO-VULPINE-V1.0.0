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
sc_arg:            .quad 0        /* syscall arg0 scratch (user rdi)            */
sc_arg1:           .quad 0        /* syscall arg1 scratch (user rsi)            */
sc_arg2:           .quad 0        /* syscall arg2 scratch (user rdx)            */
.global x86_last_exc
x86_last_exc:      .quad 0        /* vector of the last CPU exception (or -1)   */
.global x86_exc_vec
x86_exc_vec:       .quad 0        /* captured vector of a fault during ring-3   */
.global x86_exc_rip
x86_exc_rip:       .quad 0        /* captured faulting RIP during ring-3        */

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
    andq $-513, %rax          /* IF = 0: no hardware IRQ can preempt the ring-3 test
                               * (int 0x80 is a software interrupt, unaffected by IF) */
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
    cmpq $1, %rax             /* ZXV_SYS_EXIT (1, shared ABI) -> back to the kernel */
    je   x86_ring3_return

    /* Capture the user's argument registers BEFORE anything clobbers them.
     * The ABI passes args in rdi/rsi/rdx; the dispatch call itself uses those
     * same registers under SysV, so they must be stashed first. */
    movq %rax, sc_num(%rip)
    movq %rdi, sc_arg(%rip)
    movq %rsi, sc_arg1(%rip)
    movq %rdx, sc_arg2(%rip)
    /* preserve the caller-saved regs across the C dispatch + IRETQ back to user */
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %r8
    push %r9
    push %r10
    push %r11
    movq sc_num(%rip),  %rdi   /* SysV arg1: syscall number */
    movq sc_arg(%rip),  %rsi   /* SysV arg2: user rdi       */
    movq sc_arg1(%rip), %rdx   /* SysV arg3: user rsi       */
    movq sc_arg2(%rip), %rcx   /* SysV arg4: user rdx       */
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
/* Per-vector stubs: push a uniform [vector][errcode] then a common handler, so we
 * always know which exception fired and its faulting RIP. */
.macro EXC_NOERR n
.global x86_exc_\n
x86_exc_\n:
    pushq $0
    pushq $\n
    jmp   x86_exc_common
.endm
.macro EXC_ERR n
.global x86_exc_\n
x86_exc_\n:
    pushq $\n
    jmp   x86_exc_common
.endm
EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR   21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_NOERR 29
EXC_NOERR 30
EXC_NOERR 31

x86_exc_common:
    /* stack now: [vector][errcode][RIP][CS][RFLAGS]... */
    movq $-1, x86_last_exc(%rip)
    /* Recover FIRST if we faulted inside the ring-3 self-test — capture vector+RIP
     * for later reporting but do NO C call here (a re-fault would escalate to #DF). */
    movq g_ring3_active(%rip), %rax
    testq %rax, %rax
    jz    .Lkernel_fault
    movq 0(%rsp), %rax
    movq %rax, x86_exc_vec(%rip)
    movq 16(%rsp), %rax
    movq %rax, x86_exc_rip(%rip)
    jmp   x86_ring3_return
.Lkernel_fault:
    /* a fault in ordinary kernel code — report it and halt. */
    movq 0(%rsp), %rdi              /* vector */
    movq 16(%rsp), %rsi             /* RIP    */
    call x86_exc_report
1:  cli
    hlt
    jmp  1b

.section .rodata
.balign 8
.global x86_exc_table
x86_exc_table:
    .quad x86_exc_0,  x86_exc_1,  x86_exc_2,  x86_exc_3
    .quad x86_exc_4,  x86_exc_5,  x86_exc_6,  x86_exc_7
    .quad x86_exc_8,  x86_exc_9,  x86_exc_10, x86_exc_11
    .quad x86_exc_12, x86_exc_13, x86_exc_14, x86_exc_15
    .quad x86_exc_16, x86_exc_17, x86_exc_18, x86_exc_19
    .quad x86_exc_20, x86_exc_21, x86_exc_22, x86_exc_23
    .quad x86_exc_24, x86_exc_25, x86_exc_26, x86_exc_27
    .quad x86_exc_28, x86_exc_29, x86_exc_30, x86_exc_31
.text
