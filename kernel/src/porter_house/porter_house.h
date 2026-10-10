/* porter_house.h — The Porter House: port-seal / wall-interface firewall
 * (ZEDEC XERO VULPINE / ZXV)
 *
 * A "porter" is the doorman who decides who gets past the entrance --
 * here, that's a per-port admission policy sitting in front of the
 * existing network stack (kernel/src/net/net.c), deciding whether an
 * inbound 168-bit-identified peer gets seated (admitted) or shown the
 * door (rejected), before a single byte of payload is ever processed.
 * (The other half of the pun: this is where a connection gets "carded"
 * -- checked at the door -- same as walking into the actual restaurant.)
 *
 * Porter House does not replace net.c's transport; it is a decision
 * function net.c (or any future socket-accepting subsystem) calls
 * before accepting a connection on a sealed port. Keeping it decoupled
 * from Count House (rather than #include-ing count_house.h) is
 * deliberate: the caller looks up a peer's trust weight wherever it
 * keeps that data (Count House today, something else tomorrow) and
 * passes it in -- Porter House only knows about seals and peer IDs.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef PORTER_HOUSE_H
#define PORTER_HOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define PH_MAX_SEALS        32   /* distinct sealed ports per Porter House */
#define PH_MAX_ALLOWLIST    16   /* allowlisted peers per seal */
#define PH_MAX_LABEL_LEN     32

/* ===== Seal Modes ===== */

typedef enum {
    PH_SEAL_OPEN      = 0, /* unsealed: any peer admitted (net.c's current default behavior) */
    PH_SEAL_TRUSTED   = 1, /* admitted iff caller-supplied peer_trust_weight >= min_trust_weight */
    PH_SEAL_ALLOWLIST = 2, /* admitted iff peer_id is on this seal's allowlist */
    PH_SEAL_CLOSED    = 3, /* nobody admitted, regardless of trust or allowlist (maintenance/lockdown) */
} ph_seal_mode_t;

/* ===== Port Seal ===== */

typedef struct ph_port_seal {
    uint16_t port;
    ph_seal_mode_t mode;
    uint32_t min_trust_weight;              /* used when mode == PH_SEAL_TRUSTED (0-1000, Count House scale) */
    word168_t allowlist[PH_MAX_ALLOWLIST];  /* used when mode == PH_SEAL_ALLOWLIST */
    uint32_t allowlist_count;
    bool active;

    /* Per-seal admission stats (see porter_house_admit) */
    uint64_t admitted_count;
    uint64_t rejected_count;
} ph_port_seal_t;

/* ===== Porter House (hardware-as-code device) ===== */

typedef struct porter_house {
    uint32_t device_id;
    char name[PH_MAX_LABEL_LEN];

    ph_port_seal_t seals[PH_MAX_SEALS];
    uint32_t num_seals;

    /* System-wide admission stats */
    uint64_t total_admitted;
    uint64_t total_rejected;

    /* M5 coordinates (kernel-wide convention): r = admit rate,
     * ell = fraction of ports NOT left PH_SEAL_OPEN (a fully-open
     * Porter House is a Porter House doing no work -- ell tracks
     * "how much of the front door is actually staffed"). */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} porter_house_t;

/* ===== API ===== */

void porter_house_init(porter_house_t *ph, uint32_t device_id, const char *name);

/* Seal (or reconfigure) a port with the given mode. Creates the seal
 * if `port` isn't already sealed by this Porter House. Returns the
 * seal index, or -1 if PH_MAX_SEALS is exhausted. */
int32_t porter_house_seal_port(porter_house_t *ph, uint16_t port,
                                ph_seal_mode_t mode, uint32_t min_trust_weight);

/* Fully unseal a port: mode -> PH_SEAL_OPEN. Does not remove the seal
 * record (stats/allowlist are preserved in case it's re-sealed later). */
void porter_house_open_port(porter_house_t *ph, uint16_t port);

/* Slam the door: mode -> PH_SEAL_CLOSED regardless of prior mode.
 * Returns 0 when the port is now closed, -1 when it could not be (NULL
 * ph, or the seal table is full and no OPEN seal can be reused). */
int32_t porter_house_close_port(porter_house_t *ph, uint16_t port);

int32_t porter_house_find_seal(porter_house_t *ph, uint16_t port);

/* Add a peer to a PH_SEAL_ALLOWLIST seal's guest list. Returns 0 on
 * success, -1 if the seal doesn't exist, -2 if the allowlist is full. */
int32_t porter_house_allowlist_add(porter_house_t *ph, uint16_t port,
                                    const word168_t *peer_id);

/* The core decision: does this peer get past the door on this port?
 * peer_trust_weight is caller-supplied (e.g. from a Count House Stash
 * Bucket's peer_trust_weight, 0-1000) -- Porter House has no opinion
 * on where trust comes from, only on what to do with it. An unsealed
 * port (never passed to porter_house_seal_port) behaves exactly like
 * PH_SEAL_OPEN: always admitted, matching net.c's pre-Porter-House
 * default so adding Porter House to an existing port is opt-in. */
bool porter_house_admit(porter_house_t *ph, uint16_t port,
                         const word168_t *peer_id, uint32_t peer_trust_weight);

/* Recompute M5 coverage from current seal configuration + admission
 * stats. Returns coverage_ratio. */
surplus_real_t porter_house_update_coverage(porter_house_t *ph);

const char *ph_seal_mode_name(ph_seal_mode_t mode);

#endif /* PORTER_HOUSE_H */
