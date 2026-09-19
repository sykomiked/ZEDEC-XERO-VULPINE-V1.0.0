/* security_fabric.h — ZXV Security Fabric Compound Module
 *
 * The Security Fabric unifies all cryptographic, authentication, and
 * security modules into a single coherent fabric for post-quantum security,
 * zero-knowledge proofs, and hardware-rooted trust.
 *
 * Sub-modules integrated:
 *   1. Crypto — Core cryptographic primitives
 *   2. TLS — Transport Layer Security (post-quantum)
 *   3. ML-KEM — Module-Lattice Key Encapsulation Mechanism (FIPS 203)
 *   4. InvProof — Invertible Proofs (ZK-SNARKs/STARKs)
 *   5. ZAB — Zero-Knowledge Atomic Broadcast consensus
 *   6. Porter House — Trust-gated access control
 *   7. Identity Fabric — Sovereign identity & credentials
 *   8. Smart Adapter — Hardware security modules (HSM)
 *   9. Firmware Adapters — Secure element integration
 *   10. Orbital Fabric — Schema translation for security events
 *
 * Design principles:
 * - All crypto is post-quantum (ML-KEM, ML-DSA, SLH-DSA)
 * - Zero-knowledge proofs for all attestations
 * - Hardware-rooted trust via HSM/secure elements
 * - Trust is economic (Porter House) not social
 * - Keys are 168-bit critical words
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all security operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef SECURITY_FABRIC_H
#define SECURITY_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "crypto.h"
#include "tls.h"
#include "mlkem.h"
#include "invproof.h"
#include "zab.h"
#include "porter_house.h"
#include "identity_fabric.h"
#include "smart_adapter.h"
#include "firmware_adapters.h"
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define SECF_MAX_KEYS            1024
#define SECF_MAX_CERTS           2048
#define SECF_MAX_ZK_PROOFS       4096
#define SECF_MAX_TLS_SESSIONS    512
#define SECF_MAX_HSM_SLOTS       32
#define SECF_MAX_NAME_LEN        64

/* ===== Key Types ===== */

typedef enum {
    SECF_KEY_ML_KEM_512    = 1,
    SECF_KEY_ML_KEM_768    = 2,
    SECF_KEY_ML_KEM_1024   = 3,
    SECF_KEY_ML_DSA_44     = 4,
    SECF_KEY_ML_DSA_65     = 5,
    SECF_KEY_ML_DSA_87     = 6,
    SECF_KEY_SLH_DSA_SMALL = 7,
    SECF_KEY_SLH_DSA_FAST  = 8,
    SECF_KEY_ED25519       = 9,   /* Legacy */
    SECF_KEY_X25519        = 10,  /* Legacy */
    SECF_KEY_CRITICAL_WORD = 11   /* 168-bit sovereign key */
} secf_key_type_t;

/* ===== Key Pair ===== */

typedef struct secf_keypair {
    uint32_t id;
    char name[SECF_MAX_NAME_LEN];
    secf_key_type_t type;
    
    /* Key material */
    uint8_t public_key[256];
    uint32_t public_key_len;
    uint8_t private_key[256];
    uint32_t private_key_len;
    
    /* For critical words */
    word168_t critical_word;
    
    /* HSM backing */
    uint32_t hsm_slot_id;
    bool in_hsm;
    
    /* Usage */
    uint64_t created_tick;
    uint64_t expires_tick;
    uint64_t sign_count;
    uint64_t encrypt_count;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool compromised;
} secf_keypair_t;

/* ===== Certificate ===== */

typedef struct secf_certificate {
    uint32_t id;
    uint32_t keypair_id;           /* Subject key */
    uint32_t issuer_keypair_id;    /* Issuer key */
    
    /* Certificate data */
    uint8_t cert_data[1024];
    uint32_t cert_len;
    uint8_t cert_cid[32];
    
    /* Validity */
    uint64_t not_before;
    uint64_t not_after;
    
    /* Extensions */
    char subject[SECF_MAX_NAME_LEN];
    char issuer[SECF_MAX_NAME_LEN];
    uint32_t key_usage;            /* Bitmask */
    uint32_t extended_key_usage;
    
    /* Revocation */
    bool revoked;
    uint64_t revoked_tick;
    uint8_t revocation_cid[32];
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} secf_certificate_t;

/* ===== ZK Proof ===== */

typedef struct secf_zk_proof {
    uint32_t id;
    char circuit_name[SECF_MAX_NAME_LEN];
    
    /* Proof data */
    uint8_t proof_data[4096];
    uint32_t proof_len;
    uint8_t proof_cid[32];
    
    /* Public inputs */
    uint8_t public_inputs[1024];
    uint32_t public_inputs_len;
    
    /* Verification */
    bool verified;
    uint64_t verification_tick;
    lpres_state_t verification_attestation;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} secf_zk_proof_t;

/* ===== TLS Session ===== */

typedef struct secf_tls_session {
    uint32_t id;
    uint32_t local_keypair_id;
    uint32_t peer_keypair_id;
    
    /* Session state */
    enum {
        SECF_TLS_HANDSHAKE = 0,
        SECF_TLS_ESTABLISHED = 1,
        SECF_TLS_RENEGOTIATING = 2,
        SECF_TLS_CLOSED = 3
    } state;
    
    /* Cipher suite */
    uint16_t cipher_suite;         /* Post-quantum cipher suite */
    uint8_t session_key[64];
    uint8_t iv[16];
    
    /* Statistics */
    uint64_t bytes_encrypted;
    uint64_t bytes_decrypted;
    uint64_t records_sent;
    uint64_t records_received;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} secf_tls_session_t;

/* ===== HSM Slot ===== */

typedef struct secf_hsm_slot {
    uint32_t id;
    char label[SECF_MAX_NAME_LEN];
    
    /* Hardware */
    smart_device_t *sa_device;     /* Smart Adapter HSM */
    jdr_adapter_t *jdr_adapter;    /* JDR adapter for secure element */
    
    /* Slots */
    uint32_t keypair_ids[16];
    uint32_t num_keypairs;
    
    /* Capabilities */
    bool can_sign;
    bool can_encrypt;
    bool can_derive;
    bool can_generate;
    bool tamper_evident;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} secf_hsm_slot_t;

/* ===== Security Fabric ===== */

typedef struct security_fabric {
    /* Core sub-modules */
    crypto_subsystem_t crypto;     /* Crypto Subsystem */
    tls_stack_t tls;               /* TLS Stack */
    mlkem_ctx_t mlkem;             /* ML-KEM Context */
    invproof_engine_t invproof;    /* InvProof Engine */
    zab_consensus_t zab;           /* ZAB Consensus */
    porter_house_t porter;         /* Porter House Trust */
    
    /* Integration references */
    identity_fabric_t *identity;   /* Identity Fabric */
    smart_adapter_registry_t *sa_registry; /* Smart Adapter Registry */
    jdr_adapter_registry_t *jdr_registry;  /* JDR Adapter Registry */
    orbital_fabric_t *orbital;     /* Orbital Fabric */
    financial_fabric_t *financial; /* Financial Fabric */
    network_fabric_t *network;     /* Network Fabric */
    
    /* Fabric-level state */
    secf_keypair_t keypairs[SECF_MAX_KEYS];
    uint32_t num_keypairs;
    uint32_t next_keypair_id;
    
    secf_certificate_t certificates[SECF_MAX_CERTS];
    uint32_t num_certificates;
    
    secf_zk_proof_t zk_proofs[SECF_MAX_ZK_PROOFS];
    uint32_t num_zk_proofs;
    
    secf_tls_session_t tls_sessions[SECF_MAX_TLS_SESSIONS];
    uint32_t num_tls_sessions;
    
    secf_hsm_slot_t hsm_slots[SECF_MAX_HSM_SLOTS];
    uint32_t num_hsm_slots;
    
    /* Global statistics */
    struct {
        uint64_t total_keys_generated;
        uint64_t total_certs_issued;
        uint64_t total_zk_proofs_generated;
        uint64_t total_zk_proofs_verified;
        uint64_t total_tls_sessions;
        uint64_t total_bytes_encrypted;
        uint64_t total_bytes_decrypted;
        uint64_t total_hsm_operations;
        uint64_t total_key_rotations;
        uint64_t total_revocations;
    } stats;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Configuration */
    struct {
        bool require_hsm_for_signing;
        bool require_zk_for_attestation;
        bool auto_key_rotation;
        uint64_t key_rotation_interval;
        secf_key_type_t default_kem_type;
        secf_key_type_t default_sig_type;
    } config;
    
    bool initialized;
} security_fabric_t;

/* ===== API ===== */

/* Initialize the Security Fabric */
void secf_init(security_fabric_t *fabric,
               identity_fabric_t *identity,
               smart_adapter_registry_t *sa_registry,
               jdr_adapter_registry_t *jdr_registry,
               orbital_fabric_t *orbital,
               financial_fabric_t *financial,
               network_fabric_t *network);

/* Register built-in security modules */
void secf_register_builtins(security_fabric_t *fabric);

/* ===== Key Management ===== */

int32_t secf_generate_keypair(security_fabric_t *fabric,
                              const char *name, secf_key_type_t type,
                              bool in_hsm, uint32_t hsm_slot_id);

secf_keypair_t *secf_get_keypair(security_fabric_t *fabric, uint32_t keypair_id);

int32_t secf_rotate_key(security_fabric_t *fabric, uint32_t keypair_id);
int32_t secf_revoke_key(security_fabric_t *fabric, uint32_t keypair_id);

/* ===== Signing & Encryption ===== */

int32_t secf_sign(security_fabric_t *fabric,
                  uint32_t keypair_id,
                  const uint8_t *data, uint32_t len,
                  uint8_t *signature, uint32_t *sig_len);

int32_t secf_verify(security_fabric_t *fabric,
                    uint32_t keypair_id,
                    const uint8_t *data, uint32_t len,
                    const uint8_t *signature, uint32_t sig_len);

int32_t secf_encrypt(security_fabric_t *fabric,
                     uint32_t keypair_id,
                     const uint8_t *plaintext, uint32_t len,
                     uint8_t *ciphertext, uint32_t *ct_len);

int32_t secf_decrypt(security_fabric_t *fabric,
                     uint32_t keypair_id,
                     const uint8_t *ciphertext, uint32_t len,
                     uint8_t *plaintext, uint32_t *pt_len);

/* ===== Key Encapsulation (ML-KEM) ===== */

int32_t secf_kem_encapsulate(security_fabric_t *fabric,
                             uint32_t keypair_id,
                             uint8_t *ciphertext, uint32_t *ct_len,
                             uint8_t *shared_secret, uint32_t *ss_len);

int32_t secf_kem_decapsulate(security_fabric_t *fabric,
                             uint32_t keypair_id,
                             const uint8_t *ciphertext, uint32_t ct_len,
                             uint8_t *shared_secret, uint32_t *ss_len);

/* ===== Certificates ===== */

int32_t secf_issue_certificate(security_fabric_t *fabric,
                               uint32_t subject_keypair_id,
                               uint32_t issuer_keypair_id,
                               const char *subject, const char *issuer,
                               uint64_t not_before, uint64_t not_after,
                               uint32_t key_usage);

int32_t secf_verify_certificate(security_fabric_t *fabric, uint32_t cert_id);
int32_t secf_revoke_certificate(security_fabric_t *fabric, uint32_t cert_id);

/* ===== Zero-Knowledge Proofs ===== */

int32_t secf_generate_zk_proof(security_fabric_t *fabric,
                               const char *circuit_name,
                               const uint8_t *witness, uint32_t witness_len,
                               uint8_t *proof, uint32_t *proof_len);

int32_t secf_verify_zk_proof(security_fabric_t *fabric,
                             const char *circuit_name,
                             const uint8_t *proof, uint32_t proof_len,
                             const uint8_t *public_inputs, uint32_t pi_len);

/* ===== TLS ===== */

int32_t secf_tls_create_session(security_fabric_t *fabric,
                                uint32_t local_keypair_id,
                                uint32_t peer_keypair_id,
                                uint16_t cipher_suite);

int32_t secf_tls_handshake(security_fabric_t *fabric, uint32_t session_id);
int32_t secf_tls_encrypt(security_fabric_t *fabric, uint32_t session_id,
                         const uint8_t *plaintext, uint32_t len,
                         uint8_t *ciphertext, uint32_t *ct_len);
int32_t secf_tls_decrypt(security_fabric_t *fabric, uint32_t session_id,
                         const uint8_t *ciphertext, uint32_t len,
                         uint8_t *plaintext, uint32_t *pt_len);

/* ===== HSM ===== */

int32_t secf_register_hsm(security_fabric_t *fabric,
                          const char *label,
                          smart_device_t *sa_device,
                          jdr_adapter_t *jdr_adapter);

int32_t secf_hsm_generate_key(security_fabric_t *fabric,
                              uint32_t hsm_slot_id,
                              const char *name, secf_key_type_t type);

/* ===== Porter House Trust ===== */

int32_t secf_check_trust(security_fabric_t *fabric,
                         uint32_t identity_id,
                         surplus_real_t *trust_score);

int32_t secf_update_trust(security_fabric_t *fabric,
                          uint32_t identity_id,
                          surplus_real_t delta);

/* ===== Health & Attestation ===== */

int32_t secf_check_keypair_health(security_fabric_t *fabric,
                                  uint32_t keypair_id,
                                  void *health_out);

int32_t secf_check_hsm_health(security_fabric_t *fabric,
                              uint32_t hsm_slot_id,
                              void *health_out);

int32_t secf_check_global_health(security_fabric_t *fabric);

bool secf_global_safety_gate(security_fabric_t *fabric);

lpres_state_t secf_attest(security_fabric_t *fabric, uint32_t keypair_id,
                          uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void secf_update_coverage(security_fabric_t *fabric);
bool secf_enforce_coverage(security_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void secf_get_stats(security_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t secf_get_attestation(security_fabric_t *fabric, uint32_t keypair_id);
void secf_set_attestation(security_fabric_t *fabric, uint32_t keypair_id, lpres_state_t state);

/* Utility */
const char *secf_lpres_state_name(lpres_state_t state);
const char *secf_key_type_name(secf_key_type_t type);

#endif /* SECURITY_FABRIC_H */
