/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vf_prize.h — the prize court: how a floating voucher (vfloat.h) comes into
 * being. Vouchers are never issued for simply existing or simply sending
 * bytes: each one is drawn against a letter of marque the bounty board
 * commissioned, after the delivered work ("the prize") passes an audit.
 *
 * The letter is the bounty board's record (pay_bounty, the Sicilian Royal
 * Navy board, PR #154). This court keeps only the fields it adjudicates on
 * (vfp_warrant_t) and asks the board, through a hook, whether the
 * commissioner really sealed them. It defines no letter format of its own.
 *
 *   P1 COMMISSION. vfp_commission enters a warrant only if the verify hook
 *      (bound to the board's ML-DSA check of the commissioner's seal)
 *      accepts it, the kind and resource are known, the scope is a raw CID,
 *      the cap is set (1..VF_CAP_LIMIT; there is no default: the
 *      commissioner sets it) and the id is new.
 *   P2 TRACEABLE. A claim, or a voucher arriving at a bearer, that names no
 *      warrant this court holds is uncommissioned (VFP_E_PIRACY), counted
 *      and refused. A revoked warrant clears nothing.
 *   P3 PRIZE. vfp_claim asks the audit hook (AXION's check of the payload
 *      and its lineage to the warrant's scope CID) for a four-valued
 *      verdict and the units earned. TRUE with units clears; FALSE condemns
 *      the prize (VFP_E_CONDEMNED, no credit); BOTH or NEITHER holds it
 *      (VFP_E_HELD, no credit now; the claimant may bring it again).
 *   P4 CAP. The units drawn against one warrant never exceed its cap. A
 *      claim that would pass the cap is refused whole; the draw that reaches
 *      the cap marks the warrant FULFILLED (decommissioned).
 *   P5 ONLY THROUGH A CLAIM. Bound as the voucher module's letter hook
 *      (vfp_bind), the court approves an issuing draw only for the claim it
 *      is adjudicating at that moment. A direct vf_issue is refused, so
 *      hydration on its own creates no credit.
 *   P6 MAGISTRATE. The node owner revokes a warrant locally (vfp_revoke).
 *
 * Kinds: RETRIEVAL (deliver the exact blocks of a missing disc layer or
 * booklet), AUDIT (a checked counterexample or proof against a booklet's
 * invariants), CARTOGRAPHY (map a dataset into canonical Alexandria
 * coordinates). The court treats them alike; the audit hook knows how each
 * prize is checked.
 *
 * HONEST LIMITS. The court is only as strict as its two hooks: the verify
 * hook decides whose seal counts, and the audit hook decides what a good
 * prize is. Neither is built here. A bearer cannot see what other bearers
 * drew against an open warrant, so its own check is per voucher (units
 * within the cap); the issuer's court enforces the cumulative cap. No
 * allocation; single-threaded.
 */
#ifndef ZXV_VF_PRIZE_H
#define ZXV_VF_PRIZE_H

#include "vfloat.h"
#include "../lpres/lpres.h"

#define VFP_MAX_WARRANTS 64u

#define VFP_E_PIRACY    (-20) /* no such warrant here */
#define VFP_E_CONDEMNED (-21) /* the audit said FALSE */
#define VFP_E_HELD      (-22) /* the audit said BOTH or NEITHER */
#define VFP_E_CLOSED    (-23) /* revoked, fulfilled or expired */
#define VFP_E_DUP       (-24) /* that warrant id is already held */

typedef enum { VFP_RETRIEVAL = 1, VFP_AUDIT = 2, VFP_CARTOGRAPHY = 3 } vfp_kind_t;
typedef enum { VFP_ACTIVE = 1, VFP_FULFILLED = 2, VFP_REVOKED = 3 } vfp_state_t;

/* The adjudicated fields of a letter of marque. bearer all 0xFF: open to
 * any peer. alexandria_x100: the Alexandria class times 100 (520.10 -> 52010). */
typedef struct {
    uint8_t id[32];           /* the board's letter id */
    uint8_t commissioner[32]; /* the commissioner's key id */
    vna_id_t bearer;
    uint8_t kind;     /* vfp_kind_t */
    uint8_t resource; /* vf_res_t the prize is credited in */
    uint32_t alexandria_x100;
    uint8_t scope[VNA_CID_RAW_LEN];
    uint64_t cap; /* units, set by the commissioner */
    uint32_t expiry_epoch;
} vfp_warrant_t;

typedef struct {
    bool used;
    uint8_t state; /* vfp_state_t */
    vfp_warrant_t w;
    uint64_t drawn;
    uint32_t cleared, condemned, held;
} vfp_entry_t;

/* The board's word that the commissioner sealed this warrant. */
typedef bool (*vfp_verify_fn)(void *ctx, const vfp_warrant_t *w, const uint8_t *seal,
                              uint32_t len);
/* AXION's verdict on a prize; on TRUE it sets *units (what the prize earns). */
typedef lpres_state_t (*vfp_audit_fn)(void *ctx, const vfp_warrant_t *w, const vna_id_t *claimant,
                                      const uint8_t *payload, uint32_t len, uint64_t *units);

typedef struct {
    vfp_entry_t e[VFP_MAX_WARRANTS];
    vfp_verify_fn verify;
    void *vctx;
    vfp_audit_fn audit;
    void *actx;
    vf_t *f; /* the voucher module this court is bound to */
    /* P5: the claim being adjudicated right now */
    bool pending;
    uint32_t pend_slot;
    vna_id_t pend_bearer;
    uint64_t pend_units;
    /* statistics */
    uint32_t piracy, condemned, held, cleared;
} vfp_court_t;

void vfp_init(vfp_court_t *c, vfp_verify_fn verify, void *vctx, vfp_audit_fn audit, void *actx);
/* P5: make this court the letter hook of f. */
void vfp_bind(vfp_court_t *c, vf_t *f);

/* P1. */
int32_t vfp_commission(vfp_court_t *c, const vfp_warrant_t *w, const uint8_t *seal, uint32_t len);
/* P6. */
int32_t vfp_revoke(vfp_court_t *c, const uint8_t id[32]);
const vfp_entry_t *vfp_get(const vfp_court_t *c, const uint8_t id[32]);

/* P2-P4: adjudicate a prize delivered by `peer` against warrant `id` and,
 * when it clears, issue the voucher (vf_issue) into out. *verdict (may be
 * NULL) gets the audit's verdict, or NEITHER when no audit ran. */
int32_t vfp_claim(vfp_court_t *c, uint32_t peer, const uint8_t id[32], const uint8_t *payload,
                  uint32_t len, uint32_t lifetime, const uint8_t *rnd,
                  uint8_t out[VF_VOUCHER_LEN], lpres_state_t *verdict);

/* The vf_letter_fn the court installs (ctx = the court). */
bool vfp_letter(void *ctx, const uint8_t letter[32], const vna_id_t *bearer, uint8_t resource,
                uint64_t units, bool take);

#endif /* ZXV_VF_PRIZE_H */
