// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile.internal

/**
 * The native methods of the ABI 1.5 groups (`jni/zxv_jni15.c`): treaty,
 * CID/AIPI, coin, fee, V-Bill hub, freight, Web 4 and paradox, plus
 * `zxv_features2` and the ML-DSA-87 calls the hub's pegs need. Internal.
 *
 * The conventions of [Jni] hold, and for 1.5:
 *  - uint64 values cross as `Long` bits; several scalar outs come back packed
 *    in one `LongArray` in the order the C function lists them;
 *  - a CID out is a `ByteArray(ZXV_CID_MAX)` plus its length in a `LongArray(1)`;
 *  - byte results use the R4 two-call shape (`buf == null` asks for the size);
 *  - secret inputs are [org.zedec.zxv.mobile.SecretKey] addresses, never arrays;
 *  - the freight CIDREF resolver never runs inside a native call: Kotlin
 *    passes a table of resolved CIDs and the shim reports the first miss.
 *
 * Generated from the C file's signatures; `ShimDriftTest` keeps the two in step.
 */
@Suppress("TooManyFunctions", "LongParameterList")
internal object Jni15 {
    init {
        NativeLoader.ensureLoaded()
    }

    @JvmStatic external fun features2(out_: LongArray?): Int
    @JvmStatic external fun mldsa87Keygen(cap: Long, pk: ByteArray?, sk: Long): Int
    @JvmStatic external fun mldsa87KeygenSeed(cap: Long, seed: Long, pk: ByteArray?, sk: Long): Int
    @JvmStatic external fun mldsa87Sign(cap: Long, sk: Long, msg: ByteArray?, ctx: ByteArray?, sig: ByteArray?): Int
    @JvmStatic external fun mldsa87SignDeterministic(cap: Long, sk: Long, msg: ByteArray?, ctx: ByteArray?, sig: ByteArray?): Int
    @JvmStatic external fun mldsa87Verify(cap: Long, pk: ByteArray?, msg: ByteArray?, ctx: ByteArray?, sig: ByteArray?): Int
    @JvmStatic external fun treatyCreate(out_: LongArray?): Int
    @JvmStatic external fun treatyDestroy(t: Long)
    @JvmStatic external fun treatyClear(t: Long): Int
    @JvmStatic external fun treatyParseSutra(t: Long, text: ByteArray?, fault: LongArray?): Int
    @JvmStatic external fun treatyParseRecord(t: Long, rec: ByteArray?, fault: LongArray?): Int
    @JvmStatic external fun treatyEmitSutra(t: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun treatyEmitRecord(t: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun treatyValidate(t: Long, fault: IntArray?): Int
    @JvmStatic external fun treatyEqual(a: Long, b: Long, out_: IntArray?): Int
    @JvmStatic external fun treatyName(t: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun treatyAsset(t: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun treatyHeader(t: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyPartyId(t: Long, index: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun treatyParty(t: Long, index: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyOwe(t: Long, index: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyPicAdd(a: Long, b: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyPicSub(a: Long, b: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyPicMul(a: Long, b: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyPicDiv(a: Long, b: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyRun(t: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyResultDestroy(r: Long)
    @JvmStatic external fun treatyResultSummary(r: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyResultParty(r: Long, index: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyResultLeg(r: Long, index: Long, out_: LongArray?): Int
    @JvmStatic external fun treatyResultRecord(r: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun cidCompute(codec: Long, data: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun cidUnixfs(data: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun cidVerify(cid: ByteArray?, data: ByteArray?): Int
    @JvmStatic external fun cidToText(cid: ByteArray?, base: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun cidFromText(text: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun cidInfo(cid: ByteArray?, out_: LongArray?, digest: ByteArray?, digestLen: LongArray?): Int
    @JvmStatic external fun cidToV1(cid: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiProviderKey(cid: ByteArray?, out_: ByteArray?): Int
    @JvmStatic external fun aipiFnEncode(u: LongArray?, fuelMax: Long, module: ByteArray?, contract: ByteArray?, entry: ByteArray?, checker: ByteArray?, certifier: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiFnDecode(rec: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun aipiFnGetCid(rec: ByteArray?, field: Long, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiFnGetName(rec: ByteArray?, field: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiFnCid(rec: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiCallEncode(fnCid: ByteArray?, input: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiCallDecode(rec: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun aipiCallCid(fnCid: ByteArray?, input: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiClaimEncode(callCid: ByteArray?, executor: ByteArray?, output: ByteArray?, cert: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiClaimDecode(rec: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun aipiCreate(cap: Long, storeBytes: Long, maxBlocks: Long, nodeKey: Long, out_: LongArray?): Int
    @JvmStatic external fun aipiDestroy(a: Long)
    @JvmStatic external fun aipiPut(cap: Long, a: Long, data: ByteArray?, isPrivate: Int, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiGet(a: Long, cid: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiPublishFn(cap: Long, a: Long, rec: ByteArray?, cidOut: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiLoadFn(a: Long, fnCid: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun aipiPlan(a: Long, fnCid: ByteArray?, input: ByteArray?, inputPrivate: Int, out_: IntArray?): Int
    @JvmStatic external fun aipiInfo(a: Long, out_: LongArray?): Int
    @JvmStatic external fun coinFace(rung: Long, out_: LongArray?): Int
    @JvmStatic external fun coinRung(face: Long, out_: LongArray?): Int
    @JvmStatic external fun coinSplit(amount: Long, rungs: IntArray?, outCount: LongArray?): Int
    @JvmStatic external fun tokenFace(tier: Long, out_: LongArray?): Int
    @JvmStatic external fun tokenTier(face: Long, out_: LongArray?): Int
    @JvmStatic external fun tokenValue(counts: LongArray?, out_: LongArray?): Int
    @JvmStatic external fun tokenCanonical(value: Long, octane: Long, outCounts: LongArray?): Int
    @JvmStatic external fun tokenIsCanonical(counts: LongArray?, octane: Long, out_: IntArray?): Int
    @JvmStatic external fun tokenCompress(counts: LongArray?, octane: Long): Int
    @JvmStatic external fun tokenFuse(counts: LongArray?, octane: Long, tier: Long): Int
    @JvmStatic external fun tokenSplit(counts: LongArray?, tier: Long): Int
    @JvmStatic external fun tokenCodeBits(value: Long, out_: LongArray?): Int
    @JvmStatic external fun tokenCodePut(buf: ByteArray?, bit: LongArray?, value: Long): Int
    @JvmStatic external fun tokenCodeGet(buf: ByteArray?, bit: LongArray?, out_: LongArray?): Int
    @JvmStatic external fun tokenWalletCreate(cap: Long, modelId: Long, octane: Long, out_: LongArray?): Int
    @JvmStatic external fun tokenWalletDestroy(w: Long)
    @JvmStatic external fun tokenWalletMint(cap: Long, w: Long, amount: Long): Int
    @JvmStatic external fun tokenWalletSpend(cap: Long, w: Long, amount: Long): Int
    @JvmStatic external fun tokenWalletTransfer(cap: Long, from: Long, to: Long, amount: Long): Int
    @JvmStatic external fun tokenWalletSetOctane(cap: Long, w: Long, octane: Long): Int
    @JvmStatic external fun tokenWalletInfo(w: Long, out_: LongArray?): Int
    @JvmStatic external fun tokenWalletCounts(w: Long, outCounts: LongArray?): Int
    @JvmStatic external fun feeAssure(gross: Long, out_: LongArray?): Int
    @JvmStatic external fun feeAssureCarry(remIn: Long, gross: Long, out_: LongArray?): Int
    @JvmStatic external fun feeSplit(fee: Long, out_: LongArray?): Int
    @JvmStatic external fun feePolicyDefault(out_: LongArray?): Int
    @JvmStatic external fun vhubCreate(cap: Long, seed: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun vhubDestroy(hub: Long)
    @JvmStatic external fun vhubAddCcy(cap: Long, hub: Long, slot: Int, code: ByteArray?, minorUnits: Int, issuerPk: ByteArray?, poolFunding: Long, vbillBacking: Long): Int
    @JvmStatic external fun vhubCcyInfo(cap: Long, hub: Long, slot: Int, out_: LongArray?): Int
    @JvmStatic external fun vhubPegMsg(slot: Int, num: Long, den: Long, epoch: Long, out_: ByteArray?): Int
    @JvmStatic external fun vhubSetPeg(cap: Long, hub: Long, slot: Int, num: Long, den: Long, epoch: Long, sig: ByteArray?): Int
    @JvmStatic external fun vhubOpenAccount(cap: Long, hub: Long, slot: Int, owner: Long, out_: LongArray?): Int
    @JvmStatic external fun vhubIssue(cap: Long, hub: Long, account: Long, amount: Long): Int
    @JvmStatic external fun vhubBalance(cap: Long, hub: Long, account: Long, out_: LongArray?): Int
    @JvmStatic external fun vhubAddLimit(cap: Long, hub: Long, slot: Int, limit: Long): Int
    @JvmStatic external fun vhubNextEpoch(cap: Long, hub: Long, out_: LongArray?): Int
    @JvmStatic external fun vhubQuote(cap: Long, hub: Long, from: Int, to: Int, want: Long, out_: LongArray?): Int
    @JvmStatic external fun vhubSwap(cap: Long, hub: Long, from: Int, to: Int, sender: Long, receiver: Long, amount: Long, tick: Long, out_: LongArray?): Int
    @JvmStatic external fun vhubCheck(cap: Long, hub: Long, out_: LongArray?): Int
    @JvmStatic external fun freightHeaderInit(freightId: Long, k: Int, flags: Int, expandLimit: Long, payload: ByteArray?, outHeader: ByteArray?): Int
    @JvmStatic external fun freightHeaderSerialize(f: IntArray?, freightId: Long, expandLimit: Long, sha: ByteArray?, outHeader: ByteArray?): Int
    @JvmStatic external fun freightHeaderParse(header: ByteArray?, out_: LongArray?, sha: ByteArray?): Int
    @JvmStatic external fun freightHeaderCheck(header: ByteArray?): Int
    @JvmStatic external fun freightEncode(header: ByteArray?, payload: ByteArray?, outPackets: ByteArray?): Int
    @JvmStatic external fun freightDecode(header: ByteArray?, packets: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun freightSetCount(len: Long, k: Int, out_: LongArray?): Int
    @JvmStatic external fun freightSplit(freightId: Long, k: Int, flags: Int, expandLimit: Long, data: ByteArray?, seq: Long, outHeader: ByteArray?, outPackets: ByteArray?): Int
    @JvmStatic external fun freightJoinCreate(capacity: Long, out_: LongArray?): Int
    @JvmStatic external fun freightJoinDestroy(j: Long)
    @JvmStatic external fun freightJoinAdd(j: Long, header: ByteArray?, packets: ByteArray?): Int
    @JvmStatic external fun freightJoinStatus(j: Long, out_: LongArray?): Int
    @JvmStatic external fun freightJoinResult(j: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun freightOpsPeek(ops: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun freightOpsExpand(ops: ByteArray?, limit: Long, tblCids: ByteArray?, tblCidLens: IntArray?, tblData: ByteArray?, tblDataLens: LongArray?, buf: ByteArray?, outLen: LongArray?, missCid: ByteArray?, missInfo: LongArray?): Int
    @JvmStatic external fun freightGenRun(gen: Int, params: ByteArray?, out_: ByteArray?): Int
    @JvmStatic external fun freightOpsEncode(in_: ByteArray?, withSha: Int, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun freightOpsBound(n: Long, withSha: Int, out_: LongArray?): Int
    @JvmStatic external fun freightOpsWriterCreate(declaredTotal: Long, sha: ByteArray?, maxBytes: Long, out_: LongArray?): Int
    @JvmStatic external fun freightOpsWriterDestroy(w: Long)
    @JvmStatic external fun freightOpsPutLiteral(w: Long, bytes: ByteArray?): Int
    @JvmStatic external fun freightOpsPutCopy(w: Long, distance: Long, len: Long): Int
    @JvmStatic external fun freightOpsPutCidref(w: Long, cid: ByteArray?, len: Long): Int
    @JvmStatic external fun freightOpsPutSeed(w: Long, gen: Int, params: ByteArray?, len: Long): Int
    @JvmStatic external fun freightOpsWriterFinish(w: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4AgentCreate(cap: Long, seed: Long, name: ByteArray?, node: Long, nowMs: Long, out_: LongArray?): Int
    @JvmStatic external fun w4AgentDestroy(a: Long)
    @JvmStatic external fun w4AgentInfo(a: Long, agentId: ByteArray?, peerId: ByteArray?, pk: ByteArray?): Int
    @JvmStatic external fun w4AgentCard(a: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4CardVerify(card: ByteArray?, agentId: ByteArray?, peerId: ByteArray?, name: ByteArray?, boundMs: LongArray?): Int
    @JvmStatic external fun w4DirCreate(capacity: Long, out_: LongArray?): Int
    @JvmStatic external fun w4DirDestroy(d: Long)
    @JvmStatic external fun w4DirAdd(d: Long, card: ByteArray?): Int
    @JvmStatic external fun w4DirFind(d: Long, agentId: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4DirCount(d: Long, out_: LongArray?): Int
    @JvmStatic external fun w4ManifestCreate(version: Long, issuedMs: Long, expiresMs: Long, out_: LongArray?): Int
    @JvmStatic external fun w4ManifestDestroy(m: Long)
    @JvmStatic external fun w4ManifestAddTool(m: Long, name: ByteArray?, priceVfv: Long, u: LongArray?): Int
    @JvmStatic external fun w4ManifestSign(cap: Long, agent: Long, m: Long): Int
    @JvmStatic external fun w4ManifestVerify(m: Long, pk: ByteArray?, nowMs: Long): Int
    @JvmStatic external fun w4ManifestEncode(m: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4ManifestDecode(bytes: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun w4ManifestToJson(m: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4ManifestInfo(m: Long, agentId: ByteArray?, peerId: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun w4ManifestTool(m: Long, index: Long, name: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun w4ManifestFind(m: Long, name: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun w4ActionDigest(tool: ByteArray?, from: ByteArray?, to: ByteArray?, reqId: Long, payload: ByteArray?, out_: ByteArray?): Int
    @JvmStatic external fun w4ConsentIssue(cap: Long, human: Long, agentId: ByteArray?, action: ByteArray?, scope: Long, t: LongArray?, nonce: ByteArray?, outToken: ByteArray?): Int
    @JvmStatic external fun w4ConsentInfo(token: ByteArray?, humanId: ByteArray?, agentId: ByteArray?, action: ByteArray?, out_: LongArray?, nonce: ByteArray?): Int
    @JvmStatic external fun w4ConsentLogCreate(capacity: Long, out_: LongArray?): Int
    @JvmStatic external fun w4ConsentLogDestroy(l: Long)
    @JvmStatic external fun w4ConsentCheck(l: Long, token: ByteArray?, humanPk: ByteArray?, agentId: ByteArray?, action: ByteArray?, t: LongArray?): Int
    @JvmStatic external fun w4ConsentRevoke(l: Long, token: ByteArray?, nowMs: Long): Int
    @JvmStatic external fun w4ConsentLogPrune(l: Long, nowMs: Long, out_: LongArray?): Int
    @JvmStatic external fun w4EnvSeal(cap: Long, agent: Long, kind: Int, to: ByteArray?, u: LongArray?, tool: ByteArray?, payload: ByteArray?, consentToken: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4EnvReply(cap: Long, agent: Long, request: ByteArray?, kind: Int, u: LongArray?, payload: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4EnvDecode(envl: ByteArray?, out_: LongArray?, from: ByteArray?, to: ByteArray?, tool: ByteArray?): Int
    @JvmStatic external fun w4EnvPayload(envl: ByteArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun w4EnvConsent(envl: ByteArray?, outToken: ByteArray?): Int
    @JvmStatic external fun w4EnvVerify(envl: ByteArray?, senderPk: ByteArray?): Int
    @JvmStatic external fun pdxSignature(data: ByteArray?, out_: ByteArray?): Int
    @JvmStatic external fun pdxKindOfTag(tag: Int, out_: IntArray?): Int
    @JvmStatic external fun pdxAction(kind: Int, tag: Int, out_: IntArray?): Int
    @JvmStatic external fun pdxOutcomeAction(outcome: Int, out_: IntArray?): Int
    @JvmStatic external fun pdxRepairVerify(proof: ByteArray?): Int
    @JvmStatic external fun pdxKindName(kind: Int): String
    @JvmStatic external fun pdxOutcomeName(outcome: Int): String
    @JvmStatic external fun pdxNodeStateName(state: Int): String
    @JvmStatic external fun pdxCreate(cap: Long, logCapacity: Long, registryCapacity: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxDestroy(b: Long)
    @JvmStatic external fun pdxClassify(book: Long, tag: Int, evidence: LongArray?, stale: Int, sig: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxRegistryAdd(cap: Long, book: Long, sig: ByteArray?, kind: Int, reason: Int, flags: Int, ownerOk: Int, out_: LongArray?): Int
    @JvmStatic external fun pdxRegistryFind(book: Long, sig: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxRegistryCount(book: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxRegistryAt(book: Long, index: Long, sig: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxLogCount(book: Long, which: Int, out_: LongArray?): Int
    @JvmStatic external fun pdxLogHead(book: Long, which: Int, out_: ByteArray?): Int
    @JvmStatic external fun pdxLogRecord(book: Long, which: Int, index: Long, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun pdxLogVerify(book: Long, which: Int): Int
    @JvmStatic external fun pdxPolicyCreate(cap: Long, lo: LongArray?, hi: LongArray?, initial: LongArray?, flags: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxPolicyDestroy(p: Long)
    @JvmStatic external fun pdxConstructive(cap: Long, book: Long, policy: Long, kind: Int, tag: Int, synth: LongArray?, n: Long, tension: Long, out_: IntArray?): Int
    @JvmStatic external fun pdxPolicyValues(policy: Long, outVal: LongArray?, outPrev: LongArray?, n: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxPolicyBounds(policy: Long, lo: LongArray?, hi: LongArray?, n: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxPolicyProposal(policy: Long, synth: LongArray?, buf: ByteArray?, outLen: LongArray?): Int
    @JvmStatic external fun pdxDestructive(cap: Long, book: Long, kind: Int, tag: Int, d: LongArray?, trace: LongArray?, invariantOk: Int, state: ByteArray?, checkpoint: ByteArray?, pattern: ByteArray?, out_: IntArray?, proof: ByteArray?): Int
    @JvmStatic external fun pdxHoldCreate(cap: Long, capacity: Long, decisive: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxHoldDestroy(h: Long)
    @JvmStatic external fun pdxHoldAdd(h: Long, id: ByteArray?): Int
    @JvmStatic external fun pdxHoldEvidence(h: Long, id: ByteArray?, positive: Int): Int
    @JvmStatic external fun pdxHoldInfo(h: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxHoldBranch(h: Long, index: Long, id: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxNeutral(cap: Long, book: Long, hold: Long, kind: Int, tag: Int, ownerPick: Int, out_: LongArray?): Int
    @JvmStatic external fun pdxSpecial(cap: Long, book: Long, kind: Int, tag: Int, sig: ByteArray?, subject: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxDraft(cap: Long, book: Long, rec: Long, kind: Int, reason: Int, flags: Int, outDraft: ByteArray?): Int
    @JvmStatic external fun pdxReview(cap: Long, book: Long, draft: ByteArray?, ownerOk: Int, consensusOk: Int, out_: IntArray?): Int
    @JvmStatic external fun pdxGraphCreate(cap: Long, stepLimit: Long, out_: LongArray?): Int
    @JvmStatic external fun pdxGraphDestroy(g: Long)
    @JvmStatic external fun pdxGraphAddNode(g: Long, id: ByteArray?, kind: Int, tag: Int, mag: Long, dir: Int): Int
    @JvmStatic external fun pdxGraphAddDep(g: Long, id: ByteArray?, dep: ByteArray?): Int
    @JvmStatic external fun pdxGraphUpdate(g: Long, id: ByteArray?, kind: Int, tag: Int, mag: Long, dir: Int): Int
    @JvmStatic external fun pdxGraphPass(cap: Long, book: Long, g: Long, out_: IntArray?): Int
    @JvmStatic external fun pdxGraphState(g: Long, id: ByteArray?, out_: IntArray?): Int
    @JvmStatic external fun pdxGraphNodeAt(g: Long, index: Long, id: ByteArray?, out_: LongArray?): Int
    @JvmStatic external fun pdxGraphInfo(g: Long, out_: LongArray?, chain: ByteArray?): Int
}
