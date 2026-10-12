// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.treaty;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * Exact PIC S9(18) arithmetic, the way COBOL does it with {@code ON SIZE ERROR}:
 * every operand and every exact result must lie in
 * [-{@link #MAX}, {@link #MAX}], otherwise the call is a size error
 * ({@code ZXV_E_RANGE}, thrown as {@link org.zedec.zxv.ZxvUsageException}) and
 * nothing is returned. These are the rules treaty amounts follow. Pure: no
 * context, no capability.
 *
 * <pre>{@code
 * Pic18.add(999_999_999_999_999_998L, 1);   // 999999999999999999
 * Pic18.add(Pic18.MAX, 1);                   // ZxvUsageException (ZXV_E_RANGE)
 * Pic18.divide(-7, 2);                       // Division[quotient=-3, remainder=-1]
 * }</pre>
 */
public final class Pic18 {
    private Pic18() {}

    /** The largest PIC S9(18) magnitude, 999,999,999,999,999,999 ({@code ZXV_TREATY_PIC18_MAX}). */
    public static final long MAX = 999_999_999_999_999_999L;

    /**
     * A quotient and remainder (COBOL {@code DIVIDE a BY b GIVING q REMAINDER r}).
     *
     * @param quotient  truncated toward zero
     * @param remainder carries the dividend's sign
     */
    public record Division(long quotient, long remainder) {}

    /**
     * {@code a + b}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE on a size error
     */
    public static long add(long a, long b) {
        return op(Native15.treaty_pic_add, a, b);
    }

    /**
     * {@code a - b}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE on a size error
     */
    public static long subtract(long a, long b) {
        return op(Native15.treaty_pic_sub, a, b);
    }

    /**
     * {@code a * b}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE on a size error
     */
    public static long multiply(long a, long b) {
        return op(Native15.treaty_pic_mul, a, b);
    }

    /**
     * {@code a / b}, truncated toward zero, with the remainder taking the
     * dividend's sign: -7 / 2 = -3 rem -1, and 7 / -2 = -3 rem 1.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE on a size error or division by zero
     */
    public static Division divide(long a, long b) {
        try (Arena ar = Arena.ofConfined()) {
            MemorySegment q = ar.allocate(JAVA_LONG), r = ar.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.treaty_pic_div, a, b, q, r), null);
            return new Division(q.get(JAVA_LONG, 0), r.get(JAVA_LONG, 0));
        }
    }

    private static long op(MethodHandle mh, long a, long b) {
        try (Arena ar = Arena.ofConfined()) {
            MemorySegment out = ar.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(mh, a, b, out), null);
            return out.get(JAVA_LONG, 0);
        }
    }
}
