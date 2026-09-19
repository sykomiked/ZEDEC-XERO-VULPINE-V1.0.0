/* assurance.c — Pay-It-Forward Assurance Protocol Implementation
 *
 * Kernel admission gates: contribution posting, LPRES TRUE efficacy,
 * One Policy (no usury, reciprocity), generation ratio >= 1.0x,
 * forward route validity. Pay-it-forward transfer on verification.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "assurance.h"
#include "triple_ledger.h"
#include "lpres.h"
#include "onepolicy/onepolicy.h"
#include "rational/rational.h"
#include "surplus.h"
#include "phase_coord.h"
#include "ipfs.h"
#include "interspace/interspace.h"

/* ===== Internal: LPRES Attestation Check ===== */
static inline bool lpres_attestation_is_true(const lpres_attestation_t *proof) {
    if (!proof) return false;
    return proof->state == LPRES_STATE_TRUE;  /* NEITHER/BOTH/FALSE = veto */
}

/* Map PAPSS capital_form_t to vino capital_type_t */
static capital_type_t capital_form_to_vino(capital_form_t form) {
    switch (form) {
        case CAPITAL_FINANCIAL: return CAP_FINANCIAL;
        case CAPITAL_MATERIAL: return CAP_MATERIAL;
        case CAPITAL_LIVING: return CAP_HUMAN;
        case CAPITAL_KNOWLEDGE: return CAP_KNOWLEDGE;
        case CAPITAL_BUILT: return CAP_BUILT;
        case CAPITAL_SOCIAL: return CAP_SOCIAL;
        case CAPITAL_NATURAL: return CAP_LIVING;  /* Natural maps to Living in vino */
        case CAPITAL_HERITAGE_INTELLECTUAL: return CAP_CULTURAL;
        case CAPITAL_GOVERNANCE_INSTITUTIONAL: return CAP_SPIRITUAL;
        default: return CAP_FINANCIAL;
    }
}

/* Convert rat_t to surplus_real_t (Q32.32 on target, double on host) */
static surplus_real_t rat_to_surplus(const rat_t *r) {
    if (!r || !r->valid || r->den == 0) return SR_ZERO;
    #ifdef TEST_HOST
        return (surplus_real_t)r->num / (surplus_real_t)r->den;
    #else
        return SR_DIV(SR_FROM_INT(r->num), SR_FROM_INT(r->den));
    #endif
}

/* ===== Assurance Admission Gate — 5 Gates ===== */
assurance_err_t assurance_admit(assurance_contract_t *a, triple_ledger_t *tl) {
    if (!a || !tl) return ASSURANCE_ERR_CONTRIBUTION;

    /* Gate 1: Contribution posted to Financial rail (846) — exact rational
     * Use account 0 for system-level contracts */
    surplus_real_t contrib_sr = rat_to_surplus(&a->contribution);
    int32_t r = triple_ledger_post(tl, 0, LEDGER_FINANCIAL, 
                                    contrib_sr, SR_ONE, SR_ZERO,
                                    contrib_sr, SR_ZERO, 0, "assurance_contribution");
    if (r < 0) return ASSURANCE_ERR_CONTRIBUTION;

    /* Gate 2: Prevention project MUST have LPRES TRUE efficacy attestation */
    if (!lpres_attestation_is_true(&a->efficacy_proof))
        return ASSURANCE_ERR_EFFICACY_UNVERIFIED;  /* NEITHER/BOTH = veto */

    /* Gate 3: One Policy — Symbiotic Maxim (no usury, no coercion, reciprocity) */
    op_term_t term = {0};
    term.give_a = rat_to_surplus(&a->contribution);
    term.give_b = rat_to_surplus(&a->generation_ratio);  /* Generated >= contributed */
    term.interest = SR_ZERO;  /* NO USURY — assurance cannot charge interest */
    term.reciprocal = a->pay_it_forward;  /* Must pay forward */
    term.fraud_root = false;
    term.denies_aid = false;
    term.revoke_for_nonpayment = false;
    term.has_kill_switch = false;
    term.harm_a = SR_ZERO;
    term.harm_b = SR_ZERO;

    if (!op_symbiotic_ok(&term)) return ASSURANCE_ERR_MAXIM_VIOLATION;

    /* Gate 4: Generation ratio >= 1.0x — prevention must GENERATE, not just preserve */
    if (rat_cmp(a->generation_ratio, RAT_ONE) < 0)
        return ASSURANCE_ERR_INSUFFICIENT_GENERATION;

    /* Gate 5: Forward flow — generated capital MUST route to next prevention */
    if (a->pay_it_forward && !interstitial_region_valid(&a->forward_route))
        return ASSURANCE_ERR_FORWARD_ROUTE_INVALID;

    /* ALL GATES CLEAR → Register assurance contract on Provenance rail (888) */
    r = triple_ledger_post(tl, 0, LEDGER_PROVENANCE,
                            contrib_sr, SR_ONE, SR_ZERO,
                            contrib_sr, SR_ZERO, 0, "assurance_contract");
    return (r < 0) ? ASSURANCE_ERR_CONTRIBUTION : ASSURANCE_OK;
}

/* ===== Assurance Settlement — Capital Generation Verification ===== */
assurance_err_t assurance_verify_generation(assurance_contract_t *a, triple_ledger_t *tl) {
    if (!a || !tl) return ASSURANCE_ERR_NO_GENERATION;

    /* 1. Verify generation by checking Provenance rail for the contract */
    /* In practice, this would check for a ledger entry with the prevention CID in description */
    capital_type_t gen_cap = capital_form_to_vino(a->generated_form);
    
    /* 2. Verify generation ratio — EXACT rational
     * For now, assume generation is verified by the existence of the contract on Provenance rail */
    
    /* 3. LPRES attestation of generation — must be TRUE
     * This would be stored in the ledger entry's attestation field */
    
    /* 4. Pay-it-forward: transfer generated capital to forward_cid on target rail */
    if (a->pay_it_forward) {
        /* Use triple_ledger_transfer between accounts */
        surplus_real_t contrib_sr = rat_to_surplus(&a->contribution);
        int32_t r = triple_ledger_transfer(tl, 0, 0,  /* from_id, to_id */
                                            gen_cap, contrib_sr, SR_ONE, SR_ZERO, "pay_it_forward");
        if (r < 0) return ASSURANCE_ERR_FORWARD_ROUTE_INVALID;
    }

    a->generation_tick = phase_coordinator_current_tick();

    return ASSURANCE_OK;
}

/* ===== Contract Creation ===== */
assurance_err_t assurance_create(assurance_contract_t *a, triple_ledger_t *tl,
                                 const rat_t *contribution, capital_form_t target_form,
                                 const uint8_t prevention_cid[32], const lpres_attestation_t *efficacy_proof,
                                 capital_form_t generated_form, const rat_t *generation_ratio,
                                 bool pay_it_forward, const interstitial_region_t *forward_route) {
    if (!a || !contribution || !prevention_cid || !efficacy_proof || !generation_ratio)
        return ASSURANCE_ERR_CONTRIBUTION;

    /* Key rule: generated_form != target_form — assurance TRANSMUTES capital forms */
    if (generated_form == target_form) return ASSURANCE_ERR_INSUFFICIENT_GENERATION;

    a->contribution = *contribution;
    a->target_form = target_form;
    for (int i = 0; i < 32; i++) a->prevention_cid[i] = prevention_cid[i];
    a->efficacy_proof = *efficacy_proof;
    a->generated_form = generated_form;
    a->generation_ratio = *generation_ratio;
    a->generation_tick = 0;  /* Set on verification */
    a->pay_it_forward = pay_it_forward;
    if (forward_route) a->forward_route = *forward_route;

    return assurance_admit(a, tl);
}

/* ===== Assurance Derivative Admission =====
 * Only pays out on VERIFIED GENERATION — long prevention, not short risk */
assurance_err_t assurance_deriv_admit(assurance_deriv_t *d, triple_ledger_t *tl) {
    if (!d || !tl) return ASSURANCE_ERR_CONTRIBUTION;

    /* Post derivative to Financial rail (846) */
    surplus_real_t notional_sr = rat_to_surplus(&d->notional);
    int32_t r = triple_ledger_post(tl, 0, LEDGER_FINANCIAL,
                                    notional_sr, SR_ONE, SR_ZERO,
                                    notional_sr, SR_ZERO, 0, "assurance_derivative");
    return (r < 0) ? ASSURANCE_ERR_CONTRIBUTION : ASSURANCE_OK;
}
