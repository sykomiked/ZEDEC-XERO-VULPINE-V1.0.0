// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.fee;

import java.util.List;

/**
 * The default fee policy ({@code zxv_fee_policy_default}). All numbers are
 * exact integers; ratios are numerator/denominator pairs (unsigned).
 *
 * @param percent      the bucket shares in percent, by {@link FeeBucket}: 50, 25, 15, 10
 * @param allowBelow   whether a contribution below the fee is allowed (false: it is raised to the fee)
 * @param creditRates  the posted VFV store-credit rate per {@link FeeUnit} for a voluntary
 *                     contribution above the fee: money 1/1, the others 0/1 (no credit until posted)
 * @param multiplier   the operator's credit multiplier, 1/1
 * @param commonsCap   the most one recipient may take of the non-money commons pool, 8/21
 */
public record FeePolicy(List<Integer> percent, boolean allowBelow, List<Ratio> creditRates, Ratio multiplier,
        Ratio commonsCap) {
    /** Copies the lists. */
    public FeePolicy {
        percent = List.copyOf(percent);
        creditRates = List.copyOf(creditRates);
    }

    /** The credit rate of one unit kind. */
    public Ratio creditRate(FeeUnit unit) {
        return creditRates.get(unit.index());
    }

    /**
     * An exact ratio of two unsigned 64-bit integers.
     *
     * @param num the numerator (unsigned)
     * @param den the denominator (unsigned)
     */
    public record Ratio(long num, long den) {
        /** {@code "num/den"}, unsigned. */
        @Override
        public String toString() {
            return Long.toUnsignedString(num) + "/" + Long.toUnsignedString(den);
        }
    }
}
