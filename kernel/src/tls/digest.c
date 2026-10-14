/* digest.c — SHA-2 dispatch. See digest.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "digest.h"
#include "sha512.h"
#include "../robin_debanks/sha256.h"

uint32_t tls_digest_len(tls_hash_t h)
{
    switch (h) {
    case TLS_HASH_SHA256:
        return 32;
    case TLS_HASH_SHA384:
        return 48;
    case TLS_HASH_SHA512:
        return 64;
    default:
        return 0;
    }
}

uint32_t tls_digest3(tls_hash_t h, const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen,
                     const uint8_t *c, uint32_t clen, uint8_t *out)
{
    if (h == TLS_HASH_SHA256) {
        sha256_ctx_t s;
        sha256_init(&s);
        if (alen) sha256_update(&s, a, alen);
        if (blen) sha256_update(&s, b, blen);
        if (clen) sha256_update(&s, c, clen);
        sha256_final(&s, out);
        return 32;
    }
    if (h == TLS_HASH_SHA384 || h == TLS_HASH_SHA512) {
        tls_sha512_ctx_t s;
        if (h == TLS_HASH_SHA384)
            tls_sha384_init(&s);
        else
            tls_sha512_init(&s);
        if (alen) tls_sha512_update(&s, a, alen);
        if (blen) tls_sha512_update(&s, b, blen);
        if (clen) tls_sha512_update(&s, c, clen);
        tls_sha512_final(&s, out);
        return s.out_len;
    }
    return 0;
}

uint32_t tls_digest(tls_hash_t h, const uint8_t *data, uint32_t len, uint8_t *out)
{
    return tls_digest3(h, data, len, 0, 0, 0, 0, out);
}
