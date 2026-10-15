// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile.internal

/**
 * The native methods of the ABI 1.6 group "vino" (`jni/zxv_jni16.c`): the
 * play lane, payment requests, receipts and the daily meter. Internal.
 *
 * The conventions of [Jni] and [Jni15] hold: handles and uint64 values cross
 * as `Long` bits, uint32 values as `Long` (range-checked in C), several
 * scalar outs come back packed in one `LongArray` in the order the C function
 * lists them, and the wallet's secret key is a [org.zedec.zxv.mobile.SecretKey]
 * address, never an array. `ShimDriftTest` keeps this list and the C file in step.
 */
@Suppress("TooManyFunctions", "LongParameterList")
internal object Jni16 {
    init {
        NativeLoader.ensureLoaded()
    }

    @JvmStatic external fun vinoPlayCreate(cap: Long, out_: LongArray?): Int
    @JvmStatic external fun vinoPlayDestroy(p: Long)
    @JvmStatic external fun vinoPlayOpen(cap: Long, p: Long, owner: Long, isIssuer: Int, out_: LongArray?): Int
    @JvmStatic external fun vinoPlayBack(cap: Long, p: Long, issuer: Long, amount: Long): Int
    @JvmStatic external fun vinoPlayUnback(cap: Long, p: Long, issuer: Long, amount: Long): Int
    @JvmStatic external fun vinoIssue(cap: Long, p: Long, issuer: Long, holder: Long, face: Long, rail: Int, out_: LongArray?): Int
    @JvmStatic external fun vinoTransfer(cap: Long, p: Long, voucher: Long, from: Long, to: Long, rail: Int): Int
    @JvmStatic external fun vinoSetRail(cap: Long, p: Long, voucher: Long, holder: Long, rail: Int): Int
    @JvmStatic external fun vinoRedeem(cap: Long, p: Long, voucher: Long, holder: Long): Int

    /** out = {face, issuer, holder, rail, state}. */
    @JvmStatic external fun vinoVoucherInfo(cap: Long, p: Long, voucher: Long, out_: LongArray?): Int

    /** out = {owner, isIssuer, backing, faces, count}. */
    @JvmStatic external fun vinoAccountInfo(cap: Long, p: Long, account: Long, out_: LongArray?): Int

    /** Two-call: `ids == null` asks for the count; `outCount[0]` is written on OK and on E_BUFFER. */
    @JvmStatic external fun vinoVouchers(cap: Long, p: Long, account: Long, ids: LongArray?, outCount: LongArray?): Int
    @JvmStatic external fun vinoPlayCheck(cap: Long, p: Long): Int
    @JvmStatic external fun vinoRequestEncode(lane: Int, flags: Int, merchant: ByteArray?, nonce: ByteArray?, amount: Long, createdAt: Long, expiresAt: Long, memo: ByteArray?, out_: ByteArray?, outId: ByteArray?): Int

    /** out = {lane, flags, amount, createdAt, expiresAt}; memo is 65 bytes, NUL-terminated. */
    @JvmStatic external fun vinoRequestDecode(in_: ByteArray?, out_: LongArray?, merchant: ByteArray?, nonce: ByteArray?, memo: ByteArray?, id: ByteArray?): Int
    @JvmStatic external fun vinoReceiptMsg(requestId: ByteArray?, walletPk: ByteArray?, approvedAt: Long, out_: ByteArray?): Int
    @JvmStatic external fun vinoReceiptSign(cap: Long, request: ByteArray?, walletSk: Long, walletPk: ByteArray?, approvedAt: Long, sig: ByteArray?): Int
    @JvmStatic external fun vinoReceiptVerify(request: ByteArray?, walletPk: ByteArray?, approvedAt: Long, sig: ByteArray?, now: Long): Int
    @JvmStatic external fun vinoMeterCreate(cap: Long, dailyCap: Long, out_: LongArray?): Int
    @JvmStatic external fun vinoMeterDestroy(m: Long)

    /** `outLeft[0]` is written on OK and on E_LIMIT. */
    @JvmStatic external fun vinoMeterAdmit(cap: Long, m: Long, day: Long, amount: Long, outLeft: LongArray?): Int
}
