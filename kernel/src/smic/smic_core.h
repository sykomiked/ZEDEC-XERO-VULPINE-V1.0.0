/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* smic_core.h — the SMIC provisioning registry and paraconsistent ring
 * admission. A verified manifest (smic_verify.h) is positive evidence a
 * node is what it says it is; probe reports join against that evidence
 * through lpres. The node's health rides a 6-bit lpx hexagram tag
 * (lpres_hex.h); admission answers with the table's own action:
 *
 *   admitted TRUE  + healthy        -> LPX_ACT_PROCEED  (in the ring)
 *   NEITHER        (not yet proven) -> LPX_ACT_WAIT     (pending)
 *   BOTH           (probes differ)  -> LPX_ACT_ESCALATE (quarantine)
 *   FALSE          (rejected/gone)  -> LPX_ACT_REFUSE   (isolated)
 *   forbidden lpx                   -> LPX_ACT_ROLLBACK (quarantine X6)
 *
 * The registry is a bounded caller-owned table — no allocation, no state
 * outside it. */
#ifndef SMIC_CORE_H
#define SMIC_CORE_H

#include "smic_manifest.h"
#include "../lpres/lpres.h"

typedef struct {
    uint8_t used;
    uint8_t quarantined; /* BOTH evidence — isolated pending review */
    uint8_t node_id[32];
    uint8_t region, role, octane, arch, flags;
    uint64_t caps, disk_mb, provisioned_at;
    uint16_t net_mbps;
    uint32_t fw_crc;
    uint32_t pos_ev, neg_ev;    /* probe receipts */
    lpx_t health;               /* the node's hexagram tag */
    lpres_state_t admitted;     /* four-valued ring admission */
    uint64_t last_seen;         /* ms tick of last verified evidence */
} smic_node_t;

typedef struct {
    smic_node_t *ents;
    uint32_t cap, n;
} smic_t;

void smic_init(smic_t *r, smic_node_t *ents, uint32_t cap);

/* A verified manifest is positive provisioning evidence: registers the
 * node (or refreshes its inventory), folds the evidence into its
 * admission, and answers the lpx action the ring should take.
 * `now` is the caller's ms tick. */
lpx_action_t smic_admit(smic_t *r, const smic_hw_t *hw, uint64_t now);

/* A probe receipt about a registered node: `ok` is the probe's verdict.
 * Joins against the admission record (lpres_kjoin across sources):
 * agreeing probes keep TRUE, a first contradiction lands BOTH
 * (quarantine), a node that was never verified and keeps failing goes
 * FALSE (isolated). Returns the action for the tag after the join. */
lpx_action_t smic_health(smic_t *r, const uint8_t node_id[32], uint8_t ok, uint64_t now);

/* Operator verdicts: forbid a node outright (quarantine, X6) or clear
 * one back to pending. */
lpx_action_t smic_forbid(smic_t *r, const uint8_t node_id[32]);
lpx_action_t smic_clear(smic_t *r, const uint8_t node_id[32]);

/* Time decay: a node with no verified evidence for stale_ms slides its
 * polarity entropic (evidence fades); admission stays as recorded —
 * re-admission needs a fresh attestation. Returns nodes touched. */
uint32_t smic_reap(smic_t *r, uint64_t now, uint64_t stale_ms);

const smic_node_t *smic_find(const smic_t *r, const uint8_t node_id[32]);

/* Ring summary: per-region count of PROCEED nodes (out must hold
 * SMIC_REGIONS) plus the admitted-TRUE total in *total (may be NULL). */
void smic_ring(const smic_t *r, uint8_t out[SMIC_REGIONS], uint32_t *total);

#endif /* SMIC_CORE_H */
