// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.MemoryLayout;
import java.lang.invoke.MethodHandle;

/**
 * Downcall handles for the ABI 1.6 group of {@code zxv_api.h}: vino (Vino
 * Embed: the play lane, payment requests, receipts and the daily meter).
 * They share {@link Native}'s symbol lookup and version gate. Internal: the
 * public API in {@code org.zedec.zxv.vino} never exposes these.
 *
 * <p>Layouts follow {@link Native}: {@code I} is int32 / uint32, {@code L}
 * is int64 / uint64 / size_t and {@code A} is a pointer.
 */
public final class Native16 {
    private Native16() {}

    private static final MemoryLayout I = JAVA_INT;
    private static final MemoryLayout L = JAVA_LONG;
    private static final MemoryLayout A = ADDRESS;

    private static MethodHandle fn(String name, MemoryLayout ret, MemoryLayout... args) {
        return Native.fn(name, ret, args);
    }

    // ---- vino: play lane (cap, play, ...)
    /** {@code zxv_vino_play_create(cap, out)}. */
    public static final MethodHandle vino_play_create = fn("zxv_vino_play_create", I, A, A);
    /** {@code zxv_vino_play_destroy(p)}. */
    public static final MethodHandle vino_play_destroy = fn("zxv_vino_play_destroy", null, A);
    /** {@code zxv_vino_play_open(cap, p, owner, is_issuer, out_account)}. */
    public static final MethodHandle vino_play_open = fn("zxv_vino_play_open", I, A, A, I, I, A);
    /** {@code zxv_vino_play_back(cap, p, issuer, amount)}. */
    public static final MethodHandle vino_play_back = fn("zxv_vino_play_back", I, A, A, I, L);
    /** {@code zxv_vino_play_unback(cap, p, issuer, amount)}. */
    public static final MethodHandle vino_play_unback = fn("zxv_vino_play_unback", I, A, A, I, L);
    /** {@code zxv_vino_issue(cap, p, issuer, holder, face, rail, out_id)}. */
    public static final MethodHandle vino_issue = fn("zxv_vino_issue", I, A, A, I, I, L, I, A);
    /** {@code zxv_vino_transfer(cap, p, voucher, from, to, rail)}. */
    public static final MethodHandle vino_transfer = fn("zxv_vino_transfer", I, A, A, L, I, I, I);
    /** {@code zxv_vino_set_rail(cap, p, voucher, holder, rail)}. */
    public static final MethodHandle vino_set_rail = fn("zxv_vino_set_rail", I, A, A, L, I, I);
    /** {@code zxv_vino_redeem(cap, p, voucher, holder)}. */
    public static final MethodHandle vino_redeem = fn("zxv_vino_redeem", I, A, A, L, I);
    /** {@code zxv_vino_voucher_info(cap, p, voucher, face, issuer, holder, rail, state)}. */
    public static final MethodHandle vino_voucher_info = fn("zxv_vino_voucher_info", I, A, A, L, A, A, A, A, A);
    /** {@code zxv_vino_account_info(cap, p, account, owner, is_issuer, backing, faces, count)}. */
    public static final MethodHandle vino_account_info = fn("zxv_vino_account_info", I, A, A, I, A, A, A, A, A);
    /** {@code zxv_vino_vouchers(cap, p, account, ids, cap_len, out_count)}. */
    public static final MethodHandle vino_vouchers = fn("zxv_vino_vouchers", I, A, A, I, A, L, A);
    /** {@code zxv_vino_play_check(cap, p)}. */
    public static final MethodHandle vino_play_check = fn("zxv_vino_play_check", I, A, A);

    // ---- vino: payment requests (pure)
    /** {@code zxv_vino_request_encode(lane, flags, merchant, nonce, amount, created_at, expires_at, memo, out, out_id)}. */
    public static final MethodHandle vino_request_encode = fn("zxv_vino_request_encode", I, I, I, A, A, L, L, L, A, A, A);
    /** {@code zxv_vino_request_decode(in, lane, flags, merchant, nonce, amount, created_at, expires_at, memo, out_id)}. */
    public static final MethodHandle vino_request_decode = fn("zxv_vino_request_decode", I, A, A, A, A, A, A, A, A, A, A);

    // ---- vino: receipts
    /** {@code zxv_vino_receipt_msg(request_id, wallet_pk, approved_at, out)}. */
    public static final MethodHandle vino_receipt_msg = fn("zxv_vino_receipt_msg", I, A, A, L, A);
    /** {@code zxv_vino_receipt_sign(cap, request, wallet_sk, wallet_pk, approved_at, sig)}. */
    public static final MethodHandle vino_receipt_sign = fn("zxv_vino_receipt_sign", I, A, A, A, A, L, A);
    /** {@code zxv_vino_receipt_verify(request, wallet_pk, approved_at, sig, now)}. */
    public static final MethodHandle vino_receipt_verify = fn("zxv_vino_receipt_verify", I, A, A, L, A, L);

    // ---- vino: the daily meter
    /** {@code zxv_vino_meter_create(cap, daily_cap, out)}. */
    public static final MethodHandle vino_meter_create = fn("zxv_vino_meter_create", I, A, L, A);
    /** {@code zxv_vino_meter_destroy(m)}. */
    public static final MethodHandle vino_meter_destroy = fn("zxv_vino_meter_destroy", null, A);
    /** {@code zxv_vino_meter_admit(cap, m, day, amount, out_left)}. */
    public static final MethodHandle vino_meter_admit = fn("zxv_vino_meter_admit", I, A, A, L, L, A);
}
