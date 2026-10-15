// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

/**
 * A snapshot of one play-lane voucher ({@code zxv_vino_voucher_info}).
 *
 * <p>A voucher is one coin of the play lane: a Fibonacci ladder face (1, 2, 3, 5, 8, 13, ...)
 * issued by one issuer and held by one holder on exactly one rail. It is a snapshot: it does not
 * change when the sandbox does; read it again with {@link PlaySandbox#voucher(long)}.
 *
 * @param id     the voucher's id: positive, unique in its sandbox and never reused (unsigned 64-bit)
 * @param face   its face in play units, a Fibonacci ladder face (unsigned 64-bit)
 * @param issuer the account number of the issuer that issued it
 * @param holder the account number of the holder that holds it (or last held it, once redeemed)
 * @param rail   the rail it is on
 * @param state  whether it is still active or has been redeemed
 */
public record Voucher(long id, long face, int issuer, int holder, Rail rail, VoucherState state) {
    /**
     * Whether the voucher is still active (not redeemed).
     *
     * @return true while it can still be transferred, switched or redeemed
     */
    public boolean isActive() {
        return state == VoucherState.ACTIVE;
    }
}
