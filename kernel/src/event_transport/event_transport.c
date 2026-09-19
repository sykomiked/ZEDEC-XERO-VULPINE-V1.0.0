/* event_transport.c — ZXV Event Transport Abstraction
 *
 * Implements the transport channel management, pending delivery queues,
 * and send/receive operations. In a real kernel, the transport-specific
 * backends (shmem, PCIe, network) would plug into this abstraction.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "event_transport.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return false;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Initialization ===== */

void et_init(et_transport_t *et) {
    if (!et) return;
    ev_memset(et, 0, sizeof(*et));
    et->next_channel_id = 1;
}

/* ===== Channel Management ===== */

int32_t et_create_channel(et_transport_t *et, const char *name,
                           et_transport_type_t type,
                           const char *local_node,
                           const char *remote_node) {
    if (!et || !name) return -1;

    for (uint32_t i = 0; i < ET_MAX_CHANNELS; i++) {
        if (!et->channels[i].registered) {
            ev_memset(&et->channels[i], 0, sizeof(et->channels[i]));
            et->channels[i].id = et->next_channel_id++;
            copy_str(et->channels[i].name, name, ET_MAX_NAME_LEN);
            et->channels[i].type = type;
            et->channels[i].state = ET_CHANNEL_CONNECTING;
            if (local_node)
                copy_str(et->channels[i].local_node, local_node, EV_NODE_ID_LEN);
            if (remote_node)
                copy_str(et->channels[i].remote_node, remote_node, EV_NODE_ID_LEN);
            et->channels[i].registered = true;
            et->num_channels++;
            return (int32_t)i;
        }
    }
    return -1;
}

et_channel_t *et_get_channel(et_transport_t *et, uint32_t idx) {
    if (!et || idx >= ET_MAX_CHANNELS) return NULL;
    if (!et->channels[idx].registered) return NULL;
    return &et->channels[idx];
}

bool et_channel_activate(et_transport_t *et, uint32_t channel_idx) {
    if (!et) return false;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return false;
    if (ch->state != ET_CHANNEL_CONNECTING && ch->state != ET_CHANNEL_DEGRADED)
        return false;
    ch->state = ET_CHANNEL_ACTIVE;
    ch->consecutive_errors = 0;
    return true;
}

bool et_channel_deactivate(et_transport_t *et, uint32_t channel_idx) {
    if (!et) return false;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return false;
    ch->state = ET_CHANNEL_DISCONNECTED;
    return true;
}

int32_t et_find_channel(et_transport_t *et, const char *remote_node) {
    if (!et || !remote_node) return -1;
    for (uint32_t i = 0; i < ET_MAX_CHANNELS; i++) {
        if (!et->channels[i].registered) continue;
        if (str_eq(et->channels[i].remote_node, remote_node))
            return (int32_t)i;
    }
    return -1;
}

bool et_channel_healthy(et_transport_t *et, uint32_t channel_idx) {
    if (!et) return false;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return false;
    return ch->state == ET_CHANNEL_ACTIVE && ch->consecutive_errors < 3;
}

/* ===== Send / Receive ===== */

et_delivery_result_t et_send(et_transport_t *et, uint32_t channel_idx,
                              const ev_envelope_t *env) {
    if (!et || !env) return ET_DELIVERY_FAILED;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return ET_DELIVERY_NO_CHANNEL;

    if (ch->state != ET_CHANNEL_ACTIVE) {
        ch->total_dropped++;
        et->total_failures++;
        return ET_DELIVERY_DROPPED;
    }

    /* Validate envelope before transport */
    if (!ev_envelope_validate(env)) {
        ch->consecutive_errors++;
        ch->total_dropped++;
        et->total_failures++;
        return ET_DELIVERY_FAILED;
    }

    /* For local transport, deliver immediately (no queueing) */
    if (ch->type == ET_TRANSPORT_LOCAL) {
        ch->total_sent++;
        et->total_deliveries++;
        return ET_DELIVERY_OK;
    }

    /* For other transports, enqueue for delivery */
    if (ch->pending_count >= ET_MAX_PENDING) {
        ch->total_dropped++;
        et->total_failures++;
        return ET_DELIVERY_DROPPED;
    }

    ch->pending[ch->pending_head] = *env;
    ch->pending_head = (ch->pending_head + 1) % ET_MAX_PENDING;
    ch->pending_count++;
    ch->total_sent++;
    et->total_deliveries++;

    return ET_DELIVERY_PENDING;
}

et_delivery_result_t et_receive(et_transport_t *et, uint32_t channel_idx,
                                 ev_envelope_t *out) {
    if (!et || !out) return ET_DELIVERY_FAILED;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return ET_DELIVERY_NO_CHANNEL;

    if (ch->pending_count == 0) return ET_DELIVERY_DROPPED;

    *out = ch->pending[ch->pending_tail];
    ch->pending_tail = (ch->pending_tail + 1) % ET_MAX_PENDING;
    ch->pending_count--;
    ch->total_received++;

    /* Validate received envelope */
    if (!ev_envelope_validate(out)) {
        ch->consecutive_errors++;
        et->total_failures++;
        return ET_DELIVERY_FAILED;
    }

    return ET_DELIVERY_OK;
}

uint32_t et_process_pending(et_transport_t *et, uint32_t channel_idx) {
    if (!et) return 0;
    et_channel_t *ch = et_get_channel(et, channel_idx);
    if (!ch) return 0;
    if (ch->state != ET_CHANNEL_ACTIVE) return 0;

    /* In a real implementation, this would flush pending envelopes
     * to the physical transport. For now, we just count them. */
    uint32_t count = ch->pending_count;

    /* Simulate successful delivery — clear pending queue */
    ch->pending_head = 0;
    ch->pending_tail = 0;
    ch->pending_count = 0;

    return count;
}

/* ---- DECLARATION -----------------------------------------------------------
 * A channel is addressed by NODE NAME, and the names come from the
 * constellation registry -- the loopback channel the arch main creates has both
 * endpoints set to a node id registered there. So the requirement is the
 * registry, transitively oseq, and the fixpoint derives that second edge
 * without it being restated here.
 *
 * NO BRING-UP YET: the transport instance is caller-owned, same as the
 * registry it sits on. */
#include "zxv_decl.h"

ZXV_DECLARE(event_transport,
    ZXV_PROVIDES(transport_ready),
    ZXV_REQUIRES(node_registry_ready),
    ZXV_NO_BRINGUP);
