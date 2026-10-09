/* record.h — the TLS 1.3 record layer (RFC 8446 section 5)
 *
 * This is the framing every other TLS message travels inside. It is short,
 * and almost all of its difficulty is in four details that are silent when
 * wrong:
 *
 *  1. THE AAD IS THE RECORD HEADER ITSELF. RFC 8446 5.2 says the additional
 *     authenticated data for the AEAD is the five bytes of the TLSCiphertext
 *     header — opaque_type, legacy_record_version, and the length of the
 *     ENCRYPTED record (which includes the tag). Using the plaintext length,
 *     or omitting the AAD, produces records a peer silently discards.
 *
 *  2. THE REAL CONTENT TYPE IS INSIDE THE ENCRYPTION. The outer type is
 *     always 23 (application_data) once records are protected; the true type
 *     is the last non-zero byte of the decrypted TLSInnerPlaintext, after any
 *     zero padding. A decoder that trusts the outer type sees every handshake
 *     message as application data.
 *
 *  3. THE SEQUENCE NUMBER IS PER-KEY AND RESETS WHEN KEYS CHANGE. Read and
 *     write have separate counters. Carrying a counter across a key change,
 *     or sharing one between directions, produces a nonce the peer does not
 *     expect — and, worse, can repeat a nonce under a new key.
 *
 *  4. A ZERO-LENGTH INNER PLAINTEXT IS AN ERROR, not an empty message. A
 *     record whose decrypted content is all padding has no content type; RFC
 *     8446 requires the connection be torn down rather than guessing.
 *
 * On limits: a protected record's payload may not exceed 2^14 + 256 octets.
 * The bound is enforced on both send and receive, so a peer cannot make this
 * layer allocate or copy beyond its fixed buffers.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TLS slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_RECORD_H
#define ZXV_TLS_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include "aead.h"

#define TLS_REC_HDR_LEN     5u
#define TLS_MAX_PLAINTEXT   16384u          /* 2^14                        */
#define TLS_MAX_CIPHERTEXT  (16384u + 256u) /* 2^14 + 256, RFC 8446 5.2    */
#define TLS_REC_BUF         (TLS_REC_HDR_LEN + TLS_MAX_CIPHERTEXT)

/* ContentType (RFC 8446 B.1) */
#define TLS_CT_CHANGE_CIPHER_SPEC 20u
#define TLS_CT_ALERT              21u
#define TLS_CT_HANDSHAKE          22u
#define TLS_CT_APPLICATION_DATA   23u

/* record-layer outcomes */
#define TLS_REC_OK          0
#define TLS_REC_NEED_MORE   1   /* not a whole record yet — read more bytes */
#define TLS_REC_BAD         (-1)
#define TLS_REC_AUTH_FAIL   (-2)
#define TLS_REC_SKIP        2   /* a legacy change_cipher_spec: ignore it   */

/* One direction's protection state. Read and write each get one, and each
 * owns its own sequence number. */
typedef struct {
    uint8_t  key[CHACHA20_KEY_LEN];
    uint8_t  iv[CHACHA20_NONCE_LEN];
    uint64_t seq;
    bool     active;      /* false until keys are installed: records pass in clear */
} tls_keys_t;

/* Install traffic keys and RESET the sequence number. Every key change must go
 * through this, because a carried-over sequence number can repeat a nonce
 * under the new key. */
void tls_keys_set(tls_keys_t *k,
                  const uint8_t key[CHACHA20_KEY_LEN],
                  const uint8_t iv[CHACHA20_NONCE_LEN]);
void tls_keys_clear(tls_keys_t *k);

/* Write one record. `type` is the REAL content type; if `k` is active the
 * record is protected and the outer type becomes application_data.
 * Returns the total bytes written to `out`, or 0 on error. */
uint32_t tls_record_write(tls_keys_t *k, uint8_t type,
                          const uint8_t *payload, uint32_t payload_len,
                          uint8_t *out, uint32_t cap);

/* Read one record from a byte stream.
 *
 *   *consumed   bytes of `in` used (0 when more data is needed)
 *   *out_type   the REAL content type (from inside the encryption when protected)
 *   out/out_len the payload
 *
 * Returns one of the TLS_REC_* codes. TLS_REC_NEED_MORE is not an error. */
int tls_record_read(tls_keys_t *k,
                    const uint8_t *in, uint32_t in_len, uint32_t *consumed,
                    uint8_t *out_type, uint8_t *out, uint32_t cap, uint32_t *out_len);

#endif /* ZXV_TLS_RECORD_H */
