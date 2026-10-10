/* tripartite_fs.h — ZXV Tripartite File System index
 *
 * An index of tripartite files (positive / negative / neutral context files
 * with the custom extensions below), their pairs, datasets, containers and
 * renderers, addressed by real content identifiers.
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
 * WHAT IS REAL
 *   - Content IDs are binary CIDv1: 0x01 (version) 0x55 (raw codec)
 *     0x12 0x20 (sha2-256 multihash, 32 bytes) || SHA-256(payload), 36 bytes,
 *     the same encoding as kernel/src/ipfs_node raw leaves. tf_cid_to_string()
 *     gives the usual base32 "b..." text form.
 *   - A file is `verified` only after tf_verify_file_content() has hashed the
 *     bytes it was given and matched them against the stored CID. Nothing
 *     else sets it.
 *   - Pairs carry a SHA-256 digest over their members' CIDs; tf_validate_pair()
 *     recomputes it. Interference reconstruction runs only on a pair loaded
 *     with tf_load_pair(), which hashes both payloads first.
 *
 * WHAT IS NOT HERE (each fails closed with TF_ENOTSUP)
 *   - No payload storage: the index keeps CIDs and sizes, the caller keeps
 *     the bytes (tf_load_pair keeps pointers until the next load).
 *   - No signing: tf_wallet_sign_file / tf_wallet_verify_file.
 *   - No settlement: tf_purchase_access (the old code edited identity
 *     balances directly, outside the financial fabric).
 *   - No chain bridge (tf_bridge_file), no policy engine (tf_enforce_policy).
 *   - No replication, no encryption, no healing: tf_self_heal_file cannot
 *     restore content without a replica, so it refuses.
 *   - tf_share_on_mesh records a route ID in the index; nothing is sent.
 *
 * The subsystem pointers passed to tf_init are stored opaquely and are not
 * dereferenced by this module.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef TRIPARTITE_FS_H
#define TRIPARTITE_FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "holographic/holo.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define TF_MAX_FILES             4096
#define TF_MAX_PAIRS             1024
#define TF_MAX_CONTAINERS        64
#define TF_CONTAINER_MAX_ENTRIES 256
#define TF_MAX_DATASETS          64
#define TF_DATASET_MAX_PAIRS     256
#define TF_MAX_RENDERERS         64
#define TF_MAX_NAME_LEN          128
#define TF_DIGEST_LEN            32
#define TF_CID_PREFIX_LEN        4
#define TF_CID_LEN               (TF_CID_PREFIX_LEN + TF_DIGEST_LEN) /* 36: CIDv1 raw sha2-256 */
#define TF_CID_STR_LEN           60 /* 'b' + 58 base32 chars + NUL */

/* Error codes (0 or a non-negative id is success) */
#define TF_EINVAL    (-1) /* bad argument or unknown id */
#define TF_EFULL     (-2) /* table full */
#define TF_EMISMATCH (-3) /* content does not match its CID / digest */
#define TF_ENOTSUP   (-4) /* capability not implemented: nothing happened */

/* ===== Tripartite File Types (mirrors holographic) ===== */

typedef enum {
    TF_TYPE_36N9 = 1,   /* .36n9 — Positive Space (TRUE) */
    TF_TYPE_9N63 = 2,   /* .9n63 — Negative Space (FALSE) */
    TF_TYPE_36M9 = 3,   /* .36m9 — Positive Manifest (GLUT+) */
    TF_TYPE_ZEDEI = 4,  /* .zedei — Positive Transform (GLUT+) */
    TF_TYPE_ZEDEC = 5,  /* .zedec — Positive Container (GLUT+) */
    TF_TYPE_9M63 = 6,   /* .9m63 — Negative Manifest (GLUT-) */
    TF_TYPE_IEDEZ = 7,  /* .iedez — Negative Transform (GLUT-) */
    TF_TYPE_CEDEZ = 8,  /* .cedez — Negative Container (GLUT-) */
    TF_TYPE_0N0 = 9,    /* .0n0 — Neutral Source (NEITHER) */
    TF_TYPE_0M0 = 10,   /* .0m0 — Neutral Manifest (NEITHER) */
    TF_TYPE_ZEDEZ = 11, /* .zedez — Neutral Transform (GLUT0) */
    TF_TYPE_CEDEC = 12, /* .cedec — Neutral Container (GLUT0) */
    TF_TYPE_MAX
} tf_type_t;

/* ===== LPRES Logic Phase Mapping ===== */

typedef enum {
    TF_PHASE_TRUE = 1,         /* .36n9, .36m9, .zedei, .zedec */
    TF_PHASE_FALSE = 2,        /* .9n63, .9m63, .iedez, .cedez */
    TF_PHASE_GLUT_PLUS = 3,    /* .zedec, .zedei, .36m9 */
    TF_PHASE_GLUT_MINUS = 4,   /* .vino, .cedez, .iedez, .9m63 */
    TF_PHASE_GLUT_NEUTRAL = 5, /* .0n0, .0m0, .zedez, .cedec */
    TF_PHASE_UNKNOWN = 0
} tf_phase_t;

/* ===== Tripartite File Entry ===== */

typedef struct tf_file_entry {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    tf_type_t type;
    tf_phase_t phase;

    /* Content addressing */
    uint8_t cid[TF_CID_LEN]; /* CIDv1 raw sha2-256 of the payload */
    uint64_t size;
    uint32_t payload_offset;
    uint32_t payload_length;

    /* Tripartite pairing (pair_id is pair index + 1; 0 = unpaired) */
    uint32_t pair_id;
    uint32_t dataset_id;
    uint32_t container_id;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;

    /* Paraconsistent state */
    lpres_state_t lpres_state;
    lpres_state_t global_attestation;

    /* Economic: a price label only; tf_purchase_access settles nothing */
    uint8_t pricing_form; /* 1-based capital form, 5..9 (alienable) */
    uint64_t price_per_access;

    /* Identity */
    uint32_t creator_identity_id;

    /* Mesh Net: recorded route IDs only */
    uint32_t mesh_network_id;
    uint32_t trade_route_ids[8];
    uint32_t num_trade_routes;

    /* Statistics */
    uint64_t accesses;
    uint64_t modifications;
    uint64_t last_access_tick;

    /* Lifecycle */
    bool active;
    bool verified; /* set only by tf_verify_file_content() */
} tf_file_entry_t;

/* ===== Tripartite Pair ===== */

typedef struct tf_pair {
    uint32_t id;
    uint32_t positive_id; /* .36n9, .36m9, .zedei, .zedec */
    uint32_t negative_id; /* .9n63, .9m63, .iedez, .cedez */
    uint32_t neutral_id;  /* .0n0, .0m0, .zedez, .cedec (TF_NO_FILE if none) */

    /* SHA-256(pos CID || neg CID [|| neutral CID]) */
    uint8_t pair_digest[TF_DIGEST_LEN];
    uint32_t render_order;

    holo_blend_mode_t blend_mode;
    lpres_state_t pair_attestation;

    bool active;
} tf_pair_t;

#define TF_NO_FILE 0xFFFFFFFFu

/* ===== Dataset ===== */

typedef struct tf_dataset {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    uint32_t pair_ids[TF_DATASET_MAX_PAIRS];
    uint32_t num_pairs;
    uint32_t renderer_id;
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    lpres_state_t attestation;
    bool active;
} tf_dataset_t;

/* ===== Container ===== */

typedef struct tf_container_entry {
    uint32_t file_id;
    tf_type_t type;
    uint32_t offset;
    uint32_t length;
    uint8_t cid[TF_CID_LEN];
} tf_container_entry_t;

typedef struct tf_container {
    uint32_t id;
    char name[TF_MAX_NAME_LEN];
    char signature[64]; /* a label: nothing signs or checks it */
    tf_container_entry_t entries[TF_CONTAINER_MAX_ENTRIES];
    uint32_t num_entries;
    uint64_t total_size;
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
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
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    lpres_state_t attestation;
    bool active;
} tf_renderer_t;

/* ===== Statistics ===== */

typedef struct tf_stats {
    uint64_t total_files_created;
    uint64_t total_pairs_created;
    uint64_t total_datasets_created;
    uint64_t total_containers_created;
    uint64_t total_renderers_created;
    uint64_t total_interference_reconstructions;
    uint64_t total_file_accesses;
    uint64_t total_file_modifications;
    uint64_t total_verifications;
    uint64_t total_verification_failures;
    uint64_t total_mesh_shares;
} tf_stats_t;

/* ===== Tripartite File System ===== */

typedef struct tripartite_fs {
    /* Opaque subsystem handles (stored, never dereferenced here) */
    void *vfs;
    void *zxvfs;
    void *financial;
    void *identity;
    void *orbital;
    void *crypto_wallet;
    void *mesh;

    tf_file_entry_t files[TF_MAX_FILES];
    uint32_t num_files;
    uint32_t next_file_id;

    tf_pair_t pairs[TF_MAX_PAIRS];
    uint32_t num_pairs;

    tf_dataset_t datasets[TF_MAX_DATASETS];
    uint32_t num_datasets;

    tf_container_t containers[TF_MAX_CONTAINERS];
    uint32_t num_containers;

    tf_renderer_t renderers[TF_MAX_RENDERERS];
    uint32_t num_renderers;

    /* Holographic context; payloads are the caller's (see tf_load_pair) */
    holo_ctx_t holo_ctx;
    uint32_t loaded_pair; /* pair index + 1 of the verified loaded pair, 0 = none */

    tf_stats_t stats;

    lpres_state_t global_attestation;
    bool global_safety_gate;

    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;

    struct {
        surplus_real_t min_global_coverage;
    } config;

    bool initialized;
} tripartite_fs_t;

/* ===== API ===== */

void tf_init(tripartite_fs_t *fs, void *vfs, void *zxvfs, void *financial, void *identity,
             void *orbital, void *crypto_wallet, void *mesh);

/* No built-in operations exist; kept for callers. */
void tf_register_builtins(tripartite_fs_t *fs);

/* ===== Content IDs ===== */

/* CIDv1 (raw, sha2-256) of data. */
void tf_compute_cid(const uint8_t *data, uint32_t len, uint8_t out[TF_CID_LEN]);
/* Multibase base32 text form ("b..."); returns the length or TF_EINVAL. */
int32_t tf_cid_to_string(const uint8_t cid[TF_CID_LEN], char *out, uint32_t cap);

/* ===== File Operations ===== */

/* Returns the new file id (>= 0) or a TF_E* code. The payload is hashed into
 * the CID and not retained. */
int32_t tf_create_file(tripartite_fs_t *fs, const char *name, tf_type_t type,
                       const uint8_t *payload, uint32_t payload_len, uint32_t creator_identity_id);

/* Hash payload and compare with the file's CID (and size). On a match the
 * file becomes verified (lpres TRUE) and 0 is returned; otherwise it becomes
 * unverified (lpres FALSE) and TF_EMISMATCH is returned. */
int32_t tf_verify_file_content(tripartite_fs_t *fs, uint32_t file_id, const uint8_t *payload,
                               uint32_t payload_len);

tf_file_entry_t *tf_get_file(tripartite_fs_t *fs, uint32_t file_id);
tf_file_entry_t *tf_get_file_by_name(tripartite_fs_t *fs, const char *name);
tf_file_entry_t *tf_get_file_by_cid(tripartite_fs_t *fs, const uint8_t *cid);

/* Type detection from extension */
tf_type_t tf_type_from_extension(const char *filename);
tf_phase_t tf_phase_from_type(tf_type_t type);
const char *tf_type_name(tf_type_t type);
const char *tf_phase_name(tf_phase_t phase);
const char *tf_type_extension(tf_type_t type);

/* Pair management: returns the pair index or a TF_E* code. neutral_id may be
 * TF_NO_FILE. */
int32_t tf_create_pair(tripartite_fs_t *fs, uint32_t positive_id, uint32_t negative_id,
                       uint32_t neutral_id);

/* Recompute the pair digest from the members' current CIDs: 0 or TF_EMISMATCH. */
int32_t tf_validate_pair(tripartite_fs_t *fs, uint32_t pair_id);

/* Verify both payloads against the pair's CIDs and load them for
 * reconstruction / rendering. The pointers must stay valid until the next
 * load. Returns 0 or a TF_E* code (nothing is loaded on failure). */
int32_t tf_load_pair(tripartite_fs_t *fs, uint32_t pair_id, const uint8_t *pos, uint32_t pos_len,
                     const uint8_t *neg, uint32_t neg_len);

/* Interference reconstruction of the loaded pair; returns output length or
 * a TF_E* code (TF_EINVAL if pair_id is not the verified loaded pair). */
int32_t tf_reconstruct_interference(tripartite_fs_t *fs, uint32_t pair_id, uint8_t *output,
                                    uint32_t max_out, holo_blend_mode_t blend_mode);

/* Dataset management (ids returned) */
int32_t tf_create_dataset(tripartite_fs_t *fs, const char *name, const uint32_t *pair_ids,
                          uint32_t num_pairs);
int32_t tf_add_pair_to_dataset(tripartite_fs_t *fs, uint32_t dataset_id, uint32_t pair_id);

/* Container management (ids returned) */
int32_t tf_create_container(tripartite_fs_t *fs, const char *name, const char *signature);
int32_t tf_container_add_file(tripartite_fs_t *fs, uint32_t container_id, uint32_t file_id);

/* Renderer management */
int32_t tf_create_renderer(tripartite_fs_t *fs, holo_render_mode_t render_mode,
                           holo_blend_mode_t blend_mode, uint32_t width, uint32_t height,
                           uint32_t dataset_id);
/* Renders the verified loaded pair; TF_EINVAL if none is loaded. */
int32_t tf_render(tripartite_fs_t *fs, uint32_t renderer_id, uint8_t *output, uint32_t max_out);

/* ===== Economic Operations ===== */

/* Set a price label. pricing_form is the 1-based capital form; forms 1..4
 * (Social, Cultural, Spiritual, Governance) are state-reserved and refused. */
int32_t tf_price_file(tripartite_fs_t *fs, uint32_t file_id, uint64_t price, uint8_t pricing_form);
/* TF_ENOTSUP: no settlement path exists. */
int32_t tf_purchase_access(tripartite_fs_t *fs, uint32_t file_id, uint32_t buyer_identity_id);

/* ===== Mesh Net Sharing (records the route ID only) ===== */

int32_t tf_share_on_mesh(tripartite_fs_t *fs, uint32_t file_id, uint32_t mesh_network_id,
                         uint32_t trade_route_id);

/* ===== Not implemented: each returns TF_ENOTSUP and changes nothing ===== */

int32_t tf_wallet_sign_file(tripartite_fs_t *fs, uint32_t file_id, uint32_t key_id);
int32_t tf_wallet_verify_file(tripartite_fs_t *fs, uint32_t file_id);
int32_t tf_bridge_file(tripartite_fs_t *fs, uint32_t file_id, const char *target_chain,
                       const char *target_address);
int32_t tf_enforce_policy(tripartite_fs_t *fs, uint32_t file_id, uint32_t policy_id);

/* ===== Self-Audit ===== */

/* Audit = content verified, coverage at the minimum, pair digest valid.
 * Returns 0 if all hold, TF_EMISMATCH otherwise. Never sets `verified`. */
int32_t tf_self_audit_file(tripartite_fs_t *fs, uint32_t file_id);
int32_t tf_self_audit_system(tripartite_fs_t *fs);
/* No replica exists to restore from: 0 if the file already audits clean,
 * TF_ENOTSUP otherwise. */
int32_t tf_self_heal_file(tripartite_fs_t *fs, uint32_t file_id);

/* ===== Health & Attestation ===== */

int32_t tf_check_file_health(tripartite_fs_t *fs, uint32_t file_id, void *health_out);
int32_t tf_check_global_health(tripartite_fs_t *fs);
bool tf_global_safety_gate(tripartite_fs_t *fs);

/* Fold an operation result into a file's attestation. */
lpres_state_t tf_attest(tripartite_fs_t *fs, uint32_t file_id, uint32_t op_id, void *args,
                        int32_t result);

/* Coverage enforcement */
void tf_update_coverage(tripartite_fs_t *fs);
bool tf_enforce_coverage(tripartite_fs_t *fs, surplus_real_t min_ratio);

/* Statistics (stats_out is a tf_stats_t) */
void tf_get_stats(tripartite_fs_t *fs, void *stats_out);

/* Paraconsistent state */
lpres_state_t tf_get_file_attestation(tripartite_fs_t *fs, uint32_t file_id);
void tf_set_file_attestation(tripartite_fs_t *fs, uint32_t file_id, lpres_state_t state);

const char *tf_lpres_state_name(lpres_state_t state);

#endif /* TRIPARTITE_FS_H */
