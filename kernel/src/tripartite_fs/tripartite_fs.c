/* tripartite_fs.c — ZXV Tripartite File System index (see tripartite_fs.h)
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#define M5_TYPES_INTEGER_ONLY /* no FPU types needed here */
#include "tripartite_fs.h"
#include "../robin_debanks/sha256.h"

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static void tf_mem_set(void *dst, int val, uint32_t len)
{
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t) val;
}

static void tf_mem_copy(void *dst, const void *src, uint32_t len)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static bool tf_mem_eq(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < len; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static int tf_str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

static void tf_str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* ============================================================================
 * COVERAGE
 * ============================================================================ */

static surplus_real_t tf_compute_coverage(const m5_coords_t *m5)
{
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, m5->r), m5->ell);
    surplus_real_t denominator = SR_MUL(m5->phi, SR_FROM_INT(m5->chi));
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_INT(100);
    return SR_DIV(numerator, denominator);
}

static void tf_m5_default(m5_coords_t *m5, uint64_t omega)
{
    m5->omega = omega;
    m5->r = SR_FROM_INT(13); /* Tripartite rail */
    m5->ell = SR_ONE;
    m5->phi = SR_ZERO;
    m5->chi = 0;
}

/* ============================================================================
 * CONTENT IDS
 * ============================================================================ */

void tf_compute_cid(const uint8_t *data, uint32_t len, uint8_t out[TF_CID_LEN])
{
    out[0] = 0x01; /* CIDv1 */
    out[1] = 0x55; /* raw */
    out[2] = 0x12; /* sha2-256 */
    out[3] = 0x20; /* 32-byte digest */
    sha256(data, len, out + TF_CID_PREFIX_LEN);
}

int32_t tf_cid_to_string(const uint8_t cid[TF_CID_LEN], char *out, uint32_t cap)
{
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyz234567";
    if (!cid || !out || cap < TF_CID_STR_LEN) return TF_EINVAL;
    uint32_t n = 0, acc = 0, bits = 0;
    out[n++] = 'b';
    for (uint32_t i = 0; i < TF_CID_LEN; i++) {
        acc = (acc << 8) | cid[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[n++] = alpha[(acc >> bits) & 31u];
        }
    }
    if (bits) out[n++] = alpha[(acc << (5 - bits)) & 31u];
    out[n] = 0;
    return (int32_t) n;
}

/* ============================================================================
 * LPRES ATTESTATION
 * ============================================================================ */

lpres_state_t tf_attest(tripartite_fs_t *fs, uint32_t file_id, uint32_t op_id, void *args,
                        int32_t result)
{
    (void) op_id;
    (void) args;
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return LPRES_STATE_NEITHER;
    lpres_state_t r = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    file->lpres_state = lpres_conjoin(file->lpres_state, r);
    file->global_attestation = file->lpres_state;
    return file->lpres_state;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void tf_init(tripartite_fs_t *fs, void *vfs, void *zxvfs, void *financial, void *identity,
             void *orbital, void *crypto_wallet, void *mesh)
{
    if (!fs) return;
    tf_mem_set(fs, 0, sizeof(*fs));
    fs->vfs = vfs;
    fs->zxvfs = zxvfs;
    fs->financial = financial;
    fs->identity = identity;
    fs->orbital = orbital;
    fs->crypto_wallet = crypto_wallet;
    fs->mesh = mesh;

    holo_init(&fs->holo_ctx);

    tf_m5_default(&fs->m5, 1);
    fs->coverage_ratio = tf_compute_coverage(&fs->m5);
    fs->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    fs->config.min_global_coverage = SR_FROM_FLOAT(1.8);

    fs->global_attestation = LPRES_STATE_NEITHER;
    fs->global_safety_gate = false;
    fs->initialized = true;
}

void tf_register_builtins(tripartite_fs_t *fs)
{
    (void) fs;
}

/* ============================================================================
 * TYPE DETECTION
 * ============================================================================ */

tf_type_t tf_type_from_extension(const char *filename)
{
    if (!filename) return TF_TYPE_MAX;
    holo_type_t t = holo_type_from_extension(filename);
    if (t == HOLO_TYPE_NONE || (int) t >= (int) TF_TYPE_MAX) return TF_TYPE_MAX;
    return (tf_type_t) t;
}

tf_phase_t tf_phase_from_type(tf_type_t type)
{
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

const char *tf_type_name(tf_type_t type)
{
    return holo_type_name((holo_type_t) type);
}

const char *tf_phase_name(tf_phase_t phase)
{
    static const char *names[] = {"UNKNOWN",   "TRUE",       "FALSE",
                                  "GLUT_PLUS", "GLUT_MINUS", "GLUT_NEUTRAL"};
    if ((uint32_t) phase <= (uint32_t) TF_PHASE_GLUT_NEUTRAL) return names[phase];
    return "UNKNOWN";
}

const char *tf_type_extension(tf_type_t type)
{
    return holo_type_extension((holo_type_t) type);
}

/* ============================================================================
 * FILE OPERATIONS
 * ============================================================================ */

int32_t tf_create_file(tripartite_fs_t *fs, const char *name, tf_type_t type,
                       const uint8_t *payload, uint32_t payload_len, uint32_t creator_identity_id)
{
    if (!fs || !name || (!payload && payload_len > 0)) return TF_EINVAL;
    if (tf_phase_from_type(type) == TF_PHASE_UNKNOWN) return TF_EINVAL;
    if (fs->num_files >= TF_MAX_FILES) return TF_EFULL;

    tf_file_entry_t *file = &fs->files[fs->num_files];
    tf_mem_set(file, 0, sizeof(*file));
    file->id = fs->next_file_id++;
    tf_str_copy(file->name, name, TF_MAX_NAME_LEN);
    file->type = type;
    file->phase = tf_phase_from_type(type);
    file->creator_identity_id = creator_identity_id;

    tf_compute_cid(payload, payload_len, file->cid);
    file->size = payload_len;
    file->payload_length = payload_len;

    tf_m5_default(&file->m5, (uint64_t) fs->num_files + 1);
    file->coverage_ratio = tf_compute_coverage(&file->m5);
    file->min_coverage_ratio = fs->config.min_global_coverage;

    /* The creator handed us the bytes, but the index does not keep them:
     * a later reader must present them again to verify. */
    file->lpres_state = LPRES_STATE_NEITHER;
    file->global_attestation = LPRES_STATE_NEITHER;
    file->pricing_form = 8; /* Knowledge */
    file->price_per_access = 0;
    file->active = true;
    file->verified = false;

    fs->num_files++;
    fs->stats.total_files_created++;
    return (int32_t) file->id;
}

int32_t tf_verify_file_content(tripartite_fs_t *fs, uint32_t file_id, const uint8_t *payload,
                               uint32_t payload_len)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file || (!payload && payload_len > 0)) return TF_EINVAL;
    uint8_t cid[TF_CID_LEN];
    tf_compute_cid(payload, payload_len, cid);
    bool ok = file->size == payload_len && tf_mem_eq(cid, file->cid, TF_CID_LEN);
    file->verified = ok;
    file->lpres_state = ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    file->global_attestation = file->lpres_state;
    if (ok) {
        fs->stats.total_verifications++;
        file->accesses++;
        fs->stats.total_file_accesses++;
    } else {
        fs->stats.total_verification_failures++;
    }
    return ok ? 0 : TF_EMISMATCH;
}

tf_file_entry_t *tf_get_file(tripartite_fs_t *fs, uint32_t file_id)
{
    if (!fs) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].id == file_id && fs->files[i].active) return &fs->files[i];
    return NULL;
}

tf_file_entry_t *tf_get_file_by_name(tripartite_fs_t *fs, const char *name)
{
    if (!fs || !name) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].active && tf_str_cmp(fs->files[i].name, name) == 0) return &fs->files[i];
    return NULL;
}

tf_file_entry_t *tf_get_file_by_cid(tripartite_fs_t *fs, const uint8_t *cid)
{
    if (!fs || !cid) return NULL;
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].active && tf_mem_eq(fs->files[i].cid, cid, TF_CID_LEN))
            return &fs->files[i];
    return NULL;
}

/* ============================================================================
 * PAIRS
 * ============================================================================ */

/* SHA-256(pos CID || neg CID [|| neutral CID]); false if a member is gone. */
static bool tf_pair_digest(tripartite_fs_t *fs, const tf_pair_t *pair, uint8_t out[32])
{
    tf_file_entry_t *pos = tf_get_file(fs, pair->positive_id);
    tf_file_entry_t *neg = tf_get_file(fs, pair->negative_id);
    if (!pos || !neg) return false;
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pos->cid, TF_CID_LEN);
    sha256_update(&ctx, neg->cid, TF_CID_LEN);
    if (pair->neutral_id != TF_NO_FILE) {
        tf_file_entry_t *neu = tf_get_file(fs, pair->neutral_id);
        if (!neu) return false;
        sha256_update(&ctx, neu->cid, TF_CID_LEN);
    }
    sha256_final(&ctx, out);
    return true;
}

int32_t tf_create_pair(tripartite_fs_t *fs, uint32_t positive_id, uint32_t negative_id,
                       uint32_t neutral_id)
{
    if (!fs) return TF_EINVAL;
    if (fs->num_pairs >= TF_MAX_PAIRS) return TF_EFULL;
    tf_file_entry_t *pos = tf_get_file(fs, positive_id);
    tf_file_entry_t *neg = tf_get_file(fs, negative_id);
    if (!pos || !neg || pos->phase != TF_PHASE_TRUE || neg->phase != TF_PHASE_FALSE)
        return TF_EINVAL;
    tf_file_entry_t *neu = NULL;
    if (neutral_id != TF_NO_FILE) {
        neu = tf_get_file(fs, neutral_id);
        if (!neu || neu->phase != TF_PHASE_GLUT_NEUTRAL) return TF_EINVAL;
    }

    tf_pair_t *pair = &fs->pairs[fs->num_pairs];
    tf_mem_set(pair, 0, sizeof(*pair));
    pair->id = fs->num_pairs;
    pair->positive_id = positive_id;
    pair->negative_id = negative_id;
    pair->neutral_id = neutral_id;
    (void) tf_pair_digest(fs, pair, pair->pair_digest);
    pair->render_order = fs->num_pairs;
    pair->blend_mode = HOLO_BLEND_INTERFERENCE;
    pair->pair_attestation = LPRES_STATE_TRUE;
    pair->active = true;

    pos->pair_id = pair->id + 1;
    neg->pair_id = pair->id + 1;
    if (neu) neu->pair_id = pair->id + 1;

    fs->num_pairs++;
    fs->stats.total_pairs_created++;
    return (int32_t) pair->id;
}

int32_t tf_validate_pair(tripartite_fs_t *fs, uint32_t pair_id)
{
    if (!fs || pair_id >= fs->num_pairs || !fs->pairs[pair_id].active) return TF_EINVAL;
    tf_pair_t *pair = &fs->pairs[pair_id];
    uint8_t d[32];
    bool ok = tf_pair_digest(fs, pair, d) && tf_mem_eq(d, pair->pair_digest, 32);
    pair->pair_attestation = ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    return ok ? 0 : TF_EMISMATCH;
}

int32_t tf_load_pair(tripartite_fs_t *fs, uint32_t pair_id, const uint8_t *pos, uint32_t pos_len,
                     const uint8_t *neg, uint32_t neg_len)
{
    if (!fs || !pos || !neg) return TF_EINVAL;
    int32_t r = tf_validate_pair(fs, pair_id);
    if (r < 0) return r;
    tf_pair_t *pair = &fs->pairs[pair_id];
    fs->loaded_pair = 0;
    if (tf_verify_file_content(fs, pair->positive_id, pos, pos_len) < 0 ||
        tf_verify_file_content(fs, pair->negative_id, neg, neg_len) < 0) {
        pair->pair_attestation = LPRES_STATE_FALSE;
        return TF_EMISMATCH;
    }
    if (holo_positive_set_payload(&fs->holo_ctx, pos, pos_len) < 0 ||
        holo_negative_set_payload(&fs->holo_ctx, neg, neg_len) < 0)
        return TF_EINVAL;
    fs->loaded_pair = pair_id + 1;
    return 0;
}

int32_t tf_reconstruct_interference(tripartite_fs_t *fs, uint32_t pair_id, uint8_t *output,
                                    uint32_t max_out, holo_blend_mode_t blend_mode)
{
    if (!fs || !output || fs->loaded_pair != pair_id + 1) return TF_EINVAL;
    tf_pair_t *pair = &fs->pairs[pair_id];
    int32_t len =
        holo_interference(fs->holo_ctx.positive_payload, fs->holo_ctx.positive_payload_len,
                          fs->holo_ctx.negative_payload, fs->holo_ctx.negative_payload_len, output,
                          max_out, blend_mode);
    if (len >= 0) fs->stats.total_interference_reconstructions++;
    pair->pair_attestation = len >= 0 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    return len;
}

/* ============================================================================
 * DATASETS
 * ============================================================================ */

int32_t tf_create_dataset(tripartite_fs_t *fs, const char *name, const uint32_t *pair_ids,
                          uint32_t num_pairs)
{
    if (!fs || !name || (!pair_ids && num_pairs > 0) || num_pairs > TF_DATASET_MAX_PAIRS)
        return TF_EINVAL;
    if (fs->num_datasets >= TF_MAX_DATASETS) return TF_EFULL;
    for (uint32_t i = 0; i < num_pairs; i++)
        if (pair_ids[i] >= fs->num_pairs) return TF_EINVAL;

    tf_dataset_t *ds = &fs->datasets[fs->num_datasets];
    tf_mem_set(ds, 0, sizeof(*ds));
    ds->id = fs->num_datasets;
    tf_str_copy(ds->name, name, TF_MAX_NAME_LEN);
    for (uint32_t i = 0; i < num_pairs; i++) ds->pair_ids[ds->num_pairs++] = pair_ids[i];
    tf_m5_default(&ds->m5, (uint64_t) fs->num_datasets + 1);
    ds->coverage_ratio = tf_compute_coverage(&ds->m5);
    ds->attestation = LPRES_STATE_NEITHER;
    ds->active = true;

    fs->num_datasets++;
    fs->stats.total_datasets_created++;
    return (int32_t) ds->id;
}

int32_t tf_add_pair_to_dataset(tripartite_fs_t *fs, uint32_t dataset_id, uint32_t pair_id)
{
    if (!fs || dataset_id >= fs->num_datasets || pair_id >= fs->num_pairs) return TF_EINVAL;
    tf_dataset_t *ds = &fs->datasets[dataset_id];
    if (!ds->active) return TF_EINVAL;
    if (ds->num_pairs >= TF_DATASET_MAX_PAIRS) return TF_EFULL;
    ds->pair_ids[ds->num_pairs++] = pair_id;
    return 0;
}

/* ============================================================================
 * CONTAINERS
 * ============================================================================ */

int32_t tf_create_container(tripartite_fs_t *fs, const char *name, const char *signature)
{
    if (!fs || !name) return TF_EINVAL;
    if (fs->num_containers >= TF_MAX_CONTAINERS) return TF_EFULL;
    tf_container_t *c = &fs->containers[fs->num_containers];
    tf_mem_set(c, 0, sizeof(*c));
    c->id = fs->num_containers;
    tf_str_copy(c->name, name, TF_MAX_NAME_LEN);
    if (signature) tf_str_copy(c->signature, signature, 64);
    c->attestation = LPRES_STATE_NEITHER;
    c->active = true;
    fs->num_containers++;
    fs->stats.total_containers_created++;
    return (int32_t) c->id;
}

int32_t tf_container_add_file(tripartite_fs_t *fs, uint32_t container_id, uint32_t file_id)
{
    if (!fs || container_id >= fs->num_containers) return TF_EINVAL;
    tf_container_t *c = &fs->containers[container_id];
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!c->active || !file) return TF_EINVAL;
    if (c->num_entries >= TF_CONTAINER_MAX_ENTRIES) return TF_EFULL;

    tf_container_entry_t *e = &c->entries[c->num_entries++];
    e->file_id = file_id;
    e->type = file->type;
    e->offset = file->payload_offset;
    e->length = file->payload_length;
    tf_mem_copy(e->cid, file->cid, TF_CID_LEN);
    file->container_id = container_id;
    c->total_size += file->size;
    return 0;
}

/* ============================================================================
 * RENDERERS
 * ============================================================================ */

int32_t tf_create_renderer(tripartite_fs_t *fs, holo_render_mode_t render_mode,
                           holo_blend_mode_t blend_mode, uint32_t width, uint32_t height,
                           uint32_t dataset_id)
{
    if (!fs || dataset_id >= fs->num_datasets) return TF_EINVAL;
    if (fs->num_renderers >= TF_MAX_RENDERERS) return TF_EFULL;
    tf_renderer_t *r = &fs->renderers[fs->num_renderers];
    tf_mem_set(r, 0, sizeof(*r));
    r->id = fs->num_renderers;
    r->render_mode = render_mode;
    r->blend_mode = blend_mode;
    r->width = width;
    r->height = height;
    r->depth = 1;
    r->dataset_id = dataset_id;
    tf_m5_default(&r->m5, (uint64_t) fs->num_renderers + 1);
    r->coverage_ratio = tf_compute_coverage(&r->m5);
    r->attestation = LPRES_STATE_NEITHER;
    r->active = true;
    fs->num_renderers++;
    fs->stats.total_renderers_created++;
    return (int32_t) r->id;
}

int32_t tf_render(tripartite_fs_t *fs, uint32_t renderer_id, uint8_t *output, uint32_t max_out)
{
    if (!fs || !output || renderer_id >= fs->num_renderers) return TF_EINVAL;
    tf_renderer_t *r = &fs->renderers[renderer_id];
    if (!r->active || !fs->datasets[r->dataset_id].active) return TF_EINVAL;
    if (fs->loaded_pair == 0) return TF_EINVAL; /* nothing verified to render */
    int32_t len = holo_render(&fs->holo_ctx, output, max_out);
    r->attestation = len >= 0 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    return len;
}

/* ============================================================================
 * ECONOMIC OPERATIONS
 * ============================================================================ */

int32_t tf_price_file(tripartite_fs_t *fs, uint32_t file_id, uint64_t price, uint8_t pricing_form)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return TF_EINVAL;
    /* 1-based capital forms: 1..4 are state-reserved (inalienable), 5..9
     * alienable. Only alienable forms can carry a price. */
    if (pricing_form < 5 || pricing_form > 9) return TF_EINVAL;
    file->price_per_access = price;
    file->pricing_form = pricing_form;
    return 0;
}

int32_t tf_purchase_access(tripartite_fs_t *fs, uint32_t file_id, uint32_t buyer_identity_id)
{
    (void) buyer_identity_id;
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return TF_ENOTSUP;
}

/* ============================================================================
 * MESH NET (index record only)
 * ============================================================================ */

int32_t tf_share_on_mesh(tripartite_fs_t *fs, uint32_t file_id, uint32_t mesh_network_id,
                         uint32_t trade_route_id)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file || !fs->mesh) return TF_EINVAL;
    if (file->num_trade_routes >= 8) return TF_EFULL;
    file->trade_route_ids[file->num_trade_routes++] = trade_route_id;
    file->mesh_network_id = mesh_network_id;
    fs->stats.total_mesh_shares++;
    return 0;
}

/* ============================================================================
 * NOT IMPLEMENTED (fail closed: no state changes)
 * ============================================================================ */

int32_t tf_wallet_sign_file(tripartite_fs_t *fs, uint32_t file_id, uint32_t key_id)
{
    (void) key_id;
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return TF_ENOTSUP;
}

int32_t tf_wallet_verify_file(tripartite_fs_t *fs, uint32_t file_id)
{
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return TF_ENOTSUP;
}

int32_t tf_bridge_file(tripartite_fs_t *fs, uint32_t file_id, const char *target_chain,
                       const char *target_address)
{
    (void) target_chain;
    (void) target_address;
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return TF_ENOTSUP;
}

int32_t tf_enforce_policy(tripartite_fs_t *fs, uint32_t file_id, uint32_t policy_id)
{
    (void) policy_id;
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return TF_ENOTSUP;
}

/* ============================================================================
 * SELF-AUDIT
 * ============================================================================ */

int32_t tf_self_audit_file(tripartite_fs_t *fs, uint32_t file_id)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return TF_EINVAL;
    bool ok = file->verified;
    file->coverage_ratio = tf_compute_coverage(&file->m5);
    if (SR_CMP(file->coverage_ratio, fs->config.min_global_coverage) < 0) ok = false;
    if (file->pair_id > 0 && tf_validate_pair(fs, file->pair_id - 1) < 0) ok = false;
    if (!ok) {
        file->lpres_state = file->verified ? LPRES_STATE_BOTH : LPRES_STATE_FALSE;
        file->global_attestation = file->lpres_state;
    }
    return ok ? 0 : TF_EMISMATCH;
}

int32_t tf_self_audit_system(tripartite_fs_t *fs)
{
    if (!fs) return TF_EINVAL;
    uint32_t failed = 0;
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].active && tf_self_audit_file(fs, fs->files[i].id) < 0) failed++;
    for (uint32_t i = 0; i < fs->num_pairs; i++)
        if (fs->pairs[i].active && tf_validate_pair(fs, i) < 0) failed++;
    return failed == 0 ? 0 : TF_EMISMATCH;
}

int32_t tf_self_heal_file(tripartite_fs_t *fs, uint32_t file_id)
{
    if (!tf_get_file(fs, file_id)) return TF_EINVAL;
    return tf_self_audit_file(fs, file_id) == 0 ? 0 : TF_ENOTSUP;
}

/* ============================================================================
 * HEALTH
 * ============================================================================ */

int32_t tf_check_file_health(tripartite_fs_t *fs, uint32_t file_id, void *health_out)
{
    (void) health_out;
    return tf_self_audit_file(fs, file_id);
}

int32_t tf_check_global_health(tripartite_fs_t *fs)
{
    if (!fs) return TF_EINVAL;
    int32_t r = tf_self_audit_system(fs);
    fs->global_safety_gate = (r == 0 && fs->num_files > 0);
    fs->global_attestation = fs->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    return r;
}

bool tf_global_safety_gate(tripartite_fs_t *fs)
{
    return fs ? fs->global_safety_gate : false;
}

/* ============================================================================
 * COVERAGE ENFORCEMENT
 * ============================================================================ */

void tf_update_coverage(tripartite_fs_t *fs)
{
    if (!fs) return;
    fs->coverage_ratio = tf_compute_coverage(&fs->m5);
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].active)
            fs->files[i].coverage_ratio = tf_compute_coverage(&fs->files[i].m5);
}

bool tf_enforce_coverage(tripartite_fs_t *fs, surplus_real_t min_ratio)
{
    if (!fs) return false;
    if (SR_CMP(fs->coverage_ratio, min_ratio) < 0) return false;
    for (uint32_t i = 0; i < fs->num_files; i++)
        if (fs->files[i].active && SR_CMP(fs->files[i].coverage_ratio, min_ratio) < 0) return false;
    return true;
}

/* ============================================================================
 * STATISTICS / STATE
 * ============================================================================ */

void tf_get_stats(tripartite_fs_t *fs, void *stats_out)
{
    if (!fs || !stats_out) return;
    tf_mem_copy(stats_out, &fs->stats, sizeof(fs->stats));
}

lpres_state_t tf_get_file_attestation(tripartite_fs_t *fs, uint32_t file_id)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    return file ? file->lpres_state : LPRES_STATE_NEITHER;
}

/* Sets the attestation label only; it cannot make a file verified. */
void tf_set_file_attestation(tripartite_fs_t *fs, uint32_t file_id, lpres_state_t state)
{
    tf_file_entry_t *file = tf_get_file(fs, file_id);
    if (!file) return;
    file->lpres_state = state;
    file->global_attestation = state;
}

const char *tf_lpres_state_name(lpres_state_t state)
{
    return lpres_state_name(state);
}
