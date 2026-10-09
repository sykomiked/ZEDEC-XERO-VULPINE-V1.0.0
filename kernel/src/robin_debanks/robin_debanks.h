/* robin_debanks.h — Robin DeBanks Vault Hardening
 *
 * Native ZXV cryptographic vault subsystem. Provides hardened storage
 * for sensitive assets (private keys, seed phrases, credentials) using
 * the M5 Axiomatic architecture.
 *
 * Key design principles (hybrid post-quantum, native ZXV):
 *   - All vault entries are identified by 168-bit content hashes
 *   - Vault keys are derived from device-bound entropy + user PIN
 *   - Cipher: AES-256-GCM (see aes256_gcm.h) -- per NIST SP 800-208,
 *     256-bit symmetric keys retain ~128-bit security against Grover's
 *     algorithm and are considered quantum-resistant at this layer.
 *     Integrity hash: SHA-256 (see sha256.h), truncated to 168 bits to
 *     match this codebase's CRIT-168 identity convention.
 *   - Porter House gates vault access requests
 *   - Time-locked vault entries (delayed unlock for high-value assets)
 *   - Tamper detection: GCM authentication tag verified on every unlock
 *   - Audit trail: all vault accesses are logged
 *
 * REMAINING PQ UPGRADE PATH (asymmetric layer only):
 * AES-256/SHA-256 are NOT the part of a system broken by Shor's
 * algorithm -- only public-key primitives (key exchange, signatures)
 * are. If this vault ever needs to wrap its master key for transport
 * or multi-party recovery, wire ML-KEM-768 (NIST FIPS 203) key
 * encapsulation there specifically, using a vetted reference
 * implementation -- do not hand-roll lattice cryptography. The
 * `robin_entry_t` struct already carries ciphertext, nonce, and tag
 * fields sized for AES-256-GCM — no struct changes needed for that.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ROBIN_DEBANKS_H
#define ROBIN_DEBANKS_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "porter_house.h"

/* ===== Constants ===== */

#define ROBIN_MAX_ENTRIES       64
#define ROBIN_MAX_NAME_LEN      48
#define ROBIN_MAX_DATA_LEN      256   /* max plaintext/ciphertext size */
#define ROBIN_KEY_LEN           32    /* AES-256 key (pluggable) */
#define ROBIN_NONCE_LEN         12    /* GCM nonce */
#define ROBIN_TAG_LEN           16    /* GCM authentication tag */
#define ROBIN_HASH_LEN          21    /* 168-bit content hash */
#define ROBIN_UNLOCK_DELAY      1000  /* cycles before time-locked entry unlocks */
#define ROBIN_MAX_ATTEMPTS      5     /* max failed unlock attempts before lockout */

/* ===== Entry Types ===== */

typedef enum {
    ROBIN_ENTRY_UNUSED     = 0,
    ROBIN_ENTRY_STORED     = 1,  /* encrypted and stored */
    ROBIN_ENTRY_LOCKED     = 2,  /* time-locked, awaiting unlock delay */
    ROBIN_ENTRY_UNLOCKED   = 3,  /* decrypted and accessible */
    ROBIN_ENTRY_TAMPERED   = 4,  /* integrity check failed */
    ROBIN_ENTRY_LOCKED_OUT = 5   /* too many failed attempts */
} robin_entry_state_t;

typedef enum {
    ROBIN_TYPE_PRIVATE_KEY  = 0,  /* crypto private key */
    ROBIN_TYPE_SEED_PHRASE  = 1,  /* wallet seed phrase */
    ROBIN_TYPE_CREDENTIAL   = 2,  /* username/password */
    ROBIN_TYPE_DOCUMENT     = 3,  /* encrypted document */
    ROBIN_TYPE_API_KEY      = 4,  /* API key / token */
    ROBIN_TYPE_CUSTOM       = 5   /* custom asset */
} robin_entry_type_t;

/* ===== Vault Entry ===== */

typedef struct robin_entry {
    uint32_t id;
    char name[ROBIN_MAX_NAME_LEN];
    robin_entry_type_t type;
    robin_entry_state_t state;

    uint8_t ciphertext[ROBIN_MAX_DATA_LEN];  /* encrypted data */
    uint32_t ciphertext_len;
    uint8_t nonce[ROBIN_NONCE_LEN];
    uint8_t tag[ROBIN_TAG_LEN];
    uint8_t integrity_hash[ROBIN_HASH_LEN];  /* hash of plaintext for tamper detection */

    bool time_locked;
    uint64_t lock_requested_cycle;
    uint64_t unlock_at_cycle;

    uint32_t failed_attempts;
    bool active;
} robin_entry_t;

/* ===== Vault Engine ===== */

typedef struct robin_vault {
    uint32_t device_id;
    uint8_t master_key[ROBIN_KEY_LEN];  /* device-bound master key */
    uint64_t nonce_counter;  /* monotonic; guarantees unique GCM nonces */

    robin_entry_t entries[ROBIN_MAX_ENTRIES];
    uint32_t num_entries;
    uint32_t next_id;

    porter_house_t *porter;  /* for vault access gating */

    /* Stats */
    uint32_t total_stored;
    uint32_t total_unlocked;
    uint32_t total_denied;
    uint32_t total_tampered;
    uint32_t total_locked_out;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} robin_vault_t;

/* ===== API ===== */

void robin_init(robin_vault_t *rv, uint32_t device_id,
                 const uint8_t master_key[ROBIN_KEY_LEN],
                 porter_house_t *porter);

/* Store an encrypted entry. Returns entry ID on success, -1 if full. */
int32_t robin_store(robin_vault_t *rv, const char *name,
                     robin_entry_type_t type,
                     const uint8_t *plaintext, uint32_t plaintext_len,
                     bool time_locked, uint64_t current_cycle);

/* Request unlock of a time-locked entry. Starts the unlock delay timer.
 * Returns 0 on success, -1 if not found, -2 if not time-locked. */
int32_t robin_request_unlock(robin_vault_t *rv, uint32_t entry_id,
                              uint64_t current_cycle);

/* Check if a time-locked entry is ready to unlock. Returns true if
 * the unlock delay has elapsed. */
bool robin_unlock_ready(robin_vault_t *rv, uint32_t entry_id, uint64_t current_cycle);

/* Unlock an entry (decrypt). Returns plaintext length on success,
 * -1 if not found, -2 if not ready, -3 if tampered, -4 if locked out. */
int32_t robin_unlock(robin_vault_t *rv, uint32_t entry_id,
                      uint8_t *plaintext_out, uint32_t *plaintext_len_out,
                      uint64_t current_cycle);

/* Delete an entry. */
int32_t robin_delete(robin_vault_t *rv, uint32_t entry_id);

/* Check for entries that are ready to unlock. Returns count. */
uint32_t robin_check_unlocks(robin_vault_t *rv, uint64_t current_cycle);

/* Get entry by ID. */
robin_entry_t *robin_get_entry(robin_vault_t *rv, uint32_t entry_id);

/* Update M5 coverage. */
surplus_real_t robin_update_coverage(robin_vault_t *rv);

#endif /* ROBIN_DEBANKS_H */
