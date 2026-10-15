/* x86_selftest.c: the x86-64 ring-3 program behind the scheduler self-test
 *
 * One static ELF64 (EM_X86_64), linked into user space by x86_user.ld and
 * embedded in the kernel (arch/x86_64/x86_userbin.S). The kernel starts it
 * several times; rdi picks the role, rsi and rdx are its arguments. It uses
 * only int 0x80 on the shared ABI (<zxv_syscall_user.h>): no libc, no kernel
 * symbols. Built -mgeneral-regs-only so the compiler never touches xmm0 or
 * xmm7, which the spin role owns.
 *
 *   1 yield   write 'y' and yield, rsi times, then exit 0
 *   2 spin    hold the pattern rdx in xmm0 and xmm7 and spin for rsi TSC
 *             cycles (long enough to be preempted many times), checking the
 *             registers all along; exit 2 if they ever change (FP state
 *             leaked between processes), else write 's' and exit 0
 *   3 sleep   sleep rdx ms, write 'z', rsi times, then exit 0
 *   10-13     fault on purpose: write to a kernel page (#PF), hlt (#GP),
 *             ud2 (#UD), divide by zero (#DE). Reaching the exit after the
 *             faulting instruction (code 5) means the fault did not happen.
 * Every role first checks ABI_VERSION and GETPID (exit 3 or 4 if wrong).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <zxv_syscall_user.h>

static inline long sc(long nr, long a0, long a1, long a2)
{
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1), "d"(a2) : "memory");
    return r;
}

__attribute__((noreturn)) static void leave(long code)
{
    sc(ZXV_SYS_EXIT, code, 0, 0);
    for (;;) {
    }
}

static inline unsigned long rdtsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long) hi << 32) | lo;
}

static void spin(unsigned long budget, unsigned long pattern)
{
    __asm__ __volatile__("movq %0, %%xmm0\n\tmovq %0, %%xmm7" : : "r"(pattern));
    unsigned long t0 = rdtsc();
    while (rdtsc() - t0 < budget) {
        unsigned long x0, x7;
        __asm__ __volatile__("movq %%xmm0, %0\n\tmovq %%xmm7, %1" : "=r"(x0), "=r"(x7));
        if (x0 != pattern || x7 != pattern) leave(2);
    }
    sc(ZXV_SYS_WRITE, 's', 0, 0);
    leave(0);
}

__attribute__((noreturn, used)) void _start(long role, long a1, long a2)
{
    if ((unsigned long) sc(ZXV_SYS_ABI_VERSION, 0, 0, 0) != ZXV_ABI_VERSION) leave(3);
    if (sc(ZXV_SYS_GETPID, 0, 0, 0) <= 0) leave(4);

    switch (role) {
    case 1:
        for (long i = 0; i < a1; i++) {
            sc(ZXV_SYS_WRITE, 'y', 0, 0);
            sc(ZXV_SYS_YIELD, 0, 0, 0);
        }
        leave(0);
    case 2:
        spin((unsigned long) a1, (unsigned long) a2);
        leave(0);
    case 3:
        for (long i = 0; i < a1; i++) {
            sc(ZXV_SYS_SLEEP, a2, 0, 0);
            sc(ZXV_SYS_WRITE, 'z', 0, 0);
        }
        leave(0);
    case 10:
        *(volatile unsigned long *) 0x100000 = 1; /* kernel text: supervisor-only */
        leave(5);
    case 11:
        __asm__ __volatile__("hlt"); /* privileged */
        leave(5);
    case 12:
        __asm__ __volatile__("ud2");
        leave(5);
    case 13:
        __asm__ __volatile__("xorl %%ecx, %%ecx\n\tdivq %%rcx" : : : "rax", "rdx", "rcx");
        leave(5);
    default:
        leave(6);
    }
}
