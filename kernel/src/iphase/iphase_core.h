#ifndef IPHASE_CORE_H
#define IPHASE_CORE_H

#include "m5_types.h"

#define IPHASE_TOPOLOGY_TABLE_SIZE 1024

typedef struct iphase_topology_entry {
    lattice_node_id_t dest;
    phase_t phase_vector;
} iphase_topology_entry_t;

typedef struct iphase_topology_table {
    iphase_topology_entry_t entries[IPHASE_TOPOLOGY_TABLE_SIZE];
    uint32_t size;
} iphase_topology_table_t;

void iphase_init(void);
void iphase_add_entry(lattice_node_id_t dest, phase_t phase_vector);
double complex iphase_route(lattice_node_id_t src, lattice_node_id_t dest);

#endif
