// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

/**
 * A snapshot of one play-sandbox account ({@code zxv_vino_account_info}).
 *
 * <p>An account is either an issuer (it posts play backing and issues vouchers) or a holder (it
 * holds vouchers and spends or redeems them). Numbers are unsigned 64-bit values in a Java
 * {@code long}; there are no floats anywhere.
 *
 * @param account        the account number (they count up from 0 in each sandbox)
 * @param owner          the owner id given when the account was opened (unsigned 32-bit)
 * @param issuer         true for an issuer, false for a holder
 * @param backing        for an issuer, the play backing it has posted; for a holder, 0
 * @param faces          for an issuer, the sum of the faces it has outstanding; for a holder, the
 *                       sum of the faces it holds
 * @param activeVouchers how many active vouchers it has issued (issuer) or holds (holder)
 */
public record PlayAccount(int account, int owner, boolean issuer, long backing, long faces, int activeVouchers) {
    /**
     * Whether this issuer meets the 1.8x coverage rule, {@code 10 * backing >= 18 * faces}, computed
     * exactly (no rounding). Always true for a holder.
     *
     * @return true when the rule holds
     */
    public boolean isCovered() {
        if (!issuer) return true;
        java.math.BigInteger ten = java.math.BigInteger.TEN, eighteen = java.math.BigInteger.valueOf(18);
        java.math.BigInteger b = new java.math.BigInteger(Long.toUnsignedString(backing));
        java.math.BigInteger f = new java.math.BigInteger(Long.toUnsignedString(faces));
        return b.multiply(ten).compareTo(f.multiply(eighteen)) >= 0;
    }
}
