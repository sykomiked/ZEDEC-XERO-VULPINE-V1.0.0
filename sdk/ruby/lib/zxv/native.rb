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
      [:zxv_hexlog_verify, %i[pointer], :int32]
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
        attach_function :zxv_generate,
                        %i[pointer pointer pointer size_t uint32 int32 uint32 int32 int32 uint32 uint64
                           zxv_token_fn pointer pointer pointer pointer],
                        :int32, blocking: true
      end

      def check_version!
        maj = FFI::MemoryPointer.new(:uint32)
        min = FFI::MemoryPointer.new(:uint32)
        zxv_version(maj, min, nil)
        major = maj.read_uint32
        minor = min.read_uint32
        return if major == 1 && minor >= 3

        raise LoadError, "libzxv ABI #{major}.#{minor} is not compatible (need 1.x, x >= 3)"
      end
    end
  end
end
