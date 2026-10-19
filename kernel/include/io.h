/* io.h — the port-I/O abstraction, ONE definition for every target.
 *
 * The x86 IN/OUT instructions are x86-only: the same call on riscv64,
 * riscv32, arm32, arm64 or wasm32 is a compile error. This header gives
 * the real instructions on x86 and a deterministic stub elsewhere, so a
 * driver compiles on every target. The stub returns a defined value
 * (0xFF / the "no device" convention) and writes nowhere — explicit
 * absence, never a silent wrong read of real hardware.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_IO_H
#define ZXV_IO_H

#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0));
}

#else

static inline void outb(uint16_t port, uint8_t val) { (void) port; (void) val; }
static inline uint8_t inb(uint16_t port) { (void) port; return 0xFF; }
static inline void outw(uint16_t port, uint16_t val) { (void) port; (void) val; }
static inline uint16_t inw(uint16_t port) { (void) port; return 0xFFFF; }
static inline void outl(uint16_t port, uint32_t val) { (void) port; (void) val; }
static inline uint32_t inl(uint16_t port) { (void) port; return 0xFFFFFFFFu; }
static inline void io_wait(void) { }

#endif

#endif /* ZXV_IO_H */
