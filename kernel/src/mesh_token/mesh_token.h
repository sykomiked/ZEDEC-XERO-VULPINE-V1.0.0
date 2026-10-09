/* mesh_token.h — Mesh-Token External Settlement via Porter House
 *
 * A native ZXV settlement layer that routes token transfers between mesh
 * nodes through Porter House-gated connections. Unlike traditional
 * blockchain bridges that use TCP/IP sockets and external relayers,
 * Mesh-Token uses the JDR PirateNet mesh transport and Porter House
 * admission control to decide which peers are trusted enough to
 * receive settlement messages.
 *
 * Flow:
 *   1. A node requests an external settlement (send N tokens to peer P)
 *   2. Porter House checks if the settlement port admits peer P
 *   3. If admitted, a settlement message is queued for JDR PirateNet
 *      transport with a Count House-verified valuation
 *   4. The settlement is tracked until acknowledgment or timeout
 *
 * This is NOT a blockchain bridge. It is a P2P mesh settlement protocol
 * native to the M5 Axiomatic architecture, using 168-bit peer IDs,
 * Surplus Real fixed-point math, and M5 coordinate coverage verification.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef MESH_TOKEN_H
#define MESH_TOKEN_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "porter_house.h"
#include "count_house.h"

/* ===== Constants ===== */

#define MT_MAX_SETTLEMENTS    64
#define MT_MAX_LABEL_LEN      32
#define MT_SETTLEMENT_PORT    8800  /* JDR PirateNet service port (sealed by Porter House) */
#define MT_ACK_TIMEOUT_CYCLES 1000  /* timeout if no acknowledgment */

/* ===== Settlement States ===== */

typedef enum {
    MT_SETTLEMENT_UNUSED    = 0,
    MT_SETTLEMENT_PENDING   = 1,  /* queued, waiting for Porter House admission */
    MT_SETTLEMENT_ADMITTED  = 2,  /* Porter House admitted, waiting for transport */
    MT_SETTLEMENT_IN_TRANSIT = 3, /* sent via JDR PirateNet, waiting for ack */
    MT_SETTLEMENT_CONFIRMED = 4,  /* acknowledged by peer */
    MT_SETTLEMENT_REJECTED  = 5,  /* Porter House rejected the peer */
    MT_SETTLEMENT_TIMEOUT   = 6,  /* no acknowledgment within timeout */
    MT_SETTLEMENT_FAILED    = 7   /* transport or valuation failure */
} mt_settlement_state_t;

/* ===== Settlement Record ===== */

typedef struct mt_settlement {
    uint32_t id;
    word168_t sender_id;
    word168_t receiver_id;
    uint64_t amount;                 /* token amount to settle */
    surplus_real_t valuation;        /* Count House V_local at time of settlement */
    mt_settlement_state_t state;
    uint32_t trust_weight;           /* peer's trust weight at admission time */
    uint64_t created_cycle;          /* cycle when settlement was requested */
    uint64_t confirmed_cycle;        /* cycle when ack was received (0 if not) */
    bool active;
} mt_settlement_t;

/* ===== Mesh-Token Settlement Engine ===== */

typedef struct mesh_token_engine {
    uint32_t device_id;
    char name[MT_MAX_LABEL_LEN];

    mt_settlement_t settlements[MT_MAX_SETTLEMENTS];
    uint32_t num_settlements;
    uint32_t next_id;

    /* References to Porter House and Count House (not owned) */
    porter_house_t *porter;
    count_house_t *count_house;

    /* Stats */
    uint64_t total_settled;          /* total tokens successfully settled */
    uint64_t total_rejected;         /* settlements rejected by Porter House */
    uint64_t total_timeout;          /* settlements that timed out */
    uint64_t total_confirmed;        /* settlements confirmed by peer */

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} mesh_token_t;

/* ===== API ===== */

void mesh_token_init(mesh_token_t *mt, uint32_t device_id, const char *name,
                      porter_house_t *porter, count_house_t *count_house);

/* Request an external settlement: send `amount` tokens to `receiver`.
 * Porter House checks admission on MT_SETTLEMENT_PORT. If admitted,
 * the settlement is queued with the current Count House valuation.
 * Returns settlement ID on success, -1 if capacity exceeded,
 * -2 if Porter House rejected the peer. */
int32_t mesh_token_settle(mesh_token_t *mt, const word168_t *sender,
                           const word168_t *receiver, uint64_t amount,
                           uint32_t peer_trust_weight, uint64_t current_cycle);

/* Acknowledge a settlement (called when peer confirms receipt).
 * Returns 0 on success, -1 if settlement not found. */
int32_t mesh_token_ack(mesh_token_t *mt, uint32_t settlement_id, uint64_t current_cycle);

/* Check for timed-out settlements (called periodically from event loop).
 * Transitions IN_TRANSIT settlements older than MT_ACK_TIMEOUT_CYCLES
 * to TIMEOUT state. Returns number of settlements timed out. */
uint32_t mesh_token_check_timeouts(mesh_token_t *mt, uint64_t current_cycle);

/* Advance settlement state machine: move ADMITTED -> IN_TRANSIT.
 * Returns 0 on success, -1 if no settlements to advance. */
int32_t mesh_token_advance(mesh_token_t *mt, uint64_t current_cycle);

/* Get settlement by ID. Returns NULL if not found. */
mt_settlement_t *mesh_token_get(mesh_token_t *mt, uint32_t settlement_id);

/* Update M5 coverage for the settlement engine. */
surplus_real_t mesh_token_update_coverage(mesh_token_t *mt);

#endif /* MESH_TOKEN_H */
