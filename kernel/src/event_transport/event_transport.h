/* event_transport.h — ZXV Event Transport Abstraction
 *
 * All transports present the same logical event envelope regardless
 * of physical topology. This layer abstracts:
 *   - Same-core boundary: direct protected call
 *   - Same chip: shared memory and doorbell interrupt
 *   - Same motherboard: shared memory, PCIe, CXL, mailbox
 *   - Separate device: authenticated network protocol
 *   - Security microcontroller: bounded mailbox
 *   - GPU/NPU: command and completion queues
 *
 * The transport layer is the physical bridge for the event-space fabric.
 * It never interprets event semantics — it only delivers envelopes.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef EVENT_TRANSPORT_H
#define EVENT_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define ET_MAX_CHANNELS       16    /* max transport channels */
#define ET_MAX_NAME_LEN       32    /* channel name length */
#define ET_MAX_PENDING        32    /* max pending deliveries per channel */

/* ===== Transport Types ===== */

typedef enum {
    ET_TRANSPORT_NONE     = 0,
    ET_TRANSPORT_LOCAL    = 1,  /* same-core direct call */
    ET_TRANSPORT_SHMEM    = 2,  /* shared memory + doorbell */
    ET_TRANSPORT_PCIE     = 3,  /* PCIe / CXL */
    ET_TRANSPORT_NETWORK  = 4,  /* authenticated network */
    ET_TRANSPORT_MAILBOX  = 5,  /* bounded mailbox (security MCU) */
    ET_TRANSPORT_QUEUE    = 6,  /* command/completion queue (GPU/NPU) */
} et_transport_type_t;

/* ===== Channel State ===== */

typedef enum {
    ET_CHANNEL_INACTIVE   = 0,
    ET_CHANNEL_CONNECTING = 1,
    ET_CHANNEL_ACTIVE     = 2,
    ET_CHANNEL_DEGRADED   = 3,
    ET_CHANNEL_DISCONNECTED = 4,
} et_channel_state_t;

/* ===== Delivery Result ===== */

typedef enum {
    ET_DELIVERY_OK         = 0,
    ET_DELIVERY_DROPPED    = 1,  /* channel full or inactive */
    ET_DELIVERY_FAILED     = 2,  /* transport error */
    ET_DELIVERY_PENDING    = 3,  /* queued for later delivery */
    ET_DELIVERY_NO_CHANNEL = 4,  /* no channel for destination */
} et_delivery_result_t;

/* ===== Transport Channel ===== */

typedef struct et_channel {
    uint32_t id;
    char name[ET_MAX_NAME_LEN];       /* e.g., "arm64-to-x86" */
    et_transport_type_t type;
    et_channel_state_t state;

    char local_node[EV_NODE_ID_LEN];
    char remote_node[EV_NODE_ID_LEN];

    /* Pending delivery queue (simplified — real impl uses transport-specific buffer) */
    ev_envelope_t pending[ET_MAX_PENDING];
    uint32_t pending_head;
    uint32_t pending_tail;
    uint32_t pending_count;

    /* Statistics */
    uint64_t total_sent;
    uint64_t total_received;
    uint64_t total_dropped;
    uint64_t total_retried;
    uint32_t consecutive_errors;

    bool registered;
} et_channel_t;

/* ===== Event Transport ===== */

typedef struct et_transport {
    et_channel_t channels[ET_MAX_CHANNELS];
    uint32_t num_channels;
    uint32_t next_channel_id;

    /* Global statistics */
    uint64_t total_deliveries;
    uint64_t total_failures;
    uint64_t total_retries;
} et_transport_t;

/* ===== API ===== */

/* Initialize the transport layer */
void et_init(et_transport_t *et);

/* Create a transport channel */
int32_t et_create_channel(et_transport_t *et, const char *name,
                           et_transport_type_t type,
                           const char *local_node,
                           const char *remote_node);

/* Activate a channel */
bool et_channel_activate(et_transport_t *et, uint32_t channel_idx);

/* Deactivate a channel */
bool et_channel_deactivate(et_transport_t *et, uint32_t channel_idx);

/* Send an event envelope over a channel */
et_delivery_result_t et_send(et_transport_t *et, uint32_t channel_idx,
                              const ev_envelope_t *env);

/* Receive an event envelope from a channel (poll-based) */
et_delivery_result_t et_receive(et_transport_t *et, uint32_t channel_idx,
                                 ev_envelope_t *out);

/* Find a channel by remote node name */
int32_t et_find_channel(et_transport_t *et, const char *remote_node);

/* Process pending deliveries (call periodically) */
uint32_t et_process_pending(et_transport_t *et, uint32_t channel_idx);

/* Get channel by index */
et_channel_t *et_get_channel(et_transport_t *et, uint32_t idx);

/* Check if a channel is healthy */
bool et_channel_healthy(et_transport_t *et, uint32_t channel_idx);

#endif /* EVENT_TRANSPORT_H */
