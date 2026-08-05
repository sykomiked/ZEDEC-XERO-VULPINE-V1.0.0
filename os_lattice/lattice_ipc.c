/* lattice_ipc.c — OS Lattice Layer: IPC Implementation
 * Routes messages along typed lattice edges.
 * Uses IPHASE for phase-typed channels, OSEQ for ordinal ordering.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "lattice_ipc.h"
#include <string.h>

void lattice_ipc_init(lattice_ipc_t *ipc, lattice_graph_t *g) {
    memset(ipc, 0, sizeof(lattice_ipc_t));
    ipc->graph = g;
}

int lattice_ipc_send(lattice_ipc_t *ipc, uint32_t src_idx, uint32_t dst_idx,
                     lattice_edge_type_t channel, const void *payload, uint32_t len) {
    if (src_idx >= ipc->graph->num_nodes || dst_idx >= ipc->graph->num_nodes)
        return -1;
    if (ipc->msg_count >= LATTICE_IPC_MAX_MSG)
        return -1;
    if (len > 168) len = 168;

    lattice_node_t *src = &ipc->graph->nodes[src_idx];
    bool edge_found = false;
    for (uint32_t i = 0; i < src->num_edges; i++) {
        if (src->edges[i].target_node_idx == dst_idx && src->edges[i].type == channel) {
            edge_found = true;
            break;
        }
    }
    if (!edge_found) return -2;

    lattice_msg_t *msg = &ipc->msg_log[ipc->msg_count++];
    memset(msg, 0, sizeof(lattice_msg_t));
    msg->src_idx = src_idx;
    msg->dst_idx = dst_idx;
    msg->channel = channel;
    msg->ordinal = src->tick.omega;
    msg->attestation = TRIT_TRUE;
    if (payload && len > 0) {
        memcpy(msg->payload, payload, len);
        msg->payload_len = len;
    }
    return 0;
}

int lattice_ipc_send_typed(lattice_ipc_t *ipc, uint32_t src_idx, uint32_t dst_idx,
                           lattice_edge_type_t channel,
                           ordinal_t ord, rational_t quota, trit_t attest,
                           phase_t phase, collapse_t choice,
                           double complex telemetry) {
    if (lattice_ipc_send(ipc, src_idx, dst_idx, channel, NULL, 0) != 0)
        return -1;
    lattice_msg_t *msg = &ipc->msg_log[ipc->msg_count - 1];
    msg->ordinal = ord;
    msg->quota = rational_normalize(quota);
    msg->attestation = attest;
    msg->phase = phase;
    msg->choice = choice;
    msg->telemetry = telemetry;
    return 0;
}

lattice_msg_t *lattice_ipc_recv(lattice_ipc_t *ipc, uint32_t node_idx) {
    for (uint32_t i = 0; i < ipc->msg_count; i++) {
        if (ipc->msg_log[i].dst_idx == node_idx) {
            return &ipc->msg_log[i];
        }
    }
    return NULL;
}

uint32_t lattice_ipc_msg_count(const lattice_ipc_t *ipc) {
    return ipc->msg_count;
}
