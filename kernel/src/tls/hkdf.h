/* hkdf.h — HMAC-SHA256, HKDF, and the TLS 1.3 key schedule
 *
 * WHY THIS EXISTS
 * ---------------
 * Everything in TLS 1.3 hangs off one derivation ladder. Get any step of it
 * wrong and the handshake fails with an opaque decrypt error that tells you
 * nothing about which step was wrong. So this layer is built and verified on
 * its own, against published test vectors, BEFORE any of it is wired to a
 * socket:
 *
 *   HMAC-SHA256    RFC 4231 test cases
 *   HKDF           RFC 5869 test cases 1-3 (including the zero-salt case,
 *                  which TLS uses for the very first Extract)
 *   Key schedule   RFC 8448 "TLS 1.3 Traces" — the byte-exact secrets from a
 *                  real recorded handshake
 *
 * Those are EXTERNAL anchors: values published by other people from other
 * implementations. A test that only checks our output against our own output
 * proves nothing, which is exactly how a stack ends up with a checksum field
 * that is always zero.
 *
 * THE LADDER (RFC 8446 section 7.1)
 * ---------------------------------
 *             0
 *             |
 *             v
 *   PSK ->  HKDF-Extract = Early Secret
 *             |
 *             +-> Derive-Secret(., "ext binder"|"res binder", "")
 *             +-> Derive-Secret(., "c e traffic", ClientHello)
 *             |
 *             v
 *       Derive-Secret(., "derived", "")
 *             |
 *             v
 *  (EC)DHE -> HKDF-Extract = Handshake Secret
 *             |
 *             +-> Derive-Secret(., "c hs traffic", CH..SH)
 *             +-> Derive-Secret(., "s hs traffic", CH..SH)
 *             |
 *             v
 *       Derive-Secret(., "derived", "")
 *             |
 *             v
 *   0 ->  HKDF-Extract = Master Secret
 *             |
 *             +-> Derive-Secret(., "c ap traffic", CH..server Finished)
 *             +-> Derive-Secret(., "s ap traffic", CH..server Finished)
 *
 * NOTE ON THE LABEL PREFIX: HKDF-Expand-Label prepends the ASCII "tls13 " to
 * every label. Omitting that prefix produces perfectly valid-looking key
 * material that no peer can reproduce — a silent, total failure. The tests
 * pin the exact encoded HkdfLabel bytes so it cannot regress.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TLS slice)
 * License: SEL-3.3
 */
#ifndef ZXV_HKDF_H
#define ZXV_HKDF_H

#include <stdint.h>
#include <stdbool.h>

#define HASH_LEN        32u     /* SHA-256 */
#define HMAC_BLOCK_LEN  64u
#define HKDF_MAX_OKM    255u    /* we never need more than a few blocks */
#define TLS_MAX_LABEL   64u

/* HMAC-SHA256 (RFC 2104). Keys longer than the block size are hashed first,
 * shorter keys are zero-padded — both are required by the standard and both
 * are exercised by the RFC 4231 cases. */
void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *msg, uint32_t msg_len,
                 uint8_t out[HASH_LEN]);

/* HKDF-Extract (RFC 5869): a salted "condense" step. A NULL/zero-length salt
 * means HashLen zero bytes, which is what TLS 1.3 passes for the first
 * Extract — getting that wrong breaks every subsequent secret. */
void hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                  const uint8_t *ikm, uint32_t ikm_len,
                  uint8_t prk[HASH_LEN]);

/* HKDF-Expand (RFC 5869). Returns false if out_len exceeds 255*HashLen. */
bool hkdf_expand(const uint8_t prk[HASH_LEN],
                 const uint8_t *info, uint32_t info_len,
                 uint8_t *out, uint32_t out_len);

/* Build the HkdfLabel structure of RFC 8446 section 7.1:
 *     uint16 length
 *     opaque label<7..255>   -- "tls13 " || label
 *     opaque context<0..255>
 * Returns the encoded length, 0 if it does not fit. Exposed so a test can
 * pin the exact bytes. */
uint32_t tls13_hkdf_label(uint8_t *out, uint32_t cap, uint16_t out_len,
                          const char *label, const uint8_t *ctx, uint32_t ctx_len);

/* HKDF-Expand-Label (RFC 8446 section 7.1). */
bool tls13_expand_label(const uint8_t secret[HASH_LEN],
                        const char *label,
                        const uint8_t *ctx, uint32_t ctx_len,
                        uint8_t *out, uint32_t out_len);

/* Derive-Secret(Secret, Label, Messages) — Expand-Label over the TRANSCRIPT
 * HASH of Messages, not over Messages. */
bool tls13_derive_secret(const uint8_t secret[HASH_LEN],
                         const char *label,
                         const uint8_t *transcript, uint32_t transcript_len,
                         uint8_t out[HASH_LEN]);

/* Same, but the caller already has the transcript hash. */
bool tls13_derive_secret_h(const uint8_t secret[HASH_LEN],
                           const char *label,
                           const uint8_t transcript_hash[HASH_LEN],
                           uint8_t out[HASH_LEN]);

/* The whole ladder, held in one place so a caller cannot skip a rung. */
typedef struct {
    uint8_t early[HASH_LEN];
    uint8_t handshake[HASH_LEN];
    uint8_t master[HASH_LEN];
    uint8_t c_hs_traffic[HASH_LEN];
    uint8_t s_hs_traffic[HASH_LEN];
    uint8_t c_ap_traffic[HASH_LEN];
    uint8_t s_ap_traffic[HASH_LEN];
    bool    have_handshake;
    bool    have_master;
} tls13_schedule_t;

/* Rung 1: Early Secret. `psk` may be NULL for a full (non-resumption)
 * handshake, in which case HashLen zero bytes are used. */
void tls13_early_secret(tls13_schedule_t *s, const uint8_t *psk, uint32_t psk_len);

/* Rung 2: Handshake Secret + the two handshake traffic secrets.
 * `transcript` is ClientHello..ServerHello. */
bool tls13_handshake_secret(tls13_schedule_t *s,
                            const uint8_t *dhe, uint32_t dhe_len,
                            const uint8_t *transcript, uint32_t transcript_len);

/* Rung 3: Master Secret + the two application traffic secrets.
 * `transcript` is ClientHello..server Finished. */
bool tls13_master_secret(tls13_schedule_t *s,
                         const uint8_t *transcript, uint32_t transcript_len);

/* Turn a traffic secret into the record-protection key and IV. */
bool tls13_traffic_keys(const uint8_t traffic_secret[HASH_LEN],
                        uint8_t *key, uint32_t key_len,
                        uint8_t *iv, uint32_t iv_len);

/* The Finished message verify_data: HMAC over the transcript hash, keyed by
 * a "finished" key derived from the traffic secret. */
bool tls13_finished(const uint8_t traffic_secret[HASH_LEN],
                    const uint8_t transcript_hash[HASH_LEN],
                    uint8_t out[HASH_LEN]);

/* Constant-time comparison. Any comparison of a MAC, a tag or verify_data
 * MUST use this: a byte-at-a-time compare leaks where the first difference
 * is, and that is enough to forge one byte at a time. */
bool ct_equal(const uint8_t *a, const uint8_t *b, uint32_t len);

#endif /* ZXV_HKDF_H */
