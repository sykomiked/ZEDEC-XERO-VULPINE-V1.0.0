/* pq_security.h — ZEDEC pqOS Vertically Integrated Post-Quantum Security
 *
 * Bakes the finalized NIST post-quantum standards directly into the
 * atomic structure of the 5-layer cellular matrix:
 *
 *   FIPS 203 — ML-KEM-768/1024  (key encapsulation, Layer 5 mesh routing)
 *   FIPS 204 — ML-DSA-65        (module-lattice signatures, Layer 4 identity)
 *   FIPS 205 — SLH-DSA-128s     (stateless hash signatures, Layer 1 boot)
 *
 * Layer integration:
 *   Layer 1 (Membrane):  SLH-DSA boot verification — hash-based, lattice-free.
 *                        A failed check severs the voltage supply.
 *   Layer 2/3 (Organs + Nervous): Hybrid ML-DSA + SLH-DSA ledger seal.
 *                        Both proofs evaluated orthogonally via LPRES.
 *   Layer 4 (Interface): ML-DSA identity authentication for the
 *                        holographic desktop and P-TERM.
 *   Layer 5 (Mesh):      ML-KEM-1024 encapsulation for every exact-rational
 *                        event packet — no Harvest-Now-Decrypt-Later.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef PQ_SECURITY_H
#define PQ_SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ============================================================================
 * FIPS 203 — ML-KEM (already implemented in mlkem768.h)
 * ============================================================================ */

#include "../mlkem/mlkem768.h"

/* ML-KEM-1024 sizes (CNSA 2.0 highest security parameter).
 * The existing ML-KEM-768 implementation is used; 1024 is declared here
 * for the Layer 5 mesh encapsulation envelope. */
#define PQ_KEM1024_EK_BYTES  1568
#define PQ_KEM1024_DK_BYTES  3168
#define PQ_KEM1024_CT_BYTES  1568
#define PQ_KEM1024_SS_BYTES  32

/* ============================================================================
 * FIPS 204 — ML-DSA-65 (Module-Lattice Digital Signature)
 * ============================================================================ */

#define PQ_MLDSA65_PK_BYTES  1952
#define PQ_MLDSA65_SK_BYTES  4032
#define PQ_MLDSA65_SIG_BYTES 3309
#define PQ_MLDSA65_CTX_BYTES 255

/* ML-DSA-65 key generation. seed: 32 bytes fresh randomness. */
void pq_mldsa65_keygen(const uint8_t seed[32],
                       uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES]);

/* ML-DSA-65 sign. msg/msg_len: the message; ctx/ctx_len: domain separator
 * (may be NULL/0, at most 255); rnd: 32 bytes fresh randomness (hedged), or
 * NULL for the deterministic variant. On bad arguments sig is all zero,
 * which never verifies. */
void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES],
                     const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len,
                     const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

/* ML-DSA-65 verify. Returns true iff the signature is valid. */
bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

/* ============================================================================
 * FIPS 205 — SLH-DSA-128s (Stateless Hash-Based Digital Signature)
 * ============================================================================ */

#define PQ_SLH128S_PK_BYTES  32
#define PQ_SLH128S_SK_BYTES  64
#define PQ_SLH128S_SIG_BYTES 7856
#define PQ_SLH128S_CTX_BYTES 255

/* SLH-DSA-SHAKE-128s key generation. seed: 48 bytes fresh randomness,
 * read as SK.seed || SK.prf || PK.seed (16 bytes each). */
void pq_slh128s_keygen(const uint8_t seed[48],
                       uint8_t pk[PQ_SLH128S_PK_BYTES],
                       uint8_t sk[PQ_SLH128S_SK_BYTES]);

/* SLH-DSA-128s sign, empty context. opt_rnd: 16 bytes of fresh
 * randomness (hedged), or NULL for the deterministic variant. */
void pq_slh128s_sign(const uint8_t sk[PQ_SLH128S_SK_BYTES],
                     const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *opt_rnd,
                     uint8_t sig[PQ_SLH128S_SIG_BYTES]);

/* SLH-DSA-128s verify, empty context. Returns true iff the signature is valid. */
bool pq_slh128s_verify(const uint8_t pk[PQ_SLH128S_PK_BYTES],
                       const uint8_t *msg, uint32_t msg_len,
                       const uint8_t sig[PQ_SLH128S_SIG_BYTES]);

/* The same with a context string (FIPS 205 Algorithms 22/24), ctx_len <= 255. */
void pq_slh128s_sign_ctx(const uint8_t sk[PQ_SLH128S_SK_BYTES], const uint8_t *msg,
                         uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                         const uint8_t *opt_rnd, uint8_t sig[PQ_SLH128S_SIG_BYTES]);
bool pq_slh128s_verify_ctx(const uint8_t pk[PQ_SLH128S_PK_BYTES], const uint8_t *msg,
                           uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                           const uint8_t sig[PQ_SLH128S_SIG_BYTES]);

/* ============================================================================
 * HYBRID SIGNATURE — the ledger's immutable quantum seal
 * ============================================================================ */

/* A hybrid signature pairs ML-DSA (lattice) with SLH-DSA (hash).
 * Even if lattice math is shattered, the hash-based half holds. */
typedef struct {
    uint8_t mldsa_sig[PQ_MLDSA65_SIG_BYTES];
    uint8_t slh_sig[PQ_SLH128S_SIG_BYTES];
    lpres_state_t mldsa_verified;   /* LPRES: TRUE/FALSE per half */
    lpres_state_t slh_verified;
} pq_hybrid_sig_t;

/* Sign a ledger entry with BOTH schemes. Returns the hybrid signature. */
void pq_hybrid_sign(const uint8_t mldsa_sk[PQ_MLDSA65_SK_BYTES],
                    const uint8_t slh_sk[PQ_SLH128S_SK_BYTES],
                    const uint8_t *msg, uint32_t msg_len,
                    const uint8_t rnd[32],
                    pq_hybrid_sig_t *out);

/* Verify a hybrid signature. The LPRES verdict:
 *   BOTH halves TRUE  -> LPRES_STATE_TRUE   (full integrity)
 *   one half TRUE     -> LPRES_STATE_BOTH   (contradiction: one scheme broken)
 *   both FALSE        -> LPRES_STATE_FALSE  (forgery)
 * The entry remains valid while EITHER half verifies — that is the
 * paraconsistent quantum seal. */
lpres_state_t pq_hybrid_verify(const uint8_t mldsa_pk[PQ_MLDSA65_PK_BYTES],
                               const uint8_t slh_pk[PQ_SLH128S_PK_BYTES],
                               const uint8_t *msg, uint32_t msg_len,
                               const pq_hybrid_sig_t *sig);

/* ============================================================================
 * LAYER 1 — BOOT VERIFICATION (SLH-DSA, hash-based, lattice-free)
 * ============================================================================ */

/* Verify the bootloader/chiglet firmware image with SLH-DSA.
 * Returns LPRES_STATE_TRUE if the image is authentic, FALSE otherwise.
 * On FALSE the membrane severs the voltage supply — the node never
 * touches the Layer 5 mesh. */
lpres_state_t pq_boot_verify(const uint8_t slh_pk[PQ_SLH128S_PK_BYTES],
                             const uint8_t *firmware, uint32_t fw_len,
                             const uint8_t sig[PQ_SLH128S_SIG_BYTES]);

/* ============================================================================
 * LAYER 5 — MESH ENCAPSULATION (ML-KEM)
 * ============================================================================ */

/* An encapsulated event packet: the exact-rational payload is sealed
 * under a quantum-resistant shared secret. The shared secret itself never
 * travels: only the ML-KEM ciphertext does, and only the holder of the
 * decapsulation key can recover the secret.
 *
 * Sealing (SHA-3 family only, so this layer needs nothing beyond keccak):
 *   ks  = SHAKE256(ss || "ZXV-MESH-v1 stream", payload_len)
 *   mk  = SHAKE256(ss || "ZXV-MESH-v1 mac", 32)
 *   payload = plaintext XOR ks
 *   tag = SHA3-256(mk || ct || le32(payload_len) || payload)
 * Every encapsulation draws a fresh ML-KEM secret, so a key stream is never
 * reused. SHA-3 is not length-extendable, so the prefix-keyed hash is a MAC. */
#define PQ_MESH_MAX_PAYLOAD 1024u
#define PQ_MESH_TAG_BYTES   32u
typedef struct {
    uint8_t ct[MLKEM768_CT_BYTES];   /* ML-KEM-768 ciphertext */
    uint8_t tag[PQ_MESH_TAG_BYTES];  /* authenticates ct, length and payload */
    uint32_t payload_len;
    uint8_t payload[PQ_MESH_MAX_PAYLOAD]; /* sealed exact-rational IR bytes */
} pq_mesh_packet_t;

/* Encapsulate and seal a payload under the receiver's encapsulation key.
 * m is 32 bytes of fresh randomness. Returns false (and an all-zero packet)
 * when the payload is too long or an argument is missing. */
bool pq_mesh_encapsulate(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t *payload,
                         uint32_t payload_len, const uint8_t m[32], pq_mesh_packet_t *out);

/* Decapsulate, authenticate and unseal. Returns false, with out zeroed, when
 * the tag does not verify (wrong key, or any bit of the packet changed). */
bool pq_mesh_open(const uint8_t dk[MLKEM768_DK_BYTES], const pq_mesh_packet_t *packet, uint8_t *out,
                  uint32_t cap, uint32_t *out_len);

/* Decapsulate only: the receiver's copy of the shared secret, for callers
 * that derive their own session keys from it. */
void pq_mesh_decapsulate(const uint8_t dk[MLKEM768_DK_BYTES],
                         const pq_mesh_packet_t *packet,
                         uint8_t ss_out[MLKEM768_SS_BYTES]);

/* ============================================================================
 * LAYER 4 — IDENTITY AUTHENTICATION (ML-DSA)
 * ============================================================================ */

/* Authenticate an operator for holographic desktop access.
 * The biometric/physical token generates an ML-DSA signature which
 * drops through the Layer 5 mesh and is validated by the M5 kernel. */
lpres_state_t pq_identity_authenticate(const uint8_t pk[PQ_MLDSA65_PK_BYTES],
                                       const uint8_t *challenge, uint32_t challenge_len,
                                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

/* ============================================================================
 * SELF-TEST
 * ============================================================================ */

/* Run all three FIPS self-tests (keygen/sign/verify round-trips).
 * Returns the number of schemes that passed (0-3). */
uint32_t pq_self_test(void);

#endif /* PQ_SECURITY_H */
