/* lattice_ipc.h — OS Lattice Layer: Inter-Node Communication
 * Edges typed by the five M5 axes carry different communication semantics.
 * Per OS_LATTICE_LAYER_SPEC.md §1, §5.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef LATTICE_IPC_H
#define LATTICE_IPC_H

#include "lattice_core.h"

#define LATTICE_IPC_MAX_MSG 4096

typedef struct lattice_msg {
    uint32_t src_idx;
    uint32_t dst_idx;
    lattice_edge_type_t channel;
    ordinal_t ordinal;
    rational_t quota;
    trit_t attestation;
    phase_t phase;
    collapse_t choice;
    uint8_t payload[168];
    uint32_t payload_len;
    double complex telemetry;
} lattice_msg_t;

typedef struct lattice_ipc {
    lattice_graph_t *graph;
    lattice_msg_t msg_log[LATTICE_IPC_MAX_MSG];
    uint32_t msg_count;
} lattice_ipc_t;

void lattice_ipc_init(lattice_ipc_t *ipc, lattice_graph_t *g);
int lattice_ipc_send(lattice_ipc_t *ipc, uint32_t src_idx, uint32_t dst_idx,
                     lattice_edge_type_t channel, const void *payload, uint32_t len);
int lattice_ipc_send_typed(lattice_ipc_t *ipc, uint32_t src_idx, uint32_t dst_idx,
                           lattice_edge_type_t channel,
                           ordinal_t ord, rational_t quota, trit_t attest,
                           phase_t phase, collapse_t choice,
                           double complex telemetry);
lattice_msg_t *lattice_ipc_recv(lattice_ipc_t *ipc, uint32_t node_idx);
uint32_t lattice_ipc_msg_count(const lattice_ipc_t *ipc);

#endif
