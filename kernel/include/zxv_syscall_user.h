/* zxv_syscall_user.h — the ENTIRE userland view of the ZXV syscall ABI.
 *
 * A ZXV user program includes this and nothing else from the kernel tree. It
 * gets exactly two things, both generated from the one table in
 * <zxv_syscall_abi.h>:
 *
 *   * ZXV_SYS_<NAME>   the syscall numbers
 *   * ZXV_ABI_VERSION  the ABI it was built against
 *
 * WHAT IS ABSENT IS THE POINT
 * ---------------------------
 * No kernel structs. No prototypes. No capability bit values. No error-code
 * table. Not even <stdint.h>, because a freestanding -nostdlib user program
 * has no libc to give it one.
 *
 * Publishing a kernel struct in a userland header is how a system acquires a
 * flat ABI: the moment user code can see a kernel layout, that layout is
 * frozen, and every later change to it is a compatibility break instead of an
 * internal edit. ZXV's API-poly / AMI direction rejects that, so the boundary
 * is drawn narrow on purpose. Userland gets numbers; the meaning of a call is
 * negotiated through the call, not through a shared memory layout.
 *
 * The capability column of the table is likewise withheld. It is a macro
 * PARAMETER in <zxv_syscall_abi.h> and ZSC_CAP_* has no value on this side, so
 * a future edit that tried to expose it here would fail to compile rather than
 * quietly export kernel policy into the user ABI. That failure is the guard.
 *
 * WHY THIS IS NOT A COPY
 * ----------------------
 * It used to be. userapp/hello.c opened with four hand-written #defines
 * transcribed from the table, and nothing in the build could tell you when the
 * table moved out from under them -- a renumbered syscall still assembles,
 * still links, still runs, and calls the wrong kernel function. Now the numbers
 * come from the same preprocessor expansion the kernel dispatcher uses, so a
 * change to the table reaches the user binary on the next compile or not at
 * all; there is no third state where the two disagree.
 *
 * USAGE
 * -----
 *     #include <zxv_syscall_user.h>
 *     register long x8 __asm__("x8") = ZXV_SYS_WRITE;
 *     __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
 *
 * Ask the kernel whether it can serve you before relying on a newer call:
 *     if (!ZXV_ABI_AT_LEAST(sys_abi_version(), 1, 0)) { ... }
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ABI slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_SYSCALL_USER_H
#define ZXV_SYSCALL_USER_H

/* The one table. Numbers and ZXV_ABI_VERSION are generated from it. */
#include <zxv_syscall_abi.h>

/* Version arithmetic, so a program can test a kernel-reported version without
 * knowing how the two halves are packed. Same MAJOR and a MINOR at least as new
 * -- the same rule zxv_abi_compatible() applies on the kernel side, stated once
 * here so userland is not left to reimplement the shift by hand. */
#define ZXV_ABI_MAJOR_OF(v)      (((unsigned)(v) >> 16) & 0xFFFFu)
#define ZXV_ABI_MINOR_OF(v)      ((unsigned)(v) & 0xFFFFu)
#define ZXV_ABI_AT_LEAST(v, maj, min) \
    (ZXV_ABI_MAJOR_OF(v) == (unsigned)(maj) && ZXV_ABI_MINOR_OF(v) >= (unsigned)(min))

#endif /* ZXV_SYSCALL_USER_H */
