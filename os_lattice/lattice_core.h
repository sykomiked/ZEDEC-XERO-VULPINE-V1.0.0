/* lattice_core.h — OS Lattice Layer: Core Node/Edge Data Structure
 * Implements the phyllotactic lattice from OS_LATTICE_LAYER_SPEC.md §1-§4.
 * Nodes are positioned on concentric golden-spiral rings.
 * Edges are typed by the five M5 axes.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef LATTICE_CORE_H
#define LATTICE_CORE_H

#include "m5_types.h"

#define LATTICE_MAX_NODES 4096
#define LATTICE_MAX_EDGES_PER_NODE 64
#define LATTICE_MAX_LEVELS 16

#define LATTICE_SCALE_NORMAL    1.0
#define LATTICE_SCALE_QUANTUM   1.61803398874989484820  /* phi */
#define LATTICE_SCALE_POSTQUANTUM 2.61803398874989484820  /* phi^2 = phi + 1 */

typedef enum {
    EDGE_ORDINAL = 0,
    EDGE_RATIONAL = 1,
    EDGE_LOGICAL = 2,
    EDGE_PHASE = 3,
    EDGE_CHOICE = 4
} lattice_edge_type_t;

typedef struct lattice_edge {
    uint32_t target_node_idx;
    lattice_edge_type_t type;
    rational_t weight;
    trit_t attestation;
    phase_t phase_vector;
    collapse_t choice_state;
} lattice_edge_t;

typedef struct lattice_node {
    lattice_node_id_t id;
    uint32_t level;
    uint32_t index_in_level;
    double complex position;
    phase_tick_t tick;
    lattice_edge_t edges[LATTICE_MAX_EDGES_PER_NODE];
    uint32_t num_edges;
    bool active;
} lattice_node_t;

typedef struct lattice_graph {
    lattice_node_t nodes[LATTICE_MAX_NODES];
    uint32_t num_nodes;
    uint32_t num_levels;
    double scale_factor;
    axiom_matrix_t *matrix;
} lattice_graph_t;

void lattice_init(lattice_graph_t *g, double scale_factor, axiom_matrix_t *matrix);
uint32_t lattice_add_node(lattice_graph_t *g, uint32_t level, lattice_node_id_t id);
void lattice_add_edge(lattice_graph_t *g, uint32_t src_idx, uint32_t dst_idx,
                      lattice_edge_type_t type, rational_t weight,
                      trit_t attestation, phase_t phase, collapse_t choice);
double complex lattice_position(uint32_t level, uint32_t index_in_level);
uint32_t lattice_nodes_at_level(uint32_t level);
uint32_t lattice_fib(uint32_t n);
lattice_node_t *lattice_get_node(lattice_graph_t *g, uint32_t idx);

#endif
