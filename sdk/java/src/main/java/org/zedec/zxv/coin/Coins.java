// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The VFV coin ladder: Fibonacci faces only. Rung r (1..{@link #RUNGS}) has
 * face F(r+1), so the faces are 1, 2, 3, 5, 8, 13, ... up to
 * {@link #TOP_FACE}; rung 0 is the ceremonial V0 note, worth nothing. Any
 * other face (4, 6, 100, ...) is not a coin and is refused with
 * {@link NotADenominationException}. To pay an arbitrary amount, split it.
 *
 * <p>Amounts are unsigned 64-bit values carried in a Java {@code long}; use
 * {@link Long#toUnsignedString(long)} to print them. Every function here is
 * pure and thread-safe and needs no capability.
 *
 * <p>Honest limits: minting VFV coins is not in this ABI (the kernel's mint
 * path is not integer-only yet). {@link org.zedec.zxv.Permission#MINT} is
 * used only by {@link TokenWallet}.
 */
public final class Coins {
    private Coins() {}

    /** Rungs that carry value ({@code ZXV_COIN_RUNGS}). */
    public static final int RUNGS = 45;
    /** Face of rung 45, F(46) ({@code ZXV_COIN_TOP_FACE}). */
    public static final long TOP_FACE = 1836311903L;
    /** Most coins {@link #split(long)} returns as an array; larger splits are only counted. */
    public static final int SPLIT_ARRAY_MAX = Integer.MAX_VALUE - 8;

    /**
     * The face of rung {@code rung} (0..{@link #RUNGS}); rung 0 is the V0 note, face 0.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when the rung is past 45
     */
    public static long face(int rung) {
        Buf.index("rung", rung);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment f = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.coin_face, rung, f), null);
            return f.get(JAVA_LONG, 0);
        }
    }

    /**
     * The rung (1..{@link #RUNGS}) whose face is exactly {@code face}.
     *
     * @throws NotADenominationException for 0 or any face not on the ladder
     */
    public static int rung(long face) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment r = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.coin_rung, face, r), null);
            return r.get(JAVA_INT, 0);
        }
    }

    /**
     * The number of coins in the split of {@code amount} (unsigned).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when the split needs more than 4,294,967,294 coins
     */
    public static long coinCount(long amount) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native15.coin_split, amount, MemorySegment.NULL, 0L, n);
            if (st != Ffi.E_BUFFER) Ffi.check(st, null);
            return n.get(JAVA_LONG, 0);
        }
    }

    /**
     * The coins for a whole {@code amount} (unsigned): its unique fewest-coins
     * (Zeckendorf) split, as rungs, largest first. Amounts at or above the top
     * face take as many top coins as fit first. 0 is an empty split.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when the split has more coins than a
     *         Java array holds (about 3.9e18 and up); use {@link #coinCount(long)} for those
     */
    public static int[] split(long amount) {
        long count = coinCount(amount);
        if (count > SPLIT_ARRAY_MAX) {
            throw Errors.usage(org.zedec.zxv.ZxvStatus.E_RANGE, "split of " + Long.toUnsignedString(amount) + " has " + count
                    + " coins, too many for an array; use coinCount");
        }
        if (count == 0) return new int[0];
        try (Arena a = Arena.ofConfined()) {
            MemorySegment rungs = a.allocate(JAVA_INT, count), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.coin_split, amount, rungs, count, n), null);
            return rungs.asSlice(0, n.get(JAVA_LONG, 0) * 4).toArray(JAVA_INT);
        }
    }
}
