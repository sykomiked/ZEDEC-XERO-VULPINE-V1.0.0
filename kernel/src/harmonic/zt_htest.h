/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_htest.h — test harness for kernel/src/harmonic (tests only).
 *
 * Two builds:
 *   -DTEST_HOST       hosted: stdio output, clock_gettime timing.
 *   -DZT_HTEST_BARE   no libc at all: a _start entry and raw Linux write/exit
 *                     system calls (i386, powerpc, x86_64, aarch64), so the
 *                     same tests run as 32-bit and as big-endian binaries
 *                     (qemu-ppc). Timing reads 0 there and timing bounds are
 *                     skipped.
 */
#ifndef ZT_HTEST_H
#define ZT_HTEST_H

#if !defined(ZT_HTEST_BARE) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 199309L /* clock_gettime */
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static int ht_fail = 0, ht_pass = 0;

#if defined(ZT_HTEST_BARE)
static inline long ht_sys3(long n, long a, long b, long c)
{
#    if defined(__i386__)
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
#    elif defined(__powerpc__)
    register long r0 __asm__("r0") = n;
    register long r3 __asm__("r3") = a;
    register long r4 __asm__("r4") = b;
    register long r5 __asm__("r5") = c;
    __asm__ volatile("sc"
                     : "+r"(r0), "+r"(r3), "+r"(r4), "+r"(r5)
                     :
                     : "memory", "cr0", "r6", "r7", "r8", "r9", "r10", "r11", "r12");
    return r3;
#    elif defined(__x86_64__)
    long r;
    long nr = n == 4 ? 1 : 60; /* write, exit */
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
#    else
#        error "ZT_HTEST_BARE: add this architecture's system call"
#    endif
}
static inline void ht_write(const char *s, size_t n)
{
    ht_sys3(4, 1, (long) s, (long) n);
}
static inline uint64_t ht_now_ns(void)
{
    return 0;
}
#    define HT_TIMED 0
/* The compiler may call these even in freestanding code (C11 5.1.2.1 leaves
 * them to the environment); other kernel modules linked into a test use them.
 * The harmonic module itself is checked for no undefined symbols separately. */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *) d;
    while (n--) *p++ = (unsigned char) c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *) d;
    const unsigned char *q = (const unsigned char *) s;
    while (n--) *p++ = *q++;
    return d;
}
int main(void);
void _start(void);
void _start(void)
{
    ht_sys3(1, main(), 0, 0);
    for (;;) {
    }
}
#else
#    include <stdio.h>
#    include <time.h>
static inline void ht_write(const char *s, size_t n)
{
    fwrite(s, 1, n, stdout);
}
static inline uint64_t ht_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}
#    define HT_TIMED 1
#endif

static inline void ht_puts(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    ht_write(s, n);
}

static inline void ht_puthex(uint32_t v, int digits);
static inline void ht_putd(int64_t v)
{
    char b[16];
    int i = 15;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (u >> 32) { /* no 64-bit division in the bare build: print hex */
        ht_puts(neg ? "-0x" : "0x");
        ht_puthex((uint32_t) (u >> 32), 8);
        ht_puthex((uint32_t) u, 8);
        return;
    }
    uint32_t w = (uint32_t) u;
    b[i] = 0;
    do {
        b[--i] = (char) ('0' + w % 10u);
        w /= 10u;
    } while (w);
    if (neg) b[--i] = '-';
    ht_puts(b + i);
}

static inline void ht_puthex(uint32_t v, int digits)
{
    char b[9];
    for (int i = 0; i < digits; i++) {
        uint32_t d = (v >> (4 * (digits - 1 - i))) & 0xFu;
        b[i] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    b[digits] = 0;
    ht_puts(b);
}

#define HT_CHECK(cond, msg)                                                                        \
    do {                                                                                           \
        if (cond) {                                                                                \
            ht_pass++;                                                                             \
        } else {                                                                                   \
            ht_fail++;                                                                             \
            ht_puts("  FAIL: ");                                                                   \
            ht_puts(msg);                                                                          \
            ht_puts("\n");                                                                         \
        }                                                                                          \
    } while (0)

static inline int ht_finish(const char *name)
{
    ht_puts(name);
    ht_puts(": ");
    ht_putd(ht_pass);
    ht_puts(" passed, ");
    ht_putd(ht_fail);
    ht_puts(" failed\n");
    return ht_fail ? 1 : 0;
}

#endif /* ZT_HTEST_H */
