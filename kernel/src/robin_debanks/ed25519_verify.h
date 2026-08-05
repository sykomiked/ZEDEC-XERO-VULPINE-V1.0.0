/* ed25519_verify.h — Ed25519 public-key signature verification
 *
 * Replaces HMAC-SHA256 (symmetric) package "signatures" with proper
 * asymmetric verification. Public verification keys are embedded in
 * the kernel; private signing keys are held offline/HSM-backed.
 *
 * This is a minimal Ed25519 implementation sufficient for kernel-side
 * verification. It uses the existing SHA-512 primitive (or falls back
 * to SHA-256-based deterministic verification if SHA-512 is unavailable).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef ED25519_VERIFY_H
#define ED25519_VERIFY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ED25519_PUBLIC_KEY_LEN  32
#define ED25519_SIGNATURE_LEN   64

/* Verify an Ed25519 signature.
 *
 * message:     the payload bytes to verify
 * message_len: payload length
 * signature:   64-byte Ed25519 signature
 * pubkey:      32-byte public key of the trusted authority
 *
 * Returns true only if the signature is valid for the message under
 * the given public key.
 */
bool ed25519_verify(const uint8_t *message, size_t message_len,
                    const uint8_t signature[ED25519_SIGNATURE_LEN],
                    const uint8_t pubkey[ED25519_PUBLIC_KEY_LEN]);

/* Constant-time comparison (re-exported from crypto_verify for convenience) */
bool ed25519_ct_equal(const uint8_t *a, const uint8_t *b, size_t len);

/* Built-in PUBLIC verification keys (kernel root of trust).
 * These are PUBLIC keys — safe to embed in the kernel binary.
 * The corresponding PRIVATE keys are held offline and never
 * compiled into any source or binary. */
extern const uint8_t ED25519_PUBKEY_IMMIGRATION[32];
extern const uint8_t ED25519_PUBKEY_COMMUNITY_CHEST[32];
extern const uint8_t ED25519_PUBKEY_COUNT_HOUSE[32];
extern const uint8_t ED25519_PUBKEY_AI_LAYER[32];
extern const uint8_t ED25519_PUBKEY_PORTER_HOUSE[32];

#endif /* ED25519_VERIFY_H */
