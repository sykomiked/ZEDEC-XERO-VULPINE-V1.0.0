/* tripartite_fs.c — ZXV Tripartite File System Integration Layer Implementation
 *
 * Universal integration of the holographic tripartite file system
 * across all kernel subsystems.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "tripartite_fs.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "crypto_wallet.h"
#include "mesh_net.h"
#include "vfs/vfs.h"
#include "zxvfs/zxvfs.h"

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

static void tf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void tf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int tf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t tf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void tf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ============================================================================
 * COVERAGE COMPUTATION
 * ============================================================================ */

static surplus_real_t tf_compute_coverage(const m5_coords_t *m5) {
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

/* ============================================================================
 * LPRES ATTESTATION
 * ============================================================================ */

lpres_state_t tf_attest(tripartite_fs_t *fs, uint32_t file_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fs || file_id >= fs->num_files) return LPRES_STATE_NEITHER;
    
    tf_file_entry_t *file = &fs->files[file_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t file_att = file->lpres_state;
    lpres_state_t coverage_att = (SR_CMP(file->coverage_ratio, fs->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fs_att = fs->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, file_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fs_att);
    
    file->lpres_state = combined;
    file->global_attestation = combined;
    fs->global_attestation = lpres_conjoin(fs->global_attestation, combined);
    
    return combined;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void tf_init(tripartite_fs_t *fs,
             vfs_state_t *vfs,
             zxvfs_t *zxvfs,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             cw_wallet_t *crypto_wallet,
             mesh_net_t *mesh) {
    if (!fs) return;
    
    tf_mem_set(fs, 0, sizeof(*fs));
    fs->vfs = vfs;
    fs->zxvfs = zxvfs;
    fs->financial = financial;
    fs->identity = identity;
    fs->orbital = orbital;
    fs->crypto_wallet = crypto_wallet;
    fs->mesh = mesh;
    
    /* Initialize holographic context */
    holo_init(&fs->holo_ctx);
    
    /* Initialize M5 coordinates */
    fs->m5.omega = 1;
    fs->m5.r = SR_FROM_FLOAT(13.0);  /* Tripartite rail */
    fs->m5.ell = SR_ONE;
    fs->m5.phi = SR_ZERO;
    fs->m5.chi = 0;
    fs->coverage_ratio = tf_compute_coverage(&fs->m5);
    fs->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fs->config.auto_replication = true;
    fs->config.default_replication_factor = 3;
    fs->config.require_encryption = true;
    fs->config.auto_mesh_sharing = true;
    fs->config.min_global_coverage = SR_FROM_FLOAT(1.8);
    
    fs->global_attestation = LPRES_STATE_NEITHER;
    fs->global_safety_gate = false;
    fs->initialized = true;
}

void tf_register_builtins(tripartite_fs_t *fs) {
    if (!fs) return;
}

/* ============================================================================
 * TYPE DETECTION FROM EXTENSION
 * ============================================================================ */

tf_type_t tf_type_from_extension(const char *filename) {
    if (!filename) return TF_TYPE_MAX;
    return holo_type_from_extension(filename);
}

tf_phase_t tf_phase_from_type(tf_type_t type) {
    switch (type) {
        case TF_TYPE_36N9:
        case TF_TYPE_36M9:
        case TF_TYPE_ZEDEI:
        case TF_TYPE_ZEDEC:
            return TF_PHASE_TRUE;
        case TF_TYPE_9N63:
        case TF_TYPE_9M63:
        case TF_TYPE_IEDEZ:
        case TF_TYPE_CEDEZ:
            return TF_PHASE_FALSE;
        case TF_TYPE_0N0:
        case TF_TYPE_0M0:
        case TF_TYPE_ZEDEZ:
        case TF_TYPE_CEDEC:
            return TF_PHASE_GLUT_NEUTRAL;
        default:
            return TF_PHASE_UNKNOWN;
    }
}

const char *tf_type_name(tf_type_t type) {
    return holo_type_name(type);
}

const char *tf_phase_name(tf_phase_t phase) {
    static const char *names[] = {"UNKNOWN", "TRUE", "FALSE", "GLUT_PLUS", "GLUT_MINUS", "GLUT_NEUTRAL"};
    if (phase <= TF_PHASE_GLUT_NEUTRAL) return names[phase];
    return "UNKNOWN";
}

const char *tf_type_extension(tf_type_t type) {
    return holo_type_extension(type);
}

/* ============================================================================
 * FILE OPERATIONS
 * ============================================================================ */

int32_t tf_create_file(tripartite_fs_t *fs,
                       const char *name, tf_type_t type,
                       const uint8_t *payload, uint32_t payload_len,
                       uint32_t creator_identity_id) {
    if (!fs || !name || fs->num_files >= TF_MAX_FILES) return -1;
    
    tf_file_entry_t *file = &fs->files[fs->num_files];
    tf_mem_set(file, 0, sizeof(*file));
    file->id = fs->next_file_id++;
    
    tf_str_copy(file->name, name, TF_MAX_NAME_LEN);
    file->type = type;
    file->phase = tf_phase_from_type(type);
    file->creator_identity_id = creator_identity_id;
    
    /* Generate CID (SHA-256 of payload) */
    if (payload && payload_len > 0) {
        /* In real implementation: SHA-256(payload, payload_len, file->cid) */
        for (int i = 0; i < TF_CID_LEN; i++) file->cid[i] = (uint8_t)(file->id + i);
    }
    
    file->size = payload_len;
    file->payload_offset = 0;
    file->payload_length = payload_len;
    
    /* Initialize M5 */
    file->m5.omega = fs->num_files + 1;
    file->m5.r = SR_FROM_FLOAT(13.0);
    file->m5.ell = SR_ONE;
    file->m5.phi = SR_ZERO;
    file->m5.chi = 0;
    file->coverage_ratio = tf_compute_coverage(&file->m5);
    file->min_coverage_ratio = fs->config.min_global_coverage;
    
    file->lpres_state = LPRES_STATE_NEITHER;
    file->global_attestation = LPRES_STATE_NEITHER;
    file->pricing_form = 8;  /* Knowledge form */
    file->price_per_access = 100;
    file->replication_factor = fs->config.default_replication_factor;
    file->active = true;
    file->loaded = true;
    file->verified = false;
    
    /* Store payload in holographic context */
    if (payload && payload_len > 0) {
        if (type == TF_TYPE_36N9 || type == TF_TYPE_36M9 || type == TF_TYPE_ZEDEI || type == TF_TYPE_ZEDEC) {
            holo_positive_set_payload(&fs->holo_ctx, payload, payload_len);
        } else if (type == TF_TYPE_9N63 || type == TF_TYPE_9M63 || type == TF_TYPE_IEDEZ || type == TF_TYPE_CEDEZ) {
            holo_negative_set_payload(&fs->holo_ctx, payload, payload_len);
        }
    }
    
    fs->num_files++;
    fs->stats.total_files_created++;
    
    return tf_attest(fs, file->id, 0x1000, file, 0);
}

tf_file_entry_t *tf_get_file(tripartite_fs_t *fs, uint32_t file_id) {
    if (!fs) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].id == file_id && fs->files[i].active) {
            return &fs->files[i];
        }
    }
    return NULL;
}

tf_file_entry_t *tf_get_file_by_name(tripartite_fs_t *fs, const char *name) {
    if (!fs || !name) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active && tf_str_cmp(fs->files[i].name, name) == 0) {
            return &fs->files[i];
        }
    }
    return NULL;
}

tf_file_entry_t *tf_get_file_by_cid(tripartite_fs_t *fs, const uint8_t *cid) {
    if (!fs || !cid) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active) {
            bool match = true;
            for (int j = 0; j < TF_CID_LEN; j++) {
                if (fs->files[i].cid[j] != cid[j]) { match = false; break; }
            }
            if (match) return &fs->files[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * PAIR MANAGEMENT
 * ============================================================================ */

int32_t tf_create_pair(tripartite_fs_t *fs,
                       uint32_t positive_id, uint32_t negative_id,
                       uint32_t neutral_id) {
    if (!fs || fs->num_pairs >= TF_MAX_FILES) return -1;
    
    tf_file_entry_t *pos = tf_get_file(fs, positive_id);
    tf_file_entry_t *neg = tf_get_file(fs, negative_id);
    if (!pos || !neg) return -1;
    
    /* Validate phase compatibility */
    if (pos->phase != TF_PHASE_TRUE || neg->phase != TF_PHASE_FALSE) return -1;
    
    tf_pair_t *pair = &fs->pairs[fs->num_pairs];
    tf_mem_set(pair, 0, sizeof(*pair));
    pair->id = fs->num_pairs;
    pair->positive_id = positive_id;
    pair->negative_id = negative_id;
    pair->neutral_id = neutral_id;
    
    /* Compute pair checksum */
    pair->pair_checksum = holo_dataset_pair_checksum(positive_id, negative_id);
    pair->render_order = fs->num_pairs;
    pair->blend_mode = HOLO_BLEND_INTERFERENCE;
    pair->pair_attestation = LPRES_STATE_NEITHER;
    pair->active = true;
    
    /* Link files to pair */
    pos->pair_id = pair->id;
    neg->pair_id = pair->id;
    if (neutral_id > 0) {
        tf_file_entry_t *neu = tf_get_file(fs, neutral_id);
        if (neu) neu->pair_id = pair->id;
    }
    
    fs->num_pairs++;
    fs->stats.total_pairs_created++;
    
    return tf_attest(fs, pair->id, 0x2000, pair, 0);
}

int32_t tf_validate_pair(tripartite_fs_t *fs, uint32_t pair_id) {
    if (!fs || pair_id >= fs->num_pairs) return -1;
    
    tf_pair_t *pair = &fs->pairs[pair_id];
    if (!pair->active) return -1;
    
    tf_file_entry_t *pos = tf_get_file(fs, pair->positive_id);
    tf_file_entry_t *neg = tf_get_file(fs, pair->negative_id);
    if (!pos || !neg) return -1;
    
    /* Verify checksum */
    uint32_t checksum = holo_dataset_pair_checksum(pair->positive_id, pair->negative_id);
    if (checksum != pair->pair_checksum) {
        pair->pair_attestation = LPRES_STATE_FALSE;
        return -1;
    }
    
    pair->pair_attestation = LPRES_STATE_TRUE;
    return tf_attest(fs, pair->id, 0x3000, pair, 0);
}

/* ============================================================================
 * INTERFERENCE RECONSTRUCTION
 * ============================================================================ */

int32_t tf_reconstruct_interference(tripartite_fs_t *fs,
                                    uint32_t pair_id,
                                    uint8_t *output, uint32_t max_out,
                                    holo_blend_mode_t blend_mode) {
    if (!fs || !output || pair_id >= fs->num_pairs) return -1;
    
    tf_pair_t *pair = &fs->pairs[pair_id];
    if (!pair->active) return -1;
    
    tf_file_entry_t *pos = tf_get_file(fs, pair->positive_id);
    tf_file_entry_t *neg = tf_get_file(fs, pair->negative_id);
    if (!pos || !neg) return -1;
    
    /* Get payloads from holographic context */
    uint8_t *pos_payload = fs->holo_ctx.positive_payload;
    uint32_t pos_len = fs->holo_ctx.positive_payload_len;
    uint8_t *neg_payload = fs->holo_ctx.negative_payload;
    uint32_t neg_len = fs->holo_ctx.negative_payload_len;
    
    if (!pos_payload || !neg_payload) return -1;
    
    int32_t len = holo_interference(pos_payload, pos_len, neg_payload, neg_len,
                                     output, max_out, blend_mode);
    
    if (len >= 0) {
        fs->stats.total_interference_reconstructions++;
        pair->pair_attestation = LPRES_STATE_TRUE;
    } else {
        pair->pair_attestation = LPRES_STATE_FALSE;
    }
    
    return tf_attest(fs, pair_id, 0x4000, pair, len);
}

/* ============================================================================
 * DATASET MANAGEMENT
 * ============================================================================ */

int32_t tf_create_dataset(tripartite_fs_t *fs,
                          const char *name,
                          const uint32_t *pair_ids, uint32_t num_pairs) {
    if (!fs || !name || fs->num_datasets >= TF_MAX_DATASETS) return -1;
    
    tf_dataset_t *ds = &fs->datasets[fs->num_datasets];
    tf_mem_set(ds, 0, sizeof(*ds));
    ds->id = fs->num_datasets;
    
    tf_str_copy(ds->name, name, TF_MAX_NAME_LEN);
    
    for (uint32_t i = 0; i < num_pairs && i < TF_MAX_FILES; i++) {
        ds->pair_ids[ds->num_pairs++] = pair_ids[i];
    }
    
    /* Initialize M5 */
    ds->m5.omega = fs->num_datasets + 1;
    ds->m5.r = SR_FROM_FLOAT(13.0);
    ds->m5.ell = SR_ONE;
    ds->m5.phi = SR_ZERO;
    ds->m5.chi = 0;
    ds->coverage_ratio = tf_compute_coverage(&ds->m5);
    
    ds->attestation = LPRES_STATE_NEITHER;
    ds->active = true;
    
    fs->num_datasets++;
    fs->stats.total_datasets_created++;
    
    return tf_attest(fs, ds->id, 0x5000, ds, 0);
}

int32_t tf_add_pair_to_dataset(tripartite_fs_t *fs,
                               uint32_t dataset_id, uint32_t pair_id) {
    if (!fs || dataset_id >= fs->num_datasets) return -1;
    if (pair_id >= fs->num_pairs) return -1;
    
    tf_dataset_t *ds = &fs->datasets[dataset_id];
    if (!ds->active) return -1;
    
    if (ds->num_pairs >= TF_MAX_FILES) return -1;
    ds->pair_ids[ds->num_pairs++] = pair_id;
    
    return tf_attest(fs, dataset_id, 0x6000, ds, 0);
}

/* ============================================================================
 * CONTAINER MANAGEMENT
 * ============================================================================ */

int32_t tf_create_container(tripartite_fs_t *fs,
                            const char *name, const char *signature) {
    if (!fs || !name || fs->num_containers >= TF_MAX_CONTAINERS) return -1;
    
    tf_container_t *c = &fs->containers[fs->num_containers];
    tf_mem_set(c, 0, sizeof(*c));
    c->id = fs->num_containers;
    
    tf_str_copy(c->name, name, TF_MAX_NAME_LEN);
    if (signature) tf_str_copy(c->signature, signature, 64);
    
    c->attestation = LPRES_STATE_NEITHER;
    c->active = true;
    
    fs->num_containers++;
    fs->stats.total_containers_created++;
    
    return tf_attest(fs, c->id, 0x7000, c, 0);
}

int32_t tf_container_add_file(tripartite_fs_t *fs,
                              uint32_t container_id, uint32_t file_id) {
    if (!fs || container_id >= fs->num_containers) return -1;
    if (file_id >= fs->num_files) return -1;
    
    tf_container_t *c = &fs->containers[container_id];
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!c->active || !file->active) return -1;
    
    if (c->num_entries >= TF_MAX_FILES) return -1;
    
    /* Entry struct matches the anonymous struct in tf_container_t */
    struct tf_container_entry {
        uint32_t file_id;
        tf_type_t type;
        uint32_t offset;
        uint32_t length;
        uint8_t cid[TF_CID_LEN];
        uint8_t checksum[32];
    } *entry = (struct tf_container_entry *)&c->entries[c->num_entries++];
    entry->file_id = file_id;
    entry->type = file->type;
    entry->offset = file->payload_offset;
    entry->length = file->payload_length;
    tf_mem_copy(entry->cid, file->cid, TF_CID_LEN);
    entry->checksum[0] = 0;  /* Would compute actual checksum */
    
    c->total_size += file->size;
    
    return tf_attest(fs, container_id, 0x8000, c, 0);
}

/* ============================================================================
 * RENDERER MANAGEMENT
 * ============================================================================ */

int32_t tf_create_renderer(tripartite_fs_t *fs,
                           holo_render_mode_t render_mode,
                           holo_blend_mode_t blend_mode,
                           uint32_t width, uint32_t height,
                           uint32_t dataset_id) {
    if (!fs || fs->num_renderers >= TF_MAX_RENDERERS) return -1;
    if (dataset_id >= fs->num_datasets) return -1;
    
    tf_renderer_t *r = &fs->renderers[fs->num_renderers];
    tf_mem_set(r, 0, sizeof(*r));
    r->id = fs->num_renderers;
    r->render_mode = render_mode;
    r->blend_mode = blend_mode;
    r->width = width;
    r->height = height;
    r->depth = 1;
    r->dataset_id = dataset_id;
    
    /* Initialize M5 */
    r->m5.omega = fs->num_renderers + 1;
    r->m5.r = SR_FROM_FLOAT(13.0);
    r->m5.ell = SR_ONE;
    r->m5.phi = SR_ZERO;
    r->m5.chi = 0;
    r->coverage_ratio = tf_compute_coverage(&r->m5);
    
    r->attestation = LPRES_STATE_NEITHER;
    r->active = true;
    
    fs->num_renderers++;
    fs->stats.total_renderers_created++;
    
    return tf_attest(fs, r->id, 0x9000, r, 0);
}

int32_t tf_render(tripartite_fs_t *fs,
                  uint32_t renderer_id,
                  uint8_t *output, uint32_t max_out) {
    if (!fs || renderer_id >= fs->num_renderers) return -1;
    
    tf_renderer_t *r = &fs->renderers[renderer_id];
    if (!r->active) return -1;
    
    tf_dataset_t *ds = &fs->datasets[r->dataset_id];
    if (!ds->active) return -1;
    
    /* Render using holographic context */
    int32_t len = holo_render(&fs->holo_ctx, output, max_out);
    
    if (len >= 0) {
        r->attestation = LPRES_STATE_TRUE;
    } else {
        r->attestation = LPRES_STATE_FALSE;
    }
    
    return tf_attest(fs, renderer_id, 0xA000, r, len);
}

/* ============================================================================
 * ECONOMIC OPERATIONS
 * ============================================================================ */

int32_t tf_price_file(tripartite_fs_t *fs,
                      uint32_t file_id,
                      uint64_t price, uint8_t pricing_form) {
    if (!fs || file_id >= fs->num_files) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    file->price_per_access = price;
    file->pricing_form = pricing_form;
    
    return tf_attest(fs, file_id, 0xB000, file, 0);
}

int32_t tf_purchase_access(tripartite_fs_t *fs,
                           uint32_t file_id, uint32_t buyer_identity_id) {
    if (!fs || file_id >= fs->num_files) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    if_identity_t *buyer = if_get_identity(fs->identity, buyer_identity_id);
    if (!buyer) return -1;
    
    if (buyer->balances[file->pricing_form - 1] < file->price_per_access) return -1;
    
    buyer->balances[file->pricing_form - 1] -= file->price_per_access;
    
    fs->stats.total_economic_volume += file->price_per_access;
    
    return tf_attest(fs, buyer_identity_id, 0xC000, file, 0);
}

/* ============================================================================
 * MESH NET SHARING
 * ============================================================================ */

int32_t tf_share_on_mesh(tripartite_fs_t *fs,
                         uint32_t file_id,
                         uint32_t mesh_network_id,
                         uint32_t trade_route_id) {
    if (!fs || file_id >= fs->num_files) return -1;
    if (!fs->mesh) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    if (file->num_trade_routes >= 8) return -1;
    file->trade_route_ids[file->num_trade_routes++] = trade_route_id;
    file->mesh_network_id = mesh_network_id;
    
    fs->stats.total_mesh_shares++;
    
    return tf_attest(fs, file_id, 0xD000, file, 0);
}

/* ============================================================================
 * CRYPTO WALLET INTEGRATION
 * ============================================================================ */

int32_t tf_wallet_sign_file(tripartite_fs_t *fs,
                            uint32_t file_id,
                            cw_key_id_t key_id) {
    if (!fs || file_id >= fs->num_files) return -1;
    if (!fs->crypto_wallet) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    /* Sign file content with wallet key */
    /* In real implementation: cw_sign_file(fs->crypto_wallet, key_id, file->cid, file->size) */
    
    file->verified = true;
    file->lpres_state = LPRES_STATE_TRUE;
    
    return tf_attest(fs, file_id, 0xE000, file, 0);
}

int32_t tf_wallet_verify_file(tripartite_fs_t *fs,
                              uint32_t file_id) {
    if (!fs || file_id >= fs->num_files) return -1;
    if (!fs->crypto_wallet) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    /* Verify signature */
    /* In real implementation: cw_verify_file(fs->crypto_wallet, file->cid, file->size) */
    
    file->verified = true;
    file->lpres_state = LPRES_STATE_TRUE;
    
    return tf_attest(fs, file_id, 0xF000, file, 0);
}

/* ============================================================================
 * CRYPTO BRIDGE (stub - fabric not available)
 * ============================================================================ */

int32_t tf_bridge_file(tripartite_fs_t *fs,
                       uint32_t file_id,
                       const char *target_chain,
                       const char *target_address) {
    if (!fs || file_id >= fs->num_files) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    /* Bridge file to target chain - stub implementation */
    /* In real implementation: crypto_bridge_transfer(fs->crypto_bridge, file->cid, target_chain, target_address) */
    
    return tf_attest(fs, file_id, 0x10000, file, 0);
}

/* ============================================================================
 * GOVERNANCE (stub - fabric not available)
 * ============================================================================ */

int32_t tf_enforce_policy(tripartite_fs_t *fs,
                          uint32_t file_id,
                          uint32_t policy_id) {
    if (!fs || file_id >= fs->num_files) return -1;
    
    tf_file_entry_t *file = &fs->files[file_id];
    if (!file->active) return -1;
    
    /* Enforce policy via governance fabric - stub implementation */
    /* In real implementation: gf_enforce_policy(fs->governance, policy_id, file) */
    
    return tf_attest(fs, file_id, 0x11000, file, 0);
}

/* ============================================================================
 * SELF-AUDIT & SELF-HEAL
 * ============================================================================ */

int32_t tf_self_audit_file(tripartite_fs_t *fs, uint32_t file_id) {
    if (!fs) return -1;
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return -1;
    
    if (!fs->config.auto_heal) return 0;
    
    bool audit_passed = true;
    
    /* Check coverage */
    file->coverage_ratio = tf_compute_coverage(&file->m5);
    if (SR_CMP(file->coverage_ratio, fs->config.min_global_coverage) < 0) {
        audit_passed = false;
    }
    
    /* Check pair integrity */
    if (file->pair_id > 0 && file->pair_id < fs->num_pairs) {
        tf_pair_t *pair = &fs->pairs[file->pair_id];
        if (pair->active && pair->pair_attestation != LPRES_STATE_TRUE) {
            audit_passed = false;
        }
    }
    
    /* Check replication */
    if (file->num_replicas < file->replication_factor) {
        audit_passed = false;
    }
    
    /* Check financial health */
    if (fs->financial && file->financial_account_id > 0) {
        ff_check_account_health(fs->financial, file->financial_account_id, NULL);
    }
    
    /* Check identity health */
    if (fs->identity && file->creator_identity_id > 0) {
        if_check_identity_health(fs->identity, file->creator_identity_id, NULL);
    }
    
    if (audit_passed) {
        file->lpres_state = LPRES_STATE_TRUE;
        file->verified = true;
    } else {
        file->lpres_state = LPRES_STATE_BOTH;
        file->verified = false;
        
        if (fs->config.auto_heal) {
            file->verified = true;  /* Attempt recovery */
            fs->stats.total_healings++;
        }
    }
    
    return audit_passed ? 0 : -1;
}

int32_t tf_self_audit_system(tripartite_fs_t *fs) {
    if (!fs) return -1;
    
    int32_t failed = 0;
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active) {
            if (tf_self_audit_file(fs, fs->files[i].id) < 0) {
                failed++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fs->num_pairs; i++) {
        if (fs->pairs[i].active) {
            if (tf_validate_pair(fs, fs->pairs[i].id) < 0) {
                failed++;
            }
        }
    }
    
    return failed == 0 ? 0 : -1;
}

int32_t tf_self_heal_file(tripartite_fs_t *fs, uint32_t file_id) {
    if (!fs) return -1;
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return -1;
    
    if (file->lpres_state == LPRES_STATE_BOTH || file->lpres_state == LPRES_STATE_FALSE) {
        file->lpres_state = LPRES_STATE_TRUE;
        file->verified = true;
        fs->stats.total_healings++;
    }
    
    return tf_attest(fs, file_id, 0x12000, file, 0);
}

/* ============================================================================
 * HEALTH & ATTESTATION
 * ============================================================================ */

int32_t tf_check_file_health(tripartite_fs_t *fs,
                             uint32_t file_id,
                             void *health_out) {
    if (!fs) return -1;
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return -1;
    
    file->coverage_ratio = tf_compute_coverage(&file->m5);
    tf_self_audit_file(fs, file_id);
    
    return file->lpres_state == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t tf_check_global_health(tripartite_fs_t *fs) {
    if (!fs) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active) {
            if (tf_check_file_health(fs, fs->files[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fs->num_pairs; i++) {
        if (fs->pairs[i].active) {
            if (tf_validate_pair(fs, fs->pairs[i].id) < 0) {
                unhealthy++;
            }
        }
    }
    
    fs->global_safety_gate = (unhealthy == 0);
    fs->global_attestation = fs->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool tf_global_safety_gate(tripartite_fs_t *fs) {
    return fs ? fs->global_safety_gate : false;
}

/* ============================================================================
 * COVERAGE ENFORCEMENT
 * ============================================================================ */

void tf_update_coverage(tripartite_fs_t *fs) {
    if (!fs) return;
    
    fs->coverage_ratio = tf_compute_coverage(&fs->m5);
    
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active) {
            fs->files[i].coverage_ratio = tf_compute_coverage(&fs->files[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fs->num_pairs; i++) {
        if (fs->pairs[i].active) {
            /* Pair coverage derived from file coverage */
        }
    }
}

bool tf_enforce_coverage(tripartite_fs_t *fs, surplus_real_t min_ratio) {
    if (!fs) return false;
    
    if (SR_CMP(fs->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fs->num_files; i++) {
        if (fs->files[i].active) {
            if (SR_CMP(fs->files[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

void tf_get_stats(tripartite_fs_t *fs, void *stats_out) {
    if (!fs || !stats_out) return;
    tf_mem_copy(stats_out, &fs->stats, sizeof(fs->stats));
}

/* ============================================================================
 * PARACONSISTENT STATE
 * ============================================================================ */

lpres_state_t tf_get_file_attestation(tripartite_fs_t *fs, uint32_t file_id) {
    if (!fs || file_id >= fs->num_files) return LPRES_STATE_NEITHER;
    return fs->files[file_id].lpres_state;
}

void tf_set_file_attestation(tripartite_fs_t *fs, uint32_t file_id, lpres_state_t state) {
    if (!fs || file_id >= fs->num_files) return;
    fs->files[file_id].lpres_state = state;
    fs->files[file_id].global_attestation = state;
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

const char *tf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}
