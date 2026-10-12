# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require "ffi"

module Zxv
  # Raw bindings to libzxv (zxv_api.h, ABI 1.x). Internal: application code
  # uses the Ruby classes, never these functions directly.
  #
  # The library is loaded on first use, from (in order) the path given to
  # {Zxv.load_library}, +ZXV_LIBRARY_PATH+, a copy bundled in this gem
  # (+lib/zxv/native/+), the in-repository build (+sdk/libzxv/build+), then
  # the system loader path (+libzxv+).
  module Native
    extend FFI::Library

    # [name, args, return, options]
    FUNCTIONS = [
      [:zxv_version, %i[pointer pointer pointer], :int32],
      [:zxv_features, %i[pointer], :int32],
      [:zxv_status_name, %i[int32], :string],
      [:zxv_status_class, %i[int32], :int32],
      [:zxv_last_error, %i[pointer size_t pointer], :int32],
      [:zxv_ctx_create, %i[pointer], :int32],
      [:zxv_ctx_destroy, %i[pointer], :void],
      [:zxv_ctx_is_poisoned, %i[pointer pointer], :int32],
      [:zxv_ctx_halt, %i[pointer string], :int32],
      # ISO 4217 and currencies
      [:zxv_iso4217_by_alpha, %i[string pointer pointer pointer], :int32],
      [:zxv_iso4217_by_numeric, %i[uint16 pointer size_t pointer pointer pointer], :int32],
      [:zxv_iso4217_name, %i[string pointer size_t pointer], :int32],
      [:zxv_iso4217_count, %i[pointer], :int32],
      [:zxv_iso4217_at, %i[uint32 pointer size_t pointer], :int32],
      [:zxv_iso4217_published, %i[pointer size_t pointer], :int32],
      [:zxv_ccy_caveat, %i[uint16 pointer size_t pointer], :int32],
      [:zxv_au_is_member, %i[string pointer], :int32],
      [:zxv_rail_numeric, %i[int32 pointer], :int32],
      [:zxv_ccy_enable, %i[pointer string], :int32],
      [:zxv_ccy_enable_with_minor, %i[pointer string uint8], :int32],
      [:zxv_ccy_register_private, %i[pointer string uint8], :int32],
      [:zxv_ccy_get, %i[pointer string pointer pointer], :int32],
      [:zxv_ccy_enabled_count, %i[pointer pointer], :int32],
      [:zxv_ccy_enabled_at, %i[pointer uint32 pointer size_t pointer], :int32],
      # ledger
      [:zxv_ledger_open_account, %i[pointer string string int32 pointer], :int32],
      [:zxv_ledger_account_ccy, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_ledger_account_name, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_ledger_account_kind, %i[pointer uint32 pointer], :int32],
      [:zxv_ledger_account_count, %i[pointer pointer], :int32],
      [:zxv_ledger_entry_count, %i[pointer uint32 pointer], :int32],
      [:zxv_ledger_entry_at, %i[pointer uint32 uint32 pointer pointer pointer pointer size_t pointer], :int32],
      [:zxv_ledger_post_cap, %i[pointer uint32 uint32 int64 string string pointer], :int32],
      # ABI 1.2: the context surface through a capability.
      [:zxv_ccy_enable_cap, %i[pointer string], :int32],
      [:zxv_ccy_enable_with_minor_cap, %i[pointer string uint8], :int32],
      [:zxv_ccy_register_private_cap, %i[pointer string uint8], :int32],
      [:zxv_ledger_open_account_cap, %i[pointer string string int32 pointer], :int32],
      [:zxv_ledger_account_ccy_cap, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_ledger_account_name_cap, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_ledger_account_kind_cap, %i[pointer uint32 pointer], :int32],
      [:zxv_ledger_account_count_cap, %i[pointer pointer], :int32],
      [:zxv_ledger_entry_count_cap, %i[pointer uint32 pointer], :int32],
      [:zxv_ledger_entry_at_cap, %i[pointer uint32 uint32 pointer pointer pointer pointer size_t pointer], :int32],
      [:zxv_msg_create_cap, %i[pointer int32 pointer], :int32],
      [:zxv_ledger_balance_cap, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_ledger_check_cap, %i[pointer], :int32],
      # ISO 20022 messages
      [:zxv_msg_create, %i[pointer int32 pointer], :int32],
      [:zxv_msg_destroy, %i[pointer], :void],
      [:zxv_msg_set_text, %i[pointer int32 string], :int32],
      [:zxv_msg_set_amount, %i[pointer int32 int64 uint8], :int32],
      [:zxv_msg_set_tx_count, %i[pointer uint32], :int32],
      [:zxv_msg_add_entry, %i[pointer int64 uint8 int32 int32], :int32],
      [:zxv_msg_render, %i[pointer pointer size_t pointer], :int32],
      [:zxv_card_check_digit, %i[int32 string pointer], :int32],
      # rationals
      [:zxv_rat_normalize, %i[int64 int64 pointer pointer], :int32],
      [:zxv_rat_add, %i[int64 int64 int64 int64 pointer pointer], :int32],
      [:zxv_rat_sub, %i[int64 int64 int64 int64 pointer pointer], :int32],
      [:zxv_rat_mul, %i[int64 int64 int64 int64 pointer pointer], :int32],
      [:zxv_rat_div, %i[int64 int64 int64 int64 pointer pointer], :int32],
      [:zxv_rat_cmp, %i[int64 int64 int64 int64 pointer], :int32],
      [:zxv_rat_parse, %i[string pointer pointer], :int32],
      [:zxv_rat_format, %i[int64 int64 pointer size_t pointer], :int32],
      [:zxv_rat_from_ieee754, %i[uint64 pointer pointer], :int32],
      [:zxv_rat_to_q16, %i[int64 int64 pointer], :int32],
      # four-valued logic
      [:zxv_lp_not, %i[int32 pointer], :int32],
      [:zxv_lp_and, %i[int32 int32 pointer], :int32],
      [:zxv_lp_or, %i[int32 int32 pointer], :int32],
      [:zxv_lp_from_evidence, %i[uint32 uint32 pointer], :int32],
      [:zxv_lp_name, %i[int32], :string],
      # capabilities
      [:zxv_cap_create, %i[pointer uint32 pointer uint64 pointer], :int32],
      [:zxv_cap_attenuate, %i[pointer uint64 pointer], :int32],
      [:zxv_cap_revoke, %i[pointer], :int32],
      [:zxv_cap_set_attestation, %i[pointer int32], :int32],
      [:zxv_cap_info, %i[pointer pointer pointer pointer pointer], :int32],
      [:zxv_cap_check, %i[pointer uint64], :int32],
      [:zxv_cap_destroy, %i[pointer], :void],
      # attestations
      [:zxv_lpres_create, %i[pointer pointer], :int32],
      [:zxv_lpres_create_cap, %i[pointer pointer], :int32],
      [:zxv_lpres_destroy, %i[pointer], :void],
      [:zxv_lpres_attest, %i[pointer pointer string int32 int32 string pointer uint32 pointer], :int32],
      [:zxv_lpres_evidence, %i[pointer pointer uint32 int32], :int32],
      [:zxv_lpres_find, %i[pointer string pointer], :int32],
      [:zxv_lpres_state, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_lpres_gate, %i[pointer pointer], :int32],
      # budgets
      [:zxv_rmag_create, %i[pointer pointer], :int32],
      [:zxv_rmag_create_cap, %i[pointer pointer], :int32],
      [:zxv_rmag_destroy, %i[pointer], :void],
      [:zxv_rmag_budget, %i[pointer pointer string int32 int64 int64 int32 pointer], :int32],
      [:zxv_rmag_consume, %i[pointer pointer uint32 int64 int64], :int32],
      [:zxv_rmag_release, %i[pointer pointer uint32 int64 int64], :int32],
      [:zxv_rmag_set_limit, %i[pointer pointer uint32 int64 int64], :int32],
      [:zxv_rmag_status, %i[pointer uint32 pointer pointer pointer pointer pointer], :int32],
      # phase
      [:zxv_phase_create, %i[pointer pointer], :int32],
      [:zxv_phase_create_cap, %i[pointer pointer], :int32],
      [:zxv_phase_destroy, %i[pointer], :void],
      [:zxv_phase_now, %i[pointer pointer], :int32],
      [:zxv_phase_advance, %i[pointer pointer uint64 pointer pointer], :int32],
      [:zxv_phase_admit, %i[pointer pointer uint32 uint32 uint32 pointer pointer], :int32],
      [:zxv_phase_set_health, %i[pointer pointer uint32 int32], :int32],
      [:zxv_phase_defer, %i[pointer pointer uint64 pointer], :int32],
      [:zxv_phase_event, %i[pointer uint64 pointer pointer], :int32],
      [:zxv_phase_cancel, %i[pointer pointer uint64], :int32],
      [:zxv_phase_next_due, %i[pointer pointer], :int32],
      # wire
      [:zxv_wire_tag, %i[uint32 uint32 uint32 pointer], :int32],
      [:zxv_wire_pack, %i[uint8 pointer pointer pointer], :int32],
      [:zxv_wire_unpack, %i[pointer pointer pointer pointer], :int32],
      [:zxv_wire_resolve, %i[pointer pointer], :int32],
      [:zxv_wire_idle, %i[uint32 pointer], :int32],
      # swarm
      [:zxv_swarm_create, %i[pointer uint32 uint64 pointer], :int32],
      [:zxv_swarm_create_cap, %i[pointer uint32 uint64 pointer], :int32],
      [:zxv_swarm_destroy, %i[pointer], :void],
      [:zxv_swarm_register, %i[pointer pointer uint32 uint32], :int32],
      [:zxv_swarm_set_active, %i[pointer pointer uint32 int32], :int32],
      [:zxv_swarm_set_rate, %i[pointer pointer uint64], :int32],
      [:zxv_swarm_set_mood, %i[pointer pointer uint32 uint32], :int32],
      [:zxv_swarm_set_feeling, %i[pointer pointer uint32 uint32 uint32], :int32],
      [:zxv_swarm_begin_cycle, %i[pointer pointer pointer], :int32],
      [:zxv_swarm_consume, %i[pointer pointer uint32 uint64 pointer], :int32],
      [:zxv_swarm_allotment, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_swarm_end_cycle, %i[pointer pointer pointer], :int32],
      [:zxv_swarm_level_capacity, %i[uint32 pointer], :int32],
      [:zxv_swarm_emotion_profile, %i[uint32 pointer pointer pointer pointer pointer], :int32],
      # hashes, random, secure memory
      [:zxv_sha3_256, %i[pointer size_t pointer], :int32],
      [:zxv_shake256, %i[pointer size_t pointer size_t], :int32],
      [:zxv_keccak256, %i[pointer size_t pointer], :int32],
      [:zxv_random, %i[pointer size_t], :int32],
      [:zxv_secure_zero, %i[pointer size_t], :int32],
      # crypto
      [:zxv_mlkem1024_keygen, %i[pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mlkem1024_keygen_derand, %i[pointer pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mlkem1024_encaps, %i[pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mlkem1024_encaps_derand, %i[pointer pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mlkem1024_decaps, %i[pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mldsa87_keygen, %i[pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mldsa87_keygen_seed, %i[pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_mldsa87_sign, %i[pointer pointer pointer size_t pointer size_t pointer], :int32, { blocking: true }],
      [:zxv_mldsa87_sign_deterministic, %i[pointer pointer pointer size_t pointer size_t pointer], :int32, { blocking: true }],
      [:zxv_mldsa87_verify, %i[pointer pointer pointer size_t pointer size_t pointer], :int32, { blocking: true }],
      [:zxv_ed25519_keygen, %i[pointer pointer pointer], :int32],
      [:zxv_ed25519_public, %i[pointer pointer pointer], :int32],
      [:zxv_ed25519_sign, %i[pointer pointer pointer size_t pointer], :int32],
      [:zxv_ed25519_verify, %i[pointer pointer pointer size_t pointer], :int32],
      # engine
      [:zxv_model_open, %i[pointer string string pointer], :int32, { blocking: true }],
      [:zxv_model_destroy, %i[pointer], :void],
      [:zxv_model_info, %i[pointer int32 pointer], :int32],
      [:zxv_tokenize, %i[pointer pointer size_t int32 pointer size_t pointer], :int32],
      [:zxv_detokenize, %i[pointer pointer size_t pointer size_t pointer], :int32],
      [:zxv_session_create, %i[pointer uint32 int32 pointer], :int32],
      [:zxv_session_destroy, %i[pointer], :void],
      [:zxv_session_reset, %i[pointer], :int32],
      [:zxv_session_position, %i[pointer pointer], :int32],
      [:zxv_session_bind_budget, %i[pointer pointer uint32], :int32],
      [:zxv_lora_load, %i[pointer pointer string int32 pointer], :int32],
      # EVM
      [:zxv_evm_encode_state, %i[pointer pointer uint64 pointer pointer uint32 pointer size_t pointer pointer], :int32],
      [:zxv_evm_ledger_state, %i[pointer pointer uint64 string pointer pointer uint32 pointer size_t pointer pointer], :int32],
      # ABI 1.3: the hexagram tag and the quarantine log
      [:zxv_hex_pack, %i[uint8 uint8 uint8 pointer], :int32],
      [:zxv_hex_unpack, %i[uint8 pointer pointer pointer], :int32],
      [:zxv_hex_action, %i[uint8 pointer], :int32],
      [:zxv_hex_phase_next, %i[uint8 pointer], :int32],
      [:zxv_hex_step, %i[uint8 uint8 pointer], :int32],
      [:zxv_hex_set, %i[uint8 uint8 uint8 pointer], :int32],
      [:zxv_hex_forbid, %i[uint8 pointer], :int32],
      [:zxv_hex_and, %i[uint8 uint8 pointer], :int32],
      [:zxv_hex_or, %i[uint8 uint8 pointer], :int32],
      [:zxv_hex_not, %i[uint8 pointer], :int32],
      [:zxv_hex_polarity, %i[uint32 uint32 uint32 uint32 int32 pointer], :int32],
      [:zxv_hex_from_lp, %i[int32 pointer], :int32],
      [:zxv_hex_to_lp, %i[uint8 pointer], :int32],
      [:zxv_hex_name, %i[uint8], :string],
      [:zxv_hex_action_name, %i[int32], :string],
      [:zxv_hexlog_create, %i[pointer uint32 pointer], :int32],
      [:zxv_hexlog_destroy, %i[pointer], :void],
      [:zxv_hexlog_append, %i[pointer pointer uint8 uint8 uint32 pointer pointer], :int32],
      [:zxv_hexlog_count, %i[pointer pointer], :int32],
      [:zxv_hexlog_head, %i[pointer pointer], :int32],
      [:zxv_hexlog_at, %i[pointer uint32 pointer pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_hexlog_verify, %i[pointer], :int32],
      # ABI 1.4: hardware emulation and hardware updates. Profiles (no capability)
      [:zxv_hw_profile_create, %i[pointer], :int32],
      [:zxv_hw_profile_destroy, %i[pointer], :void],
      [:zxv_hw_profile_probe_host, %i[pointer], :int32],
      [:zxv_hw_profile_reference, %i[pointer], :int32],
      [:zxv_hw_profile_present, %i[pointer pointer pointer], :int32],
      [:zxv_hw_profile_decode, %i[pointer size_t pointer], :int32],
      [:zxv_hw_profile_encode, %i[pointer pointer size_t pointer], :int32],
      [:zxv_hw_profile_digest, %i[pointer pointer], :int32],
      [:zxv_hw_profile_node_add, %i[pointer uint32 uint32 uint32], :int32],
      [:zxv_hw_profile_cap_set, %i[pointer uint32 uint32 uint64], :int32],
      [:zxv_hw_profile_cap_get, %i[pointer uint32 uint32 pointer], :int32],
      [:zxv_hw_profile_ext_set, %i[pointer uint32 pointer size_t], :int32],
      [:zxv_hw_profile_ext_get, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_hw_profile_count, %i[pointer pointer], :int32],
      [:zxv_hw_profile_node, %i[pointer uint32 pointer pointer pointer pointer], :int32],
      [:zxv_hw_profile_match, %i[pointer pointer pointer pointer], :int32],
      [:zxv_hw_profile_pair, %i[pointer pointer uint32 pointer pointer pointer pointer], :int32],
      # the emulation matrix
      [:zxv_hw_matrix_create_cap, %i[pointer pointer], :int32],
      [:zxv_hw_matrix_destroy, %i[pointer], :void],
      [:zxv_hw_function_add, %i[pointer pointer uint32 string], :int32],
      [:zxv_hw_vector_add, %i[pointer pointer uint32 uint32 pointer size_t pointer size_t], :int32],
      [:zxv_hw_function_contract, %i[pointer pointer uint32 uint32], :int32],
      [:zxv_hw_policy_set, %i[pointer pointer uint32 uint32 uint32 uint32], :int32],
      [:zxv_hw_function_info, %i[pointer pointer uint32 pointer pointer pointer], :int32],
      [:zxv_hw_resource_add, %i[pointer pointer uint32], :int32],
      [:zxv_hw_resource_set, %i[pointer pointer uint32 int32], :int32],
      [:zxv_hw_provider_traits, %i[pointer pointer uint32 uint32 uint32 uint32], :int32],
      [:zxv_hw_provider_dep, %i[pointer pointer uint32 uint32], :int32],
      [:zxv_hw_provider_enable, %i[pointer pointer uint32 int32], :int32],
      [:zxv_hw_attest, %i[pointer pointer uint32 pointer], :int32],
      [:zxv_hw_provider_count, %i[pointer pointer pointer], :int32],
      [:zxv_hw_provider_info, %i[pointer pointer uint32 pointer pointer pointer pointer pointer pointer pointer pointer
                                 pointer], :int32],
      [:zxv_hw_bound, %i[pointer pointer uint32 pointer], :int32],
      [:zxv_hw_call, %i[pointer pointer uint32 uint32 pointer size_t pointer size_t pointer], :int32],
      [:zxv_hw_shadow, %i[pointer pointer uint32 uint32 pointer size_t pointer size_t pointer pointer], :int32],
      [:zxv_hw_memo, %i[pointer pointer uint32 uint32 pointer pointer], :int32],
      # signed packages (SLH-DSA signing takes seconds: release the GVL)
      [:zxv_hw_keygen, %i[pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_hw_image_build, %i[pointer uint32 uint32 pointer uint32 uint32 uint32 pointer size_t pointer pointer pointer
                               size_t pointer], :int32, { blocking: true }],
      [:zxv_hw_matrix_trust_key, %i[pointer pointer pointer], :int32],
      [:zxv_hw_contract_apply, %i[pointer pointer pointer size_t pointer], :int32],
      # reconfigurable regions
      [:zxv_hw_region_destroy, %i[pointer], :void],
      [:zxv_hw_region_trust_key, %i[pointer pointer pointer], :int32],
      [:zxv_hw_region_stage, %i[pointer pointer pointer size_t], :int32],
      [:zxv_hw_region_trial, %i[pointer pointer uint32], :int32],
      [:zxv_hw_region_commit, %i[pointer pointer], :int32],
      [:zxv_hw_region_rollback, %i[pointer pointer], :int32],
      [:zxv_hw_region_tick, %i[pointer pointer], :int32],
      [:zxv_hw_region_info, %i[pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_hw_region_log, %i[pointer pointer pointer pointer], :int32],
      [:zxv_hw_region_log_entry, %i[pointer pointer uint32 pointer pointer pointer pointer pointer pointer pointer],
       :int32],
      [:zxv_hw_matrix_log, %i[pointer pointer pointer pointer], :int32],
      [:zxv_hw_matrix_log_entry, %i[pointer pointer uint32 pointer pointer pointer pointer pointer pointer pointer],
       :int32]
    ].freeze

    # ABI 1.5 (add-only). Attached only when the library has them: against
    # an older libzxv each one returns ZXV_E_NOTIMPL instead (rule R9), so
    # this SDK still loads a 1.4 library.
    FUNCTIONS_1_5 = [
      [:zxv_features2, %i[pointer], :int32],
      # treaty (pure)
      [:zxv_treaty_create, %i[pointer], :int32],
      [:zxv_treaty_destroy, %i[pointer], :void],
      [:zxv_treaty_clear, %i[pointer], :int32],
      [:zxv_treaty_parse_sutra, %i[pointer string pointer pointer], :int32],
      [:zxv_treaty_parse_record, %i[pointer pointer size_t pointer pointer], :int32],
      [:zxv_treaty_emit_sutra, %i[pointer pointer size_t pointer], :int32],
      [:zxv_treaty_emit_record, %i[pointer pointer size_t pointer], :int32],
      [:zxv_treaty_validate, %i[pointer pointer], :int32],
      [:zxv_treaty_equal, %i[pointer pointer pointer], :int32],
      [:zxv_treaty_name, %i[pointer pointer size_t pointer], :int32],
      [:zxv_treaty_asset, %i[pointer pointer size_t pointer], :int32],
      [:zxv_treaty_header, %i[pointer pointer pointer pointer], :int32],
      [:zxv_treaty_party_id, %i[pointer uint32 pointer size_t pointer], :int32],
      [:zxv_treaty_party, %i[pointer uint32 pointer pointer pointer pointer], :int32],
      [:zxv_treaty_owe, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_treaty_pic_add, %i[int64 int64 pointer], :int32],
      [:zxv_treaty_pic_sub, %i[int64 int64 pointer], :int32],
      [:zxv_treaty_pic_mul, %i[int64 int64 pointer], :int32],
      [:zxv_treaty_pic_div, %i[int64 int64 pointer pointer], :int32],
      [:zxv_treaty_run, %i[pointer pointer], :int32, { blocking: true }],
      [:zxv_treaty_result_destroy, %i[pointer], :void],
      [:zxv_treaty_result_summary, %i[pointer pointer pointer pointer], :int32],
      [:zxv_treaty_result_party, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_treaty_result_leg, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_treaty_result_record, %i[pointer pointer size_t pointer], :int32],
      # cid + aipi
      [:zxv_cid_compute, %i[uint32 pointer size_t pointer pointer], :int32],
      [:zxv_cid_unixfs, %i[pointer size_t pointer pointer], :int32, { blocking: true }],
      [:zxv_cid_verify, %i[pointer size_t pointer size_t], :int32],
      [:zxv_cid_to_text, %i[pointer size_t uint32 pointer size_t pointer], :int32],
      [:zxv_cid_from_text, %i[string pointer pointer], :int32],
      [:zxv_cid_info, %i[pointer size_t pointer pointer pointer pointer pointer], :int32],
      [:zxv_cid_to_v1, %i[pointer size_t pointer pointer], :int32],
      [:zxv_aipi_provider_key, %i[pointer size_t pointer], :int32],
      [:zxv_aipi_fn_encode, %i[uint32 uint32 uint32 uint32 uint32 uint32 uint64 pointer size_t pointer size_t pointer
                               pointer pointer pointer size_t pointer], :int32],
      [:zxv_aipi_fn_decode, %i[pointer size_t pointer pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_aipi_fn_get_cid, %i[pointer size_t uint32 pointer pointer], :int32],
      [:zxv_aipi_fn_get_name, %i[pointer size_t uint32 pointer size_t pointer], :int32],
      [:zxv_aipi_fn_cid, %i[pointer size_t pointer pointer], :int32],
      [:zxv_aipi_call_encode, %i[pointer size_t pointer size_t pointer size_t pointer], :int32],
      [:zxv_aipi_call_decode, %i[pointer size_t pointer pointer pointer pointer], :int32],
      [:zxv_aipi_call_cid, %i[pointer size_t pointer size_t pointer pointer], :int32],
      [:zxv_aipi_claim_encode, %i[pointer size_t pointer pointer size_t pointer size_t pointer size_t pointer], :int32],
      [:zxv_aipi_claim_decode, %i[pointer size_t pointer pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_aipi_create, %i[pointer size_t uint32 pointer pointer], :int32],
      [:zxv_aipi_destroy, %i[pointer], :void],
      [:zxv_aipi_put, %i[pointer pointer pointer size_t int32 pointer pointer], :int32],
      [:zxv_aipi_get, %i[pointer pointer size_t pointer size_t pointer], :int32],
      [:zxv_aipi_publish_fn, %i[pointer pointer pointer size_t pointer pointer], :int32],
      [:zxv_aipi_load_fn, %i[pointer pointer size_t pointer size_t pointer], :int32],
      [:zxv_aipi_plan, %i[pointer pointer size_t pointer size_t int32 pointer], :int32],
      [:zxv_aipi_info, %i[pointer pointer pointer pointer pointer], :int32],
      # coin + token
      [:zxv_coin_face, %i[uint32 pointer], :int32],
      [:zxv_coin_rung, %i[uint64 pointer], :int32],
      [:zxv_coin_split, %i[uint64 pointer size_t pointer], :int32],
      [:zxv_token_face, %i[uint32 pointer pointer], :int32],
      [:zxv_token_tier, %i[uint64 pointer], :int32],
      [:zxv_token_value, %i[pointer pointer pointer], :int32],
      [:zxv_token_canonical, %i[uint64 uint32 pointer], :int32],
      [:zxv_token_is_canonical, %i[pointer uint32 pointer], :int32],
      [:zxv_token_compress, %i[pointer uint32], :int32],
      [:zxv_token_fuse, %i[pointer uint32 uint32], :int32],
      [:zxv_token_split, %i[pointer uint32], :int32],
      [:zxv_token_code_bits, %i[uint64 pointer], :int32],
      [:zxv_token_code_put, %i[pointer size_t pointer uint64], :int32],
      [:zxv_token_code_get, %i[pointer size_t pointer pointer], :int32],
      [:zxv_token_wallet_create, %i[pointer uint32 uint32 pointer], :int32],
      [:zxv_token_wallet_destroy, %i[pointer], :void],
      [:zxv_token_wallet_mint, %i[pointer pointer uint64], :int32],
      [:zxv_token_wallet_spend, %i[pointer pointer uint64], :int32],
      [:zxv_token_wallet_transfer, %i[pointer pointer pointer uint64], :int32],
      [:zxv_token_wallet_set_octane, %i[pointer pointer uint32], :int32],
      [:zxv_token_wallet_info, %i[pointer pointer pointer pointer pointer], :int32],
      [:zxv_token_wallet_counts, %i[pointer pointer], :int32],
      # fee (pure)
      [:zxv_fee_assure, %i[uint64 pointer], :int32],
      [:zxv_fee_assure_carry, %i[uint64 uint64 pointer pointer], :int32],
      [:zxv_fee_split, %i[uint64 pointer], :int32],
      [:zxv_fee_policy_default, %i[pointer pointer pointer pointer pointer pointer pointer pointer], :int32],
      # vhub
      [:zxv_vhub_create, %i[pointer pointer pointer], :int32],
      [:zxv_vhub_destroy, %i[pointer], :void],
      [:zxv_vhub_add_ccy, %i[pointer pointer uint8 string uint8 pointer uint64 uint64], :int32],
      [:zxv_vhub_ccy_info, %i[pointer pointer uint8 pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_vhub_peg_msg, %i[uint8 uint64 uint64 uint64 pointer], :int32],
      [:zxv_vhub_set_peg, %i[pointer pointer uint8 uint64 uint64 uint64 pointer], :int32, { blocking: true }],
      [:zxv_vhub_open_account, %i[pointer pointer uint8 uint32 pointer], :int32],
      [:zxv_vhub_issue, %i[pointer pointer uint32 uint64], :int32],
      [:zxv_vhub_balance, %i[pointer pointer uint32 pointer pointer pointer], :int32],
      [:zxv_vhub_add_limit, %i[pointer pointer uint8 uint64], :int32],
      [:zxv_vhub_next_epoch, %i[pointer pointer pointer], :int32],
      [:zxv_vhub_quote, %i[pointer pointer uint8 uint8 uint64 pointer pointer pointer], :int32],
      [:zxv_vhub_swap, %i[pointer pointer uint8 uint8 uint32 uint32 uint64 uint64 pointer pointer], :int32],
      [:zxv_vhub_check, %i[pointer pointer pointer], :int32],
      # freight (pure)
      [:zxv_freight_header_init, %i[uint64 uint8 uint8 uint64 pointer size_t pointer], :int32],
      [:zxv_freight_header_serialize, %i[uint8 uint8 uint16 uint16 uint16 uint64 uint64 pointer pointer], :int32],
      [:zxv_freight_header_parse, %i[pointer pointer pointer pointer pointer pointer pointer pointer pointer pointer],
       :int32],
      [:zxv_freight_header_check, %i[pointer], :int32],
      [:zxv_freight_encode, %i[pointer pointer size_t pointer], :int32],
      [:zxv_freight_decode, %i[pointer pointer size_t pointer size_t pointer], :int32],
      [:zxv_freight_set_count, %i[uint64 uint8 pointer], :int32],
      [:zxv_freight_split, %i[uint64 uint8 uint8 uint64 pointer size_t uint32 pointer pointer], :int32],
      [:zxv_freight_join_create, %i[uint64 pointer], :int32],
      [:zxv_freight_join_destroy, %i[pointer], :void],
      [:zxv_freight_join_add, %i[pointer pointer pointer size_t], :int32],
      [:zxv_freight_join_status, %i[pointer pointer pointer pointer pointer], :int32],
      [:zxv_freight_join_result, %i[pointer pointer size_t pointer], :int32],
      [:zxv_freight_ops_peek, %i[pointer size_t pointer pointer], :int32],
      [:zxv_freight_gen_run, %i[uint8 pointer size_t pointer size_t], :int32],
      [:zxv_freight_ops_encode, %i[pointer size_t int32 pointer size_t pointer], :int32, { blocking: true }],
      [:zxv_freight_ops_bound, %i[uint64 int32 pointer], :int32],
      [:zxv_freight_ops_writer_create, %i[uint64 pointer uint64 pointer], :int32],
      [:zxv_freight_ops_writer_destroy, %i[pointer], :void],
      [:zxv_freight_ops_put_literal, %i[pointer pointer size_t], :int32],
      [:zxv_freight_ops_put_copy, %i[pointer uint64 uint64], :int32],
      [:zxv_freight_ops_put_cidref, %i[pointer pointer size_t uint64], :int32],
      [:zxv_freight_ops_put_seed, %i[pointer uint8 pointer size_t uint64], :int32],
      [:zxv_freight_ops_writer_finish, %i[pointer pointer size_t pointer], :int32],
      # web4 (ML-DSA-65 signing and verifying take milliseconds: release the GVL)
      [:zxv_w4_agent_create, %i[pointer pointer string pointer uint64 pointer], :int32, { blocking: true }],
      [:zxv_w4_agent_destroy, %i[pointer], :void],
      [:zxv_w4_agent_info, %i[pointer pointer pointer pointer], :int32],
      [:zxv_w4_agent_card, %i[pointer pointer size_t pointer], :int32],
      [:zxv_w4_card_verify, %i[pointer size_t pointer pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_w4_dir_create, %i[uint32 pointer], :int32],
      [:zxv_w4_dir_destroy, %i[pointer], :void],
      [:zxv_w4_dir_add, %i[pointer pointer size_t], :int32, { blocking: true }],
      [:zxv_w4_dir_find, %i[pointer pointer pointer size_t pointer], :int32],
      [:zxv_w4_dir_count, %i[pointer pointer], :int32],
      [:zxv_w4_manifest_create, %i[uint32 uint64 uint64 pointer], :int32],
      [:zxv_w4_manifest_destroy, %i[pointer], :void],
      [:zxv_w4_manifest_add_tool, %i[pointer string uint64 uint32 uint32 uint32 uint32], :int32],
      [:zxv_w4_manifest_sign, %i[pointer pointer pointer], :int32, { blocking: true }],
      [:zxv_w4_manifest_verify, %i[pointer pointer uint64], :int32, { blocking: true }],
      [:zxv_w4_manifest_encode, %i[pointer pointer size_t pointer], :int32],
      [:zxv_w4_manifest_decode, %i[pointer size_t pointer], :int32],
      [:zxv_w4_manifest_to_json, %i[pointer pointer size_t pointer], :int32],
      [:zxv_w4_manifest_info, %i[pointer pointer pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_w4_manifest_tool, %i[pointer uint32 pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_w4_manifest_find, %i[pointer string pointer], :int32],
      [:zxv_w4_action_digest, %i[string pointer pointer uint64 pointer size_t pointer], :int32],
      [:zxv_w4_consent_issue, %i[pointer pointer pointer pointer uint32 uint64 uint64 uint64 pointer pointer], :int32,
       { blocking: true }],
      [:zxv_w4_consent_info, %i[pointer pointer pointer pointer pointer pointer pointer pointer pointer], :int32],
      [:zxv_w4_consent_log_create, %i[uint32 pointer], :int32],
      [:zxv_w4_consent_log_destroy, %i[pointer], :void],
      [:zxv_w4_consent_check, %i[pointer pointer pointer pointer pointer uint32 uint64 uint64], :int32, { blocking: true }],
      [:zxv_w4_consent_revoke, %i[pointer pointer uint64], :int32],
      [:zxv_w4_consent_log_prune, %i[pointer uint64 pointer], :int32],
      [:zxv_w4_env_seal, %i[pointer pointer int32 pointer uint64 uint32 string uint32 pointer size_t pointer uint64 pointer
                            size_t pointer], :int32, { blocking: true }],
      [:zxv_w4_env_reply, %i[pointer pointer pointer size_t int32 uint32 uint32 pointer size_t uint64 pointer size_t
                             pointer], :int32, { blocking: true }],
      [:zxv_w4_env_decode, %i[pointer size_t pointer pointer pointer pointer pointer pointer pointer pointer pointer
                              pointer], :int32],
      [:zxv_w4_env_payload, %i[pointer size_t pointer size_t pointer], :int32],
      [:zxv_w4_env_consent, %i[pointer size_t pointer], :int32],
      [:zxv_w4_env_verify, %i[pointer size_t pointer], :int32, { blocking: true }],
      # paradox
      [:zxv_paradox_signature, %i[pointer size_t pointer], :int32],
      [:zxv_paradox_kind_of_tag, %i[uint8 pointer], :int32],
      [:zxv_paradox_action, %i[int32 uint8 pointer], :int32],
      [:zxv_paradox_outcome_action, %i[int32 pointer], :int32],
      [:zxv_paradox_repair_verify, %i[pointer], :int32],
      [:zxv_paradox_kind_name, %i[int32], :string],
      [:zxv_paradox_outcome_name, %i[int32], :string],
      [:zxv_paradox_node_state_name, %i[int32], :string],
      [:zxv_paradox_create, %i[pointer uint32 uint32 pointer], :int32],
      [:zxv_paradox_destroy, %i[pointer], :void],
      [:zxv_paradox_classify, %i[pointer uint8 pointer int32 pointer pointer pointer pointer], :int32],
      [:zxv_paradox_registry_add, %i[pointer pointer pointer int32 uint8 uint8 int32 pointer], :int32],
      [:zxv_paradox_registry_find, %i[pointer pointer pointer], :int32],
      [:zxv_paradox_registry_count, %i[pointer pointer], :int32],
      [:zxv_paradox_registry_at, %i[pointer uint32 pointer pointer pointer pointer pointer], :int32],
      [:zxv_paradox_log_count, %i[pointer int32 pointer], :int32],
      [:zxv_paradox_log_head, %i[pointer int32 pointer], :int32],
      [:zxv_paradox_log_record, %i[pointer int32 uint32 pointer size_t pointer], :int32],
      [:zxv_paradox_log_verify, %i[pointer int32], :int32],
      [:zxv_paradox_policy_create, %i[pointer uint32 pointer pointer pointer uint32 pointer], :int32],
      [:zxv_paradox_policy_destroy, %i[pointer], :void],
      [:zxv_paradox_constructive, %i[pointer pointer pointer int32 uint8 pointer uint32 uint32 pointer], :int32],
      [:zxv_paradox_policy_values, %i[pointer pointer pointer uint32 pointer pointer], :int32],
      [:zxv_paradox_policy_bounds, %i[pointer pointer pointer uint32 pointer], :int32],
      [:zxv_paradox_policy_proposal, %i[pointer pointer uint32 pointer size_t pointer], :int32],
      [:zxv_paradox_destructive, %i[pointer pointer int32 uint8 uint32 uint32 uint32 uint32 pointer uint32 int32 pointer
                                    pointer uint32 pointer uint32 uint32 pointer pointer], :int32],
      [:zxv_paradox_hold_create, %i[pointer uint32 uint32 pointer], :int32],
      [:zxv_paradox_hold_destroy, %i[pointer], :void],
      [:zxv_paradox_hold_add, %i[pointer pointer], :int32],
      [:zxv_paradox_hold_evidence, %i[pointer pointer int32], :int32],
      [:zxv_paradox_hold_info, %i[pointer pointer pointer pointer], :int32],
      [:zxv_paradox_hold_branch, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_paradox_neutral, %i[pointer pointer pointer int32 uint8 int32 pointer pointer], :int32],
      [:zxv_paradox_special, %i[pointer pointer int32 uint8 pointer pointer pointer pointer], :int32],
      [:zxv_paradox_draft, %i[pointer pointer uint32 int32 uint8 uint8 pointer], :int32],
      [:zxv_paradox_review, %i[pointer pointer pointer int32 int32 pointer], :int32],
      [:zxv_paradox_graph_create, %i[pointer uint32 pointer], :int32],
      [:zxv_paradox_graph_destroy, %i[pointer], :void],
      [:zxv_paradox_graph_add_node, %i[pointer pointer int32 uint8 uint32 int32], :int32],
      [:zxv_paradox_graph_add_dep, %i[pointer pointer pointer], :int32],
      [:zxv_paradox_graph_update, %i[pointer pointer int32 uint8 uint32 int32], :int32],
      [:zxv_paradox_graph_pass, %i[pointer pointer pointer pointer], :int32],
      [:zxv_paradox_graph_state, %i[pointer pointer pointer], :int32],
      [:zxv_paradox_graph_node_at, %i[pointer uint32 pointer pointer pointer], :int32],
      [:zxv_paradox_graph_info, %i[pointer pointer pointer pointer pointer pointer pointer], :int32]
    ].freeze

    @loaded = nil
    @lock = Mutex.new

    class << self
      # Path of the loaded library, or nil before the first call.
      attr_reader :loaded

      # Load libzxv (once). +path+ overrides the search order.
      # @raise [Zxv::ZxvSystemError] if no compatible library is found
      def load!(path = nil)
        @lock.synchronize do
          return @loaded if @loaded

          candidates = []
          candidates << path if path
          candidates << ENV["ZXV_LIBRARY_PATH"] if ENV["ZXV_LIBRARY_PATH"] && !ENV["ZXV_LIBRARY_PATH"].empty?
          bundled = File.expand_path("native", __dir__)
          candidates.concat(Dir[File.join(bundled, "{libzxv.so,libzxv.dylib,zxv.dll}")])
          # Inside the ZXV repository: sdk/libzxv/build next to sdk/ruby.
          %w[libzxv.so libzxv.dylib zxv.dll].each do |name|
            in_repo = File.expand_path("../../../libzxv/build/#{name}", __dir__)
            candidates << in_repo if File.exist?(in_repo)
          end
          candidates << "zxv"
          errors = []
          candidates.each do |c|
            begin
              ffi_lib c
              bind!
              check_version!
              @loaded = c
              return c
            rescue LoadError, FFI::NotFoundError => e
              errors << "#{c}: #{e.message.lines.first&.strip}"
            end
          end
          raise Zxv::ZxvSystemError.new(-23, "libzxv not found (#{errors.join('; ')})")
        end
      end

      # Return the library, loading it on first use.
      def lib
        load! unless @loaded
        self
      end

      private

      def bind!
        callback :zxv_rmag_listener, %i[pointer uint32 int32], :void
        callback :zxv_token_fn, %i[pointer int32 pointer size_t], :int32
        FUNCTIONS.each do |name, args, ret, opts|
          attach_function name, args, ret, **(opts || {})
        end
        attach_function :zxv_rmag_listen, %i[pointer zxv_rmag_listener pointer], :int32
        # ABI 1.4 callbacks: an implementation, the device side of a region,
        # and region events.
        callback :zxv_hw_call_fn, %i[pointer uint32 pointer size_t pointer size_t pointer], :int32
        callback :zxv_hw_load_fn, %i[pointer uint32 pointer size_t], :int32
        callback :zxv_hw_readback_fn, %i[pointer uint32 pointer], :int32
        callback :zxv_hw_activate_fn, %i[pointer uint32], :int32
        callback :zxv_hw_event_fn, %i[pointer uint32 int32 uint32 uint32], :void
        attach_function :zxv_hw_provider_add,
                        %i[pointer pointer uint32 uint32 int32 uint32 zxv_hw_call_fn pointer pointer], :int32
        attach_function :zxv_hw_region_create_cap,
                        %i[pointer pointer uint32 uint32 uint32 pointer zxv_hw_load_fn zxv_hw_readback_fn
                           zxv_hw_activate_fn pointer uint32 pointer], :int32
        attach_function :zxv_hw_region_events, %i[pointer pointer zxv_hw_event_fn pointer], :int32
        callback :zxv_freight_resolve_fn, %i[pointer pointer size_t pointer uint64], :int32
        bind_1_5!
        attach_function :zxv_generate,
                        %i[pointer pointer pointer size_t uint32 int32 uint32 int32 int32 uint32 uint64
                           zxv_token_fn pointer pointer pointer pointer],
                        :int32, blocking: true
      end

      # Attach the ABI 1.5 functions one by one. A symbol the library lacks
      # (an older libzxv) becomes a stub that returns ZXV_E_NOTIMPL (or
      # "INVALID" for the name functions), so callers see a clean
      # Zxv::NotImplementedError instead of a load failure.
      def bind_1_5!
        @missing_1_5 = []
        FUNCTIONS_1_5.each do |name, args, ret, opts|
          attach_function name, args, ret, **(opts || {})
        rescue FFI::NotFoundError
          @missing_1_5 << name
          stub = ret == :string ? ->(*) { "INVALID" } : ->(*) { Zxv::Status::E_NOTIMPL }
          stub = ->(*) {} if ret == :void
          define_singleton_method(name, &stub)
        end
        begin
          attach_function :zxv_freight_ops_expand,
                          %i[pointer size_t uint64 zxv_freight_resolve_fn pointer pointer size_t pointer], :int32
        rescue FFI::NotFoundError
          @missing_1_5 << :zxv_freight_ops_expand
          define_singleton_method(:zxv_freight_ops_expand) { |*| Zxv::Status::E_NOTIMPL }
        end
      end

      def check_version!
        maj = FFI::MemoryPointer.new(:uint32)
        min = FFI::MemoryPointer.new(:uint32)
        zxv_version(maj, min, nil)
        major = maj.read_uint32
        minor = min.read_uint32
        return if major == 1 && minor >= 4

        raise LoadError, "libzxv ABI #{major}.#{minor} is not compatible (need 1.x, x >= 4)"
      end
    end
  end
end
