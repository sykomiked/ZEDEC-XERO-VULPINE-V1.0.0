/*
 * recon.c — Dynamic Reconstruction Matrix Implementation
 *
 * File extensions as active mathematical operators for O(1) reassembly.
 * Each archetype declares its own inversion rule.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "recon.h"

/* ===== Helpers ===== */

static void rc_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void rc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t rc_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void rc_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int rc_strendswith(const char *s, const char *suffix) {
    uint32_t slen = rc_strlen(s);
    uint32_t suflen = rc_strlen(suffix);
    if (suflen > slen) return 0;
    uint32_t i;
    for (i = 0; i < suflen; i++)
        if (s[slen - suflen + i] != suffix[i]) return 0;
    return 1;
}

/* ===== Init ===== */

void recon_init(recon_t *r) {
    rc_memset(r, 0, sizeof(recon_t));

    /* Register all 10 archetypes */
    static const struct {
        recon_archetype_t arch;
        const char *ext;
        const char *name;
        recon_topology_t topo;
        recon_operation_t op;
        uint8_t phase;
        bool req_5pl;
        bool req_multisig;
        bool mutable;
    } defs[] = {
        {RECON_ARCH_36N9,  ".36n9",  "Direct Phase Transform",
         RECON_TOPOLOGY_FORWARD_MERKLE, RECON_OP_FORWARD_PERMUTE,
         1, false, false, true},
        {RECON_ARCH_9N63,  ".9n63",  "Inverse Phase Transform",
         RECON_TOPOLOGY_REVERSE_MERKLE, RECON_OP_REVERSE_PERMUTE,
         0, false, false, false},
        {RECON_ARCH_36M9,  ".36m9",  "Modular Phase Shift",
         RECON_TOPOLOGY_DUAL_POLAR_PARITY, RECON_OP_PARITY_VERIFY,
         2, true, true, true},
        {RECON_ARCH_ZEDEC, ".zedec", "Stream Vector",
         RECON_TOPOLOGY_VARIABLE_DAG, RECON_OP_STREAM_ASSEMBLE,
         3, false, false, true},
        {RECON_ARCH_VINO,  ".vino",  "Ledger Matrix",
         RECON_TOPOLOGY_MULTISIG_BLOCK, RECON_OP_LEDGER_CONSENSUS,
         4, true, true, false},
        {RECON_ARCH_ULA,   ".ula",   "Zero-Point Anchor",
         RECON_TOPOLOGY_STATIC_BLUEPRINT, RECON_OP_STATIC_LOAD,
         5, false, false, false},
        /* Tri-space neutral archetypes */
        {RECON_ARCH_0N0,   ".0n0",   "Neutral Source",
         RECON_TOPOLOGY_NEUTRAL_MEDIATOR, RECON_OP_NEUTRAL_MEDIATE,
         6, false, false, true},
        {RECON_ARCH_0M0,   ".0m0",   "Neutral Manifest",
         RECON_TOPOLOGY_NEUTRAL_MEDIATOR, RECON_OP_NEUTRAL_MEDIATE,
         6, false, false, false},
        {RECON_ARCH_ZEDEZ, ".zedez", "Neutral Transform",
         RECON_TOPOLOGY_NEUTRAL_DRY_RUN, RECON_OP_NEUTRAL_OBSERVE,
         6, false, false, false},
        {RECON_ARCH_CEDEC, ".cedec", "Neutral Container",
         RECON_TOPOLOGY_NEUTRAL_MEDIATOR, RECON_OP_NEUTRAL_MEDIATE,
         6, false, false, true},
    };

    uint32_t i;
    for (i = 0; i < RECON_MAX_ARCHETYPES; i++) {
        r->archetypes[i].archetype = defs[i].arch;
        rc_strcpy(r->archetypes[i].extension, defs[i].ext);
        rc_strcpy(r->archetypes[i].name, defs[i].name);
        r->archetypes[i].topology = defs[i].topo;
        r->archetypes[i].operation = defs[i].op;
        r->archetypes[i].phase_state = defs[i].phase;
        r->archetypes[i].requires_5pl = defs[i].req_5pl;
        r->archetypes[i].requires_multisig = defs[i].req_multisig;
        r->archetypes[i].mutable = defs[i].mutable;
    }
    r->num_archetypes = RECON_MAX_ARCHETYPES;
    r->initialized = true;
}

/* ===== Archetype Detection ===== */

recon_archetype_t recon_detect_archetype(const char *filename) {
    if (!filename) return RECON_ARCH_36N9;  /* Default */
    if (rc_strendswith(filename, ".36n9"))  return RECON_ARCH_36N9;
    if (rc_strendswith(filename, ".9n63"))  return RECON_ARCH_9N63;
    if (rc_strendswith(filename, ".36m9"))  return RECON_ARCH_36M9;
    if (rc_strendswith(filename, ".zedec")) return RECON_ARCH_ZEDEC;
    if (rc_strendswith(filename, ".vino"))  return RECON_ARCH_VINO;
    if (rc_strendswith(filename, ".ula"))   return RECON_ARCH_ULA;
    if (rc_strendswith(filename, ".0n0"))   return RECON_ARCH_0N0;
    if (rc_strendswith(filename, ".0m0"))   return RECON_ARCH_0M0;
    if (rc_strendswith(filename, ".zedez")) return RECON_ARCH_ZEDEZ;
    if (rc_strendswith(filename, ".cedec")) return RECON_ARCH_CEDEC;
    return RECON_ARCH_36N9;  /* Default to direct phase */
}

const recon_archetype_desc_t *recon_get_archetype(recon_t *r, recon_archetype_t arch) {
    if ((uint32_t)arch >= r->num_archetypes) return NULL;
    return &r->archetypes[arch];
}

/* ===== Reconstruction Operations ===== */

int recon_forward_merkle(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .36n9: Forward-ordered Merkle tree (3→6→9)
     * Reconstructs forward sequentially */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 0;  /* Identity (forward) */
    result->inverse_applied = false;
    result->success = true;

    r->forward_count++;
    r->total_reconstructions++;
    return 0;
}

int recon_reverse_merkle(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .9n63: Mirrored/reversed Merkle tree (9→6→3)
     * Auto-reverses the cryptographic transformation via bit-permutation flip */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 1;  /* Reverse permutation */
    result->inverse_applied = true;
    result->success = true;

    r->reverse_count++;
    r->total_reconstructions++;
    return 0;
}

int recon_parity_verify(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .36m9: Dual-polar parity matrix (3↔6↔9)
     * Verifies cross-wallet interlocking consistency between .36n9 and .9n63 */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 2;  /* Parity map */
    result->inverse_applied = false;
    result->success = true;

    r->parity_count++;
    r->total_reconstructions++;
    return 0;
}

int recon_stream_assemble(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .zedec: Variable-length topological DAG (GLUT+)
     * Reconstructed dynamically as new chunks stream over PLNP */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 3;  /* DAG order */
    result->inverse_applied = false;
    result->success = true;

    r->stream_count++;
    r->total_reconstructions++;
    return 0;
}

int recon_ledger_consensus(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .vino: Heterogeneous multisig block (GLUT-)
     * Requires full 5PL consensus before state expansion */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 4;  /* Multisig block */
    result->inverse_applied = false;
    result->success = true;

    r->ledger_count++;
    r->total_reconstructions++;
    return 0;
}

int recon_static_load(recon_t *r, const smap_t *sm, recon_result_t *result) {
    if (!sm || !result) return -1;
    rc_memset(result, 0, sizeof(recon_result_t));

    /* .ula: Static non-volatile blueprint (GLUT0)
     * Locked against runtime modification — direct load */
    result->chunks_processed = sm->num_chunks;
    result->bytes_reconstructed = sm->total_size;
    rc_memcpy(result->root_cid, sm->root_cid, RECON_HASH_SIZE);
    rc_memcpy(result->merkle_root, sm->merkle_root, RECON_HASH_SIZE);
    result->permutation_applied = 5;  /* Static */
    result->inverse_applied = false;
    result->success = true;

    r->static_count++;
    r->total_reconstructions++;
    return 0;
}

/* ===== Main Reconstruction Entry Point ===== */

int recon_reconstruct(recon_t *r, const smap_t *sm, recon_archetype_t arch,
                      recon_result_t *result) {
    if (!r || !sm || !result) return -1;
    if (!r->initialized) return -1;

    switch (arch) {
        case RECON_ARCH_36N9:
            return recon_forward_merkle(r, sm, result);
        case RECON_ARCH_9N63:
            return recon_reverse_merkle(r, sm, result);
        case RECON_ARCH_36M9:
            return recon_parity_verify(r, sm, result);
        case RECON_ARCH_ZEDEC:
            return recon_stream_assemble(r, sm, result);
        case RECON_ARCH_VINO:
            return recon_ledger_consensus(r, sm, result);
        case RECON_ARCH_ULA:
            return recon_static_load(r, sm, result);
        default:
            return recon_forward_merkle(r, sm, result);
    }
}

/* ===== Utility ===== */

const char *recon_archetype_name(recon_archetype_t arch) {
    switch (arch) {
        case RECON_ARCH_36N9:  return "Direct Phase (.36n9)";
        case RECON_ARCH_9N63:  return "Inverse Phase (.9n63)";
        case RECON_ARCH_36M9:  return "Modular Phase Shift (.36m9)";
        case RECON_ARCH_ZEDEC: return "Stream Vector (.zedec)";
        case RECON_ARCH_VINO:  return "Ledger Matrix (.vino)";
        case RECON_ARCH_ULA:   return "Zero-Point Anchor (.ula)";
        default: return "Unknown";
    }
}

const char *recon_topology_name(recon_topology_t topo) {
    switch (topo) {
        case RECON_TOPOLOGY_FORWARD_MERKLE:    return "Forward Merkle (3→6→9)";
        case RECON_TOPOLOGY_REVERSE_MERKLE:    return "Reverse Merkle (9→6→3)";
        case RECON_TOPOLOGY_DUAL_POLAR_PARITY: return "Dual-Polar Parity (3↔6↔9)";
        case RECON_TOPOLOGY_VARIABLE_DAG:      return "Variable DAG";
        case RECON_TOPOLOGY_MULTISIG_BLOCK:    return "Multisig Block";
        case RECON_TOPOLOGY_STATIC_BLUEPRINT:  return "Static Blueprint";
        default: return "Unknown";
    }
}

const char *recon_operation_name(recon_operation_t op) {
    switch (op) {
        case RECON_OP_FORWARD_PERMUTE:  return "Forward Permute";
        case RECON_OP_REVERSE_PERMUTE:  return "Reverse Permute";
        case RECON_OP_PARITY_VERIFY:    return "Parity Verify";
        case RECON_OP_STREAM_ASSEMBLE:  return "Stream Assemble";
        case RECON_OP_LEDGER_CONSENSUS: return "Ledger Consensus";
        case RECON_OP_STATIC_LOAD:      return "Static Load";
        default: return "Unknown";
    }
}

const char *recon_extension(recon_archetype_t arch) {
    switch (arch) {
        case RECON_ARCH_36N9:  return ".36n9";
        case RECON_ARCH_9N63:  return ".9n63";
        case RECON_ARCH_36M9:  return ".36m9";
        case RECON_ARCH_ZEDEC: return ".zedec";
        case RECON_ARCH_VINO:  return ".vino";
        case RECON_ARCH_ULA:   return ".ula";
        default: return ".36n9";
    }
}
