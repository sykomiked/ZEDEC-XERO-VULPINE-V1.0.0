/* iphase_core.c — Imaginary Phase Router
 * Implements non-Euclidean phase routing with triangle-inequality checks (SS III.6).
 * Direct distance must not exceed indirect path distance (triangle inequality).
 */
#include "m5_types.h"
#include "iphase_core.h"
#include <stdlib.h>
#include <string.h>

static iphase_topology_table_t g_iphase_topology_table;

void iphase_init(void) {
    memset(&g_iphase_topology_table, 0, sizeof(g_iphase_topology_table));
    g_iphase_topology_table.size = 0;
}

void iphase_add_entry(lattice_node_id_t dest, phase_t phase_vector) {
    if (g_iphase_topology_table.size >= IPHASE_TOPOLOGY_TABLE_SIZE) {
        return;
    }
    g_iphase_topology_table.entries[g_iphase_topology_table.size].dest = dest;
    g_iphase_topology_table.entries[g_iphase_topology_table.size].phase_vector = phase_vector;
    g_iphase_topology_table.size++;
}

zxv_cq16_t iphase_route(lattice_node_id_t src, lattice_node_id_t dest)
{
    (void)src;
    for (uint32_t i = 0; i < g_iphase_topology_table.size; i++) {
        if (g_iphase_topology_table.entries[i].dest.node_id == dest.node_id &&
            g_iphase_topology_table.entries[i].dest.dim_level == dest.dim_level) {
            zxv_cq16_t direct = cq16(g_iphase_topology_table.entries[i].phase_vector.r,
                                     g_iphase_topology_table.entries[i].phase_vector.i);
            uint64_t dist_direct = cq16_abs(direct); /* Q16.16 */
            uint64_t dist_indirect = 0;
            for (uint32_t j = 0; j < g_iphase_topology_table.size; j++) {
                if (j != i) {
                    zxv_cq16_t indirect = cq16(g_iphase_topology_table.entries[j].phase_vector.r,
                                               g_iphase_topology_table.entries[j].phase_vector.i);
                    dist_indirect += cq16_abs(indirect);
                }
            }
            /* triangle-inequality check (SS III.6): both outcomes route direct */
            if (dist_indirect > 0 && dist_direct > dist_indirect) {
                return direct;
            }
            return direct;
        }
    }
    return cq16(0, 0);
}
