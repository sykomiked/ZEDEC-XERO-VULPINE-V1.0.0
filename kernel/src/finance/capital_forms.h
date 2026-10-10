/* capital_forms.h — Nine Forms of Capital (finance taxonomy)
 *
 * The Nine Forms taxonomy used by the finance/ modules (derivatives,
 * Pay-It-Forward Assurance, treaty tokenization), matching the Nine Forms
 * of Capital Derivatives System Proposal. Its values are the canonical
 * zcap_form_t values (zcap_forms.h) under finance-sector names.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_CAPITAL_FORMS_H
#define ZXV_CAPITAL_FORMS_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zcapital.h"

/* ===== Nine Forms of Capital =====
 *
 * Finance-sector names for the canonical zcap_form_t (zcap_forms.h). The
 * values ARE the zcap values (checked below), so a capital_form_t handed to
 * zcapital, vino, pay or swarm means the same form there. Before this the
 * enum listed the four state-reserved forms first (SOCIAL = 0 ... FINANCIAL
 * = 4); code must use capital_is_state_reserved(), never a range compare.
 *
 *   State-reserved (inalienable, never a derivative underlying):
 *     SOCIAL, NATURAL, HERITAGE_INTELLECTUAL (cultural),
 *     GOVERNANCE_INSTITUTIONAL (spiritual)
 *   Priceable: FINANCIAL, MATERIAL, KNOWLEDGE, LIVING (developer time = human),
 *     BUILT (infrastructure = system)
 */
typedef enum {
    CAPITAL_FINANCIAL = ZCAP_FINANCIAL,            /* Vino voucher / fiat / crypto (rat_t exact) */
    CAPITAL_MATERIAL = ZCAP_MANUFACTURED,          /* Hardware CID (.zedec + holographic) */
    CAPITAL_KNOWLEDGE = ZCAP_INTELLECTUAL,         /* IP/algorithm (refinery card) */
    CAPITAL_LIVING = ZCAP_HUMAN,                   /* Developer time (community_chest P2P) */
    CAPITAL_SOCIAL = ZCAP_SOCIAL,                  /* Reputation attestation */
    CAPITAL_NATURAL = ZCAP_NATURAL,                /* Bandwidth/energy/spectrum */
    CAPITAL_HERITAGE_INTELLECTUAL = ZCAP_CULTURAL, /* Content CID (.zxvc/.cedez) */
    CAPITAL_GOVERNANCE_INSTITUTIONAL = ZCAP_SPIRITUAL, /* Governance predicate */
    CAPITAL_BUILT = ZCAP_SYSTEM, /* Infrastructure (count_house fractal reserve) */

    CAPITAL_FORM_COUNT = ZCAP_FORM_COUNT
} capital_form_t;

_Static_assert((int) CAPITAL_FINANCIAL == (int) ZCAP_FINANCIAL, "finance FINANCIAL");
_Static_assert((int) CAPITAL_MATERIAL == (int) ZCAP_MANUFACTURED, "finance MATERIAL");
_Static_assert((int) CAPITAL_KNOWLEDGE == (int) ZCAP_INTELLECTUAL, "finance KNOWLEDGE");
_Static_assert((int) CAPITAL_LIVING == (int) ZCAP_HUMAN, "finance LIVING");
_Static_assert((int) CAPITAL_SOCIAL == (int) ZCAP_SOCIAL, "finance SOCIAL");
_Static_assert((int) CAPITAL_NATURAL == (int) ZCAP_NATURAL, "finance NATURAL");
_Static_assert((int) CAPITAL_HERITAGE_INTELLECTUAL == (int) ZCAP_CULTURAL, "finance HERITAGE");
_Static_assert((int) CAPITAL_GOVERNANCE_INSTITUTIONAL == (int) ZCAP_SPIRITUAL,
               "finance GOVERNANCE");
_Static_assert((int) CAPITAL_BUILT == (int) ZCAP_SYSTEM, "finance BUILT");
_Static_assert((int) CAPITAL_FORM_COUNT == ZCAP_FORM_COUNT, "finance count");

/* ===== Rails: the canonical numerics (pay/pay_rails.h) =====
 * PROVENANCE and EXTERNALITY are this module's names for the CREDIT and
 * EQUITY rails (the triple-ledger book each feeds), not other rails. */
#include "../pay/pay_rails.h"
#define RAIL_FINANCIAL   ZXV_RAIL_CODE_DEBIT  /* 555 Asset/backing      = VINO_ISO_DEBIT  */
#define RAIL_PROVENANCE  ZXV_RAIL_CODE_CREDIT /* 777 Claim/attestation  = VINO_ISO_CREDIT */
#define RAIL_EXTERNALITY ZXV_RAIL_CODE_EQUITY /* 888 Live equity witness = VINO_ISO_EQUITY */

/* ===== Inalienability Guard ===== */
static inline bool capital_is_state_reserved(capital_form_t form)
{
    return zcap_form_is_crown((unsigned) form) != 0;
}

static inline bool capital_is_priceable(capital_form_t form)
{
    return (unsigned) form < (unsigned) CAPITAL_FORM_COUNT && !capital_is_state_reserved(form);
}

/* Map to canonical zcapital form: the identity (the values are equal).
 * Out of range -> ZCAP_FINANCIAL, as before; callers range-check first. */
static inline zcap_form_t capital_to_zcap(capital_form_t form)
{
    return ((unsigned) form < (unsigned) CAPITAL_FORM_COUNT) ? (zcap_form_t) form : ZCAP_FINANCIAL;
}

static inline capital_form_t capital_from_zcap(zcap_form_t form)
{
    return (capital_form_t) form;
}

/* Settlement medium per form (values follow the form index) */
typedef enum {
    SETTLEMENT_VINO_VOUCHER = CAPITAL_FINANCIAL,  /* Financial: rat_t exact on rail 555 */
    SETTLEMENT_HARDWARE_CID = CAPITAL_MATERIAL,   /* Material: .zedec + holographic seal */
    SETTLEMENT_KNOWLEDGE_CID = CAPITAL_KNOWLEDGE, /* Knowledge: refinery card output */
    SETTLEMENT_DEV_TIME = CAPITAL_LIVING,         /* Living: community_chest P2P shares */
    SETTLEMENT_REPUTATION = CAPITAL_SOCIAL,       /* Social: con_commons_t attestation */
    SETTLEMENT_MESH_ROUTE = CAPITAL_NATURAL,      /* Natural: mesh_net priced routes */
    SETTLEMENT_CONTENT_CID = CAPITAL_HERITAGE_INTELLECTUAL,   /* Heritage: .zxvc/.cedez */
    SETTLEMENT_GOVERNANCE = CAPITAL_GOVERNANCE_INSTITUTIONAL, /* op_evaluate verdict */
    SETTLEMENT_INFRASTRUCTURE = CAPITAL_BUILT /* Built: count_house fractal reserve */
} settlement_medium_t;

static inline settlement_medium_t capital_settlement_medium(capital_form_t form)
{
    return (settlement_medium_t) form;
}

/* ===== Rail Assignment =====
 * Each capital form settles on its native rail(s):
 * - Financial, Material, Living, Built: Rail 555 (DEBIT) only
 * - Knowledge: Rail 777 (CREDIT, "Provenance")
 * - State-reserved (Social, Natural, Heritage, Governance): Rail 777 for
 *   attestation and Rail 888 (EQUITY, "Externality") for custody
 */
static inline uint16_t capital_primary_rail(capital_form_t form)
{
    switch (form) {
    case CAPITAL_KNOWLEDGE:
        return RAIL_PROVENANCE;
    default:
        return RAIL_FINANCIAL;
    }
}

#endif /* ZXV_CAPITAL_FORMS_H */
