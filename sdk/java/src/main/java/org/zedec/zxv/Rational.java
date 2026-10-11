// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.math.BigDecimal;
import java.math.BigInteger;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * An exact rational number {@code numerator / denominator} with 64-bit parts:
 * the kernel's {@code rat_t}, the one numeric normal form of ZXV. Immutable;
 * always reduced, with a positive denominator, so {@link #equals} is value
 * equality ({@code 2/4} equals {@code 1/2}).
 *
 * <p>Arithmetic runs in libzxv and never rounds: a result that does not fit
 * in 64-bit parts raises {@link ZxvUsageException} ({@code ZXV_E_RANGE})
 * instead of wrapping or losing precision.
 *
 * <p>No method takes a {@code float} or {@code double} except
 * {@link #fromDoubleExact(double)}, whose name says what it does: it returns
 * the exact value of the IEEE-754 double, so {@code 0.1} becomes
 * {@code 3602879701896397/36028797018963968}, not {@code 1/10}. To get
 * {@code 1/10}, write {@code Rational.parse("0.1")} or {@code Rational.of(1, 10)}.
 *
 * <pre>{@code
 * Rational a = Rational.parse("0.1"), b = Rational.parse("0.2");
 * assert a.add(b).equals(Rational.of(3, 10));   // exactly
 * }</pre>
 */
public final class Rational implements Comparable<Rational> {
    /** Zero, {@code 0/1}. */
    public static final Rational ZERO = new Rational(0, 1);
    /** One, {@code 1/1}. */
    public static final Rational ONE = new Rational(1, 1);

    private final long num;
    private final long den;

    private Rational(long num, long den) {
        this.num = num;
        this.den = den;
    }

    /**
     * The reduced rational {@code numerator / denominator}.
     *
     * @throws ZxvUsageException if the denominator is zero ({@code ZXV_E_ARG}) or a
     *                           part is {@code Long.MIN_VALUE} ({@code ZXV_E_RANGE})
     */
    public static Rational of(long numerator, long denominator) {
        long[] out = new long[2];
        Ffi.check(Ffi.call(Native.rat_normalize, numerator, denominator, outNum(out), outDen(out)), null);
        return new Rational(out[0], out[1]);
    }

    /** The integer {@code value / 1}. */
    public static Rational of(long value) {
        return of(value, 1);
    }

    /**
     * Parses exact text: {@code "12.75"}, {@code "-0.1"}, {@code "3/4"} or {@code "42"}.
     * Exponents, spaces, {@code "NaN"} and a leading {@code "."} are refused.
     *
     * @throws ZxvUsageException {@code ZXV_E_FORMAT} for malformed text,
     *                           {@code ZXV_E_RANGE} if it does not fit, {@code ZXV_E_ARG} for {@code "1/0"}
     */
    public static Rational parse(String text) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_LONG), d = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.rat_parse, Ffi.cstr(a, text), n, d), null);
            return new Rational(n.get(JAVA_LONG, 0), d.get(JAVA_LONG, 0));
        }
    }

    /**
     * The exact value of a {@link BigDecimal} (which is always a terminating
     * decimal, so this never rounds).
     *
     * @throws ZxvUsageException {@code ZXV_E_RANGE} if the reduced parts do not fit in 64 bits
     */
    public static Rational from(BigDecimal value) {
        BigInteger n = value.unscaledValue();
        BigInteger d = BigInteger.ONE;
        int scale = value.scale();
        if (scale > 0) {
            d = BigInteger.TEN.pow(scale);
        } else if (scale < 0) {
            n = n.multiply(BigInteger.TEN.pow(-scale));
        }
        return from(n, d);
    }

    /**
     * The exact rational {@code numerator / denominator} of two big integers, reduced.
     *
     * @throws ZxvUsageException {@code ZXV_E_ARG} for a zero denominator,
     *                           {@code ZXV_E_RANGE} if the reduced parts do not fit in 64 bits
     */
    public static Rational from(BigInteger numerator, BigInteger denominator) {
        if (denominator.signum() == 0) throw Errors.usage(-1, "zero denominator");
        BigInteger g = numerator.gcd(denominator);
        if (g.signum() != 0) {
            numerator = numerator.divide(g);
            denominator = denominator.divide(g);
        }
        if (denominator.signum() < 0) {
            numerator = numerator.negate();
            denominator = denominator.negate();
        }
        if (numerator.bitLength() > 63 || denominator.bitLength() > 63) {
            throw Errors.usage(-8, "value " + numerator + "/" + denominator + " does not fit in 64-bit parts");
        }
        return of(numerator.longValueExact(), denominator.longValueExact());
    }

    /**
     * THE FLOAT BOUNDARY: the exact rational value of an IEEE-754 double.
     * Every finite double is exactly {@code m * 2^e}; this returns that value,
     * which makes binary drift visible ({@code 0.1} gives
     * {@code 3602879701896397/36028797018963968}). Prefer {@link #parse(String)}
     * or {@link #from(BigDecimal)} when you mean a decimal.
     *
     * @throws FloatRejectedException for NaN and infinities
     * @throws ZxvUsageException      {@code ZXV_E_RANGE} if the exact form needs more than
     *                                63 bits (for example {@code 1e-300})
     */
    public static Rational fromDoubleExact(double value) {
        long[] out = new long[2];
        long bits = Double.doubleToRawLongBits(value);
        Ffi.check(Ffi.call(Native.rat_from_ieee754, bits, outNum(out), outDen(out)), null);
        return new Rational(out[0], out[1]);
    }

    /**
     * The exact rational of raw IEEE-754 binary64 bits, for values that
     * arrive as bits (from a file or the wire) rather than as a Java double.
     */
    public static Rational fromIeee754Bits(long bits) {
        long[] out = new long[2];
        Ffi.check(Ffi.call(Native.rat_from_ieee754, bits, outNum(out), outDen(out)), null);
        return new Rational(out[0], out[1]);
    }

    /** The numerator (sign carrier). */
    public long numerator() {
        return num;
    }

    /** The denominator, always positive. */
    public long denominator() {
        return den;
    }

    /** -1, 0 or +1. */
    public int signum() {
        return Long.signum(num);
    }

    /** True if the value is a whole number. */
    public boolean isInteger() {
        return den == 1;
    }

    /** {@code this + other}, exactly. */
    public Rational add(Rational other) {
        return op(Native.rat_add, other);
    }

    /** {@code this - other}, exactly. */
    public Rational subtract(Rational other) {
        return op(Native.rat_sub, other);
    }

    /** {@code this * other}, exactly. */
    public Rational multiply(Rational other) {
        return op(Native.rat_mul, other);
    }

    /**
     * {@code this / other}, exactly.
     *
     * @throws ZxvUsageException {@code ZXV_E_ARG} when dividing by zero
     */
    public Rational divide(Rational other) {
        return op(Native.rat_div, other);
    }

    /** {@code -this}. */
    public Rational negate() {
        return num == 0 ? this : new Rational(-num, den);
    }

    /** {@code |this|}. */
    public Rational abs() {
        return num < 0 ? negate() : this;
    }

    private Rational op(MethodHandle mh, Rational o) {
        long[] out = new long[2];
        Ffi.check(Ffi.call(mh, num, den, o.num, o.den, outNum(out), outDen(out)), null);
        return new Rational(out[0], out[1]);
    }

    /** Exact comparison; no epsilon. */
    @Override
    public int compareTo(Rational other) {
        int[] out = new int[1];
        Ffi.check(Ffi.call(Native.rat_cmp, num, den, other.num, other.den, MemorySegment.ofArray(out)), null);
        return out[0];
    }

    /**
     * Rounds to Q16.16 fixed point (round half away from zero), the form the
     * engine takes for sampling parameters.
     *
     * @throws ZxvUsageException {@code ZXV_E_RANGE} outside the int32 range
     */
    public int toQ16() {
        int[] out = new int[1];
        Ffi.check(Ffi.call(Native.rat_to_q16, num, den, MemorySegment.ofArray(out)), null);
        return out[0];
    }

    /**
     * The exact {@link BigDecimal}, if the value is a terminating decimal.
     *
     * @throws ArithmeticException if it is not (for example 1/3)
     */
    public BigDecimal toBigDecimal() {
        return new BigDecimal(num).divide(new BigDecimal(den));
    }

    /** Always {@code "n/d"}, e.g. {@code "-51/4"} or {@code "42/1"}. */
    public String toFractionString() {
        return num + "/" + den;
    }

    /**
     * The exact text: a terminating decimal when one exists ({@code "12.75"}),
     * otherwise {@code "n/d"} ({@code "1/3"}). Never rounds; {@link #parse}
     * reads it back to an equal value.
     */
    @Override
    public String toString() {
        return Ffi.text(null, (buf, cap, len) -> Ffi.call(Native.rat_format, num, den, buf, cap, len));
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Rational r && r.num == num && r.den == den;
    }

    @Override
    public int hashCode() {
        return Long.hashCode(num) * 31 + Long.hashCode(den);
    }

    private static MemorySegment outNum(long[] out) {
        return MemorySegment.ofArray(out).asSlice(0, 8);
    }

    private static MemorySegment outDen(long[] out) {
        return MemorySegment.ofArray(out).asSlice(8, 8);
    }
}
