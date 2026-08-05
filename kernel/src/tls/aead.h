/* aead.h — ChaCha20-Poly1305 AEAD (RFC 8439) and the TLS 1.3 record nonce
 *
 * WHY THIS CIPHER
 * ---------------
 * ZXV already has AES-256-GCM, but it is table-driven, and a table-driven AES
 * in a kernel leaks key material through cache timing to anything that shares
 * the cache. ChaCha20 and Poly1305 are add-rotate-xor and integer arithmetic
 * only: there is no data-dependent table lookup and no data-dependent branch
 * anywhere in this file, so the timing is constant by construction rather
 * than by careful avoidance. It is also the fastest option without AES-NI,
 * which a freestanding kernel on unknown hardware cannot count on.
 *
 * TLS 1.3 lists TLS_AES_128_GCM_SHA256 as mandatory, so a client offering
 * only TLS_CHACHA20_POLY1305_SHA256 can meet a server that refuses it. That
 * is a stated limitation, not an oversight — see tls13_record.h.
 *
 * THE TWO WAYS THIS PRIMITIVE IS USUALLY GOT WRONG
 * ------------------------------------------------
 *  1. NONCE REUSE. Encrypting two different plaintexts under the same key and
 *     nonce does not merely leak the plaintexts — it leaks the Poly1305 key,
 *     and then anything can be forged. TLS 1.3 derives each record's nonce by
 *     XORing the sequence number into a per-connection IV, so this is a
 *     structural property, not a discipline. tls13_record_nonce() implements
 *     exactly that, and the sequence number is owned by the record layer.
 *  2. VERIFYING THE TAG WITH memcmp. A byte-at-a-time comparison returns
 *     early on the first difference, which tells an attacker where it is and
 *     lets a tag be forged one byte at a time. Decryption here compares in
 *     constant time and returns false; on failure the plaintext buffer is
 *     wiped rather than handed back, so a caller cannot use unauthenticated
 *     data by forgetting to check a return value.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TLS slice)
 * License: SEL-3.3
 */
#ifndef ZXV_AEAD_H
#define ZXV_AEAD_H

#include <stdint.h>
#include <stdbool.h>

#define CHACHA20_KEY_LEN   32u
#define CHACHA20_NONCE_LEN 12u
#define CHACHA20_BLOCK     64u
#define POLY1305_TAG_LEN   16u
#define POLY1305_KEY_LEN   32u

/* One ChaCha20 block: the 64 bytes of keystream for `counter`. Exposed
 * because RFC 8439 publishes block-level test vectors, and because TLS's
 * Poly1305 key is block 0 of the same keystream. */
void chacha20_block(const uint8_t key[CHACHA20_KEY_LEN],
                    uint32_t counter,
                    const uint8_t nonce[CHACHA20_NONCE_LEN],
                    uint8_t out[CHACHA20_BLOCK]);

/* XOR `len` bytes of keystream, starting at block `counter`, over `in`.
 * Encryption and decryption are the same operation. */
void chacha20_xor(const uint8_t key[CHACHA20_KEY_LEN],
                  uint32_t counter,
                  const uint8_t nonce[CHACHA20_NONCE_LEN],
                  const uint8_t *in, uint8_t *out, uint32_t len);

/* Poly1305 one-shot MAC (RFC 8439 section 2.5). `key` is one-time: reusing it
 * across two messages reveals it. */
void poly1305(const uint8_t key[POLY1305_KEY_LEN],
              const uint8_t *msg, uint32_t len,
              uint8_t tag[POLY1305_TAG_LEN]);

/* AEAD_CHACHA20_POLY1305 (RFC 8439 section 2.8).
 * `out` receives `len` ciphertext bytes; `tag` receives 16 bytes.
 * `out` may alias `in`. */
void aead_seal(const uint8_t key[CHACHA20_KEY_LEN],
               const uint8_t nonce[CHACHA20_NONCE_LEN],
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *in, uint8_t *out, uint32_t len,
               uint8_t tag[POLY1305_TAG_LEN]);

/* Returns false if the tag does not authenticate. On failure `out` is ZEROED:
 * unauthenticated plaintext is never left where a caller who ignored the
 * return value could read it. */
bool aead_open(const uint8_t key[CHACHA20_KEY_LEN],
               const uint8_t nonce[CHACHA20_NONCE_LEN],
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *in, uint8_t *out, uint32_t len,
               const uint8_t tag[POLY1305_TAG_LEN]);

/* TLS 1.3 per-record nonce (RFC 8446 section 5.3): the 64-bit sequence number
 * is left-padded to the IV length and XORed with the static IV. */
void tls13_record_nonce(const uint8_t iv[CHACHA20_NONCE_LEN], uint64_t seq,
                        uint8_t out[CHACHA20_NONCE_LEN]);

#endif /* ZXV_AEAD_H */
