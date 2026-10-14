// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.fee;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The 0.08889% assurance fee, exact and integer-only:
 * {@code fee(g) = floor(g * 8889 / 10^7)}, formed in 128 bits so every
 * unsigned 64-bit gross is exact (fee(1124) = 0, fee(1125) = 1). It is
 * charged once per transaction, on the gross: never on a fee, never
 * compounded. A fee splits 50/25/15/10 into the four {@link FeeBucket}s, the
 * reserve floor taking every remainder unit so the parts sum exactly.
 *
 * <p>Amounts are unsigned 64-bit values in a Java {@code long}. Pure and
 * thread-safe; no context or capability.
 *
 * <p>Honest limits: these are arithmetic rules only. How a fee, reserve or
 * dividend pool is treated for tax, securities or consumer law is a question
 * for counsel; nothing here is a licence or a certification.
 */
public final class AssuranceFee {
    private AssuranceFee() {}

    /** The rate's numerator ({@code ZXV_FEE_NUM}). */
    public static final long NUM = 8889;
    /** The rate's denominator ({@code ZXV_FEE_DEN}); a carry is always below it. */
    public static final long DEN = 10_000_000;

    /**
     * One charge with carry.
     *
     * @param fee       the fee (unsigned)
     * @param remainder the sub-unit remainder to store for the account's next charge (below {@link #DEN})
     */
    public record Charge(long fee, long remainder) {}

    /**
     * A fee split into its four buckets.
     *
     * @param reserveFloor   50% plus every remainder unit
     * @param vbillDividend  25%
     * @param infraBounty    15%
     * @param regenCapital   10%
     */
    public record FeeSplit(long reserveFloor, long vbillDividend, long infraBounty, long regenCapital) {
        /** The part for {@code bucket}. */
        public long part(FeeBucket bucket) {
            return switch (bucket) {
                case RESERVE_FLOOR -> reserveFloor;
                case VBILL_DIVIDEND -> vbillDividend;
                case INFRA_BOUNTY -> infraBounty;
                case REGEN_CAPITAL -> regenCapital;
            };
        }
    }

    /** The fee on {@code gross} (unsigned) with no carry. */
    public static long fee(long gross) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment f = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.fee_assure, gross, f), null);
            return f.get(JAVA_LONG, 0);
        }
    }

    /**
     * The fee on {@code gross} with the account's incoming remainder. Starting
     * from 0, many small charges pay exactly what one charge of their sum
     * pays. The caller stores the returned remainder per account.
     *
     * @param remainderIn 0..{@link #DEN}-1
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for a corrupt carry ({@code >= DEN}, unsigned)
     */
    public static Charge charge(long remainderIn, long gross) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment f = a.allocate(JAVA_LONG), r = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.fee_assure_carry, remainderIn, gross, f, r), null);
            return new Charge(f.get(JAVA_LONG, 0), r.get(JAVA_LONG, 0));
        }
    }

    /** Splits {@code fee} (unsigned) into the four buckets; the parts sum to it exactly. */
    public static FeeSplit split(long fee) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment o = a.allocate(JAVA_LONG, 4);
            Ffi.check(Ffi.call(Native15.fee_split, fee, o), null);
            return new FeeSplit(o.getAtIndex(JAVA_LONG, 0), o.getAtIndex(JAVA_LONG, 1), o.getAtIndex(JAVA_LONG, 2),
                    o.getAtIndex(JAVA_LONG, 3));
        }
    }

    /** The default policy. */
    public static FeePolicy defaultPolicy() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pct = a.allocate(JAVA_INT, 4), below = a.allocate(JAVA_INT), rn = a.allocate(JAVA_LONG, 4),
                    rd = a.allocate(JAVA_LONG, 4), mn = a.allocate(JAVA_LONG), md = a.allocate(JAVA_LONG),
                    cn = a.allocate(JAVA_LONG), cd = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.fee_policy_default, pct, below, rn, rd, mn, md, cn, cd), null);
            List<Integer> p = new ArrayList<>();
            List<FeePolicy.Ratio> rates = new ArrayList<>();
            for (int i = 0; i < 4; i++) {
                p.add(pct.getAtIndex(JAVA_INT, i));
                rates.add(new FeePolicy.Ratio(rn.getAtIndex(JAVA_LONG, i), rd.getAtIndex(JAVA_LONG, i)));
            }
            return new FeePolicy(p, below.get(JAVA_INT, 0) != 0, rates,
                    new FeePolicy.Ratio(mn.get(JAVA_LONG, 0), md.get(JAVA_LONG, 0)),
                    new FeePolicy.Ratio(cn.get(JAVA_LONG, 0), cd.get(JAVA_LONG, 0)));
        }
    }
}
