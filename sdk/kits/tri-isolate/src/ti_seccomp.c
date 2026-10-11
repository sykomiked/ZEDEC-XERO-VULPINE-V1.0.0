/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_seccomp.c — the worker's optional seccomp filter (Linux x86-64 and
 * arm64 only).
 *
 * The worker installs it after start-up (libzxv context, Z-Wasm host) and
 * before it reads the first byte of a plugin. From then on the worker may
 * only:
 *
 *   read          from fd 3 (the socket to the host)
 *   write         to fd 3, and to fd 2 (stderr: /dev/null unless the host
 *                 asked to keep it; sanitizer reports go there)
 *   close
 *   mmap, mprotect without PROT_EXEC; munmap, mremap, brk, madvise
 *                 (memory for wasm3 and malloc; wasm3 is an interpreter
 *                 and never needs executable memory)
 *   futex, sched_yield, getrandom, clock_gettime, gettid, getpid,
 *   rt_sigreturn, rt_sigprocmask, sigaltstack (the sanitizer runtimes use
 *                 it as the worker exits)
 *   exit, exit_group
 *
 * Anything else kills the whole worker (SECCOMP_RET_KILL_PROCESS, or
 * SECCOMP_RET_KILL on kernels headers that predate it): no open, no
 * socket, no exec, no fork, no kill, no ptrace. A filter for another
 * architecture's system-call table (e.g. x32 or i386 calls on x86-64) also
 * kills. PR_SET_NO_NEW_PRIVS is set first, so no unprivileged process
 * needs extra rights to install it. */
#include "ti_seccomp.h"

#include "zxv_api.h"

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define TI_HAVE_SECCOMP 1
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#if defined(__x86_64__)
#define TI_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#define TI_AUDIT_ARCH AUDIT_ARCH_AARCH64
#endif

#ifdef SECCOMP_RET_KILL_PROCESS
#define TI_RET_KILL SECCOMP_RET_KILL_PROCESS
#else
#define TI_RET_KILL SECCOMP_RET_KILL
#endif

#define LD_NR    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr))
#define LD_ARG(k) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args) + 8u * (k))
#define ALLOW    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
#define KILL     BPF_STMT(BPF_RET | BPF_K, TI_RET_KILL)
/* if (A == nr) fall through to the next instruction, else skip one */
#define IS(nr)   BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1)

int32_t ti_seccomp_available(void) { return 1; }

int32_t ti_seccomp_install(void) {
    struct sock_filter f[] = {
        /* the architecture must be ours */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, TI_AUDIT_ARCH, 1, 0),
        KILL,
        LD_NR,
#if defined(__x86_64__)
        /* x32 calls carry bit 30 */
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000u, 0, 1),
        KILL,
#endif
        /* read(fd 3) */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_read, 0, 4),
        LD_ARG(0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 3, 0, 1),
        ALLOW,
        KILL,
        /* write(fd 3 or fd 2) */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0, 5),
        LD_ARG(0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 3, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 2, 0, 1),
        ALLOW,
        KILL,
        /* mmap / mprotect without PROT_EXEC (arg 2) */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_mmap, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_mprotect, 0, 4),
        LD_ARG(2),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 1, 0),
        ALLOW,
        KILL,
        /* the rest: allowed whatever the arguments */
        IS(SYS_close), ALLOW,
        IS(SYS_munmap), ALLOW,
        IS(SYS_mremap), ALLOW,
        IS(SYS_brk), ALLOW,
        IS(SYS_madvise), ALLOW,
        IS(SYS_futex), ALLOW,
        IS(SYS_sched_yield), ALLOW,
        IS(SYS_getrandom), ALLOW,
        IS(SYS_clock_gettime), ALLOW,
        IS(SYS_gettid), ALLOW,
        IS(SYS_getpid), ALLOW,
        IS(SYS_rt_sigreturn), ALLOW,
        IS(SYS_rt_sigprocmask), ALLOW,
        IS(SYS_sigaltstack), ALLOW,
        IS(SYS_exit), ALLOW,
        IS(SYS_exit_group), ALLOW,
        KILL,
    };
    struct sock_fprog prog = {(unsigned short) (sizeof f / sizeof f[0]), f};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return ZXV_E_STATE;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) != 0)
        return errno == EINVAL ? ZXV_E_NOTIMPL : ZXV_E_STATE;
    return ZXV_OK;
}

#else

int32_t ti_seccomp_available(void) { return 0; }
int32_t ti_seccomp_install(void) { return ZXV_E_NOTIMPL; }

#endif
