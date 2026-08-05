/* user_procs.c — Two simple EL0 user-space test programs
 *
 * These are compiled as kernel functions but their code is copied
 * to user-space pages and executed at EL0. They use SVC to make
 * syscalls (write to UART, get PID, yield, exit).
 *
 * Each program writes its PID to the UART, yields a few times,
 * and then exits. The timer IRQ will preempt them so they interleave.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

#include "el0_userspace.h"

/* SVC wrapper macros — these generate the SVC instruction inline.
 * The syscall number goes in x8, return value comes back in x0. */
#define SVC_0(num) \
    __asm__ volatile("mov x8, %0\n" "svc #0\n" :: "r"((uint64_t)(num)) : "x0", "x8")

#define SVC_1(num, arg0) \
    __asm__ volatile("mov x8, %0\n" "mov x0, %1\n" "svc #0\n" \
        :: "r"((uint64_t)(num)), "r"((uint64_t)(arg0)) : "x0", "x8")

/* Syscall numbers — must match el0_userspace.c */
#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_GETPID  3
#define SYS_YIELD   4

/* User process A: print PID, yield 3 times, write 'A' chars, exit */
void user_proc_a_entry(void) {
    /* Get PID */
    uint64_t pid = 0;
    __asm__ volatile("mov x8, %0\n" "svc #0\n" "mov %1, x0\n"
        :: "r"((uint64_t)SYS_GETPID), "r"(pid) : "x0", "x8");

    /* Write 'A' to UART 5 times with yields */
    for (int i = 0; i < 5; i++) {
        SVC_1(SYS_WRITE, (uint64_t)'A');
        SVC_0(SYS_YIELD);
    }

    /* Exit */
    SVC_1(SYS_EXIT, 0);

    /* Should not reach here */
    while (1) { }
}

/* User process B: print PID, yield 3 times, write 'B' chars, exit */
void user_proc_b_entry(void) {
    /* Get PID */
    uint64_t pid = 0;
    __asm__ volatile("mov x8, %0\n" "svc #0\n" "mov %1, x0\n"
        :: "r"((uint64_t)SYS_GETPID), "r"(pid) : "x0", "x8");

    /* Write 'B' to UART 5 times with yields */
    for (int i = 0; i < 5; i++) {
        SVC_1(SYS_WRITE, (uint64_t)'B');
        SVC_0(SYS_YIELD);
    }

    /* Exit */
    SVC_1(SYS_EXIT, 0);

    /* Should not reach here */
    while (1) { }
}

/* CPU-bound preemption test: two processes that never call SYS_YIELD.
 * The timer IRQ is the only reason they alternate. */
#if ENABLE_PREEMPT_TEST
void user_proc_spin_a_entry(void) {
    for (int i = 0; i < 20; i++) {
        SVC_1(SYS_WRITE, (uint64_t)'A');
        for (volatile uint64_t j = 0; j < 10000000; j++) { }
    }
    SVC_1(SYS_EXIT, 0);
    while (1) { }
}

void user_proc_spin_b_entry(void) {
    for (int i = 0; i < 20; i++) {
        SVC_1(SYS_WRITE, (uint64_t)'B');
        for (volatile uint64_t j = 0; j < 10000000; j++) { }
    }
    SVC_1(SYS_EXIT, 0);
    while (1) { }
}
#endif

/* Syscall wrappers WITH return values — needed by the shell loop.
 * (The SVC_0/SVC_1 macros above discard x0; these capture it.)
 * MUST be always_inline: proc_create() copies exactly 4KB of the entry
 * function's code into the user page; an out-of-line helper would be a
 * branch to an unmapped kernel address and fault at EL0. */
#define SYS_READ_NUM   10
#define SYS_EXEC_NUM   11
#define SYS_SLEEP_NUM  5

__attribute__((always_inline))
static inline uint64_t svc_read(void) {
    register uint64_t x8 __asm__("x8") = SYS_READ_NUM;
    register uint64_t x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

__attribute__((always_inline))
static inline void svc_write(uint64_t c) {
    register uint64_t x8 __asm__("x8") = 2; /* SYS_WRITE */
    register uint64_t x0 __asm__("x0") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
}

__attribute__((always_inline))
static inline void svc_sleep(uint64_t ms) {
    register uint64_t x8 __asm__("x8") = SYS_SLEEP_NUM;
    register uint64_t x0 __asm__("x0") = ms;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
}

__attribute__((always_inline))
static inline uint64_t svc_exec(const char *buf, uint64_t len) {
    register uint64_t x8 __asm__("x8") = SYS_EXEC_NUM;
    register uint64_t x0 __asm__("x0") = (uint64_t)buf;
    register uint64_t x1 __asm__("x1") = len;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

/* P-TERM user process: the real interactive shell loop at EL0.
 *
 * Fully self-contained: the line buffer lives on the user stack, all
 * characters are immediates, and every kernel interaction is a syscall
 * — no kernel .rodata/.bss references (those pages are not mapped in
 * the user address space).
 *
 * Loop: poll SYS_READ (non-blocking); SYS_SLEEP between polls so the
 * CPU isn't burned spinning; echo; handle backspace; on Enter, pass
 * the line to the kernel P-TERM engine via SYS_EXEC.  'exit' leaves
 * the shell (checked user-side, char-by-char). */
void pterm_user_entry(void) {
    char line[128];
    uint64_t len = 0;

    /* Banner + prompt: "\r\nP-TERM/EL0\r\n> " */
    svc_write('\r'); svc_write('\n');
    svc_write('P'); svc_write('-'); svc_write('T'); svc_write('E');
    svc_write('R'); svc_write('M'); svc_write('/'); svc_write('E');
    svc_write('L'); svc_write('0');
    svc_write('\r'); svc_write('\n');
    svc_write('>'); svc_write(' ');

    for (;;) {
        uint64_t c = svc_read();
        if (c == 0) {
            /* No input pending — sleep one tick's worth and poll again.
             * The timer keeps the kernel event cycle running meanwhile. */
            svc_sleep(10);
            continue;
        }

        if (c == '\r' || c == '\n') {
            svc_write('\r'); svc_write('\n');
            if (len == 4 &&
                line[0] == 'e' && line[1] == 'x' &&
                line[2] == 'i' && line[3] == 't') {
                break;
            }
            if (len > 0) {
                svc_exec(line, len);
                len = 0;
            }
            svc_write('>'); svc_write(' ');
            continue;
        }

        if (c == 0x7F || c == 0x08) {          /* backspace / DEL */
            if (len > 0) {
                len--;
                svc_write(0x08); svc_write(' '); svc_write(0x08);
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7F && len < sizeof(line) - 1) {
            line[len++] = (char)c;
            svc_write(c);                       /* echo */
        }
    }

    /* "bye\r\n" then exit */
    svc_write('b'); svc_write('y'); svc_write('e');
    svc_write('\r'); svc_write('\n');
    SVC_1(SYS_EXIT, 0);

    /* Should not reach here */
    while (1) { }
}

/* ---- Negative privilege tests (W^X adversarial processes) ----
 *
 * Build with ENABLE_WX_TEST=1.  Each process deliberately violates the
 * W^X policy and MUST be terminated by the kernel with an [EL0 FAULT]
 * while sibling processes (the P-TERM shell) keep running.  If the
 * trailing '!' ever appears on the console, the privilege boundary is
 * broken and the test has failed. */
#if ENABLE_WX_TEST
/* Attempt to WRITE to the process's own code page (mapped RX).
 * Expected: Data Abort (lower EL) → process terminated. */
void user_proc_wx_write_entry(void) {
    svc_write('W'); svc_write('1'); svc_write('?');
    *(volatile uint32_t *)0x00010000ULL = 0xDEADBEEF;
    svc_write('!');   /* MUST NOT print — W^X violated if it does */
    SVC_1(SYS_EXIT, 1);
    while (1) { }
}

/* Attempt to EXECUTE from the stack region (mapped RW + UXN).
 * Expected: Instruction Abort (lower EL) → process terminated. */
void user_proc_wx_exec_entry(void) {
    svc_write('W'); svc_write('2'); svc_write('?');
    ((void (*)(void))0x00080000ULL)();
    svc_write('!');   /* MUST NOT print — NX stack violated if it does */
    SVC_1(SYS_EXIT, 1);
    while (1) { }
}
#endif
