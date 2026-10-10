/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_signer.h — TEST-ONLY Ed25519 signing for host tests.
 *
 * Keys are derived at run time from a fixed, public test seed: no private key
 * is stored in the tree, and none of these keys is trusted by any build. Link
 * src/provenance/test_signer.c together with src/robin_debanks/ed25519_verify.c
 * (which already carries the shared field/group/sha512 code). Hosted tests only.
 */
#ifndef ZXV_TEST_SIGNER_H
#define ZXV_TEST_SIGNER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* from third_party/ed25519/src/ed25519.h (not included: its ed25519_verify
 * prototype differs from the kernel's robin_debanks one) */
void ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key,
                            const unsigned char *seed);
void ed25519_sign(unsigned char *signature, const unsigned char *message, size_t message_len,
                  const unsigned char *public_key, const unsigned char *private_key);

typedef struct {
    uint8_t pk[32];
    uint8_t sk[64];
} test_signer_t;

/* A key from a one-byte label: seed = label repeated 32 times. */
static inline void test_signer_init(test_signer_t *s, uint8_t label)
{
    uint8_t seed[32];
    memset(seed, label, sizeof seed);
    ed25519_create_keypair(s->pk, s->sk, seed);
}

static inline void test_signer_sign(const test_signer_t *s, const uint8_t *msg, uint32_t len,
                                    uint8_t sig[64])
{
    ed25519_sign(sig, msg, len, s->pk, s->sk);
}

#endif /* ZXV_TEST_SIGNER_H */
