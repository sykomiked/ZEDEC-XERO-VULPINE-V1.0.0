/* crypto_wallet.h — 5-Key Vector Cryptographic File System
 *
 * The "Wallet is the OS" architecture:
 *   - BIOS/Substrate holds the Master Root Seed
 *   - HKDF-Expand derives 5 keys from the root, one per polar trit state
 *   - 5 file archetypes map to 5 logic phases (TRUE, FALSE, GLUT+, GLUT-, GLUT0)
 *   - Content-addressable storage: files referenced by Merkle hash, not by path
 *   - Zero-copy phase shifting: re-sign a Merkle pointer to change file phase
 *   - Cross-wallet interlocking hash lattice for self-verifying integrity
 *
 * 5-Key Vector:
 *   K1 (TRUE)      → .36n9  — Executable / Real-Time (fully verified)
 *   K2 (FALSE)     → .9n63  — Null / Shadow / Tombstone (zeroed data)
 *   K3 (GLUT_PLUS) → .zedec — Speculative Stream (optimistic execution)
 *   K4 (GLUT_MINUS)→ .vino  — Ledger / Defensive (immutable audit history)
 *   K5 (GLUT_NEUTRAL)→ .ula — Zero-Point Substrate (cold storage / BIOS manifest)
 *
 * Hardware-as-code: implemented as a virtual cryptographic device with
 * registers, DMA (key stream), and IRQ (integrity violation, phase shift).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */
#ifndef CRYPTO_WALLET_H
#define CRYPTO_WALLET_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define CW_MAX_KEY_LEN        64    /* 512-bit keys */
#define CW_MAX_HASH_LEN       32    /* SHA-256 / BLAKE3 hash size */
#define CW_MAX_SEED_LEN       64    /* Master root seed */
#define CW_MAX_WALLETS        16    /* Maximum wallets in the lattice */
#define CW_MAX_FILES_PER_WALLET 64
#define CW_MAX_PATH_LEN       128
#define CW_MAX_LABEL_LEN      64
#define CW_DMA_BUFFER_SIZE    8192
#define CW_NUM_REGISTERS      16

/* ===== 5-Key Vector ===== */

typedef enum {
    CW_KEY_TRUE       = 0,  /* K1 — .36n9 — verified execution */
    CW_KEY_FALSE      = 1,  /* K2 — .9n63 — null/shadow/tombstone */
    CW_KEY_GLUT_PLUS  = 2,  /* K3 — .zedec — speculative stream */
    CW_KEY_GLUT_MINUS = 3,  /* K4 — .vino — defensive ledger */
    CW_KEY_GLUT_NEUTRAL = 4, /* K5 — .ula — cold storage / BIOS */
    CW_KEY_MAX        = 5
} cw_key_id_t;

/* ===== File Archetype → Logic Phase Mapping ===== */

typedef enum {
    CW_FILE_36N9   = 0,  /* TRUE — executable / real-time */
    CW_FILE_9N63   = 1,  /* FALSE — null / shadow / tombstone */
    CW_FILE_ZEDEC  = 2,  /* GLUT_PLUS — speculative stream */
    CW_FILE_VINO   = 3,  /* GLUT_MINUS — defensive ledger */
    CW_FILE_ULA    = 4,  /* GLUT_NEUTRAL — zero-point substrate */
    CW_FILE_MAX    = 5
} cw_file_type_t;

/* ===== Key Material ===== */

typedef struct {
    cw_key_id_t id;
    uint8_t key[CW_MAX_KEY_LEN];      /* Derived key material */
    uint32_t key_len;
    char label[CW_MAX_LABEL_LEN];     /* Human-readable key label */
    trit_t logic_phase;               /* Associated polar trit state */
    cw_file_type_t file_type;         /* Associated file archetype */
    uint8_t chain_code[CW_MAX_HASH_LEN]; /* HKDF chain code for child derivation */
} cw_key_t;

/* ===== Content-Addressable File Entry ===== */

typedef struct {
    uint8_t content_hash[CW_MAX_HASH_LEN];  /* SHA-256 / BLAKE3 of payload */
    uint32_t payload_size;
    cw_file_type_t file_type;
    cw_key_id_t signing_key;               /* Which key signed this entry */
    uint8_t signature[CW_MAX_HASH_LEN];    /* HMAC signature */
    uint8_t cross_ref_hash[CW_MAX_HASH_LEN]; /* Interlocking hash from peer wallet */
    uint32_t cross_ref_wallet_id;           /* Which wallet's hash we store */
    trit_t current_phase;                   /* Current logic phase (may shift) */
    bool integrity_verified;
    char path[CW_MAX_PATH_LEN];            /* Human-readable path (optional) */
} cw_file_entry_t;

/* ===== Wallet (Directory Container) ===== */

typedef struct {
    uint32_t wallet_id;
    char label[CW_MAX_LABEL_LEN];

    /* Key set — each wallet holds all 5 keys but with different derivation paths */
    cw_key_t keys[CW_KEY_MAX];

    /* File entries in this wallet */
    cw_file_entry_t files[CW_MAX_FILES_PER_WALLET];
    uint32_t num_files;

    /* Merkle root of all file hashes in this wallet */
    uint8_t merkle_root[CW_MAX_HASH_LEN];

    /* Cross-wallet interlocking references */
    uint8_t peer_merkle_roots[16][CW_MAX_HASH_LEN];
    uint32_t peer_wallet_ids[16];
    uint32_t num_peers;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* Integrity state */
    bool integrity_ok;
    uint32_t integrity_failures;
} cw_wallet_t;

/* ===== Root Seed / BIOS Substrate ===== */

typedef struct {
    uint8_t master_seed[CW_MAX_SEED_LEN];
    uint32_t seed_len;
    uint8_t bios_hash[CW_MAX_HASH_LEN];   /* Hash of BIOS/bootloader */
    char bios_label[CW_MAX_LABEL_LEN];
} cw_root_seed_t;

/* ===== Cryptographic Wallet System (hardware-as-code device) ===== */

typedef struct {
    /* Device identity */
    uint32_t device_id;
    char name[CW_MAX_LABEL_LEN];

    /* Register file */
    uint64_t registers[CW_NUM_REGISTERS];

    /* DMA buffers */
    uint8_t tx_dma[CW_DMA_BUFFER_SIZE];   /* Key derivation input */
    uint8_t rx_dma[CW_DMA_BUFFER_SIZE];   /* Key/hash output */
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ lines */
    bool irq_integrity_violation;
    bool irq_phase_shift;
    bool irq_key_derivation_complete;
    bool irq_merkle_update;

    /* Root seed (BIOS substrate) */
    cw_root_seed_t root;

    /* Wallet lattice */
    cw_wallet_t wallets[CW_MAX_WALLETS];
    uint32_t num_wallets;

    /* System-wide Merkle root (root of all wallet Merkle roots) */
    uint8_t system_merkle_root[CW_MAX_HASH_LEN];

    /* Statistics */
    uint64_t total_phase_shifts;
    uint64_t total_integrity_checks;
    uint64_t total_key_derivations;
    uint64_t integrity_failures;
} crypto_wallet_system_t;

/* ===== Register definitions ===== */

typedef enum {
    CW_REG_ROOT_SEED     = 0,  /* Write: root seed input */
    CW_REG_SEED_LEN      = 1,  /* Write: seed length */
    CW_REG_DERIVE_KEY    = 2,  /* Write: trigger key derivation */
    CW_REG_KEY_ID        = 3,  /* Write: which key to derive */
    CW_REG_WALLET_ID     = 4,  /* Write: target wallet */
    CW_REG_FILE_TYPE     = 5,  /* Write: file archetype */
    CW_REG_PHASE_SHIFT   = 6,  /* Write: trigger phase shift */
    CW_REG_INTEGRITY     = 7,  /* Write: trigger integrity check */
    CW_REG_STATUS        = 8,  /* Read: system status */
    CW_REG_MERKLE_ROOT   = 9,  /* Read: system Merkle root */
    CW_REG_WALLET_COUNT  = 10, /* Read: number of wallets */
    CW_REG_FILE_COUNT    = 11, /* Read: total files across wallets */
} cw_reg_t;

/* ===== Status bits ===== */
#define CW_STATUS_IDLE          0x00
#define CW_STATUS_DERIVING      0x01
#define CW_STATUS_HASHING       0x02
#define CW_STATUS_VERIFYING     0x04
#define CW_STATUS_PHASE_SHIFT   0x08
#define CW_STATUS_DONE          0x10
#define CW_STATUS_ERROR         0x20
#define CW_STATUS_INTEGRITY_FAIL 0x40

/* ===== API ===== */

/* System lifecycle */
void cw_system_init(crypto_wallet_system_t *sys, uint32_t device_id, const char *name);
void cw_system_set_root_seed(crypto_wallet_system_t *sys, const uint8_t *seed, uint32_t len);

/* Key derivation — HKDF-Expand from root seed */
int cw_derive_key(crypto_wallet_system_t *sys, uint32_t wallet_id, cw_key_id_t key_id);
int cw_derive_all_keys(crypto_wallet_system_t *sys, uint32_t wallet_id);

/* Wallet management */
uint32_t cw_wallet_create(crypto_wallet_system_t *sys, const char *label);
cw_wallet_t *cw_wallet_get(crypto_wallet_system_t *sys, uint32_t wallet_id);

/* File operations — content-addressable */
int32_t cw_file_add(crypto_wallet_system_t *sys, uint32_t wallet_id,
                     cw_file_type_t type, const uint8_t *payload, uint32_t size,
                     const char *path);
cw_file_entry_t *cw_file_lookup(crypto_wallet_system_t *sys, uint32_t wallet_id,
                                 const uint8_t *content_hash);

/* Phase shifting — zero-copy file phase change via re-signing */
int cw_file_phase_shift(crypto_wallet_system_t *sys, uint32_t wallet_id,
                         uint32_t file_idx, cw_key_id_t new_key);

/* Merkle tree operations */
int cw_wallet_update_merkle(crypto_wallet_system_t *sys, uint32_t wallet_id);
int cw_system_update_merkle(crypto_wallet_system_t *sys);

/* Cross-wallet interlocking */
int cw_wallet_add_peer(crypto_wallet_system_t *sys, uint32_t wallet_id,
                        uint32_t peer_id);
int cw_wallet_sync_peer_hash(crypto_wallet_system_t *sys, uint32_t wallet_id,
                              uint32_t peer_id);

/* Integrity verification */
bool cw_verify_integrity(crypto_wallet_system_t *sys, uint32_t wallet_id);
bool cw_verify_system_integrity(crypto_wallet_system_t *sys);

/* Hash functions (freestanding — no external crypto library) */
void cw_sha256(const uint8_t *data, uint32_t len, uint8_t *out);
void cw_hmac_sha256(const uint8_t *key, uint32_t key_len,
                     const uint8_t *data, uint32_t data_len, uint8_t *out);

/* Register access (hardware-as-code) */
uint64_t cw_reg_read(crypto_wallet_system_t *sys, cw_reg_t reg);
void cw_reg_write(crypto_wallet_system_t *sys, cw_reg_t reg, uint64_t value);

/* Utility */
const char *cw_key_name(cw_key_id_t id);
const char *cw_file_type_name(cw_file_type_t type);
const char *cw_file_extension(cw_file_type_t type);
trit_t cw_file_type_to_phase(cw_file_type_t type);
cw_key_id_t cw_file_type_to_key(cw_file_type_t type);

/* IRQ handling */
void cw_handle_irq(crypto_wallet_system_t *sys);

#endif /* CRYPTO_WALLET_H */
