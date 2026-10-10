/* boot_evidence.h — Boot evidence measurement system
 *
 * Each boot message gets a sequential evidence ID and contributes to
 * a running SHA-256 hash. At boot completion, the final measurement
 * is emitted. This makes the boot log self-authenticating: a reviewer
 * can verify that the boot sequence matches expected evidence.
 *
 * Tags in the boot log:
 *   [DRIVER ONLINE]  — real hardware driver initialized
 *   [STRUCTURE READY] — data structures allocated, no hardware touched
 *   [SIMULATED]      — simulation/stub, not real hardware
 *   [SKIP]           — subsystem skipped (config-gated)
 *   [INITIALIZED]    — subsystem logic initialized (no hardware claim)
 *   [REGISTERED]     — types/formats registered (data model only)
 *
 * F8 fix: Each boot_msg now emits an evidence ID (E0001, E0002, ...)
 * and contributes to a running measurement hash.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef BOOT_EVIDENCE_H
#define BOOT_EVIDENCE_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the boot evidence system */
void boot_evidence_init(void);

/* Record a boot message and return its evidence ID.
 * Returns the sequential ID (1, 2, 3, ...). */
uint32_t boot_evidence_record(const char *msg);

/* Get the current evidence ID counter */
uint32_t boot_evidence_count(void);

/* Emit the final boot measurement hash (32 bytes as hex) */
void boot_evidence_final(void);

/* Get the raw 32-byte measurement hash */
void boot_evidence_get_hash(uint8_t out[32]);

#endif /* BOOT_EVIDENCE_H */
