/* x86_64_arch.h — minimal x86-64 port I/O and halt helpers
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef X86_64_ARCH_H
#define X86_64_ARCH_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void hlt(void) {
    __asm__ __volatile__("hlt");
}

static inline void cli(void) {
    __asm__ __volatile__("cli");
}

static inline void sti(void) {
    __asm__ __volatile__("sti");
}

#endif
