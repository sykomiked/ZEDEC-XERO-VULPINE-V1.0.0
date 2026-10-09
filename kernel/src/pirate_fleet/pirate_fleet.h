/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pirate_fleet.h — the Jolly Dragon Rogers Pirate Fleet.
 *
 * "The Jolly Dragon Roger flies over a crew that flags OUT of the port, not
 *  INTO it. Office is ministry; the ledger is sovereign; everybody eats."
 *
 * WHAT THIS IS (four rails, one deck)
 * -----------------------------------
 *   1. CHANNELS   — a collaborative crew workspace: advanced IRC-style channels
 *                   carried over the distributed transport. On-device we anchor
 *                   the posts locally; the wire is an ops boundary.
 *   2. CHARTER    — a NEW DAO type: a flag-state crew charter. Unlike a company
 *                   that registers INTO a jurisdiction, a Jolly Dragon crew flags
 *                   OUT of the Fleet port. It is a LOCAL cryptographic membership
 *                   token (Ed25519 founders) — never real-world legal personality
 *                   or diplomatic standing. A crew-state FSM gates credential and
 *                   treasury steps behind separate capabilities, with an
 *                   anti-capture wall that forbids one actor holding both.
 *   3. COMPUTE    — register remote compute as a STEWARDSHIP DEED (office is
 *                   ministry). Officers certify a node by >=2/3 Ed25519 quorum;
 *                   REAL metered work then accrues Vino through the settlement
 *                   engine. Simulated work earns NOTHING, by construction.
 *   4. MATCHING   — ISF-sponsored stranger-matching (wraps concord's con_recommend,
 *                   pure Interaction-Surplus, budget-bounded, non-coercive) OR
 *                   joining an existing crew.
 *
 * We REUSE the real subsystems and never re-implement them:
 *   concord      — the non-coercive ISF matcher (no engagement/ranking term)
 *   vino_stores  — the Vino settlement engine (coverage >= 1.8x, no debt)
 *   ed25519      — asymmetric charter/quorum signatures (kernel verify only)
 *   chiglet      — chg_interaction, a companion measure in the channel
 *
 * Freestanding: integer-only surplus_real_t, fixed-size arrays, no libc, no
 * allocation, no floating point on target, phase-tick ordering (never wall-clock).
 */
#ifndef ZXV_PIRATE_FLEET_H
#define ZXV_PIRATE_FLEET_H

#include <stdint.h>
#include <stdbool.h>
#include "concord.h"        /* con_commons_t, con_match_t, con_recommend       */
#include "chiglet.h"        /* chg_interaction — the channel companion measure */
#include "vino_stores.h"    /* vino_stores_t, vino_mint, vino_ledger_act        */
#include "ed25519_verify.h" /* ed25519_verify — real asymmetric quorum          */

/* ===== capacities (all statically sized — no allocation aboard) ===== */
#define PF_MAX_CREWS      16u
#define PF_MAX_NODES      32u
#define PF_MAX_FOUNDERS    8u
#define PF_MAX_OFFICERS    8u
#define PF_PUBKEY_LEN     32u
#define PF_SIG_LEN        64u
#define PF_ATT_LEN        32u   /* telemetry attestation digest (a SHA-256)      */
#define PF_MSG_MAX       128u   /* bytes of a post we anchor locally             */
#define PF_CHAN_RING      32u   /* recent posts kept on-device                   */

/* ===== capability bits (the anti-capture pair) =====
 * Credential authority and treasury authority are SEPARATED on purpose. The
 * wall (jdr_cap_wall_ok) forbids one actor from holding BOTH at once, so
 * nobody can both mint membership and audit the money — the classic capture. */
#define PF_CAP_CREDENTIAL      (1u << 0)   /* may take credential steps          */
#define PF_CAP_TREASURY_AUDIT  (1u << 1)   /* may take financial/treasury steps  */

/* ===== result codes (fail closed, always typed — never a fabricated result) */
#define PF_OK               0
#define PF_ERR_NULL        (-1)
#define PF_ERR_CAPACITY    (-2)   /* a fixed table is full                       */
#define PF_ERR_NOT_FOUND   (-3)   /* no such crew/node                           */
#define PF_ERR_DENIED      (-4)   /* cap wall / missing capability               */
#define PF_ERR_STATE       (-5)   /* illegal FSM transition                      */
#define PF_ERR_ARG         (-6)   /* bad argument                                */
#define PF_ERR_EXISTS      (-7)   /* crew/node id already registered             */
#define PF_ERR_QUORUM      (-8)   /* certification below 2/3 officer quorum      */
#define PF_ERR_UNCERTIFIED (-9)   /* accrue attempted before certification       */
#define PF_ERR_NO_TELEMETRY (-10) /* SIMULATED/unverified work — Vino stays ZERO */
#define PF_ERR_NO_WORK     (-11)  /* metered_work <= 0 — nothing earned          */
#define PF_ERR_NO_BACKEND  (-12)  /* settlement engine unbound — fail closed     */

/* ===== the crew-state FSM =====
 * APPLIED --(credential)--> CREDENTIALED --(treasury)--> TREASURY_OPEN --> ACTIVE
 * Any state may DISBAND. The credential edge needs PF_CAP_CREDENTIAL; the
 * treasury edge needs PF_CAP_TREASURY_AUDIT. */
typedef enum {
    CREW_NONE          = 0,
    CREW_APPLIED       = 1,   /* charter registered OUT of the port              */
    CREW_CREDENTIALED  = 2,   /* founders' credentials seated (CAP_CREDENTIAL)   */
    CREW_TREASURY_OPEN = 3,   /* treasury opened & audited (CAP_TREASURY_AUDIT)  */
    CREW_ACTIVE        = 4,   /* sailing                                          */
    CREW_DISBANDED     = 5    /* struck the colours                              */
} crew_state_t;

/* ===== a flag-state crew charter (the DAO) ===== */
typedef struct {
    uint32_t     crew_id;
    uint8_t      founder[PF_MAX_FOUNDERS][PF_PUBKEY_LEN];  /* Ed25519 founders   */
    uint32_t     n_founders;
    uint8_t      rights_flags;    /* crew-defined rights bitmap (the 5th field)  */
    crew_state_t state;
    bool         flagged_out;     /* ALWAYS true: a crew flags OUT, never IN     */
    bool         in_use;
} pf_crew_t;

/* ===== a stewardship deed — remote compute registered as ministry =====
 * The attestation is the telemetry witness the officers sign. It is an OPS
 * BOUNDARY: SUPPLIED by the metering harness, never invented here. telemetry_real
 * is the honesty bit — false means the work was SIMULATED and no Vino may accrue. */
typedef struct {
    uint32_t crew_id;
    uint8_t  officer_pubkey[PF_MAX_OFFICERS][PF_PUBKEY_LEN];
    uint32_t n_officers;
    uint8_t  attestation[PF_ATT_LEN];  /* the signed telemetry digest (supplied) */
    bool     telemetry_real;           /* false => simulated => zero accrual      */
} stewardship_deed_t;

typedef struct {
    uint32_t           node_id;
    stewardship_deed_t deed;
    bool               certified;      /* set only by a passed >=2/3 quorum       */
    uint64_t           vino_voucher_id;/* 0 until a Vino voucher is minted for it */
    bool               in_use;
} pf_node_t;

/* one locally-anchored channel post (bounded — no arbitrary storage) */
typedef struct {
    uint32_t crew_id;
    uint32_t len;
    uint32_t phase;                    /* phase-tick, never wall-clock            */
    uint8_t  bytes[PF_MSG_MAX];
} pf_post_t;

/* ===== the fleet ===== */
typedef struct {
    pf_crew_t      crew[PF_MAX_CREWS];
    uint32_t       n_crews;

    pf_node_t      node[PF_MAX_NODES];
    uint32_t       n_nodes;
    uint32_t       next_node_id;

    pf_post_t      ring[PF_CHAN_RING];
    uint32_t       ring_head;          /* next write slot                         */
    uint32_t       ring_count;         /* posts held (saturates at PF_CHAN_RING)  */

    /* Bound subsystems — both are OPS BOUNDARIES. When absent we fail closed:
     * no matches offered, no Vino credited. Never fabricated. */
    con_commons_t *commons;            /* the ISF matcher's population            */
    vino_stores_t *vino;               /* the settlement engine                   */

    uint32_t       phase;              /* the fleet's phase-tick clock            */
} pf_fleet_t;

/* ===== lifecycle ===== */
/* Bind the fleet to the shared ISF commons and the Vino settlement engine.
 * Either may be NULL — the corresponding rail then fails closed. */
void pf_init(pf_fleet_t *f, con_commons_t *commons, vino_stores_t *vino);

/* ===== the charter (a new DAO type) ===== */

/* Register a crew charter — the five-field registration
 * (crew_id, founders, n, rights_flags, state:=APPLIED). The crew flags OUT of
 * the Fleet port. This is a LOCAL cryptographic membership token ONLY — it
 * confers NO legal personality and NO diplomatic standing. Returns PF_OK. */
int32_t jdr_crew_charter(pf_fleet_t *f, uint32_t crew_id,
                         const uint8_t founders[][PF_PUBKEY_LEN], uint32_t n,
                         uint8_t rights_flags);

/* Drive the crew FSM. The credential edge requires PF_CAP_CREDENTIAL; the
 * treasury edge requires PF_CAP_TREASURY_AUDIT. Any actor holding BOTH caps is
 * refused outright (the anti-capture wall). Returns PF_OK or a typed error. */
int32_t jdr_crew_transition(pf_fleet_t *f, uint32_t crew_id, crew_state_t to,
                            uint8_t actor_caps);

/* The anti-capture wall. REJECTS (returns false) any actor holding BOTH
 * PF_CAP_CREDENTIAL and PF_CAP_TREASURY_AUDIT at once; ALLOWS either one alone
 * (or neither). Separation of the mint from the money. */
bool jdr_cap_wall_ok(uint8_t actor_caps);

/* ===== ISF stranger-matching ===== */

/* Offer introductions for `self`, drawn PURELY from concord's Interaction-
 * Surplus ranking — no engagement/recency term is added here or there. Bounded
 * by the person's sociability budget (con_budget = sociability*16/255). Returns
 * the count written, 0 if the commons is unbound. A thin, honest wrapper. */
uint32_t jdr_match_strangers(pf_fleet_t *f, uint32_t self,
                             con_match_t *out, uint32_t max);

/* ===== compute-for-Vino rail ===== */

/* Register a remote compute node from a stewardship deed. The deed's telemetry
 * attestation is SUPPLIED (ops boundary). Returns the new node_id (>0) or a
 * typed error (<0). */
int32_t jdr_node_register(pf_fleet_t *f, uint32_t crew_id, stewardship_deed_t *deed);

/* Certify a node by officer quorum. Each officer_sigs[i] is verified with REAL
 * Ed25519 against officer_pubkey[i] over the deed's attestation. Certification
 * passes iff valid_signers*3 >= n_officers*2 (a >=2/3 quorum). Returns PF_OK on
 * a passed quorum, PF_ERR_QUORUM below it — never certifies on a short count. */
int32_t jdr_node_certify(pf_fleet_t *f, uint32_t node_id,
                         const uint8_t officer_sigs[][PF_SIG_LEN], uint32_t n);

/* Accrue Vino for VERIFIED metered work through the settlement engine's triple
 * rail. Credits ONLY when: the node is certified, the deed carries REAL
 * telemetry (not simulated), metered_work > 0, and the Vino engine is bound.
 * Otherwise it accrues ZERO and returns a typed not-available code. Never
 * credits fictional/simulated work. */
int32_t jdr_accrue_vino(pf_fleet_t *f, uint32_t node_id, surplus_real_t metered_work);

/* ===== channels ===== */

/* Post to a crew's channel. The bytes are anchored locally in a bounded ring
 * (the distributed wire is an ops boundary). Requires an existing, non-disbanded
 * crew. Returns PF_OK or a typed error. */
int32_t jdr_channel_post(pf_fleet_t *f, uint32_t crew_id,
                         const uint8_t *msg, uint32_t len);

/* The Chiglet companion's read on two channel participants: the ISF interaction
 * u = 1-(a.b)^2/(|a|^2|b|^2) between their interest vectors — 1 complementary,
 * 0 redundant/opposed. Reuses chg_interaction; fabricates nothing. */
surplus_real_t jdr_channel_affinity(const surplus_real_t a[CHG_DIM],
                                    const surplus_real_t b[CHG_DIM]);

/* Number of posts currently anchored on-device (saturates at PF_CHAN_RING). */
uint32_t jdr_channel_depth(const pf_fleet_t *f);

const char *jdr_crew_state_name(crew_state_t s);

#endif /* ZXV_PIRATE_FLEET_H */
