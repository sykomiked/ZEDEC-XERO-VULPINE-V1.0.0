/* macgyver.c — MacGyver Synchronized Build System (MCG0)
 *
 * Implements shared AST registry, obligation tracking, and triad link protocol.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "macgyver.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void copy_digest(uint8_t *dst, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len && i < MCG_MAX_DIGEST_LEN; i++)
        dst[i] = src[i];
}

/* Simple digest combine: XOR-based for kernel-safe hashing */
static void combine_digests(uint8_t *out, const uint8_t *a, uint32_t a_len,
                            const uint8_t *b, uint32_t b_len,
                            const uint8_t *c, uint32_t c_len) {
    for (uint32_t i = 0; i < MCG_MAX_DIGEST_LEN; i++) {
        uint8_t va = (i < a_len) ? a[i] : 0;
        uint8_t vb = (i < b_len) ? b[i] : 0;
        uint8_t vc = (i < c_len) ? c[i] : 0;
        out[i] = va ^ vb ^ vc ^ (uint8_t)(i + 1);
    }
}

/* ===== Registry Init ===== */

void mcg_registry_init(mcg_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_node_id = 1;
    reg->next_obligation_id = 1;
    reg->next_triad_id = 1;
}

/* ===== AST Node Management ===== */

int32_t mcg_add_node(mcg_registry_t *reg, mcg_node_type_t type, mcg_role_t role,
                     const char *name, uint32_t parent_id) {
    if (!reg || !name) return -1;
    if (reg->node_count >= MCG_MAX_NODES) return -1;
    mcg_node_t *n = &reg->nodes[reg->node_count];
    ev_memset(n, 0, sizeof(*n));
    n->id = reg->next_node_id++;
    n->type = type;
    n->role = role;
    copy_str(n->name, name, MCG_MAX_NAME_LEN);
    n->parent_id = parent_id;
    n->reviewed = false;
    n->generated = false;
    return (int32_t)reg->node_count++;
}

bool mcg_add_child(mcg_registry_t *reg, uint32_t parent_id, uint32_t child_id) {
    if (!reg) return false;
    mcg_node_t *parent = mcg_get_node(reg, parent_id);
    mcg_node_t *child = mcg_get_node(reg, child_id);
    if (!parent || !child) return false;
    if (parent->child_count >= 16) return false;
    parent->child_ids[parent->child_count++] = child_id;
    return true;
}

mcg_node_t *mcg_get_node(mcg_registry_t *reg, uint32_t id) {
    if (!reg) return NULL;
    for (uint32_t i = 0; i < reg->node_count; i++) {
        if (reg->nodes[i].id == id)
            return &reg->nodes[i];
    }
    return NULL;
}

bool mcg_set_node_digest(mcg_registry_t *reg, uint32_t id,
                         const uint8_t *digest, uint32_t len) {
    mcg_node_t *n = mcg_get_node(reg, id);
    if (!n || !digest) return false;
    if (len > MCG_MAX_DIGEST_LEN) len = MCG_MAX_DIGEST_LEN;
    copy_digest(n->digest, digest, len);
    n->digest_len = len;
    return true;
}

bool mcg_mark_reviewed(mcg_registry_t *reg, uint32_t id, bool reviewed) {
    mcg_node_t *n = mcg_get_node(reg, id);
    if (!n) return false;
    n->reviewed = reviewed;
    return true;
}

bool mcg_mark_generated(mcg_registry_t *reg, uint32_t id, bool generated) {
    mcg_node_t *n = mcg_get_node(reg, id);
    if (!n) return false;
    n->generated = generated;
    return true;
}

/* ===== Obligation Management ===== */

int32_t mcg_add_obligation(mcg_registry_t *reg, mcg_role_t role,
                           const char *label, const char *description,
                           uint32_t source_node, uint32_t target_node) {
    if (!reg || !label) return -1;
    if (reg->obligation_count >= MCG_MAX_OBLIGATIONS) return -1;
    mcg_obligation_t *o = &reg->obligations[reg->obligation_count];
    ev_memset(o, 0, sizeof(*o));
    o->id = reg->next_obligation_id++;
    o->role = role;
    copy_str(o->label, label, MCG_MAX_LABEL_LEN);
    copy_str(o->description, description ? description : "", MCG_MAX_LABEL_LEN);
    o->source_node_id = source_node;
    o->target_node_id = target_node;
    o->satisfied = false;
    o->verified = false;
    return (int32_t)reg->obligation_count++;
}

bool mcg_satisfy_obligation(mcg_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->obligation_count) return false;
    reg->obligations[idx].satisfied = true;
    return true;
}

bool mcg_verify_obligation(mcg_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->obligation_count) return false;
    reg->obligations[idx].verified = true;
    return true;
}

uint32_t mcg_count_unsatisfied(mcg_registry_t *reg, mcg_role_t role) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->obligation_count; i++) {
        if (reg->obligations[i].role == role && !reg->obligations[i].satisfied)
            count++;
    }
    return count;
}

uint32_t mcg_count_unverified(mcg_registry_t *reg) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->obligation_count; i++) {
        if (!reg->obligations[i].verified)
            count++;
    }
    return count;
}

/* ===== Triad Link Protocol ===== */

int32_t mcg_create_triad(mcg_registry_t *reg, const char *name,
                         uint32_t forge_node, uint32_t counterforge_node,
                         uint32_t mediatrix_node) {
    if (!reg || !name) return -1;
    if (reg->triad_count >= MCG_MAX_TRIADS) return -1;
    mcg_triad_t *t = &reg->triads[reg->triad_count];
    ev_memset(t, 0, sizeof(*t));
    t->triad_id = reg->next_triad_id++;
    copy_str(t->name, name, MCG_MAX_NAME_LEN);
    t->forge_node_id = forge_node;
    t->counterforge_node_id = counterforge_node;
    t->mediatrix_node_id = mediatrix_node;
    t->forge_status = MCG_STATUS_PENDING;
    t->counterforge_status = MCG_STATUS_PENDING;
    t->mediatrix_status = MCG_STATUS_PENDING;
    t->linked = false;
    t->atomic = true;
    return (int32_t)reg->triad_count++;
}

bool mcg_set_component_status(mcg_registry_t *reg, uint32_t triad_idx,
                              mcg_component_t comp, mcg_status_t status) {
    if (!reg || triad_idx >= reg->triad_count) return false;
    mcg_triad_t *t = &reg->triads[triad_idx];
    switch (comp) {
        case MCG_COMPONENT_FORGE:         t->forge_status = status; break;
        case MCG_COMPONENT_COUNTERFORGE:  t->counterforge_status = status; break;
        case MCG_COMPONENT_MEDIATRIX:     t->mediatrix_status = status; break;
        default: return false;
    }
    return true;
}

bool mcg_set_component_digest(mcg_registry_t *reg, uint32_t triad_idx,
                              mcg_component_t comp,
                              const uint8_t *digest, uint32_t len) {
    if (!reg || triad_idx >= reg->triad_count || !digest) return false;
    if (len > MCG_MAX_DIGEST_LEN) len = MCG_MAX_DIGEST_LEN;
    mcg_triad_t *t = &reg->triads[triad_idx];
    switch (comp) {
        case MCG_COMPONENT_FORGE:
            copy_digest(t->forge_digest, digest, len);
            t->forge_digest_len = len;
            break;
        case MCG_COMPONENT_COUNTERFORGE:
            copy_digest(t->counterforge_digest, digest, len);
            t->counterforge_digest_len = len;
            break;
        case MCG_COMPONENT_MEDIATRIX:
            copy_digest(t->mediatrix_digest, digest, len);
            t->mediatrix_digest_len = len;
            break;
        default: return false;
    }
    return true;
}

bool mcg_is_triad_complete(mcg_registry_t *reg, uint32_t triad_idx) {
    if (!reg || triad_idx >= reg->triad_count) return false;
    mcg_triad_t *t = &reg->triads[triad_idx];
    return t->forge_status == MCG_STATUS_COMPILED &&
           t->counterforge_status == MCG_STATUS_COMPILED &&
           t->mediatrix_status == MCG_STATUS_COMPILED;
}

bool mcg_compute_triad_digest(mcg_registry_t *reg, uint32_t triad_idx) {
    if (!reg || triad_idx >= reg->triad_count) return false;
    mcg_triad_t *t = &reg->triads[triad_idx];
    combine_digests(t->triad_digest,
                    t->forge_digest, t->forge_digest_len,
                    t->counterforge_digest, t->counterforge_digest_len,
                    t->mediatrix_digest, t->mediatrix_digest_len);
    t->triad_digest_len = MCG_MAX_DIGEST_LEN;
    return true;
}

bool mcg_link_triad(mcg_registry_t *reg, uint32_t triad_idx) {
    if (!mcg_is_triad_complete(reg, triad_idx)) return false;
    if (!mcg_compute_triad_digest(reg, triad_idx)) return false;
    reg->triads[triad_idx].linked = true;
    reg->triads[triad_idx].forge_status = MCG_STATUS_LINKED;
    reg->triads[triad_idx].counterforge_status = MCG_STATUS_LINKED;
    reg->triads[triad_idx].mediatrix_status = MCG_STATUS_LINKED;
    return true;
}

/* ===== Build Orchestration ===== */

bool mcg_compile_component(mcg_registry_t *reg, uint32_t triad_idx,
                           mcg_component_t comp) {
    if (!reg || triad_idx >= reg->triad_count) return false;
    mcg_triad_t *t = &reg->triads[triad_idx];

    /* Check that the source node exists and has a digest */
    uint32_t node_id = 0;
    switch (comp) {
        case MCG_COMPONENT_FORGE:         node_id = t->forge_node_id; break;
        case MCG_COMPONENT_COUNTERFORGE:  node_id = t->counterforge_node_id; break;
        case MCG_COMPONENT_MEDIATRIX:     node_id = t->mediatrix_node_id; break;
        default: return false;
    }

    mcg_node_t *node = mcg_get_node(reg, node_id);
    if (!node) return false;
    if (node->digest_len == 0) return false;

    /* Set component digest from node digest */
    mcg_set_component_digest(reg, triad_idx, comp, node->digest, node->digest_len);

    /* Mark as compiled */
    mcg_set_component_status(reg, triad_idx, comp, MCG_STATUS_COMPILED);
    return true;
}

bool mcg_build_triad(mcg_registry_t *reg, uint32_t triad_idx) {
    if (!reg || triad_idx >= reg->triad_count) return false;
    mcg_triad_t *t = &reg->triads[triad_idx];

    /* Compile all three components */
    bool ok = true;
    if (!mcg_compile_component(reg, triad_idx, MCG_COMPONENT_FORGE))
        ok = false;
    if (!mcg_compile_component(reg, triad_idx, MCG_COMPONENT_COUNTERFORGE)) {
        if (t->atomic) {
            mcg_set_component_status(reg, triad_idx, MCG_COMPONENT_FORGE, MCG_STATUS_FAILED);
            return false;
        }
        ok = false;
    }
    if (!mcg_compile_component(reg, triad_idx, MCG_COMPONENT_MEDIATRIX)) {
        if (t->atomic) {
            mcg_set_component_status(reg, triad_idx, MCG_COMPONENT_FORGE, MCG_STATUS_FAILED);
            mcg_set_component_status(reg, triad_idx, MCG_COMPONENT_COUNTERFORGE, MCG_STATUS_FAILED);
            return false;
        }
        ok = false;
    }

    if (!ok) return false;

    /* Link the triad */
    return mcg_link_triad(reg, triad_idx);
}

uint32_t mcg_count_by_status(mcg_registry_t *reg, mcg_status_t status) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->triad_count; i++) {
        if (reg->triads[i].forge_status == status) count++;
    }
    return count;
}

/* ===== Name Functions ===== */

const char *mcg_component_name(mcg_component_t comp) {
    switch (comp) {
        case MCG_COMPONENT_FORGE:        return "forge";
        case MCG_COMPONENT_COUNTERFORGE: return "counterforge";
        case MCG_COMPONENT_MEDIATRIX:    return "mediatrix";
        default:                          return "unknown";
    }
}

const char *mcg_node_type_name(mcg_node_type_t type) {
    switch (type) {
        case MCG_NODE_MODULE:     return "module";
        case MCG_NODE_FUNCTION:   return "function";
        case MCG_NODE_VARIABLE:   return "variable";
        case MCG_NODE_TYPE:       return "type";
        case MCG_NODE_EVENT:      return "event";
        case MCG_NODE_CAPABILITY: return "capability";
        case MCG_NODE_CONTRACT:   return "contract";
        case MCG_NODE_OBLIGATION: return "obligation";
        case MCG_NODE_EFFECT:     return "effect";
        case MCG_NODE_PARAMETER:  return "parameter";
        case MCG_NODE_RECORD:     return "record";
        case MCG_NODE_ARRAY:      return "array";
        case MCG_NODE_CALL:       return "call";
        case MCG_NODE_BLOCK:      return "block";
        default:                   return "unknown";
    }
}

const char *mcg_role_name(mcg_role_t role) {
    switch (role) {
        case MCG_ROLE_S_PLUS:  return "S+";
        case MCG_ROLE_S_MINUS: return "S-";
        case MCG_ROLE_S_ZERO:  return "S0";
        default:                return "unknown";
    }
}

const char *mcg_status_name(mcg_status_t status) {
    switch (status) {
        case MCG_STATUS_PENDING:     return "pending";
        case MCG_STATUS_COMPILING:   return "compiling";
        case MCG_STATUS_COMPILED:    return "compiled";
        case MCG_STATUS_FAILED:      return "failed";
        case MCG_STATUS_QUARANTINED: return "quarantined";
        case MCG_STATUS_LINKED:      return "linked";
        default:                      return "unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * THE PREFIX TRAP, CAUGHT: this directory is macgyver/ and every symbol in it
 * is mcg_*. The declaration is written from macgyver.h, not from the path.
 *
 * REQUIRES_NONE is measured: macgyver.o's `nm -u` is EMPTY. Note what that
 * refutes -- mcg_set_node_digest and mcg_compute_triad_digest sound like they
 * hash, and this module carries digests, but it never calls sha256: the digest
 * bytes arrive from the caller. A REQUIRES(sha256_ready) written from the API
 * names would have been wrong.
 */
#include "zxv_decl.h"
static int zxvd_macgyver_bringup(void) {
    static mcg_registry_t reg;
    mcg_registry_init(&reg);
    if (mcg_count_unverified(&reg) != 0u) return -1;
    return 0;
}

ZXV_DECLARE(macgyver,
    ZXV_PROVIDES(mcg_registry_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_macgyver_bringup));
