/* porter_house.c — The Porter House implementation
 *
 * See porter_house.h for design rationale. Follows the same
 * conventions as count_house.c / dlp_projector.c: SR_* fixed-point
 * macros only, coverage computed inline (no edp_risk.c linkage needed).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "porter_house.h"
#include <string.h>

static const char *seal_mode_names[] = { "OPEN", "TRUSTED", "ALLOWLIST", "CLOSED" };

const char *ph_seal_mode_name(ph_seal_mode_t mode) {
    if ((uint32_t)mode < sizeof(seal_mode_names) / sizeof(seal_mode_names[0])) {
        return seal_mode_names[mode];
    }
    return "UNKNOWN";
}

void porter_house_init(porter_house_t *ph, uint32_t device_id, const char *name) {
    if (!ph) return;
    memset(ph, 0, sizeof(*ph));
    ph->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < PH_MAX_LABEL_LEN && name && name[i]; i++) {
        ph->name[i] = name[i];
    }
    ph->name[i] = '\0';

    ph->num_seals = 0;
    ph->total_admitted = 0;
    ph->total_rejected = 0;

    ph->m5.omega = device_id;
    ph->m5.chi = device_id;
    ph->m5.phi = SR_ZERO;

    porter_house_update_coverage(ph);
}

int32_t porter_house_find_seal(porter_house_t *ph, uint16_t port) {
    if (!ph) return -1;
    for (uint32_t i = 0; i < ph->num_seals; i++) {
        if (ph->seals[i].active && ph->seals[i].port == port) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t porter_house_seal_port(porter_house_t *ph, uint16_t port,
                                ph_seal_mode_t mode, uint32_t min_trust_weight) {
    if (!ph) return -1;

    int32_t idx = porter_house_find_seal(ph, port);
    if (idx >= 0) {
        /* Reconfigure in place -- keep the allowlist and admission
         * history, since re-sealing a port (e.g. OPEN -> TRUSTED) is
         * a policy change, not a fresh port. */
        ph->seals[idx].mode = mode;
        ph->seals[idx].min_trust_weight = min_trust_weight;
        return idx;
    }

    if (ph->num_seals >= PH_MAX_SEALS) return -1;

    idx = (int32_t)ph->num_seals++;
    ph_port_seal_t *seal = &ph->seals[idx];
    seal->port = port;
    seal->mode = mode;
    seal->min_trust_weight = min_trust_weight;
    seal->allowlist_count = 0;
    seal->active = true;
    seal->admitted_count = 0;
    seal->rejected_count = 0;

    return idx;
}

void porter_house_open_port(porter_house_t *ph, uint16_t port) {
    if (!ph) return;
    int32_t idx = porter_house_find_seal(ph, port);
    if (idx < 0) return; /* never sealed -> already behaves as open, nothing to do */
    ph->seals[idx].mode = PH_SEAL_OPEN;
}

int32_t porter_house_close_port(porter_house_t *ph, uint16_t port)
{
    if (!ph) return -1;
    int32_t idx = porter_house_find_seal(ph, port);
    if (idx < 0) {
        /* Unlike open_port, a lockdown request must always take
         * effect even on a port nobody thought to pre-seal -- silently
         * doing nothing here would be a dangerous silent failure for
         * what is, by definition, a security-critical call. */
        idx = porter_house_seal_port(ph, port, PH_SEAL_CLOSED, 0);
        if (idx >= 0) return 0;
        /* Seal table full. An OPEN seal admits exactly what an unsealed
         * port admits, so its slot can be reused for the lockdown without
         * changing any other port's policy (its stats are dropped). */
        for (uint32_t i = 0; i < ph->num_seals; i++) {
            ph_port_seal_t *s = &ph->seals[i];
            if (s->active && s->mode == PH_SEAL_OPEN) {
                s->port = port;
                s->mode = PH_SEAL_CLOSED;
                s->min_trust_weight = 0;
                s->allowlist_count = 0;
                s->admitted_count = 0;
                s->rejected_count = 0;
                return 0;
            }
        }
        return -1; /* every slot already gates a port: caller must act */
    }
    ph->seals[idx].mode = PH_SEAL_CLOSED;
    return 0;
}

int32_t porter_house_allowlist_add(porter_house_t *ph, uint16_t port,
                                    const word168_t *peer_id) {
    if (!ph || !peer_id) return -1;
    int32_t idx = porter_house_find_seal(ph, port);
    if (idx < 0) return -1;

    ph_port_seal_t *seal = &ph->seals[idx];

    /* Idempotent: adding an already-listed peer is a no-op success,
     * not a duplicate entry. */
    for (uint32_t i = 0; i < seal->allowlist_count; i++) {
        if (memcmp(seal->allowlist[i].bytes, peer_id->bytes, WORD168_OCTETS) == 0) {
            return 0;
        }
    }

    if (seal->allowlist_count >= PH_MAX_ALLOWLIST) return -2;
    seal->allowlist[seal->allowlist_count++] = *peer_id;
    return 0;
}

bool porter_house_admit(porter_house_t *ph, uint16_t port,
                         const word168_t *peer_id, uint32_t peer_trust_weight) {
    if (!ph) return false;

    int32_t idx = porter_house_find_seal(ph, port);
    if (idx < 0) {
        /* Unsealed port: matches net.c's pre-Porter-House default so
         * adopting Porter House on a given port is strictly opt-in. */
        ph->total_admitted++;
        return true;
    }

    ph_port_seal_t *seal = &ph->seals[idx];
    bool admit;

    switch (seal->mode) {
        case PH_SEAL_OPEN:
            admit = true;
            break;
        case PH_SEAL_CLOSED:
            admit = false;
            break;
        case PH_SEAL_TRUSTED:
            admit = (peer_trust_weight >= seal->min_trust_weight);
            break;
        case PH_SEAL_ALLOWLIST:
            admit = false;
            if (peer_id) {
                for (uint32_t i = 0; i < seal->allowlist_count; i++) {
                    if (memcmp(seal->allowlist[i].bytes, peer_id->bytes, WORD168_OCTETS) == 0) {
                        admit = true;
                        break;
                    }
                }
            }
            break;
        default:
            admit = false;
            break;
    }

    if (admit) {
        seal->admitted_count++;
        ph->total_admitted++;
    } else {
        seal->rejected_count++;
        ph->total_rejected++;
    }

    return admit;
}

surplus_real_t porter_house_update_coverage(porter_house_t *ph) {
    if (!ph) return SR_ZERO;

    uint64_t total_attempts = ph->total_admitted + ph->total_rejected;
    /* r: admit rate. With zero attempts there's nothing to judge yet --
     * report a neutral fully-passing rate rather than an undefined 0/0. */
    ph->m5.r = (total_attempts == 0)
        ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)ph->total_admitted), SR_FROM_INT((int64_t)total_attempts));

    /* ell: fraction of configured seals actually doing gatekeeping
     * (mode != OPEN) -- a Porter House whose every seal is left OPEN
     * is a doorman who never checks anyone, i.e. not really staffed. */
    uint32_t staffed = 0;
    for (uint32_t i = 0; i < ph->num_seals; i++) {
        if (ph->seals[i].active && ph->seals[i].mode != PH_SEAL_OPEN) staffed++;
    }
    ph->m5.ell = (ph->num_seals == 0) ? SR_ZERO : SR_DIV(SR_FROM_INT(staffed), SR_FROM_INT((int64_t)ph->num_seals));

    surplus_real_t product = SR_MUL(ph->m5.r, ph->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    ph->coverage_ratio = SR_DIV(product, floor);

    return ph->coverage_ratio;
}
