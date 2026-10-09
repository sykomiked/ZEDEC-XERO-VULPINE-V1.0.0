/*
 * recon.h — Dynamic Reconstruction Matrix
 *
 * File extensions are active mathematical operators that tell the kernel
 * how to parse, invert, and reconstruct .smap reassembly key files.
 *
 * File Archetypes:
 *   .36n9  → Direct Phase Transform (forward Merkle: 3→6→9, TRUE)
 *   .9n63  → Inverse Phase Transform (reverse Merkle: 9→6→3, FALSE/Shadow)
 *   .36m9  → Modular Phase Shift (dual-polar parity, cross-wallet bridge)
 *   .zedec → Stream Vector (variable-length DAG, GLUT+)
 *   .vino  → Ledger Matrix (multisig block, GLUT-)
 *   .ula   → Zero-Point Anchor (static blueprint, GLUT0)
 *
 * The file extension declares the topological mirror rules for reassembly.
 * Reconstruction happens in O(1) time because the archetype pre-declares
 * the inversion rule — no scanning needed.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_RECON_H
#define ZEDEC_RECON_H

#include <stdint.h>
#include <stdbool.h>

#include "smap.h"

/* ===== Constants ===== */

#define RECON_MAX_ARCHETYPES    10
#define RECON_MAX_OPERATIONS   32
#define RECON_MAX_LABEL        64
#define RECON_HASH_SIZE         32

/* ===== File Archetypes ===== */

typedef enum {
    RECON_ARCH_36N9  = 0,  /* Direct Phase Transform (TRUE) */
    RECON_ARCH_9N63  = 1,  /* Inverse Phase Transform (FALSE/Shadow) */
    RECON_ARCH_36M9  = 2,  /* Modular Phase Shift (cross-wallet bridge) */
    RECON_ARCH_ZEDEC = 3,  /* Stream Vector (GLUT+) */
    RECON_ARCH_VINO  = 4,  /* Ledger Matrix (GLUT-) */
    RECON_ARCH_ULA   = 5,  /* Zero-Point Anchor (GLUT0) */
    /* Tri-space neutral archetypes (provisional) */
    RECON_ARCH_0N0   = 6,  /* Neutral Source — unresolved branches (S0) */
    RECON_ARCH_0M0   = 7,  /* Neutral Manifest — adjudication rules (S0) */
    RECON_ARCH_ZEDEZ = 8,  /* Neutral Transform — mediator/dry-run (S0) */
    RECON_ARCH_CEDEC = 9,  /* Neutral Container — staged/quarantine (S0) */
} recon_archetype_t;

/* ===== Matrix Topology ===== */

typedef enum {
    RECON_TOPOLOGY_FORWARD_MERKLE   = 0,  /* 3→6→9 */
    RECON_TOPOLOGY_REVERSE_MERKLE   = 1,  /* 9→6→3 */
    RECON_TOPOLOGY_DUAL_POLAR_PARITY = 2,  /* 3↔6↔9 */
    RECON_TOPOLOGY_VARIABLE_DAG      = 3,  /* Stream */
    RECON_TOPOLOGY_MULTISIG_BLOCK    = 4,  /* Ledger */
    RECON_TOPOLOGY_STATIC_BLUEPRINT  = 5,  /* Cold anchor */
    RECON_TOPOLOGY_NEUTRAL_MEDIATOR  = 6,  /* S0 — deferred resolution */
    RECON_TOPOLOGY_NEUTRAL_DRY_RUN   = 7,  /* S0 — observation only */
} recon_topology_t;

/* ===== Reconstruction Operation ===== */

typedef enum {
    RECON_OP_FORWARD_PERMUTE    = 0,  /* Standard forward Merkle decode */
    RECON_OP_REVERSE_PERMUTE    = 1,  /* Bit-permutation matrix flip */
    RECON_OP_PARITY_VERIFY      = 2,  /* Cross-wallet interlocking check */
    RECON_OP_STREAM_ASSEMBLE    = 3,  /* Dynamic chunk reassembly */
    RECON_OP_LEDGER_CONSENSUS   = 4,  /* 5PL consensus before expansion */
    RECON_OP_STATIC_LOAD        = 5,  /* Direct load, no transformation */
    RECON_OP_NEUTRAL_MEDIATE    = 6,  /* S0 — mediate/defer resolution */
    RECON_OP_NEUTRAL_OBSERVE    = 7,  /* S0 — observe without state change */
} recon_operation_t;

/* ===== Archetype Descriptor ===== */

typedef struct {
    recon_archetype_t  archetype;
    char               extension[8];       /* ".36n9", ".9n63", etc. */
    char               name[RECON_MAX_LABEL];
    recon_topology_t   topology;
    recon_operation_t  operation;
    uint8_t            phase_state;        /* TRUE=1, FALSE=0, GLUT+=3, etc. */
    bool               requires_5pl;       /* Needs 5PL consensus before reassembly */
    bool               requires_multisig;  /* Needs threshold signatures */
    bool               mutable;            /* Can be modified at runtime */
} recon_archetype_desc_t;

/* ===== Reconstruction Result ===== */

typedef struct {
    bool     success;
    uint32_t bytes_reconstructed;
    uint8_t  root_cid[RECON_HASH_SIZE];
    uint8_t  merkle_root[RECON_HASH_SIZE];
    uint32_t chunks_processed;
    uint32_t permutation_applied;  /* Which permutation was used */
    bool     inverse_applied;      /* Was inverse permutation needed? */
} recon_result_t;

/* ===== Reconstruction Engine ===== */

typedef struct {
    recon_archetype_desc_t archetypes[RECON_MAX_ARCHETYPES];
    uint32_t               num_archetypes;
    uint32_t               total_reconstructions;
    uint32_t               forward_count;
    uint32_t               reverse_count;
    uint32_t               parity_count;
    uint32_t               stream_count;
    uint32_t               ledger_count;
    uint32_t               static_count;
    bool                   initialized;
} recon_t;

/* ===== API ===== */

void recon_init(recon_t *r);

/* Archetype detection from filename */
recon_archetype_t recon_detect_archetype(const char *filename);
const recon_archetype_desc_t *recon_get_archetype(recon_t *r, recon_archetype_t arch);

/* Reconstruction operations */
int recon_reconstruct(recon_t *r, const smap_t *sm, recon_archetype_t arch,
                      recon_result_t *result);
int recon_forward_merkle(recon_t *r, const smap_t *sm, recon_result_t *result);
int recon_reverse_merkle(recon_t *r, const smap_t *sm, recon_result_t *result);
int recon_parity_verify(recon_t *r, const smap_t *sm, recon_result_t *result);
int recon_stream_assemble(recon_t *r, const smap_t *sm, recon_result_t *result);
int recon_ledger_consensus(recon_t *r, const smap_t *sm, recon_result_t *result);
int recon_static_load(recon_t *r, const smap_t *sm, recon_result_t *result);

/* Utility */
const char *recon_archetype_name(recon_archetype_t arch);
const char *recon_topology_name(recon_topology_t topo);
const char *recon_operation_name(recon_operation_t op);
const char *recon_extension(recon_archetype_t arch);

#endif /* ZEDEC_RECON_H */
