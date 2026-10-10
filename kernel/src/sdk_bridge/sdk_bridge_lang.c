/* sdk_bridge_lang.c — SDK Bridge Language Bindings Implementation
 *
 * Executes kernel capabilities from any Orbital Compat language
 * through the canonical IR.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "sdk_bridge_lang.h"
#include "sdk_bridge.h"
#include "financial_fabric.h"
#include "crypto_wallet.h"
#include "mesh_net.h"
#include "crypto_bridge.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* The bridge these ops act on. It used to be "((sdk_bridge_t*)0)->field", a
 * NULL dereference on every call. Unset (NULL) means every op fails closed
 * with SB_LANG_ERR_INTERNAL. */
static sdk_bridge_t *sb_lang_bridge = 0;

void sb_lang_set_bridge(sdk_bridge_t *bridge)
{
    sb_lang_bridge = bridge;
}

/* true when the IR does not carry the n fields an op reads. oc_ir_t holds
 * only OC_MAX_FIELDS fields, so ops that need more always fail (they used to
 * read past the array). num_fields is caller-set, so it is bounded too. */
#define SB_LANG_NEED(in, n)                                                                        \
    ((in)->num_fields < (n) || (in)->num_fields > OC_MAX_FIELDS || (n) > OC_MAX_FIELDS)

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/* sb_lang_context_t is NOT layout-compatible with sb_context_t (the latter
 * has thread_id before identity and many more fields), so the old
 * (sb_context_t *)ctx casts read capabilities at the wrong offset and let
 * the bridge write past the end of the language context. Calls into the
 * bridge now go through a real sb_context_t built from the language
 * context, and the fields the bridge may change are copied back. */
static void sb_lang_to_ctx(const sb_lang_context_t *l, sb_context_t *c)
{
    uint8_t *p = (uint8_t *) c;
    for (uint32_t i = 0; i < sizeof *c; i++) p[i] = 0;
    c->process_id = l->process_id;
    c->identity = l->identity;
    for (uint32_t i = 0; i < SB_MAX_CAPABILITIES; i++) c->capabilities[i] = l->capabilities[i];
    c->num_capabilities = l->num_capabilities;
    c->m5 = l->m5;
    c->coverage_ratio = l->coverage_ratio;
    c->process_attestation = l->attestation;
    c->financial_account_id = l->financial_account_id;
    c->initialized = true;
    c->active = l->active;
}

static void sb_lang_from_ctx(sb_lang_context_t *l, const sb_context_t *c)
{
    for (uint32_t i = 0; i < SB_MAX_CAPABILITIES; i++) l->capabilities[i] = c->capabilities[i];
    l->num_capabilities = c->num_capabilities;
    l->m5 = c->m5;
    l->coverage_ratio = c->coverage_ratio;
    l->attestation = c->process_attestation;
    l->active = c->active;
}

static uint32_t g_op_counter = 1;

static uint32_t sb_lang_next_op_id(void) {
    return g_op_counter++;
}

static surplus_real_t sb_lang_compute_coverage(const m5_coords_t *m5) {
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


/* ============================================================================
 * FINANCIAL FABRIC OPERATIONS
 * ============================================================================ */

static int32_t sb_lang_ff_create_account(sb_lang_context_t *ctx,
                                         const oc_ir_t *input,
                                         oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CAPITAL_TRANSFER)) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: name[64], owner_id[21], initial_balances[9] */
    if (SB_LANG_NEED(input, 95)) return SB_LANG_ERR_ARG;
    char name[64];
    for (int i = 0; i < 64; i++) name[i] = (char)input->fields[1 + i].num.num;
    word168_t owner_id;
    for (int i = 0; i < 21; i++) owner_id.bytes[i] = (uint8_t)input->fields[65 + i].num.num;
    uint64_t initial_balances[9];
    for (int i = 0; i < 9; i++) initial_balances[i] = (uint64_t)input->fields[86 + i].num.num;
    
    int32_t acct_id = ff_create_account(ff, name, &owner_id, initial_balances);
    
    rat_t result = rat_from_int(acct_id);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_CREATE_ACCOUNT, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_ff_create_derivative(sb_lang_context_t *ctx,
                                            const oc_ir_t *input,
                                            oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_DERIVATIVES_TRADE)) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: account_id, underlying_form, notional_num, notional_den, 
     *       strike_num, strike_den, expiry_tick, backing_cid[32], 
     *       treaty_backed */
    if (SB_LANG_NEED(input, 39)) return SB_LANG_ERR_ARG;

    uint32_t account_id = (uint32_t)input->fields[1].num.num;
    m5_capital_form_t underlying = (m5_capital_form_t)input->fields[2].num.num;
    rat_t notional = input->fields[3].num;
    rat_t strike = input->fields[4].num;
    uint64_t expiry = (uint64_t)input->fields[5].num.num;
    uint8_t backing_cid[32];
    for (int i = 0; i < 32; i++) backing_cid[i] = (uint8_t)input->fields[6 + i].num.num;
    bool treaty_backed = input->fields[38].num.num != 0;
    
    surplus_real_t notional_sr = rat_to_surplus(notional);
    surplus_real_t strike_sr = rat_to_surplus(strike);
    
    int32_t pos_id = ff_create_derivative(ff, account_id, underlying,
                                          &notional_sr, &strike_sr, expiry,
                                          backing_cid, NULL, NULL, treaty_backed);
    
    rat_t result = rat_from_int(pos_id);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_CREATE_DERIVATIVE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_ff_post_quote(sb_lang_context_t *ctx,
                                     const oc_ir_t *input,
                                     oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CAPITAL_TRANSFER)) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: inst_id, price_num, price_den, phase_tick */
    if (SB_LANG_NEED(input, 5)) return SB_LANG_ERR_ARG;
    uint32_t inst_id = (uint32_t)input->fields[1].num.num;
    rat_t price = input->fields[2].num;
    uint32_t phase_tick = (uint32_t)input->fields[3].num.num;
    
    surplus_real_t price_sr = rat_to_surplus(price);
    int32_t rc = fm_post_quote(&ff->markets, inst_id, price_sr, phase_tick);
    
    rat_t result = rat_from_int(rc);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_POST_QUOTE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_ff_value_position(sb_lang_context_t *ctx,
                                         const oc_ir_t *input,
                                         oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: inst_id, qty_num, qty_den */
    if (SB_LANG_NEED(input, 4)) return SB_LANG_ERR_ARG;
    uint32_t inst_id = (uint32_t)input->fields[1].num.num;
    rat_t qty = input->fields[2].num;
    
    surplus_real_t qty_sr = rat_to_surplus(qty);
    surplus_real_t value = fm_value_position(&ff->markets, inst_id, qty_sr);
    
    rat_t result = surplus_to_rat(value);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_VALUE_POSITION, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_ff_open_position(sb_lang_context_t *ctx,
                                        const oc_ir_t *input,
                                        oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CAPITAL_TRANSFER)) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: acct, inst_id, qty_num, qty_den, cost_basis_num, cost_basis_den */
    if (SB_LANG_NEED(input, 7)) return SB_LANG_ERR_ARG;
    uint32_t acct = (uint32_t)input->fields[1].num.num;
    uint32_t inst_id = (uint32_t)input->fields[2].num.num;
    rat_t qty = input->fields[3].num;
    rat_t cost_basis = input->fields[4].num;
    
    surplus_real_t qty_sr = rat_to_surplus(qty);
    surplus_real_t cost_sr = rat_to_surplus(cost_basis);
    
    int32_t rc = fm_open_position(&ff->markets, acct, inst_id, qty_sr, cost_sr);
    
    rat_t result = rat_from_int(rc);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_OPEN_POSITION, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_ff_settle(sb_lang_context_t *ctx,
                                 const oc_ir_t *input,
                                 oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CAPITAL_TRANSFER)) return SB_LANG_ERR_CAP;

    financial_fabric_t *ff = sb_lang_bridge ? sb_lang_bridge->financial : 0;
    if (!ff) return SB_LANG_ERR_INTERNAL;
    
    /* Args: from_acct, to_acct, amount_num, amount_den, fee_bps */
    if (SB_LANG_NEED(input, 6)) return SB_LANG_ERR_ARG;
    uint32_t from_acct = (uint32_t)input->fields[1].num.num;
    uint32_t to_acct = (uint32_t)input->fields[2].num.num;
    rat_t amount = input->fields[3].num;
    uint16_t fee_bps = (uint16_t)input->fields[4].num.num;
    
    surplus_real_t amount_sr = rat_to_surplus(amount);
    int32_t rc = fm_settle(&ff->markets, from_acct, to_acct, amount_sr, fee_bps);
    
    rat_t result = rat_from_int(rc);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_FF_SETTLE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * CRYPTO WALLET OPERATIONS
 * ============================================================================ */

static int32_t sb_lang_cw_create_wallet(sb_lang_context_t *ctx,
                                        const oc_ir_t *input,
                                        oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CRYPTO_SIGN)) return SB_LANG_ERR_CAP;

    crypto_wallet_system_t *wallet_sys = sb_lang_bridge ? sb_lang_bridge->crypto_wallet : 0;
    if (!wallet_sys) return SB_LANG_ERR_INTERNAL;
    
    /* Args: label[64] */
    if (SB_LANG_NEED(input, 65)) return SB_LANG_ERR_ARG;
    char label_str[64];
    for (int i = 0; i < 64; i++) label_str[i] = (char)input->fields[1 + i].num.num;
    label_str[63] = 0;

    uint32_t wallet_id = cw_wallet_create(wallet_sys, label_str);
    
    rat_t result = rat_from_int(wallet_id);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_CW_CREATE_WALLET, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_cw_store_file(sb_lang_context_t *ctx,
                                     const oc_ir_t *input,
                                     oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_STORAGE_PERSIST)) return SB_LANG_ERR_CAP;

    crypto_wallet_system_t *wallet_sys = sb_lang_bridge ? sb_lang_bridge->crypto_wallet : 0;
    if (!wallet_sys) return SB_LANG_ERR_INTERNAL;
    
    /* Args: wallet_id, file_type, payload_hash[32], payload_len */
    if (SB_LANG_NEED(input, 36)) return SB_LANG_ERR_ARG;
    uint32_t wallet_id = (uint32_t)input->fields[1].num.num;
    cw_file_type_t file_type = (cw_file_type_t)input->fields[2].num.num;
    uint8_t payload_hash[32];
    for (int i = 0; i < 32; i++) payload_hash[i] = (uint8_t)input->fields[3 + i].num.num;
    uint32_t payload_len = (uint32_t)input->fields[35].num.num;
    
    int32_t rc = cw_file_add(wallet_sys, wallet_id, file_type, payload_hash, payload_len, NULL);
    
    rat_t result = rat_from_int(rc);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_CW_STORE_FILE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * MESH NETWORK OPERATIONS
 * ============================================================================ */

static int32_t sb_lang_mn_create_network(sb_lang_context_t *ctx,
                                         const oc_ir_t *input,
                                         oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_NETWORK_MESH)) return SB_LANG_ERR_CAP;

    mesh_net_t *mesh = sb_lang_bridge ? sb_lang_bridge->mesh : 0;
    if (!mesh) return SB_LANG_ERR_INTERNAL;
    
    /* Args: name_hash, access_type, creator_id[21], creator_trust */
    if (SB_LANG_NEED(input, 25)) return SB_LANG_ERR_ARG;
    uint64_t name_hash = (uint64_t)input->fields[1].num.num;
    mn_net_access_t access = (mn_net_access_t)input->fields[2].num.num;
    word168_t creator_id;
    for (int i = 0; i < 21; i++) creator_id.bytes[i] = (uint8_t)input->fields[3 + i].num.num;
    uint32_t creator_trust = (uint32_t)input->fields[24].num.num;
    
    int32_t net_id = mn_create_network(mesh, (const char*)&name_hash, access, &creator_id, creator_trust);
    
    rat_t result = rat_from_int(net_id);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_MN_CREATE_NETWORK, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_mn_send_message(sb_lang_context_t *ctx,
                                       const oc_ir_t *input,
                                       oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_NETWORK_MESH)) return SB_LANG_ERR_CAP;

    mesh_net_t *mesh = sb_lang_bridge ? sb_lang_bridge->mesh : 0;
    if (!mesh) return SB_LANG_ERR_INTERNAL;
    
    /* Args: route_id, data_size, current_cycle */
    if (SB_LANG_NEED(input, 4)) return SB_LANG_ERR_ARG;
    uint32_t route_id = (uint32_t)input->fields[1].num.num;
    uint64_t data_size = (uint64_t)input->fields[2].num.num;
    uint64_t current_cycle = (uint64_t)input->fields[3].num.num;
    
    int32_t rc = mn_send_data(mesh, route_id, data_size, current_cycle);
    
    rat_t result = rat_from_int(rc);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_MN_SEND_MESSAGE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * RAILS OPERATIONS
 * ============================================================================ */

static int32_t sb_lang_rails_issue_card(sb_lang_context_t *ctx,
                                        const oc_ir_t *input,
                                        oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CAPITAL_TRANSFER)) return SB_LANG_ERR_CAP;

    /* Would call rail_issue_card from rails.h */
    rat_t result = rat_from_int(0xFFFFFFFF);  /* Placeholder */
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_RAILS_ISSUE_CARD, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * CRYPTO BRIDGE OPERATIONS
 * ============================================================================ */

static int32_t sb_lang_bridge_create(sb_lang_context_t *ctx,
                                     const oc_ir_t *input,
                                     oc_ir_t *output) {
    if (!ctx || !ctx->active) return SB_LANG_ERR_CAP;
    if (!sb_lang_has_capability(ctx, SB_CAP_CRYPTO_SIGN)) return SB_LANG_ERR_CAP;

    bridge_registry_t *bridge =
        (sb_lang_bridge && sb_lang_bridge->financial) ? &sb_lang_bridge->financial->bridge : 0;
    if (!bridge) return SB_LANG_ERR_INTERNAL;
    
    /* Args: source_chain, dest_chain, direction, amount_num, amount_den, 
     *       source_addr, dest_addr, token_type, lang, symbol_hash, contract_addr_hash */
    if (SB_LANG_NEED(input, 12)) return SB_LANG_ERR_ARG;

    chain_id_t source = (chain_id_t)input->fields[1].num.num;
    chain_id_t dest = (chain_id_t)input->fields[2].num.num;
    bridge_direction_t dir = (bridge_direction_t)input->fields[3].num.num;
    rat_t amount = input->fields[4].num;
    uint64_t source_addr = (uint64_t)input->fields[5].num.num;
    uint64_t dest_addr = (uint64_t)input->fields[6].num.num;
    token_standard_t token_type = (token_standard_t)input->fields[7].num.num;
    contract_lang_t lang = (contract_lang_t)input->fields[8].num.num;
    uint64_t symbol_hash = (uint64_t)input->fields[9].num.num;
    uint64_t contract_hash = (uint64_t)input->fields[10].num.num;
    
    surplus_real_t amount_sr = rat_to_surplus(amount);
    uint32_t bridge_id = bridge_create(bridge, source, dest, dir, amount_sr,
                                       source_addr, dest_addr, token_type, lang,
                                       (const char*)&symbol_hash, (const char*)&contract_hash);
    
    rat_t result = rat_from_int(bridge_id);
    oc_ir_t out = {0};
    sb_lang_build_op(&out, SB_OP_BRIDGE_CREATE, &result, 1);
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * SELF-AUDIT / SELF-HEAL
 * ============================================================================ */

static int32_t sb_lang_self_audit_op(sb_lang_context_t *ctx,
                                     const oc_ir_t *input,
                                     oc_ir_t *output) {
    if (!ctx) return SB_LANG_ERR_ARG;

    sdk_bridge_t *bridge = sb_lang_bridge;
    if (!bridge) return SB_LANG_ERR_INTERNAL;

    sb_context_t bc;
    sb_lang_to_ctx(ctx, &bc);
    sb_result_t result = sb_self_audit_context(bridge, &bc);
    sb_lang_from_ctx(ctx, &bc);

    rat_t r_result = rat_from_int(result.code);
    rat_t r_att = rat_from_int(result.attestation);
    rat_t r_cov = surplus_to_rat(result.coverage_ratio);
    rat_t r_opid = rat_from_int(result.op_id);
    
    oc_ir_t out = {0};
    out.num_fields = 5;
    out.fields[0].type = OC_TYPE_RATIONAL; out.fields[0].num = r_result; out.fields[0].scale = 0;
    out.fields[1].type = OC_TYPE_RATIONAL; out.fields[1].num = r_att; out.fields[1].scale = 0;
    out.fields[2].type = OC_TYPE_RATIONAL; out.fields[2].num = r_cov; out.fields[2].scale = 0;
    out.fields[3].type = OC_TYPE_RATIONAL; out.fields[3].num = r_opid; out.fields[3].scale = 0;
    out.fields[4].type = OC_TYPE_RATIONAL; out.fields[4].num = rat_from_int(SB_OP_SELF_AUDIT); out.fields[4].scale = 0;
    
    *output = out;
    return SB_LANG_OK;
}

static int32_t sb_lang_self_heal_op(sb_lang_context_t *ctx,
                                    const oc_ir_t *input,
                                    oc_ir_t *output) {
    if (!ctx) return SB_LANG_ERR_ARG;

    sdk_bridge_t *bridge = sb_lang_bridge;
    if (!bridge) return SB_LANG_ERR_INTERNAL;

    sb_context_t bc;
    sb_lang_to_ctx(ctx, &bc);
    sb_result_t result = sb_self_heal_context(bridge, &bc);
    sb_lang_from_ctx(ctx, &bc);

    rat_t r_result = rat_from_int(result.code);
    rat_t r_att = rat_from_int(result.attestation);
    rat_t r_cov = surplus_to_rat(result.coverage_ratio);
    rat_t r_opid = rat_from_int(result.op_id);
    
    oc_ir_t out = {0};
    out.num_fields = 5;
    out.fields[0].type = OC_TYPE_RATIONAL; out.fields[0].num = r_result; out.fields[0].scale = 0;
    out.fields[1].type = OC_TYPE_RATIONAL; out.fields[1].num = r_att; out.fields[1].scale = 0;
    out.fields[2].type = OC_TYPE_RATIONAL; out.fields[2].num = r_cov; out.fields[2].scale = 0;
    out.fields[3].type = OC_TYPE_RATIONAL; out.fields[3].num = r_opid; out.fields[3].scale = 0;
    out.fields[4].type = OC_TYPE_RATIONAL; out.fields[4].num = rat_from_int(SB_OP_SELF_HEAL); out.fields[4].scale = 0;
    
    *output = out;
    return SB_LANG_OK;
}

/* ============================================================================
 * MAIN DISPATCHER
 * ============================================================================ */

int32_t sb_lang_execute(sb_lang_context_t *ctx,
                        const oc_ir_t *input,
                        oc_ir_t *output) {
    if (!ctx || !input || !output) return SB_LANG_ERR_ARG;
    if (!ctx->active) return SB_LANG_ERR_CAP;
    
    /* Check M5 coverage */
    ctx->coverage_ratio = sb_lang_compute_coverage(&ctx->m5);
    sdk_bridge_t *bridge = sb_lang_bridge;
    if (bridge && SR_CMP(ctx->coverage_ratio, bridge->min_global_coverage) < 0) {
        return SB_LANG_ERR_COVERAGE;
    }
    
    /* Check LPRES attestation */
    if ((int)ctx->attestation < (int)LPRES_STATE_TRUE) {
        return SB_LANG_ERR_AUDIT;
    }
    
    /* Extract operation code */
    if (input->num_fields < 1 || input->fields[0].type != OC_TYPE_RATIONAL) {
        return SB_LANG_ERR_ARG;
    }
    if (!rat_is_int(input->fields[0].num)) return SB_LANG_ERR_ARG;
    
    sb_lang_op_t op = (sb_lang_op_t)input->fields[0].num.num;
    uint32_t op_id = sb_lang_next_op_id();
    
    int32_t rc = SB_LANG_ERR_INTERNAL;
    
    switch (op) {
        /* Financial Fabric */
        case SB_OP_FF_CREATE_ACCOUNT:
            rc = sb_lang_ff_create_account(ctx, input, output);
            break;
        case SB_OP_FF_CREATE_DERIVATIVE:
            rc = sb_lang_ff_create_derivative(ctx, input, output);
            break;
        case SB_OP_FF_POST_QUOTE:
            rc = sb_lang_ff_post_quote(ctx, input, output);
            break;
        case SB_OP_FF_VALUE_POSITION:
            rc = sb_lang_ff_value_position(ctx, input, output);
            break;
        case SB_OP_FF_OPEN_POSITION:
            rc = sb_lang_ff_open_position(ctx, input, output);
            break;
        case SB_OP_FF_SETTLE:
            rc = sb_lang_ff_settle(ctx, input, output);
            break;
            
        /* Crypto Wallet */
        case SB_OP_CW_CREATE_WALLET:
            rc = sb_lang_cw_create_wallet(ctx, input, output);
            break;
        case SB_OP_CW_STORE_FILE:
            rc = sb_lang_cw_store_file(ctx, input, output);
            break;
            
        /* Mesh Network */
        case SB_OP_MN_CREATE_NETWORK:
            rc = sb_lang_mn_create_network(ctx, input, output);
            break;
        case SB_OP_MN_SEND_MESSAGE:
            rc = sb_lang_mn_send_message(ctx, input, output);
            break;
            
        /* Rails */
        case SB_OP_RAILS_ISSUE_CARD:
            rc = sb_lang_rails_issue_card(ctx, input, output);
            break;
            
        /* Crypto Bridge */
        case SB_OP_BRIDGE_CREATE:
            rc = sb_lang_bridge_create(ctx, input, output);
            break;
            
        /* Self-Audit/Heal */
        case SB_OP_SELF_AUDIT:
            rc = sb_lang_self_audit_op(ctx, input, output);
            break;
        case SB_OP_SELF_HEAL:
            rc = sb_lang_self_heal_op(ctx, input, output);
            break;
            
        default:
            rc = SB_LANG_ERR_ARG;
            break;
    }
    
    /* Add metadata to output */
    if (output->num_fields > 0) {
        /* Ensure we have space for metadata */
        if (output->num_fields < 5) {
            for (uint32_t i = output->num_fields; i < 5; i++) {
                output->fields[i].type = OC_TYPE_RATIONAL;
                output->fields[i].num = rat_zero();
                output->fields[i].scale = 0;
            }
            output->num_fields = 5;
        }
        /* Field 2: LPRES attestation */
        output->fields[2].num = rat_from_int((int64_t)ctx->attestation);
        /* Field 3: Coverage ratio */
        output->fields[3].num = surplus_to_rat(ctx->coverage_ratio);
        /* Field 4: Operation ID */
        output->fields[4].num = rat_from_int((int64_t)op_id);
    }
    
    return rc;
}

int32_t sb_lang_create_context(sdk_bridge_t *bridge,
                               const word168_t *identity,
                               sb_lang_context_t *out_ctx) {
    if (!bridge || !identity || !out_ctx) return SB_LANG_ERR_ARG;
    if (!bridge->initialized) return SB_LANG_ERR_INTERNAL;
    
    sb_context_t *ctx = sb_create_context(bridge, identity);
    if (!ctx) return SB_LANG_ERR_INTERNAL;
    
    out_ctx->process_id = ctx->process_id;
    out_ctx->identity = *identity;
    out_ctx->num_capabilities = 0;
    out_ctx->m5 = ctx->m5;
    out_ctx->coverage_ratio = ctx->coverage_ratio;
    out_ctx->attestation = ctx->process_attestation;
    out_ctx->financial_account_id = ctx->financial_account_id;
    out_ctx->active = true;
    
    return SB_LANG_OK;
}

void sb_lang_destroy_context(sb_lang_context_t *ctx) {
    if (!ctx) return;
    ctx->active = false;
    ctx->num_capabilities = 0;
}

bool sb_lang_has_capability(const sb_lang_context_t *ctx, sb_capability_t cap) {
    if (!ctx) return false;
    for (uint32_t i = 0; i < ctx->num_capabilities; i++) {
        if (ctx->capabilities[i] == cap) return true;
    }
    return false;
}

int32_t sb_lang_grant_capability(sdk_bridge_t *bridge,
                                 sb_lang_context_t *ctx,
                                 sb_capability_t cap) {
    if (!bridge || !ctx) return SB_LANG_ERR_ARG;
    sb_context_t bc;
    sb_lang_to_ctx(ctx, &bc);
    int32_t code = sb_grant_capability(bridge, &bc, cap).code;
    sb_lang_from_ctx(ctx, &bc);
    return code;
}

int32_t sb_lang_self_audit(sdk_bridge_t *bridge,
                           sb_lang_context_t *ctx,
                           oc_ir_t *out_report) {
    if (!bridge || !ctx || !out_report) return SB_LANG_ERR_ARG;
    sb_context_t bc;
    sb_lang_to_ctx(ctx, &bc);
    sb_result_t result = sb_self_audit_context(bridge, &bc);
    sb_lang_from_ctx(ctx, &bc);
    return result.code;
}

int32_t sb_lang_self_heal(sdk_bridge_t *bridge,
                          sb_lang_context_t *ctx,
                          oc_ir_t *out_report) {
    if (!bridge || !ctx || !out_report) return SB_LANG_ERR_ARG;
    sb_context_t bc;
    sb_lang_to_ctx(ctx, &bc);
    sb_result_t result = sb_self_heal_context(bridge, &bc);
    sb_lang_from_ctx(ctx, &bc);
    return result.code;
}
