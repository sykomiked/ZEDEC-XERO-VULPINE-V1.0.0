/* syscall.h — ZXV ABI: ONE syscall surface, shared by every architecture
 *
 * WHAT WAS WRONG
 * --------------
 * The syscall numbers lived in an ARCH header (arch/arm64/el0_userspace.h), and
 * the capability each one requires lived in a hand-maintained switch statement
 * next to the arm64 dispatcher. Two consequences, and the second is the serious
 * one:
 *
 *   1. every architecture is free to drift. x86_64 implements two of the eleven
 *      calls and nothing detects the gap, so "the ABI" is whatever each arch
 *      happened to do.
 *   2. adding a syscall and forgetting its capability is a silent hole. The
 *      switch has a default, and a default in a permission check means the new
 *      call is either universally allowed or universally denied depending on
 *      which way someone wrote it — decided by accident rather than by design.
 *
 * THE FIX: ONE TABLE, AND YOU CANNOT ADD A CALL WITHOUT DECLARING ITS RIGHTS
 * --------------------------------------------------------------------------
 * Every syscall is declared exactly once, in ZXV_SYSCALL_TABLE below, together
 * with the capability it requires and how many arguments it takes. The number
 * enum, the name table and the permission table are all GENERATED from that one
 * list, so they cannot disagree. A syscall with no declared capability does not
 * compile — the omission is a build error, not a security hole discovered later.
 *
 * NUMBERS ARE PERMANENT
 * ---------------------
 * A syscall number is an ABI promise: a binary compiled last year still calls
 * number 7. So numbers are never reused and never renumbered. A withdrawn call
 * leaves a HOLE, marked RESERVED, and the hole stays forever. Renumbering to
 * "tidy up" would silently redirect old binaries into a different kernel
 * function, which is the worst class of compatibility bug because everything
 * links and runs and does the wrong thing.
 *
 * VERSIONING
 * ----------
 * ZXV_ABI_VERSION increments when the surface changes in a way callers can
 * observe. Userland asks for the version it was built against; a kernel that
 * cannot honour it says so, rather than letting a program discover the mismatch
 * by getting nonsense from call 12. Adding a new call at the END is backwards
 * compatible and bumps the MINOR; changing or withdrawing an existing call is
 * not, and bumps the MAJOR.
 *
 * WHAT THIS LAYER DOES NOT DO
 * ---------------------------
 * It does not implement the calls — the arch dispatcher still does that, and it
 * still owns copying to and from user memory, which is arch-specific and is
 * where the dangerous bugs live. This layer owns the CONTRACT: numbering,
 * naming, arity, required capability, and version. That separation is
 * deliberate: a table cannot dereference a user pointer, so it cannot be the
 * place a user pointer is mishandled.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ABI slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_SYSCALL_H
#define ZXV_SYSCALL_H

#include <stdint.h>
#include <stdbool.h>

#define ZXV_ABI_MAJOR   1u
#define ZXV_ABI_MINOR   0u
#define ZXV_ABI_VERSION ((ZXV_ABI_MAJOR << 16) | ZXV_ABI_MINOR)

/* Capabilities a syscall can require. These MUST match the per-process
 * capability bits the arch scheduler assigns (arch/arm64/el0_userspace.h
 * CAP_*), and `zxv_syscall_selfcheck()` asserts that they still do. */
#define ZSC_CAP_NONE    0u
#define ZSC_CAP_WRITE   (1u << 0)   /* console output              */
#define ZSC_CAP_PROC    (1u << 1)   /* self: exit/getpid/yield/sleep */
#define ZSC_CAP_EXEC    (1u << 2)   /* run a command line          */
#define ZSC_CAP_IPC     (1u << 3)   /* send / recv                 */
#define ZSC_CAP_FS      (1u << 4)   /* open / read / close         */

/* ---- THE TABLE. Add a syscall HERE and nowhere else. -----------------------
 *   X(number, NAME, capability, arity, "what it does")
 * Numbers are permanent; a withdrawn call becomes RESERVED and keeps its slot.
 * Appending is backwards compatible (bump MINOR); changing an existing row is
 * not (bump MAJOR). */
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

/* Generated: the numbers. */
typedef enum {
#define ZXV_SC_ENUM(n, name, cap, arity, doc) ZXV_SYS_##name = (n),
    ZXV_SYSCALL_TABLE(ZXV_SC_ENUM)
#undef ZXV_SC_ENUM
    ZXV_SYS__COUNT
} zxv_syscall_nr_t;

/* Return convention: >= 0 is success (often a value), < 0 is one of these.
 * Negative errors are returned as-is; callers must not treat "non-zero" as
 * failure, because many calls legitimately return a positive result. */
#define ZXV_OK          0
#define ZXV_ENOSYS     -1   /* no such syscall in this ABI                */
#define ZXV_EPERM      -2   /* the caller lacks the required capability   */
#define ZXV_EINVAL     -3   /* malformed arguments                        */
#define ZXV_EFAULT     -4   /* a user pointer was not usable              */
#define ZXV_EAGAIN     -5   /* would block                                */
#define ZXV_EIO        -6   /* the underlying device or store failed      */
#define ZXV_ENOENT     -7   /* no such object                             */
#define ZXV_EABI       -8   /* ABI version mismatch                       */

typedef struct {
    uint32_t    nr;
    const char *name;
    uint32_t    capability;   /* ZSC_CAP_* required to make the call */
    uint8_t     arity;
    const char *doc;
    bool        reserved;     /* a permanent hole: never dispatch it */
} zxv_syscall_info_t;

/* Look up a syscall. Returns false for numbers outside the ABI — which is
 * different from a reserved hole INSIDE it, and the caller can tell them
 * apart via `reserved`. */
bool zxv_syscall_info(uint32_t nr, zxv_syscall_info_t *out);

/* How many numbers the ABI defines (including reserved holes). */
uint32_t zxv_syscall_count(void);

/* The permission decision, in ONE place. Returns ZXV_OK, ZXV_ENOSYS for an
 * unknown or reserved number, or ZXV_EPERM when `caps` lacks what the call
 * requires. Every architecture's dispatcher calls this rather than writing its
 * own switch, so the arches cannot drift apart on who may do what. */
int zxv_syscall_permit(uint32_t nr, uint32_t caps);

/* Does this kernel satisfy a caller built against `want` (a ZXV_ABI_VERSION)?
 * Same MAJOR and a MINOR at least as new. A caller from the future is refused
 * rather than served a surface it will misread. */
bool zxv_abi_compatible(uint32_t want);

/* Internal consistency: every non-reserved call has a name, a capability that
 * is a legal bit, and an arity within range; numbering is dense and ascending.
 * Returns the number of problems found (0 = healthy). */
uint32_t zxv_syscall_selfcheck(void);

#endif /* ZXV_SYSCALL_H */
