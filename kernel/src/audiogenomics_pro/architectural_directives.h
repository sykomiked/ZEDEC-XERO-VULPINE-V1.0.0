/* architectural_directives.h — Binding Constraints for ZEDEC pqOS Implementation
 *
 * These directives enforce the non-linear, multi-dimensional paradigms of
 * the VOVINA SHAKINA architecture. All code in this system MUST adhere.
 *
 * 1. 5-State Paraconsistent Logic (LPRES) — never reduce to binary-only
 * 2. Dual-Artifact Merkle & .smap Storage — zero-copy, topological reassembly
 * 3. Triadic Field Build Coordinator — Locality / Non-Locality / Omni-Presence
 * 4. Linguistic & Numeric Validation — 22 consonants, 5 vowels, φ checksums, Da'at
 * 5. Non-Linear Graph Traversal — entry-point agnostic, multi-dimensional coordinates
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ARCHITECTURAL_DIRECTIVES_H
#define ARCHITECTURAL_DIRECTIVES_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Directive 1: 5-State Paraconsistent Logic Core (LPRES) ===== */
/* STRICT PROHIBITION: Never reduce conditional branches to binary true/false
 * within core execution logic. All evaluations must use the 5-state vector:
 *   TRUE (1)       — active execution
 *   FALSE (0)      — inversion / shadow lock
 *   GLUT_PLUS (3)  — speculative superposition
 *   GLUT_MINUS (4) — immutable ledger history
 *   GLUT_NEUTRAL(5)— neutral anchor / isolation
 *
 * Contradictions → G^0 isolation blocks, NEVER exceptions or crashes.
 */

typedef trit_t ddna_logic_state_t;

#define LPRES_TRUE        TRIT_TRUE
#define LPRES_FALSE       TRIT_FALSE
#define LPRES_GLUT_PLUS   TRIT_GLUT_PLUS
#define LPRES_GLUT_MINUS  TRIT_GLUT_MINUS
#define LPRES_GLUT_NEUTRAL TRIT_GLUT_NEUTRAL

/* Paraconsistent evaluation: returns 5-state instead of bool */
static inline ddna_logic_state_t lpres_evaluate(bool condition, bool speculative) {
    if (speculative && condition)  return LPRES_GLUT_PLUS;
    if (speculative && !condition) return LPRES_GLUT_MINUS;
    if (condition)                 return LPRES_TRUE;
    if (!speculative && !condition) return LPRES_FALSE;
    return LPRES_GLUT_NEUTRAL;  /* Contradiction → isolation */
}

/* Containment handler: shifts invalid states to G^0 isolation */
static inline ddna_logic_state_t lpres_contain(ddna_logic_state_t state) {
    if (state != LPRES_TRUE && state != LPRES_FALSE &&
        state != LPRES_GLUT_PLUS && state != LPRES_GLUT_MINUS) {
        return LPRES_GLUT_NEUTRAL;  /* Unknown → isolate */
    }
    return state;
}

/* ===== Directive 2: Dual-Artifact Merkle & .smap Storage ===== */
/* Zero-copy pointer separations. Every asset = 32-byte Root CID (on-chain)
 * + off-chain topological Reassembly Key File (.smap).
 * File loading reconstructs via .smap key map, NOT linear byte blocks.
 */

#define DDNA_CID_SIZE       32    /* Root CID size (on-chain) */
#define DDNA_SMAP_MAX_KEYS  256   /* Max reassembly keys per .smap */

typedef struct ddna_cid {
    uint8_t bytes[DDNA_CID_SIZE];  /* Content identifier */
} ddna_cid_t;

typedef struct ddna_smap_key {
    uint32_t logical_offset;   /* Where in the logical file */
    uint32_t physical_offset;  /* Where in physical storage */
    uint32_t length;           /* Chunk length */
    ddna_cid_t chunk_cid;      /* CID of this chunk */
    int32_t phi_ratio;         /* Golden ratio proportion for validation, Q16.16 */
} ddna_smap_key_t;

typedef struct ddna_smap {
    ddna_cid_t root_cid;                    /* Root content identifier */
    ddna_smap_key_t keys[DDNA_SMAP_MAX_KEYS]; /* Reassembly keys */
    uint32_t num_keys;                      /* Number of keys */
    uint32_t total_length;                  /* Total logical file length */
    int32_t coherence_score;                /* φ coherence of chunk sizes, Q16.16 */
} ddna_smap_t;

/* .smap API — topological reassembly, not linear reads */
int ddna_smap_init(ddna_smap_t *smap, const ddna_cid_t *root, uint32_t total_len);
int ddna_smap_add_chunk(ddna_smap_t *smap, uint32_t logical, uint32_t physical,
                         uint32_t len, const ddna_cid_t *cid);
int ddna_smap_reassemble(const ddna_smap_t *smap, const uint8_t *storage,
                          uint32_t storage_len, uint8_t *out, uint32_t out_max);

/* ===== Directive 3: Triadic Field Build Coordinator ===== */
/* Three-domain separation:
 *   LOCALITY      — deterministic C core, bare-metal, 10ms phase clock
 *   NON-LOCALITY  — entangled P2P mesh, speculative background threads
 *   OMNI-PRESENCE — H_omni operator field, cross-language conversion
 *
 * Build = Hamiltonian state vectors resolved via measurement operators
 * at each 10ms phase boundary, NOT sequential DAGs.
 */

typedef enum {
    DDNA_DOMAIN_LOCALITY    = 0,  /* Deterministic, bare-metal, phase-clocked */
    DDNA_DOMAIN_NONLOCAL    = 1,  /* Entangled P2P, speculative */
    DDNA_DOMAIN_OMNI        = 2   /* H_omni field, cross-domain conversion */
} ddna_domain_t;

typedef struct ddna_triadic_field {
    ddna_domain_t active_domain;
    uint32_t locality_phase;   /* Phase angle for deterministic core (binary turn) */
    uint32_t nonlocal_phase;   /* Phase angle for entangled mesh (binary turn) */
    uint32_t omni_phase;       /* Phase angle for H_omni field (binary turn) */
    uint64_t phase_tick;       /* 10ms tick counter */
    bool wave_function_collapsed;  /* True after measurement at phase boundary */
} ddna_triadic_field_t;

/* Triadic field API */
void ddna_triadic_init(ddna_triadic_field_t *field);
int ddna_triadic_measure(ddna_triadic_field_t *field);  /* Collapse at phase boundary */
ddna_domain_t ddna_triadic_select(const ddna_triadic_field_t *field);

/* ===== Directive 4: Linguistic & Numeric Validation ===== */
/* Already implemented in digital_dna.h:
 *   - 22 consonant hardware register framework (Hebrew/Aramaic dual-phase)
 *   - 5 vowel-driven logic operators (5PL)
 *   - φ golden ratio checksums (ddna_phi_checksum_*)
 *   - Da'at state gate (ddna_daat_open/close)
 *   - Numerology metadata interpreter (ddna_numerology_*)
 *   - 13-month lunar calendar (ddna_lunar_*)
 *   - 13-sign zodiac with Ophiuchus (ddna_zodiac_*)
 *   - Space-Time Operator Ω_astro (ddna_spacetime_*)
 *   - Sephirotic matrix base-10 → base-13 (ddna_sephirotic_*)
 */

/* ===== Directive 5: Non-Linear Graph Traversal ===== */
/* No hard-coded hierarchies. Modules exist as interconnected graph.
 * Entry-point agnostic: APIs accept multi-dimensional coordinates.
 */

typedef struct ddna_graph_coord {
    /* Multi-dimensional entry point — any valid coordinate initiates execution */
    uint8_t zodiac_spatial;        /* 0-12: zodiac spatial operator */
    uint8_t lunar_temporal_month;  /* 1-13: lunar temporal phase */
    uint8_t lunar_temporal_day;    /* 1-28: day within month */
    uint8_t sephirot_node;         /* 0-9: Sephirotic operational node */
    uint8_t consonant_gate;        /* 0-21: consonant hardware gate */
    uint8_t vowel_phase;           /* 0-4: 5PL vowel operator */
    int64_t numerological_weight;  /* Metadata harmonic weight, Q16.16 */
    bool daat_open;                /* Whether supernal access is requested */
} ddna_graph_coord_t;

/* Graph traversal API — enter from any coordinate */
typedef struct ddna_graph_node {
    ddna_graph_coord_t coord;      /* Entry coordinate */
    uint32_t node_id;              /* Unique node identifier */
    uint32_t edges[16];            /* Connected node IDs */
    uint8_t num_edges;             /* Number of connections */
    void (*handler)(const ddna_graph_coord_t *);  /* Node handler */
} ddna_graph_node_t;

typedef struct ddna_graph {
    ddna_graph_node_t nodes[256];  /* Node pool */
    uint32_t num_nodes;            /* Active node count */
} ddna_graph_t;

/* Graph API */
void ddna_graph_init(ddna_graph_t *graph);
uint32_t ddna_graph_add_node(ddna_graph_t *graph,
                              const ddna_graph_coord_t *coord,
                              void (*handler)(const ddna_graph_coord_t *));
int ddna_graph_connect(ddna_graph_t *graph, uint32_t node_a, uint32_t node_b);
int ddna_graph_traverse(ddna_graph_t *graph, const ddna_graph_coord_t *entry);

/* Multi-dimensional coordinate resolver: any valid coordinate → node */
uint32_t ddna_graph_resolve(const ddna_graph_t *graph,
                             const ddna_graph_coord_t *coord);

#endif /* ARCHITECTURAL_DIRECTIVES_H */
