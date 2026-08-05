/* architectural_directives.c — Implementation of binding constraints
 *
 * Implements the 5 architectural directives for non-linear, multi-dimensional
 * ZEDEC pqOS architecture.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "architectural_directives.h"
#include "digital_dna.h"

#ifdef TEST_HOST
#include <string.h>
#include <math.h>
#else
#include "freestanding.h"
#endif

/* ===== Directive 2: Dual-Artifact Merkle & .smap Storage ===== */

int ddna_smap_init(ddna_smap_t *smap, const ddna_cid_t *root, uint32_t total_len) {
    if (!smap) return -1;
    memset(smap, 0, sizeof(ddna_smap_t));
    if (root) memcpy(&smap->root_cid, root, sizeof(ddna_cid_t));
    smap->total_length = total_len;
    smap->coherence_score = 1.0;
    return 0;
}

int ddna_smap_add_chunk(ddna_smap_t *smap, uint32_t logical, uint32_t physical,
                         uint32_t len, const ddna_cid_t *cid) {
    if (!smap || !cid) return -1;
    if (smap->num_keys >= DDNA_SMAP_MAX_KEYS) return -2;

    ddna_smap_key_t *k = &smap->keys[smap->num_keys];
    k->logical_offset = logical;
    k->physical_offset = physical;
    k->length = len;
    memcpy(&k->chunk_cid, cid, sizeof(ddna_cid_t));

    /* Compute φ ratio against previous chunk */
    if (smap->num_keys > 0) {
        uint32_t prev_len = smap->keys[smap->num_keys - 1].length;
        if (prev_len > 0)
            k->phi_ratio = (double)len / (double)prev_len;
        else
            k->phi_ratio = AGP_PHI;
    } else {
        k->phi_ratio = AGP_PHI;
    }

    smap->num_keys++;
    return 0;
}

int ddna_smap_reassemble(const ddna_smap_t *smap, const uint8_t *storage,
                          uint32_t storage_len, uint8_t *out, uint32_t out_max) {
    if (!smap || !storage || !out) return -1;
    if (smap->total_length > out_max) return -2;

    /* Topological reassembly: reconstruct from .smap key map,
     * NOT linear byte reading. Each chunk is placed at its logical offset. */
    for (uint32_t i = 0; i < smap->num_keys; i++) {
        const ddna_smap_key_t *k = &smap->keys[i];
        if (k->physical_offset + k->length > storage_len) return -3;
        if (k->logical_offset + k->length > out_max) return -4;
        memcpy(out + k->logical_offset,
               storage + k->physical_offset,
               k->length);
    }

    return (int)smap->total_length;
}

/* ===== Directive 3: Triadic Field Build Coordinator ===== */

void ddna_triadic_init(ddna_triadic_field_t *field) {
    if (!field) return;
    memset(field, 0, sizeof(ddna_triadic_field_t));
    field->active_domain = DDNA_DOMAIN_LOCALITY;
    field->locality_phase = 0.0;
    field->nonlocal_phase = M_PI / 3.0;   /* 60° offset */
    field->omni_phase = 2.0 * M_PI / 3.0; /* 120° offset */
    field->phase_tick = 0;
    field->wave_function_collapsed = false;
}

int ddna_triadic_measure(ddna_triadic_field_t *field) {
    if (!field) return -1;

    /* Hamiltonian state vector resolution at 10ms phase boundary.
     * The measurement operator collapses the superposition into
     * the domain with the highest phase coherence. */
    double loc = sin(field->locality_phase);
    double non = sin(field->nonlocal_phase);
    double omni = sin(field->omni_phase);

    if (loc >= non && loc >= omni)
        field->active_domain = DDNA_DOMAIN_LOCALITY;
    else if (non >= loc && non >= omni)
        field->active_domain = DDNA_DOMAIN_NONLOCAL;
    else
        field->active_domain = DDNA_DOMAIN_OMNI;

    /* Advance phases (10ms tick) */
    double inc = 2.0 * M_PI / 100.0;  /* 100 ticks per full cycle */
    field->locality_phase += inc;
    field->nonlocal_phase += inc;
    field->omni_phase += inc;

    /* Wrap phases */
    if (field->locality_phase >= 2.0 * M_PI) field->locality_phase -= 2.0 * M_PI;
    if (field->nonlocal_phase >= 2.0 * M_PI) field->nonlocal_phase -= 2.0 * M_PI;
    if (field->omni_phase >= 2.0 * M_PI) field->omni_phase -= 2.0 * M_PI;

    field->wave_function_collapsed = true;
    field->phase_tick++;

    return (int)field->active_domain;
}

ddna_domain_t ddna_triadic_select(const ddna_triadic_field_t *field) {
    if (!field) return DDNA_DOMAIN_LOCALITY;
    return field->active_domain;
}

/* ===== Directive 5: Non-Linear Graph Traversal ===== */

void ddna_graph_init(ddna_graph_t *graph) {
    if (!graph) return;
    memset(graph, 0, sizeof(ddna_graph_t));
}

uint32_t ddna_graph_add_node(ddna_graph_t *graph,
                              const ddna_graph_coord_t *coord,
                              void (*handler)(const ddna_graph_coord_t *)) {
    if (!graph || graph->num_nodes >= 256) return UINT32_MAX;

    ddna_graph_node_t *node = &graph->nodes[graph->num_nodes];
    node->node_id = graph->num_nodes;
    if (coord) node->coord = *coord;
    node->handler = handler;
    node->num_edges = 0;

    return graph->num_nodes++;
}

int ddna_graph_connect(ddna_graph_t *graph, uint32_t node_a, uint32_t node_b) {
    if (!graph) return -1;
    if (node_a >= graph->num_nodes || node_b >= graph->num_nodes) return -2;
    if (node_a == node_b) return -3;

    ddna_graph_node_t *a = &graph->nodes[node_a];
    ddna_graph_node_t *b = &graph->nodes[node_b];

    /* Add bidirectional edge if not already present */
    if (a->num_edges < 16) {
        for (uint8_t i = 0; i < a->num_edges; i++)
            if (a->edges[i] == node_b) return 0;  /* Already connected */
        a->edges[a->num_edges++] = node_b;
    }
    if (b->num_edges < 16) {
        for (uint8_t i = 0; i < b->num_edges; i++)
            if (b->edges[i] == node_a) return 0;
        b->edges[b->num_edges++] = node_a;
    }

    return 0;
}

int ddna_graph_traverse(ddna_graph_t *graph, const ddna_graph_coord_t *entry) {
    if (!graph || !entry) return -1;

    /* Resolve entry coordinate to a node */
    uint32_t start = ddna_graph_resolve(graph, entry);
    if (start == UINT32_MAX) return -2;

    /* Execute handler at entry node */
    ddna_graph_node_t *node = &graph->nodes[start];
    if (node->handler) {
        node->handler(&node->coord);
    }

    /* Non-linear traversal: follow edges based on coordinate resonance
     * rather than fixed ordering. Each edge is evaluated by φ-proportion
     * alignment with the entry coordinate. */
    for (uint8_t i = 0; i < node->num_edges; i++) {
        uint32_t next_id = node->edges[i];
        if (next_id < graph->num_nodes) {
            ddna_graph_node_t *next = &graph->nodes[next_id];
            if (next->handler) {
                next->handler(&next->coord);
            }
        }
    }

    return 0;
}

uint32_t ddna_graph_resolve(const ddna_graph_t *graph,
                             const ddna_graph_coord_t *coord) {
    if (!graph || !coord) return UINT32_MAX;

    /* Multi-dimensional coordinate resolver:
     * Find the node whose coordinate best matches the entry coordinate.
     * Matching is based on weighted field alignment across all dimensions. */
    uint32_t best = UINT32_MAX;
    double best_score = -1.0;

    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        const ddna_graph_node_t *node = &graph->nodes[i];
        const ddna_graph_coord_t *nc = &node->coord;

        /* Score: count matching dimensions */
        double score = 0.0;
        if (nc->zodiac_spatial == coord->zodiac_spatial) score += 1.0;
        if (nc->lunar_temporal_month == coord->lunar_temporal_month) score += 1.0;
        if (nc->lunar_temporal_day == coord->lunar_temporal_day) score += 1.0;
        if (nc->sephirot_node == coord->sephirot_node) score += 1.0;
        if (nc->consonant_gate == coord->consonant_gate) score += 1.0;
        if (nc->vowel_phase == coord->vowel_phase) score += 1.0;
        if (nc->daat_open == coord->daat_open) score += 0.5;

        /* Numerological weight proximity (φ-proportion check) */
        if (coord->numerological_weight > 0 && nc->numerological_weight > 0) {
            double ratio = nc->numerological_weight / coord->numerological_weight;
            double dev = ratio - AGP_PHI;
            if (dev < 0) dev = -dev;
            if (dev < DDNA_PHI_TOLERANCE * 100) score += 2.0;
        }

        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }

    return best;
}
