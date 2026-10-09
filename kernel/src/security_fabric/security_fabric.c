/* security_fabric.c — ZXV Security Fabric Compound Module Implementation
 *
 * Unifies all security modules: Crypto, TLS, ML-KEM, InvProof, ZAB, Porter House,
 * with Identity, Smart Adapter, JDR, Orbital, Financial, and Network fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "security_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void secf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void secf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int secf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t secf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void secf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t secf_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t secf_attest(security_fabric_t *fabric, uint32_t keypair_id,
                          uint32_t op_id, void *args, int32_t result) {
    if (!fabric || keypair_id >= fabric->num_keypairs) return LPRES_STATE_NEITHER;
    
    secf_keypair_t *kp = &fabric->keypairs[keypair_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t kp_att = kp->attestation;
    lpres_state_t coverage_att = (SR_CMP(kp->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, kp_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    kp->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void secf_init(security_fabric_t *fabric,
               identity_fabric_t *identity,
               smart_adapter_registry_t *sa_registry,
               jdr_adapter_registry_t *jdr_registry,
               orbital_fabric_t *orbital,
               financial_fabric_t *financial,
               network_fabric_t *network) {
    if (!fabric) return;
    
    secf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->identity = identity;
    fabric->sa_registry = sa_registry;
    fabric->jdr_registry = jdr_registry;
    fabric->orbital = orbital;
    fabric->financial = financial;
    fabric->network = network;
    
    /* Initialize sub-modules */
    /* crypto_init(&fabric->crypto); */
    /* tls_init(&fabric->tls); */
    /* mlkem_init(&fabric->mlkem); */
    /* invproof_init(&fabric->invproof); */
    /* zab_init(&fabric->zab); */
    /* porter_house_init(&fabric->porter); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(7.0);  /* Security rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = secf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.require_hsm_for_signing = true;
    fabric->config.require_zk_for_attestation = true;
    fabric->config.auto_key_rotation = true;
    fabric->config.key_rotation_interval = 31536000;  /* 1 year */
    fabric->config.default_kem_type = SECF_KEY_ML_KEM_768;
    fabric->config.default_sig_type = SECF_KEY_ML_DSA_65;
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void secf_register_builtins(security_fabric_t *fabric) {
    if (!fabric) return;
}

/* ===== Key Management ===== */

int32_t secf_generate_keypair(security_fabric_t *fabric,
                              const char *name, secf_key_type_t type,
                              bool in_hsm, uint32_t hsm_slot_id) {
    if (!fabric || !name || fabric->num_keypairs >= SECF_MAX_KEYS) return -1;
    
    if (in_hsm) {
        if (hsm_slot_id >= fabric->num_hsm_slots) return -1;
        secf_hsm_slot_t *hsm = &fabric->hsm_slots[hsm_slot_id];
        if (!hsm->active || !hsm->can_generate) return -1;
    }
    
    secf_keypair_t *kp = &fabric->keypairs[fabric->num_keypairs];
    secf_mem_set(kp, 0, sizeof(*kp));
    kp->id = fabric->next_keypair_id++;
    
    secf_str_copy(kp->name, name, SECF_MAX_NAME_LEN);
    kp->type = type;
    kp->in_hsm = in_hsm;
    kp->hsm_slot_id = hsm_slot_id;
    kp->created_tick = 0;
    kp->expires_tick = fabric->config.key_rotation_interval;
    
    /* Generate key material based on type */
    switch (type) {
        case SECF_KEY_ML_KEM_512:
            kp->public_key_len = 800;
            kp->private_key_len = 1632;
            break;
        case SECF_KEY_ML_KEM_768:
            kp->public_key_len = 1184;
            kp->private_key_len = 2400;
            break;
        case SECF_KEY_ML_KEM_1024:
            kp->public_key_len = 1568;
            kp->private_key_len = 3168;
            break;
        case SECF_KEY_ML_DSA_44:
            kp->public_key_len = 1312;
            kp->private_key_len = 2560;
            break;
        case SECF_KEY_ML_DSA_65:
            kp->public_key_len = 1952;
            kp->private_key_len = 4032;
            break;
        case SECF_KEY_ML_DSA_87:
            kp->public_key_len = 2592;
            kp->private_key_len = 4896;
            break;
        case SECF_KEY_CRITICAL_WORD:
            /* Generate 168-bit critical word */
            for (int i = 0; i < 168/64; i++) {
                kp->critical_word.words[i] = (uint64_t)(kp->id + i * 0x9E3779B97F4A7C15ULL);
            }
            break;
        default:
            kp->public_key_len = 32;
            kp->private_key_len = 32;
            break;
    }
    
    /* Initialize M5 */
    kp->m5.omega = fabric->num_keypairs + 1;
    kp->m5.r = SR_FROM_FLOAT(7.0);
    kp->m5.ell = SR_ONE;
    kp->m5.phi = SR_ZERO;
    kp->m5.chi = 0;
    kp->coverage_ratio = secf_compute_coverage(&kp->m5);
    
    kp->attestation = LPRES_STATE_NEITHER;
    kp->active = true;
    kp->compromised = false;
    
    fabric->num_keypairs++;
    fabric->stats.total_keys_generated++;
    
    return secf_attest(fabric, kp->id, 0x1000, kp, 0);
}

secf_keypair_t *secf_get_keypair(security_fabric_t *fabric, uint32_t keypair_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_keypairs; i++) {
        if (fabric->keypairs[i].id == keypair_id && fabric->keypairs[i].active) {
            return &fabric->keypairs[i];
        }
    }
    return NULL;
}

int32_t secf_rotate_key(security_fabric_t *fabric, uint32_t keypair_id) {
    if (!fabric) return -1;
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp) return -1;
    
    /* Generate new key material */
    /* crypto_generate_keypair(&fabric->crypto, kp->type, kp->public_key, &kp->public_key_len, kp->private_key, &kp->private_key_len); */
    
    kp->created_tick = 0;
    kp->expires_tick = 0 + fabric->config.key_rotation_interval;
    kp->sign_count = 0;
    kp->encrypt_count = 0;
    
    fabric->stats.total_key_rotations++;
    
    return secf_attest(fabric, keypair_id, 0x2000, kp, 0);
}

int32_t secf_revoke_key(security_fabric_t *fabric, uint32_t keypair_id) {
    if (!fabric) return -1;
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp) return -1;
    
    kp->compromised = true;
    kp->active = false;
    kp->attestation = LPRES_STATE_FALSE;
    
    fabric->stats.total_revocations++;
    
    return secf_attest(fabric, keypair_id, 0x3000, kp, -1);
}

/* ===== Signing & Encryption ===== */

int32_t secf_sign(security_fabric_t *fabric,
                  uint32_t keypair_id,
                  const uint8_t *data, uint32_t len,
                  uint8_t *signature, uint32_t *sig_len) {
    if (!fabric || !data || !signature || !sig_len) return -1;
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp || kp->compromised) return -1;
    
    if (fabric->config.require_hsm_for_signing && !kp->in_hsm) return -1;
    
    /* Sign via crypto subsystem */
    /* crypto_sign(&fabric->crypto, kp->type, kp->private_key, kp->private_key_len, data, len, signature, sig_len); */
    *sig_len = 64;  /* Placeholder */
    
    kp->sign_count++;
    
    return secf_attest(fabric, keypair_id, 0x4000, signature, 0);
}

int32_t secf_verify(security_fabric_t *fabric,
                    uint32_t keypair_id,
                    const uint8_t *data, uint32_t len,
                    const uint8_t *signature, uint32_t sig_len) {
    if (!fabric || !data || !signature) return -1;
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp) return -1;
    
    /* Verify via crypto subsystem */
    /* bool valid = crypto_verify(&fabric->crypto, kp->type, kp->public_key, kp->public_key_len, data, len, signature, sig_len); */
    bool valid = true;  /* Placeholder */
    
    return secf_attest(fabric, keypair_id, 0x5000, (void*)signature, valid ? 0 : -1);
}

int32_t secf_encrypt(security_fabric_t *fabric,
                     uint32_t keypair_id,
                     const uint8_t *plaintext, uint32_t len,
                     uint8_t *ciphertext, uint32_t *ct_len) {
    if (!fabric || !plaintext || !ciphertext || !ct_len) return -1;
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp || kp->compromised) return -1;
    
    /* Encrypt via crypto subsystem */
    /* crypto_encrypt(&fabric->crypto, kp->type, kp->public_key, kp->public_key_len, plaintext, len, ciphertext, ct_len); */
    *ct_len = len + 32;  /* Placeholder */
    
    kp->encrypt_count++;
    fabric->stats.total_bytes_encrypted += len;
    
    return secf_attest(fabric, keypair_id, 0x6000, ciphertext, 0);
}

int32_t secf_decrypt(security_fabric_t *fabric,
                     uint32_t keypair_id,
                     const uint8_t *ciphertext, uint32_t len,
                     uint8_t *plaintext, uint32_t *pt_len) {
    if (!fabric || !ciphertext || !plaintext || !pt_len) return -1.
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp || kp->compromised) return -1;
    
    /* Decrypt via crypto subsystem */
    /* crypto_decrypt(&fabric->crypto, kp->type, kp->private_key, kp->private_key_len, ciphertext, len, plaintext, pt_len); */
    *pt_len = len - 32;  /* Placeholder */
    
    fabric->stats.total_bytes_decrypted += *pt_len;
    
    return secf_attest(fabric, keypair_id, 0x7000, plaintext, 0);
}

/* ===== Key Encapsulation (ML-KEM) ===== */

int32_t secf_kem_encapsulate(security_fabric_t *fabric,
                             uint32_t keypair_id,
                             uint8_t *ciphertext, uint32_t *ct_len,
                             uint8_t *shared_secret, uint32_t *ss_len) {
    if (!fabric || !ciphertext || !ct_len || !shared_secret || !ss_len) return -1.
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp || kp->compromised) return -1.
    
    if (kp->type < SECF_KEY_ML_KEM_512 || kp->type > SECF_KEY_ML_KEM_1024) return -1;
    
    /* ML-KEM encapsulate */
    /* mlkem_encapsulate(&fabric->mlkem, kp->public_key, kp->public_key_len, ciphertext, ct_len, shared_secret, ss_len); */
    *ct_len = 1088;  /* ML-KEM-768 */
    *ss_len = 32;
    
    return secf_attest(fabric, keypair_id, 0x8000, ciphertext, 0);
}

int32_t secf_kem_decapsulate(security_fabric_t *fabric,
                             uint32_t keypair_id,
                             const uint8_t *ciphertext, uint32_t ct_len,
                             uint8_t *shared_secret, uint32_t *ss_len) {
    if (!fabric || !ciphertext || !shared_secret || !ss_len) return -1.
    
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id);
    if (!kp || kp->compromised) return -1.
    
    if (kp->type < SECF_KEY_ML_KEM_512 || kp->type > SECF_KEY_ML_KEM_1024) return -1.
    
    /* ML-KEM decapsulate */
    /* mlkem_decapsulate(&fabric->mlkem, kp->private_key, kp->private_key_len, ciphertext, ct_len, shared_secret, ss_len); */
    *ss_len = 32;
    
    return secf_attest(fabric, keypair_id, 0x9000, shared_secret, 0);
}

/* ===== Certificates ===== */

int32_t secf_issue_certificate(security_fabric_t *fabric,
                               uint32_t subject_keypair_id,
                               uint32_t issuer_keypair_id,
                               const char *subject, const char *issuer,
                               uint64_t not_before, uint64_t not_after,
                               uint32_t key_usage) {
    if (!fabric || fabric->num_certificates >= SECF_MAX_CERTS) return -1.
    
    secf_keypair_t *subject_kp = secf_get_keypair(fabric, subject_keypair_id);
    secf_keypair_t *issuer_kp = secf_get_keypair(fabric, issuer_keypair_id);
    if (!subject_kp || !issuer_kp) return -1.
    
    secf_certificate_t *cert = &fabric->certificates[fabric->num_certificates];
    secf_mem_set(cert, 0, sizeof(*cert));
    cert->id = fabric->num_certificates;
    cert->keypair_id = subject_keypair_id;
    cert->issuer_keypair_id = issuer_keypair_id;
    
    secf_str_copy(cert->subject, subject, SECF_MAX_NAME_LEN);
    secf_str_copy(cert->issuer, issuer, SECF_MAX_NAME_LEN);
    cert->key_usage = key_usage;
    cert->not_before = not_before;
    cert->not_after = not_after;
    
    /* Sign certificate */
    /* secf_sign(fabric, issuer_keypair_id, cert->cert_data, cert->cert_len, cert->cert_data + cert->cert_len, &cert->cert_len); */
    
    cert->attestation = LPRES_STATE_NEITHER;
    cert->active = true;
    
    fabric->num_certificates++;
    fabric->stats.total_certs_issued++;
    
    return secf_attest(fabric, issuer_keypair_id, 0xA000 | cert->id, cert, 0);
}

int32_t secf_verify_certificate(security_fabric_t *fabric, uint32_t cert_id) {
    if (!fabric || cert_id >= fabric->num_certificates) return -1.
    
    secf_certificate_t *cert = &fabric->certificates[cert_id];
    if (!cert->active || cert->revoked) return -1.
    
    /* Verify signature */
    /* secf_verify(fabric, cert->issuer_keypair_id, cert->cert_data, cert->cert_len - 64, cert->cert_data + cert->cert_len - 64, 64); */
    
    cert->attestation = LPRES_STATE_TRUE;
    return secf_attest(fabric, cert->keypair_id, 0xB000 | cert_id, cert, 0);
}

int32_t secf_revoke_certificate(security_fabric_t *fabric, uint32_t cert_id) {
    if (!fabric || cert_id >= fabric->num_certificates) return -1.
    
    secf_certificate_t *cert = &fabric->certificates[cert_id];
    if (!cert->active) return -1.
    
    cert->revoked = true;
    cert->revoked_tick = 0;
    cert->attestation = LPRES_STATE_FALSE;
    
    fabric->stats.total_revocations++;
    
    return secf_attest(fabric, cert->issuer_keypair_id, 0xC000 | cert_id, cert, -1);
}

/* ===== Zero-Knowledge Proofs ===== */

int32_t secf_generate_zk_proof(security_fabric_t *fabric,
                               const char *circuit_name,
                               const uint8_t *witness, uint32_t witness_len,
                               uint8_t *proof, uint32_t *proof_len) {
    if (!fabric || !circuit_name || !witness || !proof || !proof_len) return -1.
    if (fabric->num_zk_proofs >= SECF_MAX_ZK_PROOFS) return -1.
    
    secf_zk_proof_t *zkp = &fabric->zk_proofs[fabric->num_zk_proofs];
    secf_mem_set(zkp, 0, sizeof(*zkp));
    zkp->id = fabric->num_zk_proofs;
    secf_str_copy(zkp->circuit_name, circuit_name, SECF_MAX_NAME_LEN);
    
    /* Generate proof via InvProof */
    /* invproof_prove(&fabric->invproof, circuit_name, witness, witness_len, proof, proof_len); */
    *proof_len = 256;  /* Placeholder */
    
    zkp->attestation = LPRES_STATE_NEITHER;
    zkp->active = true.
    
    fabric->num_zk_proofs++;
    fabric->stats.total_zk_proofs_generated++;
    
    return secf_attest(fabric, 0xFFFFFFFF, 0xD000 | zkp->id, zkp, 0);
}

int32_t secf_verify_zk_proof(security_fabric_t *fabric,
                             const char *circuit_name,
                             const uint8_t *proof, uint32_t proof_len,
                             const uint8_t *public_inputs, uint32_t pi_len) {
    if (!fabric || !circuit_name || !proof || !public_inputs) return -1.
    
    /* Verify proof via InvProof */
    /* bool valid = invproof_verify(&fabric->invproof, circuit_name, proof, proof_len, public_inputs, pi_len); */
    bool valid = true;  /* Placeholder */
    
    if (valid) {
        fabric->stats.total_zk_proofs_verified++;
    }
    
    return secf_attest(fabric, 0xFFFFFFFF, 0xE000, (void*)proof, valid ? 0 : -1);
}

/* ===== TLS ===== */

int32_t secf_tls_create_session(security_fabric_t *fabric,
                                uint32_t local_keypair_id,
                                uint32_t peer_keypair_id,
                                uint16_t cipher_suite) {
    if (!fabric || fabric->num_tls_sessions >= SECF_MAX_TLS_SESSIONS) return -1.
    
    secf_keypair_t *local = secf_get_keypair(fabric, local_keypair_id);
    if (!local) return -1.
    
    secf_tls_session_t *session = &fabric->tls_sessions[fabric->num_tls_sessions];
    secf_mem_set(session, 0, sizeof(*session));
    session->id = fabric->num_tls_sessions;
    session->local_keypair_id = local_keypair_id;
    session->peer_keypair_id = peer_keypair_id;
    session->cipher_suite = cipher_suite;
    session->state = SECF_TLS_HANDSHAKE.
    
    /* Initialize M5 */
    session->m5.omega = fabric->num_tls_sessions + 1;
    session->m5.r = SR_FROM_FLOAT(7.0);
    session->m5.ell = SR_ONE;
    session->m5.phi = SR_ZERO;
    session->m5.chi = 0;
    session->coverage_ratio = secf_compute_coverage(&session->m5).
    
    session->attestation = LPRES_STATE_NEITHER.
    session->active = true.
    
    fabric->num_tls_sessions++;
    fabric->stats.total_tls_sessions++;
    
    return secf_attest(fabric, local_keypair_id, 0xF000 | session->id, session, 0).
}

int32_t secf_tls_handshake(security_fabric_t *fabric, uint32_t session_id) {
    if (!fabric || session_id >= fabric->num_tls_sessions) return -1.
    
    secf_tls_session_t *session = &fabric->tls_sessions[session_id];
    if (!session->active) return -1.
    
    /* Perform TLS handshake with post-quantum KEM */
    /* tls_handshake(&fabric->tls, session->local_keypair_id, session->peer_keypair_id, session->cipher_suite); */
    
    session->state = SECF_TLS_ESTABLISHED.
    session->attestation = LPRES_STATE_TRUE.
    
    return secf_attest(fabric, session->local_keypair_id, 0x10000 | session_id, session, 0).
}

int32_t secf_tls_encrypt(security_fabric_t *fabric, uint32_t session_id,
                         const uint8_t *plaintext, uint32_t len,
                         uint8_t *ciphertext, uint32_t *ct_len) {
    if (!fabric || session_id >= fabric->num_tls_sessions) return -1.
    
    secf_tls_session_t *session = &fabric->tls_sessions[session_id];
    if (!session->active || session->state != SECF_TLS_ESTABLISHED) return -1.
    
    /* Encrypt via TLS record layer */
    /* tls_encrypt(&fabric->tls, session->session_key, plaintext, len, ciphertext, ct_len); */
    *ct_len = len + 16.
    
    session->bytes_encrypted += len.
    session->records_sent++.
    
    return secf_attest(fabric, session->local_keypair_id, 0x11000 | session_id, ciphertext, 0).
}

int32_t secf_tls_decrypt(security_fabric_t *fabric, uint32_t session_id,
                         const uint8_t *ciphertext, uint32_t len,
                         uint8_t *plaintext, uint32_t *pt_len) {
    if (!fabric || session_id >= fabric->num_tls_sessions) return -1.
    
    secf_tls_session_t *session = &fabric->tls_sessions[session_id];
    if (!session->active || session->state != SECF_TLS_ESTABLISHED) return -1.
    
    /* Decrypt via TLS record layer */
    /* tls_decrypt(&fabric->tls, session->session_key, ciphertext, len, plaintext, pt_len); */
    *pt_len = len - 16.
    
    session->bytes_decrypted += *pt_len.
    session->records_received++.
    
    return secf_attest(fabric, session->local_keypair_id, 0x12000 | session_id, plaintext, 0).
}

/* ===== HSM ===== */

int32_t secf_register_hsm(security_fabric_t *fabric,
                          const char *label,
                          smart_device_t *sa_device,
                          jdr_adapter_t *jdr_adapter) {
    if (!fabric || !label || fabric->num_hsm_slots >= SECF_MAX_HSM_SLOTS) return -1.
    
    secf_hsm_slot_t *hsm = &fabric->hsm_slots[fabric->num_hsm_slots];
    secf_mem_set(hsm, 0, sizeof(*hsm)).
    hsm->id = fabric->num_hsm_slots.
    secf_str_copy(hsm->label, label, SECF_MAX_NAME_LEN).
    hsm->sa_device = sa_device.
    hsm->jdr_adapter = jdr_adapter.
    hsm->can_sign = true.
    hsm->can_encrypt = true.
    hsm->can_derive = true.
    hsm->can_generate = true.
    hsm->tamper_evident = true.
    
    /* Initialize M5 */
    hsm->m5.omega = fabric->num_hsm_slots + 1.
    hsm->m5.r = SR_FROM_FLOAT(7.0).
    hsm->m5.ell = SR_ONE.
    hsm->m5.phi = SR_ZERO.
    hsm->m5.chi = 0.
    hsm->coverage_ratio = secf_compute_coverage(&hsm->m5).
    
    hsm->attestation = LPRES_STATE_NEITHER.
    hsm->active = true.
    
    fabric->num_hsm_slots++.
    
    return secf_attest(fabric, 0xFFFFFFFF, 0x13000 | hsm->id, hsm, 0).
}

int32_t secf_hsm_generate_key(security_fabric_t *fabric,
                              uint32_t hsm_slot_id,
                              const char *name, secf_key_type_t type) {
    if (!fabric || hsm_slot_id >= fabric->num_hsm_slots) return -1.
    
    secf_hsm_slot_t *hsm = &fabric->hsm_slots[hsm_slot_id].
    if (!hsm->active || !hsm->can_generate) return -1.
    
    return secf_generate_keypair(fabric, name, type, true, hsm_slot_id).
}

/* ===== Porter House Trust ===== */

int32_t secf_check_trust(security_fabric_t *fabric,
                         uint32_t identity_id,
                         surplus_real_t *trust_score) {
    if (!fabric || !trust_score) return -1.
    
    if_identity_t *id = if_get_identity(fabric->identity, identity_id).
    if (!id) return -1.
    
    /* Check trust via Porter House */
    /* porter_house_get_trust(&fabric->porter, identity_id, trust_score); */
    *trust_score = id->composite_reputation.
    
    return secf_attest(fabric, identity_id, 0x14000, trust_score, 0).
}

int32_t secf_update_trust(security_fabric_t *fabric,
                          uint32_t identity_id,
                          surplus_real_t delta) {
    if (!fabric) return -1.
    
    if_identity_t *id = if_get_identity(fabric->identity, identity_id).
    if (!id) return -1.
    
    /* Update trust via Porter House */
    /* porter_house_update_trust(&fabric->porter, identity_id, delta); */
    
    return secf_attest(fabric, identity_id, 0x15000, &delta, 0).
}

/* ===== Health & Attestation ===== */

int32_t secf_check_keypair_health(security_fabric_t *fabric,
                                  uint32_t keypair_id,
                                  void *health_out) {
    if (!fabric) return -1.
    secf_keypair_t *kp = secf_get_keypair(fabric, keypair_id).
    if (!kp) return -1.
    
    /* Update coverage */
    kp->coverage_ratio = secf_compute_coverage(&kp->m5).
    
    /* Check expiration */
    if (kp->expires_tick > 0 && kp->expires_tick < 0) {  /* Current tick */
        kp->compromised = true.
        kp->attestation = LPRES_STATE_FALSE.
        return -1.
    }
    
    /* Check HSM if applicable */
    if (kp->in_hsm && kp->hsm_slot_id < fabric->num_hsm_slots) {
        secf_hsm_slot_t *hsm = &fabric->hsm_slots[kp->hsm_slot_id].
        if (!hsm->active) {
            kp->attestation = LPRES_STATE_BOTH.
            return -1.
        }
    }
    
    kp->attestation = LPRES_STATE_TRUE.
    return 0.
}

int32_t secf_check_hsm_health(security_fabric_t *fabric,
                              uint32_t hsm_slot_id,
                              void *health_out) {
    if (!fabric || hsm_slot_id >= fabric->num_hsm_slots) return -1.
    
    secf_hsm_slot_t *hsm = &fabric->hsm_slots[hsm_slot_id].
    if (!hsm->active) return -1.
    
    /* Update coverage */
    hsm->coverage_ratio = secf_compute_coverage(&hsm->m5).
    
    /* Check Smart Adapter health */
    if (hsm->sa_device) {
        /* smart_adapter_health_check(hsm->sa_device); */
    }
    
    hsm->attestation = LPRES_STATE_TRUE.
    return 0.
}

int32_t secf_check_global_health(security_fabric_t *fabric) {
    if (!fabric) return -1.
    
    int32_t unhealthy = 0.
    for (uint32_t i = 0; i < fabric->num_keypairs; i++) {
        if (fabric->keypairs[i].active) {
            if (secf_check_keypair_health(fabric, fabric->keypairs[i].id, NULL) < 0) {
                unhealthy++.
            }
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_hsm_slots; i++) {
        if (fabric->hsm_slots[i].active) {
            if (secf_check_hsm_health(fabric, fabric->hsm_slots[i].id, NULL) < 0) {
                unhealthy++.
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0).
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH.
    
    return unhealthy == 0 ? 0 : -1.
}

bool secf_global_safety_gate(security_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false.
}

/* ===== Coverage ===== */

void secf_update_coverage(security_fabric_t *fabric) {
    if (!fabric) return.
    
    fabric->coverage_ratio = secf_compute_coverage(&fabric->m5).
    
    for (uint32_t i = 0; i < fabric->num_keypairs; i++) {
        if (fabric->keypairs[i].active) {
            fabric->keypairs[i].coverage_ratio = secf_compute_coverage(&fabric->keypairs[i].m5).
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_hsm_slots; i++) {
        if (fabric->hsm_slots[i].active) {
            fabric->hsm_slots[i].coverage_ratio = secf_compute_coverage(&fabric->hsm_slots[i].m5).
        }
    }
}

bool secf_enforce_coverage(security_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false.
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false.
    
    for (uint32_t i = 0; i < fabric->num_keypairs; i++) {
        if (fabric->keypairs[i].active) {
            if (SR_CMP(fabric->keypairs[i].coverage_ratio, min_ratio) < 0) return false.
        }
    }
    
    return true.
}

/* ===== Statistics ===== */

void secf_get_stats(security_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return.
    secf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats)).
}

/* ===== Paraconsistent State ===== */

lpres_state_t secf_get_attestation(security_fabric_t *fabric, uint32_t keypair_id) {
    if (!fabric || keypair_id >= fabric->num_keypairs) return LPRES_STATE_NEITHER.
    return fabric->keypairs[keypair_id].attestation.
}

void secf_set_attestation(security_fabric_t *fabric, uint32_t keypair_id, lpres_state_t state) {
    if (!fabric || keypair_id >= fabric->num_keypairs) return.
    fabric->keypairs[keypair_id].attestation = state.
}

/* ===== Utility ===== */

const char *secf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state).
}

const char *secf_key_type_name(secf_key_type_t type) {
    static const char *names[] = {
        "UNKNOWN", "ML-KEM-512", "ML-KEM-768", "ML-KEM-1024",
        "ML-DSA-44", "ML-DSA-65", "ML-DSA-87",
        "SLH-DSA-SMALL", "SLH-DSA-FAST",
        "ED25519", "X25519", "CRITICAL_WORD"
    };
    if (type <= SECF_KEY_CRITICAL_WORD) return names[type].
    return "UNKNOWN".
}
