// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;

/**
 * Downcall handles for every libzxv function the SDK uses. Internal: the
 * public API never exposes these. Handles are resolved when this class is
 * first initialized, i.e. on the first native call; a missing symbol fails
 * loudly then.
 *
 * <p>Short, pure functions (rationals, hashes, wire framing) are linked with
 * {@link Linker.Option#critical(boolean) critical(true)}, so callers may pass
 * heap segments ({@code MemorySegment.ofArray}) without copying.
 */
public final class Native {
    private Native() {}

    private static final MemoryLayout I = JAVA_INT;
    private static final MemoryLayout L = JAVA_LONG; // int64, uint64, size_t (LP64/LLP64)
    private static final MemoryLayout A = ADDRESS;
    // By-value uint8_t / uint16_t parameters are declared as a 32-bit int and
    // passed as 0..255 / 0..65535. A JAVA_BYTE / JAVA_SHORT layout would be
    // signed: FFM sign-extends 0xC7 to 0xFFFFFFC7, and on Apple arm64 the
    // caller extends narrow arguments and the callee trusts the full
    // register, so libzxv would see 4294967239. An int already in range is
    // ABI-correct for a uint8_t / uint16_t argument on SysV x86-64, AAPCS64,
    // Apple arm64 and Windows x64.
    private static final MemoryLayout U8 = JAVA_INT;
    private static final MemoryLayout U16 = JAVA_INT;

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LIB = NativeLoader.open();

    static {
        if (ADDRESS.byteSize() != 8) {
            throw new UnsatisfiedLinkError("the ZXV SDK needs a 64-bit JVM (size_t is mapped to int64)");
        }
    }

    private static MethodHandle fn(String name, MemoryLayout ret, MemoryLayout... args) {
        MemorySegment sym = LIB.find(name).orElseThrow(
                () -> new UnsatisfiedLinkError("libzxv has no symbol " + name + " (" + NativeLoader.loadedFrom() + ")"));
        FunctionDescriptor d = ret == null ? FunctionDescriptor.ofVoid(args) : FunctionDescriptor.of(ret, args);
        return LINKER.downcallHandle(sym, d);
    }

    private static MethodHandle pure(String name, MemoryLayout ret, MemoryLayout... args) {
        MemorySegment sym = LIB.find(name).orElseThrow(
                () -> new UnsatisfiedLinkError("libzxv has no symbol " + name));
        return LINKER.downcallHandle(sym, FunctionDescriptor.of(ret, args), Linker.Option.critical(true));
    }

    /** The linker, for upcall stubs. */
    public static Linker linker() {
        return LINKER;
    }

    // ---- library
    public static final MethodHandle version = fn("zxv_version", I, A, A, A);
    public static final MethodHandle features = fn("zxv_features", I, A);
    public static final MethodHandle status_name = fn("zxv_status_name", A, I);
    public static final MethodHandle status_class = fn("zxv_status_class", I, I);
    public static final MethodHandle last_error = fn("zxv_last_error", I, A, L, A);
    // ---- context
    public static final MethodHandle ctx_create = fn("zxv_ctx_create", I, A);
    public static final MethodHandle ctx_destroy = fn("zxv_ctx_destroy", null, A);
    public static final MethodHandle ctx_is_poisoned = fn("zxv_ctx_is_poisoned", I, A, A);
    public static final MethodHandle ctx_halt = fn("zxv_ctx_halt", I, A, A);
    // ---- ISO 4217
    public static final MethodHandle iso4217_by_alpha = fn("zxv_iso4217_by_alpha", I, A, A, A, A);
    public static final MethodHandle iso4217_by_numeric = fn("zxv_iso4217_by_numeric", I, U16, A, L, A, A, A);
    public static final MethodHandle iso4217_name = fn("zxv_iso4217_name", I, A, A, L, A);
    public static final MethodHandle iso4217_count = fn("zxv_iso4217_count", I, A);
    public static final MethodHandle iso4217_at = fn("zxv_iso4217_at", I, I, A, L, A);
    public static final MethodHandle iso4217_published = fn("zxv_iso4217_published", I, A, L, A);
    public static final MethodHandle ccy_caveat = fn("zxv_ccy_caveat", I, U16, A, L, A);
    public static final MethodHandle au_is_member = fn("zxv_au_is_member", I, A, A);
    public static final MethodHandle rail_numeric = fn("zxv_rail_numeric", I, I, A);
    // ---- currencies per context
    public static final MethodHandle ccy_enable = fn("zxv_ccy_enable", I, A, A);
    public static final MethodHandle ccy_enable_with_minor = fn("zxv_ccy_enable_with_minor", I, A, A, U8);
    public static final MethodHandle ccy_register_private = fn("zxv_ccy_register_private", I, A, A, U8);
    public static final MethodHandle ccy_get = fn("zxv_ccy_get", I, A, A, A, A);
    public static final MethodHandle ccy_enabled_count = fn("zxv_ccy_enabled_count", I, A, A);
    public static final MethodHandle ccy_enabled_at = fn("zxv_ccy_enabled_at", I, A, I, A, L, A);
    // ---- ledger
    public static final MethodHandle ledger_open_account = fn("zxv_ledger_open_account", I, A, A, A, I, A);
    public static final MethodHandle ledger_account_ccy = fn("zxv_ledger_account_ccy", I, A, I, A, L, A);
    public static final MethodHandle ledger_account_name = fn("zxv_ledger_account_name", I, A, I, A, L, A);
    public static final MethodHandle ledger_account_kind = fn("zxv_ledger_account_kind", I, A, I, A);
    public static final MethodHandle ledger_account_count = fn("zxv_ledger_account_count", I, A, A);
    public static final MethodHandle ledger_entry_count = fn("zxv_ledger_entry_count", I, A, I, A);
    public static final MethodHandle ledger_entry_at = fn("zxv_ledger_entry_at", I, A, I, I, A, A, A, A, L, A);
    public static final MethodHandle ledger_post_cap = fn("zxv_ledger_post_cap", I, A, I, I, L, A, A, A);
    public static final MethodHandle ledger_balance_cap = fn("zxv_ledger_balance_cap", I, A, I, A, A, A);
    public static final MethodHandle ledger_check_cap = fn("zxv_ledger_check_cap", I, A);
    // ABI 1.2: the context surface through a capability.
    public static final MethodHandle ccy_enable_cap = fn("zxv_ccy_enable_cap", I, A, A);
    public static final MethodHandle ccy_enable_with_minor_cap = fn("zxv_ccy_enable_with_minor_cap", I, A, A, U8);
    public static final MethodHandle ccy_register_private_cap = fn("zxv_ccy_register_private_cap", I, A, A, U8);
    public static final MethodHandle ledger_open_account_cap = fn("zxv_ledger_open_account_cap", I, A, A, A, I, A);
    public static final MethodHandle ledger_account_ccy_cap = fn("zxv_ledger_account_ccy_cap", I, A, I, A, L, A);
    public static final MethodHandle ledger_account_name_cap = fn("zxv_ledger_account_name_cap", I, A, I, A, L, A);
    public static final MethodHandle ledger_account_kind_cap = fn("zxv_ledger_account_kind_cap", I, A, I, A);
    public static final MethodHandle ledger_account_count_cap = fn("zxv_ledger_account_count_cap", I, A, A);
    public static final MethodHandle ledger_entry_count_cap = fn("zxv_ledger_entry_count_cap", I, A, I, A);
    public static final MethodHandle ledger_entry_at_cap = fn("zxv_ledger_entry_at_cap", I, A, I, I, A, A, A, A, L, A);
    // ---- ISO 20022
    public static final MethodHandle msg_create = fn("zxv_msg_create", I, A, I, A);
    public static final MethodHandle msg_create_cap = fn("zxv_msg_create_cap", I, A, I, A);
    public static final MethodHandle msg_destroy = fn("zxv_msg_destroy", null, A);
    public static final MethodHandle msg_set_text = fn("zxv_msg_set_text", I, A, I, A);
    public static final MethodHandle msg_set_amount = fn("zxv_msg_set_amount", I, A, I, L, U8);
    public static final MethodHandle msg_set_tx_count = fn("zxv_msg_set_tx_count", I, A, I);
    public static final MethodHandle msg_add_entry = fn("zxv_msg_add_entry", I, A, L, U8, I, I);
    public static final MethodHandle msg_render = fn("zxv_msg_render", I, A, A, L, A);
    // ---- rationals (pure)
    public static final MethodHandle rat_normalize = pure("zxv_rat_normalize", I, L, L, A, A);
    public static final MethodHandle rat_add = pure("zxv_rat_add", I, L, L, L, L, A, A);
    public static final MethodHandle rat_sub = pure("zxv_rat_sub", I, L, L, L, L, A, A);
    public static final MethodHandle rat_mul = pure("zxv_rat_mul", I, L, L, L, L, A, A);
    public static final MethodHandle rat_div = pure("zxv_rat_div", I, L, L, L, L, A, A);
    public static final MethodHandle rat_cmp = pure("zxv_rat_cmp", I, L, L, L, L, A);
    public static final MethodHandle rat_parse = fn("zxv_rat_parse", I, A, A, A);
    public static final MethodHandle rat_format = fn("zxv_rat_format", I, L, L, A, L, A);
    public static final MethodHandle rat_from_ieee754 = pure("zxv_rat_from_ieee754", I, L, A, A);
    public static final MethodHandle rat_to_q16 = pure("zxv_rat_to_q16", I, L, L, A);
    // ---- four-valued logic (pure)
    public static final MethodHandle lp_not = pure("zxv_lp_not", I, I, A);
    public static final MethodHandle lp_and = pure("zxv_lp_and", I, I, I, A);
    public static final MethodHandle lp_or = pure("zxv_lp_or", I, I, I, A);
    public static final MethodHandle lp_from_evidence = pure("zxv_lp_from_evidence", I, I, I, A);
    // ---- capabilities
    public static final MethodHandle cap_create = fn("zxv_cap_create", I, A, I, A, L, A);
    public static final MethodHandle cap_attenuate = fn("zxv_cap_attenuate", I, A, L, A);
    public static final MethodHandle cap_revoke = fn("zxv_cap_revoke", I, A);
    public static final MethodHandle cap_set_attestation = fn("zxv_cap_set_attestation", I, A, I);
    public static final MethodHandle cap_info = fn("zxv_cap_info", I, A, A, A, A, A);
    public static final MethodHandle cap_check = fn("zxv_cap_check", I, A, L);
    public static final MethodHandle cap_destroy = fn("zxv_cap_destroy", null, A);
    // ---- attestations
    public static final MethodHandle lpres_create = fn("zxv_lpres_create", I, A, A);
    public static final MethodHandle lpres_create_cap = fn("zxv_lpres_create_cap", I, A, A);
    public static final MethodHandle lpres_destroy = fn("zxv_lpres_destroy", null, A);
    public static final MethodHandle lpres_attest = fn("zxv_lpres_attest", I, A, A, A, I, I, A, A, I, A);
    public static final MethodHandle lpres_evidence = fn("zxv_lpres_evidence", I, A, A, I, I);
    public static final MethodHandle lpres_find = fn("zxv_lpres_find", I, A, A, A);
    public static final MethodHandle lpres_state = fn("zxv_lpres_state", I, A, I, A, A, A);
    public static final MethodHandle lpres_gate = fn("zxv_lpres_gate", I, A, A);
    // ---- budgets
    public static final MethodHandle rmag_create = fn("zxv_rmag_create", I, A, A);
    public static final MethodHandle rmag_create_cap = fn("zxv_rmag_create_cap", I, A, A);
    public static final MethodHandle rmag_destroy = fn("zxv_rmag_destroy", null, A);
    public static final MethodHandle rmag_budget = fn("zxv_rmag_budget", I, A, A, A, I, L, L, I, A);
    public static final MethodHandle rmag_consume = fn("zxv_rmag_consume", I, A, A, I, L, L);
    public static final MethodHandle rmag_release = fn("zxv_rmag_release", I, A, A, I, L, L);
    public static final MethodHandle rmag_set_limit = fn("zxv_rmag_set_limit", I, A, A, I, L, L);
    public static final MethodHandle rmag_status = fn("zxv_rmag_status", I, A, I, A, A, A, A, A);
    public static final MethodHandle rmag_listen = fn("zxv_rmag_listen", I, A, A, A);
    // ---- phase
    public static final MethodHandle phase_create = fn("zxv_phase_create", I, A, A);
    public static final MethodHandle phase_create_cap = fn("zxv_phase_create_cap", I, A, A);
    public static final MethodHandle phase_destroy = fn("zxv_phase_destroy", null, A);
    public static final MethodHandle phase_now = fn("zxv_phase_now", I, A, A);
    public static final MethodHandle phase_advance = fn("zxv_phase_advance", I, A, A, L, A, A);
    public static final MethodHandle phase_admit = fn("zxv_phase_admit", I, A, A, I, I, I, A, A);
    public static final MethodHandle phase_set_health = fn("zxv_phase_set_health", I, A, A, I, I);
    public static final MethodHandle phase_defer = fn("zxv_phase_defer", I, A, A, L, A);
    public static final MethodHandle phase_event = fn("zxv_phase_event", I, A, L, A, A);
    public static final MethodHandle phase_cancel = fn("zxv_phase_cancel", I, A, A, L);
    public static final MethodHandle phase_next_due = fn("zxv_phase_next_due", I, A, A);
    // ---- wire (pure)
    public static final MethodHandle wire_tag = pure("zxv_wire_tag", I, I, I, I, A);
    public static final MethodHandle wire_pack = pure("zxv_wire_pack", I, U8, A, A, A);
    public static final MethodHandle wire_unpack = pure("zxv_wire_unpack", I, A, A, A, A);
    public static final MethodHandle wire_resolve = pure("zxv_wire_resolve", I, A, A);
    public static final MethodHandle wire_idle = pure("zxv_wire_idle", I, I, A);
    // ---- swarm
    public static final MethodHandle swarm_create = fn("zxv_swarm_create", I, A, I, L, A);
    public static final MethodHandle swarm_create_cap = fn("zxv_swarm_create_cap", I, A, I, L, A);
    public static final MethodHandle swarm_destroy = fn("zxv_swarm_destroy", null, A);
    public static final MethodHandle swarm_register = fn("zxv_swarm_register", I, A, A, I, I);
    public static final MethodHandle swarm_set_active = fn("zxv_swarm_set_active", I, A, A, I, I);
    public static final MethodHandle swarm_set_rate = fn("zxv_swarm_set_rate", I, A, A, L);
    public static final MethodHandle swarm_set_mood = fn("zxv_swarm_set_mood", I, A, A, I, I);
    public static final MethodHandle swarm_set_feeling = fn("zxv_swarm_set_feeling", I, A, A, I, I, I);
    public static final MethodHandle swarm_begin_cycle = fn("zxv_swarm_begin_cycle", I, A, A, A);
    public static final MethodHandle swarm_consume = fn("zxv_swarm_consume", I, A, A, I, L, A);
    public static final MethodHandle swarm_allotment = fn("zxv_swarm_allotment", I, A, I, A, A, A);
    public static final MethodHandle swarm_end_cycle = fn("zxv_swarm_end_cycle", I, A, A, A);
    public static final MethodHandle swarm_level_capacity = pure("zxv_swarm_level_capacity", I, I, A);
    public static final MethodHandle swarm_emotion_profile = fn("zxv_swarm_emotion_profile", I, I, A, A, A, A, A);
    // ---- hashes, random, secure memory
    public static final MethodHandle sha3_256 = pure("zxv_sha3_256", I, A, L, A);
    public static final MethodHandle shake256 = pure("zxv_shake256", I, A, L, A, L);
    public static final MethodHandle keccak256 = pure("zxv_keccak256", I, A, L, A);
    public static final MethodHandle random = fn("zxv_random", I, A, L);
    public static final MethodHandle secure_zero = fn("zxv_secure_zero", I, A, L);
    // ---- ML-KEM-1024
    public static final MethodHandle mlkem_keygen = fn("zxv_mlkem1024_keygen", I, A, A, A);
    public static final MethodHandle mlkem_keygen_derand = fn("zxv_mlkem1024_keygen_derand", I, A, A, A, A, A);
    public static final MethodHandle mlkem_encaps = fn("zxv_mlkem1024_encaps", I, A, A, A, A);
    public static final MethodHandle mlkem_encaps_derand = fn("zxv_mlkem1024_encaps_derand", I, A, A, A, A, A);
    public static final MethodHandle mlkem_decaps = fn("zxv_mlkem1024_decaps", I, A, A, A, A);
    // ---- ML-DSA-87
    public static final MethodHandle mldsa_keygen = fn("zxv_mldsa87_keygen", I, A, A, A);
    public static final MethodHandle mldsa_keygen_seed = fn("zxv_mldsa87_keygen_seed", I, A, A, A, A);
    public static final MethodHandle mldsa_sign = fn("zxv_mldsa87_sign", I, A, A, A, L, A, L, A);
    public static final MethodHandle mldsa_sign_det = fn("zxv_mldsa87_sign_deterministic", I, A, A, A, L, A, L, A);
    public static final MethodHandle mldsa_verify = fn("zxv_mldsa87_verify", I, A, A, A, L, A, L, A);
    // ---- Ed25519
    public static final MethodHandle ed_keygen = fn("zxv_ed25519_keygen", I, A, A, A);
    public static final MethodHandle ed_public = fn("zxv_ed25519_public", I, A, A, A);
    public static final MethodHandle ed_sign = fn("zxv_ed25519_sign", I, A, A, A, L, A);
    public static final MethodHandle ed_verify = fn("zxv_ed25519_verify", I, A, A, A, L, A);
    // ---- engine
    public static final MethodHandle model_open = fn("zxv_model_open", I, A, A, A, A);
    public static final MethodHandle model_open_memory = fn("zxv_model_open_memory", I, A, A, L, A, L, A);
    public static final MethodHandle model_destroy = fn("zxv_model_destroy", null, A);
    public static final MethodHandle model_info = fn("zxv_model_info", I, A, I, A);
    public static final MethodHandle tokenize = fn("zxv_tokenize", I, A, A, L, I, A, L, A);
    public static final MethodHandle detokenize = fn("zxv_detokenize", I, A, A, L, A, L, A);
    public static final MethodHandle session_create = fn("zxv_session_create", I, A, I, I, A);
    public static final MethodHandle session_destroy = fn("zxv_session_destroy", null, A);
    public static final MethodHandle session_reset = fn("zxv_session_reset", I, A);
    public static final MethodHandle session_position = fn("zxv_session_position", I, A, A);
    public static final MethodHandle session_bind_budget = fn("zxv_session_bind_budget", I, A, A, I);
    public static final MethodHandle generate = fn("zxv_generate", I,
            A, A, A, L, I, I, I, I, I, I, L, A, A, A, A, A);
    public static final MethodHandle lora_load = fn("zxv_lora_load", I, A, A, A, I, A);
    public static final MethodHandle lora_destroy = fn("zxv_lora_destroy", null, A);
    // ---- ABI 1.3: the 64-state hexagram tag (pure) and the quarantine log
    public static final MethodHandle hex_pack = pure("zxv_hex_pack", I, U8, U8, U8, A);
    public static final MethodHandle hex_unpack = pure("zxv_hex_unpack", I, U8, A, A, A);
    public static final MethodHandle hex_action = pure("zxv_hex_action", I, U8, A);
    public static final MethodHandle hex_phase_next = pure("zxv_hex_phase_next", I, U8, A);
    public static final MethodHandle hex_step = pure("zxv_hex_step", I, U8, U8, A);
    public static final MethodHandle hex_set = pure("zxv_hex_set", I, U8, U8, U8, A);
    public static final MethodHandle hex_forbid = pure("zxv_hex_forbid", I, U8, A);
    public static final MethodHandle hex_and = pure("zxv_hex_and", I, U8, U8, A);
    public static final MethodHandle hex_or = pure("zxv_hex_or", I, U8, U8, A);
    public static final MethodHandle hex_not = pure("zxv_hex_not", I, U8, A);
    public static final MethodHandle hex_polarity = pure("zxv_hex_polarity", I, I, I, I, I, I, A);
    public static final MethodHandle hex_from_lp = pure("zxv_hex_from_lp", I, I, A);
    public static final MethodHandle hex_to_lp = pure("zxv_hex_to_lp", I, U8, A);
    public static final MethodHandle hex_name = fn("zxv_hex_name", A, U8);
    public static final MethodHandle hex_action_name = fn("zxv_hex_action_name", A, I);
    public static final MethodHandle hexlog_create = fn("zxv_hexlog_create", I, A, I, A);
    public static final MethodHandle hexlog_destroy = fn("zxv_hexlog_destroy", null, A);
    public static final MethodHandle hexlog_append = fn("zxv_hexlog_append", I, A, A, U8, U8, I, A, A);
    public static final MethodHandle hexlog_count = fn("zxv_hexlog_count", I, A, A);
    public static final MethodHandle hexlog_head = fn("zxv_hexlog_head", I, A, A);
    public static final MethodHandle hexlog_at = fn("zxv_hexlog_at", I, A, I, A, A, A, A, A, A, A);
    public static final MethodHandle hexlog_verify = fn("zxv_hexlog_verify", I, A);
    // ---- ABI 1.4: hardware emulation (ZHP-1 profiles, the matrix) and hardware updates
    public static final MethodHandle hw_profile_create = fn("zxv_hw_profile_create", I, A);
    public static final MethodHandle hw_profile_destroy = fn("zxv_hw_profile_destroy", null, A);
    public static final MethodHandle hw_profile_probe_host = fn("zxv_hw_profile_probe_host", I, A);
    public static final MethodHandle hw_profile_reference = fn("zxv_hw_profile_reference", I, A);
    public static final MethodHandle hw_profile_present = fn("zxv_hw_profile_present", I, A, A, A);
    public static final MethodHandle hw_profile_decode = fn("zxv_hw_profile_decode", I, A, L, A);
    public static final MethodHandle hw_profile_encode = fn("zxv_hw_profile_encode", I, A, A, L, A);
    public static final MethodHandle hw_profile_digest = fn("zxv_hw_profile_digest", I, A, A);
    public static final MethodHandle hw_profile_node_add = fn("zxv_hw_profile_node_add", I, A, I, I, I);
    public static final MethodHandle hw_profile_cap_set = fn("zxv_hw_profile_cap_set", I, A, I, I, L);
    public static final MethodHandle hw_profile_cap_get = fn("zxv_hw_profile_cap_get", I, A, I, I, A);
    public static final MethodHandle hw_profile_ext_set = fn("zxv_hw_profile_ext_set", I, A, I, A, L);
    public static final MethodHandle hw_profile_ext_get = fn("zxv_hw_profile_ext_get", I, A, I, A, L, A);
    public static final MethodHandle hw_profile_count = fn("zxv_hw_profile_count", I, A, A);
    public static final MethodHandle hw_profile_node = fn("zxv_hw_profile_node", I, A, I, A, A, A, A);
    public static final MethodHandle hw_profile_match = fn("zxv_hw_profile_match", I, A, A, A, A);
    public static final MethodHandle hw_profile_pair = fn("zxv_hw_profile_pair", I, A, A, I, A, A, A, A);
    public static final MethodHandle hw_matrix_create_cap = fn("zxv_hw_matrix_create_cap", I, A, A);
    public static final MethodHandle hw_matrix_destroy = fn("zxv_hw_matrix_destroy", null, A);
    public static final MethodHandle hw_function_add = fn("zxv_hw_function_add", I, A, A, I, A);
    public static final MethodHandle hw_vector_add = fn("zxv_hw_vector_add", I, A, A, I, I, A, L, A, L);
    public static final MethodHandle hw_function_contract = fn("zxv_hw_function_contract", I, A, A, I, I);
    public static final MethodHandle hw_policy_set = fn("zxv_hw_policy_set", I, A, A, I, I, I, I);
    public static final MethodHandle hw_function_info = fn("zxv_hw_function_info", I, A, A, I, A, A, A);
    public static final MethodHandle hw_resource_add = fn("zxv_hw_resource_add", I, A, A, I);
    public static final MethodHandle hw_resource_set = fn("zxv_hw_resource_set", I, A, A, I, I);
    public static final MethodHandle hw_provider_add = fn("zxv_hw_provider_add", I, A, A, I, I, I, I, A, A, A);
    public static final MethodHandle hw_provider_traits = fn("zxv_hw_provider_traits", I, A, A, I, I, I, I);
    public static final MethodHandle hw_provider_dep = fn("zxv_hw_provider_dep", I, A, A, I, I);
    public static final MethodHandle hw_provider_enable = fn("zxv_hw_provider_enable", I, A, A, I, I);
    public static final MethodHandle hw_attest = fn("zxv_hw_attest", I, A, A, I, A);
    public static final MethodHandle hw_provider_count = fn("zxv_hw_provider_count", I, A, A, A);
    public static final MethodHandle hw_provider_info = fn("zxv_hw_provider_info", I, A, A, I, A, A, A, A, A, A, A, A, A);
    public static final MethodHandle hw_bound = fn("zxv_hw_bound", I, A, A, I, A);
    public static final MethodHandle hw_call = fn("zxv_hw_call", I, A, A, I, I, A, L, A, L, A);
    public static final MethodHandle hw_shadow = fn("zxv_hw_shadow", I, A, A, I, I, A, L, A, L, A, A);
    public static final MethodHandle hw_memo = fn("zxv_hw_memo", I, A, A, I, I, A, A);
    public static final MethodHandle hw_keygen = fn("zxv_hw_keygen", I, A, A, A, A);
    public static final MethodHandle hw_image_build = fn("zxv_hw_image_build", I, A, I, I, A, I, I, I, A, L, A, A, A, L, A);
    public static final MethodHandle hw_matrix_trust_key = fn("zxv_hw_matrix_trust_key", I, A, A, A);
    public static final MethodHandle hw_contract_apply = fn("zxv_hw_contract_apply", I, A, A, A, L, A);
    public static final MethodHandle hw_region_create_cap = fn("zxv_hw_region_create_cap", I, A, A, I, I, I, A, A, A, A, A, I, A);
    public static final MethodHandle hw_region_destroy = fn("zxv_hw_region_destroy", null, A);
    public static final MethodHandle hw_region_events = fn("zxv_hw_region_events", I, A, A, A, A);
    public static final MethodHandle hw_region_trust_key = fn("zxv_hw_region_trust_key", I, A, A, A);
    public static final MethodHandle hw_region_stage = fn("zxv_hw_region_stage", I, A, A, A, L);
    public static final MethodHandle hw_region_trial = fn("zxv_hw_region_trial", I, A, A, I);
    public static final MethodHandle hw_region_commit = fn("zxv_hw_region_commit", I, A, A);
    public static final MethodHandle hw_region_rollback = fn("zxv_hw_region_rollback", I, A, A);
    public static final MethodHandle hw_region_tick = fn("zxv_hw_region_tick", I, A, A);
    public static final MethodHandle hw_region_info = fn("zxv_hw_region_info", I, A, A, A, A, A, A);
    public static final MethodHandle hw_region_log = fn("zxv_hw_region_log", I, A, A, A, A);
    public static final MethodHandle hw_region_log_entry = fn("zxv_hw_region_log_entry", I, A, A, I, A, A, A, A, A, A, A);
    public static final MethodHandle hw_matrix_log = fn("zxv_hw_matrix_log", I, A, A, A, A);
    public static final MethodHandle hw_matrix_log_entry = fn("zxv_hw_matrix_log_entry", I, A, A, I, A, A, A, A, A, A, A);
    // ---- EVM
    public static final MethodHandle evm_encode_state = fn("zxv_evm_encode_state", I, A, A, L, A, A, I, A, L, A, A);
    public static final MethodHandle evm_ledger_state = fn("zxv_evm_ledger_state", I, A, A, L, A, A, A, I, A, L, A, A);

    static {
        // The version gate of SDK_DESIGN section 1: same major, minor >= 4 (the _cap forms of
        // ABI 1.2, the hexagram tag of ABI 1.3 and the hardware emulation of ABI 1.4).
        try (var arena = java.lang.foreign.Arena.ofConfined()) {
            MemorySegment ma = arena.allocate(JAVA_INT), mi = arena.allocate(JAVA_INT), pa = arena.allocate(JAVA_INT);
            int st = (int) version.invokeExact(ma, mi, pa);
            int major = ma.get(JAVA_INT, 0), minor = mi.get(JAVA_INT, 0);
            if (st != 0 || major != 1 || minor < 4) {
                throw new UnsatisfiedLinkError("libzxv at " + NativeLoader.loadedFrom() + " reports ABI "
                        + major + "." + minor + "; this SDK needs ABI 1.x with x >= 4");
            }
        } catch (UnsatisfiedLinkError e) {
            throw e;
        } catch (Throwable t) {
            throw new UnsatisfiedLinkError("libzxv version check failed: " + t);
        }
    }
}
