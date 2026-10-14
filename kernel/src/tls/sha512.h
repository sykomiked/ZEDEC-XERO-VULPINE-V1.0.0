/* sha512.h — SHA-512 and SHA-384 (FIPS 180-4)
 *
 * The certificate verifier needs both: P-384 certificates are signed with
 * ecdsa-with-SHA384, many RSA roots and intermediates with sha384WithRSA, and
 * TLS 1.3 offers rsa_pss_rsae_sha384/512. SHA-384 is SHA-512 with different
 * initial values and a truncated output, so one compression function serves
 * both.
 *
 * The names carry a tls_ prefix because robin_debanks/ed25519_verify.c
 * already links the vendored orlp SHA-512 (which has no SHA-384) under the
 * plain names into the same kernel images.
 *
 * Freestanding: integer only, no libc, no allocation. Streaming, so a caller
 * never stages a message in a fixed buffer.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_SHA512_H
#define ZXV_TLS_SHA512_H

#include <stdint.h>

#define SHA512_DIGEST_LEN 64u
#define SHA384_DIGEST_LEN 48u

typedef struct {
    uint64_t h[8];
    uint8_t buf[128];
    uint32_t buf_len;
    uint64_t total; /* message length in bytes so far */
    uint32_t out_len;
} tls_sha512_ctx_t;

void tls_sha512_init(tls_sha512_ctx_t *c);
void tls_sha384_init(tls_sha512_ctx_t *c);
void tls_sha512_update(tls_sha512_ctx_t *c, const uint8_t *data, uint32_t len);
/* Writes c->out_len bytes: 64 after tls_sha512_init, 48 after tls_sha384_init. */
void tls_sha512_final(tls_sha512_ctx_t *c, uint8_t *out);

void tls_sha512(const uint8_t *data, uint32_t len, uint8_t out[SHA512_DIGEST_LEN]);
void tls_sha384(const uint8_t *data, uint32_t len, uint8_t out[SHA384_DIGEST_LEN]);

#endif /* ZXV_TLS_SHA512_H */
