/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* entropy_jitter.c — the architectural jitter harvester (entropy_jitter.h).
 *
 * Every substrate reads its own cycle counter with the tightest available
 * read (CNTPCT_EL0 on AArch64, rdtsc on x86, rdcycle on RISC-V, CNTPCT on
 * ARM32), then samples it hundreds of times at varying delays and offsets,
 * XOR-mixing the low bits (the jitter) with uninitialised stack RAM and the
 * running pool. The pool is finally diffused through SHAKE256 so every
 * output byte depends on every pool bit. A volatile counter stands in where
 * the substrate has no counter at all (wasm32: the host owns time).
 *
 * Freestanding: no libc, no allocation, no floats. */
#include "entropy_jitter.h"
#include <stddef.h>

extern void shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);

/* The architecture's finest-grained cycle counter. */
static uint64_t cyc(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(v));
    return v;
#elif defined(__x86_64__) || defined(__i386__) || defined(__i686__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
#elif defined(__riscv)
    uint64_t v;
    __asm__ volatile("rdcycle %0" : "=r"(v));
    return v;
#elif defined(__arm__)
    uint64_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
#else
    /* wasm32 and the hosted shell: no counter; a volatile ladder the host
     * can perturb, plus whatever the caller mixes in. */
    static volatile uint64_t s;
    return (s += 0x9E3779B97F4A7C15ull);
#endif
}

void entropy_jitter_fill(uint8_t *out, uint32_t len)
{
    uint8_t pool[64];
    volatile uint8_t scratch[128]; /* uninitialised RAM: whatever the boot left */
    uint32_t i;

    for (i = 0; i < 64; i++) pool[i] = (uint8_t) (i * 31u + 7u);
    for (i = 0; i < 512; i++) {
        uint64_t t = cyc();
        uint8_t s = scratch[(i * 7u + (uint8_t) t) % sizeof scratch];
        pool[i % 64] ^= (uint8_t) (t >> ((i & 7u) * 8u)) ^ s ^ (uint8_t) (i * 37u);
        pool[(i + 13u) % 64] ^= (uint8_t) (t >> 32);
        /* a delay that varies with the sample so consecutive reads jitter */
        for (volatile uint32_t d = 0; d < (i & 7u) * 5u + 1u; d++) {
        }
    }
    shake256(pool, sizeof pool, out, len);
}