// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Globalization;

namespace Zxv;

/// <summary>
/// An exact amount of money: a signed count of minor units (cents, kobo,
/// pesewas, centimes) in one currency with a fixed number of decimal places.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="MinorUnits"/> is the only value the platform ever stores or
/// computes with. <see cref="ToDecimal"/> and <see cref="ToString()"/> exist for
/// display. <see cref="FromDecimal"/> is exact: it refuses a value that has
/// more decimal places than the currency allows instead of rounding.
/// </para>
/// <para>
/// The platform bounds every amount and balance to
/// ±<see cref="MaxMinorUnits"/> (2^53 - 1) so that it is exact in every kernel
/// number representation.
/// </para>
/// </remarks>
/// <param name="MinorUnits">Signed amount in minor units.</param>
/// <param name="Currency">ISO 4217 alpha-3 code (or a platform-private unit code such as <c>VFV</c>).</param>
/// <param name="Scale">Number of decimal places (the currency's ISO 4217 minor unit).</param>
public readonly record struct Money(long MinorUnits, string Currency, byte Scale)
{
    /// <summary>The largest magnitude accepted by the platform, 2^53 - 1 minor units.</summary>
    public const long MaxMinorUnits = 9_007_199_254_740_991L;

    private static readonly decimal[] Pow10 = { 1m, 10m, 100m, 1000m, 10000m, 100000m, 1000000m, 10000000m, 100000000m };

    /// <summary>Gets a value indicating whether the amount is zero.</summary>
    public bool IsZero => MinorUnits == 0;

    /// <summary>Gets a value indicating whether the amount is below zero.</summary>
    public bool IsNegative => MinorUnits < 0;

    /// <summary>Creates a zero amount.</summary>
    /// <param name="currency">The currency.</param>
    /// <returns>Zero in <paramref name="currency"/>.</returns>
    public static Money Zero(Currencies.Currency currency)
    {
        ArgumentNullException.ThrowIfNull(currency);
        return new Money(0, currency.Code, currency.RequireMinorUnits());
    }

    /// <summary>Creates an amount from minor units in a known currency.</summary>
    /// <param name="minorUnits">Signed minor units.</param>
    /// <param name="currency">The currency.</param>
    /// <returns>The amount.</returns>
    public static Money FromMinorUnits(long minorUnits, Currencies.Currency currency)
    {
        ArgumentNullException.ThrowIfNull(currency);
        return new Money(minorUnits, currency.Code, currency.RequireMinorUnits());
    }

    /// <summary>
    /// Converts a decimal major-unit amount (e.g. <c>2500.50m</c> NGN) to exact
    /// minor units. Throws when the value cannot be represented exactly.
    /// </summary>
    /// <param name="amount">Major-unit amount.</param>
    /// <param name="currency">The currency.</param>
    /// <returns>The exact amount.</returns>
    /// <exception cref="ArgumentException">The amount has more decimal places than the currency allows.</exception>
    /// <exception cref="ArgumentOutOfRangeException">The amount exceeds <see cref="MaxMinorUnits"/>.</exception>
    public static Money FromDecimal(decimal amount, Currencies.Currency currency)
    {
        ArgumentNullException.ThrowIfNull(currency);
        byte scale = currency.RequireMinorUnits();
        decimal scaled = amount * Pow10[scale];
        if (scaled != decimal.Truncate(scaled))
        {
            throw new ArgumentException(
                $"{amount.ToString(CultureInfo.InvariantCulture)} has more than {scale} decimal places for {currency.Code}; the SDK never rounds.",
                nameof(amount));
        }

        if (scaled > MaxMinorUnits || scaled < -MaxMinorUnits)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), amount, "Amount exceeds the exact range of 2^53 - 1 minor units.");
        }

        return new Money((long)scaled, currency.Code, scale);
    }

    /// <summary>Returns the amount in major units, for display only.</summary>
    /// <returns>The decimal value, e.g. <c>2500.50</c>.</returns>
    public decimal ToDecimal() => MinorUnits / Pow10[Scale];

    /// <summary>Formats as <c>NGN 2500.50</c> using invariant culture.</summary>
    /// <returns>The formatted amount.</returns>
    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"{Currency} {ToDecimal().ToString("F" + Scale.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture)}");

    /// <summary>Formats the amount for a culture (digits and separators only; no rounding).</summary>
    /// <param name="provider">The format provider, e.g. <c>CultureInfo.GetCultureInfo("fr-SN")</c>.</param>
    /// <returns>The formatted amount with the ISO code.</returns>
    public string ToString(IFormatProvider? provider) =>
        string.Create(provider, $"{Currency} {ToDecimal().ToString("N" + Scale.ToString(CultureInfo.InvariantCulture), provider)}");

    /// <summary>Returns the negated amount.</summary>
    /// <returns>The negation.</returns>
    public Money Negate() => this with { MinorUnits = checked(-MinorUnits) };

    /// <summary>Adds two amounts in the same currency (checked).</summary>
    /// <param name="other">The other amount.</param>
    /// <returns>The sum.</returns>
    public Money Add(Money other)
    {
        RequireSameCurrency(other);
        return this with { MinorUnits = checked(MinorUnits + other.MinorUnits) };
    }

    /// <summary>Subtracts an amount in the same currency (checked).</summary>
    /// <param name="other">The other amount.</param>
    /// <returns>The difference.</returns>
    public Money Subtract(Money other)
    {
        RequireSameCurrency(other);
        return this with { MinorUnits = checked(MinorUnits - other.MinorUnits) };
    }

    /// <summary>Adds two amounts in the same currency.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>The sum.</returns>
    public static Money operator +(Money left, Money right) => left.Add(right);

    /// <summary>Subtracts two amounts in the same currency.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>The difference.</returns>
    public static Money operator -(Money left, Money right) => left.Subtract(right);

    /// <summary>Negates an amount.</summary>
    /// <param name="value">The amount.</param>
    /// <returns>The negation.</returns>
    public static Money operator -(Money value) => value.Negate();

    private void RequireSameCurrency(Money other)
    {
        if (!string.Equals(Currency, other.Currency, StringComparison.Ordinal) || Scale != other.Scale)
        {
            throw new InvalidOperationException($"Cannot combine {Currency} and {other.Currency}: the platform never converts implicitly.");
        }
    }
}
