/* tripartite_fs.h — ZXV Tripartite File System Integration Layer
 *
 * Universal integration of the holographic tripartite file system
 * (positive/negative/neutral context files with custom extensions)
 * across all kernel subsystems.
 *
 * File Extensions & Logic Phases:
 *   .36n9   — Positive Space (TRUE)      — Executable / Real-Time
 *   .9n63   — Negative Space (FALSE)     — Null / Shadow / Tombstone
 *   .36m9   — Positive Manifest (GLUT+)  — Speculative Stream
 *   .zedei  — Positive Transform (GLUT+) — Transform / Render
 *   .zedec  — Positive Container (GLUT+) — Dataset / Bundle
 *   .9m63   — Negative Manifest (GLUT-)  — Anti-pattern
 *   .iedez  — Negative Transform (GLUT-) — Inverse Transform
 *   .cedez  — Negative Container (GLUT-) — Inverse Bundle
 *   .0n0    — Neutral Source (NEITHER)   — Source / Origin
 *   .0m0    — Neutral Manifest (NEITHER) — Manifest / Index
 *   .zedez  — Neutral Transform (GLUT0)  — Balanced Transform
 *   .cedec  — Neutral Container (GLUT0)  — Balanced Container
 *
 * Integration Points:
 *   1. VFS — File type detection, custom extensions, tripartite operations
 *   2. Storage Fabric — Content-addressable, replicated, encrypted
 *   3. Vena Runtime — App loading, phase shifting, smart contracts
 *   4. Crypto Wallet — 5-key vector, cross-wallet interlocking
 *   5. Crypto Bridge — Web2/Web3 asset bridging via tripartite
 *   6. Mesh Net — P2P file sharing via trade routes
 *   7. App Fabric — App sandboxing, capability-gated file access
 *   8. Identity Fabric — Credential-gated file access
 *   9. Governance Fabric — Policy-enforced file operations
 *   10. Media Fabric — Streaming, generative media via tripartite
 *   11. Holographic Renderer — Interference reconstruction
 *   12. App Constellation — Service mesh file operations
 *
 * Design Principles:
 * - Every file operation is paraconsistent (LPRES four-valued)
 * - M5 coverage ≥ 1.8 enforced on all file operations
 * - Self-audit/self-heal built into every file operation
 * - Economic settlement via Financial Fabric (Vino vouchers)
 * - Identity-gated via Identity Fabric (168-bit critical words)
 * - Schema translation via Orbital Elevator
 * - Content-addressable (CID-based) with Merkle proofs
 * - Cross-wallet interlocking hash lattice
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef TRIPARTITE_FS_H
#define TRIPARTITE_FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "holographic/holo.h"
#include "vfs/vfs.h"
#include "zxvfs/zxvfs.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "crypto_wallet.h"
#include "mesh_net.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "vfs/vfs.h"

/* ===== Constants ===== */

#define TF_MAX_FILES             4096
#define TF_MAX_CONTAINERS        1024
#define TF_MAX_DATASETS          256
#define TF_MAX_RENDERERS         64
#define TF_MAX_NAME_LEN          128
#define TF_CID_LEN               32

/* ===== Tripartite File Types (mirrors holographic) ===== */

typedef enum {
    TF_TYPE_36N9    = 1,   /* .36n9 — Positive Space (TRUE) */
    TF_TYPE_9N63    = 2,   /* .9n63 — Negative Space (FALSE) */
    TF_TYPE_36M9    = 3,   /* .36m9 — Positive Manifest (GLUT+) */
    TF_TYPE_ZEDEI   = 4,   /* .zedei — Positive Transform (GLUT+) */
    TF_TYPE_ZEDEC   = 5,   /* .zedec — Positive Container (GLUT+) */
    TF_TYPE_9M63    = 6,   /* .9m63 — Negative Manifest (GLUT-) */
    TF_TYPE_IEDEZ   = 7,   /* .iedez — Negative Transform (GLUT-) */
    TF_TYPE_CEDEZ   = 8,   /* .cedez — Negative Container (GLUT-) */
    TF_TYPE_0N0     = 9,   /* .0n0 — Neutral Source (NEITHER) */
    TF_TYPE_0M0     = 10,  /* .0m0 — Neutral Manifest (NEITHER) */
    TF_TYPE_ZEDEZ   = 11,  /* .zedez — Neutral Transform (GLUT0) */
    TF_TYPE_CEDEC   = 12,  /* .cedec — Neutral Container (GLUT0) */
    TF_TYPE_MAX
} tf_type_t;

/* ===== LPRES Logic Phase Mapping ===== */

typedef enum {
    TF_PHASE_TRUE      = 1,  /* .36n9, .36m9, .zedei, .zedec */
    TF_PHASE_FALSE     = 2,  /* .9n63, .9m63, .iedez, .cedez */
    TF_PHASE_GLUT_PLUS = 3,  /* .zedec, .zedei, .36m9 */
    TF_PHASE_GLUT_MINUS = 4, /* .vino, .cedez, .iedez, .9m63 */
    TF_PHASE_GLUT_NEUTRAL = 5, /* .0n0, .0m0, .zedez, .cedec */
    TF_PHASE_UNKNOWN   = 0
} tf_phase_t;

/* ===== Tripartite File Entry ===== */

typedef struct tf_file_entry {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    tf_type_t type;
    tf_phase_t phase;
    
    /* Content addressing */
    uint8_t cid[TF_CID_LEN];           /* SHA-256 / BLAKE3 content ID */
    uint64_t size;
    uint32_t payload_offset;
    uint32_t payload_length;
    
    /* Tripartite pairing */
    uint32_t pair_id;                  /* Pair with opposite phase */
    uint32_t dataset_id;               /* Dataset membership */
    uint32_t container_id;             /* Container membership */
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t lpres_state;
    lpres_state_t global_attestation;
    
    /* Economic */
    uint32_t financial_account_id;
    uint8_t pricing_form;
    uint64_t price_per_access;
    
    /* Identity */
    uint32_t creator_identity_id;
    uint32_t credential_id;
    
    /* Security */
    uint32_t keypair_id;
    bool encrypted;
    uint8_t encryption_key[32];
    
    /* Replication */
    uint8_t replication_factor;
    uint32_t replica_cids[8];
    uint32_t num_replicas;
    
    /* Mesh Net */
    uint32_t mesh_network_id;
    uint32_t trade_route_ids[8];
    uint32_t num_trade_routes;
    
    /* Statistics */
    uint64_t accesses;
    uint64_t modifications;
    uint64_t last_access_tick;
    
    /* Lifecycle */
    bool active;
    bool loaded;
    bool verified;
} tf_file_entry_t;

/* ===== Tripartite Pair ===== */

typedef struct tf_pair {
    uint32_t id;
    uint32_t positive_id;      /* .36n9, .36m9, .zedei, .zedec */
    uint32_t negative_id;      /* .9n63, .9m63, .iedez, .cedez */
    uint32_t neutral_id;       /* .0n0, .0m0, .zedez, .cedec (optional) */
    
    /* Pair metadata */
    uint32_t pair_checksum;
    uint32_t render_order;
    
    /* Interference reconstruction */
    holo_blend_mode_t blend_mode;
    uint8_t interference_pattern[256];
    
    /* Paraconsistent state */
    lpres_state_t pair_attestation;
    
    bool active;
} tf_pair_t;

/* ===== Dataset ===== */

typedef struct tf_dataset {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    uint32_t pair_ids[TF_MAX_FILES];
    uint32_t num_pairs;
    
    /* Renderer */
    uint32_t renderer_id;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    
    bool active;
} tf_dataset_t;

/* ===== Container ===== */

typedef struct tf_container {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    char signature[64];
    
    /* Entries */
    struct {
        uint32_t file_id;
        tf_type_t type;
        uint32_t offset;
        uint32_t length;
        uint8_t cid[TF_CID_LEN];
        uint8_t checksum[32];
    } entries[TF_MAX_FILES];
    uint32_t num_entries;
    uint64_t total_size;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    
    bool active;
} tf_container_t;

/* ===== Renderer ===== */

typedef struct tf_renderer {
    uint32_t id;
    holo_render_mode_t render_mode;
    holo_blend_mode_t blend_mode;
    uint32_t width, height, depth;
    uint32_t dataset_id;
    uint32_t shader_cid[TF_CID_LEN];
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    
    bool active;
} tf_renderer_t;

/* ===== Tripartite File System ===== */

typedef struct tripartite_fs {
    /* Core subsystems (available) */
    vfs_state_t *vfs;
    zxvfs_t *zxvfs;
    financial_fabric_t *financial;
    identity_fabric_t *identity;
    orbital_fabric_t *orbital;
    cw_wallet_t *crypto_wallet;
    mesh_net_t *mesh;
    
    /* File entries */
    tf_file_entry_t files[TF_MAX_FILES];
    uint32_t num_files;
    uint32_t next_file_id;
    
    /* Pairs */
    tf_pair_t pairs[TF_MAX_FILES];
    uint32_t num_pairs;
    
    /* Datasets */
    tf_dataset_t datasets[TF_MAX_DATASETS];
    uint32_t num_datasets;
    
    /* Containers */
    tf_container_t containers[TF_MAX_CONTAINERS];
    uint32_t num_containers;
    
    /* Renderers */
    tf_renderer_t renderers[TF_MAX_RENDERERS];
    uint32_t num_renderers;
    
    /* Holographic context */
    holo_ctx_t holo_ctx;
    
    /* Global statistics */
    struct {
        uint64_t total_files_created;
        uint64_t total_pairs_created;
        uint64_t total_datasets_created;
        uint64_t total_containers_created;
        uint64_t total_renderers_created;
        uint64_t total_interference_reconstructions;
        uint64_t total_file_accesses;
        uint64_t total_file_modifications;
        uint64_t total_replications;
        uint64_t total_mesh_shares;
        uint64_t total_economic_volume;
        uint64_t total_healings;
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
        bool auto_replication;
        uint8_t default_replication_factor;
        bool require_encryption;
        bool auto_mesh_sharing;
        bool auto_heal;
        surplus_real_t min_global_coverage;
    } config;
    
    bool initialized;
} tripartite_fs_t;

/* ===== API ===== */

/* Initialize the Tripartite File System */
void tf_init(tripartite_fs_t *fs,
             vfs_state_t *vfs,
             zxvfs_t *zxvfs,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             cw_wallet_t *crypto_wallet,
             mesh_net_t *mesh);

/* Register built-in tripartite operations */
void tf_register_builtins(tripartite_fs_t *fs);

/* ===== File Operations ===== */

int32_t tf_create_file(tripartite_fs_t *fs,
                       const char *name, tf_type_t type,
                       const uint8_t *payload, uint32_t payload_len,
                       uint32_t creator_identity_id);

tf_file_entry_t *tf_get_file(tripartite_fs_t *fs, uint32_t file_id);
tf_file_entry_t *tf_get_file_by_name(tripartite_fs_t *fs, const char *name);
tf_file_entry_t *tf_get_file_by_cid(tripartite_fs_t *fs, const uint8_t *cid);

/* Type detection from extension */
tf_type_t tf_type_from_extension(const char *filename);
tf_phase_t tf_phase_from_type(tf_type_t type);
const char *tf_type_name(tf_type_t type);
const char *tf_phase_name(tf_phase_t phase);
const char *tf_type_extension(tf_type_t type);

/* Pair management */
int32_t tf_create_pair(tripartite_fs_t *fs,
                       uint32_t positive_id, uint32_t negative_id,
                       uint32_t neutral_id);

int32_t tf_validate_pair(tripartite_fs_t *fs, uint32_t pair_id);

/* Interference reconstruction */
int32_t tf_reconstruct_interference(tripartite_fs_t *fs,
                                    uint32_t pair_id,
                                    uint8_t *output, uint32_t max_out,
                                    holo_blend_mode_t blend_mode);

/* Dataset management */
int32_t tf_create_dataset(tripartite_fs_t *fs,
                          const char *name,
                          const uint32_t *pair_ids, uint32_t num_pairs);

int32_t tf_add_pair_to_dataset(tripartite_fs_t *fs,
                               uint32_t dataset_id, uint32_t pair_id);

/* Container management */
int32_t tf_create_container(tripartite_fs_t *fs,
                            const char *name, const char *signature);

int32_t tf_container_add_file(tripartite_fs_t *fs,
                              uint32_t container_id, uint32_t file_id);

/* Renderer management */
int32_t tf_create_renderer(tripartite_fs_t *fs,
                           holo_render_mode_t render_mode,
                           holo_blend_mode_t blend_mode,
                           uint32_t width, uint32_t height,
                           uint32_t dataset_id);

int32_t tf_render(tripartite_fs_t *fs,
                  uint32_t renderer_id,
                  uint8_t *output, uint32_t max_out);

/* ===== Economic Operations ===== */

int32_t tf_price_file(tripartite_fs_t *fs,
                      uint32_t file_id,
                      uint64_t price, uint8_t pricing_form);

int32_t tf_purchase_access(tripartite_fs_t *fs,
                           uint32_t file_id, uint32_t buyer_identity_id);

/* ===== Mesh Net Sharing ===== */

int32_t tf_share_on_mesh(tripartite_fs_t *fs,
                         uint32_t file_id,
                         uint32_t mesh_network_id,
                         uint32_t trade_route_id);

/* ===== Crypto Wallet Integration ===== */

int32_t tf_wallet_sign_file(tripartite_fs_t *fs,
                            uint32_t file_id,
                            cw_key_id_t key_id);

int32_t tf_wallet_verify_file(tripartite_fs_t *fs,
                              uint32_t file_id);

/* ===== Crypto Bridge ===== */

int32_t tf_bridge_file(tripartite_fs_t *fs,
                       uint32_t file_id,
                       const char *target_chain,
                       const char *target_address);

/* ===== Governance ===== */

int32_t tf_enforce_policy(tripartite_fs_t *fs,
                          uint32_t file_id,
                          uint32_t policy_id);

/* ===== Self-Audit & Self-Heal ===== */

int32_t tf_self_audit_file(tripartite_fs_t *fs, uint32_t file_id);
int32_t tf_self_audit_system(tripartite_fs_t *fs);
int32_t tf_self_heal_file(tripartite_fs_t *fs, uint32_t file_id);

/* ===== Health & Attestation ===== */

int32_t tf_check_file_health(tripartite_fs_t *fs,
                             uint32_t file_id,
                             void *health_out);

int32_t tf_check_global_health(tripartite_fs_t *fs);

bool tf_global_safety_gate(tripartite_fs_t *fs);

lpres_state_t tf_attest(tripartite_fs_t *fs, uint32_t file_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void tf_update_coverage(tripartite_fs_t *fs);
bool tf_enforce_coverage(tripartite_fs_t *fs, surplus_real_t min_ratio);

/* Statistics */
void tf_get_stats(tripartite_fs_t *fs, void *stats_out);

/* Paraconsistent state */
lpres_state_t tf_get_file_attestation(tripartite_fs_t *fs, uint32_t file_id);
void tf_set_file_attestation(tripartite_fs_t *fs, uint32_t file_id, lpres_state_t state);

/* Utility */
const char *tf_lpres_state_name(lpres_state_t state);

#endif /* TRIPARTITE_FS_H */
