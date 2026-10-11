// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// An exact rational number: a 64-bit numerator over a positive 64-bit
/// denominator, always in lowest terms. This is the kernel's <c>rat_t</c>, the
/// numeric normal form of the platform, and the only way non-integer numbers
/// cross the ZXV boundary: no public SDK method takes or returns a
/// <see cref="double"/> or <see cref="float"/>.
/// </summary>
/// <remarks>
/// <para>
/// Build one from integers (<see cref="Of(long, long)"/>), from exact text
/// (<see cref="Parse(string)"/>), or from a <see cref="decimal"/>
/// (<see cref="FromDecimal(decimal)"/>, exact). A <see cref="double"/> enters
/// only through <see cref="FromDoubleExact(double)"/>, which returns the value
/// the double really holds: <c>0.1</c> becomes
/// <c>3602879701896397/36028797018963968</c>, not <c>1/10</c>.
/// </para>
/// <para>
/// Arithmetic runs in the native library and never rounds or wraps: a result
/// that does not fit in 64 bits raises <see cref="ZxvUsageException"/> with
/// <see cref="ZxvStatus.AmountOutOfRange"/>. <c>default(Rational)</c> is zero.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// Rational a = Rational.Of(1, 10), b = Rational.Parse("0.2");
/// Console.WriteLine(a + b);                 // 0.3 (exactly 3/10)
/// Console.WriteLine(Rational.Of(1, 3));     // 1/3 (never rounded)
/// </code>
/// </example>
public readonly record struct Rational : IComparable<Rational>, IComparable
{
    // Stored as denominator - 1 so that default(Rational) is 0/1.
    private readonly long _num;
    private readonly long _denMinusOne;

    private Rational(long num, long den)
    {
        _num = num;
        _denMinusOne = den - 1;
    }

    /// <summary>Gets zero.</summary>
    public static Rational Zero => default;

    /// <summary>Gets one.</summary>
    public static Rational One => new(1, 1);

    /// <summary>Gets the numerator (carries the sign).</summary>
    public long Numerator => _num;

    /// <summary>Gets the denominator (always positive).</summary>
    public long Denominator => _denMinusOne + 1;

    /// <summary>Gets a value indicating whether this is zero.</summary>
    public bool IsZero => _num == 0;

    /// <summary>Gets a value indicating whether this is a whole number.</summary>
    public bool IsInteger => Denominator == 1;

    /// <summary>Gets -1, 0 or +1.</summary>
    public int Sign => Math.Sign(_num);

    /// <summary>Creates <paramref name="numerator"/>/<paramref name="denominator"/> in lowest terms.</summary>
    /// <param name="numerator">The numerator.</param>
    /// <param name="denominator">The denominator (non-zero; a negative one moves the sign to the numerator).</param>
    /// <returns>The reduced rational.</returns>
    /// <exception cref="DivideByZeroException"><paramref name="denominator"/> is zero.</exception>
    /// <exception cref="ZxvUsageException"><see cref="long.MinValue"/> was passed (its negation is not a 64-bit value).</exception>
    public static Rational Of(long numerator, long denominator = 1)
    {
        if (denominator == 0)
        {
            throw new DivideByZeroException("A rational's denominator must not be zero.");
        }

        if (denominator == 1 && numerator != long.MinValue)
        {
            return new Rational(numerator, 1);
        }

        NativeCall.Check(NativeMethods.RatNormalize(numerator, denominator, out long n, out long d), "Rational.Of");
        return new Rational(n, d);
    }

    /// <summary>
    /// Parses exact text: <c>"12.75"</c>, <c>"-0.1"</c>, <c>"3/4"</c> or <c>"42"</c>.
    /// Exponents, spaces, <c>"NaN"</c> and a leading <c>"."</c> are refused.
    /// </summary>
    /// <param name="text">The text.</param>
    /// <returns>The exact value.</returns>
    /// <exception cref="ZxvUsageException">
    /// <see cref="ZxvStatus.InvalidFormat"/> for malformed text,
    /// <see cref="ZxvStatus.AmountOutOfRange"/> if it does not fit,
    /// <see cref="ZxvStatus.InvalidArgument"/> for a zero denominator.
    /// </exception>
    public static Rational Parse(string text)
    {
        Guard.Text(text, nameof(text));
        NativeCall.Check(NativeMethods.RatParse(text, out long n, out long d), "Rational.Parse");
        return new Rational(n, d);
    }

    /// <summary>Tries to parse exact text (see <see cref="Parse(string)"/>).</summary>
    /// <param name="text">The text.</param>
    /// <param name="value">The value, when parsed.</param>
    /// <returns><see langword="true"/> on success.</returns>
    public static bool TryParse([NotNullWhen(true)] string? text, out Rational value)
    {
        value = default;
        if (string.IsNullOrEmpty(text) || text.Contains('\0', StringComparison.Ordinal))
        {
            return false;
        }

        if (NativeMethods.RatParse(text, out long n, out long d) != (int)ZxvStatus.Ok)
        {
            return false;
        }

        value = new Rational(n, d);
        return true;
    }

    /// <summary>
    /// Converts a <see cref="decimal"/> exactly (a decimal is an integer over a
    /// power of ten, so it always has an exact rational form).
    /// </summary>
    /// <param name="value">The decimal, e.g. <c>12.75m</c>.</param>
    /// <returns>The exact value.</returns>
    /// <exception cref="ZxvUsageException">The value needs more than 64 bits (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static Rational FromDecimal(decimal value) =>
        Parse(value.ToString(CultureInfo.InvariantCulture));

    /// <summary>
    /// THE FLOAT BOUNDARY. Returns the exact value a finite <see cref="double"/>
    /// holds (every finite double is m * 2^e). <c>0.1</c> maps to
    /// <c>3602879701896397/36028797018963968</c>: the binary drift is made
    /// visible instead of hidden. Prefer <see cref="Parse(string)"/> or
    /// <see cref="FromDecimal(decimal)"/> for values written in decimal.
    /// </summary>
    /// <param name="value">A finite double.</param>
    /// <returns>Its exact rational value.</returns>
    /// <exception cref="FloatRejectedException">NaN or an infinity.</exception>
    /// <exception cref="ZxvUsageException">The exact form needs more than 63 bits, e.g. <c>1e-300</c> (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static Rational FromDoubleExact(double value)
    {
        ulong bits = unchecked((ulong)BitConverter.DoubleToInt64Bits(value));
        NativeCall.Check(NativeMethods.RatFromIeee754(bits, out long n, out long d), "Rational.FromDoubleExact");
        return new Rational(n, d);
    }

    /// <summary>
    /// Converts raw IEEE-754 binary64 bits (as from
    /// <see cref="BitConverter.DoubleToInt64Bits(double)"/>) exactly. Same rules as
    /// <see cref="FromDoubleExact(double)"/>.
    /// </summary>
    /// <param name="bits">The raw bits.</param>
    /// <returns>The exact value.</returns>
    public static Rational FromIeee754Bits(ulong bits)
    {
        NativeCall.Check(NativeMethods.RatFromIeee754(bits, out long n, out long d), "Rational.FromIeee754Bits");
        return new Rational(n, d);
    }

    /// <summary>Converts a whole number.</summary>
    /// <param name="value">The integer.</param>
    /// <returns>The rational <paramref name="value"/>/1.</returns>
    public static Rational FromInt64(long value) => Of(value);

    /// <summary>Converts a whole number implicitly (integers are exact; there is no conversion from floating point).</summary>
    /// <param name="value">The integer.</param>
    public static implicit operator Rational(long value) => Of(value);

    /// <summary>Returns the sum.</summary>
    /// <param name="other">The other operand.</param>
    /// <returns>this + other, exact.</returns>
    public Rational Add(Rational other)
    {
        NativeCall.Check(NativeMethods.RatAdd(Numerator, Denominator, other.Numerator, other.Denominator, out long n, out long d), "Rational.Add");
        return new Rational(n, d);
    }

    /// <summary>Returns the difference.</summary>
    /// <param name="other">The other operand.</param>
    /// <returns>this - other, exact.</returns>
    public Rational Subtract(Rational other)
    {
        NativeCall.Check(NativeMethods.RatSub(Numerator, Denominator, other.Numerator, other.Denominator, out long n, out long d), "Rational.Subtract");
        return new Rational(n, d);
    }

    /// <summary>Returns the product.</summary>
    /// <param name="other">The other operand.</param>
    /// <returns>this * other, exact.</returns>
    public Rational Multiply(Rational other)
    {
        NativeCall.Check(NativeMethods.RatMul(Numerator, Denominator, other.Numerator, other.Denominator, out long n, out long d), "Rational.Multiply");
        return new Rational(n, d);
    }

    /// <summary>Returns the quotient.</summary>
    /// <param name="other">The divisor.</param>
    /// <returns>this / other, exact.</returns>
    /// <exception cref="DivideByZeroException"><paramref name="other"/> is zero.</exception>
    public Rational Divide(Rational other)
    {
        if (other.IsZero)
        {
            throw new DivideByZeroException();
        }

        NativeCall.Check(NativeMethods.RatDiv(Numerator, Denominator, other.Numerator, other.Denominator, out long n, out long d), "Rational.Divide");
        return new Rational(n, d);
    }

    /// <summary>Returns the negation.</summary>
    /// <returns>-this.</returns>
    public Rational Negate() => new(checked(-_num), Denominator);

    /// <summary>Returns the absolute value.</summary>
    /// <returns>|this|.</returns>
    public Rational Abs() => _num < 0 ? Negate() : this;

    /// <summary>Compares exactly (no epsilon).</summary>
    /// <param name="other">The other value.</param>
    /// <returns>-1, 0 or +1.</returns>
    public int CompareTo(Rational other)
    {
        NativeCall.Check(NativeMethods.RatCmp(Numerator, Denominator, other.Numerator, other.Denominator, out int c), "Rational.CompareTo");
        return c;
    }

    /// <inheritdoc/>
    int IComparable.CompareTo(object? obj) => obj switch
    {
        null => 1,
        Rational r => CompareTo(r),
        _ => throw new ArgumentException("Object must be a Rational.", nameof(obj)),
    };

    /// <summary>
    /// Rounds to Q16.16 fixed point (round half away from zero), the form the
    /// engine takes its sampling parameters in.
    /// </summary>
    /// <returns>round(this * 65536).</returns>
    /// <exception cref="ZxvUsageException">Outside the int32 range (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public int ToQ16()
    {
        NativeCall.Check(NativeMethods.RatToQ16(Numerator, Denominator, out int q), "Rational.ToQ16");
        return q;
    }

    /// <summary>
    /// Formats exactly: a terminating decimal when one exists (<c>"12.75"</c>),
    /// otherwise <c>"n/d"</c> (<c>"1/3"</c>). Never rounds. <see cref="Parse(string)"/> reads it back.
    /// </summary>
    /// <returns>The exact text.</returns>
    public override unsafe string ToString()
    {
        long n = Numerator, d = Denominator;
        return NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.RatFormat(n, d, b, c, out l), "Rational.ToString");
    }

    /// <summary>Adds.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>The exact sum.</returns>
    public static Rational operator +(Rational left, Rational right) => left.Add(right);

    /// <summary>Subtracts.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>The exact difference.</returns>
    public static Rational operator -(Rational left, Rational right) => left.Subtract(right);

    /// <summary>Multiplies.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>The exact product.</returns>
    public static Rational operator *(Rational left, Rational right) => left.Multiply(right);

    /// <summary>Divides.</summary>
    /// <param name="left">Dividend.</param>
    /// <param name="right">Divisor.</param>
    /// <returns>The exact quotient.</returns>
    public static Rational operator /(Rational left, Rational right) => left.Divide(right);

    /// <summary>Negates.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The negation.</returns>
    public static Rational operator -(Rational value) => value.Negate();

    /// <summary>Compares exactly.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether left &lt; right.</returns>
    public static bool operator <(Rational left, Rational right) => left.CompareTo(right) < 0;

    /// <summary>Compares exactly.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether left &gt; right.</returns>
    public static bool operator >(Rational left, Rational right) => left.CompareTo(right) > 0;

    /// <summary>Compares exactly.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether left &lt;= right.</returns>
    public static bool operator <=(Rational left, Rational right) => left.CompareTo(right) <= 0;

    /// <summary>Compares exactly.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether left &gt;= right.</returns>
    public static bool operator >=(Rational left, Rational right) => left.CompareTo(right) >= 0;
}
