/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* entropy_jitter.h — best-effort architectural entropy harvester.
 *
 * When a substrate has no hardware RNG (RISC-V without Zkr, ARM32 without
 * a cycle counter, x86 without RDRAND, QEMU), this seeds a pool from
 * fine-grained timer-jitter samples interleaved with uninitialised RAM and
 * diffuses it through SHAKE256. It is honest: a jitter harvester is NOT a
 * certified TRNG. It is for making network ids and dev vault keys
 * unpredictable on any substrate, never a claim of cryptographic-grade
 * entropy — callers keep their fail-closed paths. */
#ifndef ZXV_ENTROPY_JITTER_H
#define ZXV_ENTROPY_JITTER_H

#include <stdint.h>

/* Fill `len` bytes from the jitter harvester (always best-effort). */
void entropy_jitter_fill(uint8_t *out, uint32_t len);

#endif /* ZXV_ENTROPY_JITTER_H */