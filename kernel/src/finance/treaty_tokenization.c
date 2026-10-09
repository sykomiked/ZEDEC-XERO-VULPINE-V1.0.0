/* treaty_tokenization.c — Treaty-Backed Asset Tokenization Implementation
 *
 * Conservation easements → treaty-backed Natural capital tokens on rail 888.
 * Never fractionalized. Backing ratio >= 1.0x enforced by kernel.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "treaty_tokenization.h"
#include "triple_ledger.h"
#include "lpres.h"
#include "ipfs.h"
#include "count_house.h"
#include "phase_coord.h"
#include "interspace/interspace.h"
#include "vino.h"
#include "rational/rational.h"

/* Map PAPSS capital_form_t to vino capital_type_t */
static capital_type_t capital_form_to_vino(capital_form_t form) {
    switch (form) {
        case CAPITAL_FINANCIAL: return CAP_FINANCIAL;
        case CAPITAL_MATERIAL: return CAP_MATERIAL;
        case CAPITAL_LIVING: return CAP_HUMAN;
        case CAPITAL_KNOWLEDGE: return CAP_KNOWLEDGE;
        case CAPITAL_BUILT: return CAP_BUILT;
        case CAPITAL_SOCIAL: return CAP_SOCIAL;
        case CAPITAL_NATURAL: return CAP_LIVING;
        case CAPITAL_HERITAGE_INTELLECTUAL: return CAP_CULTURAL;
        case CAPITAL_GOVERNANCE_INSTITUTIONAL: return CAP_SPIRITUAL;
        default: return CAP_FINANCIAL;
    }
}

/* Convert rat_t to surplus_real_t (Q32.32 on target, double on host) */
static surplus_real_t rat_to_surplus(const rat_t *r) {
    if (!r || !r->valid || r->den == 0) return SR_ZERO;
    /* On host (double): exact division. On target (Q32.32): use SR_DIV with scaled numerator. */
    #ifdef TEST_HOST
        return (surplus_real_t)r->num / (surplus_real_t)r->den;
    #else
        /* Q32.32: multiply numerator by SR_ONE before division */
        return SR_DIV(SR_FROM_INT(r->num), SR_FROM_INT(r->den));
    #endif
}

/* ===== Treaty Asset Verification & Tokenization ===== */
treaty_err_t treaty_asset_verify(const treaty_asset_t *ta, const triple_ledger_t *tl) {
    if (!ta || !tl) return TREATY_ERR_NO_TREATY;

    /* 1. Treaty CID must resolve on IPFS */
    if (!treaty_cid_resolves(ta->treaty_cid)) return TREATY_ERR_NO_TREATY;

    /* 2. Sovereignty proof must be LPRES STATE_TRUE (not NEITHER/BOTH) */
    if (ta->sovereignty_proof.state != LPRES_STATE_TRUE) return TREATY_ERR_SOVEREIGNTY;

    /* 3. Asset must be in State-Reserved (inalienable) or Priceable with backing
     *    NATURAL (2) and BUILT (9) require Count House valuation */
    if (ta->form == CAPITAL_NATURAL || ta->form == CAPITAL_BUILT) {
        /* Convert quantified_value to surplus_real_t for comparison */
        surplus_real_t qv = rat_to_surplus(&ta->quantified_value);
        /* Use count_house_valuation - returns floor price as surplus_real_t */
        surplus_real_t valuation = count_house_valuation(NULL);  /* Would need actual count_house instance */
        if (valuation < qv) return TREATY_ERR_VALUATION;
    }

    /* 4. Asset CID must resolve on IPFS */
    if (!treaty_cid_resolves(ta->asset_cid)) return TREATY_ERR_CID_MISMATCH;

    return TREATY_OK;
}

/* ===== Create Treaty-Backed Asset ===== */
treaty_err_t treaty_asset_create(treaty_asset_t *ta, const triple_ledger_t *tl,
                                 const uint8_t treaty_cid[32], const uint8_t asset_cid[32],
                                 capital_form_t form, const rat_t *quantified_value,
                                 const lpres_attestation_t *sovereignty_proof,
                                 const interstitial_region_t *corridor) {
    if (!ta || !treaty_cid || !asset_cid || !quantified_value || !sovereignty_proof || !corridor)
        return TREATY_ERR_NO_TREATY;

    /* Only NATURAL and BUILT forms can be treaty-tokenized */
    if (form != CAPITAL_NATURAL && form != CAPITAL_BUILT)
        return TREATY_ERR_VALUATION;

    for (int i = 0; i < 32; i++) {
        ta->treaty_cid[i] = treaty_cid[i];
        ta->asset_cid[i] = asset_cid[i];
    }
    ta->form = form;
    ta->quantified_value = *quantified_value;
    ta->sovereignty_proof = *sovereignty_proof;
    ta->corridor = *corridor;
    ta->tokenization_tick = phase_coordinator_current_tick();

    /* Verify before posting */
    treaty_err_t err = treaty_asset_verify(ta, tl);
    if (err != TREATY_OK) return err;

    /* 5. Post to Externality rail (888) as treaty-backed asset */
    capital_type_t cap = capital_form_to_vino(form);
    surplus_real_t qv = rat_to_surplus(quantified_value);
    int32_t r = triple_ledger_post((triple_ledger_t *)tl, 0, LEDGER_EXTERNALITY,
                                    qv, SR_ONE, SR_ZERO,
                                    qv, SR_ZERO, 0, "treaty_asset");
    if (r < 0) return TREATY_ERR_RAIL_FULL;

    return TREATY_OK;
}
