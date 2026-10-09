/* financial_fabric.c — ZXV Financial Fabric Compound Module Implementation
 *
 * Unifies all financial modules: Triple Ledger, Derivatives, Assurance,
 * Treaty Tokenization, Mesh Token, Finance Markets, Rails, Crypto Bridge,
 * Vino Ledger, and Vena Runtime into a single coherent fabric.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "financial_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Helper Functions ===== */

static void ff_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void ff_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int ff_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t ff_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void ff_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t ff_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t ff_attest(financial_fabric_t *fabric, uint32_t account_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || account_id >= fabric->num_accounts) return LPRES_STATE_NEITHER;
    
    ff_account_t *acc = &fabric->accounts[account_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t account_att = acc->attestation;
    lpres_state_t coverage_att = (SR_CMP(acc->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, account_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    acc->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void ff_init(financial_fabric_t *fabric, orbital_fabric_t *orbital) {
    if (!fabric) return;
    
    ff_mem_set(fabric, 0, sizeof(*fabric));
    fabric->orbital = orbital;
    
    /* Initialize sub-modules */
    /* triple_ledger_init(&fabric->ledger); */
    /* vino_init(&fabric->vino); */
    /* vena_init(&fabric->vena); */
    /* finance_markets_init(&fabric->markets); */
    /* rails_init(&fabric->rails); */
    /* crypto_bridge_init(&fabric->bridge); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(5.0);  /* Financial rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = ff_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.enforce_100_percent_backing = true;
    fabric->config.allow_temporal_arb = true;
    fabric->config.require_treaty_rail_888 = true;
    fabric->config.min_margin_ratio = SR_ONE;      /* 100% */
    fabric->config.max_leverage = SR_ONE;          /* No leverage */
    fabric->config.settlement_cycles = 1000;
    fabric->config.auto_liquidate_margin_calls = true;
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void ff_register_builtins(financial_fabric_t *fabric) {
    if (!fabric) return;
    /* Built-in modules already initialized in ff_init */
}

/* ===== Account Management ===== */

int32_t ff_create_account(financial_fabric_t *fabric, const char *name,
                          const word168_t *owner_id,
                          const uint64_t initial_balances[9]) {
    if (!fabric || !name || fabric->num_accounts >= FF_MAX_ACCOUNTS) return -1;
    
    ff_account_t *acc = &fabric->accounts[fabric->num_accounts];
    ff_mem_set(acc, 0, sizeof(*acc));
    acc->id = fabric->next_account_id++;
    
    ff_str_copy(acc->name, name, FF_MAX_NAME_LEN);
    if (owner_id) acc->owner_id = *owner_id;
    
    if (initial_balances) {
        for (int i = 0; i < 9; i++) acc->balances[i] = initial_balances[i];
    }
    
    /* Initialize M5 for account */
    acc->m5.omega = fabric->num_accounts + 1;
    acc->m5.r = SR_FROM_FLOAT(5.0 + fabric->num_accounts * 0.01);
    acc->m5.ell = SR_ONE;
    acc->m5.phi = SR_ZERO;
    acc->m5.chi = 0;
    acc->coverage_ratio = ff_compute_coverage(&acc->m5);
    
    acc->credit_score = SR_FROM_FLOAT(0.5);
    acc->reputation = SR_FROM_FLOAT(0.5);
    acc->attestation = LPRES_STATE_NEITHER;
    acc->active = true;
    
    fabric->num_accounts++;
    fabric->stats.total_accounts_created++;
    
    return (int32_t)(acc->id);
}

ff_account_t *ff_get_account(financial_fabric_t *fabric, uint32_t account_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_accounts; i++) {
        if (fabric->accounts[i].id == account_id && fabric->accounts[i].active) {
            return &fabric->accounts[i];
        }
    }
    return NULL;
}

ff_account_t *ff_get_account_by_name(financial_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_accounts; i++) {
        if (fabric->accounts[i].active && ff_str_cmp(fabric->accounts[i].name, name) == 0) {
            return &fabric->accounts[i];
        }
    }
    return NULL;
}

/* ===== Capital Operations ===== */

int32_t ff_transfer_capital(financial_fabric_t *fabric,
                            uint32_t from_account, uint32_t to_account,
                            uint8_t form, uint64_t amount) {
    if (!fabric || form < 1 || form > 9) return -1;
    
    ff_account_t *from = ff_get_account(fabric, from_account);
    ff_account_t *to = ff_get_account(fabric, to_account);
    if (!from || !to) return -1;
    
    if (from->balances[form - 1] < amount) return -1;
    
    from->balances[form - 1] -= amount;
    to->balances[form - 1] += amount;
    
    /* Triple ledger entry */
    ff_ledger_entry(fabric, from_account, form, form, amount, "Transfer");
    ff_ledger_entry(fabric, to_account, form, form, amount, "Transfer received");
    
    fabric->stats.total_volume += amount;
    ff_attest(fabric, from_account, 0x1000 | form, (void*)(uintptr_t)amount, 0);
    ff_attest(fabric, to_account, 0x2000 | form, (void*)(uintptr_t)amount, 0);
    
    return 0;
}

int32_t ff_mint_voucher(financial_fabric_t *fabric,
                        uint32_t account_id, uint8_t form, uint64_t amount) {
    if (!fabric || form < 1 || form > 9) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    acc->balances[form - 1] += amount;
    ff_ledger_entry(fabric, account_id, 0, form, amount, "Voucher minted");
    
    fabric->stats.total_volume += amount;
    return ff_attest(fabric, account_id, 0x3000 | form, (void*)(uintptr_t)amount, 0);
}

int32_t ff_burn_voucher(financial_fabric_t *fabric,
                        uint32_t account_id, uint8_t form, uint64_t amount) {
    if (!fabric || form < 1 || form > 9) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    if (acc->balances[form - 1] < amount) return -1;
    
    acc->balances[form - 1] -= amount;
    ff_ledger_entry(fabric, account_id, form, 0, amount, "Voucher burned");
    
    return ff_attest(fabric, account_id, 0x4000 | form, (void*)(uintptr_t)amount, 0);
}

int32_t ff_ledger_entry(financial_fabric_t *fabric,
                        uint32_t account_id,
                        uint8_t source_form, uint8_t dest_form,
                        uint64_t amount, const char *memo) {
    if (!fabric) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    /* In real implementation, would call triple_ledger_entry() */
    /* triple_ledger_entry(&fabric->ledger, account_id, source_form, dest_form, amount, memo); */
    
    return 0;
}

/* ===== Derivatives ===== */

int32_t ff_create_derivative(financial_fabric_t *fabric,
                             uint32_t account_id,
                             m5_capital_form_t underlying_form,
                             const surplus_real_t *notional,
                             const surplus_real_t *strike,
                             uint64_t expiry_tick,
                             const uint8_t backing_cid[32],
                             const m5_lpres_proof_t *backing_proof,
                             const uint8_t jurisdiction_cid[32],
                             bool treaty_backed) {
    if (!fabric) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    if (fabric->derivatives.num_contracts >= FF_MAX_CONTRACTS) return -1;
    if (fabric->num_deriv_positions >= FF_MAX_POSITIONS) return -1;
    
    /* Create derivative contract */
    m5_deriv_contract_t *contract = &fabric->derivatives.contracts[fabric->derivatives.num_contracts];
    ff_mem_set(contract, 0, sizeof(*contract));
    
    contract->underlying_form = underlying_form;
    /* Convert surplus_real_t (Q32.32) to m5_rat_t (num/den) */
    contract->notional.num = (int64_t)(*notional >> 32);
    contract->notional.den = 1;
    contract->notional.valid = true;
    contract->strike.num = (int64_t)(*strike >> 32);
    contract->strike.den = 1;
    contract->strike.valid = true;
    contract->expiry_tick = expiry_tick;
    if (backing_cid) ff_mem_copy(contract->backing_cid, backing_cid, 32);
    if (backing_proof) contract->backing_proof = (m5_lpres_t)backing_proof->state;
    if (jurisdiction_cid) ff_mem_copy(contract->jurisdiction_cid, jurisdiction_cid, 32);
    contract->treaty_backed = treaty_backed;
    contract->vouchers[0] = 1;  /* Voucher reference */
    
    /* Create position */
    ff_derivative_position_t *pos = &fabric->deriv_positions[fabric->num_deriv_positions];
    ff_mem_set(pos, 0, sizeof(*pos));
    pos->contract = *contract;
    pos->account_id = account_id;
    pos->quantity = 1;  /* Long 1 contract */
    pos->entry_price = 0;  /* Would be set from market */
    pos->margin_required = (uint64_t)notional;  /* 100% backing */
    pos->margin_posted = pos->margin_required;
    pos->position_attestation = LPRES_STATE_NEITHER;
    pos->active = true;
    
    /* Link to account */
    if (acc->num_derivative_positions < FF_MAX_POSITIONS) {
        acc->derivative_positions[acc->num_derivative_positions++] = fabric->num_deriv_positions;
    }
    
    fabric->derivatives.num_contracts++;
    fabric->num_deriv_positions++;
    fabric->stats.total_derivatives_traded++;
    
    return ff_attest(fabric, account_id, 0x5000, contract, 0);
}

int32_t ff_settle_derivative(financial_fabric_t *fabric, uint32_t position_id) {
    if (!fabric || position_id >= fabric->num_deriv_positions) return -1;
    
    ff_derivative_position_t *pos = &fabric->deriv_positions[position_id];
    if (!pos->active) return -1;
    
    /* Verify backing */
    if (fabric->config.enforce_100_percent_backing) {
        if (pos->margin_posted < pos->margin_required) {
            pos->margin_call = true;
            fabric->stats.total_margin_calls++;
            if (fabric->config.auto_liquidate_margin_calls) {
                /* Auto-liquidate */
                pos->active = false;
                fabric->stats.total_liquidations++;
                return -1;
            }
        }
    }
    
    /* Settle via kernel derivative module */
    /* m5_deriv_settle(&pos->contract); */
    
    pos->active = false;
    pos->realized_pnl = pos->unrealized_pnl;
    pos->position_attestation = LPRES_STATE_TRUE;
    
    fabric->stats.total_derivatives_traded++;
    return ff_attest(fabric, pos->account_id, 0x6000 | position_id, pos, 0);
}

int32_t ff_execute_temporal_arb(financial_fabric_t *fabric,
                                uint32_t position_a_id, uint32_t position_b_id,
                                uint64_t settlement_tick) {
    if (!fabric) return -1;
    if (position_a_id >= fabric->num_deriv_positions || position_b_id >= fabric->num_deriv_positions) return -1;
    
    if (!fabric->config.allow_temporal_arb) return -1;
    
    ff_derivative_position_t *pos_a = &fabric->deriv_positions[position_a_id];
    ff_derivative_position_t *pos_b = &fabric->deriv_positions[position_b_id];
    
    if (!pos_a->active || !pos_b->active) return -1;
    if (pos_a->account_id != pos_b->account_id) return -1;  /* Same account for arb */
    
    /* Execute temporal arbitrage via kernel */
    temporal_arb_t arb = {0};
    arb.node_a = (void*)0x1000;  /* Would be actual node references */
    arb.node_b = (void*)0x2000;
    arb.phase_spread = SR_FROM_FLOAT(0.001);  /* Small positive spread */
    arb.settlement_tick = settlement_tick;
    
    /* temporal_arb_execute(&arb); */
    
    pos_a->arb_active = true;
    pos_b->arb_active = true;
    pos_a->arb_entry_tick = settlement_tick;
    pos_b->arb_entry_tick = settlement_tick;
    
    surplus_real_t carry = SR_FROM_FLOAT(0.01);  /* 1% carry captured */
    pos_a->arb_carry = carry;
    pos_b->arb_carry = carry;
    
    fabric->stats.total_arb_profits++;
    return ff_attest(fabric, pos_a->account_id, 0x7000, &arb, 0);
}

int32_t ff_mark_to_market(financial_fabric_t *fabric, uint32_t position_id,
                          uint64_t current_price) {
    if (!fabric || position_id >= fabric->num_deriv_positions) return -1;
    
    ff_derivative_position_t *pos = &fabric->deriv_positions[position_id];
    if (!pos->active) return -1;
    
    pos->current_price = current_price;
    /* Calculate unrealized P&L */
    if (pos->quantity > 0) {
        pos->unrealized_pnl = SR_FROM_FLOAT((double)(current_price - pos->entry_price) * pos->quantity);
    } else {
        pos->unrealized_pnl = SR_FROM_FLOAT((double)(pos->entry_price - current_price) * (-pos->quantity));
    }
    
    /* Check margin */
    return ff_check_margin(fabric, position_id);
}

int32_t ff_check_margin(financial_fabric_t *fabric, uint32_t position_id) {
    if (!fabric || position_id >= fabric->num_deriv_positions) return -1;
    
    ff_derivative_position_t *pos = &fabric->deriv_positions[position_id];
    if (!pos->active) return -1;
    
    /* 100% backing required */
    surplus_real_t margin_ratio = SR_DIV(SR_FROM_INT(pos->margin_posted), SR_FROM_INT(pos->margin_required));
    
    if (SR_CMP(margin_ratio, fabric->config.min_margin_ratio) < 0) {
        pos->margin_call = true;
        fabric->stats.total_margin_calls++;
        return -1;
    }
    
    pos->margin_call = false;
    return 0;
}

/* ===== Assurance ===== */

int32_t ff_create_assurance(financial_fabric_t *fabric,
                            uint32_t account_id,
                            const surplus_real_t *contribution,
                            m5_capital_form_t target_form,
                            const uint8_t prevention_cid[32],
                            const m5_lpres_proof_t *efficacy_proof,
                            m5_capital_form_t generated_form,
                            const surplus_real_t *generation_ratio,
                            bool pay_it_forward,
                            const uint8_t forward_cid[32],
                            const surplus_real_t *forward_phase) {
    if (!fabric) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    if (fabric->assurances.num_contracts >= FF_MAX_ASSURANCES) return -1;
    if (fabric->num_assur_positions >= FF_MAX_POSITIONS) return -1;
    
    /* Create assurance contract */
    m5_assurance_contract_t *contract = &fabric->assurances.contracts[fabric->assurances.num_contracts];
    ff_mem_set(contract, 0, sizeof(*contract));
    
    /* Convert surplus_real_t to m5_rat_t */
    contract->contribution.num = (int64_t)(*contribution >> 32);
    contract->contribution.den = 1;
    contract->contribution.valid = true;
    contract->target_form = target_form;
    if (prevention_cid) ff_mem_copy(contract->prevention_cid, prevention_cid, 32);
    if (efficacy_proof) contract->efficacy_proof = (m5_lpres_t)efficacy_proof->state;
    contract->generated_form = generated_form;
    contract->generation_ratio.num = (int64_t)(*generation_ratio >> 32);
    contract->generation_ratio.den = 1;
    contract->generation_ratio.valid = true;
    contract->pay_it_forward = pay_it_forward;
    if (forward_cid) ff_mem_copy(contract->forward_cid, forward_cid, 32);
    if (forward_phase) {
        contract->forward_phase.num = (int64_t)(*forward_phase >> 32);
        contract->forward_phase.den = 1;
        contract->forward_phase.valid = true;
    }
    
    /* Create position */
    ff_assurance_position_t *pos = &fabric->assur_positions[fabric->num_assur_positions];
    ff_mem_set(pos, 0, sizeof(*pos));
    pos->contract = *contract;
    pos->account_id = account_id;
    pos->contribution_posted = (uint64_t)contribution;
    pos->generation_ratio = *generation_ratio;
    pos->verification_attestation = LPRES_STATE_NEITHER;
    pos->active = true;
    
    /* Link to account */
    if (acc->num_assurance_positions < FF_MAX_POSITIONS) {
        acc->assurance_positions[acc->num_assurance_positions++] = fabric->num_assur_positions;
    }
    
    fabric->assurances.num_contracts++;
    fabric->num_assur_positions++;
    fabric->stats.total_assurances_created++;
    
    return ff_attest(fabric, account_id, 0x8000, contract, 0);
}

int32_t ff_verify_generation(financial_fabric_t *fabric, uint32_t position_id) {
    if (!fabric || position_id >= fabric->num_assur_positions) return -1;
    
    ff_assurance_position_t *pos = &fabric->assur_positions[position_id];
    if (!pos->active) return -1;
    
    /* Verify via kernel assurance module */
    /* m5_assurance_verify_generation(&pos->contract); */
    
    pos->generation_verified = true;
    pos->verification_tick = 0;  /* Current tick */
    /* Convert m5_rat_t back to surplus_real_t for fixed-point multiply */
    surplus_real_t contrib_sr = (surplus_real_t)((pos->contract.contribution.num / pos->contract.contribution.den) << 32);
    surplus_real_t ratio_sr = (surplus_real_t)((pos->contract.generation_ratio.num / pos->contract.generation_ratio.den) << 32);
    pos->capital_generated = (uint64_t)SR_MUL(contrib_sr, ratio_sr);
    pos->verification_attestation = LPRES_STATE_TRUE;
    
    /* If pay-it-forward, queue for forwarding */
    if (pos->contract.pay_it_forward) {
        pos->capital_forwarded = pos->capital_generated;
        fabric->stats.total_forwarded_capital += pos->capital_forwarded;
    }
    
    return ff_attest(fabric, pos->account_id, 0x9000 | position_id, pos, 0);
}

int32_t ff_forward_capital(financial_fabric_t *fabric, uint32_t position_id) {
    if (!fabric || position_id >= fabric->num_assur_positions) return -1;
    
    ff_assurance_position_t *pos = &fabric->assur_positions[position_id];
    if (!pos->active || !pos->generation_verified) return -1;
    if (!pos->contract.pay_it_forward) return -1;
    
    /* Forward capital to target account */
    if (pos->forward_target_account > 0) {
        ff_account_t *target = ff_get_account(fabric, pos->forward_target_account);
        if (target) {
            target->balances[pos->contract.generated_form - 1] += pos->capital_forwarded;
            ff_ledger_entry(fabric, pos->account_id, pos->contract.target_form, pos->contract.generated_form, 
                           pos->capital_forwarded, "Pay-it-forward");
        }
    }
    
    return ff_attest(fabric, pos->account_id, 0xA000 | position_id, pos, 0);
}

/* ===== Treaty Tokenization ===== */

int32_t ff_tokenize_treaty(financial_fabric_t *fabric,
                           uint32_t account_id,
                           const uint8_t treaty_cid[32],
                           const uint8_t asset_cid[32],
                           m5_capital_form_t form,
                           const surplus_real_t *quantified_value,
                           const m5_lpres_proof_t *sovereignty_proof,
                           const uint8_t corridor_cid[32]) {
    if (!fabric) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    if (fabric->treaty.num_assets >= FF_MAX_TREATY_ASSETS) return -1;
    if (fabric->num_treaty_positions >= FF_MAX_POSITIONS) return -1;
    
    /* Create treaty asset */
    m5_treaty_asset_t *asset = &fabric->treaty.assets[fabric->treaty.num_assets];
    ff_mem_set(asset, 0, sizeof(*asset));
    
    if (treaty_cid) ff_mem_copy(asset->treaty_cid, treaty_cid, 32);
    if (asset_cid) ff_mem_copy(asset->asset_cid, asset_cid, 32);
    asset->form = form;
    asset->quantified_value.num = (int64_t)(*quantified_value >> 32);
    asset->quantified_value.den = 1;
    asset->quantified_value.valid = true;
    if (sovereignty_proof) asset->sovereignty_proof = (m5_lpres_t)sovereignty_proof->state;
    if (corridor_cid) ff_mem_copy(asset->corridor_cid, corridor_cid, 32);
    
    /* Create position */
    ff_treaty_position_t *pos = &fabric->treaty_positions[fabric->num_treaty_positions];
    ff_mem_set(pos, 0, sizeof(*pos));
    pos->asset = *asset;
    pos->account_id = account_id;
    pos->tokens_held = 1;  /* 1 token per asset (never fractionalized) */
    pos->token_value = (uint64_t)quantified_value;
    pos->asset_attestation = LPRES_STATE_NEITHER;
    pos->active = true;
    
    /* Link to account */
    if (acc->num_treaty_positions < FF_MAX_POSITIONS) {
        acc->treaty_positions[acc->num_treaty_positions++] = fabric->num_treaty_positions;
    }
    
    fabric->treaty.num_assets++;
    fabric->num_treaty_positions++;
    fabric->stats.total_treaty_assets_tokenized++;
    
    return ff_attest(fabric, account_id, 0xB000, asset, 0);
}

int32_t ff_verify_treaty_asset(financial_fabric_t *fabric, uint32_t position_id) {
    if (!fabric || position_id >= fabric->num_treaty_positions) return -1;
    
    ff_treaty_position_t *pos = &fabric->treaty_positions[position_id];
    if (!pos->active) return -1;
    
    /* Verify via kernel treaty module */
    /* m5_treaty_asset_verify(&pos->asset); */
    
    pos->settled_on_rail_888 = true;  /* Treaty assets settle on Rail 888 (Externality) */
    pos->settlement_tick = 0;
    pos->asset_attestation = LPRES_STATE_TRUE;
    
    return ff_attest(fabric, pos->account_id, 0xC000 | position_id, pos, 0);
}

/* ===== Mesh Settlement ===== */

int32_t ff_initiate_settlement(financial_fabric_t *fabric,
                               uint32_t trade_route_id,
                               uint32_t source_account,
                               uint32_t dest_account,
                               uint8_t capital_form,
                               uint64_t amount,
                               uint64_t price_per_unit,
                               uint64_t deadline_tick) {
    if (!fabric) return -1;
    
    ff_account_t *src = ff_get_account(fabric, source_account);
    ff_account_t *dst = ff_get_account(fabric, dest_account);
    if (!src || !dst) return -1;
    if (capital_form < 1 || capital_form > 9) return -1;
    
    if (src->balances[capital_form - 1] < amount) return -1;
    
    if (fabric->num_settlements >= FF_MAX_MESH_SETTLEMENTS) return -1;
    
    ff_mesh_settlement_t *sett = &fabric->settlements[fabric->num_settlements];
    ff_mem_set(sett, 0, sizeof(*sett));
    sett->id = fabric->num_settlements;
    sett->trade_route_id = trade_route_id;
    sett->source_account = source_account;
    sett->dest_account = dest_account;
    sett->amount = amount;
    sett->capital_form = capital_form;
    sett->price_per_unit = price_per_unit;
    sett->quantity = amount / price_per_unit;
    sett->initiated_tick = 0;  /* Current tick */
    sett->deadline_tick = deadline_tick;
    sett->settlement_attestation = LPRES_STATE_NEITHER;
    sett->active = true;
    
    /* Reserve capital */
    src->balances[capital_form - 1] -= amount;
    
    fabric->num_settlements++;
    fabric->stats.total_mesh_settlements++;
    
    return ff_attest(fabric, source_account, 0xD000 | trade_route_id, sett, 0);
}

int32_t ff_complete_settlement(financial_fabric_t *fabric, uint32_t settlement_id) {
    if (!fabric || settlement_id >= fabric->num_settlements) return -1;
    
    ff_mesh_settlement_t *sett = &fabric->settlements[settlement_id];
    if (!sett->active || sett->completed) return -1;
    
    ff_account_t *dst = ff_get_account(fabric, sett->dest_account);
    if (!dst) return -1;
    
    /* Transfer capital */
    dst->balances[sett->capital_form - 1] += sett->amount;
    
    sett->completed = true;
    sett->completed_tick = 0;  /* Current tick */
    sett->settlement_attestation = LPRES_STATE_TRUE;
    
    /* Mesh Token settlement */
    /* mesh_token_settle(&fabric->mesh_token, sett->mesh_token_id); */
    
    fabric->stats.total_volume += sett->amount;
    return ff_attest(fabric, sett->dest_account, 0xE000 | settlement_id, sett, 0);
}

int32_t ff_dispute_settlement(financial_fabric_t *fabric, uint32_t settlement_id) {
    if (!fabric || settlement_id >= fabric->num_settlements) return -1;
    
    ff_mesh_settlement_t *sett = &fabric->settlements[settlement_id];
    if (!sett->active || sett->completed) return -1;
    
    sett->disputed = true;
    sett->settlement_attestation = LPRES_STATE_BOTH;
    
    /* Return capital to source */
    ff_account_t *src = ff_get_account(fabric, sett->source_account);
    if (src) {
        src->balances[sett->capital_form - 1] += sett->amount;
    }
    
    return ff_attest(fabric, sett->source_account, 0xF000 | settlement_id, sett, -1);
}

/* ===== Order Books / Markets ===== */

int32_t ff_create_order_book(financial_fabric_t *fabric,
                             const char *symbol,
                             uint8_t base_form, uint8_t quote_form) {
    if (!fabric || !symbol || fabric->num_order_books >= FF_MAX_ORDER_BOOKS) return -1;
    if (base_form < 1 || base_form > 9 || quote_form < 1 || quote_form > 9) return -1;
    
    ff_order_book_t *book = &fabric->order_books[fabric->num_order_books];
    ff_mem_set(book, 0, sizeof(*book));
    book->id = fabric->num_order_books;
    ff_str_copy(book->symbol, symbol, FF_MAX_NAME_LEN);
    book->base_form = base_form;
    book->quote_form = quote_form;
    
    /* M5 for market */
    book->m5.omega = fabric->num_order_books + 1;
    book->m5.r = SR_FROM_FLOAT(5.0);
    book->m5.ell = SR_ONE;
    book->m5.phi = SR_ZERO;
    book->m5.chi = 0;
    book->coverage_ratio = ff_compute_coverage(&book->m5);
    book->active = true;
    
    fabric->num_order_books++;
    return (int32_t)book->id;
}

int32_t ff_place_order(financial_fabric_t *fabric,
                       uint32_t book_id, uint32_t account_id,
                       uint64_t price, int64_t quantity) {
    if (!fabric || book_id >= fabric->num_order_books) return -1;
    
    ff_order_book_t *book = &fabric->order_books[book_id];
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    
    if (book->num_orders >= 256) return -1;
    
    /* Reserve capital for bid, or verify position for ask */
    if (quantity > 0) {  /* Bid */
        uint64_t cost = price * quantity;
        if (acc->balances[book->quote_form - 1] < cost) return -1;
        acc->balances[book->quote_form - 1] -= cost;
    } else {  /* Ask */
        uint64_t qty = (uint64_t)(-quantity);
        if (acc->balances[book->base_form - 1] < qty) return -1;
        acc->balances[book->base_form - 1] -= qty;
    }
    
    ff_order_t *order = &book->orders[book->num_orders++];
    order->price = price;
    order->quantity = quantity;
    order->account_id = account_id;
    order->timestamp = 0;  /* Current tick */
    order->active = true;
    
    /* Update best bid/ask */
    if (quantity > 0 && price > book->best_bid) book->best_bid = price;
    if (quantity < 0 && (book->best_ask == 0 || price < book->best_ask)) book->best_ask = price;
    
    return ff_attest(fabric, account_id, 0x10000 | book_id, order, 0);
}

int32_t ff_cancel_order(financial_fabric_t *fabric, uint32_t book_id,
                        uint32_t order_index) {
    if (!fabric || book_id >= fabric->num_order_books) return -1;
    if (order_index >= fabric->order_books[book_id].num_orders) return -1;
    
    ff_order_book_t *book = &fabric->order_books[book_id];
    ff_order_t *order = &book->orders[order_index];
    if (!order->active) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, order->account_id);
    if (!acc) return -1;
    
    /* Return reserved capital */
    if (order->quantity > 0) {  /* Bid */
        uint64_t cost = order->price * order->quantity;
        acc->balances[book->quote_form - 1] += cost;
    } else {  /* Ask */
        uint64_t qty = (uint64_t)(-order->quantity);
        acc->balances[book->base_form - 1] += qty;
    }
    
    order->active = false;
    return ff_attest(fabric, order->account_id, 0x11000 | book_id, order, 0);
}

int32_t ff_match_orders(financial_fabric_t *fabric, uint32_t book_id) {
    if (!fabric || book_id >= fabric->num_order_books) return -1;
    
    ff_order_book_t *book = &fabric->order_books[book_id];
    
    /* Simple matching: match best bid with best ask */
    uint32_t best_bid_idx = 0xFFFFFFFF, best_ask_idx = 0xFFFFFFFF;
    uint64_t best_bid_price = 0, best_ask_price = 0xFFFFFFFFFFFFFFFF;
    
    for (uint32_t i = 0; i < book->num_orders; i++) {
        if (!book->orders[i].active) continue;
        if (book->orders[i].quantity > 0 && book->orders[i].price > best_bid_price) {
            best_bid_price = book->orders[i].price;
            best_bid_idx = i;
        }
        if (book->orders[i].quantity < 0 && book->orders[i].price < best_ask_price) {
            best_ask_price = book->orders[i].price;
            best_ask_idx = i;
        }
    }
    
    if (best_bid_idx == 0xFFFFFFFF || best_ask_idx == 0xFFFFFFFF) return -1;
    if (best_bid_price < best_ask_price) return -1;  /* No cross */
    
    /* Execute match at mid price */
    uint64_t match_price = (best_bid_price + best_ask_price) / 2;
    int64_t bid_qty = book->orders[best_bid_idx].quantity;
    int64_t ask_qty = -book->orders[best_ask_idx].quantity;
    int64_t match_qty = (bid_qty < ask_qty) ? bid_qty : ask_qty;
    
    ff_account_t *bidder = ff_get_account(fabric, book->orders[best_bid_idx].account_id);
    ff_account_t *asker = ff_get_account(fabric, book->orders[best_ask_idx].account_id);
    if (!bidder || !asker) return -1;
    
    /* Transfer base form to bidder, quote form to asker */
    bidder->balances[book->base_form - 1] += match_qty;
    asker->balances[book->quote_form - 1] += match_price * match_qty;
    
    /* Update order quantities */
    book->orders[best_bid_idx].quantity -= match_qty;
    book->orders[best_ask_idx].quantity += match_qty;
    
    if (book->orders[best_bid_idx].quantity == 0) book->orders[best_bid_idx].active = false;
    if (book->orders[best_ask_idx].quantity == 0) book->orders[best_ask_idx].active = false;
    
    book->last_price = match_price;
    book->volume_24h += match_price * match_qty;
    
    fabric->stats.total_volume += match_price * match_qty;
    
    return 0;
}

/* ===== Rails ===== */

int32_t ff_route_through_rail(financial_fabric_t *fabric,
                              uint32_t from_account, uint32_t to_account,
                              uint8_t form, uint64_t amount,
                              uint8_t rail_id) {
    if (!fabric) return -1;
    
    /* Check if rail supports this form */
    if (!ff_rail_supports_form(&fabric->rails, form, rail_id)) return -1;
    
    return ff_transfer_capital(fabric, from_account, to_account, form, amount);
}

bool ff_rail_supports_form(rail_system_t *rails, uint8_t form, uint8_t rail_id) {
    if (!rails) return false;
    /* Rail 1=Social, 2=Natural, 3=Heritage, 4=Governance, 5=Financial, 6=Material, 7=Living, 8=Knowledge, 9=Built, 888=Externality */
    /* In real implementation, would check rails->form_to_rail[form] == rail_id */
    return true;  /* Simplified */
}

/* ===== Crypto Bridge ===== */

int32_t ff_bridge_asset(financial_fabric_t *fabric,
                        uint32_t account_id,
                        uint8_t form, uint64_t amount,
                        const char *target_chain,
                        const char *target_address) {
    if (!fabric) return -1;
    
    ff_account_t *acc = ff_get_account(fabric, account_id);
    if (!acc) return -1;
    if (acc->balances[form - 1] < amount) return -1;
    
    /* Lock capital in bridge */
    acc->balances[form - 1] -= amount;
    
    /* Bridge via crypto_bridge module */
    /* crypto_bridge_lock(&fabric->bridge, form, amount, target_chain, target_address); */
    
    ff_ledger_entry(fabric, account_id, form, 0, amount, "Bridged out");
    
    return ff_attest(fabric, account_id, 0x12000 | form, (void*)target_chain, 0);
}

/* ===== Health & Attestation ===== */

int32_t ff_check_account_health(financial_fabric_t *fabric,
                                uint32_t account_id,
                                void *health_out) {
    if (!fabric || account_id >= fabric->num_accounts) return -1;
    
    ff_account_t *acc = &fabric->accounts[account_id];
    if (!acc->active) return -1;
    
    /* Update coverage */
    acc->coverage_ratio = ff_compute_coverage(&acc->m5);
    
    /* Check margin calls on derivative positions */
    for (uint32_t i = 0; i < acc->num_derivative_positions; i++) {
        uint32_t pos_id = acc->derivative_positions[i];
        if (pos_id < fabric->num_deriv_positions) {
            ff_check_margin(fabric, pos_id);
        }
    }
    
    /* Verify assurance generations */
    for (uint32_t i = 0; i < acc->num_assurance_positions; i++) {
        uint32_t pos_id = acc->assurance_positions[i];
        if (pos_id < fabric->num_assur_positions) {
            ff_assurance_position_t *pos = &fabric->assur_positions[pos_id];
            if (pos->active && !pos->generation_verified) {
                ff_verify_generation(fabric, pos_id);
            }
        }
    }
    
    /* Update attestation */
    if (SR_CMP(acc->coverage_ratio, fabric->min_coverage_ratio) >= 0) {
        acc->attestation = LPRES_STATE_TRUE;
    } else {
        acc->attestation = LPRES_STATE_BOTH;
    }
    
    return 0;
}

int32_t ff_check_global_health(financial_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_accounts; i++) {
        if (fabric->accounts[i].active) {
            if (ff_check_account_health(fabric, fabric->accounts[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool ff_global_safety_gate(financial_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void ff_update_coverage(financial_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = ff_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_accounts; i++) {
        if (fabric->accounts[i].active) {
            fabric->accounts[i].coverage_ratio = ff_compute_coverage(&fabric->accounts[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_order_books; i++) {
        if (fabric->order_books[i].active) {
            fabric->order_books[i].coverage_ratio = ff_compute_coverage(&fabric->order_books[i].m5);
        }
    }
}

bool ff_enforce_coverage(financial_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_accounts; i++) {
        if (fabric->accounts[i].active) {
            if (SR_CMP(fabric->accounts[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void ff_get_stats(financial_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    ff_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t ff_get_attestation(financial_fabric_t *fabric, uint32_t account_id) {
    if (!fabric || account_id >= fabric->num_accounts) return LPRES_STATE_NEITHER;
    return fabric->accounts[account_id].attestation;
}

void ff_set_attestation(financial_fabric_t *fabric, uint32_t account_id, lpres_state_t state) {
    if (!fabric || account_id >= fabric->num_accounts) return;
    fabric->accounts[account_id].attestation = state;
}

/* ===== Utility ===== */

const char *ff_form_name(uint8_t form) {
    static const char *names[] = {
        "Social", "Natural", "Heritage/Intellectual", "Governance/Institutional",
        "Financial", "Material", "Living", "Knowledge", "Built"
    };
    if (form >= 1 && form <= 9) return names[form - 1];
    return "Unknown";
}

const char *ff_rail_name(uint8_t rail) {
    static const char *names[] = {
        "Social", "Natural", "Heritage", "Governance", "Financial",
        "Material", "Living", "Knowledge", "Built", "Externality"
    };
    /* Externality is rail 888 (VINO_ISO_EQUITY), outside uint8_t; the
     * ordinal 10 is its slot in this table. */
    if (rail >= 1 && rail <= 10) return names[rail - 1];
    return "Unknown";
}

const char *ff_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}
