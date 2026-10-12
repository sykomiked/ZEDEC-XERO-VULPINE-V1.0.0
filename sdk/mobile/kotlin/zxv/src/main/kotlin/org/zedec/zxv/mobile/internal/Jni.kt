// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile.internal

/**
 * The native methods of libzxvjni (`jni/zxv_jni.c`), one per libzxv
 * function. Internal: every public type wraps these with capability,
 * lifetime and error rules.
 *
 * Conventions (mirrored in the C file): handles are `Long`; uint8 arguments
 * are `Int` in 0..255; uint32 arguments are `Long` in 0..4294967295; strings
 * are NUL-terminated UTF-8 `ByteArray`s; out values come back through small
 * arrays written only on ZXV_OK. Touching this object loads the libraries
 * and checks the ABI version.
 */
@Suppress("TooManyFunctions")
internal object Jni {
    /** The shim ABI this Kotlin code was written for (ZXVJ_SHIM_ABI in zxvj_common.h). */
    const val SHIM_ABI: Int = 2

    init {
        NativeLoader.ensureLoaded()
    }

    @JvmStatic external fun shimAbi(): Int
    @JvmStatic external fun version(out: IntArray): Int
    @JvmStatic external fun features(out: LongArray): Int
    @JvmStatic external fun statusName(status: Int): String
    @JvmStatic external fun statusClass(status: Int): Int
    @JvmStatic external fun lastError(buf: ByteArray?, outLen: LongArray): Int

    @JvmStatic external fun ctxCreate(out: LongArray): Int
    @JvmStatic external fun ctxDestroy(ctx: Long)
    @JvmStatic external fun ctxIsPoisoned(ctx: Long, out: IntArray): Int
    @JvmStatic external fun ctxHalt(ctx: Long, reason: ByteArray?): Int

    @JvmStatic external fun capCreate(ctx: Long, pid: Long, identity: ByteArray?, caps: Long, out: LongArray): Int
    @JvmStatic external fun capAttenuate(cap: Long, keep: Long, out: LongArray): Int
    @JvmStatic external fun capRevoke(cap: Long): Int
    @JvmStatic external fun capSetAttestation(cap: Long, lp: Int): Int
    @JvmStatic external fun capInfo(cap: Long, out: LongArray): Int
    @JvmStatic external fun capCheck(cap: Long, needed: Long): Int
    @JvmStatic external fun capDestroy(cap: Long)

    @JvmStatic external fun ccyEnableCap(cap: Long, alpha: ByteArray): Int
    @JvmStatic external fun ccyEnableWithMinorCap(cap: Long, alpha: ByteArray, minor: Int): Int
    @JvmStatic external fun ccyRegisterPrivateCap(cap: Long, code: ByteArray, minor: Int): Int
    @JvmStatic external fun ledgerOpenAccountCap(cap: Long, name: ByteArray, ccy: ByteArray, kind: Int, out: LongArray): Int
    @JvmStatic external fun ledgerPostCap(
        cap: Long, from: Long, to: Long, amount: Long, ccy: ByteArray, ref: ByteArray, out: LongArray,
    ): Int
    @JvmStatic external fun ledgerBalanceCap(cap: Long, account: Long, out: LongArray): Int
    @JvmStatic external fun ledgerCheckCap(cap: Long): Int
    @JvmStatic external fun ledgerAccountCountCap(cap: Long, out: LongArray): Int
    @JvmStatic external fun ledgerAccountNameCap(cap: Long, account: Long, buf: ByteArray?, outLen: LongArray): Int
    @JvmStatic external fun ledgerAccountCcyCap(cap: Long, account: Long, buf: ByteArray?, outLen: LongArray): Int
    @JvmStatic external fun ledgerAccountKindCap(cap: Long, account: Long, out: IntArray): Int
    @JvmStatic external fun ledgerEntryCountCap(cap: Long, account: Long, out: LongArray): Int
    @JvmStatic external fun ledgerEntryAtCap(
        cap: Long, account: Long, index: Long, out: LongArray, refBuf: ByteArray?, outLen: LongArray,
    ): Int

    @JvmStatic external fun ratNormalize(n: Long, d: Long, out: LongArray): Int
    @JvmStatic external fun ratAdd(an: Long, ad: Long, bn: Long, bd: Long, out: LongArray): Int
    @JvmStatic external fun ratSub(an: Long, ad: Long, bn: Long, bd: Long, out: LongArray): Int
    @JvmStatic external fun ratMul(an: Long, ad: Long, bn: Long, bd: Long, out: LongArray): Int
    @JvmStatic external fun ratDiv(an: Long, ad: Long, bn: Long, bd: Long, out: LongArray): Int
    @JvmStatic external fun ratCmp(an: Long, ad: Long, bn: Long, bd: Long, out: IntArray): Int
    @JvmStatic external fun ratParse(text: ByteArray, out: LongArray): Int
    @JvmStatic external fun ratFormat(n: Long, d: Long, buf: ByteArray?, outLen: LongArray): Int
    @JvmStatic external fun ratFromIeee754(bits: Long, out: LongArray): Int
    @JvmStatic external fun ratToQ16(n: Long, d: Long, out: IntArray): Int

    @JvmStatic external fun lpNot(a: Int, out: IntArray): Int
    @JvmStatic external fun lpAnd(a: Int, b: Int, out: IntArray): Int
    @JvmStatic external fun lpOr(a: Int, b: Int, out: IntArray): Int

    @JvmStatic external fun hexPack(base: Int, polarity: Int, phase: Int, out: IntArray): Int
    @JvmStatic external fun hexUnpack(tag: Int, out: IntArray?): Int
    @JvmStatic external fun hexAction(tag: Int, out: IntArray): Int
    @JvmStatic external fun hexPhaseNext(phase: Int, out: IntArray): Int
    @JvmStatic external fun hexStep(tag: Int, phase: Int, out: IntArray): Int
    @JvmStatic external fun hexSet(tag: Int, base: Int, polarity: Int, out: IntArray): Int
    @JvmStatic external fun hexForbid(tag: Int, out: IntArray): Int
    @JvmStatic external fun hexAnd(a: Int, b: Int, out: IntArray): Int
    @JvmStatic external fun hexOr(a: Int, b: Int, out: IntArray): Int
    @JvmStatic external fun hexNot(a: Int, out: IntArray): Int
    @JvmStatic external fun hexPolarity(pos0: Long, neg0: Long, pos1: Long, neg1: Long, stale: Int, out: IntArray): Int
    @JvmStatic external fun hexFromLp(lp: Int, out: IntArray): Int
    @JvmStatic external fun hexToLp(base: Int, out: IntArray): Int
    @JvmStatic external fun hexName(tag: Int): String
    @JvmStatic external fun hexActionName(action: Int): String

    @JvmStatic external fun hexlogCreate(cap: Long, capacity: Long, out: LongArray): Int
    @JvmStatic external fun hexlogDestroy(log: Long)
    @JvmStatic external fun hexlogAppend(
        cap: Long, log: Long, tag: Int, reason: Int, submitter: Long, subject: ByteArray, out: LongArray,
    ): Int
    @JvmStatic external fun hexlogCount(log: Long, out: LongArray): Int
    @JvmStatic external fun hexlogHead(log: Long, out: ByteArray): Int
    @JvmStatic external fun hexlogAt(
        log: Long, index: Long, out: LongArray, subject: ByteArray, prev: ByteArray, hash: ByteArray,
    ): Int
    @JvmStatic external fun hexlogVerify(log: Long): Int

    @JvmStatic external fun rmagCreateCap(cap: Long, out: LongArray): Int
    @JvmStatic external fun rmagDestroy(rmag: Long)
    @JvmStatic external fun rmagBudget(
        cap: Long, rmag: Long, name: ByteArray, resource: Int, limitNum: Long, limitDen: Long, enforce: Int, out: LongArray,
    ): Int
    @JvmStatic external fun rmagConsume(cap: Long, rmag: Long, budget: Long, num: Long, den: Long): Int
    @JvmStatic external fun rmagRelease(cap: Long, rmag: Long, budget: Long, num: Long, den: Long): Int
    @JvmStatic external fun rmagSetLimit(cap: Long, rmag: Long, budget: Long, num: Long, den: Long): Int
    @JvmStatic external fun rmagStatus(rmag: Long, budget: Long, out: LongArray): Int

    @JvmStatic external fun sha3256(data: ByteArray, out: ByteArray): Int
    @JvmStatic external fun keccak256(data: ByteArray, out: ByteArray): Int
    @JvmStatic external fun shake256(data: ByteArray, out: ByteArray): Int

    @JvmStatic external fun secretAlloc(len: Long): Long
    @JvmStatic external fun secretFree(ptr: Long, len: Long)
    @JvmStatic external fun secretCopyIn(ptr: Long, src: ByteArray): Int
    @JvmStatic external fun secretCopyOut(ptr: Long, dst: ByteArray): Int

    @JvmStatic external fun ed25519Keygen(cap: Long, pk: ByteArray, sk: Long): Int
    @JvmStatic external fun ed25519Public(cap: Long, sk: Long, pk: ByteArray): Int
    @JvmStatic external fun ed25519Sign(cap: Long, sk: Long, msg: ByteArray, sig: ByteArray): Int
    @JvmStatic external fun ed25519Verify(cap: Long, pk: ByteArray, msg: ByteArray, sig: ByteArray): Int
}
