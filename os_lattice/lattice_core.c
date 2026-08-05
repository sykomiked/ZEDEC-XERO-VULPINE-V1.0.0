/* lattice_core.c — OS Lattice Layer: Core Implementation
 * Phyllotactic position formula: position(n,i) = phi^n * e^(2*pi*i*i / Fib(n+2))
 * Per OS_LATTICE_LAYER_SPEC.md §4.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "lattice_core.h"
#include <math.h>
#include <string.h>

static const double LATTICE_PHI = 1.61803398874989484820;

static uint32_t fib_table[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987};

uint32_t lattice_fib(uint32_t n) {
    if (n < sizeof(fib_table) / sizeof(fib_table[0])) return fib_table[n];
    uint32_t a = 0, b = 1;
    for (uint32_t i = 0; i < n; i++) { uint32_t t = a + b; a = b; b = t; }
    return a;
}

uint32_t lattice_nodes_at_level(uint32_t level) {
    return lattice_fib(level + 2);
}

double complex lattice_position(uint32_t level, uint32_t index_in_level) {
    uint32_t slots = lattice_nodes_at_level(level);
    if (slots == 0) slots = 1;
    double radius = 1.0;
    for (uint32_t i = 0; i < level; i++) radius *= LATTICE_PHI;
    double angle = 2.0 * M_PI * (double)index_in_level / (double)slots;
    return radius * (cos(angle) + I * sin(angle));
}

void lattice_init(lattice_graph_t *g, double scale_factor, axiom_matrix_t *matrix) {
    memset(g, 0, sizeof(lattice_graph_t));
    g->scale_factor = scale_factor;
    g->matrix = matrix;
    g->num_nodes = 0;
    g->num_levels = 0;
}

uint32_t lattice_add_node(lattice_graph_t *g, uint32_t level, lattice_node_id_t id) {
    if (g->num_nodes >= LATTICE_MAX_NODES) return UINT32_MAX;
    uint32_t idx = g->num_nodes++;
    lattice_node_t *node = &g->nodes[idx];
    memset(node, 0, sizeof(lattice_node_t));
    node->id = id;
    node->level = level;
    node->index_in_level = lattice_nodes_at_level(level);
    node->position = lattice_position(level, idx % lattice_nodes_at_level(level));
    node->active = true;
    node->num_edges = 0;
    if (level + 1 > g->num_levels) g->num_levels = level + 1;
    return idx;
}

void lattice_add_edge(lattice_graph_t *g, uint32_t src_idx, uint32_t dst_idx,
                      lattice_edge_type_t type, rational_t weight,
                      trit_t attestation, phase_t phase, collapse_t choice) {
    if (src_idx >= g->num_nodes || dst_idx >= g->num_nodes) return;
    lattice_node_t *node = &g->nodes[src_idx];
    if (node->num_edges >= LATTICE_MAX_EDGES_PER_NODE) return;
    lattice_edge_t *edge = &node->edges[node->num_edges++];
    edge->target_node_idx = dst_idx;
    edge->type = type;
    edge->weight = rational_normalize(weight);
    edge->attestation = attestation;
    edge->phase_vector = phase;
    edge->choice_state = choice;
}

lattice_node_t *lattice_get_node(lattice_graph_t *g, uint32_t idx) {
    if (idx >= g->num_nodes) return NULL;
    return &g->nodes[idx];
}
