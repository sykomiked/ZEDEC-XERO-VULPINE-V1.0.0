/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey.h — signing keys sealed by the machine's security hardware.
 *
 * The account key is ML-DSA-87 (FIPS 204). No shipping TPM or Secure
 * Enclave can run ML-DSA today, so the hardware cannot hold that key
 * itself. What it can hold is a P-256 key that never leaves the chip.
 * This module seals the ML-DSA seed to that chip key and opens it only for
 * the length of one signature.
 *
 * THE KEYS
 * --------
 *   K1  The device key: P-256, created inside the TPM (Windows Platform
 *       Crypto Provider, Linux /dev/tpmrm0) or the Secure Enclave (macOS,
 *       CryptoKit). The host keeps only an opaque handle: a TPM template
 *       label, a key name, or the enclave's encrypted blob. The private
 *       half is never in host memory.
 *   K2  The account key: a 32-byte ML-DSA-87 seed (FIPS 204 Algorithm 6).
 *       It is stored only sealed (K3) and lives in host memory only inside
 *       hwk_keygen and hwk_sign, which wipe it before they return.
 *   K3  Sealing is ECIES-shaped. hwk_keygen asks the chip for a fresh
 *       ephemeral point E and the shared secret Z = ECDH(device key, E);
 *       only E is stored. Opening asks the chip for Z again from E. The
 *       key-encryption key is SHAKE256 over the provider kind, the handle,
 *       E and Z, split into a 32-byte pad (XORed with the seed: E is fresh
 *       for every seal, so a pad is used once) and a 32-byte MAC key.
 *   K4  The MAC (SHAKE256, keyed) covers every stored byte, including the
 *       public key, so a blob moved to another machine, edited, or paired
 *       with another public key fails with HWK_ERR_AUTH, compared in
 *       constant time. Opening also re-derives the public key from the
 *       seed and compares it with the stored one.
 *
 * PROVIDERS
 * ---------
 *   A provider is four callbacks (hwk_provider_t). The host ones are in
 *   kernel/arch/hosted/hwkey_host.h. hwkey_soft.c is the fallback for a
 *   machine with no usable chip: its secret is an ordinary file, so it is
 *   reported as HWK_KIND_SOFTWARE and a caller that requires hardware
 *   (hwk_require_hardware) refuses it.
 *
 * HONEST LIMITS
 * -------------
 *   - The ML-DSA secret key is in host memory for the duration of each
 *     signature (about a millisecond). Hardware binding stops the key being
 *     copied off the machine; it does not stop code already running as the
 *     user from asking the chip to open the seal while it runs there.
 *   - TPM keys here use an empty authorisation value and the enclave keys
 *     no user-presence check. Both are policy upgrades on top of this
 *     format, not changes to it.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_HWKEY_H
#define ZXV_HWKEY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../pqsec/pq_matrix_algs.h"

#define HWK_SEED_BYTES   32u
#define HWK_SECRET_BYTES 32u
#define HWK_ID_BYTES     32u
#define HWK_LABEL_MAX    64u
#define HWK_HANDLE_MAX   1024u /* enclave blobs are a few hundred bytes */
#define HWK_EPH_MAX      160u  /* 65 bytes for an uncompressed P-256 point */
#define HWK_PK_BYTES     PQM_MLDSA87_PK_BYTES
#define HWK_SIG_BYTES    PQM_MLDSA87_SIG_BYTES

typedef enum {
    HWK_KIND_NONE = 0,
    HWK_KIND_TPM = 1,            /* TPM 2.0 (Linux or Windows)            */
    HWK_KIND_SECURE_ENCLAVE = 2, /* Apple Secure Enclave                  */
    HWK_KIND_SOFTWARE = 3        /* no chip: the secret is an ordinary file */
} hwk_kind_t;

typedef enum {
    HWK_OK = 0,
    HWK_ERR_ARG = -1,
    HWK_ERR_HW = -2,          /* the chip refused or failed                */
    HWK_ERR_UNAVAILABLE = -3, /* no such chip on this machine              */
    HWK_ERR_FORMAT = -4,      /* not a sealed key, or a field out of range */
    HWK_ERR_AUTH = -5,        /* MAC or public key mismatch: wrong machine
                                 or tampered                               */
    HWK_ERR_POLICY = -6       /* software key where hardware is required   */
} hwk_status_t;

/* Callbacks return 0 on success, HWK_ERR_UNAVAILABLE when the chip is not
 * there, any other nonzero value on failure. Lengths are in/out: on entry
 * the capacity, on return the bytes written. */
typedef struct {
    hwk_kind_t kind;
    const char *name; /* for logs: "tpm2:/dev/tpmrm0", "secure-enclave", ... */
    void *ctx;
    /* Create, or reopen, the device key for `label`; write its handle. */
    int (*create)(void *ctx, const uint8_t *label, uint32_t label_len, uint8_t *handle,
                  uint32_t *handle_len);
    /* New ephemeral point E (written to eph) and Z = ECDH(device key, E). */
    int (*wrap)(void *ctx, const uint8_t *handle, uint32_t handle_len, uint8_t *eph,
                uint32_t *eph_len, uint8_t secret[HWK_SECRET_BYTES]);
    /* Z again from a stored E. */
    int (*unwrap)(void *ctx, const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                  uint32_t eph_len, uint8_t secret[HWK_SECRET_BYTES]);
    /* Random bytes (the chip's generator where there is one). */
    int (*random)(void *ctx, uint8_t *out, uint32_t len);
} hwk_provider_t;

/* A sealed account key. Plain data: the host stores it with hwk_encode. */
typedef struct {
    uint8_t kind; /* hwk_kind_t of the provider that sealed it */
    uint8_t label_len;
    uint16_t handle_len;
    uint16_t eph_len;
    uint8_t label[HWK_LABEL_MAX];
    uint8_t handle[HWK_HANDLE_MAX];
    uint8_t eph[HWK_EPH_MAX];
    uint8_t ct[HWK_SEED_BYTES];
    uint8_t tag[32];
    uint8_t pk[HWK_PK_BYTES];
} hwk_sealed_t;

/* Largest hwk_encode output. */
#define HWK_ENCODED_MAX (12u + HWK_LABEL_MAX + HWK_HANDLE_MAX + HWK_EPH_MAX + 64u + HWK_PK_BYTES)

/* Create (or reopen) the device key for `label`, draw a fresh seed from the
 * provider, derive the ML-DSA-87 key pair and seal the seed. */
hwk_status_t hwk_keygen(const hwk_provider_t *p, const uint8_t *label, uint32_t label_len,
                        hwk_sealed_t *out);

/* Open the seal on this machine, sign (hedged, FIPS 204 Algorithm 2 with a
 * context string of at most 255 bytes) and wipe. A provider of a different
 * kind than the one that sealed the key is refused. */
hwk_status_t hwk_sign(const hwk_provider_t *p, const hwk_sealed_t *k, const uint8_t *msg,
                      size_t msg_len, const uint8_t *ctx, size_t ctx_len,
                      uint8_t sig[HWK_SIG_BYTES]);

/* Open the seal and check it without signing (K4). */
hwk_status_t hwk_check(const hwk_provider_t *p, const hwk_sealed_t *k);

/* HWK_ERR_POLICY unless the key was sealed by a chip. */
hwk_status_t hwk_require_hardware(const hwk_sealed_t *k);

/* SHAKE256 of the public key: the account's identifier. */
void hwk_key_id(const uint8_t pk[HWK_PK_BYTES], uint8_t id[HWK_ID_BYTES]);

/* Serialise ("ZHK1") and parse. encode returns the length written, or 0
 * when cap is too small; decode checks every length field. */
uint32_t hwk_encode(const hwk_sealed_t *k, uint8_t *out, uint32_t cap);
hwk_status_t hwk_decode(const uint8_t *in, uint32_t len, hwk_sealed_t *k);

/* Overwrite memory in a way the compiler may not drop. */
void hwk_wipe(void *p, size_t n);
/* Constant-time equality. */
bool hwk_ct_eq(const uint8_t *a, const uint8_t *b, size_t n);

/* ---- software fallback (hwkey_soft.c) --------------------------------- */
/* The secret is 32 bytes the host keeps in a 0600 file. rng fills random
 * bytes (the host's getrandom/arc4random/BCryptGenRandom). */
typedef struct {
    uint8_t secret[32];
    int (*rng)(void *rng_ctx, uint8_t *out, uint32_t len);
    void *rng_ctx;
} hwk_soft_t;

void hwk_soft_provider(hwk_provider_t *p, hwk_soft_t *s);

#endif /* ZXV_HWKEY_H */
