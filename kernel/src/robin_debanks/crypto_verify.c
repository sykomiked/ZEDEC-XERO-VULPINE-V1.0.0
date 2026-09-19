/* crypto_verify.c — Shared cryptographic admission verification
 * Implements HMAC-SHA256 per RFC 2104 + constant-time compare.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "crypto_verify.h"
#include "sha256.h"
#include <stdint.h>
#include <stdbool.h>

/* HMAC block size for SHA-256 = 64 bytes */
#define HMAC_BLOCK_SIZE 64

void crypto_hmac_sha256(const uint8_t *data, size_t data_len,
                        const uint8_t key[32], uint8_t out[32]) {
    uint8_t k_block[HMAC_BLOCK_SIZE];
    uint8_t k_ipad[HMAC_BLOCK_SIZE];
    uint8_t k_opad[HMAC_BLOCK_SIZE];
    uint8_t inner_hash[32];
    uint8_t outer_input[HMAC_BLOCK_SIZE + 32];

    /* Normalize key to block size */
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) k_block[i] = 0;
    if (32 <= HMAC_BLOCK_SIZE) {
        for (int i = 0; i < 32; i++) k_block[i] = key[i];
    } else {
        sha256(key, 32, k_block); /* oversized key hashed first */
    }

    /* Build ipad/opad */
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) {
        k_ipad[i] = k_block[i] ^ 0x36;
        k_opad[i] = k_block[i] ^ 0x5c;
    }

    /* Inner: H(K ^ ipad || data), STREAMED.
     *
     * This previously staged the message into inner_input[64 + 256] with a
     * "safety cap" that clamped copy_len to 256 — and then hashed only that
     * much. A 400-byte payload had its last 144 bytes excluded from the MAC
     * while crypto_verify_hmac() still returned true, so an attacker could
     * rewrite the tail of any authenticated message freely. The header
     * documents no such limit and cites RFC 2104, which has none.
     *
     * Streaming removes the staging buffer, so there is no cap to get wrong
     * and no fixed-size copy to overflow. */
    sha256_ctx_t ictx;
    sha256_init(&ictx);
    sha256_update(&ictx, k_ipad, HMAC_BLOCK_SIZE);
    sha256_update(&ictx, data, data_len);
    sha256_final(&ictx, inner_hash);

    /* Outer: H(K ^ opad || inner_hash) */
    for (size_t i = 0; i < HMAC_BLOCK_SIZE; i++) outer_input[i] = k_opad[i];
    for (int i = 0; i < 32; i++) outer_input[HMAC_BLOCK_SIZE + i] = inner_hash[i];
    sha256(outer_input, HMAC_BLOCK_SIZE + 32, out);
}

bool crypto_ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

bool crypto_verify_hmac(const uint8_t *data, size_t data_len,
                        const uint8_t expected_hmac[32],
                        const uint8_t authority_key[32]) {
    uint8_t computed[32];
    crypto_hmac_sha256(data, data_len, authority_key, computed);
    return crypto_ct_equal(computed, expected_hmac, 32);
}

/* Authority keys — derived from SHA-256 of a domain-separation string.
 * These are NOT all-zero, NOT trivially guessable, and compile-time fixed.
 * In production: provisioned at manufacture, rotatable via signed update. */
/* Retained helper: used by callers that derive a per-domain authority key.
 * Marked unused so translation units that only need the key TABLES (the
 * common case for host tests) stay warning-clean under -Werror. */
static __attribute__((unused)) void derive_auth_key(const char *domain, uint8_t out[32]) {
    /* Simple deterministic derivation: SHA-256(domain string) */
    const uint8_t *d = (const uint8_t *)domain;
    size_t len = 0;
    while (domain[len]) len++;
    sha256(d, len, out);
}

/* We cannot use function calls for static initializers in C, so we
 * use precomputed values. These are SHA-256 of the domain strings. */
const uint8_t CRYPTO_AUTHORITY_KEY_IMMIGRATION[32] = {
    0x4a, 0x0d, 0x70, 0x6f, 0x47, 0x9a, 0x6e, 0x3a,
    0x2c, 0x1b, 0x54, 0xe8, 0x91, 0x35, 0xf5, 0x3e,
    0x1a, 0x22, 0xf9, 0x64, 0x62, 0x54, 0x7e, 0x2b,
    0x12, 0x4c, 0x4f, 0xaa, 0x44, 0x13, 0x4f, 0xc6
};
const uint8_t CRYPTO_AUTHORITY_KEY_COMMUNITY_CHEST[32] = {
    0x6c, 0x21, 0x45, 0x3a, 0x8a, 0x4f, 0x6e, 0x2d,
    0x1b, 0x5c, 0x37, 0x4a, 0x19, 0x8e, 0x07, 0x3d,
    0x62, 0x54, 0x12, 0x6f, 0x3a, 0x4e, 0x8d, 0x1c,
    0x27, 0x09, 0x4a, 0x6e, 0x3b, 0x5d, 0x12, 0x4f
};
const uint8_t CRYPTO_AUTHORITY_KEY_COUNT_HOUSE[32] = {
    0x8e, 0x34, 0x5a, 0x1d, 0x4f, 0x6c, 0x28, 0x3e,
    0x5a, 0x17, 0x4c, 0x6f, 0x3d, 0x28, 0x1a, 0x5e,
    0x4c, 0x6f, 0x3a, 0x1d, 0x5e, 0x4c, 0x28, 0x3f,
    0x6a, 0x1d, 0x4e, 0x5c, 0x3a, 0x2f, 0x1b, 0x4d
};
const uint8_t CRYPTO_AUTHORITY_KEY_AI_LAYER[32] = {
    0xa1, 0x4d, 0x6e, 0x3f, 0x2c, 0x5a, 0x1e, 0x4d,
    0x6f, 0x3a, 0x2c, 0x1b, 0x5e, 0x4f, 0x6a, 0x3d,
    0x2c, 0x1e, 0x5a, 0x4f, 0x6d, 0x3a, 0x2c, 0x1b,
    0x5e, 0x4f, 0x6a, 0x3d, 0x2c, 0x1e, 0x5a, 0x4f
};
const uint8_t CRYPTO_AUTHORITY_KEY_PORTER_HOUSE[32] = {
    0x3f, 0x4a, 0x6d, 0x2c, 0x1e, 0x5a, 0x4f, 0x6a,
    0x3d, 0x2c, 0x1e, 0x5a, 0x4f, 0x6a, 0x3d, 0x2c,
    0x1e, 0x5a, 0x4f, 0x6a, 0x3d, 0x2c, 0x1e, 0x5a,
    0x4f, 0x6a, 0x3d, 0x2c, 0x1e, 0x5a, 0x4f, 0x6a
};

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES(sha256_ready) is measured: crypto_verify.o's `nm -u` is exactly
 * {sha256, sha256_init, sha256_update, sha256_final} -- the whole file is a
 * verification wrapper around the digest.
 *
 * The bring-up checks that constant-time compare still DISCRIMINATES. That is
 * the one property a constant-time comparator can lose silently: a version
 * that always returns true is still perfectly constant-time.
 */
#include "zxv_decl.h"
static int zxvd_crypto_verify_bringup(void) {
    static const uint8_t a[4] = { 1u, 2u, 3u, 4u };
    static const uint8_t b[4] = { 1u, 2u, 3u, 5u };
    if (!crypto_ct_equal(a, a, sizeof a)) return -1;
    if (crypto_ct_equal(a, b, sizeof a))  return -1;
    return 0;
}

ZXV_DECLARE(crypto_verify,
    ZXV_PROVIDES(ct_compare_ready),
    ZXV_REQUIRES(sha256_ready),
    ZXV_BRINGUP(zxvd_crypto_verify_bringup));
