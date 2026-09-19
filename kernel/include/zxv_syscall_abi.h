/* zxv_syscall_abi.h — THE syscall table. The one place a number is written.
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * src/syscall/syscall.h already stopped ARCH-vs-ARCH drift: every dispatcher
 * generates its numbering, naming, arity and capability check from one X-macro,
 * so no two architectures can disagree about what call 7 is. It did nothing
 * about the OTHER axis of drift, which is worse:
 *
 *     KERNEL-vs-USERLAND.
 *
 * userapp/hello.c used to open with `#define SYS_EXIT 1 / SYS_WRITE 2 / ...`,
 * hand-transcribed from the table. That copy is correct exactly until someone
 * edits the table, and then it is silently wrong: the binary keeps building,
 * keeps linking, keeps running, and calls the WRONG kernel function. Nothing
 * fails loudly, because a syscall number is just an integer in x8 and the
 * kernel has no way to know the caller meant something else. This is precisely
 * the class of bug that only appears AFTER binaries ship.
 *
 * So the table moved down here, below both sides, and both sides now generate
 * from it:
 *
 *     kernel/include/zxv_syscall_abi.h      <- the table (this file)
 *          |                    |
 *   src/syscall/syscall.h   kernel/include/zxv_syscall_user.h
 *   (kernel view: caps,      (userland view: numbers + version, and
 *    structs, dispatch)       deliberately nothing else)
 *
 * WHY A SHARED HEADER AND NOT A GENERATOR SCRIPT
 * ----------------------------------------------
 * A code-generation build step would also work, and it is what most projects
 * reach for. It was rejected here on purpose: a generator adds a script, an
 * output that is either checked in (and can go stale) or built (and must then
 * be ordered correctly against every one of the five arch Makefiles plus the
 * standalone userapp build, which does not go through those Makefiles at all).
 * The C preprocessor is already a hermetic, reproducible code generator that
 * every one of those builds runs anyway. Including the table costs one -I and
 * cannot go stale, because there is no intermediate artefact to be stale. The
 * X-macro IS the generator; this file is its input.
 *
 * WHAT IS IN HERE, AND WHAT IS DELIBERATELY NOT
 * ---------------------------------------------
 * Only the ABI contract that both sides must agree on: the version, the table,
 * and the numbers generated from it. No structs, no function prototypes, no
 * capability bit VALUES, no includes -- not even <stdint.h>, so it is safe in a
 * freestanding user program built with -nostdlib.
 *
 * The capability column is a macro PARAMETER here, never a value. Userland
 * expanders ignore that parameter, and ZSC_CAP_* is defined only on the kernel
 * side (src/syscall/syscall.h). That is not an oversight: it means a userland
 * header that tried to publish the capability column would fail to compile
 * rather than quietly export a kernel policy decision into the user ABI.
 *
 * NUMBERS ARE PERMANENT. A withdrawn call leaves a RESERVED hole and the hole
 * stays forever; renumbering to tidy up redirects already-shipped binaries into
 * a different kernel function.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ABI slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_SYSCALL_ABI_H
#define ZXV_SYSCALL_ABI_H

/* ZXV_ABI_VERSION increments when the surface changes in a way callers can
 * observe. Appending a call at the END is backwards compatible and bumps the
 * MINOR; changing or withdrawing an existing call is not, and bumps the MAJOR.
 * A caller asks for the version it was built against and a kernel that cannot
 * honour it says so, instead of letting the program find out by getting
 * nonsense back from call 12. */
#define ZXV_ABI_MAJOR   1u
#define ZXV_ABI_MINOR   0u
#define ZXV_ABI_VERSION ((ZXV_ABI_MAJOR << 16) | ZXV_ABI_MINOR)

/* ---- THE TABLE. Add a syscall HERE and nowhere else. -----------------------
 *   X(number, NAME, capability, arity, "what it does")
 *
 * The capability column is a bare token, expanded only by kernel-side users of
 * this table (they define ZSC_CAP_*). Userland expanders must ignore it. */
#define ZXV_SYSCALL_TABLE(X)                                                   \
    X( 0, RESERVED0, ZSC_CAP_NONE,  0, "reserved: 0 is never a valid call")    \
    X( 1, EXIT,      ZSC_CAP_PROC,  1, "terminate the calling process")        \
    X( 2, WRITE,     ZSC_CAP_WRITE, 2, "write bytes to the console")           \
    X( 3, GETPID,    ZSC_CAP_PROC,  0, "the caller's process id")              \
    X( 4, YIELD,     ZSC_CAP_PROC,  0, "give up the rest of this slice")       \
    X( 5, SLEEP,     ZSC_CAP_PROC,  1, "sleep for n phase ticks")              \
    X( 6, SEND,      ZSC_CAP_IPC,   3, "send a message to a process")          \
    X( 7, RECV,      ZSC_CAP_IPC,   2, "receive a message")                    \
    X( 8, OPEN,      ZSC_CAP_FS,    2, "open a file, returning a handle")      \
    X( 9, CLOSE,     ZSC_CAP_FS,    1, "close a handle")                       \
    X(10, READ,      ZSC_CAP_FS,    3, "read from a handle")                   \
    X(11, EXEC,      ZSC_CAP_EXEC,  2, "run a shell command line")             \
    X(12, ABI_VERSION, ZSC_CAP_NONE,0, "the ABI version this kernel provides")

/* Return convention: >= 0 is success (often a value), < 0 is one of these.
 * Negative errors are returned as-is; callers must not treat "non-zero" as
 * failure, because many calls legitimately return a positive result.
 *
 * These live in the SHARED header, not the kernel one, because they are wire
 * values: the integer the kernel leaves in x0 and the user program reads back.
 * They are not a kernel layout, so publishing them does not create the flat ABI
 * the project rejects -- and withholding them only forces userland to
 * hand-write the magic number, which is the exact drift this file exists to
 * end. (It had already happened: hello.c tested the EXEC refusal against a
 * hard-coded -3 while the kernel returned ZXV_EPERM = -2, so a WORKING
 * capability gate was being reported as a FAILED one.) */
#define ZXV_OK          0
#define ZXV_ENOSYS     -1   /* no such syscall in this ABI                */
#define ZXV_EPERM      -2   /* the caller lacks the required capability   */
#define ZXV_EINVAL     -3   /* malformed arguments                        */
#define ZXV_EFAULT     -4   /* a user pointer was not usable              */
#define ZXV_EAGAIN     -5   /* would block                                */
#define ZXV_EIO        -6   /* the underlying device or store failed      */
#define ZXV_ENOENT     -7   /* no such object                             */
#define ZXV_EABI       -8   /* ABI version mismatch                       */

/* Generated: the numbers. Defined HERE, once, so that a translation unit which
 * pulls in both the kernel and the userland view (a test, say) gets one set of
 * enumerators rather than a redefinition error. */
typedef enum {
#define ZXV_SC_ENUM(n, name, cap, arity, doc) ZXV_SYS_##name = (n),
    ZXV_SYSCALL_TABLE(ZXV_SC_ENUM)
#undef ZXV_SC_ENUM
    ZXV_SYS__COUNT
} zxv_syscall_nr_t;

#endif /* ZXV_SYSCALL_ABI_H */
