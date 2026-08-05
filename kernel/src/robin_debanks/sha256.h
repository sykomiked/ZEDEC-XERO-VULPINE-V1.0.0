/* sha256.h — Freestanding SHA-256 (FIPS 180-4)
 *
 * Self-contained, dependency-free, single-shot SHA-256 digest.
 * No libc dependency beyond stdint.h/stddef.h.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_LEN 32

/* One-shot digest: hashes `len` bytes of `data`, writes 32-byte digest to out. */
void sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

/* Streaming digest.
 *
 * This exists because a one-shot-only hash forces every caller that must
 * cover a large or piecewise message to stage it in a fixed buffer first —
 * and two HMAC implementations in this tree did exactly that and then
 * SILENTLY TRUNCATED anything past 256 bytes, so a MAC covered only a prefix
 * of what it claimed to authenticate. With streaming there is no staging
 * buffer, so there is no cap to get wrong.
 *
 * Usage: sha256_init, sha256_update (any number of times, any sizes),
 * sha256_final. */
typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    size_t   buf_len;
    uint64_t total;      /* message length in BYTES so far */
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *c);
void sha256_update(sha256_ctx_t *c, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *c, uint8_t out[SHA256_DIGEST_LEN]);

#endif /* SHA256_H */
