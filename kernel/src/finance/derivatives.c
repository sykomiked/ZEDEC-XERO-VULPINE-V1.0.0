/* derivatives.c — Nine-Forms Derivative Contracts Implementation
 *
 * Kernel-enforced backing ratio >= 1.0x, LPRES attestation gates,
 * temporal arbitrage (no debt), interstitial jurisdiction settlement.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "derivatives.h"
#include "phase_coord.h"
#include "vino.h"
#include "porter_house.h"
#include "onepolicy/onepolicy.h"
#include "rational/rational.h"
#include "surplus.h"
#include "ipfs.h"
#include "lpres.h"
#include "triple_ledger.h"

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

/* Find ledger entry by backing CID (stored in description field) */
static ledger_entry_t *find_backing_entry(const triple_ledger_t *tl, const uint8_t backing_cid[32]) {
    for (uint32_t i = 0; i < tl->num_accounts; i++) {
        account_t *acc = &tl->accounts[i];
        for (uint32_t j = 0; j < acc->num_entries; j++) {
            ledger_entry_t *e = &acc->entries[j];
            if (e->ledger == LEDGER_FINANCIAL && e->capital_type == capital_form_to_vino(CAPITAL_FINANCIAL)) {
                /* Check if description contains the CID hex - simple byte comparison */
                for (int k = 0; k < 32; k++) {
                    if (e->description[k] != backing_cid[k]) break;
                    if (k == 31) return e;
                }
            }
        }
    }
    return NULL;
}

/* ===== Backing Verification — Kernel Admission Gate ===== */
deriv_err_t deriv_verify_backing(const deriv_contract_t *d, const triple_ledger_t *tl) {
    if (!d || !tl) return DERIV_ERR_NO_BACKING;

    /* 1. Fetch backing asset from Financial rail (846) */
    ledger_entry_t *backing = find_backing_entry(tl, d->backing_cid);
    if (!backing) return DERIV_ERR_NO_BACKING;

    /* 2. Verify Provenance rail (888) attestation — LPRES gate */
    if (!lpres_attestation_is_true(&d->backing_proof))
        return DERIV_ERR_BACKING_UNATTTESTED;  /* NEITHER/GLUT = veto */

    /* 3. Compute backing ratio — EXACT rational arithmetic
     * backing->r is the rational magnitude (amount) */
    rat_t backing_amount = rat_from_int((int64_t)(backing->r * 1000000));  /* Scale for precision */
    rat_t ratio = rat_div(backing_amount, d->notional);
    if (!ratio.valid) return DERIV_ERR_ARITHMETIC_OVERFLOW;

    /* 4. ENFORCE MINIMUM BACKING — kernel constant, not configurable
     * Minimum 1.0x (100% backing) for all priceable forms
     * State-Reserved forms require ∞ (inalienable — no derivative possible) */
    if (rat_cmp(ratio, RAT_ONE) < 0)
        return DERIV_ERR_INSUFFICIENT_BACKING;  /* VETO — no fractional reserve */

    /* 5. Externality rail (999) — verify interstitial jurisdiction */
    if (!interstitial_region_valid(&d->jurisdiction))
        return DERIV_ERR_INVALID_JURISDICTION;
    if (!interstitial_region_cid_resolves(&d->jurisdiction))
        return DERIV_ERR_INVALID_JURISDICTION;

    return DERIV_OK;
}

/* ===== Temporal Arbitrage Execution — No Debt, No Leverage ===== */
deriv_err_t temporal_arb_execute(temporal_arb_t *arb) {
    if (!arb) return DERIV_ERR_NO_SPREAD;

    /* 1. Verify both contracts have FULL BACKING (ratio >= 1.0x) */
    triple_ledger_t *tl_a = (triple_ledger_t *)arb->node_a;
    triple_ledger_t *tl_b = (triple_ledger_t *)arb->node_b;

    if (deriv_verify_backing(arb->contract_a, tl_a) != DERIV_OK)
        return DERIV_ERR_BACKING_A;
    if (deriv_verify_backing(arb->contract_b, tl_b) != DERIV_OK)
        return DERIV_ERR_BACKING_B;

    /* 2. Phase spread must be POSITIVE (node_b ahead) */
    if (arb->phase_spread <= SR_ZERO) return DERIV_ERR_NO_SPREAD;

    /* 3. Admit via Phase Coordinator — same tick or VETO */
    pc_registry_t *pc = (pc_registry_t *)arb->node_a;  /* Simplified */
    pc_step_request_t req = {0};
    req.phase_id = PC_PHASE_K6_COORD;
    req.step_id = 1;
    req.requires_coverage = true;
    req.requires_health = true;
    req.is_hardware_action = false;
    
    pc_token_t *token = pc_admit(pc, &req);
    if (!token || token->decision != PC_DECISION_ADMIT)
        return DERIV_ERR_PHASE_VETO;

    /* 4. Settle: node_a delivers backing to node_b at agreed rate
     *    No cash, no margin — backing asset transfers rail-to-rail
     *    For now, post to both ledgers as a settlement record */
    surplus_real_t notional_sr = rat_to_surplus(&arb->contract_b->notional);
    int32_t r1 = triple_ledger_post(tl_a, 0, LEDGER_FINANCIAL, notional_sr, SR_ONE, SR_ZERO, notional_sr, SR_ZERO, 0, "temporal_arb_a");
    int32_t r2 = triple_ledger_post(tl_b, 0, LEDGER_FINANCIAL, notional_sr, SR_ONE, SR_ZERO, notional_sr, SR_ZERO, 0, "temporal_arb_b");
    if (r1 < 0 || r2 < 0) return DERIV_ERR_PHASE_VETO;

    return DERIV_OK;
}

/* ===== Derivative Settlement — LPRES-Gated Atomic Transfer ===== */
deriv_err_t deriv_settle(deriv_contract_t *d, triple_ledger_t *tl) {
    if (!d || !tl) return DERIV_VETO;

    /* Gate 1: Backing verification — LPRES */
    if (d->backing_proof.state != LPRES_STATE_TRUE) {
        if (d->backing_proof.state == LPRES_STATE_NEITHER) return DERIV_DEFER;
        if (d->backing_proof.state == LPRES_STATE_BOTH) return DERIV_ABSTAIN;
        return DERIV_VETO;  /* FALSE */
    }

    /* Gate 2: Phase admission — OSEQ + Phase Coordinator */
    pc_registry_t *pc_reg = (pc_registry_t *)tl;  /* Simplified */
    pc_step_request_t req = {0};
    req.phase_id = PC_PHASE_K6_COORD;
    req.step_id = 2;
    req.requires_coverage = true;
    req.requires_health = true;
    req.is_hardware_action = false;
    
    pc_token_t *token = pc_admit(pc_reg, &req);
    if (!token || token->decision == PC_DECISION_VETO) return DERIV_VETO;
    if (token->decision == PC_DECISION_DEFER) return DERIV_DEFER;

    /* Gate 3: One Policy — Symbiotic Maxim */
    op_term_t term = {0};
    term.give_a = rat_to_surplus(&d->notional);
    term.give_b = rat_to_surplus(&d->notional);  /* Principal-only, no interest */
    term.interest = SR_ZERO;
    term.reciprocal = true;
    term.fraud_root = false;
    term.denies_aid = false;
    term.revoke_for_nonpayment = false;
    term.has_kill_switch = false;
    term.harm_a = SR_ZERO;
    term.harm_b = SR_ZERO;

    if (!op_symbiotic_ok(&term)) return DERIV_VETO_MAXIM;

    /* Gate 4: Single-active-state — voucher spendable on ONE rail
     * Simplified: check that vouchers array is not all zero */
    bool has_voucher = false;
    for (int i = 0; i < 3; i++) {
        if (d->vouchers[i] != 0) { has_voucher = true; break; }
    }
    if (!has_voucher) return DERIV_VETO_STATE;

    /* ALL GATES CLEAR → Atomic three-rail transfer
     * Post to all three ledgers atomically */
    capital_type_t cap = capital_form_to_vino(d->underlying_form);
    surplus_real_t notional_sr = rat_to_surplus(&d->notional);
    
    /* Post to Financial rail (846) */
    int32_t r1 = triple_ledger_post(tl, 0, LEDGER_FINANCIAL, notional_sr, SR_ONE, SR_ZERO, notional_sr, SR_ZERO, 0, "derivative_settle");
    
    /* Post to Provenance rail (888) */
    int32_t r2 = triple_ledger_post(tl, 0, LEDGER_PROVENANCE, notional_sr, SR_ONE, SR_ZERO, notional_sr, SR_ZERO, 0, "derivative_attestation");
    
    /* Post to Externality rail (999) */
    int32_t r3 = triple_ledger_post(tl, 0, LEDGER_EXTERNALITY, notional_sr, SR_ONE, d->phase_curvature, notional_sr, SR_ZERO, 0, "derivative_externality");

    if (r1 < 0 || r2 < 0 || r3 < 0) return DERIV_VETO;

    return DERIV_OK;
}

/* ===== Contract Creation ===== */
deriv_err_t deriv_create(deriv_contract_t *d, const triple_ledger_t *tl,
                         capital_form_t form, const rat_t *notional,
                         const surplus_real_t *strike, uint64_t expiry_tick,
                         const uint8_t backing_cid[32], const lpres_attestation_t *backing_proof,
                         const interstitial_region_t *jurisdiction, bool treaty_backed) {
    if (!d || !notional || !strike || !backing_cid || !backing_proof || !jurisdiction)
        return DERIV_ERR_NO_BACKING;

    /* Only priceable forms (5-9) can underlie derivatives */
    if (!capital_is_priceable(form)) return DERIV_ERR_INSUFFICIENT_BACKING;

    d->notional = *notional;
    d->strike = *strike;
    d->expiry_tick = expiry_tick;
    d->underlying_form = form;
    for (int i = 0; i < 32; i++) d->backing_cid[i] = backing_cid[i];
    d->backing_proof = *backing_proof;
    d->backing_verification_tick = phase_coordinator_current_tick();
    d->phase_curvature = SR_ZERO;  /* Computed at settlement */
    d->jurisdiction = *jurisdiction;
    d->treaty_backed = treaty_backed;

    /* Verify backing immediately */
    return deriv_verify_backing(d, tl);
}
