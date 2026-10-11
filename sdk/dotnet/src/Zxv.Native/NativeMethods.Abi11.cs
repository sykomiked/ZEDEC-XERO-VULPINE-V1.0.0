// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.1 groups of <c>zxv_api.h</c>: status classes, context halting,
/// rationals, four-valued logic, capabilities, attestations, budgets, phase
/// ticks, UBH-168 wire frames, the swarm economy, hashes, post-quantum and
/// classical signatures, the integer inference engine and EVM encoding.
/// </summary>
/// <remarks>
/// Buffers cross as raw pointers so callers can pass <see cref="Span{T}"/>
/// memory (pinned with <c>fixed</c>) and off-heap secret buffers alike.
/// Callback parameters are unmanaged function pointers to
/// <see cref="System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute"/> methods.
/// </remarks>
internal static unsafe partial class NativeMethods
{
    // ----- Library / context (1.1) -----
    [LibraryImport(Library, EntryPoint = "zxv_status_class")]
    internal static partial int StatusClass(int status);

    [LibraryImport(Library, EntryPoint = "zxv_ctx_is_poisoned")]
    internal static partial int CtxIsPoisoned(ZxvContextHandle ctx, out int poisoned);

    [LibraryImport(Library, EntryPoint = "zxv_ctx_halt", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CtxHalt(ZxvContextHandle ctx, string? reason);

    // ----- Rationals -----
    [LibraryImport(Library, EntryPoint = "zxv_rat_normalize")]
    internal static partial int RatNormalize(long num, long den, out long outNum, out long outDen);

    [LibraryImport(Library, EntryPoint = "zxv_rat_add")]
    internal static partial int RatAdd(long an, long ad, long bn, long bd, out long outNum, out long outDen);

    [LibraryImport(Library, EntryPoint = "zxv_rat_sub")]
    internal static partial int RatSub(long an, long ad, long bn, long bd, out long outNum, out long outDen);

    [LibraryImport(Library, EntryPoint = "zxv_rat_mul")]
    internal static partial int RatMul(long an, long ad, long bn, long bd, out long outNum, out long outDen);

    [LibraryImport(Library, EntryPoint = "zxv_rat_div")]
    internal static partial int RatDiv(long an, long ad, long bn, long bd, out long outNum, out long outDen);

    [LibraryImport(Library, EntryPoint = "zxv_rat_cmp")]
    internal static partial int RatCmp(long an, long ad, long bn, long bd, out int cmp);

    [LibraryImport(Library, EntryPoint = "zxv_rat_parse", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int RatParse(string text, out long num, out long den);

    [LibraryImport(Library, EntryPoint = "zxv_rat_format")]
    internal static partial int RatFormat(long num, long den, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_rat_from_ieee754")]
    internal static partial int RatFromIeee754(ulong bits, out long num, out long den);

    [LibraryImport(Library, EntryPoint = "zxv_rat_to_q16")]
    internal static partial int RatToQ16(long num, long den, out int q16);

    // ----- Four-valued logic -----
    [LibraryImport(Library, EntryPoint = "zxv_lp_not")]
    internal static partial int LpNot(int a, out int result);

    [LibraryImport(Library, EntryPoint = "zxv_lp_and")]
    internal static partial int LpAnd(int a, int b, out int result);

    [LibraryImport(Library, EntryPoint = "zxv_lp_or")]
    internal static partial int LpOr(int a, int b, out int result);

    [LibraryImport(Library, EntryPoint = "zxv_lp_from_evidence")]
    internal static partial int LpFromEvidence(uint positive, uint negative, out int result);

    [LibraryImport(Library, EntryPoint = "zxv_lp_name")]
    internal static partial byte* LpName(int value);

    // ----- Capabilities -----
    [LibraryImport(Library, EntryPoint = "zxv_cap_create")]
    internal static partial int CapCreate(ZxvContextHandle ctx, uint processId, byte* identity, ulong caps, out ZxvCapabilityHandle cap);

    [LibraryImport(Library, EntryPoint = "zxv_cap_attenuate")]
    internal static partial int CapAttenuate(ZxvCapabilityHandle parent, ulong keep, out ZxvCapabilityHandle cap);

    [LibraryImport(Library, EntryPoint = "zxv_cap_revoke")]
    internal static partial int CapRevoke(ZxvCapabilityHandle cap);

    [LibraryImport(Library, EntryPoint = "zxv_cap_set_attestation")]
    internal static partial int CapSetAttestation(ZxvCapabilityHandle cap, int lpState);

    [LibraryImport(Library, EntryPoint = "zxv_cap_info")]
    internal static partial int CapInfo(ZxvCapabilityHandle cap, out ulong caps, out uint processId, out int attestation, out int revoked);

    [LibraryImport(Library, EntryPoint = "zxv_cap_check")]
    internal static partial int CapCheck(ZxvCapabilityHandle cap, ulong needed);

    [LibraryImport(Library, EntryPoint = "zxv_cap_destroy")]
    internal static partial void CapDestroy(IntPtr cap);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_post_cap", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LedgerPostCap(ZxvCapabilityHandle cap, uint fromAccount, uint toAccount, long amountMinor, string ccy, string reference, out ulong entryId);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_balance_cap")]
    internal static partial int LedgerBalanceCap(ZxvCapabilityHandle cap, uint account, out long debitMinor, out long creditMinor, out long equityMinor);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_check_cap")]
    internal static partial int LedgerCheckCap(ZxvCapabilityHandle cap);

    // ----- Attestation registry -----
    [LibraryImport(Library, EntryPoint = "zxv_lpres_create")]
    internal static partial int LpresCreate(ZxvContextHandle ctx, out ZxvAttestationsHandle reg);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_destroy")]
    internal static partial void LpresDestroy(IntPtr reg);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_attest", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LpresAttest(ZxvCapabilityHandle cap, ZxvAttestationsHandle reg, string name, int subject, int lpState, string attester, byte* digest, uint digestLen, out uint index);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_evidence")]
    internal static partial int LpresEvidence(ZxvCapabilityHandle cap, ZxvAttestationsHandle reg, uint index, int positive);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_find", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LpresFind(ZxvAttestationsHandle reg, string name, out uint index);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_state")]
    internal static partial int LpresState(ZxvAttestationsHandle reg, uint index, out int lpState, out uint positive, out uint negative);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_gate")]
    internal static partial int LpresGate(ZxvAttestationsHandle reg, out int clear);

    // ----- Rational resource budgets -----
    [LibraryImport(Library, EntryPoint = "zxv_rmag_create")]
    internal static partial int RmagCreate(ZxvContextHandle ctx, out ZxvBudgetsHandle rmag);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_destroy")]
    internal static partial void RmagDestroy(IntPtr rmag);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_budget", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int RmagBudget(ZxvCapabilityHandle cap, ZxvBudgetsHandle rmag, string name, int resource, long limitNum, long limitDen, int enforce, out uint budget);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_consume")]
    internal static partial int RmagConsume(ZxvCapabilityHandle cap, ZxvBudgetsHandle rmag, uint budget, long num, long den);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_release")]
    internal static partial int RmagRelease(ZxvCapabilityHandle cap, ZxvBudgetsHandle rmag, uint budget, long num, long den);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_set_limit")]
    internal static partial int RmagSetLimit(ZxvCapabilityHandle cap, ZxvBudgetsHandle rmag, uint budget, long num, long den);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_status")]
    internal static partial int RmagStatus(ZxvBudgetsHandle rmag, uint budget, out long remNum, out long remDen, out long usedNum, out long usedDen, out ulong epoch);

    [LibraryImport(Library, EntryPoint = "zxv_rmag_listen")]
    internal static partial int RmagListen(ZxvBudgetsHandle rmag, delegate* unmanaged[Cdecl]<void*, uint, int, void> fn, void* user);

    // ----- Phase coordinator -----
    [LibraryImport(Library, EntryPoint = "zxv_phase_create")]
    internal static partial int PhaseCreate(ZxvContextHandle ctx, out ZxvPhaseHandle phase);

    [LibraryImport(Library, EntryPoint = "zxv_phase_destroy")]
    internal static partial void PhaseDestroy(IntPtr phase);

    [LibraryImport(Library, EntryPoint = "zxv_phase_now")]
    internal static partial int PhaseNow(ZxvPhaseHandle phase, out ulong tick);

    [LibraryImport(Library, EntryPoint = "zxv_phase_advance")]
    internal static partial int PhaseAdvance(ZxvCapabilityHandle cap, ZxvPhaseHandle phase, ulong delta, out ulong now, out uint cleared);

    [LibraryImport(Library, EntryPoint = "zxv_phase_admit")]
    internal static partial int PhaseAdmit(ZxvCapabilityHandle cap, ZxvPhaseHandle phase, uint phaseId, uint stepId, uint flags, out int decision, out ulong token);

    [LibraryImport(Library, EntryPoint = "zxv_phase_set_health")]
    internal static partial int PhaseSetHealth(ZxvCapabilityHandle cap, ZxvPhaseHandle phase, uint phaseId, int health);

    [LibraryImport(Library, EntryPoint = "zxv_phase_defer")]
    internal static partial int PhaseDefer(ZxvCapabilityHandle cap, ZxvPhaseHandle phase, ulong dueTick, out ulong eventId);

    [LibraryImport(Library, EntryPoint = "zxv_phase_event")]
    internal static partial int PhaseEvent(ZxvPhaseHandle phase, ulong eventId, out int state, out ulong dueTick);

    [LibraryImport(Library, EntryPoint = "zxv_phase_cancel")]
    internal static partial int PhaseCancel(ZxvCapabilityHandle cap, ZxvPhaseHandle phase, ulong eventId);

    [LibraryImport(Library, EntryPoint = "zxv_phase_next_due")]
    internal static partial int PhaseNextDue(ZxvPhaseHandle phase, out ulong tick);

    // ----- UBH-168 wire -----
    [LibraryImport(Library, EntryPoint = "zxv_wire_tag")]
    internal static partial int WireTag(uint trunk, uint shell, uint sync, out byte tag);

    [LibraryImport(Library, EntryPoint = "zxv_wire_pack")]
    internal static partial int WirePack(byte tag, uint* sPlus, uint* sMinus, byte* frame);

    [LibraryImport(Library, EntryPoint = "zxv_wire_unpack")]
    internal static partial int WireUnpack(byte* frame, out byte tag, uint* sPlus, uint* sMinus);

    [LibraryImport(Library, EntryPoint = "zxv_wire_resolve")]
    internal static partial int WireResolve(byte* frame, out int truth);

    [LibraryImport(Library, EntryPoint = "zxv_wire_idle")]
    internal static partial int WireIdle(uint trunk, byte* frame);

    // ----- Swarm economy -----
    [LibraryImport(Library, EntryPoint = "zxv_swarm_create")]
    internal static partial int SwarmCreate(ZxvContextHandle ctx, uint numLevels, ulong tokensPerCycle, out ZxvSwarmHandle swarm);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_destroy")]
    internal static partial void SwarmDestroy(IntPtr swarm);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_register")]
    internal static partial int SwarmRegister(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, uint modelId, uint level);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_set_active")]
    internal static partial int SwarmSetActive(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, uint modelId, int active);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_set_rate")]
    internal static partial int SwarmSetRate(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, ulong tokensPerCycle);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_set_mood")]
    internal static partial int SwarmSetMood(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, uint codepoint, uint intensity);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_set_feeling")]
    internal static partial int SwarmSetFeeling(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, uint modelId, uint codepoint, uint intensity);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_begin_cycle")]
    internal static partial int SwarmBeginCycle(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, out ulong cycle);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_consume")]
    internal static partial int SwarmConsume(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, uint modelId, ulong requested, out ulong granted);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_allotment")]
    internal static partial int SwarmAllotment(ZxvSwarmHandle swarm, uint modelId, out ulong allotted, out ulong imaginary, out ulong used);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_end_cycle")]
    internal static partial int SwarmEndCycle(ZxvCapabilityHandle cap, ZxvSwarmHandle swarm, out ulong expired);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_level_capacity")]
    internal static partial int SwarmLevelCapacity(uint level, out uint capacity);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_emotion_profile")]
    internal static partial int SwarmEmotionProfile(uint codepoint, out int valence, out uint temperatureMilli, out uint explorePermille, out uint verifyPasses, out uint priority);

    // ----- Hashes and secure memory -----
    [LibraryImport(Library, EntryPoint = "zxv_sha3_256")]
    internal static partial int Sha3_256(byte* data, nuint len, byte* output);

    [LibraryImport(Library, EntryPoint = "zxv_shake256")]
    internal static partial int Shake256(byte* data, nuint len, byte* output, nuint outputLen);

    [LibraryImport(Library, EntryPoint = "zxv_keccak256")]
    internal static partial int Keccak256(byte* data, nuint len, byte* output);

    [LibraryImport(Library, EntryPoint = "zxv_random")]
    internal static partial int Random(byte* buf, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_secure_zero")]
    internal static partial int SecureZero(void* buf, nuint len);

    // ----- ML-KEM-1024 -----
    [LibraryImport(Library, EntryPoint = "zxv_mlkem1024_keygen")]
    internal static partial int MlKemKeygen(ZxvCapabilityHandle cap, byte* ek, byte* dk);

    [LibraryImport(Library, EntryPoint = "zxv_mlkem1024_keygen_derand")]
    internal static partial int MlKemKeygenDerand(ZxvCapabilityHandle cap, byte* d, byte* z, byte* ek, byte* dk);

    [LibraryImport(Library, EntryPoint = "zxv_mlkem1024_encaps")]
    internal static partial int MlKemEncaps(ZxvCapabilityHandle cap, byte* ek, byte* ct, byte* ss);

    [LibraryImport(Library, EntryPoint = "zxv_mlkem1024_encaps_derand")]
    internal static partial int MlKemEncapsDerand(ZxvCapabilityHandle cap, byte* ek, byte* m, byte* ct, byte* ss);

    [LibraryImport(Library, EntryPoint = "zxv_mlkem1024_decaps")]
    internal static partial int MlKemDecaps(ZxvCapabilityHandle cap, byte* dk, byte* ct, byte* ss);

    // ----- ML-DSA-87 -----
    [LibraryImport(Library, EntryPoint = "zxv_mldsa87_keygen")]
    internal static partial int MlDsaKeygen(ZxvCapabilityHandle cap, byte* pk, byte* sk);

    [LibraryImport(Library, EntryPoint = "zxv_mldsa87_keygen_seed")]
    internal static partial int MlDsaKeygenSeed(ZxvCapabilityHandle cap, byte* seed, byte* pk, byte* sk);

    [LibraryImport(Library, EntryPoint = "zxv_mldsa87_sign")]
    internal static partial int MlDsaSign(ZxvCapabilityHandle cap, byte* sk, byte* msg, nuint msgLen, byte* ctx, nuint ctxLen, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_mldsa87_sign_deterministic")]
    internal static partial int MlDsaSignDeterministic(ZxvCapabilityHandle cap, byte* sk, byte* msg, nuint msgLen, byte* ctx, nuint ctxLen, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_mldsa87_verify")]
    internal static partial int MlDsaVerify(ZxvCapabilityHandle cap, byte* pk, byte* msg, nuint msgLen, byte* ctx, nuint ctxLen, byte* sig);

    // ----- Ed25519 -----
    [LibraryImport(Library, EntryPoint = "zxv_ed25519_keygen")]
    internal static partial int Ed25519Keygen(ZxvCapabilityHandle cap, byte* pk, byte* sk);

    [LibraryImport(Library, EntryPoint = "zxv_ed25519_public")]
    internal static partial int Ed25519Public(ZxvCapabilityHandle cap, byte* sk, byte* pk);

    [LibraryImport(Library, EntryPoint = "zxv_ed25519_sign")]
    internal static partial int Ed25519Sign(ZxvCapabilityHandle cap, byte* sk, byte* msg, nuint msgLen, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_ed25519_verify")]
    internal static partial int Ed25519Verify(ZxvCapabilityHandle cap, byte* pk, byte* msg, nuint msgLen, byte* sig);

    // ----- Engine -----
    [LibraryImport(Library, EntryPoint = "zxv_model_open", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int ModelOpen(ZxvCapabilityHandle cap, string modelPath, string? tokenizerPath, out ZxvModelHandle model);

    [LibraryImport(Library, EntryPoint = "zxv_model_open_memory")]
    internal static partial int ModelOpenMemory(ZxvCapabilityHandle cap, byte* model, nuint modelLen, byte* tokenizer, nuint tokenizerLen, out ZxvModelHandle handle);

    [LibraryImport(Library, EntryPoint = "zxv_model_destroy")]
    internal static partial void ModelDestroy(IntPtr model);

    [LibraryImport(Library, EntryPoint = "zxv_model_info")]
    internal static partial int ModelInfo(ZxvModelHandle model, int key, out long value);

    [LibraryImport(Library, EntryPoint = "zxv_tokenize")]
    internal static partial int Tokenize(ZxvModelHandle model, byte* text, nuint len, int parseSpecial, int* ids, nuint cap, out nuint count);

    [LibraryImport(Library, EntryPoint = "zxv_detokenize")]
    internal static partial int Detokenize(ZxvModelHandle model, int* ids, nuint count, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_session_create")]
    internal static partial int SessionCreate(ZxvModelHandle model, uint nCtx, int kvType, out ZxvSessionHandle session);

    [LibraryImport(Library, EntryPoint = "zxv_session_destroy")]
    internal static partial void SessionDestroy(IntPtr session);

    [LibraryImport(Library, EntryPoint = "zxv_session_reset")]
    internal static partial int SessionReset(ZxvSessionHandle session);

    [LibraryImport(Library, EntryPoint = "zxv_session_position")]
    internal static partial int SessionPosition(ZxvSessionHandle session, out uint position);

    [LibraryImport(Library, EntryPoint = "zxv_session_bind_budget")]
    internal static partial int SessionBindBudget(ZxvSessionHandle session, IntPtr rmag, uint budget);

    [LibraryImport(Library, EntryPoint = "zxv_generate")]
    internal static partial int Generate(
        ZxvCapabilityHandle cap,
        ZxvSessionHandle session,
        byte* prompt,
        nuint promptLen,
        uint maxTokens,
        int temperatureQ16,
        uint topK,
        int topPQ16,
        int repeatPenaltyQ16,
        uint repeatLastN,
        ulong seed,
        delegate* unmanaged[Cdecl]<void*, int, byte*, nuint, int> onToken,
        void* user,
        out uint nPrompt,
        out uint nGenerated,
        out int stop);

    [LibraryImport(Library, EntryPoint = "zxv_lora_load", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LoraLoad(ZxvCapabilityHandle cap, ZxvModelHandle model, string path, int scaleQ16, out ZxvLoraHandle lora);

    [LibraryImport(Library, EntryPoint = "zxv_lora_apply")]
    internal static partial int LoraApply(ZxvCapabilityHandle cap, ZxvSessionHandle session, ZxvLoraHandle lora);

    [LibraryImport(Library, EntryPoint = "zxv_lora_destroy")]
    internal static partial void LoraDestroy(IntPtr lora);

    // ----- EVM state channels -----
    [LibraryImport(Library, EntryPoint = "zxv_evm_encode_state")]
    internal static partial int EvmEncodeState(ZxvCapabilityHandle cap, byte* channelId, ulong nonce, byte* addresses, long* balances, uint count, byte* buf, nuint capBytes, out nuint len, byte* digest);

    [LibraryImport(Library, EntryPoint = "zxv_evm_ledger_state", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int EvmLedgerState(ZxvCapabilityHandle cap, byte* channelId, ulong nonce, string ccy, uint* accounts, byte* addresses, uint count, byte* buf, nuint capBytes, out nuint len, byte* digest);
}
