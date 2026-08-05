/* crypto_verify.h — Shared cryptographic admission verification
 *
 * Replaces permissive placeholder signature checks with real HMAC-SHA256
 * verification. Each subsystem (immigration, community_chest, count_house,
 * ai_layer, porter_house) calls crypto_verify_hmac with a known authority
 * key and the daemons signed payload. The authority key is compiled in
 * (kernel-embedded root of trust) and can be rotated via signed updates.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef CRYPTO_VERIFY_H
#define CRYPTO_VERIFY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CRYPTO_VERIFY_KEY_LEN 32

/* Verify that the given data was HMAC-SHA256 authenticated by an authority
 * whose secret key produces the expected HMAC. Returns true only if the
 * recomputed HMAC matches expected_hmac exactly (constant-time compare).
 *
 * data:        the payload bytes to verify
 * data_len:    payload length
 * expected_hmac: 32-byte HMAC digest claimed by the submitter
 * authority_key:  32-byte secret key of the trusted authority
 */
bool crypto_verify_hmac(const uint8_t *data, size_t data_len,
                        const uint8_t expected_hmac[32],
                        const uint8_t authority_key[CRYPTO_VERIFY_KEY_LEN]);

/* Compute HMAC-SHA256 (used internally by verify, exposed for signers). */
void crypto_hmac_sha256(const uint8_t *data, size_t data_len,
                        const uint8_t key[CRYPTO_VERIFY_KEY_LEN],
                        uint8_t out[32]);

/* Constant-time comparison. Returns true if a and b match for len bytes. */
bool crypto_ct_equal(const uint8_t *a, const uint8_t *b, size_t len);

/* Built-in authority keys (kernel root of trust). In production these
 * would be provisioned at manufacture time and rotatable via signed
 * update. For now they are compile-time constants that are NOT all-zero,
 * ensuring the permissive "any non-zero" pattern is eliminated. */
extern const uint8_t CRYPTO_AUTHORITY_KEY_IMMIGRATION[32];
extern const uint8_t CRYPTO_AUTHORITY_KEY_COMMUNITY_CHEST[32];
extern const uint8_t CRYPTO_AUTHORITY_KEY_COUNT_HOUSE[32];
extern const uint8_t CRYPTO_AUTHORITY_KEY_AI_LAYER[32];
extern const uint8_t CRYPTO_AUTHORITY_KEY_PORTER_HOUSE[32];

#endif /* CRYPTO_VERIFY_H */
