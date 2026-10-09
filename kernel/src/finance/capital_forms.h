/* capital_forms.h — Canonical Nine Forms of Capital (PAPSS-Aligned)
 *
 * This is the PAPSS-sanitized taxonomy matching the Nine Forms of Capital
 * Derivatives System Proposal. It maps to the canonical zcapital.h forms
 * but uses central-bank-acceptable terminology.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_CAPITAL_FORMS_H
#define ZXV_CAPITAL_FORMS_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zcapital.h"

/* ===== Nine Forms of Capital (PAPSS Taxonomy) =====
 *
 * Forms 1-4: State-Reserved (inalienable, cannot be derivative underlyings)
 * Forms 5-9: Priceable (can underlie derivatives, settle on native rails)
 */
typedef enum {
    /* STATE-RESERVED (inalienable) — governed by Crown/State authority */
    CAPITAL_SOCIAL              = 0,  /* zcap: ZCAP_SOCIAL      — Reputation attestation */
    CAPITAL_NATURAL             = 1,  /* zcap: ZCAP_NATURAL     — Bandwidth/energy/spectrum */
    CAPITAL_HERITAGE_INTELLECTUAL = 2, /* zcap: ZCAP_CULTURAL    — Content CID (.zxvc/.cedez) */
    CAPITAL_GOVERNANCE_INSTITUTIONAL = 3, /* zcap: ZCAP_SPIRITUAL — Governance predicate */

    /* PRICEABLE — settle on native rails via triple ledger */
    CAPITAL_FINANCIAL           = 4,  /* zcap: ZCAP_FINANCIAL   — Vino voucher / fiat / crypto (rat_t exact) */
    CAPITAL_MATERIAL            = 5,  /* zcap: ZCAP_MANUFACTURED — Hardware CID (.zedec + holographic) */
    CAPITAL_LIVING              = 6,  /* zcap: ZCAP_HUMAN       — Developer time (community_chest P2P) */
    CAPITAL_KNOWLEDGE           = 7,  /* zcap: ZCAP_INTELLECTUAL — IP/algorithm (refinery card) */
    CAPITAL_BUILT               = 8,  /* zcap: ZCAP_SYSTEM      — Infrastructure (count_house fractal reserve) */

    CAPITAL_FORM_COUNT          = 9
} capital_form_t;

/* ===== ISO 4217 Rail Mapping ===== */
#define RAIL_FINANCIAL      846  /* Asset/backing — VINO_ISO_DEBIT */
#define RAIL_PROVENANCE     810  /* Claim/attestation — VINO_ISO_CREDIT */
#define RAIL_EXTERNALITY    888  /* Live equity witness — VINO_ISO_EQUITY */

/* ===== Inalienability Guard ===== */
static inline bool capital_is_state_reserved(capital_form_t form) {
    return form <= CAPITAL_GOVERNANCE_INSTITUTIONAL;
}

static inline bool capital_is_priceable(capital_form_t form) {
    return form >= CAPITAL_FINANCIAL;
}

/* Map to canonical zcapital form */
static inline zcap_form_t capital_to_zcap(capital_form_t form) {
    static const zcap_form_t map[CAPITAL_FORM_COUNT] = {
        ZCAP_SOCIAL,           /* 0 -> SOCIAL */
        ZCAP_NATURAL,          /* 1 -> NATURAL */
        ZCAP_CULTURAL,         /* 2 -> CULTURAL (Heritage/Intellectual) */
        ZCAP_SPIRITUAL,        /* 3 -> SPIRITUAL (Governance/Institutional) */
        ZCAP_FINANCIAL,        /* 4 -> FINANCIAL */
        ZCAP_MANUFACTURED,     /* 5 -> MANUFACTURED (Material) */
        ZCAP_HUMAN,            /* 6 -> HUMAN (Living) */
        ZCAP_INTELLECTUAL,     /* 7 -> INTELLECTUAL (Knowledge) */
        ZCAP_SYSTEM            /* 8 -> SYSTEM (Built) */
    };
    return (form < CAPITAL_FORM_COUNT) ? map[form] : ZCAP_FINANCIAL;
}

/* Settlement medium per form */
typedef enum {
    SETTLEMENT_REPUTATION     = 0,  /* Social: con_commons_t attestation */
    SETTLEMENT_MESH_ROUTE     = 1,  /* Natural: mesh_net priced routes */
    SETTLEMENT_CONTENT_CID    = 2,  /* Heritage/Intellectual: .zxvc/.cedez */
    SETTLEMENT_GOVERNANCE     = 3,  /* Governance/Institutional: op_evaluate verdict */
    SETTLEMENT_VINO_VOUCHER   = 4,  /* Financial: rat_t exact on rail 846 */
    SETTLEMENT_HARDWARE_CID   = 5,  /* Material: .zedec + holographic seal */
    SETTLEMENT_DEV_TIME       = 6,  /* Living: community_chest P2P shares */
    SETTLEMENT_KNOWLEDGE_CID  = 7,  /* Knowledge: refinery card output */
    SETTLEMENT_INFRASTRUCTURE = 8   /* Built: count_house fractal reserve */
} settlement_medium_t;

static inline settlement_medium_t capital_settlement_medium(capital_form_t form) {
    return (settlement_medium_t)form;
}

/* ===== Rail Assignment =====
 * Each capital form settles on its native rail(s):
 * - Financial (5): Rail 846 only
 * - Material (6): Rail 846 only
 * - Living (7): Rail 846 only
 * - Knowledge (8): Rail 810 (Provenance)
 * - Built (9): Rail 846 only
 * - State-Reserved (1-4): Rail 810 (Provenance) for attestation, Rail 888 (Externality) for custody
 */
static inline uint16_t capital_primary_rail(capital_form_t form) {
    switch (form) {
        case CAPITAL_KNOWLEDGE: return RAIL_PROVENANCE;
        default: return RAIL_FINANCIAL;
    }
}

#endif /* ZXV_CAPITAL_FORMS_H */
