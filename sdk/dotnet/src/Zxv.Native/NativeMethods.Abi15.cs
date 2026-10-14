// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.5 groups of <c>zxv_api.h</c>: <c>zxv_features2</c>, treaty,
/// cid and aipi, coin and token, fee, vhub, freight, web4 and paradox.
/// </summary>
/// <remarks>
/// <c>uint8_t</c> is <see cref="byte"/>, <c>uint32_t</c> <see cref="uint"/>,
/// <c>uint64_t</c> <see cref="ulong"/>, <c>int64_t</c> <see cref="long"/>,
/// <c>size_t</c> <see cref="nuint"/>; fixed byte arrays and caller buffers are
/// <c>byte*</c> to pinned memory. A handle parameter the C header allows to
/// be NULL (a web4 node, a paradox book in <c>zxv_paradox_classify</c>) is an
/// <see cref="IntPtr"/>, and the caller holds a reference on its SafeHandle
/// for the duration of the call.
/// </remarks>
internal static unsafe partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "zxv_features2")]
    internal static partial int Features2(out uint mask);

    // ===== treaty =====
    [LibraryImport(Library, EntryPoint = "zxv_treaty_create")]
    internal static partial int TreatyCreate(out ZxvTreatyHandle treaty);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_destroy")]
    internal static partial void TreatyDestroy(IntPtr treaty);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_clear")]
    internal static partial int TreatyClear(ZxvTreatyHandle treaty);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_parse_sutra")]
    internal static partial int TreatyParseSutra(ZxvTreatyHandle treaty, byte* text, out uint faultOffset, out int fault);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_parse_record")]
    internal static partial int TreatyParseRecord(ZxvTreatyHandle treaty, byte* rec, nuint len, out uint faultOffset, out int fault);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_emit_sutra")]
    internal static partial int TreatyEmitSutra(ZxvTreatyHandle treaty, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_emit_record")]
    internal static partial int TreatyEmitRecord(ZxvTreatyHandle treaty, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_validate")]
    internal static partial int TreatyValidate(ZxvTreatyHandle treaty, out int fault);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_equal")]
    internal static partial int TreatyEqual(ZxvTreatyHandle a, ZxvTreatyHandle b, out int equal);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_name")]
    internal static partial int TreatyName(ZxvTreatyHandle treaty, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_asset")]
    internal static partial int TreatyAsset(ZxvTreatyHandle treaty, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_header")]
    internal static partial int TreatyHeader(ZxvTreatyHandle treaty, out int op, out uint parties, out uint owes);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_party_id")]
    internal static partial int TreatyPartyId(ZxvTreatyHandle treaty, uint index, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_party")]
    internal static partial int TreatyParty(ZxvTreatyHandle treaty, uint index, out long deposit, out long claim, out uint weightNum, out uint weightDen);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_owe")]
    internal static partial int TreatyOwe(ZxvTreatyHandle treaty, uint index, out uint from, out uint to, out long amount);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_pic_add")]
    internal static partial int TreatyPicAdd(long a, long b, out long result);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_pic_sub")]
    internal static partial int TreatyPicSub(long a, long b, out long result);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_pic_mul")]
    internal static partial int TreatyPicMul(long a, long b, out long result);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_pic_div")]
    internal static partial int TreatyPicDiv(long a, long b, out long quotient, out long remainder);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_run")]
    internal static partial int TreatyRun(ZxvTreatyHandle treaty, out ZxvTreatyResultHandle result);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_result_destroy")]
    internal static partial void TreatyResultDestroy(IntPtr result);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_result_summary")]
    internal static partial int TreatyResultSummary(ZxvTreatyResultHandle result, out uint parties, out uint legs, out long fees);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_result_party")]
    internal static partial int TreatyResultParty(ZxvTreatyResultHandle result, uint index, out long net, out long balance, out long dividend);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_result_leg")]
    internal static partial int TreatyResultLeg(ZxvTreatyResultHandle result, uint index, out uint from, out uint to, out long amount);

    [LibraryImport(Library, EntryPoint = "zxv_treaty_result_record")]
    internal static partial int TreatyResultRecord(ZxvTreatyResultHandle result, byte* buf, nuint cap, out nuint len);

    // ===== cid =====
    [LibraryImport(Library, EntryPoint = "zxv_cid_compute")]
    internal static partial int CidCompute(uint codec, byte* data, nuint len, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_cid_unixfs")]
    internal static partial int CidUnixfs(byte* data, nuint len, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_cid_verify")]
    internal static partial int CidVerify(byte* cid, nuint cidLen, byte* data, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_cid_to_text")]
    internal static partial int CidToText(byte* cid, nuint cidLen, uint @base, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_cid_from_text")]
    internal static partial int CidFromText(byte* text, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_cid_info")]
    internal static partial int CidInfo(byte* cid, nuint cidLen, out uint version, out uint codec, out uint hash, byte* digest, out nuint digestLen);

    [LibraryImport(Library, EntryPoint = "zxv_cid_to_v1")]
    internal static partial int CidToV1(byte* cid, nuint cidLen, byte* outCid, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_provider_key")]
    internal static partial int AipiProviderKey(byte* cid, nuint cidLen, byte* key);

    // ===== aipi records =====
    [LibraryImport(Library, EntryPoint = "zxv_aipi_fn_encode")]
    internal static partial int AipiFnEncode(uint intent, uint verify, uint privacy, uint determinism, uint quorumN, uint quorumK, ulong fuelMax, byte* moduleCid, nuint moduleCidLen, byte* contractCid, nuint contractCidLen, byte* entry, byte* checker, byte* certifier, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_fn_decode")]
    internal static partial int AipiFnDecode(byte* rec, nuint len, out uint intent, out uint verify, out uint privacy, out uint determinism, out uint quorumN, out uint quorumK, out ulong fuelMax);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_fn_get_cid")]
    internal static partial int AipiFnGetCid(byte* rec, nuint len, uint field, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_fn_get_name")]
    internal static partial int AipiFnGetName(byte* rec, nuint len, uint field, byte* buf, nuint cap, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_fn_cid")]
    internal static partial int AipiFnCid(byte* rec, nuint len, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_call_encode")]
    internal static partial int AipiCallEncode(byte* fnCid, nuint fnCidLen, byte* input, nuint inLen, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_call_decode")]
    internal static partial int AipiCallDecode(byte* rec, nuint len, out nuint fnCidOff, out nuint fnCidLen, out nuint inputOff, out nuint inputLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_call_cid")]
    internal static partial int AipiCallCid(byte* fnCid, nuint fnCidLen, byte* input, nuint inLen, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_claim_encode")]
    internal static partial int AipiClaimEncode(byte* callCid, nuint callCidLen, byte* executor, byte* output, nuint outputLen, byte* cert, nuint certLen, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_claim_decode")]
    internal static partial int AipiClaimDecode(byte* rec, nuint len, out nuint callOff, out nuint callLen, out nuint executorOff, out nuint outputOff, out nuint outputLen, out nuint certOff, out nuint certLen);

    // ===== aipi node =====
    [LibraryImport(Library, EntryPoint = "zxv_aipi_create")]
    internal static partial int AipiCreate(ZxvCapabilityHandle cap, nuint storeBytes, uint maxBlocks, byte* nodeKey, out ZxvAipiHandle node);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_destroy")]
    internal static partial void AipiDestroy(IntPtr node);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_put")]
    internal static partial int AipiPut(ZxvCapabilityHandle cap, ZxvAipiHandle node, byte* data, nuint len, int isPrivate, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_get")]
    internal static partial int AipiGet(ZxvAipiHandle node, byte* cid, nuint cidLen, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_publish_fn")]
    internal static partial int AipiPublishFn(ZxvCapabilityHandle cap, ZxvAipiHandle node, byte* rec, nuint len, byte* cid, out nuint cidLen);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_load_fn")]
    internal static partial int AipiLoadFn(ZxvAipiHandle node, byte* fnCid, nuint fnCidLen, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_plan")]
    internal static partial int AipiPlan(ZxvAipiHandle node, byte* fnCid, nuint fnCidLen, byte* input, nuint inLen, int inputPrivate, out int route);

    [LibraryImport(Library, EntryPoint = "zxv_aipi_info")]
    internal static partial int AipiInfo(ZxvAipiHandle node, out uint blocks, out ulong storeUsed, out uint getsVerified, out uint forbidden);

    // ===== coin and token =====
    [LibraryImport(Library, EntryPoint = "zxv_coin_face")]
    internal static partial int CoinFace(uint rung, out ulong face);

    [LibraryImport(Library, EntryPoint = "zxv_coin_rung")]
    internal static partial int CoinRung(ulong face, out uint rung);

    [LibraryImport(Library, EntryPoint = "zxv_coin_split")]
    internal static partial int CoinSplit(ulong amount, uint* rungs, nuint cap, out nuint count);

    [LibraryImport(Library, EntryPoint = "zxv_token_face")]
    internal static partial int TokenFace(uint tier, out ulong face, out int isSuper);

    [LibraryImport(Library, EntryPoint = "zxv_token_tier")]
    internal static partial int TokenTier(ulong face, out uint tier);

    [LibraryImport(Library, EntryPoint = "zxv_token_value")]
    internal static partial int TokenValue(ulong* counts, out ulong value, out ulong pieces);

    [LibraryImport(Library, EntryPoint = "zxv_token_canonical")]
    internal static partial int TokenCanonical(ulong value, uint octane, ulong* counts);

    [LibraryImport(Library, EntryPoint = "zxv_token_is_canonical")]
    internal static partial int TokenIsCanonical(ulong* counts, uint octane, out int canonical);

    [LibraryImport(Library, EntryPoint = "zxv_token_compress")]
    internal static partial int TokenCompress(ulong* counts, uint octane);

    [LibraryImport(Library, EntryPoint = "zxv_token_fuse")]
    internal static partial int TokenFuse(ulong* counts, uint octane, uint tier);

    [LibraryImport(Library, EntryPoint = "zxv_token_split")]
    internal static partial int TokenSplit(ulong* counts, uint tier);

    [LibraryImport(Library, EntryPoint = "zxv_token_code_bits")]
    internal static partial int TokenCodeBits(ulong value, out uint bits);

    [LibraryImport(Library, EntryPoint = "zxv_token_code_put")]
    internal static partial int TokenCodePut(byte* buf, nuint bufLen, uint* bit, ulong value);

    [LibraryImport(Library, EntryPoint = "zxv_token_code_get")]
    internal static partial int TokenCodeGet(byte* buf, nuint bufLen, uint* bit, out ulong value);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_create")]
    internal static partial int TokenWalletCreate(ZxvCapabilityHandle cap, uint modelId, uint octane, out ZxvTokenWalletHandle wallet);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_destroy")]
    internal static partial void TokenWalletDestroy(IntPtr wallet);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_mint")]
    internal static partial int TokenWalletMint(ZxvCapabilityHandle cap, ZxvTokenWalletHandle wallet, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_spend")]
    internal static partial int TokenWalletSpend(ZxvCapabilityHandle cap, ZxvTokenWalletHandle wallet, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_transfer")]
    internal static partial int TokenWalletTransfer(ZxvCapabilityHandle cap, ZxvTokenWalletHandle from, ZxvTokenWalletHandle to, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_set_octane")]
    internal static partial int TokenWalletSetOctane(ZxvCapabilityHandle cap, ZxvTokenWalletHandle wallet, uint octane);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_info")]
    internal static partial int TokenWalletInfo(ZxvTokenWalletHandle wallet, out uint modelId, out uint octane, out ulong value, out ulong pieces);

    [LibraryImport(Library, EntryPoint = "zxv_token_wallet_counts")]
    internal static partial int TokenWalletCounts(ZxvTokenWalletHandle wallet, ulong* counts);

    // ===== fee =====
    [LibraryImport(Library, EntryPoint = "zxv_fee_assure")]
    internal static partial int FeeAssure(ulong gross, out ulong fee);

    [LibraryImport(Library, EntryPoint = "zxv_fee_assure_carry")]
    internal static partial int FeeAssureCarry(ulong remIn, ulong gross, out ulong fee, out ulong rem);

    [LibraryImport(Library, EntryPoint = "zxv_fee_split")]
    internal static partial int FeeSplit(ulong fee, ulong* parts);

    [LibraryImport(Library, EntryPoint = "zxv_fee_policy_default")]
    internal static partial int FeePolicyDefault(uint* pct, out int allowBelow, ulong* rateNum, ulong* rateDen, out ulong multNum, out ulong multDen, out ulong commonsNum, out ulong commonsDen);

    // ===== vhub =====
    [LibraryImport(Library, EntryPoint = "zxv_vhub_create")]
    internal static partial int VhubCreate(ZxvCapabilityHandle cap, byte* seed, out ZxvVHubHandle hub);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_destroy")]
    internal static partial void VhubDestroy(IntPtr hub);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_add_ccy")]
    internal static partial int VhubAddCcy(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte slot, byte* code, byte minorUnits, byte* issuerPk, ulong poolFunding, ulong vbillBacking);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_ccy_info")]
    internal static partial int VhubCcyInfo(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte slot, out uint issuerAccount, out uint poolAccount, out uint vfloatAccount, out ulong pegNum, out ulong pegDen, out ulong pegEpoch);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_peg_msg")]
    internal static partial int VhubPegMsg(byte slot, ulong num, ulong den, ulong epoch, byte* msg);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_set_peg")]
    internal static partial int VhubSetPeg(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte slot, ulong num, ulong den, ulong epoch, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_open_account")]
    internal static partial int VhubOpenAccount(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte slot, uint owner, out uint account);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_issue")]
    internal static partial int VhubIssue(ZxvCapabilityHandle cap, ZxvVHubHandle hub, uint account, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_balance")]
    internal static partial int VhubBalance(ZxvCapabilityHandle cap, ZxvVHubHandle hub, uint account, out ulong debit, out ulong credit, out long equity);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_add_limit")]
    internal static partial int VhubAddLimit(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte slot, ulong limit);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_next_epoch")]
    internal static partial int VhubNextEpoch(ZxvCapabilityHandle cap, ZxvVHubHandle hub, out ulong epoch);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_quote")]
    internal static partial int VhubQuote(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte fromSlot, byte toSlot, ulong want, out ulong amount, out ulong vbills, out ulong received);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_swap")]
    internal static partial int VhubSwap(ZxvCapabilityHandle cap, ZxvVHubHandle hub, byte fromSlot, byte toSlot, uint sender, uint receiver, ulong amount, ulong tick, out ulong vbills, out ulong received);

    [LibraryImport(Library, EntryPoint = "zxv_vhub_check")]
    internal static partial int VhubCheck(ZxvCapabilityHandle cap, ZxvVHubHandle hub, out ulong postings);

    // ===== freight =====
    [LibraryImport(Library, EntryPoint = "zxv_freight_header_init")]
    internal static partial int FreightHeaderInit(ulong freightId, byte k, byte flags, ulong expandLimit, byte* payload, nuint len, byte* header);

    [LibraryImport(Library, EntryPoint = "zxv_freight_header_serialize")]
    internal static partial int FreightHeaderSerialize(byte k, byte flags, ushort payloadLen, ushort seq, ushort seqCount, ulong freightId, ulong expandLimit, byte* payloadSha256, byte* header);

    [LibraryImport(Library, EntryPoint = "zxv_freight_header_parse")]
    internal static partial int FreightHeaderParse(byte* header, out byte version, out byte k, out byte flags, out ushort payloadLen, out ushort seq, out ushort seqCount, out ulong freightId, out ulong expandLimit, byte* payloadSha256);

    [LibraryImport(Library, EntryPoint = "zxv_freight_header_check")]
    internal static partial int FreightHeaderCheck(byte* header);

    [LibraryImport(Library, EntryPoint = "zxv_freight_encode")]
    internal static partial int FreightEncode(byte* header, byte* payload, nuint len, byte* packets);

    [LibraryImport(Library, EntryPoint = "zxv_freight_decode")]
    internal static partial int FreightDecode(byte* header, byte* packets, nuint packetsLen, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_set_count")]
    internal static partial int FreightSetCount(ulong len, byte k, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_freight_split")]
    internal static partial int FreightSplit(ulong freightId, byte k, byte flags, ulong expandLimit, byte* data, nuint len, uint seq, byte* header, byte* packets);

    [LibraryImport(Library, EntryPoint = "zxv_freight_join_create")]
    internal static partial int FreightJoinCreate(ulong capacity, out ZxvFreightJoinHandle join);

    [LibraryImport(Library, EntryPoint = "zxv_freight_join_destroy")]
    internal static partial void FreightJoinDestroy(IntPtr join);

    [LibraryImport(Library, EntryPoint = "zxv_freight_join_add")]
    internal static partial int FreightJoinAdd(ZxvFreightJoinHandle join, byte* header, byte* packets, nuint packetsLen);

    [LibraryImport(Library, EntryPoint = "zxv_freight_join_status")]
    internal static partial int FreightJoinStatus(ZxvFreightJoinHandle join, out uint received, out uint count, out int complete, out ulong total);

    [LibraryImport(Library, EntryPoint = "zxv_freight_join_result")]
    internal static partial int FreightJoinResult(ZxvFreightJoinHandle join, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_peek")]
    internal static partial int FreightOpsPeek(byte* ops, nuint opsLen, out ulong total, out int hasSha);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_expand")]
    internal static partial int FreightOpsExpand(
        byte* ops,
        nuint opsLen,
        ulong limit,
        delegate* unmanaged[Cdecl]<void*, byte*, nuint, byte*, ulong, int> resolve,
        void* user,
        byte* buf,
        nuint cap,
        out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_gen_run")]
    internal static partial int FreightGenRun(byte gen, byte* parameters, nuint paramsLen, byte* output, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_encode")]
    internal static partial int FreightOpsEncode(byte* input, nuint n, int withSha, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_bound")]
    internal static partial int FreightOpsBound(ulong n, int withSha, out ulong bound);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_writer_create")]
    internal static partial int FreightOpsWriterCreate(ulong declaredTotal, byte* sha256, ulong maxBytes, out ZxvFreightOpsWriterHandle writer);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_writer_destroy")]
    internal static partial void FreightOpsWriterDestroy(IntPtr writer);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_put_literal")]
    internal static partial int FreightOpsPutLiteral(ZxvFreightOpsWriterHandle writer, byte* bytes, nuint n);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_put_copy")]
    internal static partial int FreightOpsPutCopy(ZxvFreightOpsWriterHandle writer, ulong distance, ulong len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_put_cidref")]
    internal static partial int FreightOpsPutCidref(ZxvFreightOpsWriterHandle writer, byte* cid, nuint cidLen, ulong len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_put_seed")]
    internal static partial int FreightOpsPutSeed(ZxvFreightOpsWriterHandle writer, byte gen, byte* parameters, nuint paramsLen, ulong len);

    [LibraryImport(Library, EntryPoint = "zxv_freight_ops_writer_finish")]
    internal static partial int FreightOpsWriterFinish(ZxvFreightOpsWriterHandle writer, byte* buf, nuint cap, out nuint len);

    // ===== web4 =====
    [LibraryImport(Library, EntryPoint = "zxv_w4_agent_create")]
    internal static partial int W4AgentCreate(ZxvCapabilityHandle cap, byte* seed, byte* name, IntPtr node, ulong nowMs, out ZxvW4AgentHandle agent);

    [LibraryImport(Library, EntryPoint = "zxv_w4_agent_destroy")]
    internal static partial void W4AgentDestroy(IntPtr agent);

    [LibraryImport(Library, EntryPoint = "zxv_w4_agent_info")]
    internal static partial int W4AgentInfo(ZxvW4AgentHandle agent, byte* agentId, byte* peerId, byte* publicKey);

    [LibraryImport(Library, EntryPoint = "zxv_w4_agent_card")]
    internal static partial int W4AgentCard(ZxvW4AgentHandle agent, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_w4_card_verify")]
    internal static partial int W4CardVerify(byte* card, nuint len, byte* agentId, byte* peerId, byte* name, out ulong boundMs);

    [LibraryImport(Library, EntryPoint = "zxv_w4_dir_create")]
    internal static partial int W4DirCreate(uint capacity, out ZxvW4DirHandle dir);

    [LibraryImport(Library, EntryPoint = "zxv_w4_dir_destroy")]
    internal static partial void W4DirDestroy(IntPtr dir);

    [LibraryImport(Library, EntryPoint = "zxv_w4_dir_add")]
    internal static partial int W4DirAdd(ZxvW4DirHandle dir, byte* card, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_w4_dir_find")]
    internal static partial int W4DirFind(ZxvW4DirHandle dir, byte* agentId, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_w4_dir_count")]
    internal static partial int W4DirCount(ZxvW4DirHandle dir, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_create")]
    internal static partial int W4ManifestCreate(uint version, ulong issuedMs, ulong expiresMs, out ZxvW4ManifestHandle manifest);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_destroy")]
    internal static partial void W4ManifestDestroy(IntPtr manifest);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_add_tool")]
    internal static partial int W4ManifestAddTool(ZxvW4ManifestHandle manifest, byte* name, ulong priceVfv, uint ratePerMin, uint burst, uint flags, uint maxInput);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_sign")]
    internal static partial int W4ManifestSign(ZxvCapabilityHandle cap, ZxvW4AgentHandle agent, ZxvW4ManifestHandle manifest);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_verify")]
    internal static partial int W4ManifestVerify(ZxvW4ManifestHandle manifest, byte* publicKey, ulong nowMs);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_encode")]
    internal static partial int W4ManifestEncode(ZxvW4ManifestHandle manifest, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_decode")]
    internal static partial int W4ManifestDecode(byte* bytes, nuint len, out ZxvW4ManifestHandle manifest);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_to_json")]
    internal static partial int W4ManifestToJson(ZxvW4ManifestHandle manifest, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_info")]
    internal static partial int W4ManifestInfo(ZxvW4ManifestHandle manifest, byte* agentId, byte* peerId, out uint version, out ulong issuedMs, out ulong expiresMs, out uint toolCount, out int signed);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_tool")]
    internal static partial int W4ManifestTool(ZxvW4ManifestHandle manifest, uint index, byte* name, out ulong priceVfv, out uint ratePerMin, out uint burst, out uint flags, out uint maxInput);

    [LibraryImport(Library, EntryPoint = "zxv_w4_manifest_find")]
    internal static partial int W4ManifestFind(ZxvW4ManifestHandle manifest, byte* name, out uint index);

    [LibraryImport(Library, EntryPoint = "zxv_w4_action_digest")]
    internal static partial int W4ActionDigest(byte* tool, byte* from, byte* to, ulong reqId, byte* payload, nuint len, byte* digest);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_issue")]
    internal static partial int W4ConsentIssue(ZxvCapabilityHandle cap, ZxvW4AgentHandle human, byte* agentId, byte* action, uint scope, ulong maxVfv, ulong issuedMs, ulong expiresMs, byte* nonce, byte* token);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_info")]
    internal static partial int W4ConsentInfo(byte* token, byte* humanId, byte* agentId, byte* action, out uint scope, out ulong maxVfv, out ulong issuedMs, out ulong expiresMs, byte* nonce);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_log_create")]
    internal static partial int W4ConsentLogCreate(uint capacity, out ZxvW4ConsentLogHandle log);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_log_destroy")]
    internal static partial void W4ConsentLogDestroy(IntPtr log);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_check")]
    internal static partial int W4ConsentCheck(ZxvW4ConsentLogHandle log, byte* token, byte* humanPublicKey, byte* agentId, byte* action, uint need, ulong amount, ulong nowMs);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_revoke")]
    internal static partial int W4ConsentRevoke(ZxvW4ConsentLogHandle log, byte* token, ulong nowMs);

    [LibraryImport(Library, EntryPoint = "zxv_w4_consent_log_prune")]
    internal static partial int W4ConsentLogPrune(ZxvW4ConsentLogHandle log, ulong nowMs, out uint remaining);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_seal")]
    internal static partial int W4EnvSeal(ZxvCapabilityHandle cap, ZxvW4AgentHandle agent, int kind, byte* to, ulong reqId, uint chunk, byte* tool, uint flags, byte* payload, nuint len, byte* consentToken, ulong nowMs, byte* buf, nuint bufCap, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_reply")]
    internal static partial int W4EnvReply(ZxvCapabilityHandle cap, ZxvW4AgentHandle agent, byte* request, nuint requestLen, int kind, uint chunk, uint flags, byte* payload, nuint len, ulong nowMs, byte* buf, nuint bufCap, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_decode")]
    internal static partial int W4EnvDecode(byte* env, nuint len, out int kind, out uint flags, byte* from, byte* to, out ulong seq, out ulong tsMs, out ulong reqId, out uint chunk, byte* tool, out uint payloadLen);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_payload")]
    internal static partial int W4EnvPayload(byte* env, nuint len, byte* buf, nuint cap, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_consent")]
    internal static partial int W4EnvConsent(byte* env, nuint len, byte* token);

    [LibraryImport(Library, EntryPoint = "zxv_w4_env_verify")]
    internal static partial int W4EnvVerify(byte* env, nuint len, byte* senderPublicKey);

    // ===== paradox =====
    [LibraryImport(Library, EntryPoint = "zxv_paradox_signature")]
    internal static partial int ParadoxSignature(byte* data, nuint len, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_kind_of_tag")]
    internal static partial int ParadoxKindOfTag(byte tag, out int kind);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_action")]
    internal static partial int ParadoxAction(int kind, byte tag, out int action);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_outcome_action")]
    internal static partial int ParadoxOutcomeAction(int outcome, out int action);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_repair_verify")]
    internal static partial int ParadoxRepairVerify(byte* proof);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_kind_name")]
    internal static partial IntPtr ParadoxKindName(int kind);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_outcome_name")]
    internal static partial IntPtr ParadoxOutcomeName(int outcome);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_node_state_name")]
    internal static partial IntPtr ParadoxNodeStateName(int state);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_create")]
    internal static partial int ParadoxCreate(ZxvCapabilityHandle cap, uint logCapacity, uint registryCapacity, out ZxvParadoxHandle book);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_destroy")]
    internal static partial void ParadoxDestroy(IntPtr book);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_classify")]
    internal static partial int ParadoxClassify(IntPtr book, byte tag, uint* evidence, int stale, byte* sig, out int kind, out byte outTag, out int regIndex);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_registry_add")]
    internal static partial int ParadoxRegistryAdd(ZxvCapabilityHandle cap, ZxvParadoxHandle book, byte* sig, int kind, byte reason, byte flags, int ownerOk, out uint index);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_registry_find")]
    internal static partial int ParadoxRegistryFind(ZxvParadoxHandle book, byte* sig, out uint index);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_registry_count")]
    internal static partial int ParadoxRegistryCount(ZxvParadoxHandle book, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_registry_at")]
    internal static partial int ParadoxRegistryAt(ZxvParadoxHandle book, uint index, byte* sig, out int kind, out byte reason, out byte flags, out ulong src);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_log_count")]
    internal static partial int ParadoxLogCount(ZxvParadoxHandle book, int which, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_log_head")]
    internal static partial int ParadoxLogHead(ZxvParadoxHandle book, int which, byte* head);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_log_record")]
    internal static partial int ParadoxLogRecord(ZxvParadoxHandle book, int which, uint index, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_log_verify")]
    internal static partial int ParadoxLogVerify(ZxvParadoxHandle book, int which);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_policy_create")]
    internal static partial int ParadoxPolicyCreate(ZxvCapabilityHandle cap, uint n, long* lo, long* hi, long* initial, uint flags, out ZxvParadoxPolicyHandle policy);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_policy_destroy")]
    internal static partial void ParadoxPolicyDestroy(IntPtr policy);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_constructive")]
    internal static partial int ParadoxConstructive(ZxvCapabilityHandle cap, ZxvParadoxHandle book, ZxvParadoxPolicyHandle policy, int kind, byte tag, long* synth, uint n, uint tension, out int outcome);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_policy_values")]
    internal static partial int ParadoxPolicyValues(ZxvParadoxPolicyHandle policy, long* values, long* prev, uint n, out int armed, out uint tensionAt);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_policy_bounds")]
    internal static partial int ParadoxPolicyBounds(ZxvParadoxPolicyHandle policy, long* lo, long* hi, uint n, out uint flags);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_policy_proposal")]
    internal static partial int ParadoxPolicyProposal(ZxvParadoxPolicyHandle policy, long* synth, uint n, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_destructive")]
    internal static partial int ParadoxDestructive(
        ZxvCapabilityHandle cap,
        ZxvParadoxHandle book,
        int kind,
        byte tag,
        uint steps,
        uint stepLimit,
        uint spent,
        uint budget,
        ulong* trace,
        uint traceN,
        int invariantOk,
        byte* state,
        byte* checkpoint,
        uint len,
        byte* pattern,
        uint patternLen,
        uint submitter,
        out int outcome,
        byte* proof);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_create")]
    internal static partial int ParadoxHoldCreate(ZxvCapabilityHandle cap, uint capacity, uint decisive, out ZxvParadoxHoldHandle hold);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_destroy")]
    internal static partial void ParadoxHoldDestroy(IntPtr hold);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_add")]
    internal static partial int ParadoxHoldAdd(ZxvParadoxHoldHandle hold, byte* id);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_evidence")]
    internal static partial int ParadoxHoldEvidence(ZxvParadoxHoldHandle hold, byte* id, int positive);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_info")]
    internal static partial int ParadoxHoldInfo(ZxvParadoxHoldHandle hold, out uint count, out int settled, out int decisive);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_hold_branch")]
    internal static partial int ParadoxHoldBranch(ZxvParadoxHoldHandle hold, uint index, byte* id, out uint pos, out uint neg);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_neutral")]
    internal static partial int ParadoxNeutral(ZxvCapabilityHandle cap, ZxvParadoxHandle book, ZxvParadoxHoldHandle hold, int kind, byte tag, int ownerPick, out int outcome, out int settled);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_special")]
    internal static partial int ParadoxSpecial(ZxvCapabilityHandle cap, ZxvParadoxHandle book, int kind, byte tag, byte* sig, byte* subject, out int outcome, out uint rec);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_draft")]
    internal static partial int ParadoxDraft(ZxvCapabilityHandle cap, ZxvParadoxHandle book, uint rec, int kind, byte reason, byte flags, byte* draft);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_review")]
    internal static partial int ParadoxReview(ZxvCapabilityHandle cap, ZxvParadoxHandle book, byte* draft, int ownerOk, int consensusOk, out int outcome);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_create")]
    internal static partial int ParadoxGraphCreate(ZxvCapabilityHandle cap, uint stepLimit, out ZxvParadoxGraphHandle graph);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_destroy")]
    internal static partial void ParadoxGraphDestroy(IntPtr graph);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_add_node")]
    internal static partial int ParadoxGraphAddNode(ZxvParadoxGraphHandle graph, byte* id, int kind, byte tag, uint mag, int dir);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_add_dep")]
    internal static partial int ParadoxGraphAddDep(ZxvParadoxGraphHandle graph, byte* id, byte* dep);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_update")]
    internal static partial int ParadoxGraphUpdate(ZxvParadoxGraphHandle graph, byte* id, int kind, byte tag, uint mag, int dir);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_pass")]
    internal static partial int ParadoxGraphPass(ZxvCapabilityHandle cap, ZxvParadoxHandle book, ZxvParadoxGraphHandle graph, out int outcome);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_state")]
    internal static partial int ParadoxGraphState(ZxvParadoxGraphHandle graph, byte* id, out int state);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_node_at")]
    internal static partial int ParadoxGraphNodeAt(ZxvParadoxGraphHandle graph, uint index, byte* id, out int kind, out int state);

    [LibraryImport(Library, EntryPoint = "zxv_paradox_graph_info")]
    internal static partial int ParadoxGraphInfo(ZxvParadoxGraphHandle graph, out uint nodes, out uint pass, out uint totalMag, out int totalDir, out int escalated, byte* chain);
}
