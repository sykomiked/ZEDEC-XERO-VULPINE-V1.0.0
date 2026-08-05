/* mesh_token.c — Mesh-Token External Settlement implementation
 *
 * See mesh_token.h for design rationale. Uses Porter House for admission
 * control and Count House for valuation, following the same conventions
 * as count_house.c / porter_house.c: SR_* fixed-point, M5 coordinates,
 * inline coverage computation.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "mesh_token.h"
#include <string.h>

void mesh_token_init(mesh_token_t *mt, uint32_t device_id, const char *name,
                      porter_house_t *porter, count_house_t *count_house) {
    if (!mt) return;
    memset(mt, 0, sizeof(*mt));
    mt->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < MT_MAX_LABEL_LEN && name && name[i]; i++) {
        mt->name[i] = name[i];
    }
    mt->name[i] = '\0';

    mt->porter = porter;
    mt->count_house = count_house;
    mt->num_settlements = 0;
    mt->next_id = 1;

    mt->m5.omega = device_id;
    mt->m5.chi = device_id;
    mt->m5.phi = SR_ZERO;

    mesh_token_update_coverage(mt);
}

int32_t mesh_token_settle(mesh_token_t *mt, const word168_t *sender,
                           const word168_t *receiver, uint64_t amount,
                           uint32_t peer_trust_weight, uint64_t current_cycle) {
    if (!mt || !sender || !receiver || amount == 0) return -1;

    /* Find unused slot */
    uint32_t slot = MT_MAX_SETTLEMENTS;
    for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
        if (!mt->settlements[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= MT_MAX_SETTLEMENTS) return -1;

    /* Porter House admission check: does the receiver get past the door? */
    bool admitted = true;
    if (mt->porter) {
        admitted = porter_house_admit(mt->porter, MT_SETTLEMENT_PORT,
                                       receiver, peer_trust_weight);
    }

    mt_settlement_t *s = &mt->settlements[slot];
    memset(s, 0, sizeof(*s));
    s->id = mt->next_id++;
    s->sender_id = *sender;
    s->receiver_id = *receiver;
    s->amount = amount;
    s->trust_weight = peer_trust_weight;
    s->created_cycle = current_cycle;
    s->confirmed_cycle = 0;
    s->active = true;

    /* Get current valuation from Count House */
    if (mt->count_house) {
        s->valuation = count_house_valuation(mt->count_house);
    } else {
        s->valuation = SR_ZERO;
    }

    if (admitted) {
        s->state = MT_SETTLEMENT_ADMITTED;
    } else {
        s->state = MT_SETTLEMENT_REJECTED;
        mt->total_rejected++;
    }

    mt->num_settlements++;
    mesh_token_update_coverage(mt);
    return admitted ? (int32_t)s->id : -2;
}

int32_t mesh_token_ack(mesh_token_t *mt, uint32_t settlement_id, uint64_t current_cycle) {
    if (!mt) return -1;
    mt_settlement_t *s = mesh_token_get(mt, settlement_id);
    if (!s) return -1;

    if (s->state != MT_SETTLEMENT_IN_TRANSIT) return -1;

    s->state = MT_SETTLEMENT_CONFIRMED;
    s->confirmed_cycle = current_cycle;
    mt->total_confirmed++;
    mt->total_settled += s->amount;

    mesh_token_update_coverage(mt);
    return 0;
}

uint32_t mesh_token_check_timeouts(mesh_token_t *mt, uint64_t current_cycle) {
    if (!mt) return 0;
    uint32_t timed_out = 0;

    for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
        mt_settlement_t *s = &mt->settlements[i];
        if (!s->active) continue;
        if (s->state != MT_SETTLEMENT_IN_TRANSIT) continue;

        if (current_cycle - s->created_cycle >= MT_ACK_TIMEOUT_CYCLES) {
            s->state = MT_SETTLEMENT_TIMEOUT;
            mt->total_timeout++;
            timed_out++;
        }
    }

    if (timed_out > 0) mesh_token_update_coverage(mt);
    return timed_out;
}

int32_t mesh_token_advance(mesh_token_t *mt, uint64_t current_cycle) {
    if (!mt) return -1;
    (void)current_cycle;

    /* Find first ADMITTED settlement and transition to IN_TRANSIT */
    for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
        mt_settlement_t *s = &mt->settlements[i];
        if (!s->active) continue;
        if (s->state == MT_SETTLEMENT_ADMITTED) {
            s->state = MT_SETTLEMENT_IN_TRANSIT;
            mesh_token_update_coverage(mt);
            return 0;
        }
    }
    return -1;
}

mt_settlement_t *mesh_token_get(mesh_token_t *mt, uint32_t settlement_id) {
    if (!mt) return NULL;
    for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
        if (mt->settlements[i].active && mt->settlements[i].id == settlement_id) {
            return &mt->settlements[i];
        }
    }
    return NULL;
}

surplus_real_t mesh_token_update_coverage(mesh_token_t *mt) {
    if (!mt) return SR_ZERO;

    /* r: success rate = confirmed / (confirmed + rejected + timeout) */
    uint64_t total_resolved = mt->total_confirmed + mt->total_rejected + mt->total_timeout;
    if (total_resolved == 0) {
        mt->m5.r = SR_ONE; /* neutral: no settlements resolved yet */
    } else {
        mt->m5.r = SR_DIV(SR_FROM_INT((int64_t)mt->total_confirmed),
                           SR_FROM_INT((int64_t)total_resolved));
    }

    /* ell: fraction of active settlements that are in-progress
     * (ADMITTED or IN_TRANSIT) -- a settlement engine where nothing
     * is moving is not doing work. */
    uint32_t in_progress = 0;
    uint32_t total_active = 0;
    for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
        if (!mt->settlements[i].active) continue;
        total_active++;
        if (mt->settlements[i].state == MT_SETTLEMENT_ADMITTED ||
            mt->settlements[i].state == MT_SETTLEMENT_IN_TRANSIT) {
            in_progress++;
        }
    }
    mt->m5.ell = (total_active == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)in_progress), SR_FROM_INT((int64_t)total_active));

    /* Coverage hyperbola */
    surplus_real_t product = SR_MUL(mt->m5.r, mt->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    mt->coverage_ratio = SR_DIV(product, floor);

    return mt->coverage_ratio;
}
