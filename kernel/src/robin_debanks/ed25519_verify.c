/* ed25519_verify.c — Ed25519 public-key signature verification
 *
 * Wraps the public-domain orlp/ed25519 implementation vendored under
 * third_party/ed25519. The wrapper preserves the project's existing
 * ed25519_verify.h interface and the embedded public verification keys.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 for wrapper; orlp/ed25519 is public domain.
 */
#include "ed25519_verify.h"

/* Namespace the vendored implementation so it does not collide with this
 * wrapper's public symbols. */
#define ed25519_verify  orlp_ed25519_verify
#define sc_reduce       orlp_sc_reduce

#include "../../third_party/ed25519/src/fe.c"
#include "../../third_party/ed25519/src/ge.c"

/* sc.c duplicates the small static helpers load_3/load_4 from fe.c. */
#define load_3 orlp_sc_load_3
#define load_4 orlp_sc_load_4
#include "../../third_party/ed25519/src/sc.c"
#undef load_3
#undef load_4

#include "../../third_party/ed25519/src/sha512.c"
#include "../../third_party/ed25519/src/verify.c"

#undef ed25519_verify
#undef sc_reduce

bool ed25519_ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

/* Compatibility shim: the host KAT tests call sc_reduce(in, out). */
void sc_reduce(const uint8_t k[64], uint8_t out[32]) {
    unsigned char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = k[i];
    orlp_sc_reduce(buf);
    for (int i = 0; i < 32; i++) out[i] = buf[i];
}

bool ed25519_verify(const uint8_t *message, size_t message_len,
                    const uint8_t signature[ED25519_SIGNATURE_LEN],
                    const uint8_t pubkey[ED25519_PUBLIC_KEY_LEN]) {
    if (!signature || !pubkey) return false;
    if (message_len > 0 && !message) return false;
    return orlp_ed25519_verify(signature, message, message_len, pubkey) == 1;
}

/* Built-in PUBLIC verification keys (kernel root of trust).
 * These are PUBLIC keys — safe to embed in the kernel binary.
 * The corresponding PRIVATE keys are held offline and never
 * compiled into any source or binary. */
const uint8_t ED25519_PUBKEY_IMMIGRATION[32] = {
    0x0b, 0x03, 0x05, 0x6f, 0x4d, 0x06, 0x5f, 0xac,
    0x5e, 0x22, 0x3a, 0x3a, 0x17, 0xdf, 0x0b, 0x3b,
    0x4f, 0xb4, 0x79, 0x61, 0xf3, 0xbb, 0x77, 0x8d,
    0xc9, 0x7a, 0x06, 0xfd, 0xbf, 0x55, 0x0d, 0x10
};
const uint8_t ED25519_PUBKEY_COMMUNITY_CHEST[32] = {
    0x01, 0x00, 0xad, 0x79, 0xf7, 0xe6, 0x79, 0x2a,
    0x36, 0x42, 0x3f, 0x6c, 0x8d, 0xfc, 0xd9, 0x10,
    0x58, 0xb0, 0x88, 0x59, 0x2f, 0xd0, 0x45, 0xaf,
    0x10, 0x8c, 0x3a, 0x4c, 0x5e, 0xe4, 0x24, 0x73
};
const uint8_t ED25519_PUBKEY_COUNT_HOUSE[32] = {
    0xd6, 0xdc, 0xec, 0xfd, 0xdd, 0x2f, 0x0f, 0x15,
    0x7c, 0x76, 0xe4, 0x3a, 0x6b, 0xfb, 0xe5, 0x31,
    0x69, 0x15, 0x6d, 0x57, 0x80, 0x5b, 0x8e, 0x20,
    0xa1, 0xfb, 0xf2, 0x86, 0xab, 0xf8, 0x2e, 0xec
};
const uint8_t ED25519_PUBKEY_AI_LAYER[32] = {
    0x30, 0xe7, 0x71, 0xeb, 0x25, 0x9f, 0xab, 0x8e,
    0x03, 0x30, 0x6c, 0x0c, 0x7e, 0x05, 0x9b, 0x2f,
    0x8e, 0x5f, 0x51, 0xfb, 0xcb, 0xe5, 0x0b, 0x55,
    0x44, 0x8c, 0x26, 0xb8, 0xf5, 0xa5, 0x07, 0xf1
};
const uint8_t ED25519_PUBKEY_PORTER_HOUSE[32] = {
    0x22, 0x52, 0x62, 0x02, 0x7a, 0xac, 0x68, 0xd0,
    0xec, 0xf4, 0x6a, 0x02, 0x0f, 0x1a, 0xef, 0x8d,
    0x11, 0xb3, 0xe8, 0x46, 0xcd, 0x46, 0x2c, 0x51,
    0x98, 0xe4, 0xa1, 0x05, 0x8c, 0x52, 0x20, 0x33
};
