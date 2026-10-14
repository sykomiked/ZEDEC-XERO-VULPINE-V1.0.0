// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.MemoryLayout;
import java.lang.invoke.MethodHandle;

/**
 * Downcall handles for the ABI 1.5 groups of {@code zxv_api.h}: treaty,
 * cid/aipi, coin/token, fee, vhub, freight, web4 and paradox. They live apart
 * from {@link Native} only to keep each file readable; they share its symbol
 * lookup and its version gate (initializing this class initializes
 * {@link Native} first). Internal: the public API never exposes these.
 *
 * <p>Layouts follow {@link Native}: {@code I} is int32 / uint32, {@code L}
 * is int64 / uint64 / size_t, {@code A} is a pointer, and by-value
 * {@code uint8_t} / {@code uint16_t} are passed as a 32-bit int already in
 * range ({@code U8}, {@code U16}), never as a signed Java byte or short.
 */
public final class Native15 {
    private Native15() {}

    private static final MemoryLayout I = JAVA_INT;
    private static final MemoryLayout L = JAVA_LONG;
    private static final MemoryLayout A = ADDRESS;
    private static final MemoryLayout U8 = JAVA_INT;
    private static final MemoryLayout U16 = JAVA_INT;

    private static MethodHandle fn(String name, MemoryLayout ret, MemoryLayout... args) {
        return Native.fn(name, ret, args);
    }

    // ---- features2
    public static final MethodHandle features2 = fn("zxv_features2", I, A);

    // ---- treaty
    public static final MethodHandle treaty_create = fn("zxv_treaty_create", I, A);
    public static final MethodHandle treaty_destroy = fn("zxv_treaty_destroy", null, A);
    public static final MethodHandle treaty_clear = fn("zxv_treaty_clear", I, A);
    public static final MethodHandle treaty_parse_sutra = fn("zxv_treaty_parse_sutra", I, A, A, A, A);
    public static final MethodHandle treaty_parse_record = fn("zxv_treaty_parse_record", I, A, A, L, A, A);
    public static final MethodHandle treaty_emit_sutra = fn("zxv_treaty_emit_sutra", I, A, A, L, A);
    public static final MethodHandle treaty_emit_record = fn("zxv_treaty_emit_record", I, A, A, L, A);
    public static final MethodHandle treaty_validate = fn("zxv_treaty_validate", I, A, A);
    public static final MethodHandle treaty_equal = fn("zxv_treaty_equal", I, A, A, A);
    public static final MethodHandle treaty_name = fn("zxv_treaty_name", I, A, A, L, A);
    public static final MethodHandle treaty_asset = fn("zxv_treaty_asset", I, A, A, L, A);
    public static final MethodHandle treaty_header = fn("zxv_treaty_header", I, A, A, A, A);
    public static final MethodHandle treaty_party_id = fn("zxv_treaty_party_id", I, A, I, A, L, A);
    public static final MethodHandle treaty_party = fn("zxv_treaty_party", I, A, I, A, A, A, A);
    public static final MethodHandle treaty_owe = fn("zxv_treaty_owe", I, A, I, A, A, A);
    public static final MethodHandle treaty_pic_add = fn("zxv_treaty_pic_add", I, L, L, A);
    public static final MethodHandle treaty_pic_sub = fn("zxv_treaty_pic_sub", I, L, L, A);
    public static final MethodHandle treaty_pic_mul = fn("zxv_treaty_pic_mul", I, L, L, A);
    public static final MethodHandle treaty_pic_div = fn("zxv_treaty_pic_div", I, L, L, A, A);
    public static final MethodHandle treaty_run = fn("zxv_treaty_run", I, A, A);
    public static final MethodHandle treaty_result_destroy = fn("zxv_treaty_result_destroy", null, A);
    public static final MethodHandle treaty_result_summary = fn("zxv_treaty_result_summary", I, A, A, A, A);
    public static final MethodHandle treaty_result_party = fn("zxv_treaty_result_party", I, A, I, A, A, A);
    public static final MethodHandle treaty_result_leg = fn("zxv_treaty_result_leg", I, A, I, A, A, A);
    public static final MethodHandle treaty_result_record = fn("zxv_treaty_result_record", I, A, A, L, A);

    // ---- cid
    public static final MethodHandle cid_compute = fn("zxv_cid_compute", I, I, A, L, A, A);
    public static final MethodHandle cid_unixfs = fn("zxv_cid_unixfs", I, A, L, A, A);
    public static final MethodHandle cid_verify = fn("zxv_cid_verify", I, A, L, A, L);
    public static final MethodHandle cid_to_text = fn("zxv_cid_to_text", I, A, L, I, A, L, A);
    public static final MethodHandle cid_from_text = fn("zxv_cid_from_text", I, A, A, A);
    public static final MethodHandle cid_info = fn("zxv_cid_info", I, A, L, A, A, A, A, A);
    public static final MethodHandle cid_to_v1 = fn("zxv_cid_to_v1", I, A, L, A, A);
    public static final MethodHandle aipi_provider_key = fn("zxv_aipi_provider_key", I, A, L, A);

    // ---- aipi records
    public static final MethodHandle aipi_fn_encode = fn("zxv_aipi_fn_encode", I,
            I, I, I, I, I, I, L, A, L, A, L, A, A, A, A, L, A);
    public static final MethodHandle aipi_fn_decode = fn("zxv_aipi_fn_decode", I, A, L, A, A, A, A, A, A, A);
    public static final MethodHandle aipi_fn_get_cid = fn("zxv_aipi_fn_get_cid", I, A, L, I, A, A);
    public static final MethodHandle aipi_fn_get_name = fn("zxv_aipi_fn_get_name", I, A, L, I, A, L, A);
    public static final MethodHandle aipi_fn_cid = fn("zxv_aipi_fn_cid", I, A, L, A, A);
    public static final MethodHandle aipi_call_encode = fn("zxv_aipi_call_encode", I, A, L, A, L, A, L, A);
    public static final MethodHandle aipi_call_decode = fn("zxv_aipi_call_decode", I, A, L, A, A, A, A);
    public static final MethodHandle aipi_call_cid = fn("zxv_aipi_call_cid", I, A, L, A, L, A, A);
    public static final MethodHandle aipi_claim_encode = fn("zxv_aipi_claim_encode", I, A, L, A, A, L, A, L, A, L, A);
    public static final MethodHandle aipi_claim_decode = fn("zxv_aipi_claim_decode", I, A, L, A, A, A, A, A, A, A);
    // ---- aipi node
    public static final MethodHandle aipi_create = fn("zxv_aipi_create", I, A, L, I, A, A);
    public static final MethodHandle aipi_destroy = fn("zxv_aipi_destroy", null, A);
    public static final MethodHandle aipi_put = fn("zxv_aipi_put", I, A, A, A, L, I, A, A);
    public static final MethodHandle aipi_get = fn("zxv_aipi_get", I, A, A, L, A, L, A);
    public static final MethodHandle aipi_publish_fn = fn("zxv_aipi_publish_fn", I, A, A, A, L, A, A);
    public static final MethodHandle aipi_load_fn = fn("zxv_aipi_load_fn", I, A, A, L, A, L, A);
    public static final MethodHandle aipi_plan = fn("zxv_aipi_plan", I, A, A, L, A, L, I, A);
    public static final MethodHandle aipi_info = fn("zxv_aipi_info", I, A, A, A, A, A);

    // ---- coin and token
    public static final MethodHandle coin_face = fn("zxv_coin_face", I, I, A);
    public static final MethodHandle coin_rung = fn("zxv_coin_rung", I, L, A);
    public static final MethodHandle coin_split = fn("zxv_coin_split", I, L, A, L, A);
    public static final MethodHandle token_face = fn("zxv_token_face", I, I, A, A);
    public static final MethodHandle token_tier = fn("zxv_token_tier", I, L, A);
    public static final MethodHandle token_value = fn("zxv_token_value", I, A, A, A);
    public static final MethodHandle token_canonical = fn("zxv_token_canonical", I, L, I, A);
    public static final MethodHandle token_is_canonical = fn("zxv_token_is_canonical", I, A, I, A);
    public static final MethodHandle token_compress = fn("zxv_token_compress", I, A, I);
    public static final MethodHandle token_fuse = fn("zxv_token_fuse", I, A, I, I);
    public static final MethodHandle token_split = fn("zxv_token_split", I, A, I);
    public static final MethodHandle token_code_bits = fn("zxv_token_code_bits", I, L, A);
    public static final MethodHandle token_code_put = fn("zxv_token_code_put", I, A, L, A, L);
    public static final MethodHandle token_code_get = fn("zxv_token_code_get", I, A, L, A, A);
    public static final MethodHandle token_wallet_create = fn("zxv_token_wallet_create", I, A, I, I, A);
    public static final MethodHandle token_wallet_destroy = fn("zxv_token_wallet_destroy", null, A);
    public static final MethodHandle token_wallet_mint = fn("zxv_token_wallet_mint", I, A, A, L);
    public static final MethodHandle token_wallet_spend = fn("zxv_token_wallet_spend", I, A, A, L);
    public static final MethodHandle token_wallet_transfer = fn("zxv_token_wallet_transfer", I, A, A, A, L);
    public static final MethodHandle token_wallet_set_octane = fn("zxv_token_wallet_set_octane", I, A, A, I);
    public static final MethodHandle token_wallet_info = fn("zxv_token_wallet_info", I, A, A, A, A, A);
    public static final MethodHandle token_wallet_counts = fn("zxv_token_wallet_counts", I, A, A);

    // ---- fee
    public static final MethodHandle fee_assure = fn("zxv_fee_assure", I, L, A);
    public static final MethodHandle fee_assure_carry = fn("zxv_fee_assure_carry", I, L, L, A, A);
    public static final MethodHandle fee_split = fn("zxv_fee_split", I, L, A);
    public static final MethodHandle fee_policy_default = fn("zxv_fee_policy_default", I, A, A, A, A, A, A, A, A);

    // ---- vhub
    public static final MethodHandle vhub_create = fn("zxv_vhub_create", I, A, A, A);
    public static final MethodHandle vhub_destroy = fn("zxv_vhub_destroy", null, A);
    public static final MethodHandle vhub_add_ccy = fn("zxv_vhub_add_ccy", I, A, A, U8, A, U8, A, L, L);
    public static final MethodHandle vhub_ccy_info = fn("zxv_vhub_ccy_info", I, A, A, U8, A, A, A, A, A, A);
    public static final MethodHandle vhub_peg_msg = fn("zxv_vhub_peg_msg", I, U8, L, L, L, A);
    public static final MethodHandle vhub_set_peg = fn("zxv_vhub_set_peg", I, A, A, U8, L, L, L, A);
    public static final MethodHandle vhub_open_account = fn("zxv_vhub_open_account", I, A, A, U8, I, A);
    public static final MethodHandle vhub_issue = fn("zxv_vhub_issue", I, A, A, I, L);
    public static final MethodHandle vhub_balance = fn("zxv_vhub_balance", I, A, A, I, A, A, A);
    public static final MethodHandle vhub_add_limit = fn("zxv_vhub_add_limit", I, A, A, U8, L);
    public static final MethodHandle vhub_next_epoch = fn("zxv_vhub_next_epoch", I, A, A, A);
    public static final MethodHandle vhub_quote = fn("zxv_vhub_quote", I, A, A, U8, U8, L, A, A, A);
    public static final MethodHandle vhub_swap = fn("zxv_vhub_swap", I, A, A, U8, U8, I, I, L, L, A, A);
    public static final MethodHandle vhub_check = fn("zxv_vhub_check", I, A, A, A);

    // ---- freight
    public static final MethodHandle freight_header_init = fn("zxv_freight_header_init", I, L, U8, U8, L, A, L, A);
    public static final MethodHandle freight_header_serialize = fn("zxv_freight_header_serialize", I,
            U8, U8, U16, U16, U16, L, L, A, A);
    public static final MethodHandle freight_header_parse = fn("zxv_freight_header_parse", I,
            A, A, A, A, A, A, A, A, A, A);
    public static final MethodHandle freight_header_check = fn("zxv_freight_header_check", I, A);
    public static final MethodHandle freight_encode = fn("zxv_freight_encode", I, A, A, L, A);
    public static final MethodHandle freight_decode = fn("zxv_freight_decode", I, A, A, L, A, L, A);
    public static final MethodHandle freight_set_count = fn("zxv_freight_set_count", I, L, U8, A);
    public static final MethodHandle freight_split = fn("zxv_freight_split", I, L, U8, U8, L, A, L, I, A, A);
    public static final MethodHandle freight_join_create = fn("zxv_freight_join_create", I, L, A);
    public static final MethodHandle freight_join_destroy = fn("zxv_freight_join_destroy", null, A);
    public static final MethodHandle freight_join_add = fn("zxv_freight_join_add", I, A, A, A, L);
    public static final MethodHandle freight_join_status = fn("zxv_freight_join_status", I, A, A, A, A, A);
    public static final MethodHandle freight_join_result = fn("zxv_freight_join_result", I, A, A, L, A);
    public static final MethodHandle freight_ops_peek = fn("zxv_freight_ops_peek", I, A, L, A, A);
    public static final MethodHandle freight_ops_expand = fn("zxv_freight_ops_expand", I, A, L, L, A, A, A, L, A);
    public static final MethodHandle freight_gen_run = fn("zxv_freight_gen_run", I, U8, A, L, A, L);
    public static final MethodHandle freight_ops_encode = fn("zxv_freight_ops_encode", I, A, L, I, A, L, A);
    public static final MethodHandle freight_ops_bound = fn("zxv_freight_ops_bound", I, L, I, A);
    public static final MethodHandle freight_ops_writer_create = fn("zxv_freight_ops_writer_create", I, L, A, L, A);
    public static final MethodHandle freight_ops_writer_destroy = fn("zxv_freight_ops_writer_destroy", null, A);
    public static final MethodHandle freight_ops_put_literal = fn("zxv_freight_ops_put_literal", I, A, A, L);
    public static final MethodHandle freight_ops_put_copy = fn("zxv_freight_ops_put_copy", I, A, L, L);
    public static final MethodHandle freight_ops_put_cidref = fn("zxv_freight_ops_put_cidref", I, A, A, L, L);
    public static final MethodHandle freight_ops_put_seed = fn("zxv_freight_ops_put_seed", I, A, U8, A, L, L);
    public static final MethodHandle freight_ops_writer_finish = fn("zxv_freight_ops_writer_finish", I, A, A, L, A);

    // ---- web4
    public static final MethodHandle w4_agent_create = fn("zxv_w4_agent_create", I, A, A, A, A, L, A);
    public static final MethodHandle w4_agent_destroy = fn("zxv_w4_agent_destroy", null, A);
    public static final MethodHandle w4_agent_info = fn("zxv_w4_agent_info", I, A, A, A, A);
    public static final MethodHandle w4_agent_card = fn("zxv_w4_agent_card", I, A, A, L, A);
    public static final MethodHandle w4_card_verify = fn("zxv_w4_card_verify", I, A, L, A, A, A, A);
    public static final MethodHandle w4_dir_create = fn("zxv_w4_dir_create", I, I, A);
    public static final MethodHandle w4_dir_destroy = fn("zxv_w4_dir_destroy", null, A);
    public static final MethodHandle w4_dir_add = fn("zxv_w4_dir_add", I, A, A, L);
    public static final MethodHandle w4_dir_find = fn("zxv_w4_dir_find", I, A, A, A, L, A);
    public static final MethodHandle w4_dir_count = fn("zxv_w4_dir_count", I, A, A);
    public static final MethodHandle w4_manifest_create = fn("zxv_w4_manifest_create", I, I, L, L, A);
    public static final MethodHandle w4_manifest_destroy = fn("zxv_w4_manifest_destroy", null, A);
    public static final MethodHandle w4_manifest_add_tool = fn("zxv_w4_manifest_add_tool", I, A, A, L, I, I, I, I);
    public static final MethodHandle w4_manifest_sign = fn("zxv_w4_manifest_sign", I, A, A, A);
    public static final MethodHandle w4_manifest_verify = fn("zxv_w4_manifest_verify", I, A, A, L);
    public static final MethodHandle w4_manifest_encode = fn("zxv_w4_manifest_encode", I, A, A, L, A);
    public static final MethodHandle w4_manifest_decode = fn("zxv_w4_manifest_decode", I, A, L, A);
    public static final MethodHandle w4_manifest_to_json = fn("zxv_w4_manifest_to_json", I, A, A, L, A);
    public static final MethodHandle w4_manifest_info = fn("zxv_w4_manifest_info", I, A, A, A, A, A, A, A, A);
    public static final MethodHandle w4_manifest_tool = fn("zxv_w4_manifest_tool", I, A, I, A, A, A, A, A, A);
    public static final MethodHandle w4_manifest_find = fn("zxv_w4_manifest_find", I, A, A, A);
    public static final MethodHandle w4_action_digest = fn("zxv_w4_action_digest", I, A, A, A, L, A, L, A);
    public static final MethodHandle w4_consent_issue = fn("zxv_w4_consent_issue", I, A, A, A, A, I, L, L, L, A, A);
    public static final MethodHandle w4_consent_info = fn("zxv_w4_consent_info", I, A, A, A, A, A, A, A, A, A);
    public static final MethodHandle w4_consent_log_create = fn("zxv_w4_consent_log_create", I, I, A);
    public static final MethodHandle w4_consent_log_destroy = fn("zxv_w4_consent_log_destroy", null, A);
    public static final MethodHandle w4_consent_check = fn("zxv_w4_consent_check", I, A, A, A, A, A, I, L, L);
    public static final MethodHandle w4_consent_revoke = fn("zxv_w4_consent_revoke", I, A, A, L);
    public static final MethodHandle w4_consent_log_prune = fn("zxv_w4_consent_log_prune", I, A, L, A);
    public static final MethodHandle w4_env_seal = fn("zxv_w4_env_seal", I, A, A, I, A, L, I, A, I, A, L, A, L, A, L, A);
    public static final MethodHandle w4_env_reply = fn("zxv_w4_env_reply", I, A, A, A, L, I, I, I, A, L, L, A, L, A);
    public static final MethodHandle w4_env_decode = fn("zxv_w4_env_decode", I, A, L, A, A, A, A, A, A, A, A, A, A);
    public static final MethodHandle w4_env_payload = fn("zxv_w4_env_payload", I, A, L, A, L, A);
    public static final MethodHandle w4_env_consent = fn("zxv_w4_env_consent", I, A, L, A);
    public static final MethodHandle w4_env_verify = fn("zxv_w4_env_verify", I, A, L, A);

    // ---- paradox (pure)
    public static final MethodHandle paradox_signature = fn("zxv_paradox_signature", I, A, L, A);
    public static final MethodHandle paradox_kind_of_tag = fn("zxv_paradox_kind_of_tag", I, U8, A);
    public static final MethodHandle paradox_action = fn("zxv_paradox_action", I, I, U8, A);
    public static final MethodHandle paradox_outcome_action = fn("zxv_paradox_outcome_action", I, I, A);
    public static final MethodHandle paradox_repair_verify = fn("zxv_paradox_repair_verify", I, A);
    public static final MethodHandle paradox_kind_name = fn("zxv_paradox_kind_name", A, I);
    public static final MethodHandle paradox_outcome_name = fn("zxv_paradox_outcome_name", A, I);
    public static final MethodHandle paradox_node_state_name = fn("zxv_paradox_node_state_name", A, I);
    // ---- paradox book
    public static final MethodHandle paradox_create = fn("zxv_paradox_create", I, A, I, I, A);
    public static final MethodHandle paradox_destroy = fn("zxv_paradox_destroy", null, A);
    public static final MethodHandle paradox_classify = fn("zxv_paradox_classify", I, A, U8, A, I, A, A, A, A);
    public static final MethodHandle paradox_registry_add = fn("zxv_paradox_registry_add", I, A, A, A, I, U8, U8, I, A);
    public static final MethodHandle paradox_registry_find = fn("zxv_paradox_registry_find", I, A, A, A);
    public static final MethodHandle paradox_registry_count = fn("zxv_paradox_registry_count", I, A, A);
    public static final MethodHandle paradox_registry_at = fn("zxv_paradox_registry_at", I, A, I, A, A, A, A, A);
    public static final MethodHandle paradox_log_count = fn("zxv_paradox_log_count", I, A, I, A);
    public static final MethodHandle paradox_log_head = fn("zxv_paradox_log_head", I, A, I, A);
    public static final MethodHandle paradox_log_record = fn("zxv_paradox_log_record", I, A, I, I, A, L, A);
    public static final MethodHandle paradox_log_verify = fn("zxv_paradox_log_verify", I, A, I);
    // ---- paradox operators and their handles
    public static final MethodHandle paradox_policy_create = fn("zxv_paradox_policy_create", I, A, I, A, A, A, I, A);
    public static final MethodHandle paradox_policy_destroy = fn("zxv_paradox_policy_destroy", null, A);
    public static final MethodHandle paradox_constructive = fn("zxv_paradox_constructive", I, A, A, A, I, U8, A, I, I, A);
    public static final MethodHandle paradox_policy_values = fn("zxv_paradox_policy_values", I, A, A, A, I, A, A);
    public static final MethodHandle paradox_policy_bounds = fn("zxv_paradox_policy_bounds", I, A, A, A, I, A);
    public static final MethodHandle paradox_policy_proposal = fn("zxv_paradox_policy_proposal", I, A, A, I, A, L, A);
    public static final MethodHandle paradox_destructive = fn("zxv_paradox_destructive", I,
            A, A, I, U8, I, I, I, I, A, I, I, A, A, I, A, I, I, A, A);
    public static final MethodHandle paradox_hold_create = fn("zxv_paradox_hold_create", I, A, I, I, A);
    public static final MethodHandle paradox_hold_destroy = fn("zxv_paradox_hold_destroy", null, A);
    public static final MethodHandle paradox_hold_add = fn("zxv_paradox_hold_add", I, A, A);
    public static final MethodHandle paradox_hold_evidence = fn("zxv_paradox_hold_evidence", I, A, A, I);
    public static final MethodHandle paradox_hold_info = fn("zxv_paradox_hold_info", I, A, A, A, A);
    public static final MethodHandle paradox_hold_branch = fn("zxv_paradox_hold_branch", I, A, I, A, A, A);
    public static final MethodHandle paradox_neutral = fn("zxv_paradox_neutral", I, A, A, A, I, U8, I, A, A);
    public static final MethodHandle paradox_special = fn("zxv_paradox_special", I, A, A, I, U8, A, A, A, A);
    public static final MethodHandle paradox_draft = fn("zxv_paradox_draft", I, A, A, I, I, U8, U8, A);
    public static final MethodHandle paradox_review = fn("zxv_paradox_review", I, A, A, A, I, I, A);
    public static final MethodHandle paradox_graph_create = fn("zxv_paradox_graph_create", I, A, I, A);
    public static final MethodHandle paradox_graph_destroy = fn("zxv_paradox_graph_destroy", null, A);
    public static final MethodHandle paradox_graph_add_node = fn("zxv_paradox_graph_add_node", I, A, A, I, U8, I, I);
    public static final MethodHandle paradox_graph_add_dep = fn("zxv_paradox_graph_add_dep", I, A, A, A);
    public static final MethodHandle paradox_graph_update = fn("zxv_paradox_graph_update", I, A, A, I, U8, I, I);
    public static final MethodHandle paradox_graph_pass = fn("zxv_paradox_graph_pass", I, A, A, A, A);
    public static final MethodHandle paradox_graph_state = fn("zxv_paradox_graph_state", I, A, A, A);
    public static final MethodHandle paradox_graph_node_at = fn("zxv_paradox_graph_node_at", I, A, I, A, A, A);
    public static final MethodHandle paradox_graph_info = fn("zxv_paradox_graph_info", I, A, A, A, A, A, A, A);
}
