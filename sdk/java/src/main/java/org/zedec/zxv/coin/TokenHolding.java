// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * A token holding: how many tokens are held on each of the 64 tiers. Its
 * value is the sum of count times face. Immutable: every operation returns a
 * new holding and leaves this one unchanged, so a refused operation (which
 * changes nothing in C either) leaves nothing half done.
 *
 * <p>Counts and values are unsigned 64-bit values in a Java {@code long}.
 * The CANONICAL form of a value at octane o puts floor(value / face(o))
 * tokens on tier o and the rest in Zeckendorf form below it; every value has
 * exactly one canonical form per octane. Pure and thread-safe.
 */
public final class TokenHolding {
    private final long[] counts;

    private TokenHolding(long[] counts) {
        this.counts = counts;
    }

    /** The empty holding. */
    public static TokenHolding empty() {
        return new TokenHolding(new long[Tokens.TIERS]);
    }

    /**
     * A holding from per-tier counts (copied).
     *
     * @param counts exactly {@link Tokens#TIERS} counts, tier 0 first
     */
    public static TokenHolding of(long[] counts) {
        Buf.nonNull("counts", counts);
        if (counts.length != Tokens.TIERS) throw Errors.usage(-1, "counts must have 64 entries");
        return new TokenHolding(counts.clone());
    }

    /**
     * The canonical form of {@code value} (unsigned) at {@code octane}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for octane 64 and up
     */
    public static TokenHolding canonical(long value, int octane) {
        Buf.index("octane", octane);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_LONG, Tokens.TIERS);
            Ffi.check(Ffi.call(Native15.token_canonical, value, octane, c), null);
            return new TokenHolding(c.toArray(JAVA_LONG));
        }
    }

    /** The count on {@code tier} (unsigned). */
    public long count(int tier) {
        return counts[tier];
    }

    /** A copy of the 64 counts, tier 0 first. */
    public long[] counts() {
        return counts.clone();
    }

    /**
     * A copy of this holding with {@code count} tokens on {@code tier}.
     *
     * @param tier  0..63
     * @param count unsigned
     */
    public TokenHolding with(int tier, long count) {
        if (tier < 0 || tier >= Tokens.TIERS) throw Errors.usage(org.zedec.zxv.ZxvStatus.E_RANGE, "tier " + tier);
        long[] c = counts.clone();
        c[tier] = count;
        return new TokenHolding(c);
    }

    /**
     * The value: the sum of count times face (unsigned).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when it passes 64 bits
     */
    public long value() {
        return valueAndPieces()[0];
    }

    /**
     * The number of pieces: the sum of counts (unsigned).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when the value passes 64 bits
     */
    public long pieces() {
        return valueAndPieces()[1];
    }

    private long[] valueAndPieces() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_LONG), p = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.token_value, seg(a), v, p), null);
            return new long[] {v.get(JAVA_LONG, 0), p.get(JAVA_LONG, 0)};
        }
    }

    /** Whether this is the canonical form of its value at {@code octane} (false when a denser tier is used). */
    public boolean isCanonical(int octane) {
        Buf.index("octane", octane);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment r = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.token_is_canonical, seg(a), octane, r), null);
            return r.get(JAVA_INT, 0) != 0;
        }
    }

    /** The canonical form of the same value at {@code octane}; the value never changes. */
    public TokenHolding compress(int octane) {
        Buf.index("octane", octane);
        return mutate(Native15.token_compress, octane);
    }

    /**
     * Fuses one tier-k and one tier-(k+1) token into one tier-(k+2) token.
     *
     * @throws org.zedec.zxv.InsufficientFundsException without a token on both tiers
     * @throws org.zedec.zxv.ZxvRefusedException        ZXV_E_STATE when tier k+2 is denser than {@code octane}
     * @throws org.zedec.zxv.ZxvUsageException          ZXV_E_RANGE for a tier past 61 or a full count
     */
    public TokenHolding fuse(int octane, int tier) {
        Buf.index("octane", octane);
        Buf.index("tier", tier);
        return mutate(Native15.token_fuse, octane, tier);
    }

    /**
     * Splits one tier-k token into one tier-(k-1) and one tier-(k-2) token
     * (tier 1 becomes two plain tokens; tier 0 is atomic).
     *
     * @throws org.zedec.zxv.InsufficientFundsException without a tier-k token
     * @throws org.zedec.zxv.ZxvUsageException          ZXV_E_RANGE for tier 0, 64 and up, or a full count
     */
    public TokenHolding split(int tier) {
        Buf.index("tier", tier);
        return mutate(Native15.token_split, tier);
    }

    private TokenHolding mutate(java.lang.invoke.MethodHandle mh, Object... tail) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = seg(a);
            Object[] args = new Object[tail.length + 1];
            args[0] = c;
            System.arraycopy(tail, 0, args, 1, tail.length);
            Ffi.check(Ffi.call(mh, args), null);
            return new TokenHolding(c.toArray(JAVA_LONG));
        }
    }

    private MemorySegment seg(Arena a) {
        MemorySegment c = a.allocate(JAVA_LONG, Tokens.TIERS);
        MemorySegment.copy(counts, 0, c, JAVA_LONG, 0, Tokens.TIERS);
        return c;
    }

    /** Equal when every count is equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof TokenHolding h && Arrays.equals(counts, h.counts);
    }

    /** A hash of the counts. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(counts);
    }

    /** The non-zero tiers, as {@code tier:count} pairs. */
    @Override
    public String toString() {
        StringBuilder b = new StringBuilder("TokenHolding[");
        boolean first = true;
        for (int k = 0; k < Tokens.TIERS; k++) {
            if (counts[k] == 0) continue;
            if (!first) b.append(", ");
            b.append(k).append(':').append(Long.toUnsignedString(counts[k]));
            first = false;
        }
        return b.append(']').toString();
    }
}
