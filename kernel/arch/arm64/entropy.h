/* entropy.h — ARM64 entropy source (FEAT_RNG / RNDR)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ARM64_ENTROPY_H
#define ZXV_ARM64_ENTROPY_H

#include <stdint.h>
#include <stdbool.h>

/* True if the CPU implements FEAT_RNG (the RNDR instruction). */
bool arm64_rng_available(void);

/* Get one 64-bit hardware random value (RNDR + health test + retries).
 * Returns false if FEAT_RNG is absent or the source looks unhealthy. */
bool arm64_rng_get64(uint64_t *out);

/* Fill `len` bytes from RNDR. True only if every byte is hardware entropy. */
bool arm64_rng_fill(uint8_t *buf, uint32_t len);

#endif /* ZXV_ARM64_ENTROPY_H */
