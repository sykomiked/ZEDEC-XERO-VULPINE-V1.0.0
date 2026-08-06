/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* pirate_fleet.c — the Jolly Dragon Rogers Pirate Fleet. See pirate_fleet.h.
 *
 * Freestanding: no libc. The tiny byte helpers below keep us honest about that
 * (a for-loop is a fine memcpy when you own the deck). */
#include "pirate_fleet.h"

/* ---- freestanding byte helpers (no <string.h> aboard) ---- */
static void pf_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}
static void pf_zero(uint8_t *dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = 0;
}
static bool pf_att_is_zero(const uint8_t *a, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i]) return false;
    return true;
}

/* ---- lookups ---- */
static pf_crew_t *pf_find_crew(pf_fleet_t *f, uint32_t crew_id) {
    for (uint32_t i = 0; i < PF_MAX_CREWS; i++)
        if (f->crew[i].in_use && f->crew[i].crew_id == crew_id) return &f->crew[i];
    return 0;
}
static pf_node_t *pf_find_node(pf_fleet_t *f, uint32_t node_id) {
    for (uint32_t i = 0; i < PF_MAX_NODES; i++)
        if (f->node[i].in_use && f->node[i].node_id == node_id) return &f->node[i];
    return 0;
}

/* ============================ lifecycle ============================ */
void pf_init(pf_fleet_t *f, con_commons_t *commons, vino_stores_t *vino) {
    if (!f) return;
    for (uint32_t i = 0; i < PF_MAX_CREWS; i++) f->crew[i].in_use = false;
    for (uint32_t i = 0; i < PF_MAX_NODES; i++) f->node[i].in_use = false;
    f->n_crews = 0;
    f->n_nodes = 0;
    f->next_node_id = 1;           /* node ids are 1-based; 0 means "none"       */
    f->ring_head = 0;
    f->ring_count = 0;
    f->commons = commons;          /* either may be NULL — that rail fails closed */
    f->vino = vino;
    f->phase = 0;
}

/* ============================ the charter ============================ */
int32_t jdr_crew_charter(pf_fleet_t *f, uint32_t crew_id,
                         const uint8_t founders[][PF_PUBKEY_LEN], uint32_t n,
                         uint8_t rights_flags) {
    if (!f || !founders) return PF_ERR_NULL;
    if (n == 0 || n > PF_MAX_FOUNDERS) return PF_ERR_ARG;
    if (pf_find_crew(f, crew_id)) return PF_ERR_EXISTS;

    for (uint32_t i = 0; i < PF_MAX_CREWS; i++) {
        if (f->crew[i].in_use) continue;
        pf_crew_t *cr = &f->crew[i];
        /* the five fields of the registration */
        cr->crew_id      = crew_id;                       /* (1) */
        for (uint32_t k = 0; k < n; k++)                  /* (2) */
            pf_copy(cr->founder[k], founders[k], PF_PUBKEY_LEN);
        cr->n_founders   = n;                             /* (3) */
        cr->rights_flags = rights_flags;                  /* (4) */
        cr->state        = CREW_APPLIED;                  /* (5) */
        /* A crew flags OUT of the Fleet port, never INTO it. This is a LOCAL
         * cryptographic membership token — not legal personality, not a state. */
        cr->flagged_out  = true;
        cr->in_use       = true;
        f->n_crews++;
        return PF_OK;
    }
    return PF_ERR_CAPACITY;
}

bool jdr_cap_wall_ok(uint8_t actor_caps) {
    /* The whole point of the wall: whoever mints membership must NOT also audit
     * the money. Holding both is capture, so it is refused before anything else. */
    bool has_cred     = (actor_caps & PF_CAP_CREDENTIAL) != 0;
    bool has_treasury = (actor_caps & PF_CAP_TREASURY_AUDIT) != 0;
    return !(has_cred && has_treasury);
}

/* Is `to` a legal successor of `from`? Forward one rung at a time, or DISBAND. */
static bool pf_edge_ok(crew_state_t from, crew_state_t to) {
    if (to == CREW_DISBANDED) return from != CREW_DISBANDED;
    switch (from) {
    case CREW_APPLIED:       return to == CREW_CREDENTIALED;
    case CREW_CREDENTIALED:  return to == CREW_TREASURY_OPEN;
    case CREW_TREASURY_OPEN: return to == CREW_ACTIVE;
    default:                 return false;
    }
}

int32_t jdr_crew_transition(pf_fleet_t *f, uint32_t crew_id, crew_state_t to,
                            uint8_t actor_caps) {
    if (!f) return PF_ERR_NULL;
    /* Anti-capture wall first: an actor with BOTH caps may take NO step. */
    if (!jdr_cap_wall_ok(actor_caps)) return PF_ERR_DENIED;

    pf_crew_t *cr = pf_find_crew(f, crew_id);
    if (!cr) return PF_ERR_NOT_FOUND;
    if (!pf_edge_ok(cr->state, to)) return PF_ERR_STATE;

    /* Per-edge capability gate: the credential step needs CAP_CREDENTIAL, the
     * treasury step needs CAP_TREASURY_AUDIT. (Disband needs neither.) */
    if (to == CREW_CREDENTIALED && !(actor_caps & PF_CAP_CREDENTIAL))
        return PF_ERR_DENIED;
    if (to == CREW_TREASURY_OPEN && !(actor_caps & PF_CAP_TREASURY_AUDIT))
        return PF_ERR_DENIED;

    cr->state = to;
    f->phase++;                    /* phase-tick advances on every fleet event   */
    return PF_OK;
}

/* ======================= ISF stranger-matching ======================= */
uint32_t jdr_match_strangers(pf_fleet_t *f, uint32_t self,
                             con_match_t *out, uint32_t max) {
    if (!f || !out || !max) return 0;
    if (!f->commons) return 0;     /* matcher unbound => offer nothing (honest)  */
    /* Pure pass-through to the real ISF ranker. We add NO term of our own — not
     * recency, not retention, not "engagement". The budget bound and the surplus
     * ordering live in concord and stay there. */
    return con_recommend(f->commons, self, out, max);
}

/* ===================== compute-for-Vino rail ===================== */
int32_t jdr_node_register(pf_fleet_t *f, uint32_t crew_id, stewardship_deed_t *deed) {
    if (!f || !deed) return PF_ERR_NULL;
    if (deed->n_officers == 0 || deed->n_officers > PF_MAX_OFFICERS)
        return PF_ERR_ARG;
    if (!pf_find_crew(f, crew_id)) return PF_ERR_NOT_FOUND;

    /* The >=2/3 quorum only means anything if the officers are DISTINCT. Reject a
     * deed with any duplicate officer pubkey — otherwise a lone actor could list
     * the same key in several slots and replay ONE genuine signature across them,
     * self-certifying the node and drawing Vino for work no real quorum witnessed. */
    for (uint32_t a = 0; a < deed->n_officers; a++)
        for (uint32_t b = a + 1; b < deed->n_officers; b++) {
            bool same = true;
            for (uint32_t k = 0; k < 32; k++)
                if (deed->officer_pubkey[a][k] != deed->officer_pubkey[b][k]) { same = false; break; }
            if (same) return PF_ERR_ARG;   /* duplicate officer — no fake quorum */
        }

    for (uint32_t i = 0; i < PF_MAX_NODES; i++) {
        if (f->node[i].in_use) continue;
        pf_node_t *nd = &f->node[i];
        nd->node_id = f->next_node_id++;
        nd->deed = *deed;
        nd->deed.crew_id = crew_id;     /* bind the deed to the registering crew */
        nd->certified = false;
        nd->vino_voucher_id = 0;
        nd->in_use = true;
        f->n_nodes++;
        f->phase++;
        return (int32_t)nd->node_id;    /* >0 on success                         */
    }
    return PF_ERR_CAPACITY;
}

int32_t jdr_node_certify(pf_fleet_t *f, uint32_t node_id,
                         const uint8_t officer_sigs[][PF_SIG_LEN], uint32_t n) {
    if (!f || !officer_sigs) return PF_ERR_NULL;
    pf_node_t *nd = pf_find_node(f, node_id);
    if (!nd) return PF_ERR_NOT_FOUND;

    uint32_t officers = nd->deed.n_officers;
    if (officers == 0) return PF_ERR_ARG;

    /* Count DISTINCT officers whose provided signature verifies with REAL
     * Ed25519 over the deed attestation. We never certify on a fabricated or
     * short count — the signature check is genuine asymmetric verification. */
    uint32_t checked = (n < officers) ? n : officers;
    uint32_t valid = 0;
    for (uint32_t i = 0; i < checked; i++) {
        if (ed25519_verify(nd->deed.attestation, PF_ATT_LEN,
                           officer_sigs[i], nd->deed.officer_pubkey[i]))
            valid++;
    }

    /* >= 2/3 quorum, expressed in integers to avoid any rounding: a valid
     * fraction v/officers >= 2/3  <=>  v*3 >= officers*2. */
    if (valid * 3u >= officers * 2u) {
        nd->certified = true;
        f->phase++;
        return PF_OK;
    }
    return PF_ERR_QUORUM;           /* short of quorum — stays UNcertified        */
}

int32_t jdr_accrue_vino(pf_fleet_t *f, uint32_t node_id, surplus_real_t metered_work) {
    if (!f) return PF_ERR_NULL;
    pf_node_t *nd = pf_find_node(f, node_id);
    if (!nd) return PF_ERR_NOT_FOUND;

    /* Fail-closed gates, in order. Each returns a TYPED not-available code and
     * credits exactly nothing — no fabricated accrual ever reaches the ledger. */
    if (!nd->certified) return PF_ERR_UNCERTIFIED;
    /* The honesty bit: "connect remote compute to earn Vino" needs REAL work
     * metering. A simulated deed (or an empty attestation) earns ZERO. */
    if (!nd->deed.telemetry_real || pf_att_is_zero(nd->deed.attestation, PF_ATT_LEN))
        return PF_ERR_NO_TELEMETRY;
    if (SR_CMP(metered_work, SR_ZERO) <= 0) return PF_ERR_NO_WORK;
    if (!f->vino) return PF_ERR_NO_BACKEND;   /* settlement unbound — fail closed */

    /* Mint the node's Vino voucher on first accrual. The proof_cid is the deed's
     * telemetry attestation — a SUPPLIED, content-addressed witness (ops
     * boundary), not something we invent. */
    if (nd->vino_voucher_id == 0) {
        uint64_t vid = 0;
        int32_t mrc = vino_mint(f->vino, &vid, SR_FROM_INT(89), SECOND_MINTS_OIL,
                                VINO_ACTIVE_DIGITAL, nd->deed.attestation);
        if (mrc != VINO_OK) return PF_ERR_NO_BACKEND;
        nd->vino_voucher_id = vid;
    }

    /* Post the triple rail for verified work `w`:
     *   debit  = 2w  (the backing the work produced)
     *   credit =  w  (the claim raised against it)
     *   equity =  w  == debit - credit, so NO interest is implied (usury veto
     *                  in vino_ledger_act would refuse equity > debit-credit).
     * This yields ledger coverage 2.0 >= 1.8x and equity >= 0, so the whole
     * three-rail event commits atomically. Money is equity, never debt. */
    surplus_real_t debit  = SR_MUL(metered_work, SR_FROM_INT(2));
    surplus_real_t credit = metered_work;
    surplus_real_t equity = metered_work;
    int32_t rc = vino_ledger_act(f->vino, nd->vino_voucher_id, VINO_CIRCULATE,
                                 debit, credit, equity, nd->deed.attestation);
    if (rc != VINO_OK) return rc;   /* propagate the settlement engine's verdict  */
    f->phase++;
    return PF_OK;
}

/* ============================ channels ============================ */
int32_t jdr_channel_post(pf_fleet_t *f, uint32_t crew_id,
                         const uint8_t *msg, uint32_t len) {
    if (!f || !msg) return PF_ERR_NULL;
    if (len == 0 || len > PF_MSG_MAX) return PF_ERR_ARG;
    pf_crew_t *cr = pf_find_crew(f, crew_id);
    if (!cr) return PF_ERR_NOT_FOUND;
    if (cr->state == CREW_NONE || cr->state == CREW_DISBANDED) return PF_ERR_STATE;

    pf_post_t *slot = &f->ring[f->ring_head];
    slot->crew_id = crew_id;
    slot->len = len;
    slot->phase = f->phase;         /* phase-tick order, never a wall-clock stamp */
    pf_copy(slot->bytes, msg, len);
    if (len < PF_MSG_MAX) pf_zero(slot->bytes + len, PF_MSG_MAX - len);

    f->ring_head = (f->ring_head + 1u) % PF_CHAN_RING;
    if (f->ring_count < PF_CHAN_RING) f->ring_count++;
    f->phase++;
    return PF_OK;
}

surplus_real_t jdr_channel_affinity(const surplus_real_t a[CHG_DIM],
                                    const surplus_real_t b[CHG_DIM]) {
    if (!a || !b) return SR_ZERO;
    /* The companion's honest read: reuse the real Chiglet interaction measure. */
    return chg_interaction(a, b, CHG_DIM);
}

uint32_t jdr_channel_depth(const pf_fleet_t *f) {
    return f ? f->ring_count : 0u;
}

const char *jdr_crew_state_name(crew_state_t s) {
    switch (s) {
    case CREW_NONE:          return "NONE";
    case CREW_APPLIED:       return "APPLIED";
    case CREW_CREDENTIALED:  return "CREDENTIALED";
    case CREW_TREASURY_OPEN: return "TREASURY_OPEN";
    case CREW_ACTIVE:        return "ACTIVE";
    case CREW_DISBANDED:     return "DISBANDED";
    default:                 return "?";
    }
}
