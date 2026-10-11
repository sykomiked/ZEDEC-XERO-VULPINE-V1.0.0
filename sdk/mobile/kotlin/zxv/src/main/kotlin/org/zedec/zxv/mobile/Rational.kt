// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.math.BigDecimal
import java.math.BigInteger
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_RANGE
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr

/**
 * An exact rational `numerator / denominator` with 64-bit parts: the
 * kernel's `rat_t`. Immutable; always reduced with a positive denominator,
 * so `==` is value equality (`2/4 == 1/2`).
 *
 * Arithmetic runs in libzxv and never rounds: a result that does not fit
 * raises [ZxvUsageException] (`ZXV_E_RANGE`) instead of wrapping.
 *
 * No method takes a `Float` or `Double` except [fromDoubleExact], whose name
 * says what it does: `0.1` becomes `3602879701896397/36028797018963968`.
 * To get one tenth, write `Rational.parse("0.1")` or `Rational.of(1, 10)`.
 *
 * ```
 * val a = Rational.parse("0.1"); val b = Rational.parse("0.2")
 * check(a + b == Rational.of(3, 10))   // exactly
 * ```
 */
class Rational private constructor(
    /** The reduced numerator (carries the sign). */
    val numerator: Long,
    /** The reduced denominator, always > 0. */
    val denominator: Long,
) : Comparable<Rational> {

    companion object {
        /** 0/1. */
        @JvmField val ZERO = Rational(0, 1)

        /** 1/1. */
        @JvmField val ONE = Rational(1, 1)

        /**
         * The reduced rational `numerator / denominator`.
         *
         * @throws ZxvUsageException `ZXV_E_ARG` for a zero denominator, `ZXV_E_RANGE` for `Long.MIN_VALUE`
         */
        @JvmStatic
        @JvmOverloads
        fun of(numerator: Long, denominator: Long = 1): Rational {
            val out = LongArray(2)
            Status.check(Jni.ratNormalize(numerator, denominator, out), null)
            return Rational(out[0], out[1])
        }

        /**
         * Parses exact text: `"12.75"`, `"-0.1"`, `"3/4"`, `"42"`. Exponents,
         * spaces, `"NaN"` and a leading `"."` are refused (`ZXV_E_FORMAT`).
         */
        @JvmStatic
        fun parse(text: String): Rational {
            val out = LongArray(2)
            Status.check(Jni.ratParse(cstr(text, "text"), out), null)
            return Rational(out[0], out[1])
        }

        /** The exact value of a [BigDecimal] (always a terminating decimal, so this never rounds). */
        @JvmStatic
        fun from(value: BigDecimal): Rational {
            var n = value.unscaledValue()
            var d = BigInteger.ONE
            val scale = value.scale()
            if (scale > 0) d = BigInteger.TEN.pow(scale) else if (scale < 0) n = n.multiply(BigInteger.TEN.pow(-scale))
            return from(n, d)
        }

        /** The exact rational of two big integers, reduced; `ZXV_E_RANGE` if the parts do not fit in 64 bits. */
        @JvmStatic
        fun from(numerator: BigInteger, denominator: BigInteger): Rational {
            if (denominator.signum() == 0) throw Status.usage(E_ARG, "zero denominator")
            var n = numerator
            var d = denominator
            val g = n.gcd(d)
            if (g.signum() != 0) {
                n /= g
                d /= g
            }
            if (d.signum() < 0) {
                n = n.negate()
                d = d.negate()
            }
            if (n.bitLength() > 63 || d.bitLength() > 63) {
                throw Status.usage(E_RANGE, "value $n/$d does not fit in 64-bit parts")
            }
            return of(n.toLong(), d.toLong())
        }

        /**
         * THE FLOAT BOUNDARY: the exact value of an IEEE-754 double. Every
         * finite double is exactly `m * 2^e`; this returns that value, so
         * binary drift is visible. Prefer [parse] or [from] for decimals.
         *
         * @throws FloatRejectedException for NaN and infinities
         * @throws ZxvUsageException `ZXV_E_RANGE` if the exact form needs more than 63 bits
         */
        @JvmStatic
        fun fromDoubleExact(value: Double): Rational = fromIeee754Bits(java.lang.Double.doubleToRawLongBits(value))

        /** The exact rational of raw IEEE-754 binary64 bits (from a file or the wire). */
        @JvmStatic
        fun fromIeee754Bits(bits: Long): Rational {
            val out = LongArray(2)
            Status.check(Jni.ratFromIeee754(bits, out), null)
            return Rational(out[0], out[1])
        }

        private inline fun bin(a: Rational, b: Rational, f: (Long, Long, Long, Long, LongArray) -> Int): Rational {
            val out = LongArray(2)
            Status.check(f(a.numerator, a.denominator, b.numerator, b.denominator, out), null)
            return Rational(out[0], out[1])
        }
    }

    /** -1, 0 or 1. */
    val signum: Int get() = java.lang.Long.signum(numerator)

    /** True if the denominator is 1. */
    val isInteger: Boolean get() = denominator == 1L

    operator fun plus(other: Rational): Rational = bin(this, other, Jni::ratAdd)
    operator fun minus(other: Rational): Rational = bin(this, other, Jni::ratSub)
    operator fun times(other: Rational): Rational = bin(this, other, Jni::ratMul)

    /** Exact division; `ZXV_E_ARG` for division by zero. */
    operator fun div(other: Rational): Rational = bin(this, other, Jni::ratDiv)
    operator fun unaryMinus(): Rational = ZERO - this

    /** Absolute value. */
    fun abs(): Rational = if (signum < 0) -this else this

    override fun compareTo(other: Rational): Int {
        val out = IntArray(1)
        Status.check(Jni.ratCmp(numerator, denominator, other.numerator, other.denominator, out), null)
        return out[0]
    }

    /** Q16.16 (value * 65536, rounded as libzxv rounds); `ZXV_E_RANGE` outside the int32 range. */
    fun toQ16(): Int {
        val out = IntArray(1)
        Status.check(Jni.ratToQ16(numerator, denominator, out), null)
        return out[0]
    }

    /** The exact [BigDecimal], or `ArithmeticException` if the decimal does not terminate (1/3). */
    fun toBigDecimal(): BigDecimal = BigDecimal(numerator).divide(BigDecimal(denominator))

    /** `"n/d"` (or `"n"` for an integer). */
    fun toFractionString(): String = if (denominator == 1L) "$numerator" else "$numerator/$denominator"

    /** libzxv's canonical text (`zxv_rat_format`): a terminating decimal such as `"0.75"`, else `"1/3"`. */
    override fun toString(): String = Status.checkedText(null) { buf, len -> Jni.ratFormat(numerator, denominator, buf, len) }

    override fun equals(other: Any?): Boolean =
        other is Rational && other.numerator == numerator && other.denominator == denominator

    override fun hashCode(): Int = 31 * numerator.hashCode() + denominator.hashCode()
}
